/*
 * loopback_test.cpp - GNU Radio IIO Loopback Test for OPS-SAT PRETTY
 *
 * Validates AD9361 SDR integration using internal loopback mode.
 * TX: WAV file -> resample -> FM mod -> AD9361 TX (sdr_rate e.g. 2.4 MSPS)
 * RX: AD9361 RX (sdr_rate) -> Decimating LPF (decim=12 -> 200 kSPS effective)
 *     -> sc16 I/Q + FM demod -> bandpass -> 16 kHz WAV
 *
 * Loopback mode routes TX internally to RX (no RF emission).
 *
 * Usage: loopback_test -i input.wav -o output.wav [-c config.cfg]
 */

#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <cmath>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

#include <gnuradio/top_block.h>
#include <gnuradio/blocks/vector_source.h>
#include <gnuradio/blocks/wavfile_sink.h>
#include <gnuradio/blocks/file_sink.h>
#include <gnuradio/blocks/multiply_const.h>
#include <gnuradio/blocks/head.h>
#include <gnuradio/blocks/short_to_float.h>
#include <gnuradio/blocks/float_to_complex.h>
#include <gnuradio/blocks/complex_to_float.h>
#include <gnuradio/blocks/float_to_short.h>
#include <gnuradio/blocks/complex_to_interleaved_short.h>
#include <gnuradio/analog/frequency_modulator_fc.h>
#include <gnuradio/analog/quadrature_demod_cf.h>
#include <gnuradio/filter/rational_resampler.h>
#include <gnuradio/filter/fir_filter_blk.h>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/iio/device_source.h>
#include <gnuradio/iio/device_sink.h>
#include <numeric>  // for std::gcd

#include <sched.h>

#include <iio.h>
#include <sndfile.h>

// Global flags for signal handling (only volatile sig_atomic_t is async-signal-safe)
volatile sig_atomic_t g_running = 1;
volatile sig_atomic_t g_signal_received = 0;

void signal_handler(int signum) {
    g_signal_received = signum;
    g_running = 0;
}

// Maps |x|≈1.0 (nominal fc32 magnitude) -> 8192.
// Leaves ~12 dB headroom before int16 rails (±32767).
constexpr float IQ_SCALE = 8192.0f;

// Timestamped logging helpers — return a stream reference for << chaining
static std::tm local_tm(std::time_t t) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

static std::ostream& log_info() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto tm = local_tm(t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "[%Y-%m-%d %H:%M:%S] ", &tm);
    return std::cout << buf;
}

static std::ostream& log_warning() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto tm = local_tm(t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "[%Y-%m-%d %H:%M:%S] ", &tm);
    return std::cerr << buf << "WARNING: ";
}

static std::ostream& log_error() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto tm = local_tm(t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "[%Y-%m-%d %H:%M:%S] ", &tm);
    return std::cerr << buf;
}

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Defaults are overridden by config file values (-c), then by command-line args.
struct LoopbackConfig {
    // SDR hardware
    unsigned long long frequency = 1296000000;  // overridden by config: frequency
    unsigned long sdr_rate = 2400000;           // overridden by config: sdr_rate (AD9361 hardware rate)
    int decimation = 12;                        // overridden by config: decimation (LPF decimation factor)
    unsigned long rf_bandwidth = 200000;        // overridden by config: rf_bandwidth (AD9361 analog filter)
    double fm_deviation = 5000.0;              // overridden by config: fm_deviation
    double tx_attenuation = 10.0;              // overridden by config: tx_attenuation
    double rx_gain = 50.0;                     // overridden by config: gain
    std::string uri = "local:";                // overridden by config: uri

    // Derived (set after parsing)
    unsigned long effective_rate = 200000;      // sdr_rate / decimation

    // Downlink budget
    int max_iq_mb = 20;                       // overridden by config: max_iq_mb

    // Audio
    int audio_rate = 16000;                    // overridden by config: audio_rate
    double bandpass_low = 300.0;               // overridden by config: bandpass_low
    double bandpass_high = 3400.0;             // overridden by config: bandpass_high

    // LPF
    double lpf_cutoff = 85000.0;              // overridden by config: lpf_cutoff
    double lpf_transition = 15000.0;          // overridden by config: lpf_transition

    // I/O
    std::string input_wav;
    std::string output_wav = "output.wav";

    // Testing / diagnostics
    bool min_readback = false;          // --min-readback: downgrade sample rate check to warning (emulator)
    bool single_core = false;           // --single-core: pin process to CPU 0 (diagnose threading issues)
    long rate_tolerance = 10;           // max Hz offset for sample rate readback before fatal (AD9361 PLL quantization)
};

bool load_config(const std::string& path, LoopbackConfig& cfg) {
    std::ifstream file(path);
    if (!file.is_open()) {
        log_error() << "Could not open config file: " << path << "\n";
        return false;
    }

    std::string line;
    int line_no = 0;
    while (std::getline(file, line)) {
        line_no++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        try {
            if (key == "frequency") cfg.frequency = std::stoull(value);
            else if (key == "sdr_rate") cfg.sdr_rate = std::stoul(value);
            else if (key == "decimation") cfg.decimation = std::stoi(value);
            else if (key == "rf_bandwidth") cfg.rf_bandwidth = std::stoul(value);
            else if (key == "gain") cfg.rx_gain = std::stod(value);
            else if (key == "tx_attenuation") cfg.tx_attenuation = std::stod(value);
            else if (key == "fm_deviation") cfg.fm_deviation = std::stod(value);
            else if (key == "uri") cfg.uri = value;
            else if (key == "max_iq_mb") cfg.max_iq_mb = std::stoi(value);
            else if (key == "audio_rate") cfg.audio_rate = std::stoi(value);
            else if (key == "bandpass_low") cfg.bandpass_low = std::stod(value);
            else if (key == "bandpass_high") cfg.bandpass_high = std::stod(value);
            else if (key == "lpf_cutoff") cfg.lpf_cutoff = std::stod(value);
            else if (key == "lpf_transition") cfg.lpf_transition = std::stod(value);
            else if (key == "min_readback") cfg.min_readback = (value == "true" || value == "1");
            else if (key == "single_core") cfg.single_core = (value == "true" || value == "1");
            else if (key == "rate_tolerance") cfg.rate_tolerance = std::stol(value);
            else log_warning() << path << ":" << line_no << ": unknown config key: " << key << "\n";
        } catch (const std::exception& e) {
            log_error() << path << ":" << line_no << ": parse error: " << key << "=" << value
                        << " (" << e.what() << ")\n";
            return false;
        }
    }

    return true;
}

void print_usage(const char* prog) {
    std::cout << "OPS-SAT IIO Loopback Test\n"
              << "=========================\n\n"
              << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  -c, --config    Config file path (KEY=VALUE format)\n"
              << "  -i, --input     Input WAV file (required)\n"
              << "  -o, --output    Output WAV file (default: output.wav)\n"
              << "  -u, --uri       IIO URI (default: local:)\n"
              << "  -f, --freq      Frequency in Hz (default: 1296000000)\n"
              << "  -d, --deviation FM deviation in Hz (default: 5000)\n"
              << "      --min-readback  Minimal readback: downgrade sample rate check to warning (emulator)\n"
              << "      --single-core   Pin process to CPU 0 (diagnose multi-threading crashes)\n"
              << "  -h, --help      Show this help\n";
}

// Parse config file (-c) then command-line overrides. Returns 0 on success,
// 1 on error, 2 if --help was requested (caller should exit 0).
int parse_args(int argc, char* argv[], LoopbackConfig& cfg) {
    std::string config_path;

    // First pass: find config file
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    // Load config file if specified
    if (!config_path.empty()) {
        if (!load_config(config_path, cfg)) {
            return 1;
        }
    }

    // Second pass: command-line overrides
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            i++;  // already handled
        } else if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
            cfg.input_wav = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            cfg.output_wav = argv[++i];
        } else if ((arg == "-u" || arg == "--uri") && i + 1 < argc) {
            cfg.uri = argv[++i];
        } else if ((arg == "-f" || arg == "--freq") && i + 1 < argc) {
            cfg.frequency = std::stoull(argv[++i]);
        } else if ((arg == "-d" || arg == "--deviation") && i + 1 < argc) {
            cfg.fm_deviation = std::stod(argv[++i]);
            if (cfg.fm_deviation <= 0) {
                log_error() << "FM deviation must be > 0\n";
                return 1;
            }
        } else if (arg == "--min-readback") {
            cfg.min_readback = true;
        } else if (arg == "--single-core") {
            cfg.single_core = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 2;
        } else if (arg[0] == '-') {
            log_error() << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Validate
    if (cfg.input_wav.empty()) {
        log_error() << "Input WAV file required\n";
        print_usage(argv[0]);
        return 1;
    }
    if (cfg.sdr_rate == 0) {
        log_error() << "sdr_rate must be > 0\n";
        return 1;
    }
    if (cfg.decimation <= 0) {
        log_error() << "decimation must be > 0\n";
        return 1;
    }
    if (cfg.sdr_rate % cfg.decimation != 0) {
        log_error() << "sdr_rate (" << cfg.sdr_rate << ") must be divisible by decimation ("
                    << cfg.decimation << ")\n";
        return 1;
    }
    cfg.effective_rate = cfg.sdr_rate / cfg.decimation;
    if (cfg.effective_rate == 0) {
        log_error() << "effective_rate (sdr_rate/decimation) must be > 0\n";
        return 1;
    }
    // AD9361 sampling_frequency_Hz limits: 2,083,000 - 61,440,000 Hz
    if (cfg.sdr_rate < 2083000 || cfg.sdr_rate > 61440000) {
        log_error() << "sdr_rate " << cfg.sdr_rate
                    << " Hz out of AD9361 range (2,083,000 - 61,440,000 Hz)\n";
        return 1;
    }
    // AD9361 rf_bandwidth_Hz limits: 200,000 - 56,000,000 Hz
    if (cfg.rf_bandwidth < 200000 || cfg.rf_bandwidth > 56000000) {
        log_error() << "rf_bandwidth " << cfg.rf_bandwidth
                    << " Hz out of AD9361 range (200,000 - 56,000,000 Hz)\n";
        return 1;
    }
    if (cfg.max_iq_mb <= 0) {
        log_error() << "max_iq_mb must be > 0\n";
        return 1;
    }
    if (cfg.audio_rate <= 0) {
        log_error() << "audio_rate must be > 0\n";
        return 1;
    }

    return 0;
}

// Derive .sc16 filename from a .wav filename
std::string make_iq_filename(const std::string& wav_path) {
    std::string iq_file = wav_path;
    size_t dot_pos = iq_file.rfind('.');
    size_t sep_pos = iq_file.find_last_of("/\\");
    bool has_extension = (dot_pos != std::string::npos) &&
                         (sep_pos == std::string::npos || dot_pos > sep_pos + 1);
    if (has_extension) {
        iq_file = iq_file.substr(0, dot_pos) + ".sc16";
    } else {
        iq_file += ".sc16";
    }
    return iq_file;
}

void log_config(const LoopbackConfig& cfg) {
    log_info() << "OPS-SAT PRETTY IIO Loopback Test\n";
    log_info() << "Input:       " << cfg.input_wav << "\n";
    log_info() << "Output:      " << cfg.output_wav << "\n";
    log_info() << "URI:         " << cfg.uri << "\n";
    log_info() << "Frequency:   " << cfg.frequency / 1e6 << " MHz\n";
    log_info() << "SDR rate:    " << cfg.sdr_rate << " Hz (AD9361 hardware)\n";
    log_info() << "Decimation:  " << cfg.decimation << "x\n";
    log_info() << "Eff. rate:   " << cfg.effective_rate << " Hz (post-LPF)\n";
    log_info() << "RF BW:       " << cfg.rf_bandwidth << " Hz\n";
    log_info() << "Audio rate:  " << cfg.audio_rate << " Hz\n";
    log_info() << "FM Dev:      " << cfg.fm_deviation << " Hz\n";
    log_info() << "LPF cutoff:  " << cfg.lpf_cutoff << " Hz\n";
    log_info() << "LPF trans:   " << cfg.lpf_transition << " Hz\n";
    log_info() << "Bandpass:    " << cfg.bandpass_low << " - " << cfg.bandpass_high << " Hz\n";
}

// Safe IIO channel attribute read — returns empty string on failure.
// iio_channel_attr_read() does not guarantee NUL-termination, so we
// construct std::string from (buf, n) to avoid reading past the payload.
static std::string iio_attr_read_str(struct iio_channel* ch, const char* attr) {
    char buf[256];
    ssize_t n = iio_channel_attr_read(ch, attr, buf, sizeof(buf));
    if (n <= 0) return "";
    if ((size_t)n >= sizeof(buf)) {
        log_warning() << "IIO attr '" << attr << "' truncated (" << n << " bytes, buf=" << sizeof(buf) << ")\n";
        buf[sizeof(buf) - 1] = '\0';
        return std::string(buf, sizeof(buf) - 1);
    }
    // Strip trailing NUL bytes — iio_channel_attr_read() may include the
    // NUL terminator in the returned byte count, which would create a
    // std::string containing embedded NUL that breaks string comparisons.
    while (n > 0 && buf[n - 1] == '\0') n--;
    return std::string(buf, (size_t)n);
}

// Read back actual AD9361 configuration and warn if values differ from requested.
// When strict=true, sample rate mismatch is fatal (returns false).
// When strict=false (--min-readback), sample rate mismatch is a warning (returns true).
bool readback_iio_config(struct iio_device* phy, const LoopbackConfig& cfg, bool strict) {
    // RX LO frequency — always warn-only because the AD9361 PLL quantizes to the
    // nearest achievable frequency (typically a few Hz off). This is normal and has
    // no practical impact on reception. Sample rate mismatches, on the other hand,
    // are fatal (when strict) because they affect file sizes and downlink budget.
    struct iio_channel* rx_lo = iio_device_find_channel(phy, "altvoltage0", true);
    if (rx_lo) {
        std::string val = iio_attr_read_str(rx_lo, "frequency");
        if (!val.empty()) {
            log_info() << "AD9361 RX LO readback: " << val << " Hz\n";
            try {
                long long actual_freq = std::stoll(trim(val));
                if ((unsigned long long)actual_freq != cfg.frequency) {
                    log_warning() << "RX LO mismatch — requested " << cfg.frequency
                                << " Hz, got " << actual_freq << " Hz\n";
                }
            } catch (const std::exception& e) {
                log_warning() << "could not parse RX LO frequency: " << val
                              << " (" << e.what() << ")\n";
            }
        }
    }

    // TX LO frequency — same PLL quantization as RX LO, always warn-only.
    struct iio_channel* tx_lo = iio_device_find_channel(phy, "altvoltage1", true);
    if (tx_lo) {
        std::string val = iio_attr_read_str(tx_lo, "frequency");
        if (!val.empty()) {
            log_info() << "AD9361 TX LO readback: " << val << " Hz\n";
            try {
                long long actual_freq = std::stoll(trim(val));
                if ((unsigned long long)actual_freq != cfg.frequency) {
                    log_warning() << "TX LO mismatch — requested " << cfg.frequency
                                << " Hz, got " << actual_freq << " Hz\n";
                }
            } catch (const std::exception& e) {
                log_warning() << "could not parse TX LO frequency: " << val
                              << " (" << e.what() << ")\n";
            }
        }
    }

    // RX channel attributes (voltage0, input) — sample rate confirmation is mandatory
    // for downlink budget correctness when strict=true.
    struct iio_channel* rx0 = iio_device_find_channel(phy, "voltage0", false);
    if (!rx0) {
        log_error() << "FATAL: could not find RX channel voltage0 — "
                    << "cannot confirm sample rate for budget correctness\n";
        return false;
    }
    {
        std::string val;

        val = iio_attr_read_str(rx0, "sampling_frequency");
        if (val.empty()) {
            if (strict) {
                log_error() << "FATAL: could not read sampling_frequency — "
                            << "cannot confirm sample rate for budget correctness\n";
                return false;
            } else {
                log_warning() << "could not read sampling_frequency (--min-readback, continuing)\n";
            }
        } else {
            log_info() << "AD9361 sample rate readback: " << val << " Hz\n";
            try {
                long actual_rate = std::stol(trim(val));
                // AD9361 may quantize the sample rate by a few Hz (e.g. 2399999
                // vs 2400000).  Allow ±rate_tolerance Hz before treating it as a mismatch.
                long rate_delta = std::abs(actual_rate - (long)cfg.sdr_rate);
                if (rate_delta > cfg.rate_tolerance) {
                    if (strict) {
                        log_error() << "FATAL: sample rate mismatch — requested " << cfg.sdr_rate
                                    << " Hz, got " << actual_rate
                                    << " Hz (invalidates downlink budget and file size expectations)\n";
                        return false;
                    } else {
                        log_warning() << "sample rate mismatch — requested " << cfg.sdr_rate
                                    << " Hz, got " << actual_rate
                                    << " Hz (--min-readback, continuing)\n";
                    }
                } else if (rate_delta > 0) {
                    log_info() << "AD9361 sample rate readback: " << actual_rate
                               << " Hz (within ±" << cfg.rate_tolerance << " Hz of requested " << cfg.sdr_rate << " Hz)\n";
                }
            } catch (const std::exception& e) {
                if (strict) {
                    log_error() << "FATAL: could not parse sampling_frequency '" << val
                                << "' (" << e.what()
                                << ") — cannot confirm sample rate for budget correctness\n";
                    return false;
                } else {
                    log_warning() << "could not parse sampling_frequency '" << val
                                << "' (" << e.what() << ") (--min-readback, continuing)\n";
                }
            }
        }

        val = iio_attr_read_str(rx0, "rf_bandwidth");
        if (!val.empty()) {
            log_info() << "AD9361 RX RF bandwidth readback: " << val << " Hz\n";
            try {
                long actual_bw = std::stol(trim(val));
                if ((unsigned long)actual_bw != cfg.rf_bandwidth) {
                    log_warning() << "RX RF bandwidth mismatch — requested " << cfg.rf_bandwidth
                                << " Hz, got " << actual_bw << " Hz\n";
                }
            } catch (const std::exception& e) {
                log_warning() << "could not parse RX RF bandwidth: " << val
                              << " (" << e.what() << ")\n";
            }
        }

        val = iio_attr_read_str(rx0, "gain_control_mode");
        if (!val.empty()) {
            log_info() << "AD9361 RX gain control mode: " << val << "\n";
            std::string mode = trim(val);
            if (mode != "manual") {
                log_warning() << "gain control mode mismatch — requested manual, got " << mode << "\n";
            }
        }

        val = iio_attr_read_str(rx0, "hardwaregain");
        if (!val.empty()) {
            log_info() << "AD9361 RX hardware gain: " << val << "\n";
            try {
                double actual_gain = std::stod(trim(val));
                if (std::abs(actual_gain - cfg.rx_gain) > 0.5) {
                    log_warning() << "RX gain mismatch — requested " << cfg.rx_gain
                                << " dB, got " << actual_gain << " dB\n";
                }
            } catch (const std::exception& e) {
                log_warning() << "could not parse RX hardwaregain: " << val
                              << " (" << e.what() << ")\n";
            }
        }

        val = iio_attr_read_str(rx0, "rssi");
        if (!val.empty()) {
            log_info() << "AD9361 RX RSSI: " << val << "\n";
        }
    }

    // TX channel attributes (voltage0, output)
    struct iio_channel* tx0 = iio_device_find_channel(phy, "voltage0", true);
    if (tx0) {
        std::string val;

        val = iio_attr_read_str(tx0, "rf_bandwidth");
        if (!val.empty()) {
            log_info() << "AD9361 TX RF bandwidth readback: " << val << " Hz\n";
            try {
                long actual_bw = std::stol(trim(val));
                if ((unsigned long)actual_bw != cfg.rf_bandwidth) {
                    log_warning() << "TX RF bandwidth mismatch — requested " << cfg.rf_bandwidth
                                << " Hz, got " << actual_bw << " Hz\n";
                }
            } catch (const std::exception& e) {
                log_warning() << "could not parse TX RF bandwidth: " << val
                              << " (" << e.what() << ")\n";
            }
        }

        val = iio_attr_read_str(tx0, "hardwaregain");
        if (!val.empty()) {
            log_info() << "AD9361 TX hardware gain: " << val << "\n";
            try {
                double actual_atten = std::abs(std::stod(trim(val)));
                if (std::abs(actual_atten - cfg.tx_attenuation) > 0.5) {
                    log_warning() << "TX attenuation mismatch — requested " << cfg.tx_attenuation
                                << " dB, got " << actual_atten << " dB\n";
                }
            } catch (const std::exception& e) {
                log_warning() << "could not parse TX hardwaregain: " << val
                              << " (" << e.what() << ")\n";
            }
        }
    }

    return true;
}

// RMS normalize a WAV file to target_dbfs (e.g. -20 dBFS -> target_rms = 0.1)
bool rms_normalize(const std::string& wav_path, double target_dbfs) {
    double target_rms = std::pow(10.0, target_dbfs / 20.0);

    SF_INFO sf_info = {0};
    SNDFILE* sf = sf_open(wav_path.c_str(), SFM_READ, &sf_info);
    if (!sf) {
        log_error() << "RMS normalize: Could not open " << wav_path << ": " << sf_strerror(NULL) << "\n";
        return false;
    }
    if (sf_info.channels <= 0) {
        log_error() << "RMS normalize: invalid channel count\n";
        sf_close(sf);
        return false;
    }

    std::vector<float> samples((size_t)sf_info.frames * (size_t)sf_info.channels);
    sf_count_t read = sf_readf_float(sf, samples.data(), sf_info.frames);
    sf_close(sf);

    if (read <= 0) {
        log_error() << "RMS normalize: No samples read from " << wav_path << "\n";
        return false;
    }
    size_t valid = (size_t)read * (size_t)sf_info.channels;
    samples.resize(valid);

    // Compute RMS over actual read samples only
    double sum_sq = 0.0;
    for (size_t i = 0; i < valid; i++) {
        sum_sq += (double)samples[i] * samples[i];
    }
    double rms = std::sqrt(sum_sq / valid);

    if (rms < 1e-10) {
        log_error() << "RMS normalize: Signal is silent, skipping normalization\n";
        return true;
    }

    double scale = target_rms / rms;
    log_info() << "RMS normalize: rms=" << rms << ", scale=" << scale
               << ", target=" << target_dbfs << " dBFS\n";

    for (size_t i = 0; i < samples.size(); i++) {
        float s = samples[i] * (float)scale;
        if (s > 1.0f) s = 1.0f;
        else if (s < -1.0f) s = -1.0f;
        samples[i] = s;
    }

    SF_INFO out_info = sf_info;
    out_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* out = sf_open(wav_path.c_str(), SFM_WRITE, &out_info);
    if (!out) {
        log_error() << "RMS normalize: Could not write " << wav_path << ": " << sf_strerror(NULL) << "\n";
        return false;
    }
    sf_writef_float(out, samples.data(), read);
    sf_close(out);

    return true;
}

// Check sc16 file for clipping and peak magnitude. Reads first and last
// `check_seconds` worth of samples. Sets clip_rate and peak_abs (0-32768 scale).
void check_sc16_quality(const std::string& path, unsigned long sample_rate,
                        double& clip_rate, int& peak_abs, double check_seconds = 2.0) {
    clip_rate = -1.0;
    peak_abs = 0;

    if (sample_rate == 0) return;

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return;

    long long file_bytes = (long long)f.tellg();
    if (file_bytes < 0) return;
    if (file_bytes % 4 != 0)
        log_warning() << "sc16 file not multiple of 4 bytes (truncated?)\n";
    long long total_pairs = file_bytes / 4;  // 2 shorts per complex sample
    if (total_pairs == 0) return;
    long long check_pairs = (long long)(check_seconds * sample_rate);
    if (check_pairs > total_pairs) check_pairs = total_pairs;

    long long clipped = 0;
    long long checked = 0;
    std::vector<int16_t> buf(4096);

    auto scan_region = [&](long long start_pair, long long num_pairs) {
        f.clear();
        f.seekg(start_pair * 4, std::ios::beg);
        long long remaining = num_pairs * 2;  // number of int16 values
        while (remaining > 0 && f.good()) {
            long long to_read = std::min(remaining, (long long)buf.size());
            f.read(reinterpret_cast<char*>(buf.data()), to_read * sizeof(int16_t));
            long long got = f.gcount() / sizeof(int16_t);
            if (got <= 0) break;
            for (long long i = 0; i < got; i++) {
                int abs_val = std::abs((int)buf[i]);
                if (abs_val > peak_abs) peak_abs = abs_val;
                if (buf[i] == 32767 || buf[i] == -32768) clipped++;
            }
            checked += got;
            remaining -= got;
        }
    };

    // Check first N seconds
    scan_region(0, check_pairs);

    // Check last N seconds (if file is long enough to not overlap)
    if (total_pairs > check_pairs * 2) {
        scan_region(total_pairs - check_pairs, check_pairs);
    }

    if (checked <= 0) return;  // keep clip_rate = -1.0
    clip_rate = (double)clipped / checked;
}

// Loopback signal quality check: mean-centered normalized cross-correlation
// between input and output WAV over a fixed window (first 3 seconds).
// Resamples input to output rate via linear interpolation if rates differ.
// Uses max(|corr|) to handle polarity flips from the demod chain.
// Returns peak |correlation| (0.0 = uncorrelated, 1.0 = identical).
// Sets best_lag_ms to the alignment offset in milliseconds.
double loopback_quality_check(const std::string& input_wav, const std::string& output_wav,
                              double& best_lag_ms) {
    best_lag_ms = 0.0;
    const double WINDOW_SEC = 3.0;  // fixed window to keep O(N*lags) bounded on ARM

    // Read input WAV
    SF_INFO in_info = {0};
    SNDFILE* in_sf = sf_open(input_wav.c_str(), SFM_READ, &in_info);
    if (!in_sf) {
        log_warning() << "Quality check: could not open input WAV: " << sf_strerror(NULL) << "\n";
        return -1.0;
    }
    std::vector<float> in_samples(in_info.frames);
    sf_count_t in_read = sf_readf_float(in_sf, in_samples.data(), in_info.frames);
    sf_close(in_sf);
    if (in_read <= 0) {
        log_warning() << "Quality check: no frames read from input WAV\n";
        return -1.0;
    }
    in_samples.resize((size_t)in_read);

    // Read output WAV
    SF_INFO out_info = {0};
    SNDFILE* out_sf = sf_open(output_wav.c_str(), SFM_READ, &out_info);
    if (!out_sf) {
        log_warning() << "Quality check: could not open output WAV: " << sf_strerror(NULL) << "\n";
        return -1.0;
    }
    std::vector<float> out_samples(out_info.frames);
    sf_count_t out_read = sf_readf_float(out_sf, out_samples.data(), out_info.frames);
    sf_close(out_sf);
    if (out_read <= 0) {
        log_warning() << "Quality check: no frames read from output WAV\n";
        return -1.0;
    }
    out_samples.resize((size_t)out_read);

    if (out_info.samplerate <= 0) {
        log_warning() << "Quality check: invalid output sample rate\n";
        return -1.0;
    }

    // Resample input to output rate if different (linear interpolation)
    std::vector<float> resampled;
    if (in_info.samplerate != out_info.samplerate && in_info.samplerate > 0) {
        double ratio = (double)out_info.samplerate / in_info.samplerate;
        size_t new_len = (size_t)(in_samples.size() * ratio);
        if (new_len == 0) {
            log_warning() << "Quality check: resampled length is 0\n";
            return -1.0;
        }
        resampled.resize(new_len);
        for (size_t i = 0; i < new_len; i++) {
            double src_idx = i / ratio;
            size_t idx0 = (size_t)src_idx;
            double frac = src_idx - idx0;
            if (idx0 + 1 < in_samples.size()) {
                resampled[i] = (float)((1.0 - frac) * in_samples[idx0] + frac * in_samples[idx0 + 1]);
            } else if (idx0 < in_samples.size()) {
                resampled[i] = in_samples[idx0];
            }
        }
        log_info() << "Quality check: resampled input " << in_info.samplerate
                   << " -> " << out_info.samplerate << " Hz (" << resampled.size() << " samples)\n";
    }

    // Use resampled input if rate conversion was needed, otherwise original
    const std::vector<float>& ref = resampled.empty() ? in_samples : resampled;

    // Cap to fixed window (first WINDOW_SEC seconds) to bound CPU on constrained targets
    size_t window_samples = (out_info.samplerate > 0)
        ? (size_t)(WINDOW_SEC * out_info.samplerate) : ref.size();

    // Search window: ±0.1 seconds at output rate (handles FM demod group delay)
    int max_lag = out_info.samplerate / 10;
    // Cap max_lag to 1/4 of shortest input to preserve useful overlap
    {
        size_t shortest = std::min({ref.size(), out_samples.size(), window_samples});
        if (max_lag > (int)shortest / 4) max_lag = (int)shortest / 4;
    }
    if (max_lag <= 0) return -1.0;

    // Compute overlap_len directly from both vectors and lag window.
    // Worst-case out index: out_start_max + overlap_len - 1 = 2*max_lag + overlap_len - 1.
    // Require: 2*max_lag + overlap_len <= out_samples.size()
    // Also:    overlap_len <= ref.size()
    // Also:    overlap_len <= window_samples
    if (out_samples.size() <= (size_t)(2 * max_lag)) return -1.0;
    size_t overlap_len = std::min({
        out_samples.size() - (size_t)(2 * max_lag),
        ref.size(),
        window_samples
    });
    if (overlap_len < 100) {
        log_warning() << "Quality check: overlap too short (" << overlap_len << " samples) for correlation\n";
        return -1.0;
    }
    log_info() << "Quality check: overlap=" << overlap_len << " samples ("
               << (double)overlap_len / out_info.samplerate << "s), max_lag=±"
               << max_lag << "\n";

    // Mean-center reference signal over the overlap window to remove DC offset
    // (FM demod can introduce DC bias). Reference window is fixed at [0..overlap_len).
    double ref_mean = 0.0;
    for (size_t i = 0; i < overlap_len; i++) {
        ref_mean += ref[i];
    }
    ref_mean /= overlap_len;

    // Compute energy of mean-centered reference (constant across all lags)
    double ref_energy = 0.0;
    for (size_t i = 0; i < overlap_len; i++) {
        double v = ref[i] - ref_mean;
        ref_energy += v * v;
    }
    if (ref_energy < 1e-20) {
        log_warning() << "Quality check: reference signal is silent after DC removal\n";
        return -1.0;
    }

    double best_corr = 0.0;
    int best_lag = 0;

    for (int lag = -max_lag; lag <= max_lag; lag++) {
        // For each lag, correlate ref[0..overlap_len) with out[out_start..out_start+overlap_len)
        int out_start = max_lag + lag;

        // Compute output mean over the actual correlated window (not a fixed offset)
        double out_mean = 0.0;
        for (size_t i = 0; i < overlap_len; i++) {
            out_mean += out_samples[out_start + (int)i];
        }
        out_mean /= overlap_len;

        double sum = 0.0;
        double out_energy = 0.0;
        for (size_t i = 0; i < overlap_len; i++) {
            double r = ref[i] - ref_mean;
            double o = out_samples[out_start + (int)i] - out_mean;
            sum += r * o;
            out_energy += o * o;
        }

        if (out_energy < 1e-20) continue;
        double corr = sum / std::sqrt(ref_energy * out_energy);
        // Use |corr| to handle polarity flips from demod chain
        if (std::abs(corr) > std::abs(best_corr)) {
            best_corr = corr;
            best_lag = lag;
        }
    }

    best_lag_ms = (out_info.samplerate > 0)
        ? (double)best_lag * 1000.0 / out_info.samplerate : 0.0;

    return std::abs(best_corr);
}

// Read input WAV file via libsndfile. Returns duration in seconds, or -1 on error.
// Loads all samples into `samples` as float (deterministic type, no GNU Radio version dependency).
double read_input_wav(const std::string& path, int& audio_rate, long long& frame_count,
                      std::vector<float>& samples) {
    SF_INFO sf_info = {0};
    SNDFILE* sf = sf_open(path.c_str(), SFM_READ, &sf_info);
    if (!sf) {
        log_error() << "Could not open WAV file: " << path << "\n";
        log_error() << "Reason: " << sf_strerror(NULL) << "\n";
        return -1.0;
    }

    audio_rate = sf_info.samplerate;
    frame_count = sf_info.frames;
    int channels = sf_info.channels;
    int format = sf_info.format;

    if (channels > 1) {
        sf_close(sf);
        log_error() << "Input file has " << channels << " channels — must be mono\n";
        return -1.0;
    }
    if ((format & SF_FORMAT_SUBMASK) != SF_FORMAT_PCM_16) {
        sf_close(sf);
        log_error() << "Input WAV must be 16-bit PCM (use convert-sample.sh)\n";
        return -1.0;
    }
    if (audio_rate <= 0) {
        sf_close(sf);
        log_error() << "Invalid WAV file sample rate: " << audio_rate << "\n";
        return -1.0;
    }
    if (frame_count <= 0) {
        sf_close(sf);
        log_error() << "WAV file is empty or invalid\n";
        return -1.0;
    }

    samples.resize((size_t)frame_count);
    sf_count_t read = sf_readf_float(sf, samples.data(), frame_count);
    sf_close(sf);

    if (read <= 0) {
        log_error() << "Could not read samples from WAV file\n";
        samples.clear();
        return -1.0;
    }
    if (read < frame_count) {
        log_warning() << "WAV read returned " << read << "/" << frame_count << " frames\n";
        frame_count = read;
    }
    samples.resize((size_t)read);

    return (double)frame_count / audio_rate;
}

// Helper: restore loopback attribute via a fresh IIO context.
// Called after run_loopback() returns so no context conflicts with GNU Radio.
static void restore_loopback(const std::string& uri, const std::string& prev_loopback) {
    log_info() << "Restoring loopback to '" << prev_loopback << "'...\n";
    struct iio_context* ctx = iio_create_context_from_uri(uri.c_str());
    if (!ctx) {
        log_warning() << "could not connect to IIO for loopback restore\n";
        return;
    }
    struct iio_device* phy = iio_context_find_device(ctx, "ad9361-phy");
    if (!phy) {
        log_warning() << "could not find ad9361-phy for loopback restore\n";
        iio_context_destroy(ctx);
        return;
    }
    int ret = iio_device_attr_write(phy, "loopback", prev_loopback.c_str());
    if (ret < 0) {
        log_warning() << "restore failed (ret=" << ret << "), trying '0'\n";
        iio_device_attr_write(phy, "loopback", "0");
    }
    iio_context_destroy(ctx);
}

// Build GNU Radio flowgraph, run loopback test, then post-process audio.
// iq_sample_count is the pre-snapped integer sample count (avoids float->int truncation).
// input_samples: PCM float samples read via libsndfile (deterministic type).
// prev_loopback: value to restore on forced exit (_Exit path bypasses RAII).
int run_loopback(const LoopbackConfig& cfg,
                 int input_audio_rate, long long iq_sample_count,
                 const std::vector<float>& input_samples,
                 const std::string& prev_loopback) {
    // Reset signal flags (safe for multi-run or future use)
    g_running = 1;
    g_signal_received = 0;

    if (cfg.effective_rate == 0 || cfg.audio_rate <= 0) {
        log_error() << "Invalid rates: effective_rate and audio_rate must be > 0\n";
        return 1;
    }
    if (input_audio_rate <= 0) {
        log_error() << "Invalid input audio rate: " << input_audio_rate << "\n";
        return 1;
    }

    std::string iq_file = make_iq_filename(cfg.output_wav);

    log_info() << "Input audio rate: " << input_audio_rate << " Hz\n";
    log_info() << "Output audio rate: " << cfg.audio_rate << " Hz\n";

    // TX resampling: input audio rate -> SDR hardware rate (sdr_rate)
    unsigned long tx_gcd = std::gcd((unsigned long)cfg.sdr_rate, (unsigned long)input_audio_rate);
    unsigned long tx_interp = cfg.sdr_rate / tx_gcd;
    unsigned long tx_decim = input_audio_rate / tx_gcd;
    log_info() << "TX resample:      " << input_audio_rate << " -> " << cfg.sdr_rate
               << " (interp=" << tx_interp << ", decim=" << tx_decim << ")\n";
    // Sanity cap: absurd ratios produce huge FIR filter tap counts and blow memory
    if (tx_interp > 1000 || tx_decim > 1000) {
        log_error() << "TX resampler ratio " << tx_interp << "/" << tx_decim
                    << " too large (sdr_rate=" << cfg.sdr_rate
                    << ", input_audio_rate=" << input_audio_rate
                    << ") — choose rates with a reasonable GCD\n";
        return 1;
    }

    // RX resampling: effective_rate (post-LPF) -> output audio rate (always 16 kHz)
    unsigned long rx_gcd = std::gcd((unsigned long)cfg.effective_rate, (unsigned long)cfg.audio_rate);
    unsigned long rx_interp = cfg.audio_rate / rx_gcd;
    unsigned long rx_decim = cfg.effective_rate / rx_gcd;
    log_info() << "RX resample:      " << cfg.effective_rate << " -> " << cfg.audio_rate
               << " (interp=" << rx_interp << ", decim=" << rx_decim << ")\n";
    if (rx_interp > 1000 || rx_decim > 1000) {
        log_error() << "RX resampler ratio " << rx_interp << "/" << rx_decim
                    << " too large (effective_rate=" << cfg.effective_rate
                    << ", audio_rate=" << cfg.audio_rate
                    << ") — choose rates with a reasonable GCD\n";
        return 1;
    }

    // Use integer sample count directly (passed from main, avoids float->int truncation)
    // iq_samples is at effective_rate (post-LPF). TX runs at sdr_rate.
    long long iq_samples = iq_sample_count;
    long long tx_samples = iq_samples * (long long)cfg.decimation;  // TX at sdr_rate
    double duration_sec = (double)iq_samples / cfg.effective_rate;
    // Divide before multiply to avoid overflow in the intermediate product.
    if (iq_samples % (long long)rx_decim != 0) {
        long long snapped = (iq_samples / (long long)rx_decim) * (long long)rx_decim;
        log_info() << "Snapping iq_samples " << iq_samples << " -> " << snapped
                   << " (multiple of rx_decim=" << rx_decim << ")\n";
        iq_samples = snapped;
        tx_samples = snapped * (long long)cfg.decimation;  // TX at sdr_rate
        if (iq_samples <= 0) {
            log_error() << "iq_samples snapped to 0 — duration too short for rx_decim\n";
            return 1;
        }
        duration_sec = (double)iq_samples / cfg.effective_rate;
    }
    long long rx_audio_samples = (iq_samples / (long long)rx_decim) * (long long)rx_interp;
    log_info() << "I/Q scale:        " << IQ_SCALE << " (|1.0| -> " << (int)IQ_SCALE << " int16)\n";
    log_info() << "TX samples:       " << tx_samples << " (at " << cfg.sdr_rate << " Hz)\n";
    log_info() << "RX I/Q samples:   " << iq_samples << " (at " << cfg.effective_rate << " Hz post-LPF)\n";
    log_info() << "RX audio samples: " << rx_audio_samples << " (at " << cfg.audio_rate << " Hz)\n";

    // Expected sc16 file size: 2 shorts per complex sample = 4 bytes/sample
    long long expected_iq_bytes = iq_samples * 4LL;
    log_info() << "Expected .sc16:   " << (double)expected_iq_bytes / (1024.0 * 1024.0) << " MiB\n";
    log_info() << "I/Q file:         " << iq_file << "\n";

    // Validate DSP parameters before designing filter taps
    // LPF cutoff is validated against sdr_rate (LPF input rate before decimation)
    if (cfg.lpf_cutoff <= 0 || cfg.lpf_cutoff >= cfg.sdr_rate / 2.0) {
        log_error() << "LPF cutoff " << cfg.lpf_cutoff << " Hz out of range (0, "
                    << cfg.sdr_rate / 2.0 << " Hz)\n";
        return 1;
    }
    if (cfg.lpf_transition <= 0) {
        log_error() << "LPF transition width must be > 0\n";
        return 1;
    }
    if (cfg.bandpass_low <= 0 || cfg.bandpass_high <= cfg.bandpass_low ||
        cfg.bandpass_high >= cfg.audio_rate / 2.0) {
        log_error() << "Bandpass range [" << cfg.bandpass_low << ", " << cfg.bandpass_high
                    << "] Hz invalid for audio rate " << cfg.audio_rate << " Hz\n";
        return 1;
    }

    // Design LPF taps at sdr_rate (input rate before decimation)
    std::vector<float> lpf_taps = gr::filter::firdes::low_pass(
        1.0, cfg.sdr_rate, cfg.lpf_cutoff, cfg.lpf_transition,
        gr::fft::window::WIN_HAMMING
    );
    log_info() << "LPF taps: " << lpf_taps.size()
               << " (designed at " << cfg.sdr_rate << " Hz, decim=" << cfg.decimation << ")\n";

    std::vector<float> bp_taps = gr::filter::firdes::band_pass(
        1.0, cfg.audio_rate, cfg.bandpass_low, cfg.bandpass_high, 200.0,
        gr::fft::window::WIN_HAMMING
    );
    log_info() << "Bandpass taps: " << bp_taps.size() << "\n";

    // --- Build flowgraph ---
    // Retry loop handles stale IIO connections from previously crashed runs.
    // When a process is killed by a signal, the IIO server (especially
    // network-based like iio-emu) may take time to detect the dead TCP connection
    // and free the connection slot. Retrying gives it time to recover.
    // On local IIO (real hardware), the first attempt always succeeds.
    gr::top_block_sptr tb;
    gr::blocks::head::sptr tx_head, iq_head, rx_head;

    const int MAX_BUILD_ATTEMPTS = 3;
    const int BUILD_RETRY_DELAY_SEC = 5;

    for (int attempt = 1; ; attempt++) {
    try {
        log_info() << "Building flowgraph...\n";
        tb = gr::make_top_block("loopback_test");

        // TX path blocks (input samples loaded via libsndfile — deterministic float type)
        // TX runs at sdr_rate (AD9361 hardware rate)
        auto wav_src      = gr::blocks::vector_source_f::make(input_samples, false);
        auto resampler_tx = gr::filter::rational_resampler_fff::make(tx_interp, tx_decim);
        tx_head           = gr::blocks::head::make(sizeof(float), tx_samples);
        auto scaler       = gr::blocks::multiply_const_ff::make(0.8);
        double sensitivity = 2.0 * M_PI * cfg.fm_deviation / cfg.sdr_rate;
        auto fm_mod       = gr::analog::frequency_modulator_fc::make(sensitivity);

        // TX IIO sink — uses device_sink directly instead of fmcomms2_sink_fc32
        // to avoid the underflow-check thread that crashes when FPGA register
        // reads are unsupported (throws from std::thread → std::terminate).
        gr::iio::iio_param_vec_t tx_params;
        tx_params.emplace_back("out_altvoltage1_TX_LO_frequency",
                               static_cast<unsigned long long>(cfg.frequency));
        tx_params.emplace_back("out_voltage_sampling_frequency",
                               static_cast<unsigned long>(cfg.sdr_rate));
        tx_params.emplace_back("out_voltage_rf_bandwidth",
                               static_cast<unsigned long>(cfg.rf_bandwidth));
        tx_params.emplace_back("out_voltage0_hardwaregain", -cfg.tx_attenuation);

        std::vector<std::string> tx_channels = {"voltage0", "voltage1"};
        auto iio_sink = gr::iio::device_sink::make(
            cfg.uri, "cf-ad9361-dds-core-lpc", tx_channels, "ad9361-phy",
            tx_params, 0x8000, 0, false);

        // TX format conversion: gr_complex → float I/Q → int16
        // fmcomms2_sink_fc32 internally multiplies by 32768.0
        auto from_fc32   = gr::blocks::complex_to_float::make(1);
        auto tx_r_to_s16 = gr::blocks::float_to_short::make(1, 32768.0f);
        auto tx_i_to_s16 = gr::blocks::float_to_short::make(1, 32768.0f);

        // RX IIO source — same device_source approach as sdr-capture
        gr::iio::iio_param_vec_t rx_params;
        rx_params.emplace_back("out_altvoltage0_RX_LO_frequency",
                               static_cast<unsigned long long>(cfg.frequency));
        rx_params.emplace_back("in_voltage0_sampling_frequency",
                               static_cast<unsigned long>(cfg.sdr_rate));
        rx_params.emplace_back("in_voltage0_rf_bandwidth",
                               static_cast<unsigned long>(cfg.rf_bandwidth));
        rx_params.emplace_back("in_voltage0_gain_control_mode=manual");
        rx_params.emplace_back("in_voltage0_hardwaregain", cfg.rx_gain);
        rx_params.emplace_back("in_voltage_quadrature_tracking_en", 1);
        rx_params.emplace_back("in_voltage_rf_dc_offset_tracking_en", 1);
        rx_params.emplace_back("in_voltage_bb_dc_offset_tracking_en", 1);

        std::vector<std::string> rx_channels = {"voltage0", "voltage1"};
        auto iio_src = gr::iio::device_source::make(
            cfg.uri, "cf-ad9361-lpc", rx_channels, "ad9361-phy",
            rx_params, 0x8000);

        // RX format conversion: int16 → float → gr_complex
        // AD9361 ADC is 12-bit: divide by 2048.0 to normalize to ≈±1.0
        auto i_s2f   = gr::blocks::short_to_float::make(1, 2048.0f);
        auto q_s2f   = gr::blocks::short_to_float::make(1, 2048.0f);
        auto to_fc32 = gr::blocks::float_to_complex::make(1);

        // Readback validation with a temporary IIO context.
        {
            struct iio_context* rb_ctx = iio_create_context_from_uri(cfg.uri.c_str());
            if (!rb_ctx) {
                log_error() << "Could not connect to IIO for readback at " << cfg.uri << "\n";
                return 1;
            }
            struct iio_device* phy = iio_context_find_device(rb_ctx, "ad9361-phy");
            if (!phy) {
                log_error() << "No ad9361-phy device for readback\n";
                iio_context_destroy(rb_ctx);
                return 1;
            }
            if (cfg.min_readback) {
                log_info() << "Minimal readback mode (--min-readback): sample rate mismatch is non-fatal\n";
            }
            if (!readback_iio_config(phy, cfg, !cfg.min_readback)) {
                iio_context_destroy(rb_ctx);
                return 1;
            }
            iio_context_destroy(rb_ctx);
        }

        // RX DSP blocks — LPF decimates from sdr_rate to effective_rate
        auto lpf         = gr::filter::fir_filter_ccf::make(cfg.decimation, lpf_taps);
        auto to_short    = gr::blocks::complex_to_interleaved_short::make(false, IQ_SCALE);
        iq_head          = gr::blocks::head::make(sizeof(gr_complex), iq_samples);
        // iq_samples counts complex samples at effective_rate (post-LPF);
        // sink writes 2 int16 per sample (I,Q) => 4 bytes/sample
        auto iq_sink_blk = gr::blocks::file_sink::make(sizeof(short), iq_file.c_str(), false);
        // FM demod operates at effective_rate (post-LPF output)
        auto fm_demod    = gr::analog::quadrature_demod_cf::make(
            cfg.effective_rate / (2.0 * M_PI * cfg.fm_deviation)
        );
        // Resample from effective_rate to audio_rate
        auto resampler_rx = gr::filter::rational_resampler_fff::make(rx_interp, rx_decim);
        auto bandpass     = gr::filter::fir_filter_fff::make(1, bp_taps);
        rx_head           = gr::blocks::head::make(sizeof(float), rx_audio_samples);
        auto wav_sink     = gr::blocks::wavfile_sink::make(
            cfg.output_wav.c_str(), 1, cfg.audio_rate,
            gr::blocks::FORMAT_WAV, gr::blocks::FORMAT_PCM_16
        );

        // Connect TX path: wav → resample → head → scaler → FM mod → convert → IIO sink
        tb->connect(wav_src, 0, resampler_tx, 0);
        tb->connect(resampler_tx, 0, tx_head, 0);
        tb->connect(tx_head, 0, scaler, 0);
        tb->connect(scaler, 0, fm_mod, 0);
        tb->connect(fm_mod, 0, from_fc32, 0);
        tb->connect(from_fc32, 0, tx_r_to_s16, 0);
        tb->connect(from_fc32, 1, tx_i_to_s16, 0);
        tb->connect(tx_r_to_s16, 0, iio_sink, 0);
        tb->connect(tx_i_to_s16, 0, iio_sink, 1);

        // Connect RX path: IIO src (shorts) → fc32 conversion → LPF → branches
        tb->connect(iio_src, 0, i_s2f, 0);
        tb->connect(iio_src, 1, q_s2f, 0);
        tb->connect(i_s2f, 0, to_fc32, 0);
        tb->connect(q_s2f, 0, to_fc32, 1);
        tb->connect(to_fc32, 0, lpf, 0);

        // Branch 1: LPF -> head -> sc16 conversion -> file_sink (.sc16)
        tb->connect(lpf, 0, iq_head, 0);
        tb->connect(iq_head, 0, to_short, 0);
        tb->connect(to_short, 0, iq_sink_blk, 0);

        // Branch 2: LPF -> FM demod -> resample -> bandpass -> head -> wav_sink
        tb->connect(lpf, 0, fm_demod, 0);
        tb->connect(fm_demod, 0, resampler_rx, 0);
        tb->connect(resampler_rx, 0, bandpass, 0);
        tb->connect(bandpass, 0, rx_head, 0);
        tb->connect(rx_head, 0, wav_sink, 0);

        // --- Start ---
        log_info() << "Starting flowgraph...\n";
        tb->start();
        break;  // flowgraph started — exit retry loop
    } catch (const std::exception& e) {
        // Release partial resources (including any IIO contexts held by GNU Radio blocks)
        tb.reset();
        tx_head.reset();
        iq_head.reset();
        rx_head.reset();

        if (attempt >= MAX_BUILD_ATTEMPTS) {
            log_error() << "FATAL: flowgraph build/start failed after "
                        << MAX_BUILD_ATTEMPTS << " attempts: " << e.what() << "\n";
            return 1;
        }
        log_warning() << "Flowgraph attempt " << attempt << "/" << MAX_BUILD_ATTEMPTS
                      << " failed: " << e.what()
                      << " — retrying in " << BUILD_RETRY_DELAY_SEC << "s\n";
        std::this_thread::sleep_for(std::chrono::seconds(BUILD_RETRY_DELAY_SEC));
    }
    } // end retry loop

    auto start_time = std::chrono::steady_clock::now();
    int timeout_sec = (int)(duration_sec * 2) + 10;
    log_info() << "Timeout:  " << timeout_sec << " seconds (2x duration + 10)\n";

    bool timed_out = false;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

        bool iq_done = iq_head->nitems_written(0) >= (uint64_t)iq_samples;
        if (iq_done) {
            // Grace period: let the scheduler drain the I/Q pipeline
            // (head → complex_to_interleaved_short → file_sink) before we
            // call tb->stop().  Without this, in-flight buffer items could
            // be lost, producing a slightly short sc16 file.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            log_info() << "Capture complete (TX: " << tx_head->nitems_written(0) << "/" << tx_samples
                       << ", RX audio: " << rx_head->nitems_written(0) << "/" << rx_audio_samples
                       << ", RX I/Q: " << iq_head->nitems_written(0) << "/" << iq_samples
                       << " samples, " << elapsed_sec << "s)\n";
            break;
        }

        // Progress every 5 seconds
        if (elapsed_sec > 0 && elapsed_sec % 5 == 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() % 5000 < 100) {
            log_info() << "Progress [" << elapsed_sec << "s]: TX "
                       << tx_head->nitems_written(0) << "/" << tx_samples
                       << ", I/Q " << iq_head->nitems_written(0) << "/" << iq_samples
                       << ", audio " << rx_head->nitems_written(0) << "/" << rx_audio_samples << "\n";
        }

        if (elapsed >= std::chrono::seconds(timeout_sec)) {
            log_error() << "Timeout after " << elapsed_sec << " seconds"
                        << " (TX: " << tx_head->nitems_written(0) << "/" << tx_samples
                        << ", RX audio: " << rx_head->nitems_written(0) << "/" << rx_audio_samples
                        << ", RX I/Q: " << iq_head->nitems_written(0) << "/" << iq_samples << ")\n";
            timed_out = true;
            tb->stop();  // belt-and-suspenders: stop immediately so wait() won't hang on wedged graph
            break;
        }
    }

    bool interrupted = (g_signal_received != 0);
    if (interrupted) {
        log_info() << "Received signal " << g_signal_received << ", stopping...\n";
    }
    if (timed_out) {
        log_error() << "Loopback may have failed - check SDR connection\n";
    }

    log_info() << "Stopping flowgraph...\n";
    tb->stop();

    // Watchdog: run tb->wait() on a thread with a grace period.
    // If wait() hangs (wedged IIO driver, stuck DMA), force-exit to prevent
    // mission automation scripts from stalling indefinitely.
    {
        const int WAIT_GRACE_SEC = 10;
        std::atomic<bool> wait_done{false};
        std::thread wait_thread([&]() {
            tb->wait();
            wait_done = true;
        });
        auto wait_start = std::chrono::steady_clock::now();
        while (!wait_done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto wait_elapsed = std::chrono::steady_clock::now() - wait_start;
            if (wait_elapsed >= std::chrono::seconds(WAIT_GRACE_SEC)) {
                log_error() << "FATAL: tb->wait() hung for " << WAIT_GRACE_SEC
                            << "s after stop — force-exiting to prevent automation stall\n";
                // Best-effort loopback restore before forced exit (_Exit skips RAII)
                restore_loopback(cfg.uri, prev_loopback);
                std::cerr.flush();
                std::cout.flush();
                wait_thread.detach();
                _Exit(2);
            }
        }
        wait_thread.join();
    }

    auto end_time = std::chrono::steady_clock::now();
    auto total_sec = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();

    bool early_stop = timed_out || interrupted;
    long long actual_tx_written = (long long)tx_head->nitems_written(0);
    long long actual_iq_written = (long long)iq_head->nitems_written(0);
    long long actual_audio_written = (long long)rx_head->nitems_written(0);

    log_info() << (early_stop ? "Loopback stopped early" : "Loopback test complete") << "\n";
    log_info() << "Wall time: " << total_sec << " seconds\n";
    log_info() << "TX:    " << actual_tx_written << "/" << tx_samples << " samples\n";
    log_info() << "Audio: " << cfg.output_wav << "\n";
    log_info() << "I/Q:   " << iq_file << "\n";
    if (early_stop) {
        log_info() << "Partial:   TX " << actual_tx_written << "/" << tx_samples
                   << ", I/Q " << actual_iq_written << "/" << iq_samples
                   << ", audio " << actual_audio_written << "/" << rx_audio_samples << "\n";
        // Use libsndfile as authoritative source for actual WAV frame count
        SF_INFO sf_check = {0};
        SNDFILE* sf_partial = sf_open(cfg.output_wav.c_str(), SFM_READ, &sf_check);
        if (sf_partial) {
            double wav_duration = (sf_check.samplerate > 0)
                ? (double)sf_check.frames / sf_check.samplerate : 0.0;
            log_info() << "WAV file:  " << sf_check.frames << " frames @ "
                       << sf_check.samplerate << " Hz (" << wav_duration << "s, partial)\n";
            sf_close(sf_partial);
        } else {
            log_warning() << "could not open WAV for partial frame count check\n";
        }
    }

    // Track hard validation failures (logged as FAIL but must also affect exit code)
    bool hard_fail = false;

    // Post-processing: RMS normalization to -20 dBFS (skip on partial capture)
    if (early_stop) {
        log_warning() << "skipping audio normalization (partial capture)\n";
    } else {
        log_info() << "Normalizing audio...\n";
        if (!rms_normalize(cfg.output_wav, -20.0)) {
            log_warning() << "audio normalization failed\n";
        }
    }

    // Verify sc16 file — file size on disk is the authoritative source of truth
    {
        std::ifstream iq_check(iq_file, std::ios::binary | std::ios::ate);
        if (iq_check.is_open()) {
            long long actual_bytes = (long long)iq_check.tellg();
            if (actual_bytes < 0) {
                log_warning() << "sc16 file size check: tellg() failed\n";
            } else if (actual_bytes == 0) {
                log_error() << "FAIL: sc16 file is empty\n";
                hard_fail = true;
            } else {
                if (actual_bytes % 4 != 0) {
                    log_warning() << "sc16 file size " << actual_bytes
                                  << " not a multiple of 4 bytes (truncated write?)\n";
                }
                long long actual_samples = actual_bytes / 4;
                double actual_duration = (double)actual_samples / cfg.effective_rate;
                if (early_stop) {
                    // On early stop, just report what's on disk — don't compare to expected
                    log_info() << "sc16 file: " << actual_bytes << " bytes, "
                               << actual_samples << " samples ("
                               << actual_duration << "s, partial)\n";
                } else {
                    long long expected_bytes = iq_samples * 4LL;
                    log_info() << "sc16 file size: " << actual_bytes << " bytes"
                               << " (expected " << expected_bytes << ")\n";
                    if (expected_bytes <= 0) {
                        log_warning() << "expected_bytes is 0, skipping deviation check\n";
                    } else if (actual_bytes != expected_bytes) {
                        double deviation = std::abs((double)(actual_bytes - expected_bytes) / expected_bytes);
                        if (deviation > 0.05) {
                            log_error() << "FAIL: sc16 size deviation " << (deviation * 100.0)
                                        << "% — got " << actual_samples << " samples ("
                                        << actual_duration << "s)\n";
                            hard_fail = true;
                        } else if (deviation > 0.005) {
                            log_warning() << "sc16 size deviation " << (deviation * 100.0)
                                        << "% — got " << actual_samples << " samples ("
                                        << actual_duration << "s)\n";
                        }
                    }
                }
            }
        } else {
            log_error() << "FAIL: Could not open sc16 file: " << iq_file << "\n";
            hard_fail = true;
        }
    }

    // Check for I/Q clipping and peak magnitude (still useful on partial captures)
    double clip_rate;
    int peak_abs;
    check_sc16_quality(iq_file, cfg.effective_rate, clip_rate, peak_abs);
    if (clip_rate >= 0.0) {
        double peak_dbfs = (peak_abs > 0) ? 20.0 * std::log10((double)peak_abs / IQ_SCALE) : -999.0;
        log_info() << "sc16 peak: " << peak_abs << "/" << (int)IQ_SCALE
                   << " (" << peak_dbfs << " dBFS@IQ_SCALE)\n";
        log_info() << "sc16 rail hit rate: " << (clip_rate * 100.0) << "% (per int16, ±32767)\n";
        if (clip_rate > 0.01) {
            log_warning() << "sc16 rail hit rate " << (clip_rate * 100.0)
                        << "% exceeds 1% — RX may be saturating "
                        << "(reduce TX level or RX gain)\n";
        }
    }

    // Loopback signal quality validation: cross-correlate input and output audio
    if (!early_stop) {
        double lag_ms = 0.0;
        double correlation = loopback_quality_check(cfg.input_wav, cfg.output_wav, lag_ms);
        if (correlation < 0.0) {
            log_warning() << "could not compute loopback quality metric\n";
        } else {
            log_info() << "Loopback quality: correlation=" << correlation
                       << ", lag=" << lag_ms << " ms\n";
            if (correlation < 0.3) {
                log_error() << "FAIL: loopback correlation " << correlation
                            << " < 0.3 — output does not resemble input "
                            << "(loopback may not be working)\n";
                hard_fail = true;
            } else if (correlation < 0.7) {
                log_warning() << "loopback correlation " << correlation
                            << " < 0.7 — marginal signal quality\n";
            } else {
                log_info() << "Loopback PASS: correlation " << correlation << " >= 0.7\n";
            }
        }
    } else {
        log_warning() << "skipping loopback quality check (partial capture)\n";
    }

    log_info() << "NOTE: SDR at " << cfg.sdr_rate / 1000 << " kSPS, decimation "
               << cfg.decimation << "x -> " << cfg.effective_rate / 1000 << " kSPS effective\n";

    if (timed_out) return 2;
    if (interrupted) return 128 + g_signal_received;  // conventional: 130 for SIGINT, 143 for SIGTERM
    if (hard_fail) return 1;
    return 0;
}

int main(int argc, char* argv[]) {
    LoopbackConfig cfg;

    try {
        int rc = parse_args(argc, argv, cfg);
        if (rc == 2) return 0;  // --help
        if (rc != 0) return rc;
    } catch (const std::exception& e) {
        log_error() << "Invalid argument value: " << e.what() << "\n";
        print_usage(argv[0]);
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Pin to CPU 0 if requested — serializes all GNU Radio scheduler threads
    // onto one core to diagnose multi-threading race conditions.
    if (cfg.single_core) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(0, &cpuset);
        if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0) {
            log_info() << "Pinned to CPU 0 (--single-core)\n";
        } else {
            log_warning() << "sched_setaffinity failed: " << strerror(errno)
                          << " (continuing without CPU pinning)\n";
        }
    }

    log_config(cfg);

    // --- Enable Loopback Mode via libiio ---
    // Context is created, used to enable loopback, then destroyed BEFORE
    // run_loopback() creates GNU Radio blocks (which open their own contexts).
    // Keeping this context alive during flowgraph execution causes segfaults
    // due to IIO device conflicts with the device_source/sink contexts.
    std::string prev_loopback = "0";
    {
        log_info() << "Connecting to IIO context for loopback setup...\n";
        struct iio_context* ctx = iio_create_context_from_uri(cfg.uri.c_str());
        if (!ctx) {
            log_error() << "Could not connect to IIO context at " << cfg.uri << "\n";
            return 1;
        }

        struct iio_device* phy = iio_context_find_device(ctx, "ad9361-phy");
        if (!phy) {
            log_error() << "Could not find ad9361-phy device\n";
            iio_context_destroy(ctx);
            return 1;
        }

        // Save current loopback value for restore on exit
        {
            char lb_buf[64];
            ssize_t rb = iio_device_attr_read(phy, "loopback", lb_buf, sizeof(lb_buf));
            if (rb <= 0) {
                log_warning() << "Could not read loopback attribute, will restore to '0'\n";
            } else if ((size_t)rb >= sizeof(lb_buf)) {
                log_warning() << "Loopback attr truncated (" << rb << " bytes, buf="
                              << sizeof(lb_buf) << "), will restore to '0'\n";
            } else {
                prev_loopback = trim(std::string(lb_buf, (size_t)rb));
                log_info() << "AD9361 loopback current value: " << prev_loopback << "\n";
            }
        }

        log_info() << "Enabling loopback mode (writing '1' to loopback attr)...\n";
        int ret = iio_device_attr_write(phy, "loopback", "1");
        if (ret < 0) {
            log_error() << "FATAL: could not enable loopback (ret=" << ret
                        << "). Loopback validation requires working loopback mode.\n";
            iio_context_destroy(ctx);
            return 1;
        }
        log_info() << "Loopback write OK (wrote " << ret << " bytes)\n";

        // Read back loopback attribute to confirm actual mode
        {
            char lb_buf[64];
            ssize_t rb = iio_device_attr_read(phy, "loopback", lb_buf, sizeof(lb_buf));
            if (rb <= 0) {
                log_error() << "FATAL: could not read back loopback attribute — "
                            << "cannot confirm loopback is active\n";
                iio_context_destroy(ctx);
                return 1;
            } else if ((size_t)rb >= sizeof(lb_buf)) {
                log_error() << "FATAL: loopback readback truncated (" << rb << " bytes, buf="
                            << sizeof(lb_buf) << ") — cannot confirm loopback is active\n";
                iio_context_destroy(ctx);
                return 1;
            } else {
                std::string readback = trim(std::string(lb_buf, (size_t)rb));
                log_info() << "AD9361 loopback readback: " << readback << "\n";
                if (readback != "1") {
                    log_error() << "FATAL: loopback readback is '" << readback
                                << "', expected '1' — loopback not confirmed active\n";
                    iio_context_destroy(ctx);
                    return 1;
                }
            }
        }

        // Destroy context — loopback setting persists in the kernel driver.
        // No IIO context will be alive during flowgraph execution.
        iio_context_destroy(ctx);
        log_info() << "Loopback setup context released\n";
    }

    // Read input WAV (samples loaded via libsndfile for deterministic float type)
    int input_audio_rate = 0;
    long long input_frames = 0;
    std::vector<float> input_samples;
    double duration_sec = read_input_wav(cfg.input_wav, input_audio_rate, input_frames, input_samples);
    if (duration_sec < 0) {
        restore_loopback(cfg.uri, prev_loopback);
        return 1;
    }
    log_info() << "Input frames:     " << input_frames << " (" << duration_sec << " sec)\n";

    // Cap duration to stay within downlink budget (sc16: 4 bytes per complex sample)
    // File is written at effective_rate (post-LPF decimation)
    const long long max_iq_bytes = (long long)cfg.max_iq_mb * 1024LL * 1024;
    double max_duration = (double)max_iq_bytes / (cfg.effective_rate * 4.0);
    log_info() << "Capture cap:      " << max_duration << "s ("
               << cfg.max_iq_mb << " MiB @ "
               << cfg.effective_rate << " SPS sc16)\n";
    if (duration_sec > max_duration) {
        log_warning() << "Input duration " << duration_sec << "s exceeds cap, truncating\n";
        duration_sec = max_duration;
    }

    // Snap to sample boundary at effective_rate for deterministic file sizes
    long long snap_samples = (long long)(duration_sec * cfg.effective_rate);
    if (snap_samples <= 0) {
        log_error() << "Effective duration must be > 0 (increase max_iq_mb or input length)\n";
        restore_loopback(cfg.uri, prev_loopback);
        return 1;
    }
    // Align to RX decimation multiple *before* computing TX need_in,
    // otherwise we can falsely reject "too short" WAVs.
    unsigned long rx_gcd_m = std::gcd((unsigned long)cfg.effective_rate, (unsigned long)cfg.audio_rate);
    unsigned long rx_decim_m = cfg.effective_rate / rx_gcd_m;
    {
        long long rem = snap_samples % (long long)rx_decim_m;
        if (rem != 0) {
            long long snapped = snap_samples - rem;
            if (snapped <= 0) {
                log_error() << "Effective duration too short after RX alignment snap\n";
                restore_loopback(cfg.uri, prev_loopback);
                return 1;
            }
            log_info() << "Aligning snap_samples to RX decim: " << snap_samples
                       << " -> " << snapped << " (rx_decim=" << rx_decim_m << ")\n";
            snap_samples = snapped;
        }
    }

    log_info() << "Effective duration: " << (double)snap_samples / cfg.effective_rate << " sec ("
               << snap_samples << " samples @ " << cfg.effective_rate << " Hz)\n";

    // TX input is not truncated — tx_head in the flowgraph caps output to tx_samples.
    // Truncating here would risk starving the resampler due to filter group delay.
    // The full (already duration-capped) input is passed; any excess is simply unused.

    int rc;
    try {
        rc = run_loopback(cfg, input_audio_rate, snap_samples, input_samples, prev_loopback);
    } catch (const std::exception& e) {
        log_error() << e.what() << "\n";
        rc = 1;
    }

    // Restore loopback via a fresh context (no conflicts with GNU Radio)
    restore_loopback(cfg.uri, prev_loopback);
    return rc;
}

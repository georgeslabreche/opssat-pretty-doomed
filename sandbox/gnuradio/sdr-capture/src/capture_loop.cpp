/*
 * capture_loop.cpp - GNU Radio IIO RF Capture for OPS-SAT PRETTY
 *
 * Continuous RX capture loop for live RF reception.
 * Captures FM-demodulated audio and sc16 I/Q from AD9361 SDR.
 *
 * DSP chain:
 *   IIO src (fc32, sdr_rate e.g. 2.4 MSPS)
 *     -> Decimating LPF (85 kHz cutoff, decim=12 -> 200 kSPS effective)
 *       +-> head -> complex_to_interleaved_short -> file_sink (.sc16)
 *       +-> FM demod -> rational_resampler (200k->16k)
 *           -> bandpass (300-3400 Hz) -> head -> wav_sink
 *   Post-processing: RMS normalization to -20 dBFS
 *
 * Usage: capture_loop -o output.wav -d duration_seconds [-c config.cfg]
 */

#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <vector>

#include <gnuradio/top_block.h>
#include <gnuradio/blocks/wavfile_sink.h>
#include <gnuradio/blocks/file_sink.h>
#include <gnuradio/blocks/head.h>
#include <gnuradio/blocks/short_to_float.h>
#include <gnuradio/blocks/float_to_complex.h>
#include <gnuradio/blocks/complex_to_interleaved_short.h>
#include <gnuradio/analog/quadrature_demod_cf.h>
#include <gnuradio/filter/rational_resampler.h>
#include <gnuradio/filter/fir_filter_blk.h>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/iio/device_source.h>
#include <numeric>  // for std::gcd

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
struct CaptureConfig {
    // SDR hardware
    long long frequency = 1296000000;  // overridden by config: frequency
    long sdr_rate = 2400000;           // overridden by config: sdr_rate (AD9361 hardware rate)
    int decimation = 12;               // overridden by config: decimation (LPF decimation factor)
    long rf_bandwidth = 200000;        // overridden by config: rf_bandwidth (AD9361 analog filter)
    double gain = 50.0;               // overridden by config: gain
    double fm_deviation = 5000.0;     // overridden by config: fm_deviation
    std::string uri = "local:";       // overridden by config: uri

    // Derived (set after parsing)
    long effective_rate = 200000;      // sdr_rate / decimation

    // Capture
    int duration = 20;                // overridden by config: duration
    int max_iq_mb = 20;              // overridden by config: max_iq_mb

    // Audio
    int audio_rate = 16000;           // overridden by config: audio_rate
    double bandpass_low = 300.0;      // overridden by config: bandpass_low
    double bandpass_high = 3400.0;    // overridden by config: bandpass_high

    // LPF
    double lpf_cutoff = 85000.0;     // overridden by config: lpf_cutoff
    double lpf_transition = 15000.0; // overridden by config: lpf_transition

    // Output
    std::string output_wav = "capture.wav";

    // Testing
    bool min_readback = false;          // --min-readback: downgrade sample rate check to warning (emulator)
};

bool load_config(const std::string& path, CaptureConfig& cfg) {
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
            if (key == "frequency") cfg.frequency = std::stoll(value);
            else if (key == "sdr_rate") cfg.sdr_rate = std::stol(value);
            else if (key == "decimation") cfg.decimation = std::stoi(value);
            else if (key == "rf_bandwidth") cfg.rf_bandwidth = std::stol(value);
            else if (key == "gain") cfg.gain = std::stod(value);
            else if (key == "fm_deviation") cfg.fm_deviation = std::stod(value);
            else if (key == "uri") cfg.uri = value;
            else if (key == "duration") cfg.duration = std::stoi(value);
            else if (key == "max_iq_mb") cfg.max_iq_mb = std::stoi(value);
            else if (key == "audio_rate") cfg.audio_rate = std::stoi(value);
            else if (key == "bandpass_low") cfg.bandpass_low = std::stod(value);
            else if (key == "bandpass_high") cfg.bandpass_high = std::stod(value);
            else if (key == "lpf_cutoff") cfg.lpf_cutoff = std::stod(value);
            else if (key == "lpf_transition") cfg.lpf_transition = std::stod(value);
            else if (key == "min_readback") cfg.min_readback = (value == "true" || value == "1");
            // Keys consumed by the run script, not by capture_loop
            else if (key == "captures") { /* ignored */ }
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
    std::cout << "OPS-SAT SDR RF Capture\n"
              << "======================\n\n"
              << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  -c, --config    Config file path (KEY=VALUE format)\n"
              << "  -o, --output    Output WAV file (default: capture.wav)\n"
              << "  -d, --duration  Capture duration in seconds (default: 20)\n"
              << "  -u, --uri       IIO URI (default: local:)\n"
              << "  -f, --freq      Frequency in Hz (default: 1296000000)\n"
              << "  -g, --gain      RX gain in dB (default: 50)\n"
              << "  -e, --deviation FM deviation in Hz (default: 5000)\n"
              << "      --min-readback  Minimal readback: downgrade sample rate check to warning (emulator)\n"
              << "  -h, --help      Show this help\n";
}

// Parse config file (-c) then command-line overrides. Returns 0 on success,
// 1 on error, 2 if --help was requested (caller should exit 0).
int parse_args(int argc, char* argv[], CaptureConfig& cfg) {
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
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            cfg.output_wav = argv[++i];
        } else if ((arg == "-d" || arg == "--duration") && i + 1 < argc) {
            cfg.duration = std::stoi(argv[++i]);
        } else if ((arg == "-u" || arg == "--uri") && i + 1 < argc) {
            cfg.uri = argv[++i];
        } else if ((arg == "-f" || arg == "--freq") && i + 1 < argc) {
            cfg.frequency = std::stoll(argv[++i]);
        } else if ((arg == "-g" || arg == "--gain") && i + 1 < argc) {
            cfg.gain = std::stod(argv[++i]);
        } else if ((arg == "-e" || arg == "--deviation") && i + 1 < argc) {
            cfg.fm_deviation = std::stod(argv[++i]);
            if (cfg.fm_deviation <= 0) {
                log_error() << "FM deviation must be > 0\n";
                return 1;
            }
        } else if (arg == "--min-readback") {
            cfg.min_readback = true;
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
    if (cfg.duration <= 0) {
        log_error() << "Duration must be > 0\n";
        return 1;
    }
    if (cfg.sdr_rate <= 0) {
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
    if (cfg.effective_rate <= 0) {
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

void log_config(const CaptureConfig& cfg, const std::string& iq_file,
                long long audio_samples, long long iq_samples) {
    log_info() << "OPS-SAT PRETTY SDR RF Capture\n";
    log_info() << "Output:      " << cfg.output_wav << "\n";
    log_info() << "I/Q file:    " << iq_file << "\n";
    log_info() << "URI:         " << cfg.uri << "\n";
    log_info() << "Frequency:   " << cfg.frequency / 1e6 << " MHz\n";
    log_info() << "SDR rate:    " << cfg.sdr_rate << " Hz (AD9361 hardware)\n";
    log_info() << "Decimation:  " << cfg.decimation << "x\n";
    log_info() << "Eff. rate:   " << cfg.effective_rate << " Hz (post-LPF)\n";
    log_info() << "RF BW:       " << cfg.rf_bandwidth << " Hz\n";
    log_info() << "Audio rate:  " << cfg.audio_rate << " Hz\n";
    log_info() << "Gain:        " << cfg.gain << " dB\n";
    log_info() << "FM Dev:      " << cfg.fm_deviation << " Hz\n";
    log_info() << "Duration:    " << cfg.duration << " seconds\n";
    log_info() << "LPF cutoff:  " << cfg.lpf_cutoff << " Hz\n";
    log_info() << "LPF trans:   " << cfg.lpf_transition << " Hz\n";
    log_info() << "Bandpass:    " << cfg.bandpass_low << " - " << cfg.bandpass_high << " Hz\n";
    log_info() << "I/Q scale:   " << IQ_SCALE << " (|1.0| -> " << (int)IQ_SCALE << " int16)\n";
    log_info() << "Samples:     " << audio_samples << " audio, " << iq_samples << " I/Q\n";
    log_info() << "Expected .sc16:  " << (double)iq_samples * 4.0 / (1024.0 * 1024.0) << " MiB\n";
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
bool readback_iio_config(struct iio_device* phy, const CaptureConfig& cfg, bool strict) {
    // RX LO frequency
    struct iio_channel* rx_lo = iio_device_find_channel(phy, "altvoltage0", true);
    if (rx_lo) {
        std::string val = iio_attr_read_str(rx_lo, "frequency");
        if (!val.empty()) {
            log_info() << "AD9361 RX LO readback: " << val << " Hz\n";
            try {
                long long actual_freq = std::stoll(trim(val));
                if (actual_freq != cfg.frequency) {
                    log_warning() << "RX LO mismatch — requested " << cfg.frequency
                                << " Hz, got " << actual_freq << " Hz\n";
                }
            } catch (const std::exception& e) {
                log_warning() << "could not parse RX LO frequency: " << val
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
                if (actual_rate != cfg.sdr_rate) {
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
                if (std::abs(actual_gain - cfg.gain) > 0.5) {
                    log_warning() << "RX gain mismatch — requested " << cfg.gain
                                << " dB, got " << actual_gain << " dB\n";
                }
            } catch (const std::exception& e) {
                log_warning() << "could not parse hardwaregain: " << val
                              << " (" << e.what() << ")\n";
            }
        }

        val = iio_attr_read_str(rx0, "rssi");
        if (!val.empty()) {
            log_info() << "AD9361 RX RSSI: " << val << "\n";
        }
    }

    return true;
}

// Check sc16 file for clipping and peak magnitude. Reads first and last
// `check_seconds` worth of samples. Sets clip_rate and peak_abs (0-32768 scale).
void check_sc16_quality(const std::string& path, long sample_rate,
                        double& clip_rate, int& peak_abs, double check_seconds = 2.0) {
    clip_rate = -1.0;
    peak_abs = 0;

    if (sample_rate <= 0) return;

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

    // Read all samples
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

    // Scale to target RMS
    double scale = target_rms / rms;
    log_info() << "RMS normalize: rms=" << rms << ", scale=" << scale
               << ", target=" << target_dbfs << " dBFS\n";

    for (size_t i = 0; i < samples.size(); i++) {
        float s = samples[i] * (float)scale;
        // Soft-clip at +/-1.0
        if (s > 1.0f) s = 1.0f;
        else if (s < -1.0f) s = -1.0f;
        samples[i] = s;
    }

    // Rewrite WAV
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

// Build GNU Radio flowgraph, run capture, then post-process audio.
// interp/decim are the GCD-reduced resampler ratio (effective_rate -> audio_rate).
int run_capture(const CaptureConfig& cfg,
                const std::string& iq_file,
                long long audio_samples, long long iq_samples,
                unsigned long interpolation, unsigned long decimation) {
    if (cfg.effective_rate <= 0 || cfg.audio_rate <= 0) {
        log_error() << "Invalid rates: effective_rate and audio_rate must be > 0\n";
        return 1;
    }

    log_info() << "Resample: " << cfg.effective_rate << " -> " << cfg.audio_rate
               << " (interp=" << interpolation << ", decim=" << decimation << ")\n";

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

    // Design audio bandpass taps
    std::vector<float> bp_taps = gr::filter::firdes::band_pass(
        1.0, cfg.audio_rate, cfg.bandpass_low, cfg.bandpass_high, 200.0,
        gr::fft::window::WIN_HAMMING
    );
    log_info() << "Bandpass taps: " << bp_taps.size() << "\n";

    // --- Build flowgraph ---
    // Wrap construction + connection + start in try/catch — GNU Radio blocks can throw
    // on bad parameters, missing IIO devices, file I/O errors, etc.
    gr::top_block_sptr tb;
    gr::blocks::head::sptr iq_head, head;

    try {
        log_info() << "Building flowgraph...\n";
        tb = gr::make_top_block("capture_loop");

        // IIO RX source — uses device_source directly instead of
        // fmcomms2_source_fc32 to avoid the overflow-check thread that
        // crashes when FPGA register reads are unsupported (the thread
        // throws std::runtime_error → std::terminate).
        gr::iio::iio_param_vec_t ad9361_params;
        ad9361_params.emplace_back("out_altvoltage0_RX_LO_frequency",
                                   static_cast<unsigned long long>(cfg.frequency));
        ad9361_params.emplace_back("in_voltage0_sampling_frequency",
                                   static_cast<unsigned long>(cfg.sdr_rate));
        ad9361_params.emplace_back("in_voltage0_rf_bandwidth",
                                   static_cast<unsigned long>(cfg.rf_bandwidth));
        ad9361_params.emplace_back("in_voltage0_gain_control_mode=manual");
        ad9361_params.emplace_back("in_voltage0_hardwaregain", cfg.gain);
        ad9361_params.emplace_back("in_voltage_quadrature_tracking_en", 1);
        ad9361_params.emplace_back("in_voltage_rf_dc_offset_tracking_en", 1);
        ad9361_params.emplace_back("in_voltage_bb_dc_offset_tracking_en", 1);

        std::vector<std::string> iio_channels = {"voltage0", "voltage1"};
        auto iio_src = gr::iio::device_source::make(
            cfg.uri, "cf-ad9361-lpc", iio_channels, "ad9361-phy",
            ad9361_params, 0x8000);

        // Readback validation with a temporary IIO context.
        // Destroyed before tb->start() so only GNU Radio's internal context(s)
        // exist during flowgraph execution.
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

        // Convert device_source shorts to gr_complex.
        // device_source outputs int16 per IIO channel (voltage0=I, voltage1=Q).
        // AD9361 ADC is 12-bit: divide by 2048.0 to normalize to ≈±1.0
        // (same conversion that fmcomms2_source_fc32::work() performs internally).
        auto i_s2f  = gr::blocks::short_to_float::make(1, 2048.0f);
        auto q_s2f  = gr::blocks::short_to_float::make(1, 2048.0f);
        auto to_fc32 = gr::blocks::float_to_complex::make(1);

        // DSP blocks — LPF decimates from sdr_rate to effective_rate
        auto lpf       = gr::filter::fir_filter_ccf::make(cfg.decimation, lpf_taps);
        auto to_short  = gr::blocks::complex_to_interleaved_short::make(false, IQ_SCALE);
        iq_head        = gr::blocks::head::make(sizeof(gr_complex), iq_samples);
        // iq_samples counts complex samples at effective_rate (post-LPF);
        // sink writes 2 int16 per sample (I,Q) => 4 bytes/sample
        auto iq_sink   = gr::blocks::file_sink::make(sizeof(short), iq_file.c_str(), false);
        // FM demod operates at effective_rate (post-LPF output)
        auto fm_demod  = gr::analog::quadrature_demod_cf::make(
            cfg.effective_rate / (2.0 * M_PI * cfg.fm_deviation)
        );
        // Resample from effective_rate to audio_rate
        auto resampler = gr::filter::rational_resampler_fff::make(interpolation, decimation);
        auto bandpass  = gr::filter::fir_filter_fff::make(1, bp_taps);
        head           = gr::blocks::head::make(sizeof(float), audio_samples);
        auto wav_sink  = gr::blocks::wavfile_sink::make(
            cfg.output_wav.c_str(), 1, cfg.audio_rate,
            gr::blocks::FORMAT_WAV, gr::blocks::FORMAT_PCM_16
        );

        // Connect: IIO src (shorts) → fc32 conversion → LPF → branches
        tb->connect(iio_src, 0, i_s2f, 0);
        tb->connect(iio_src, 1, q_s2f, 0);
        tb->connect(i_s2f, 0, to_fc32, 0);
        tb->connect(q_s2f, 0, to_fc32, 1);
        tb->connect(to_fc32, 0, lpf, 0);

        // Branch 1: LPF -> head -> sc16 conversion -> file_sink (.sc16)
        tb->connect(lpf, 0, iq_head, 0);
        tb->connect(iq_head, 0, to_short, 0);
        tb->connect(to_short, 0, iq_sink, 0);

        // Branch 2: LPF -> FM demod -> resample -> bandpass -> head -> wav_sink
        tb->connect(lpf, 0, fm_demod, 0);
        tb->connect(fm_demod, 0, resampler, 0);
        tb->connect(resampler, 0, bandpass, 0);
        tb->connect(bandpass, 0, head, 0);
        tb->connect(head, 0, wav_sink, 0);

        // --- Start ---
        log_info() << "Starting capture for " << cfg.duration << " seconds...\n";

        tb->start();
    } catch (const std::exception& e) {
        log_error() << "FATAL: flowgraph build/start failed: " << e.what() << "\n";
        return 1;
    }

    auto start_time = std::chrono::steady_clock::now();
    int timeout_sec = cfg.duration * 2 + 10;
    log_info() << "Timeout:  " << timeout_sec << " seconds (2x duration + 10)\n";

    bool timed_out = false;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        bool audio_done = head->nitems_written(0) >= (uint64_t)audio_samples;
        bool iq_done = iq_head->nitems_written(0) >= (uint64_t)iq_samples;
        if (audio_done && iq_done) {
            log_info() << "Capture complete (audio: " << head->nitems_written(0)
                       << ", I/Q: " << iq_head->nitems_written(0) << " samples)\n";
            break;
        }

        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed >= std::chrono::seconds(timeout_sec)) {
            auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            log_error() << "Timeout after " << elapsed_sec << " seconds"
                        << " (audio: " << head->nitems_written(0) << "/" << audio_samples
                        << ", I/Q: " << iq_head->nitems_written(0) << "/" << iq_samples << ")\n";
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
        log_error() << "Capture may have failed - check SDR connection / IIO buffers\n";
    }

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
    long long actual_iq_written = (long long)iq_head->nitems_written(0);
    long long actual_audio_written = (long long)head->nitems_written(0);

    log_info() << (early_stop ? "Capture stopped early" : "Capture complete") << "\n";
    log_info() << "Wall time: " << total_sec << " seconds\n";
    log_info() << "Audio:     " << cfg.output_wav << "\n";
    log_info() << "I/Q:       " << iq_file << "\n";
    if (early_stop) {
        log_info() << "Partial:   " << actual_iq_written << "/" << iq_samples << " I/Q, "
                   << actual_audio_written << "/" << audio_samples << " audio\n";
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
                        << "% exceeds 1% — RX may be saturating (reduce gain)\n";
        }
    }

    if (timed_out) return 2;
    if (interrupted) return 128 + g_signal_received;  // conventional: 130 for SIGINT, 143 for SIGTERM
    if (hard_fail) return 1;
    return 0;
}

int main(int argc, char* argv[]) {
    CaptureConfig cfg;

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

    // Cap duration to stay within downlink budget (sc16: 4 bytes per complex sample)
    // File is written at effective_rate (post-LPF decimation)
    const long long max_iq_bytes = (long long)cfg.max_iq_mb * 1024LL * 1024;
    int max_duration = (int)(max_iq_bytes / ((long long)cfg.effective_rate * 4LL));
    log_info() << "Capture cap:  " << max_duration << "s ("
               << cfg.max_iq_mb << " MiB @ "
               << cfg.effective_rate << " SPS sc16)\n";
    if (cfg.duration > max_duration) {
        log_warning() << "duration " << cfg.duration << "s exceeds cap, truncating\n";
        cfg.duration = max_duration;
    }
    if (cfg.duration <= 0) {
        log_error() << "Effective duration must be > 0 (increase max_iq_mb or duration)\n";
        return 1;
    }
    log_info() << "Effective duration: " << cfg.duration << " sec\n";

    // I/Q samples counted at effective_rate (post-LPF output rate)
    long long iq_samples = (long long)cfg.duration * (long long)cfg.effective_rate;
    // Use actual resampler ratio (interp/decim via GCD) for exact integer math.
    // Resampler operates on effective_rate -> audio_rate.
    unsigned long gcd_rates = std::gcd((unsigned long)cfg.effective_rate, (unsigned long)cfg.audio_rate);
    unsigned long interp = cfg.audio_rate / gcd_rates;
    unsigned long decim = cfg.effective_rate / gcd_rates;
    // Sanity cap: absurd ratios produce huge FIR filter tap counts and blow memory
    if (interp > 1000 || decim > 1000) {
        log_error() << "Resampler ratio " << interp << "/" << decim
                    << " too large (effective_rate=" << cfg.effective_rate
                    << ", audio_rate=" << cfg.audio_rate
                    << ") — choose rates with a reasonable GCD\n";
        return 1;
    }
    if (iq_samples % (long long)decim != 0) {
        long long snapped = (iq_samples / (long long)decim) * (long long)decim;
        log_info() << "Snapping iq_samples " << iq_samples << " -> " << snapped
                   << " (multiple of decim=" << decim << ")\n";
        iq_samples = snapped;
        if (iq_samples <= 0) {
            log_error() << "iq_samples snapped to 0 — duration too short for decim\n";
            return 1;
        }
    }
    long long audio_samples = (iq_samples / (long long)decim) * (long long)interp;
    std::string iq_file = make_iq_filename(cfg.output_wav);

    log_config(cfg, iq_file, audio_samples, iq_samples);

    int rc;
    try {
        rc = run_capture(cfg, iq_file, audio_samples, iq_samples, interp, decim);
    } catch (const std::exception& e) {
        log_error() << e.what() << "\n";
        rc = 1;
    }

    return rc;
}

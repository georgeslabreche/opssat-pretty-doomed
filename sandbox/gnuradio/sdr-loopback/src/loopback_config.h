#ifndef LOOPBACK_CONFIG_H
#define LOOPBACK_CONFIG_H

#include <string>
#include <iostream>
#include "pretty_log.h"
#include "pretty_config.h"

using namespace pretty;

// AD9361 hardware limits
static constexpr unsigned long AD9361_SAMPLE_RATE_MIN = 2083000;    // Hz
static constexpr unsigned long AD9361_SAMPLE_RATE_MAX = 61440000;   // Hz
static constexpr unsigned long AD9361_RF_BANDWIDTH_MIN = 200000;    // Hz
static constexpr unsigned long AD9361_RF_BANDWIDTH_MAX = 56000000;  // Hz

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
    int timeout_multiplier = 5;         // config: timeout_multiplier — timeout = duration * N + 10 (default 5 for ARM CPU headroom)
    int iio_buffer_size = 0x8000;       // config: iio_buffer_size — IIO DMA buffer size (samples per channel)
    bool use_file_loopback = false;     // config: use_file_loopback — bypass IIO, test DSP chain with file I/O
    std::string loopback_file = "loopback_iq.raw";  // config: loopback_file — intermediate gr_complex file

    // Feature flags — toggle flowgraph blocks for isolation testing on the EM
    bool enable_tx = true;              // config: enable_tx — disable to capture RX-only (no TX path)
    bool enable_fm_mod = true;          // config: enable_fm_mod — disable to send raw audio as I/Q
    bool enable_fm_demod = true;        // config: enable_fm_demod — disable to skip FM demodulation
    bool enable_lpf = true;             // config: enable_lpf — disable to skip LPF decimation
    bool enable_resampler_tx = true;    // config: enable_resampler_tx — disable TX resampler
    bool enable_resampler_rx = true;    // config: enable_resampler_rx — disable RX resampler
    bool enable_bandpass = true;        // config: enable_bandpass — disable bandpass filter
    bool enable_spectrogram = true;     // config: enable_spectrogram — generate spectrogram BMP
    bool enable_constellation = true;   // config: enable_constellation — generate constellation BMP

    // TX execution mode: controls how TX interacts with RX streaming
    //   "default"   — TX and RX start simultaneously (current behavior)
    //   "staggered" — prepend silence to TX so RX streams first
    //   "cyclic"    — TX loops a single DMA buffer (no continuous CPU refill)
    std::string tx_mode = "default";    // config: tx_mode
    int tx_startup_delay = 2;           // config: tx_startup_delay — seconds of TX silence before audio (staggered mode)
};

inline bool load_config(const std::string& path, LoopbackConfig& cfg) {
    auto result = load_config_map(path);
    if (!result.ok) return false;

    for (const auto& [key, value] : result.values) {
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
            else if (key == "timeout_multiplier") cfg.timeout_multiplier = std::stoi(value);
            else if (key == "iio_buffer_size") cfg.iio_buffer_size = std::stoi(value);
            else if (key == "use_file_loopback") cfg.use_file_loopback = (value == "true" || value == "1");
            else if (key == "loopback_file") cfg.loopback_file = value;
            else if (key == "enable_tx") cfg.enable_tx = (value == "true" || value == "1");
            else if (key == "enable_fm_mod") cfg.enable_fm_mod = (value == "true" || value == "1");
            else if (key == "enable_fm_demod") cfg.enable_fm_demod = (value == "true" || value == "1");
            else if (key == "enable_lpf") cfg.enable_lpf = (value == "true" || value == "1");
            else if (key == "enable_resampler_tx") cfg.enable_resampler_tx = (value == "true" || value == "1");
            else if (key == "enable_resampler_rx") cfg.enable_resampler_rx = (value == "true" || value == "1");
            else if (key == "enable_bandpass") cfg.enable_bandpass = (value == "true" || value == "1");
            else if (key == "enable_spectrogram") cfg.enable_spectrogram = (value == "true" || value == "1");
            else if (key == "enable_constellation") cfg.enable_constellation = (value == "true" || value == "1");
            else if (key == "tx_mode") cfg.tx_mode = value;
            else if (key == "tx_startup_delay") cfg.tx_startup_delay = std::stoi(value);
            else log_warning() << path << ": unknown config key: " << key << "\n";
        } catch (const std::exception& e) {
            log_error() << path << ": parse error: " << key << "=" << value
                        << " (" << e.what() << ")\n";
            return false;
        }
    }

    return true;
}

inline void print_usage(const char* prog) {
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
inline int parse_args(int argc, char* argv[], LoopbackConfig& cfg) {
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
    // AD9361 hardware range checks (skip in file loopback mode — no real hardware)
    if (!cfg.use_file_loopback) {
        if (cfg.sdr_rate < AD9361_SAMPLE_RATE_MIN || cfg.sdr_rate > AD9361_SAMPLE_RATE_MAX) {
            log_error() << "sdr_rate " << cfg.sdr_rate << " Hz out of AD9361 range ("
                        << AD9361_SAMPLE_RATE_MIN << " - " << AD9361_SAMPLE_RATE_MAX << " Hz)\n";
            return 1;
        }
        if (cfg.rf_bandwidth < AD9361_RF_BANDWIDTH_MIN || cfg.rf_bandwidth > AD9361_RF_BANDWIDTH_MAX) {
            log_error() << "rf_bandwidth " << cfg.rf_bandwidth << " Hz out of AD9361 range ("
                        << AD9361_RF_BANDWIDTH_MIN << " - " << AD9361_RF_BANDWIDTH_MAX << " Hz)\n";
            return 1;
        }
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

inline void log_config(const LoopbackConfig& cfg) {
    log_info() << "OPS-SAT PRETTY IIO Loopback Test\n";
    log_info() << "Input:       " << cfg.input_wav << "\n";
    log_info() << "Output:      " << cfg.output_wav << "\n";
    if (cfg.use_file_loopback) {
        log_info() << "Mode:        FILE LOOPBACK (no IIO hardware)\n";
        log_info() << "Loopback:    " << cfg.loopback_file << "\n";
    } else {
        log_info() << "URI:         " << cfg.uri << "\n";
    }
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
    log_info() << "IIO buf:     " << cfg.iio_buffer_size << " samples\n";
    // Log any disabled feature flags
    if (!cfg.enable_tx) log_info() << "FLAG:        enable_tx=false (TX path disabled)\n";
    if (!cfg.enable_fm_mod) log_info() << "FLAG:        enable_fm_mod=false\n";
    if (!cfg.enable_fm_demod) log_info() << "FLAG:        enable_fm_demod=false\n";
    if (!cfg.enable_lpf) log_info() << "FLAG:        enable_lpf=false\n";
    if (!cfg.enable_resampler_tx) log_info() << "FLAG:        enable_resampler_tx=false\n";
    if (!cfg.enable_resampler_rx) log_info() << "FLAG:        enable_resampler_rx=false\n";
    if (!cfg.enable_bandpass) log_info() << "FLAG:        enable_bandpass=false\n";
    if (!cfg.enable_spectrogram) log_info() << "FLAG:        enable_spectrogram=false\n";
    if (!cfg.enable_constellation) log_info() << "FLAG:        enable_constellation=false\n";
    if (cfg.tx_mode != "default") {
        log_info() << "TX mode:     " << cfg.tx_mode;
        if (cfg.tx_mode == "staggered") {
            log_info() << " (delay=" << cfg.tx_startup_delay << "s)";
        }
        log_info() << "\n";
    }
}

#endif // LOOPBACK_CONFIG_H

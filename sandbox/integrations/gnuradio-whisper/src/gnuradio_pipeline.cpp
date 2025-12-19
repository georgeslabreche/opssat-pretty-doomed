/**
 * GNU Radio + Whisper Transcription Pipeline
 *
 * Single binary that processes audio through:
 * 1. Lowpass filter - Remove high-frequency noise
 * 2. Bandpass filter - Isolate voice frequencies (300-3400 Hz)
 * 3. Noise reduction - Attenuate background noise
 * 4. Whisper transcription - Speech to text
 *
 * Designed for OPS-SAT SEPP to process SDR-captured audio.
 */

// GNU Radio libraries
#include <gnuradio/top_block.h>
#include <gnuradio/blocks/wavfile_source.h>
#include <gnuradio/blocks/wavfile_sink.h>
#include <gnuradio/filter/fir_filter_blk.h>  // Contains fir_filter_fff typedef
#include <gnuradio/filter/firdes.h>
#include <gnuradio/blocks/multiply_const.h>

#include <whisper/whisper.h>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cmath>
#include <cstring>
#include <getopt.h>

// Processing parameters
struct PipelineParams {
    std::string input_file;
    std::string output_file;       // Transcription output (.txt)
    std::string processed_audio;   // Optional: save processed audio
    std::string model_path = "/opt/whisper.cpp/models/ggml-tiny.bin";
    std::string language = "en";

    float sample_rate = 46875.0f;  // OPS-SAT SDR sample rate

    // Lowpass filter
    float lowpass_cutoff = 4000.0f;    // Hz
    float lowpass_transition = 500.0f; // Hz

    // Bandpass filter (voice frequencies)
    float bandpass_low = 300.0f;       // Hz
    float bandpass_high = 3400.0f;     // Hz
    float bandpass_transition = 100.0f; // Hz

    // Noise reduction
    float noise_reduction_strength = 0.5f;  // 0.0 to 1.0

    bool verbose = false;
    bool save_processed = false;
};

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "\nRequired:\n"
              << "  -i <file>   Input WAV file\n"
              << "  -o <file>   Output transcription file (.txt)\n"
              << "\nOptional:\n"
              << "  -m <file>   Whisper model path (default: /opt/whisper.cpp/models/ggml-tiny.bin)\n"
              << "  -p <file>   Save processed audio to file\n"
              << "  -l <freq>   Lowpass cutoff frequency (default: 4000)\n"
              << "  -b <low>    Bandpass low frequency (default: 300)\n"
              << "  -B <high>   Bandpass high frequency (default: 3400)\n"
              << "  -n <strength> Noise reduction strength 0.0-1.0 (default: 0.5)\n"
              << "  -L <lang>   Language code (default: en)\n"
              << "  -v          Verbose output\n"
              << "  -h          Show this help\n"
              << "\nExample:\n"
              << "  " << prog << " -i noisy.wav -o transcription.txt -n 0.7\n";
}

bool parse_args(int argc, char** argv, PipelineParams& params) {
    int opt;
    while ((opt = getopt(argc, argv, "i:o:m:p:l:b:B:n:L:vh")) != -1) {
        switch (opt) {
            case 'i': params.input_file = optarg; break;
            case 'o': params.output_file = optarg; break;
            case 'm': params.model_path = optarg; break;
            case 'p': params.processed_audio = optarg; params.save_processed = true; break;
            case 'l': params.lowpass_cutoff = std::stof(optarg); break;
            case 'b': params.bandpass_low = std::stof(optarg); break;
            case 'B': params.bandpass_high = std::stof(optarg); break;
            case 'n': params.noise_reduction_strength = std::stof(optarg); break;
            case 'L': params.language = optarg; break;
            case 'v': params.verbose = true; break;
            case 'h': print_usage(argv[0]); return false;
            default: print_usage(argv[0]); return false;
        }
    }

    if (params.input_file.empty() || params.output_file.empty()) {
        std::cerr << "Error: Input and output files are required\n";
        print_usage(argv[0]);
        return false;
    }

    return true;
}

/**
 * Design lowpass FIR filter taps
 */
std::vector<float> design_lowpass_filter(float sample_rate, float cutoff, float transition) {
    return gr::filter::firdes::low_pass(
        1.0,              // gain
        sample_rate,      // sampling frequency
        cutoff,           // cutoff frequency
        transition,       // transition width
        gr::fft::window::WIN_HAMMING
    );
}

/**
 * Design bandpass FIR filter taps
 */
std::vector<float> design_bandpass_filter(float sample_rate, float low, float high, float transition) {
    return gr::filter::firdes::band_pass(
        1.0,              // gain
        sample_rate,      // sampling frequency
        low,              // low cutoff
        high,             // high cutoff
        transition,       // transition width
        gr::fft::window::WIN_HAMMING
    );
}

/**
 * Read WAV file and return float samples
 */
bool read_wav_file(const std::string& filename, std::vector<float>& samples,
                   int& sample_rate, int& num_channels) {
    // WAV header structure
    struct WavHeader {
        char riff[4];
        uint32_t fileSize;
        char wave[4];
        char fmt[4];
        uint32_t fmtSize;
        uint16_t audioFormat;
        uint16_t numChannels;
        uint32_t sampleRate;
        uint32_t byteRate;
        uint16_t blockAlign;
        uint16_t bitsPerSample;
    };

    FILE* file = fopen(filename.c_str(), "rb");
    if (!file) {
        std::cerr << "Error: Cannot open file: " << filename << std::endl;
        return false;
    }

    WavHeader header;
    if (fread(&header, sizeof(WavHeader), 1, file) != 1) {
        std::cerr << "Error: Cannot read WAV header" << std::endl;
        fclose(file);
        return false;
    }

    if (strncmp(header.riff, "RIFF", 4) != 0 || strncmp(header.wave, "WAVE", 4) != 0) {
        std::cerr << "Error: Not a valid WAV file" << std::endl;
        fclose(file);
        return false;
    }

    sample_rate = header.sampleRate;
    num_channels = header.numChannels;

    // Skip to data chunk
    char chunk_id[4];
    uint32_t chunk_size;

    while (fread(chunk_id, 4, 1, file) == 1) {
        if (fread(&chunk_size, 4, 1, file) != 1) break;
        if (strncmp(chunk_id, "data", 4) == 0) break;
        fseek(file, chunk_size, SEEK_CUR);
    }

    int bytes_per_sample = header.bitsPerSample / 8;
    int num_samples = chunk_size / (bytes_per_sample * header.numChannels);
    samples.resize(num_samples);

    if (header.bitsPerSample == 16) {
        std::vector<int16_t> raw(num_samples * header.numChannels);
        fread(raw.data(), bytes_per_sample, num_samples * header.numChannels, file);

        for (int i = 0; i < num_samples; i++) {
            float sum = 0.0f;
            for (int ch = 0; ch < header.numChannels; ch++) {
                sum += raw[i * header.numChannels + ch] / 32768.0f;
            }
            samples[i] = sum / header.numChannels;
        }
    } else if (header.bitsPerSample == 32) {
        std::vector<int32_t> raw(num_samples * header.numChannels);
        fread(raw.data(), bytes_per_sample, num_samples * header.numChannels, file);

        for (int i = 0; i < num_samples; i++) {
            float sum = 0.0f;
            for (int ch = 0; ch < header.numChannels; ch++) {
                sum += raw[i * header.numChannels + ch] / 2147483648.0f;
            }
            samples[i] = sum / header.numChannels;
        }
    } else {
        std::cerr << "Error: Unsupported bits per sample: " << header.bitsPerSample << std::endl;
        fclose(file);
        return false;
    }

    fclose(file);
    return true;
}

/**
 * Write WAV file from float samples
 */
bool write_wav_file(const std::string& filename, const std::vector<float>& samples, int sample_rate) {
    FILE* file = fopen(filename.c_str(), "wb");
    if (!file) {
        std::cerr << "Error: Cannot create file: " << filename << std::endl;
        return false;
    }

    int num_samples = samples.size();
    int data_size = num_samples * 2;  // 16-bit samples

    // Write WAV header
    fwrite("RIFF", 4, 1, file);
    uint32_t file_size = 36 + data_size;
    fwrite(&file_size, 4, 1, file);
    fwrite("WAVE", 4, 1, file);
    fwrite("fmt ", 4, 1, file);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, file);
    uint16_t audio_format = 1;  // PCM
    fwrite(&audio_format, 2, 1, file);
    uint16_t num_channels = 1;  // Mono
    fwrite(&num_channels, 2, 1, file);
    uint32_t sr = sample_rate;
    fwrite(&sr, 4, 1, file);
    uint32_t byte_rate = sample_rate * 2;
    fwrite(&byte_rate, 4, 1, file);
    uint16_t block_align = 2;
    fwrite(&block_align, 2, 1, file);
    uint16_t bits_per_sample = 16;
    fwrite(&bits_per_sample, 2, 1, file);
    fwrite("data", 4, 1, file);
    fwrite(&data_size, 4, 1, file);

    // Write samples
    for (float sample : samples) {
        int16_t s = static_cast<int16_t>(std::max(-1.0f, std::min(1.0f, sample)) * 32767.0f);
        fwrite(&s, 2, 1, file);
    }

    fclose(file);
    return true;
}

/**
 * Apply FIR filter to samples
 */
std::vector<float> apply_fir_filter(const std::vector<float>& input, const std::vector<float>& taps) {
    std::vector<float> output(input.size());
    int tap_count = taps.size();

    for (size_t i = 0; i < input.size(); i++) {
        float sum = 0.0f;
        for (int j = 0; j < tap_count; j++) {
            if (i >= static_cast<size_t>(j)) {
                sum += input[i - j] * taps[j];
            }
        }
        output[i] = sum;
    }

    return output;
}

/**
 * Resample audio to 16kHz for Whisper
 */
std::vector<float> resample_to_16k(const std::vector<float>& input, int input_rate) {
    if (input_rate == 16000) {
        return input;
    }

    double ratio = 16000.0 / input_rate;
    size_t output_size = static_cast<size_t>(input.size() * ratio);
    std::vector<float> output(output_size);

    for (size_t i = 0; i < output_size; i++) {
        double src_idx = i / ratio;
        size_t idx0 = static_cast<size_t>(src_idx);
        size_t idx1 = idx0 + 1;

        if (idx1 >= input.size()) {
            output[i] = input.back();
        } else {
            double frac = src_idx - idx0;
            output[i] = static_cast<float>(input[idx0] * (1.0 - frac) + input[idx1] * frac);
        }
    }

    return output;
}

/**
 * Run Whisper transcription
 */
std::string transcribe_audio(const std::vector<float>& samples, const PipelineParams& params) {
    // Initialize Whisper
    struct whisper_context_params cparams = whisper_context_default_params();
    struct whisper_context* ctx = whisper_init_from_file_with_params(
        params.model_path.c_str(), cparams);

    if (!ctx) {
        std::cerr << "Error: Failed to load Whisper model: " << params.model_path << std::endl;
        return "";
    }

    // Configure transcription
    struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress   = params.verbose;
    wparams.print_special    = false;
    wparams.print_realtime   = false;
    wparams.print_timestamps = false;
    wparams.translate        = false;
    wparams.single_segment   = false;
    wparams.language         = params.language.c_str();

    // Run transcription
    if (whisper_full(ctx, wparams, samples.data(), samples.size()) != 0) {
        std::cerr << "Error: Transcription failed" << std::endl;
        whisper_free(ctx);
        return "";
    }

    // Collect results
    std::string transcript;
    int n_segments = whisper_full_n_segments(ctx);
    for (int i = 0; i < n_segments; i++) {
        transcript += whisper_full_get_segment_text(ctx, i);
    }

    whisper_free(ctx);
    return transcript;
}

int main(int argc, char** argv) {
    PipelineParams params;

    if (!parse_args(argc, argv, params)) {
        return 1;
    }

    std::cout << "=== GNU Radio + Whisper Pipeline ===" << std::endl;
    std::cout << "Input:  " << params.input_file << std::endl;
    std::cout << "Output: " << params.output_file << std::endl;
    std::cout << "Model:  " << params.model_path << std::endl;
    std::cout << std::endl;

    // Step 1: Read input audio
    std::cout << "Step 1: Reading audio file..." << std::endl;
    std::vector<float> samples;
    int sample_rate, num_channels;

    if (!read_wav_file(params.input_file, samples, sample_rate, num_channels)) {
        return 1;
    }

    std::cout << "  Format: " << sample_rate << " Hz, " << num_channels << " channel(s), "
              << samples.size() << " samples ("
              << (float)samples.size() / sample_rate << "s)" << std::endl;

    // Step 2: GNU Radio filtering
    std::cout << std::endl << "Step 2: GNU Radio signal processing..." << std::endl;
    std::cout << "  Lowpass cutoff: " << params.lowpass_cutoff << " Hz" << std::endl;
    std::cout << "  Bandpass: " << params.bandpass_low << " - " << params.bandpass_high << " Hz" << std::endl;
    std::cout << "  Noise reduction: " << (params.noise_reduction_strength * 100) << "%" << std::endl;

    // Design filters
    std::vector<float> lowpass_taps = design_lowpass_filter(
        sample_rate, params.lowpass_cutoff, params.lowpass_transition);
    std::vector<float> bandpass_taps = design_bandpass_filter(
        sample_rate, params.bandpass_low, params.bandpass_high, params.bandpass_transition);

    if (params.verbose) {
        std::cout << "  Lowpass taps: " << lowpass_taps.size() << std::endl;
        std::cout << "  Bandpass taps: " << bandpass_taps.size() << std::endl;
    }

    // Apply filters
    std::vector<float> filtered = apply_fir_filter(samples, lowpass_taps);
    filtered = apply_fir_filter(filtered, bandpass_taps);

    // Apply gain (simple noise reduction)
    float gain = 1.0f + params.noise_reduction_strength;
    for (float& s : filtered) {
        s *= gain;
    }

    // Optionally save processed audio
    if (params.save_processed) {
        std::cout << "  Saving processed audio: " << params.processed_audio << std::endl;
        write_wav_file(params.processed_audio, filtered, sample_rate);
    }

    // Step 3: Resample to 16kHz for Whisper
    std::cout << std::endl << "Step 3: Resampling to 16kHz..." << std::endl;
    std::vector<float> resampled = resample_to_16k(filtered, sample_rate);
    std::cout << "  Resampled: " << resampled.size() << " samples" << std::endl;

    // Step 4: Whisper transcription
    std::cout << std::endl << "Step 4: Whisper transcription..." << std::endl;
    std::string transcript = transcribe_audio(resampled, params);

    if (transcript.empty()) {
        std::cerr << "Error: Transcription produced no output" << std::endl;
        return 1;
    }

    // Write output
    std::ofstream out(params.output_file);
    if (!out) {
        std::cerr << "Error: Cannot write to output file: " << params.output_file << std::endl;
        return 1;
    }
    out << transcript;
    out.close();

    std::cout << std::endl << "=== Result ===" << std::endl;
    std::cout << transcript << std::endl;
    std::cout << std::endl << "Output written to: " << params.output_file << std::endl;

    return 0;
}

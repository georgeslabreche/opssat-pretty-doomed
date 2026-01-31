// sherpa-stt — Speech-to-text using sherpa-onnx C API
//
// Reads a WAV file, transcribes it using the sherpa-onnx offline recognizer,
// and writes transcription output files.
//
// Usage:
//   sherpa-stt -i input.wav -m model/ -o output/ [-v]

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>

#include <sndfile.h>
#include "sherpa-onnx/c-api/c-api.h"

static bool verbose = false;

static std::string timestamp() {
    time_t now = time(nullptr);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return buf;
}

static void log(std::ofstream& logfile, const std::string& msg) {
    std::string line = timestamp() + " - " + msg;
    logfile << line << "\n";
    if (verbose) {
        std::cout << line << std::endl;
    }
}

static bool read_wav(const std::string& path, std::vector<float>& samples, int& sample_rate) {
    SF_INFO sf_info;
    memset(&sf_info, 0, sizeof(sf_info));

    SNDFILE* sf = sf_open(path.c_str(), SFM_READ, &sf_info);
    if (!sf) {
        std::cerr << "Error: Cannot open input file: " << path << std::endl;
        return false;
    }

    sample_rate = sf_info.samplerate;
    int total_frames = sf_info.frames;
    int channels = sf_info.channels;

    std::vector<float> raw(total_frames * channels);
    sf_count_t frames_read = sf_readf_float(sf, raw.data(), total_frames);
    sf_close(sf);

    if (frames_read != total_frames) {
        total_frames = frames_read;
    }

    // Downmix to mono
    samples.resize(total_frames);
    if (channels > 1) {
        for (int i = 0; i < total_frames; i++) {
            float sum = 0.0f;
            for (int c = 0; c < channels; c++) {
                sum += raw[i * channels + c];
            }
            samples[i] = sum / channels;
        }
    } else {
        samples = std::move(raw);
    }

    return true;
}

static std::string basename_no_ext(const std::string& path) {
    size_t slash = path.rfind('/');
    std::string name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
    size_t dot = name.rfind('.');
    if (dot != std::string::npos) {
        name = name.substr(0, dot);
    }
    return name;
}

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " -i <wav> -m <model_dir> -o <output_dir> [-v]\n"
              << "\n"
              << "Options:\n"
              << "  -i <file>   Input WAV file\n"
              << "  -m <dir>    Model directory (contains encoder, decoder, joiner, tokens.txt)\n"
              << "  -o <dir>    Output directory\n"
              << "  -v          Verbose output\n";
}

int main(int argc, char* argv[]) {
    std::string input_path;
    std::string model_dir;
    std::string output_dir;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            input_path = argv[++i];
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            model_dir = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (input_path.empty() || model_dir.empty() || output_dir.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    // Model file paths (fixed filenames within model dir)
    std::string encoder_path = model_dir + "/encoder-epoch-99-avg-1.int8.onnx";
    std::string decoder_path = model_dir + "/decoder-epoch-99-avg-1.onnx";
    std::string joiner_path  = model_dir + "/joiner-epoch-99-avg-1.int8.onnx";
    std::string tokens_path  = model_dir + "/tokens.txt";

    // Open log file
    std::string log_path = output_dir + "/sherpa-stt.log";
    std::ofstream logfile(log_path, std::ios::app);
    if (!logfile) {
        std::cerr << "Error: Cannot open log file: " << log_path << std::endl;
        return 1;
    }

    log(logfile, "==========================================");
    log(logfile, "Starting sherpa-stt");
    log(logfile, "==========================================");
    log(logfile, "Input: " + input_path);
    log(logfile, "Model dir: " + model_dir);
    log(logfile, "Output dir: " + output_dir);
    log(logfile, "Decoding method: modified_beam_search");

    // Read WAV file
    log(logfile, "Reading WAV file...");
    std::vector<float> samples;
    int sample_rate = 0;

    if (!read_wav(input_path, samples, sample_rate)) {
        log(logfile, "ERROR: Failed to read WAV file");
        return 1;
    }

    log(logfile, "  Sample rate: " + std::to_string(sample_rate) + " Hz");
    log(logfile, "  Samples: " + std::to_string(samples.size()));
    log(logfile, "  Duration: " + std::to_string(samples.size() / (float)sample_rate) + "s");

    // Configure recognizer
    log(logfile, "Initializing recognizer...");
    SherpaOnnxOfflineRecognizerConfig config;
    memset(&config, 0, sizeof(config));

    config.model_config.transducer.encoder = encoder_path.c_str();
    config.model_config.transducer.decoder = decoder_path.c_str();
    config.model_config.transducer.joiner  = joiner_path.c_str();
    config.model_config.tokens = tokens_path.c_str();
    config.model_config.num_threads = 1;
    config.model_config.debug = 0;
    config.decoding_method = "modified_beam_search";

    const SherpaOnnxOfflineRecognizer* recognizer =
        SherpaOnnxCreateOfflineRecognizer(&config);

    if (!recognizer) {
        log(logfile, "ERROR: Cannot create recognizer. Check model paths:");
        log(logfile, "  Encoder: " + encoder_path);
        log(logfile, "  Decoder: " + decoder_path);
        log(logfile, "  Joiner: " + joiner_path);
        log(logfile, "  Tokens: " + tokens_path);
        return 1;
    }

    // Create stream and feed audio
    const SherpaOnnxOfflineStream* stream = SherpaOnnxCreateOfflineStream(recognizer);
    if (!stream) {
        log(logfile, "ERROR: Cannot create stream");
        SherpaOnnxDestroyOfflineRecognizer(recognizer);
        return 1;
    }

    log(logfile, "Transcribing...");
    time_t start_time = time(nullptr);

    SherpaOnnxAcceptWaveformOffline(stream, sample_rate, samples.data(), samples.size());
    SherpaOnnxDecodeOfflineStream(recognizer, stream);

    const SherpaOnnxOfflineRecognizerResult* result =
        SherpaOnnxGetOfflineStreamResult(stream);

    time_t end_time = time(nullptr);
    int elapsed = (int)(end_time - start_time);

    std::string transcription = (result && result->text) ? result->text : "";

    // Cleanup sherpa-onnx
    SherpaOnnxDestroyOfflineRecognizerResult(result);
    SherpaOnnxDestroyOfflineStream(stream);
    SherpaOnnxDestroyOfflineRecognizer(recognizer);

    log(logfile, "Transcription completed in " + std::to_string(elapsed) + "s");
    log(logfile, "Result: " + transcription);

    // Write transcription text file
    std::string basename = basename_no_ext(input_path);
    std::string txt_path = output_dir + "/" + basename + ".txt";
    {
        std::ofstream txt(txt_path);
        if (txt) {
            txt << transcription << "\n";
            log(logfile, "Text saved: " + txt_path);
        } else {
            log(logfile, "ERROR: Cannot write " + txt_path);
        }
    }

    // Write summary file
    std::string summary_path = output_dir + "/summary.txt";
    {
        std::ofstream summary(summary_path);
        if (summary) {
            summary << "Sherpa-ONNX Speech-to-Text Results\n";
            summary << "===================================\n";
            summary << "Model: zipformer-small-en (int8 quantized, ~28 MB)\n";
            summary << "Decoding: modified_beam_search\n";
            summary << "Input: " << input_path << "\n";
            summary << "Duration: " << samples.size() / (float)sample_rate << "s\n";
            summary << "Processing time: " << elapsed << "s\n";
            summary << "\n";
            summary << "Transcription:\n";
            summary << "---------------\n";
            summary << "[" << basename << ".txt]\n";
            summary << transcription << "\n";
            log(logfile, "Summary saved: " + summary_path);
        }
    }

    log(logfile, "==========================================");
    log(logfile, "Done");
    log(logfile, "==========================================");

    return 0;
}

/**
 * Sherpa-ONNX Speech-to-Text Transcriber
 *
 * Usage: sherpa_transcriber -i input.wav -o output.txt -m model_dir
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sndfile.h>
#include "sherpa-onnx/c-api/c-api.h"

#define SAMPLE_RATE 16000

typedef struct {
    char *input_file;
    char *output_file;
    char *model_dir;
    int verbose;
} Config;

void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s -i input.wav -o output.txt -m model_dir [-v]\n", prog);
    fprintf(stderr, "  -i  Input WAV file\n");
    fprintf(stderr, "  -o  Output text file\n");
    fprintf(stderr, "  -m  Sherpa-ONNX model directory\n");
    fprintf(stderr, "  -v  Verbose output\n");
}

int parse_args(int argc, char **argv, Config *cfg) {
    int opt;
    memset(cfg, 0, sizeof(Config));

    while ((opt = getopt(argc, argv, "i:o:m:v")) != -1) {
        switch (opt) {
            case 'i': cfg->input_file = optarg; break;
            case 'o': cfg->output_file = optarg; break;
            case 'm': cfg->model_dir = optarg; break;
            case 'v': cfg->verbose = 1; break;
            default:
                print_usage(argv[0]);
                return -1;
        }
    }

    if (!cfg->input_file || !cfg->output_file || !cfg->model_dir) {
        print_usage(argv[0]);
        return -1;
    }

    return 0;
}

// Simple linear interpolation resampler
float* resample(float *input, int input_frames, int input_rate, int output_rate, int *output_frames) {
    double ratio = (double)output_rate / input_rate;
    *output_frames = (int)(input_frames * ratio);

    float *output = malloc(*output_frames * sizeof(float));
    if (!output) return NULL;

    for (int i = 0; i < *output_frames; i++) {
        double src_idx = i / ratio;
        int idx0 = (int)src_idx;
        int idx1 = idx0 + 1;
        double frac = src_idx - idx0;

        if (idx1 >= input_frames) idx1 = input_frames - 1;

        output[i] = input[idx0] * (1.0 - frac) + input[idx1] * frac;
    }

    return output;
}

int main(int argc, char **argv) {
    Config cfg;
    if (parse_args(argc, argv, &cfg) != 0) {
        return 1;
    }

    // Open audio file
    SF_INFO sf_info;
    memset(&sf_info, 0, sizeof(sf_info));
    SNDFILE *sf = sf_open(cfg.input_file, SFM_READ, &sf_info);
    if (!sf) {
        fprintf(stderr, "Error: Cannot open input file: %s\n", cfg.input_file);
        return 1;
    }

    if (cfg.verbose) {
        printf("Input: %s\n", cfg.input_file);
        printf("  Sample rate: %d Hz\n", sf_info.samplerate);
        printf("  Channels: %d\n", sf_info.channels);
        printf("  Frames: %ld\n", (long)sf_info.frames);
    }

    // Read all audio data
    int total_frames = sf_info.frames;
    float *audio = malloc(total_frames * sf_info.channels * sizeof(float));
    if (!audio) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        sf_close(sf);
        return 1;
    }

    sf_count_t frames_read = sf_readf_float(sf, audio, total_frames);
    sf_close(sf);

    if (frames_read != total_frames) {
        fprintf(stderr, "Warning: Read %ld frames, expected %d\n", (long)frames_read, total_frames);
        total_frames = frames_read;
    }

    // Convert stereo to mono if needed
    float *mono;
    if (sf_info.channels > 1) {
        mono = malloc(total_frames * sizeof(float));
        if (!mono) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            free(audio);
            return 1;
        }
        for (int i = 0; i < total_frames; i++) {
            float sum = 0;
            for (int c = 0; c < sf_info.channels; c++) {
                sum += audio[i * sf_info.channels + c];
            }
            mono[i] = sum / sf_info.channels;
        }
        free(audio);
    } else {
        mono = audio;
    }

    // Resample to 16kHz if needed
    float *resampled;
    int resampled_frames;
    if (sf_info.samplerate != SAMPLE_RATE) {
        if (cfg.verbose) {
            printf("  Resampling from %d Hz to %d Hz\n", sf_info.samplerate, SAMPLE_RATE);
        }
        resampled = resample(mono, total_frames, sf_info.samplerate, SAMPLE_RATE, &resampled_frames);
        if (!resampled) {
            fprintf(stderr, "Error: Resampling failed\n");
            free(mono);
            return 1;
        }
        free(mono);
    } else {
        resampled = mono;
        resampled_frames = total_frames;
    }

    // Build model paths
    char encoder_path[512], decoder_path[512], joiner_path[512], tokens_path[512];
    snprintf(encoder_path, sizeof(encoder_path), "%s/encoder-epoch-99-avg-1.int8.onnx", cfg.model_dir);
    snprintf(decoder_path, sizeof(decoder_path), "%s/decoder-epoch-99-avg-1.onnx", cfg.model_dir);
    snprintf(joiner_path, sizeof(joiner_path), "%s/joiner-epoch-99-avg-1.int8.onnx", cfg.model_dir);
    snprintf(tokens_path, sizeof(tokens_path), "%s/tokens.txt", cfg.model_dir);

    // Configure Sherpa-ONNX
    SherpaOnnxOfflineRecognizerConfig config;
    memset(&config, 0, sizeof(config));

    config.model_config.transducer.encoder = encoder_path;
    config.model_config.transducer.decoder = decoder_path;
    config.model_config.transducer.joiner = joiner_path;
    config.model_config.tokens = tokens_path;
    config.model_config.num_threads = 1;
    config.model_config.debug = cfg.verbose ? 1 : 0;
    config.decoding_method = "greedy_search";

    // Create recognizer
    SherpaOnnxOfflineRecognizer *recognizer = SherpaOnnxCreateOfflineRecognizer(&config);
    if (!recognizer) {
        fprintf(stderr, "Error: Cannot create recognizer. Check model paths:\n");
        fprintf(stderr, "  Encoder: %s\n", encoder_path);
        fprintf(stderr, "  Decoder: %s\n", decoder_path);
        fprintf(stderr, "  Joiner: %s\n", joiner_path);
        fprintf(stderr, "  Tokens: %s\n", tokens_path);
        free(resampled);
        return 1;
    }

    // Create stream
    SherpaOnnxOfflineStream *stream = SherpaOnnxCreateOfflineStream(recognizer);
    if (!stream) {
        fprintf(stderr, "Error: Cannot create stream\n");
        SherpaOnnxDestroyOfflineRecognizer(recognizer);
        free(resampled);
        return 1;
    }

    // Feed audio
    SherpaOnnxAcceptWaveformOffline(stream, SAMPLE_RATE, resampled, resampled_frames);

    // Decode
    SherpaOnnxDecodeOfflineStream(recognizer, stream);

    // Get result
    const SherpaOnnxOfflineRecognizerResult *result = SherpaOnnxGetOfflineStreamResult(stream);

    const char *transcription = result->text ? result->text : "";

    // Output result
    printf("Transcription: %s\n", transcription);

    // Write to file
    FILE *out = fopen(cfg.output_file, "w");
    if (out) {
        fprintf(out, "%s\n", transcription);
        fclose(out);
        if (cfg.verbose) {
            printf("Output written to: %s\n", cfg.output_file);
        }
    } else {
        fprintf(stderr, "Error: Cannot write to output file: %s\n", cfg.output_file);
    }

    // Cleanup
    SherpaOnnxDestroyOfflineRecognizerResult(result);
    SherpaOnnxDestroyOfflineStream(stream);
    SherpaOnnxDestroyOfflineRecognizer(recognizer);
    free(resampled);

    return 0;
}

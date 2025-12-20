/**
 * PocketSphinx Speech-to-Text Transcriber
 *
 * Usage: pocketsphinx_transcriber -i input.wav -o output.txt [-v]
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sndfile.h>
#include <pocketsphinx.h>

#define SAMPLE_RATE 16000

typedef struct {
    char *input_file;
    char *output_file;
    int verbose;
} Config;

void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s -i input.wav -o output.txt [-v]\n", prog);
    fprintf(stderr, "  -i  Input WAV file\n");
    fprintf(stderr, "  -o  Output text file\n");
    fprintf(stderr, "  -v  Verbose output\n");
}

int parse_args(int argc, char **argv, Config *cfg) {
    int opt;
    memset(cfg, 0, sizeof(Config));

    while ((opt = getopt(argc, argv, "i:o:v")) != -1) {
        switch (opt) {
            case 'i': cfg->input_file = optarg; break;
            case 'o': cfg->output_file = optarg; break;
            case 'v': cfg->verbose = 1; break;
            default:
                print_usage(argv[0]);
                return -1;
        }
    }

    if (!cfg->input_file || !cfg->output_file) {
        print_usage(argv[0]);
        return -1;
    }

    return 0;
}

// Simple linear interpolation resampler
int16_t* resample(float *input, int input_frames, int input_rate, int output_rate, int *output_frames) {
    double ratio = (double)output_rate / input_rate;
    *output_frames = (int)(input_frames * ratio);

    int16_t *output = malloc(*output_frames * sizeof(int16_t));
    if (!output) return NULL;

    for (int i = 0; i < *output_frames; i++) {
        double src_idx = i / ratio;
        int idx0 = (int)src_idx;
        int idx1 = idx0 + 1;
        double frac = src_idx - idx0;

        if (idx1 >= input_frames) idx1 = input_frames - 1;

        // Interpolate and convert to int16
        float sample = input[idx0] * (1.0 - frac) + input[idx1] * frac;
        // Clamp and convert to 16-bit
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        output[i] = (int16_t)(sample * 32767);
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

    // Resample to 16kHz and convert to int16 for PocketSphinx
    int16_t *samples;
    int n_samples;
    if (sf_info.samplerate != SAMPLE_RATE) {
        if (cfg.verbose) {
            printf("  Resampling from %d Hz to %d Hz\n", sf_info.samplerate, SAMPLE_RATE);
        }
        samples = resample(mono, total_frames, sf_info.samplerate, SAMPLE_RATE, &n_samples);
        if (!samples) {
            fprintf(stderr, "Error: Resampling failed\n");
            free(mono);
            return 1;
        }
        free(mono);
    } else {
        // Convert float to int16
        n_samples = total_frames;
        samples = malloc(n_samples * sizeof(int16_t));
        if (!samples) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            free(mono);
            return 1;
        }
        for (int i = 0; i < n_samples; i++) {
            float sample = mono[i];
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            samples[i] = (int16_t)(sample * 32767);
        }
        free(mono);
    }

    // Create PocketSphinx configuration
    ps_config_t *ps_config = ps_config_init(NULL);
    if (!ps_config) {
        fprintf(stderr, "Error: Cannot create PocketSphinx config\n");
        free(samples);
        return 1;
    }

    // Use default acoustic model
    ps_default_search_args(ps_config);

    // Create decoder
    ps_decoder_t *decoder = ps_init(ps_config);
    if (!decoder) {
        fprintf(stderr, "Error: Cannot create PocketSphinx decoder\n");
        ps_config_free(ps_config);
        free(samples);
        return 1;
    }

    if (cfg.verbose) {
        printf("  PocketSphinx decoder initialized\n");
    }

    // Start utterance
    if (ps_start_utt(decoder) < 0) {
        fprintf(stderr, "Error: Cannot start utterance\n");
        ps_free(decoder);
        ps_config_free(ps_config);
        free(samples);
        return 1;
    }

    // Process audio
    if (ps_process_raw(decoder, samples, n_samples, 0, 1) < 0) {
        fprintf(stderr, "Error: Cannot process audio\n");
        ps_free(decoder);
        ps_config_free(ps_config);
        free(samples);
        return 1;
    }

    // End utterance
    if (ps_end_utt(decoder) < 0) {
        fprintf(stderr, "Error: Cannot end utterance\n");
        ps_free(decoder);
        ps_config_free(ps_config);
        free(samples);
        return 1;
    }

    // Get hypothesis
    const char *hyp = ps_get_hyp(decoder, NULL);
    const char *transcription = hyp ? hyp : "";

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
    ps_free(decoder);
    ps_config_free(ps_config);
    free(samples);

    return 0;
}

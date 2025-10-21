#include "audio_io.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Simple linear interpolation resampling
static void resample_audio(float* input, int input_len, int input_rate,
                          float* output, int* output_len, int target_rate) {
    double ratio = (double)input_rate / target_rate;
    *output_len = (int)(input_len / ratio);

    for (int i = 0; i < *output_len; i++) {
        double src_index = i * ratio;
        int src_i = (int)src_index;
        double frac = src_index - src_i;

        if (src_i + 1 < input_len) {
            output[i] = input[src_i] * (1.0 - frac) + input[src_i + 1] * frac;
        } else {
            output[i] = input[src_i];
        }
    }
}

// Convert stereo to mono (take left channel)
static void stereo_to_mono(float* stereo_input, float* mono_output, int frames) {
    for (int i = 0; i < frames; i++) {
        mono_output[i] = stereo_input[i * 2];
    }
}

AudioData* load_audio(const char* filename) {
    SF_INFO sf_info;
    SNDFILE* file = sf_open(filename, SFM_READ, &sf_info);

    if (!file) {
        fprintf(stderr, "Failed to open audio file: %s\n", filename);
        return NULL;
    }

    printf("Loaded: %d channels, %d Hz, %ld frames\n",
           sf_info.channels, sf_info.samplerate, sf_info.frames);

    // Read audio data
    float* raw_data = malloc(sf_info.frames * sf_info.channels * sizeof(float));
    sf_readf_float(file, raw_data, sf_info.frames);
    sf_close(file);

    // Convert to mono if needed
    float* mono_data = malloc(sf_info.frames * sizeof(float));
    if (sf_info.channels == 2) {
        stereo_to_mono(raw_data, mono_data, sf_info.frames);
        printf("Converted stereo to mono\n");
    } else {
        memcpy(mono_data, raw_data, sf_info.frames * sizeof(float));
    }
    free(raw_data);

    // Resample to 16kHz if needed
    float* final_data;
    int final_frames;

    if (sf_info.samplerate != TARGET_SAMPLE_RATE) {
        final_data = malloc(sf_info.frames * 2 * sizeof(float));
        resample_audio(mono_data, sf_info.frames, sf_info.samplerate,
                      final_data, &final_frames, TARGET_SAMPLE_RATE);
        free(mono_data);
        printf("Resampled from %d Hz to %d Hz, %d frames\n",
               sf_info.samplerate, TARGET_SAMPLE_RATE, final_frames);
    } else {
        final_data = mono_data;
        final_frames = sf_info.frames;
        printf("Already at %d Hz\n", TARGET_SAMPLE_RATE);
    }

    // Create AudioData structure
    AudioData* audio = malloc(sizeof(AudioData));
    audio->data = final_data;
    audio->frames = final_frames;
    audio->channels = 1;
    audio->samplerate = TARGET_SAMPLE_RATE;

    return audio;
}

int save_audio(const char* filename, float* data, int frames, int samplerate) {
    SF_INFO sf_info;
    sf_info.samplerate = samplerate;
    sf_info.channels = 1;
    sf_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE* file = sf_open(filename, SFM_WRITE, &sf_info);
    if (!file) {
        fprintf(stderr, "Failed to create output file: %s\n", filename);
        return -1;
    }

    sf_writef_float(file, data, frames);
    sf_close(file);

    printf("Saved: %s (%d frames, %d Hz)\n", filename, frames, samplerate);
    return 0;
}

void free_audio(AudioData* audio) {
    if (audio) {
        if (audio->data) free(audio->data);
        free(audio);
    }
}

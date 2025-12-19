#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fftw3.h>
#include "common/audio_io.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Spectral subtraction parameters
#define FRAME_SIZE 512
#define HOP_SIZE 256
#define NOISE_EST_FRAMES 20  // Number of initial frames for noise estimation
#define OVERSUB_FACTOR 1.0   // Oversubtraction factor (1.0-2.0, higher = more aggressive)
#define MIN_GAIN 0.1         // Minimum gain floor to prevent complete suppression (0.0-0.3)

typedef struct {
    float* noise_power;      // Estimated noise power spectrum
    int fft_size;
    fftwf_plan forward_plan;
    fftwf_plan inverse_plan;
    float* window;
    float* fft_in;
    fftwf_complex* fft_out;
    float* ifft_out;
} SpectralSubtractor;

// Create Hann window
static void create_hann_window(float* window, int size) {
    for (int i = 0; i < size; i++) {
        window[i] = 0.5 * (1.0 - cos(2.0 * M_PI * i / (size - 1)));
    }
}

SpectralSubtractor* spectral_subtractor_init() {
    SpectralSubtractor* ss = malloc(sizeof(SpectralSubtractor));

    ss->fft_size = FRAME_SIZE;
    ss->noise_power = calloc(ss->fft_size / 2 + 1, sizeof(float));
    ss->window = malloc(FRAME_SIZE * sizeof(float));
    ss->fft_in = malloc(FRAME_SIZE * sizeof(float));
    ss->fft_out = fftwf_alloc_complex(ss->fft_size / 2 + 1);
    ss->ifft_out = malloc(FRAME_SIZE * sizeof(float));

    // Create Hann window
    create_hann_window(ss->window, FRAME_SIZE);

    // Create FFTW plans
    ss->forward_plan = fftwf_plan_dft_r2c_1d(FRAME_SIZE, ss->fft_in, ss->fft_out, FFTW_ESTIMATE);
    ss->inverse_plan = fftwf_plan_dft_c2r_1d(FRAME_SIZE, ss->fft_out, ss->ifft_out, FFTW_ESTIMATE);

    return ss;
}

void spectral_subtractor_cleanup(SpectralSubtractor* ss) {
    if (ss) {
        if (ss->noise_power) free(ss->noise_power);
        if (ss->window) free(ss->window);
        if (ss->fft_in) free(ss->fft_in);
        if (ss->fft_out) fftwf_free(ss->fft_out);
        if (ss->ifft_out) free(ss->ifft_out);
        if (ss->forward_plan) fftwf_destroy_plan(ss->forward_plan);
        if (ss->inverse_plan) fftwf_destroy_plan(ss->inverse_plan);
        free(ss);
    }
}

// Estimate noise spectrum from initial frames
void estimate_noise(SpectralSubtractor* ss, float* audio, int frames) {
    int num_frames = NOISE_EST_FRAMES;
    if (frames < num_frames * FRAME_SIZE) {
        num_frames = frames / FRAME_SIZE;
    }

    printf("Estimating noise from first %d frames...\n", num_frames);

    // Accumulate power spectrum from initial frames
    for (int frame = 0; frame < num_frames; frame++) {
        int offset = frame * HOP_SIZE;

        // Apply window and copy to FFT input
        for (int i = 0; i < FRAME_SIZE; i++) {
            if (offset + i < frames) {
                ss->fft_in[i] = audio[offset + i] * ss->window[i];
            } else {
                ss->fft_in[i] = 0.0f;
            }
        }

        // Compute FFT
        fftwf_execute(ss->forward_plan);

        // Accumulate power spectrum
        for (int i = 0; i < ss->fft_size / 2 + 1; i++) {
            float re = ss->fft_out[i][0];
            float im = ss->fft_out[i][1];
            ss->noise_power[i] += (re * re + im * im) / num_frames;
        }
    }

    printf("Noise estimation complete\n");
}

// Process audio with spectral subtraction
float* spectral_subtraction_process(SpectralSubtractor* ss, float* audio, int frames, int* out_frames) {
    // Estimate noise from initial frames
    estimate_noise(ss, audio, frames);

    // Allocate output buffer
    float* output = calloc(frames, sizeof(float));
    *out_frames = frames;

    // Process frames
    int num_frames = (frames - FRAME_SIZE) / HOP_SIZE + 1;
    printf("Processing %d frames...\n", num_frames);

    for (int frame = 0; frame < num_frames; frame++) {
        int offset = frame * HOP_SIZE;

        // Apply window and copy to FFT input
        for (int i = 0; i < FRAME_SIZE; i++) {
            if (offset + i < frames) {
                ss->fft_in[i] = audio[offset + i] * ss->window[i];
            } else {
                ss->fft_in[i] = 0.0f;
            }
        }

        // Compute FFT
        fftwf_execute(ss->forward_plan);

        // Apply spectral subtraction using gain-based approach
        for (int i = 0; i < ss->fft_size / 2 + 1; i++) {
            float re = ss->fft_out[i][0];
            float im = ss->fft_out[i][1];
            float signal_power = re * re + im * im;

            // Subtract noise power with oversubtraction
            float clean_power = signal_power - OVERSUB_FACTOR * ss->noise_power[i];

            // Compute gain (ratio of clean to noisy power)
            float gain = 0.0f;
            if (signal_power > 0.0f) {
                gain = sqrt(fmax(clean_power, 0.0f) / signal_power);
            }

            // Apply minimum gain floor to prevent over-suppression and distortion
            if (gain < MIN_GAIN) {
                gain = MIN_GAIN;
            }

            // Apply gain (no need to extract/reconstruct phase - just scale)
            ss->fft_out[i][0] = re * gain;
            ss->fft_out[i][1] = im * gain;
        }

        // Compute IFFT
        fftwf_execute(ss->inverse_plan);

        // Overlap-add with window (for proper COLA with 50% overlap and Hann window)
        // Note: Window is applied both during analysis and synthesis for WOLA (Weighted Overlap-Add)
        for (int i = 0; i < FRAME_SIZE && (offset + i) < frames; i++) {
            output[offset + i] += ss->ifft_out[i] * ss->window[i] / FRAME_SIZE;
        }
    }

    printf("Processing complete\n");
    return output;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s input.wav output.wav\n", argv[0]);
        return -1;
    }

    const char* input_path = argv[1];
    const char* output_path = argv[2];

    printf("=== Spectral Subtraction Denoiser ===\n");

    // Load audio
    AudioData* audio = load_audio(input_path);
    if (!audio) {
        return -1;
    }

    // Initialize spectral subtractor
    SpectralSubtractor* ss = spectral_subtractor_init();

    // Process audio
    int out_frames;
    float* denoised = spectral_subtraction_process(ss, audio->data, audio->frames, &out_frames);

    // Save output
    save_audio(output_path, denoised, out_frames, audio->samplerate);

    // Cleanup
    free(denoised);
    spectral_subtractor_cleanup(ss);
    free_audio(audio);

    printf("Denoising complete: %s -> %s\n", input_path, output_path);
    return 0;
}

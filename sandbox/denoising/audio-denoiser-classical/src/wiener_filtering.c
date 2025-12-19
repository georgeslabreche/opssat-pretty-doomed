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

// Wiener filtering parameters
#define FRAME_SIZE 512
#define HOP_SIZE 256
#define NOISE_EST_FRAMES 20   // Number of initial frames for noise estimation
#define MIN_GAIN 0.1          // Minimum gain floor (prevents complete suppression)
#define NOISE_FLOOR 1e-10     // Small constant for numerical stability

typedef struct {
    float* noise_power;      // Estimated noise power spectrum
    int fft_size;
    fftwf_plan forward_plan;
    fftwf_plan inverse_plan;
    float* window;
    float* fft_in;
    fftwf_complex* fft_out;
    float* ifft_out;
} WienerFilter;

/**
 * Creates a Hann window for smooth frame transitions in STFT processing.
 *
 * Parameters:
 *   window - Pointer to buffer where window values will be stored (length 'size')
 *   size   - Number of samples in the window
 */
static void create_hann_window(float* window, int size) {
    for (int i = 0; i < size; i++) {
        window[i] = 0.5 * (1.0 - cos(2.0 * M_PI * i / (size - 1)));
    }
}

// Forward declaration
void wiener_filter_cleanup(WienerFilter* wf);

/**
 * Initializes and allocates a WienerFilter structure.
 *
 * Allocates:
 *   - Noise power spectrum buffer
 *   - Hann window
 *   - FFT input/output buffers
 *   - FFTW plans for forward/inverse transforms
 *
 * Returns:
 *   Pointer to initialized WienerFilter, or NULL on allocation failure
 */
WienerFilter* wiener_filter_init() {
    WienerFilter* wf = malloc(sizeof(WienerFilter));
    if (!wf) {
        fprintf(stderr, "Failed to allocate WienerFilter structure\n");
        return NULL;
    }

    wf->fft_size = FRAME_SIZE;

    // Initialize all pointers to NULL for safe cleanup
    wf->noise_power = NULL;
    wf->window = NULL;
    wf->fft_in = NULL;
    wf->fft_out = NULL;
    wf->ifft_out = NULL;
    wf->forward_plan = NULL;
    wf->inverse_plan = NULL;

    // Allocate buffers
    wf->noise_power = calloc(wf->fft_size / 2 + 1, sizeof(float));
    wf->window = malloc(FRAME_SIZE * sizeof(float));
    wf->fft_in = malloc(FRAME_SIZE * sizeof(float));
    wf->fft_out = fftwf_alloc_complex(wf->fft_size / 2 + 1);
    wf->ifft_out = malloc(FRAME_SIZE * sizeof(float));

    if (!wf->noise_power || !wf->window || !wf->fft_in ||
        !wf->fft_out || !wf->ifft_out) {
        fprintf(stderr, "Failed to allocate Wiener filter buffers\n");
        wiener_filter_cleanup(wf);
        return NULL;
    }

    // Create Hann window
    create_hann_window(wf->window, FRAME_SIZE);

    // Create FFTW plans
    wf->forward_plan = fftwf_plan_dft_r2c_1d(FRAME_SIZE, wf->fft_in,
                                             wf->fft_out, FFTW_ESTIMATE);
    wf->inverse_plan = fftwf_plan_dft_c2r_1d(FRAME_SIZE, wf->fft_out,
                                             wf->ifft_out, FFTW_ESTIMATE);

    if (!wf->forward_plan || !wf->inverse_plan) {
        fprintf(stderr, "Failed to create FFTW plans\n");
        wiener_filter_cleanup(wf);
        return NULL;
    }

    return wf;
}

/**
 * Cleans up and frees all resources associated with a WienerFilter.
 *
 * Parameters:
 *   wf - Pointer to WienerFilter to clean up (can be NULL)
 *
 * Note: Safe to call with NULL pointer or partially initialized structure
 */
void wiener_filter_cleanup(WienerFilter* wf) {
    if (wf) {
        if (wf->noise_power) free(wf->noise_power);
        if (wf->window) free(wf->window);
        if (wf->fft_in) free(wf->fft_in);
        if (wf->fft_out) fftwf_free(wf->fft_out);
        if (wf->ifft_out) free(wf->ifft_out);
        if (wf->forward_plan) fftwf_destroy_plan(wf->forward_plan);
        if (wf->inverse_plan) fftwf_destroy_plan(wf->inverse_plan);
        free(wf);
    }
}

/**
 * Estimates noise power spectrum from initial frames.
 *
 * Parameters:
 *   wf     - Pointer to initialized WienerFilter structure
 *   audio  - Pointer to input audio buffer
 *   frames - Number of samples in audio buffer
 *
 * Note: Assumes initial frames contain primarily noise.
 *       Updates wf->noise_power with averaged power spectrum.
 */
void estimate_noise(WienerFilter* wf, float* audio, int frames) {
    if (!wf || !audio) {
        fprintf(stderr, "Error: NULL pointer in estimate_noise\n");
        return;
    }

    int num_frames = NOISE_EST_FRAMES;
    if (frames < num_frames * FRAME_SIZE) {
        num_frames = frames / FRAME_SIZE;
    }

    // Ensure we have at least 1 frame for noise estimation
    if (num_frames < 1) {
        fprintf(stderr, "Warning: Audio too short for noise estimation, using minimal noise\n");
        return;  // noise_power remains zeroed from calloc
    }

    printf("Estimating noise from first %d frames...\n", num_frames);

    // Accumulate power spectrum from initial frames
    for (int frame = 0; frame < num_frames; frame++) {
        int offset = frame * HOP_SIZE;

        // Apply window and copy to FFT input
        for (int i = 0; i < FRAME_SIZE; i++) {
            if (offset + i < frames) {
                wf->fft_in[i] = audio[offset + i] * wf->window[i];
            } else {
                wf->fft_in[i] = 0.0f;
            }
        }

        // Compute FFT
        fftwf_execute(wf->forward_plan);

        // Accumulate power spectrum
        for (int i = 0; i < wf->fft_size / 2 + 1; i++) {
            float re = wf->fft_out[i][0];
            float im = wf->fft_out[i][1];
            wf->noise_power[i] += (re * re + im * im) / num_frames;
        }
    }

    printf("Noise estimation complete\n");
}

/**
 * Processes audio using Wiener filtering to reduce noise.
 *
 * Algorithm:
 *   1. Estimates noise power from initial frames
 *   2. For each frame:
 *      - Transform to frequency domain
 *      - Compute optimal Wiener gain: H = clean_power / noisy_power
 *      - Apply gain with minimum floor to prevent over-suppression
 *      - Transform back to time domain
 *   3. Overlap-add reconstruction
 *
 * Parameters:
 *   wf         - Pointer to initialized WienerFilter structure
 *   audio      - Pointer to input audio buffer
 *   frames     - Number of samples in input
 *   out_frames - Pointer to store output frame count
 *
 * Returns:
 *   Pointer to newly allocated denoised audio buffer, or NULL on failure.
 *   Caller must free the returned buffer.
 */
float* wiener_filtering_process(WienerFilter* wf, float* audio, int frames, int* out_frames) {
    // Check for NULL pointers
    if (!wf || !audio || !out_frames) {
        fprintf(stderr, "Error: NULL pointer passed to wiener_filtering_process\n");
        return NULL;
    }

    // Check for invalid frame count
    if (frames <= 0) {
        fprintf(stderr, "Error: Invalid frame count (%d)\n", frames);
        return NULL;
    }

    // Check minimum frame size
    if (frames < FRAME_SIZE) {
        fprintf(stderr, "Error: Audio too short (%d samples), need at least %d samples\n",
                frames, FRAME_SIZE);
        return NULL;
    }

    // Estimate noise from initial frames
    estimate_noise(wf, audio, frames);

    // Allocate output buffer
    float* output = calloc(frames, sizeof(float));
    if (!output) {
        fprintf(stderr, "Failed to allocate output buffer\n");
        return NULL;
    }
    *out_frames = frames;

    // Process frames
    int num_frames = (frames - FRAME_SIZE) / HOP_SIZE + 1;
    printf("Processing %d frames with Wiener filtering...\n", num_frames);

    for (int frame = 0; frame < num_frames; frame++) {
        int offset = frame * HOP_SIZE;

        // Apply window and copy to FFT input
        for (int i = 0; i < FRAME_SIZE; i++) {
            if (offset + i < frames) {
                wf->fft_in[i] = audio[offset + i] * wf->window[i];
            } else {
                wf->fft_in[i] = 0.0f;
            }
        }

        // Compute FFT
        fftwf_execute(wf->forward_plan);

        // Apply Wiener filtering in frequency domain
        for (int i = 0; i < wf->fft_size / 2 + 1; i++) {
            float re = wf->fft_out[i][0];
            float im = wf->fft_out[i][1];
            float noisy_power = re * re + im * im;

            // Estimate clean signal power (noisy - noise)
            float clean_power = noisy_power - wf->noise_power[i];

            // Ensure non-negative
            if (clean_power < 0.0f) {
                clean_power = 0.0f;
            }

            // Compute Wiener gain: H = clean_power / (clean_power + noise_power)
            // Equivalent to: H = clean_power / noisy_power
            float gain = 0.0f;
            if (noisy_power > NOISE_FLOOR) {
                gain = clean_power / noisy_power;
            }

            // Clamp gain to [MIN_GAIN, 1.0] range
            // - Prevents over-suppression (MIN_GAIN floor)
            // - Prevents amplification (1.0 ceiling)
            if (gain > 1.0f) {
                gain = 1.0f;
            }
            if (gain < MIN_GAIN) {
                gain = MIN_GAIN;
            }

            // Apply gain (preserves phase)
            wf->fft_out[i][0] = re * gain;
            wf->fft_out[i][1] = im * gain;
        }

        // Compute IFFT
        fftwf_execute(wf->inverse_plan);

        // Overlap-add with window
        for (int i = 0; i < FRAME_SIZE && (offset + i) < frames; i++) {
            output[offset + i] += wf->ifft_out[i] * wf->window[i] / FRAME_SIZE;
        }
    }

    printf("Wiener filtering complete\n");
    return output;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s input.wav output.wav\n", argv[0]);
        return -1;
    }

    const char* input_path = argv[1];
    const char* output_path = argv[2];

    printf("=== Wiener Filter Denoiser ===\n");

    // Load audio
    AudioData* audio = load_audio(input_path);
    if (!audio) {
        return -1;
    }

    // Initialize Wiener filter
    WienerFilter* wf = wiener_filter_init();
    if (!wf) {
        fprintf(stderr, "Failed to initialize Wiener filter\n");
        free_audio(audio);
        return -1;
    }

    // Process audio
    int out_frames;
    float* denoised = wiener_filtering_process(wf, audio->data, audio->frames, &out_frames);
    if (!denoised) {
        fprintf(stderr, "Failed to process audio\n");
        wiener_filter_cleanup(wf);
        free_audio(audio);
        return -1;
    }

    // Save output
    save_audio(output_path, denoised, out_frames, audio->samplerate);

    // Cleanup
    free(denoised);
    wiener_filter_cleanup(wf);
    free_audio(audio);

    printf("Denoising complete: %s -> %s\n", input_path, output_path);
    return 0;
}

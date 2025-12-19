#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "common/audio_io.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ALE (Adaptive Line Enhancement) parameters
#define DELAY 400              // Delay in samples (25ms at 16kHz) - decorrelates speech, preserves interference
#define FILTER_LENGTH 64       // Number of adaptive filter taps
#define MU 0.01               // LMS step size (learning rate)
#define LEAK_FACTOR 0.9999    // Leaky LMS to prevent weight drift

/**
 * Adaptive Line Enhancement (ALE) structure
 *
 * ALE uses a delayed version of the input signal as reference.
 * The adaptive filter predicts periodic/quasi-periodic components (interference).
 * Subtracting the prediction from the original gives the desired signal (speech).
 */
typedef struct {
    float* weights;          // Adaptive filter coefficients [FILTER_LENGTH]
    float* delay_line;       // Delay line for input signal [FILTER_LENGTH]
    float* reference_buffer; // Buffer for delayed reference [DELAY]
    int buffer_index;        // Circular buffer index for reference
} ALEFilter;

// Forward declaration
void ale_filter_cleanup(ALEFilter* ale);

/**
 * Initializes and allocates an ALEFilter structure.
 *
 * Returns:
 *   Pointer to initialized ALEFilter, or NULL on allocation failure
 */
ALEFilter* ale_filter_init() {
    ALEFilter* ale = malloc(sizeof(ALEFilter));
    if (!ale) {
        fprintf(stderr, "Failed to allocate ALEFilter structure\n");
        return NULL;
    }

    // Initialize all pointers to NULL for safe cleanup
    ale->weights = NULL;
    ale->delay_line = NULL;
    ale->reference_buffer = NULL;
    ale->buffer_index = 0;

    // Allocate buffers
    ale->weights = calloc(FILTER_LENGTH, sizeof(float));
    ale->delay_line = calloc(FILTER_LENGTH, sizeof(float));
    ale->reference_buffer = calloc(DELAY, sizeof(float));

    if (!ale->weights || !ale->delay_line || !ale->reference_buffer) {
        fprintf(stderr, "Failed to allocate ALE filter buffers\n");
        ale_filter_cleanup(ale);
        return NULL;
    }

    return ale;
}

/**
 * Cleans up and frees all resources associated with an ALEFilter.
 *
 * Parameters:
 *   ale - Pointer to ALEFilter to clean up (can be NULL)
 */
void ale_filter_cleanup(ALEFilter* ale) {
    if (ale) {
        if (ale->weights) free(ale->weights);
        if (ale->delay_line) free(ale->delay_line);
        if (ale->reference_buffer) free(ale->reference_buffer);
        free(ale);
    }
}

/**
 * Processes a single sample through the ALE filter.
 *
 * Algorithm:
 *   1. Get delayed reference: x(n-Δ) from circular buffer
 *   2. Compute filter output (prediction): y(n) = w^T * x_delayed
 *   3. Compute error: e(n) = x(n) - y(n) (this is the desired signal)
 *   4. Update weights: w(n+1) = λ*w(n) + μ*e(n)*x_delayed
 *   5. Update delay line and reference buffer
 *
 * Parameters:
 *   ale   - Pointer to ALEFilter structure
 *   input - Current input sample
 *
 * Returns:
 *   Filtered output (error signal = input - interference prediction)
 */
float ale_filter_process_sample(ALEFilter* ale, float input) {
    if (!ale) {
        fprintf(stderr, "Error: NULL pointer in ale_filter_process_sample\n");
        return 0.0f;
    }

    // Get delayed reference from circular buffer
    float delayed_ref = ale->reference_buffer[ale->buffer_index];

    // Shift delay line: move samples right, insert delayed reference at position 0
    memmove(ale->delay_line + 1, ale->delay_line, (FILTER_LENGTH - 1) * sizeof(float));
    ale->delay_line[0] = delayed_ref;

    // Compute filter output (interference prediction): y(n) = sum(w[i] * x[i])
    float prediction = 0.0f;
    for (int i = 0; i < FILTER_LENGTH; i++) {
        prediction += ale->weights[i] * ale->delay_line[i];
    }

    // Error signal is the desired output (input minus predicted interference)
    float error = input - prediction;

    // Compute power of delay line for normalization (NLMS approach)
    float power = 0.0f;
    for (int i = 0; i < FILTER_LENGTH; i++) {
        power += ale->delay_line[i] * ale->delay_line[i];
    }
    power += 1e-10f; // Numerical stability

    // Normalized step size
    float mu_normalized = MU / power;

    // Update weights using Leaky NLMS algorithm
    // w(n+1) = λ*w(n) + μ*e(n)*x_delayed
    for (int i = 0; i < FILTER_LENGTH; i++) {
        ale->weights[i] = LEAK_FACTOR * ale->weights[i] +
                          mu_normalized * error * ale->delay_line[i];
    }

    // Update reference buffer (circular buffer)
    ale->reference_buffer[ale->buffer_index] = input;
    ale->buffer_index = (ale->buffer_index + 1) % DELAY;

    return error;
}

/**
 * Processes entire audio signal using ALE filtering.
 *
 * Parameters:
 *   ale        - Pointer to initialized ALEFilter structure
 *   audio      - Pointer to input audio buffer
 *   frames     - Number of samples in input
 *   out_frames - Pointer to store output frame count
 *
 * Returns:
 *   Pointer to newly allocated denoised audio buffer, or NULL on failure.
 *   Caller must free the returned buffer.
 */
float* ale_process_audio(ALEFilter* ale, float* audio, int frames, int* out_frames) {
    // Check for NULL pointers
    if (!ale || !audio || !out_frames) {
        fprintf(stderr, "Error: NULL pointer passed to ale_process_audio\n");
        return NULL;
    }

    // Check for invalid frame count
    if (frames <= 0) {
        fprintf(stderr, "Error: Invalid frame count (%d)\n", frames);
        return NULL;
    }

    // Allocate output buffer
    float* output = malloc(frames * sizeof(float));
    if (!output) {
        fprintf(stderr, "Failed to allocate output buffer\n");
        return NULL;
    }
    *out_frames = frames;

    printf("Processing %d samples with ALE (delay=%d, filter_length=%d)...\n",
           frames, DELAY, FILTER_LENGTH);

    // Process each sample
    for (int i = 0; i < frames; i++) {
        output[i] = ale_filter_process_sample(ale, audio[i]);

        // Progress indicator every 16000 samples (1 second at 16kHz)
        if ((i + 1) % 16000 == 0) {
            printf("  Processed %d/%d samples (%.1f%%)\n",
                   i + 1, frames, 100.0f * (i + 1) / frames);
        }
    }

    printf("ALE processing complete\n");
    return output;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s input.wav output.wav\n", argv[0]);
        fprintf(stderr, "\nAdaptive Line Enhancement (ALE) Denoiser\n");
        fprintf(stderr, "Removes quasi-periodic interference (e.g., radio carriers)\n");
        fprintf(stderr, "\nParameters:\n");
        fprintf(stderr, "  DELAY:         %d samples (%.1f ms at 16kHz)\n",
                DELAY, 1000.0f * DELAY / 16000.0f);
        fprintf(stderr, "  FILTER_LENGTH: %d taps\n", FILTER_LENGTH);
        fprintf(stderr, "  MU (step size): %.4f\n", MU);
        return -1;
    }

    const char* input_path = argv[1];
    const char* output_path = argv[2];

    printf("=== Adaptive Line Enhancement (ALE) Denoiser ===\n");

    // Load audio
    AudioData* audio = load_audio(input_path);
    if (!audio) {
        return -1;
    }

    printf("Loaded: %s\n", input_path);
    printf("  Samples: %d\n", audio->frames);
    printf("  Sample rate: %d Hz\n", audio->samplerate);
    printf("  Duration: %.2f seconds\n", (float)audio->frames / audio->samplerate);

    // Initialize ALE filter
    ALEFilter* ale = ale_filter_init();
    if (!ale) {
        fprintf(stderr, "Failed to initialize ALE filter\n");
        free_audio(audio);
        return -1;
    }

    // Process audio
    int out_frames;
    float* denoised = ale_process_audio(ale, audio->data, audio->frames, &out_frames);
    if (!denoised) {
        fprintf(stderr, "Failed to process audio\n");
        ale_filter_cleanup(ale);
        free_audio(audio);
        return -1;
    }

    // Save output
    save_audio(output_path, denoised, out_frames, audio->samplerate);

    printf("\nDenoising complete: %s -> %s\n", input_path, output_path);

    // Cleanup
    free(denoised);
    ale_filter_cleanup(ale);
    free_audio(audio);

    return 0;
}

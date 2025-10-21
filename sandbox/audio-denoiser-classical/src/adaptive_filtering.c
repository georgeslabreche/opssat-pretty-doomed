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

// Adaptive filtering parameters
#define FRAME_SIZE 512
#define HOP_SIZE 256
#define FILTER_LENGTH 64      // Adaptive filter taps per frequency bin
#define NOISE_EST_FRAMES 20   // Initial frames for noise reference estimation
#define MU 0.1                // NLMS step size (0.0-1.0, controls adaptation speed)
#define DELTA 1e-6            // Small constant for numerical stability

typedef struct {
    float* noise_reference;   // Estimated noise reference signal
    int noise_ref_length;
    float** filter_weights;   // Adaptive filter weights per frequency bin
    float** filter_buffer;    // Filter input buffer per frequency bin
    int fft_size;
    int num_bins;
    fftwf_plan forward_plan;
    fftwf_plan inverse_plan;
    float* window;
    float* fft_in;
    fftwf_complex* fft_out;
    float* ifft_out;
} AdaptiveFilter;

// Forward declaration
void adaptive_filter_cleanup(AdaptiveFilter* af);

// Create Hann window
static void create_hann_window(float* window, int size) {
    for (int i = 0; i < size; i++) {
        window[i] = 0.5 * (1.0 - cos(2.0 * M_PI * i / (size - 1)));
    }
}

/**
 * Initializes and allocates an AdaptiveFilter structure for adaptive noise filtering.
 *
 * The AdaptiveFilter structure contains:
 *   - Filter weights and input buffers for each frequency bin (allocated per bin)
 *   - FFTW plans for forward and inverse transforms
 *   - Hann window for frame processing
 *   - Buffers for FFT input/output and IFFT output
 *   - Noise reference buffer (initialized to NULL)
 *
 * Potential failure modes:
 *   - Memory allocation failures (malloc/calloc/fftwf_alloc_complex may return NULL)
 *   - FFTW plan creation failures (fftwf_plan_dft_r2c_1d, fftwf_plan_dft_c2r_1d may return NULL)
 *   - The function does not currently check for allocation failures and may return a partially initialized structure or crash.
 *
 * Returns:
 *   Pointer to the newly allocated AdaptiveFilter structure, or NULL on allocation failure.
 */
AdaptiveFilter* adaptive_filter_init() {
    AdaptiveFilter* af = malloc(sizeof(AdaptiveFilter));
    if (!af) {
        fprintf(stderr, "Failed to allocate AdaptiveFilter structure\n");
        return NULL;
    }

    af->fft_size = FRAME_SIZE;
    af->num_bins = af->fft_size / 2 + 1;

    // Initialize pointers to NULL for safe cleanup
    af->filter_weights = NULL;
    af->filter_buffer = NULL;
    af->window = NULL;
    af->fft_in = NULL;
    af->fft_out = NULL;
    af->ifft_out = NULL;
    af->noise_reference = NULL;
    af->noise_ref_length = 0;
    af->forward_plan = NULL;
    af->inverse_plan = NULL;

    // Allocate filter weights and buffers for each frequency bin
    af->filter_weights = malloc(af->num_bins * sizeof(float*));
    af->filter_buffer = malloc(af->num_bins * sizeof(float*));
    if (!af->filter_weights || !af->filter_buffer) {
        fprintf(stderr, "Failed to allocate filter arrays\n");
        adaptive_filter_cleanup(af);
        return NULL;
    }

    // Initialize pointers to NULL
    for (int i = 0; i < af->num_bins; i++) {
        af->filter_weights[i] = NULL;
        af->filter_buffer[i] = NULL;
    }

    // Allocate individual filter weights and buffers
    for (int i = 0; i < af->num_bins; i++) {
        af->filter_weights[i] = calloc(FILTER_LENGTH, sizeof(float));
        af->filter_buffer[i] = calloc(FILTER_LENGTH, sizeof(float));
        if (!af->filter_weights[i] || !af->filter_buffer[i]) {
            fprintf(stderr, "Failed to allocate filter buffers for bin %d\n", i);
            adaptive_filter_cleanup(af);
            return NULL;
        }
    }

    // Allocate FFT buffers
    af->window = malloc(FRAME_SIZE * sizeof(float));
    af->fft_in = malloc(FRAME_SIZE * sizeof(float));
    af->fft_out = fftwf_alloc_complex(af->num_bins);
    af->ifft_out = malloc(FRAME_SIZE * sizeof(float));

    if (!af->window || !af->fft_in || !af->fft_out || !af->ifft_out) {
        fprintf(stderr, "Failed to allocate FFT buffers\n");
        adaptive_filter_cleanup(af);
        return NULL;
    }

    // Create Hann window
    create_hann_window(af->window, FRAME_SIZE);

    // Create FFTW plans
    af->forward_plan = fftwf_plan_dft_r2c_1d(FRAME_SIZE, af->fft_in, af->fft_out, FFTW_ESTIMATE);
    af->inverse_plan = fftwf_plan_dft_c2r_1d(FRAME_SIZE, af->fft_out, af->ifft_out, FFTW_ESTIMATE);

    if (!af->forward_plan || !af->inverse_plan) {
        fprintf(stderr, "Failed to create FFTW plans\n");
        adaptive_filter_cleanup(af);
        return NULL;
    }

    return af;
}

void adaptive_filter_cleanup(AdaptiveFilter* af) {
    if (af) {
        if (af->noise_reference) free(af->noise_reference);
        if (af->filter_weights) {
            for (int i = 0; i < af->num_bins; i++) {
                if (af->filter_weights[i]) free(af->filter_weights[i]);
            }
            free(af->filter_weights);
        }
        if (af->filter_buffer) {
            for (int i = 0; i < af->num_bins; i++) {
                if (af->filter_buffer[i]) free(af->filter_buffer[i]);
            }
            free(af->filter_buffer);
        }
        if (af->window) free(af->window);
        if (af->fft_in) free(af->fft_in);
        if (af->fft_out) fftwf_free(af->fft_out);
        if (af->ifft_out) free(af->ifft_out);
        if (af->forward_plan) fftwf_destroy_plan(af->forward_plan);
        if (af->inverse_plan) fftwf_destroy_plan(af->inverse_plan);
        free(af);
    }
}

// Extract noise reference from initial frames
void extract_noise_reference(AdaptiveFilter* af, float* audio, int frames) {
    int num_frames = NOISE_EST_FRAMES;
    if (frames < num_frames * FRAME_SIZE) {
        num_frames = frames / FRAME_SIZE;
    }

    // Use initial silence as noise reference
    af->noise_ref_length = num_frames * HOP_SIZE;
    af->noise_reference = malloc(af->noise_ref_length * sizeof(float));
    if (!af->noise_reference) {
        fprintf(stderr, "Error: Failed to allocate memory for noise reference.\n");
        af->noise_ref_length = 0;
        return;
    }

    printf("Extracting noise reference from first %d frames (%d samples)...\n",
           num_frames, af->noise_ref_length);

    // Copy initial audio as noise reference
    for (int i = 0; i < af->noise_ref_length && i < frames; i++) {
        af->noise_reference[i] = audio[i];
    }

    printf("Noise reference extraction complete\n");
}

/**
 * NLMS adaptive filter update for a single frequency bin.
 *
 * Purpose:
 *   Updates the adaptive filter weights using the Normalized Least Mean Squares (NLMS) algorithm.
 *   This is typically used for noise reduction in the frequency domain.
 *
 * Parameters:
 *   weights - Pointer to the filter weights array (length 'length').
 *   buffer  - Pointer to the filter input buffer array (length 'length').
 *   length  - Number of filter taps (size of weights and buffer).
 *   desired - The desired signal for this frequency bin (e.g., the clean/reference signal).
 *   input   - The current input sample for this frequency bin (e.g., noisy observation).
 *   mu      - Step size parameter controlling adaptation speed (0.0 - 1.0).
 *
 * Returns:
 *   The error signal (desired - filter output), which represents the cleaned signal after noise reduction.
 *
 * Notes:
 *   - 'desired' is the target signal we want to recover (e.g., clean speech).
 *   - 'input' is the observed signal (e.g., noisy speech).
 *   - The function shifts the buffer, computes the filter output, error, and updates the weights.
 */
static float nlms_update(float* weights, float* buffer, int length,
                         float desired, float input, float mu) {
    // Shift buffer and insert new input (optimized with memmove)
    memmove(buffer + 1, buffer, (length - 1) * sizeof(float));
    buffer[0] = input;

    // Compute filter output (prediction)
    float output = 0.0f;
    for (int i = 0; i < length; i++) {
        output += weights[i] * buffer[i];
    }

    // Compute error
    float error = desired - output;

    // Compute buffer power for normalization
    float power = DELTA;
    for (int i = 0; i < length; i++) {
        power += buffer[i] * buffer[i];
    }

    // Update weights using NLMS rule
    float step = mu * error / power;
    for (int i = 0; i < length; i++) {
        weights[i] += step * buffer[i];
    }

    return error;  // Return the error (cleaned signal)
}

/**
 * Processes audio using frequency-domain adaptive filtering to reduce noise.
 *
 * Parameters:
 *   af         - Pointer to an initialized AdaptiveFilter structure. Must not be NULL.
 *   audio      - Pointer to input audio buffer (float array of length 'frames').
 *   frames     - Number of samples in the input audio buffer.
 *   out_frames - Pointer to an integer where the number of output samples will be stored.
 *
 * Processing workflow:
 *   - Extracts a noise reference from the initial frames of the input audio.
 *   - Processes the audio in overlapping frames using frequency-domain adaptive filtering.
 *   - For each frame, estimates noise and applies NLMS adaptive filtering per frequency bin.
 *   - Returns a newly allocated buffer containing the denoised audio.
 *
 * Memory ownership:
 *   - The returned float* buffer is allocated with calloc and must be freed by the caller using free().
 *   - The function also allocates temporary buffers internally, which are freed before return.
 *
 * Error conditions:
 *   - If memory allocation fails for the output buffer or temporary buffers, the function may return NULL.
 *   - If input parameters are invalid (e.g., NULL pointers, frames <= 0), behavior is undefined.
 *   - The function prints diagnostic messages to stdout but does not set errno or return error codes.
 */
// Process audio with frequency-domain adaptive filtering
float* adaptive_filtering_process(AdaptiveFilter* af, float* audio, int frames, int* out_frames) {
    // Extract noise reference from initial frames
    extract_noise_reference(af, audio, frames);

    // Allocate output buffer
    float* output = calloc(frames, sizeof(float));
    if (!output) {
        fprintf(stderr, "Failed to allocate output buffer\n");
        return NULL;
    }
    *out_frames = frames;

    // Process frames
    int num_frames = (frames - FRAME_SIZE) / HOP_SIZE + 1;
    printf("Processing %d frames with adaptive filtering...\n", num_frames);

    // Create noise reference FFT buffers
    float* noise_fft_in = malloc(FRAME_SIZE * sizeof(float));
    fftwf_complex* noise_fft_out = fftwf_alloc_complex(af->num_bins);
    if (!noise_fft_in || !noise_fft_out) {
        fprintf(stderr, "Failed to allocate noise FFT buffers\n");
        free(output);
        if (noise_fft_in) free(noise_fft_in);
        if (noise_fft_out) fftwf_free(noise_fft_out);
        return NULL;
    }

    fftwf_plan noise_plan = fftwf_plan_dft_r2c_1d(FRAME_SIZE, noise_fft_in,
                                                   noise_fft_out, FFTW_ESTIMATE);
    if (!noise_plan) {
        fprintf(stderr, "Failed to create noise FFT plan\n");
        free(output);
        free(noise_fft_in);
        fftwf_free(noise_fft_out);
        return NULL;
    }

    for (int frame = 0; frame < num_frames; frame++) {
        int offset = frame * HOP_SIZE;

        // Process noisy signal
        for (int i = 0; i < FRAME_SIZE; i++) {
            if (offset + i < frames) {
                af->fft_in[i] = audio[offset + i] * af->window[i];
            } else {
                af->fft_in[i] = 0.0f;
            }
        }
        fftwf_execute(af->forward_plan);

        // Process noise reference (circular buffer)
        for (int i = 0; i < FRAME_SIZE; i++) {
            int ref_idx = (offset + i) % af->noise_ref_length;
            noise_fft_in[i] = af->noise_reference[ref_idx] * af->window[i];
        }
        fftwf_execute(noise_plan);

        // Apply NLMS adaptive filtering in each frequency bin
        for (int bin = 0; bin < af->num_bins; bin++) {
            // Noisy signal magnitude
            float noisy_re = af->fft_out[bin][0];
            float noisy_im = af->fft_out[bin][1];
            float noisy_mag = sqrt(noisy_re * noisy_re + noisy_im * noisy_im);
            float noisy_phase = atan2(noisy_im, noisy_re);

            // Noise reference magnitude
            float noise_re = noise_fft_out[bin][0];
            float noise_im = noise_fft_out[bin][1];
            float noise_mag = sqrt(noise_re * noise_re + noise_im * noise_im);

            // NLMS adaptive filter update
            // Desired: noisy signal, Input: noise reference
            float cleaned_mag = nlms_update(af->filter_weights[bin],
                                           af->filter_buffer[bin],
                                           FILTER_LENGTH,
                                           noisy_mag,
                                           noise_mag,
                                           MU);

            // Ensure non-negative magnitude
            if (cleaned_mag < 0.0f) cleaned_mag = 0.0f;

            // Reconstruct with original phase
            af->fft_out[bin][0] = cleaned_mag * cos(noisy_phase);
            af->fft_out[bin][1] = cleaned_mag * sin(noisy_phase);
        }

        // Convert back to time domain
        fftwf_execute(af->inverse_plan);

        // Overlap-add
        for (int i = 0; i < FRAME_SIZE && (offset + i) < frames; i++) {
            output[offset + i] += af->ifft_out[i] * af->window[i] / FRAME_SIZE;
        }
    }

    // Cleanup
    free(noise_fft_in);
    fftwf_free(noise_fft_out);
    fftwf_destroy_plan(noise_plan);

    printf("Adaptive filtering complete\n");
    return output;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s input.wav output.wav\n", argv[0]);
        return -1;
    }

    const char* input_path = argv[1];
    const char* output_path = argv[2];

    printf("=== Adaptive Noise Cancellation ===\n");

    // Load audio
    AudioData* audio = load_audio(input_path);
    if (!audio) {
        return -1;
    }

    // Initialize adaptive filter
    AdaptiveFilter* af = adaptive_filter_init();
    if (!af) {
        fprintf(stderr, "Failed to initialize adaptive filter\n");
        free_audio(audio);
        return -1;
    }

    // Process audio
    int out_frames;
    float* denoised = adaptive_filtering_process(af, audio->data, audio->frames, &out_frames);
    if (!denoised) {
        fprintf(stderr, "Failed to process audio\n");
        adaptive_filter_cleanup(af);
        free_audio(audio);
        return -1;
    }

    // Save output
    save_audio(output_path, denoised, out_frames, audio->samplerate);

    // Cleanup
    free(denoised);
    adaptive_filter_cleanup(af);
    free_audio(audio);

    printf("Denoising complete: %s -> %s\n", input_path, output_path);
    return 0;
}

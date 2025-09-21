#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <sndfile.h>
#include <fftw3.h>
#include <tensorflow/lite/c/c_api.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TARGET_SAMPLE_RATE 16000
#define BLOCK_LEN 512    // 32ms at 16kHz
#define BLOCK_SHIFT 128  // 8ms at 16kHz
#define FFT_LEN 512
// Safety factor for resampling buffer allocation
// Accounts for upsampling scenarios where output may be larger than input
#define RESAMPLING_BUFFER_FACTOR 2

typedef struct {
    TfLiteModel* model1;
    TfLiteModel* model2;
    TfLiteInterpreter* interpreter1;
    TfLiteInterpreter* interpreter2;
    TfLiteInterpreterOptions* options;
    // LSTM states (single state tensors as in official implementation)
    float* states_1;
    float* states_2;
    int state_size_1;
    int state_size_2;
    // Buffers for streaming processing
    float* in_buffer;
    float* out_buffer;
    // FFTW plans (created once, reused across calls)
    fftwf_plan rfft_plan;
    fftwf_plan irfft_plan;
    // FFTW buffers (allocated once, reused)
    fftwf_complex* fft_out;
    fftwf_complex* estimated_complex;
    float* estimated_block_raw;
    // Processing buffers (allocated once, reused across audio blocks)
    float* magnitude;
    float* phase;
    float* intermediate_block;
} DTLNProcessor;

// Simple linear interpolation resampling
void resample_audio(float* input, int input_len, int input_rate,
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
void stereo_to_mono(float* stereo_input, float* mono_output, int frames) {
    for (int i = 0; i < frames; i++) {
        mono_output[i] = stereo_input[i * 2]; // Left channel
    }
}

// Initialize DTLN models
int dtln_init(DTLNProcessor* processor, const char* model1_path, const char* model2_path) {
    processor->model1 = TfLiteModelCreateFromFile(model1_path);
    processor->model2 = TfLiteModelCreateFromFile(model2_path);

    if (!processor->model1 || !processor->model2) {
        fprintf(stderr, "Failed to load models\n");
        return -1;
    }

    processor->options = TfLiteInterpreterOptionsCreate();
    TfLiteInterpreterOptionsSetNumThreads(processor->options, 1);

    processor->interpreter1 = TfLiteInterpreterCreate(processor->model1, processor->options);
    processor->interpreter2 = TfLiteInterpreterCreate(processor->model2, processor->options);

    if (!processor->interpreter1 || !processor->interpreter2) {
        fprintf(stderr, "Failed to create interpreters\n");
        return -1;
    }

    if (TfLiteInterpreterAllocateTensors(processor->interpreter1) != kTfLiteOk ||
        TfLiteInterpreterAllocateTensors(processor->interpreter2) != kTfLiteOk) {
        fprintf(stderr, "Failed to allocate tensors\n");
        return -1;
    }

    // Get input details to determine state sizes
    TfLiteTensor* input_tensor_1_1 = TfLiteInterpreterGetInputTensor(processor->interpreter1, 1);
    TfLiteTensor* input_tensor_2_1 = TfLiteInterpreterGetInputTensor(processor->interpreter2, 1);

    // Calculate state sizes from tensor shapes
    processor->state_size_1 = TfLiteTensorByteSize(input_tensor_1_1) / sizeof(float);
    processor->state_size_2 = TfLiteTensorByteSize(input_tensor_2_1) / sizeof(float);

    // Initialize LSTM states (single state tensors)
    processor->states_1 = calloc(processor->state_size_1, sizeof(float));
    processor->states_2 = calloc(processor->state_size_2, sizeof(float));

    // Initialize buffers for streaming processing
    processor->in_buffer = calloc(BLOCK_LEN, sizeof(float));
    processor->out_buffer = calloc(BLOCK_LEN, sizeof(float));

    // Allocate FFTW buffers (reused across calls)
    processor->fft_out = fftwf_alloc_complex(FFT_LEN/2 + 1);
    processor->estimated_complex = fftwf_alloc_complex(FFT_LEN/2 + 1);
    processor->estimated_block_raw = malloc(FFT_LEN * sizeof(float));

    // Allocate processing buffers (reused across audio blocks)
    processor->magnitude = malloc((FFT_LEN/2 + 1) * sizeof(float));
    processor->phase = malloc((FFT_LEN/2 + 1) * sizeof(float));
    processor->intermediate_block = malloc(BLOCK_LEN * sizeof(float));

    // Create FFTW plans (expensive operation, done once)
    processor->rfft_plan = fftwf_plan_dft_r2c_1d(FFT_LEN, processor->in_buffer, processor->fft_out, FFTW_ESTIMATE);
    processor->irfft_plan = fftwf_plan_dft_c2r_1d(FFT_LEN, processor->estimated_complex, processor->estimated_block_raw, FFTW_ESTIMATE);

    if (!processor->states_1 || !processor->states_2 ||
        !processor->in_buffer || !processor->out_buffer ||
        !processor->fft_out || !processor->estimated_complex || !processor->estimated_block_raw ||
        !processor->magnitude || !processor->phase || !processor->intermediate_block) {
        fprintf(stderr, "Failed to allocate LSTM states and buffers\n");

        // Clean up any successfully allocated memory and plans before failing
        if (processor->rfft_plan) fftwf_destroy_plan(processor->rfft_plan);
        if (processor->irfft_plan) fftwf_destroy_plan(processor->irfft_plan);
        if (processor->states_1) free(processor->states_1);
        if (processor->states_2) free(processor->states_2);
        if (processor->in_buffer) free(processor->in_buffer);
        if (processor->out_buffer) free(processor->out_buffer);
        if (processor->fft_out) fftwf_free(processor->fft_out);
        if (processor->estimated_complex) fftwf_free(processor->estimated_complex);
        if (processor->estimated_block_raw) free(processor->estimated_block_raw);
        if (processor->magnitude) free(processor->magnitude);
        if (processor->phase) free(processor->phase);
        if (processor->intermediate_block) free(processor->intermediate_block);

        return -1;
    }

    return 0;
}

// Process audio shift through DTLN (streaming implementation matching official code)
void dtln_process_shift(DTLNProcessor* processor, float* audio_shift, float* output_shift) {
    // Shift values and write to input buffer (official implementation style)
    memmove(processor->in_buffer, processor->in_buffer + BLOCK_SHIFT,
            (BLOCK_LEN - BLOCK_SHIFT) * sizeof(float));
    memcpy(processor->in_buffer + (BLOCK_LEN - BLOCK_SHIFT), audio_shift,
           BLOCK_SHIFT * sizeof(float));

    // === STAGE 1: Magnitude masking with Model 1 ===

    // Calculate real FFT of input buffer using pre-allocated plan
    fftwf_execute(processor->rfft_plan);

    // Extract magnitude and phase using pre-allocated buffers
    for (int i = 0; i < FFT_LEN/2 + 1; i++) {
        processor->magnitude[i] = sqrt(processor->fft_out[i][0] * processor->fft_out[i][0] + processor->fft_out[i][1] * processor->fft_out[i][1]);
        processor->phase[i] = atan2(processor->fft_out[i][1], processor->fft_out[i][0]);
    }

    // Prepare inputs for Model 1: [magnitude, states]
    TfLiteTensor* mag_input = TfLiteInterpreterGetInputTensor(processor->interpreter1, 0);
    TfLiteTensor* states_input1 = TfLiteInterpreterGetInputTensor(processor->interpreter1, 1);

    // Set magnitude input (reshape to [1, 1, 257])
    float* mag_data = (float*)TfLiteTensorData(mag_input);
    memcpy(mag_data, processor->magnitude, (FFT_LEN/2 + 1) * sizeof(float));

    // Set LSTM states
    float* states_data1 = (float*)TfLiteTensorData(states_input1);
    memcpy(states_data1, processor->states_1, processor->state_size_1 * sizeof(float));

    // Run Model 1
    TfLiteInterpreterInvoke(processor->interpreter1);

    // Get outputs: [mask, new_states]
    const TfLiteTensor* mask_output = TfLiteInterpreterGetOutputTensor(processor->interpreter1, 0);
    const TfLiteTensor* new_states1 = TfLiteInterpreterGetOutputTensor(processor->interpreter1, 1);

    const float* mask = (const float*)TfLiteTensorData(mask_output);

    // Update LSTM states for next iteration
    memcpy(processor->states_1, TfLiteTensorData(new_states1), processor->state_size_1 * sizeof(float));

    // Apply mask and reconstruct complex spectrum with original phase (using pre-allocated buffer)
    for (int i = 0; i < FFT_LEN/2 + 1; i++) {
        float masked_magnitude = processor->magnitude[i] * mask[i];
        processor->estimated_complex[i][0] = masked_magnitude * cos(processor->phase[i]);
        processor->estimated_complex[i][1] = masked_magnitude * sin(processor->phase[i]);
    }

    // Use pre-allocated FFTW plan for inverse transform
    fftwf_execute(processor->irfft_plan);

    // Extract intermediate time domain signal using pre-allocated buffer
    for (int i = 0; i < BLOCK_LEN; i++) {
        processor->intermediate_block[i] = processor->estimated_block_raw[i] / FFT_LEN;
    }

    // === STAGE 2: Time domain enhancement with Model 2 ===

    // Prepare inputs for Model 2: [time_block, states]
    TfLiteTensor* time_input = TfLiteInterpreterGetInputTensor(processor->interpreter2, 0);
    TfLiteTensor* states_input2 = TfLiteInterpreterGetInputTensor(processor->interpreter2, 1);

    // Set time domain input (reshape to [1, 1, 512])
    float* time_data = (float*)TfLiteTensorData(time_input);
    memcpy(time_data, processor->intermediate_block, BLOCK_LEN * sizeof(float));

    // Set LSTM states for Model 2
    float* states_data2 = (float*)TfLiteTensorData(states_input2);
    memcpy(states_data2, processor->states_2, processor->state_size_2 * sizeof(float));

    // Run Model 2
    TfLiteInterpreterInvoke(processor->interpreter2);

    // Get outputs: [enhanced_block, new_states]
    const TfLiteTensor* enhanced_output = TfLiteInterpreterGetOutputTensor(processor->interpreter2, 0);
    const TfLiteTensor* new_states2 = TfLiteInterpreterGetOutputTensor(processor->interpreter2, 1);

    const float* enhanced_block = (const float*)TfLiteTensorData(enhanced_output);

    // Update LSTM states for next iteration
    memcpy(processor->states_2, TfLiteTensorData(new_states2), processor->state_size_2 * sizeof(float));

    // Overlap-add to output buffer (official implementation style)
    memmove(processor->out_buffer, processor->out_buffer + BLOCK_SHIFT,
            (BLOCK_LEN - BLOCK_SHIFT) * sizeof(float));
    memset(processor->out_buffer + (BLOCK_LEN - BLOCK_SHIFT), 0, BLOCK_SHIFT * sizeof(float));

    // Add enhanced block to output buffer
    for (int i = 0; i < BLOCK_LEN; i++) {
        processor->out_buffer[i] += enhanced_block[i];
    }

    // Copy the output shift
    memcpy(output_shift, processor->out_buffer, BLOCK_SHIFT * sizeof(float));

    // No cleanup needed - all buffers are pre-allocated and reused
}

void dtln_cleanup(DTLNProcessor* processor) {
    if (processor->interpreter1) TfLiteInterpreterDelete(processor->interpreter1);
    if (processor->interpreter2) TfLiteInterpreterDelete(processor->interpreter2);
    if (processor->model1) TfLiteModelDelete(processor->model1);
    if (processor->model2) TfLiteModelDelete(processor->model2);
    if (processor->options) TfLiteInterpreterOptionsDelete(processor->options);

    // Free LSTM states and buffers
    if (processor->states_1) free(processor->states_1);
    if (processor->states_2) free(processor->states_2);
    if (processor->in_buffer) free(processor->in_buffer);
    if (processor->out_buffer) free(processor->out_buffer);

    // Destroy FFTW plans and free buffers
    if (processor->rfft_plan) fftwf_destroy_plan(processor->rfft_plan);
    if (processor->irfft_plan) fftwf_destroy_plan(processor->irfft_plan);
    if (processor->fft_out) fftwf_free(processor->fft_out);
    if (processor->estimated_complex) fftwf_free(processor->estimated_complex);
    if (processor->estimated_block_raw) free(processor->estimated_block_raw);

    // Free processing buffers
    if (processor->magnitude) free(processor->magnitude);
    if (processor->phase) free(processor->phase);
    if (processor->intermediate_block) free(processor->intermediate_block);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s input.wav resampled_output.wav denoised_output.wav\n", argv[0]);
        return -1;
    }

    const char* input_path = argv[1];
    const char* resampled_path = argv[2];
    const char* output_path = argv[3];

    // Open input file
    SF_INFO sf_info;
    SNDFILE* input_file = sf_open(input_path, SFM_READ, &sf_info);
    if (!input_file) {
        fprintf(stderr, "Failed to open input file: %s\n", input_path);
        return -1;
    }

    printf("Input: %d channels, %d Hz, %ld frames\n",
           sf_info.channels, sf_info.samplerate, sf_info.frames);

    // Read entire input file
    float* input_data = malloc(sf_info.frames * sf_info.channels * sizeof(float));
    sf_readf_float(input_file, input_data, sf_info.frames);
    sf_close(input_file);

    // Convert to mono if stereo
    float* mono_data = malloc(sf_info.frames * sizeof(float));
    if (sf_info.channels == 2) {
        stereo_to_mono(input_data, mono_data, sf_info.frames);
        printf("Converted stereo to mono\n");
    } else {
        memcpy(mono_data, input_data, sf_info.frames * sizeof(float));
        printf("Audio already mono\n");
    }
    free(input_data);

    // Resample to 16kHz if needed
    float* resampled_data;
    int resampled_frames;
    if (sf_info.samplerate != TARGET_SAMPLE_RATE) {
        resampled_data = malloc(sf_info.frames * RESAMPLING_BUFFER_FACTOR * sizeof(float));
        resample_audio(mono_data, sf_info.frames, sf_info.samplerate,
                      resampled_data, &resampled_frames, TARGET_SAMPLE_RATE);
        free(mono_data);
        printf("Resampled from %d Hz to %d Hz, %d frames\n", sf_info.samplerate, TARGET_SAMPLE_RATE, resampled_frames);
    } else {
        resampled_data = mono_data;
        resampled_frames = sf_info.frames;
        printf("Audio already at %d Hz, no resampling needed\n", TARGET_SAMPLE_RATE);
    }

    // Save resampled mono input for comparison (only if processing was needed)
    bool preprocessing_needed = (sf_info.channels != 1) || (sf_info.samplerate != TARGET_SAMPLE_RATE);

    if (preprocessing_needed) {
        SF_INFO resampled_info;
        resampled_info.samplerate = TARGET_SAMPLE_RATE;
        resampled_info.channels = 1;
        resampled_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

        SNDFILE* resampled_file = sf_open(resampled_path, SFM_WRITE, &resampled_info);
        if (resampled_file) {
            sf_writef_float(resampled_file, resampled_data, resampled_frames);
            sf_close(resampled_file);
            printf("Saved resampled input: %s\n", resampled_path);
        } else {
            fprintf(stderr, "Warning: Could not save resampled file: %s\n", resampled_path);
        }
    } else {
        printf("No preprocessing needed, skipping resampled file: %s\n", resampled_path);
    }

    // Initialize DTLN
    DTLNProcessor processor;
    if (dtln_init(&processor, "models/model_1.tflite", "models/model_2.tflite") != 0) {
        free(resampled_data);
        return -1;
    }

    // Open output file
    SF_INFO out_info;
    out_info.samplerate = TARGET_SAMPLE_RATE;
    out_info.channels = 1;
    out_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE* output_file = sf_open(output_path, SFM_WRITE, &out_info);
    if (!output_file) {
        fprintf(stderr, "Failed to open output file: %s\n", output_path);
        free(resampled_data);
        dtln_cleanup(&processor);
        return -1;
    }

    // Process audio using streaming approach (matching official implementation)
    float* output_data = malloc(resampled_frames * sizeof(float));

    // Calculate number of blocks as in official implementation
    int num_blocks = (resampled_frames - (BLOCK_LEN - BLOCK_SHIFT)) / BLOCK_SHIFT;

    // Pre-allocate output shift buffer (reused across all iterations)
    float* output_shift = malloc(BLOCK_SHIFT * sizeof(float));

    for (int idx = 0; idx < num_blocks; idx++) {
        // Get audio shift for this iteration
        float* audio_shift = resampled_data + (idx * BLOCK_SHIFT);

        // Process using streaming approach
        dtln_process_shift(&processor, audio_shift, output_shift);

        // Write block to output file (matching official implementation)
        memcpy(output_data + (idx * BLOCK_SHIFT), output_shift, BLOCK_SHIFT * sizeof(float));
    }

    // Write output (only the processed portion)
    int processed_frames = num_blocks * BLOCK_SHIFT;
    sf_writef_float(output_file, output_data, processed_frames);

    // Cleanup
    free(resampled_data);
    free(output_data);
    free(output_shift);
    sf_close(output_file);
    dtln_cleanup(&processor);

    printf("Denoising complete: %s -> %s\n", input_path, output_path);
    return 0;
}
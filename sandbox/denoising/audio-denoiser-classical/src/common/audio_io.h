#ifndef AUDIO_IO_H
#define AUDIO_IO_H

#include <sndfile.h>

#define TARGET_SAMPLE_RATE 16000

typedef struct {
    float* data;
    int frames;
    int channels;
    int samplerate;
} AudioData;

// Load audio file and convert to mono 16kHz
AudioData* load_audio(const char* filename);

// Save audio to file
int save_audio(const char* filename, float* data, int frames, int samplerate);

// Free audio data
void free_audio(AudioData* audio);

#endif

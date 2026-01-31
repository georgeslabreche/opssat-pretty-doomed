#ifndef AUDIO_IO_H
#define AUDIO_IO_H

#include <string>
#include <vector>

// Read WAV file into mono float samples normalized to [-1.0, 1.0].
// Multi-channel audio is downmixed to mono.
bool read_wav(const std::string& path, std::vector<float>& samples, int& sample_rate);

// Write mono float samples as 16-bit PCM WAV file.
bool write_wav(const std::string& path, const std::vector<float>& samples, int sample_rate);

#endif

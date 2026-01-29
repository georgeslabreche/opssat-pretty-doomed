#include "audio_io.h"
#include <sndfile.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>

bool read_wav(const std::string& path, std::vector<float>& samples, int& sample_rate) {
    SF_INFO sf_info;
    memset(&sf_info, 0, sizeof(sf_info));

    SNDFILE* sf = sf_open(path.c_str(), SFM_READ, &sf_info);
    if (!sf) {
        std::cerr << "Error: Cannot open input file: " << path << std::endl;
        return false;
    }

    sample_rate = sf_info.samplerate;
    int total_frames = sf_info.frames;
    int channels = sf_info.channels;

    // Read all frames (interleaved if multi-channel)
    std::vector<float> raw(total_frames * channels);
    sf_count_t frames_read = sf_readf_float(sf, raw.data(), total_frames);
    sf_close(sf);

    if (frames_read != total_frames) {
        std::cerr << "Warning: Read " << frames_read << " frames, expected "
                  << total_frames << std::endl;
        total_frames = frames_read;
    }

    // Downmix to mono
    samples.resize(total_frames);
    if (channels > 1) {
        for (int i = 0; i < total_frames; i++) {
            float sum = 0.0f;
            for (int c = 0; c < channels; c++) {
                sum += raw[i * channels + c];
            }
            samples[i] = sum / channels;
        }
    } else {
        samples = std::move(raw);
    }

    return true;
}

bool write_wav(const std::string& path, const std::vector<float>& samples, int sample_rate) {
    SF_INFO sf_info;
    memset(&sf_info, 0, sizeof(sf_info));
    sf_info.samplerate = sample_rate;
    sf_info.channels = 1;
    sf_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE* sf = sf_open(path.c_str(), SFM_WRITE, &sf_info);
    if (!sf) {
        std::cerr << "Error: Cannot create output file: " << path << std::endl;
        return false;
    }

    sf_writef_float(sf, samples.data(), samples.size());
    sf_close(sf);
    return true;
}

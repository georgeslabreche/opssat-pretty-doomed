#ifndef LOOPBACK_WAV_H
#define LOOPBACK_WAV_H

#include <string>
#include <vector>
#include <sndfile.h>
#include "pretty_log.h"

using namespace pretty;

// Read input WAV file via libsndfile. Returns duration in seconds, or -1 on error.
// Loads all samples into `samples` as float (deterministic type, no GNU Radio version dependency).
inline double read_input_wav(const std::string& path, int& audio_rate, long long& frame_count,
                      std::vector<float>& samples) {
    SF_INFO sf_info = {0};
    SNDFILE* sf = sf_open(path.c_str(), SFM_READ, &sf_info);
    if (!sf) {
        log_error() << "Could not open WAV file: " << path << "\n";
        log_error() << "Reason: " << sf_strerror(NULL) << "\n";
        return -1.0;
    }

    audio_rate = sf_info.samplerate;
    frame_count = sf_info.frames;
    int channels = sf_info.channels;
    int format = sf_info.format;

    if (channels > 1) {
        sf_close(sf);
        log_error() << "Input file has " << channels << " channels — must be mono\n";
        return -1.0;
    }
    if ((format & SF_FORMAT_SUBMASK) != SF_FORMAT_PCM_16) {
        sf_close(sf);
        log_error() << "Input WAV must be 16-bit PCM (use convert-sample.sh)\n";
        return -1.0;
    }
    if (audio_rate <= 0) {
        sf_close(sf);
        log_error() << "Invalid WAV file sample rate: " << audio_rate << "\n";
        return -1.0;
    }
    if (frame_count <= 0) {
        sf_close(sf);
        log_error() << "WAV file is empty or invalid\n";
        return -1.0;
    }

    samples.resize((size_t)frame_count);
    sf_count_t read = sf_readf_float(sf, samples.data(), frame_count);
    sf_close(sf);

    if (read <= 0) {
        log_error() << "Could not read samples from WAV file\n";
        samples.clear();
        return -1.0;
    }
    if (read < frame_count) {
        log_warning() << "WAV read returned " << read << "/" << frame_count << " frames\n";
        frame_count = read;
    }
    samples.resize((size_t)read);

    return (double)frame_count / audio_rate;
}

#endif // LOOPBACK_WAV_H

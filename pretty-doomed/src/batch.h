#ifndef BATCH_H
#define BATCH_H

// Pure helpers for file-input batch runs and windowed transcription (#142).
// No external dependencies: unit-tested in tests/test_batch.cpp.

#include <cstddef>
#include <istream>
#include <string>
#include <utility>
#include <vector>

// Window bounds [begin, end) for windowed transcription of n samples.
// window_s <= 0 gives one window over all samples (the v8 behavior).
// A window shorter than min_samples is dropped, except when it is the only window.
std::vector<std::pair<std::size_t, std::size_t>> stt_windows(std::size_t n,
                                                             int sample_rate,
                                                             double window_s,
                                                             std::size_t min_samples);

// Join the window transcripts with one space. Empty parts are skipped.
std::string join_transcripts(const std::vector<std::string>& parts);

// The .wav files in a directory (not recursive), sorted by name. Empty if dir is not a directory.
std::vector<std::string> list_wav_files(const std::string& dir);

// The file name without the directory and the extension: "/a/b/41096-kupreanof.wav" -> "41096-kupreanof".
std::string file_stem(const std::string& path);

// Peak resident memory (VmHWM) in kB from the text of /proc/self/status. -1 if not found.
long long parse_vmhwm_kb(std::istream& status);

// Peak resident memory of this process in kB. -1 if /proc/self/status is not available.
long long peak_rss_kb();

#endif

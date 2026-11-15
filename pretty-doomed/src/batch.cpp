#include "batch.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

std::vector<std::pair<std::size_t, std::size_t>> stt_windows(std::size_t n,
                                                             int sample_rate,
                                                             double window_s,
                                                             std::size_t min_samples) {
    std::vector<std::pair<std::size_t, std::size_t>> windows;
    if (n == 0) return windows;
    std::size_t step = window_s > 0 ? static_cast<std::size_t>(window_s * sample_rate) : n;
    if (step == 0) step = n;
    for (std::size_t begin = 0; begin < n; begin += step) {
        std::size_t end = std::min(n, begin + step);
        if (end - begin < min_samples && begin > 0) continue;
        windows.emplace_back(begin, end);
    }
    return windows;
}

std::string join_transcripts(const std::vector<std::string>& parts) {
    std::string joined;
    for (const auto& part : parts) {
        if (part.empty()) continue;
        if (!joined.empty()) joined += " ";
        joined += part;
    }
    return joined;
}

std::vector<std::string> list_wav_files(const std::string& dir) {
    namespace fs = std::filesystem;
    std::vector<std::string> files;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return files;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".wav") {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string file_stem(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

long long parse_vmhwm_kb(std::istream& status) {
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmHWM:", 0) == 0) {
            std::istringstream fields(line.substr(6));
            long long kb = -1;
            if (fields >> kb) return kb;
            return -1;
        }
    }
    return -1;
}

long long peak_rss_kb() {
    std::ifstream status("/proc/self/status");
    if (!status) return -1;
    return parse_vmhwm_kb(status);
}

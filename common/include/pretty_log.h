/*
 * pretty_log.h - Timestamped logging helpers for OPS-SAT PRETTY
 *
 * Header-only. Returns std::ostream& for << chaining.
 */
#ifndef PRETTY_LOG_H
#define PRETTY_LOG_H

#include <chrono>
#include <ctime>
#include <iostream>
#include <string>

namespace pretty {

inline std::tm local_tm(std::time_t t) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

inline std::ostream& log_info() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto tm = local_tm(t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "[%Y-%m-%d %H:%M:%S] ", &tm);
    return std::cout << buf;
}

inline std::ostream& log_warning() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto tm = local_tm(t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "[%Y-%m-%d %H:%M:%S] ", &tm);
    return std::cerr << buf << "WARNING: ";
}

inline std::ostream& log_error() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto tm = local_tm(t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "[%Y-%m-%d %H:%M:%S] ", &tm);
    return std::cerr << buf;
}

inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

} // namespace pretty

#endif // PRETTY_LOG_H

/*
 * pretty_log.h - Timestamped logging helpers for OPS-SAT PRETTY
 *
 * Header-only. Returns std::ostream& for << chaining.
 */
#ifndef PRETTY_LOG_H
#define PRETTY_LOG_H

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>

#ifdef __linux__
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

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

namespace detail {
inline void format_timestamp(char* buf, size_t len) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    auto tm = local_tm(t);
    int n = (int)std::strftime(buf, len, "[%Y-%m-%d %H:%M:%S", &tm);
#ifdef __linux__
    long tid = syscall(SYS_gettid);
    int cpu = sched_getcpu();
    std::snprintf(buf + n, len - n, ".%03d][c%d/t%ld] ", (int)ms, cpu, tid);
#else
    std::snprintf(buf + n, len - n, ".%03d] ", (int)ms);
#endif
}
} // namespace detail

inline std::ostream& log_info() {
    char buf[64];
    detail::format_timestamp(buf, sizeof(buf));
    return std::cout << buf;
}

inline std::ostream& log_warning() {
    char buf[64];
    detail::format_timestamp(buf, sizeof(buf));
    return std::cerr << buf << "WARNING: ";
}

inline std::ostream& log_error() {
    char buf[64];
    detail::format_timestamp(buf, sizeof(buf));
    return std::cerr << buf;
}

inline std::string trim(const std::string& s) {
    // Strip trailing NUL bytes first (libiio includes NUL in byte count)
    size_t len = s.size();
    while (len > 0 && s[len - 1] == '\0') --len;
    std::string stripped = s.substr(0, len);
    const std::string ws(" \t\r\n");
    size_t start = stripped.find_first_not_of(ws);
    if (start == std::string::npos) return "";
    size_t end = stripped.find_last_not_of(ws);
    return stripped.substr(start, end - start + 1);
}

} // namespace pretty

#endif // PRETTY_LOG_H

/*
 * pretty_signal.h - Signal handling for OPS-SAT PRETTY
 *
 * Header-only. Provides global flags and a signal handler for graceful
 * shutdown on SIGINT/SIGTERM.
 */
#ifndef PRETTY_SIGNAL_H
#define PRETTY_SIGNAL_H

#include <csignal>

namespace pretty {

// Only volatile sig_atomic_t is async-signal-safe
inline volatile sig_atomic_t g_running = 1;
inline volatile sig_atomic_t g_signal_received = 0;

inline void signal_handler(int signum) {
    g_signal_received = signum;
    g_running = 0;
}

} // namespace pretty

#endif // PRETTY_SIGNAL_H

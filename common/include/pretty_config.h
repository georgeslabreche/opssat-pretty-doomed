/*
 * pretty_config.h - KEY=VALUE config file parser for OPS-SAT PRETTY
 *
 * Header-only. Parses config files with `#` comments and KEY=VALUE lines.
 * Returns a map that consuming code maps to its own config struct.
 */
#ifndef PRETTY_CONFIG_H
#define PRETTY_CONFIG_H

#include <fstream>
#include <string>
#include <unordered_map>

#include "pretty_log.h"

namespace pretty {

// Parse a KEY=VALUE config file. Lines starting with `#` and blank lines are
// skipped.  Returns an ordered list of (key, value) pairs. Duplicate keys are
// allowed — later values win when inserted into a map.
// On file-open failure, logs an error and returns an empty map with ok=false.
struct ConfigResult {
    std::unordered_map<std::string, std::string> values;
    bool ok = false;
};

inline ConfigResult load_config_map(const std::string& path) {
    ConfigResult result;

    std::ifstream file(path);
    if (!file.is_open()) {
        log_error() << "Could not open config file: " << path << "\n";
        return result;
    }

    std::string line;
    int line_no = 0;
    while (std::getline(file, line)) {
        line_no++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        result.values[key] = value;
    }

    result.ok = true;
    return result;
}

} // namespace pretty

#endif // PRETTY_CONFIG_H

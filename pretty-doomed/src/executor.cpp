#include "executor.h"
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <ctime>

static std::string ts() {
    time_t now = time(nullptr);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return buf;
}

static std::string abs_path(const std::string& path) {
    char* resolved = realpath(path.c_str(), nullptr);
    if (resolved) {
        std::string result(resolved);
        free(resolved);
        return result;
    }
    return path;
}

std::vector<std::string> find_demo_files(const std::string& demos_dir) {
    std::vector<std::string> demos;
    DIR* dir = opendir(demos_dir.c_str());
    if (!dir) return demos;

    const std::string ext = ".lmp";
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name[0] == '.') continue;

        // Look for .lmp demo files
        if (name.size() <= ext.size() ||
            name.compare(name.size() - ext.size(), ext.size(), ext) != 0) continue;

        // Check it's a regular file
        std::string path = demos_dir + "/" + name;
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            demos.push_back(name.substr(0, name.size() - ext.size()));
        }
    }
    closedir(dir);
    std::sort(demos.begin(), demos.end());
    return demos;
}

// Read the raw run counter from state file.
static size_t read_demo_index(const std::string& state_file) {
    size_t idx = 0;
    std::ifstream in(state_file);
    if (in) {
        in >> idx;
    }
    return idx;
}

static void write_demo_index(const std::string& state_file, size_t next_idx) {
    std::ofstream out(state_file);
    if (out) {
        out << next_idx << "\n";
    }
}

// Resolve a frames spec for a given demo run.
// "-1" → random frame in [100, maxframes-50].
// Comma-separated integers (no dashes) → cycle through list by run_cycle.
// Otherwise → pass through unchanged (single number or GIF ranges).
static std::string resolve_frames(const std::string& spec,
                                   const std::string& demo,
                                   size_t run_cycle,
                                   const std::unordered_map<std::string, int>& maxframes_map) {
    if (spec == "-1") {
        auto it = maxframes_map.find(demo);
        int maxf = (it != maxframes_map.end()) ? it->second : 500;
        int lo = 100;
        int hi = std::max(lo + 1, maxf - 50);
        srand(static_cast<unsigned>(time(nullptr)));
        int frame = lo + rand() % (hi - lo + 1);
        return std::to_string(frame);
    }

    // Comma-separated list without dashes → cycling list
    if (spec.find('-') == std::string::npos && spec.find(',') != std::string::npos) {
        std::vector<std::string> items;
        std::istringstream ss(spec);
        std::string token;
        while (std::getline(ss, token, ',')) {
            if (!token.empty()) items.push_back(token);
        }
        if (!items.empty()) {
            return items[run_cycle % items.size()];
        }
    }

    return spec;
}

// Fork+exec DOOM for a single demo. Returns 0 on success, 1 on failure.
static int exec_doom(const std::string& doom_binary,
                     const std::string& wad_path,
                     const std::string& demo_path,
                     const std::string& statdump_path,
                     const std::string& framedir,
                     const std::string& log_path,
                     const std::string& demo,
                     const std::unordered_map<std::string, std::string>& frames_map,
                     bool keepgifframes) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child: redirect stdout/stderr to log
        FILE* log = fopen(log_path.c_str(), "a");
        if (log) {
            dup2(fileno(log), STDOUT_FILENO);
            dup2(fileno(log), STDERR_FILENO);
            fclose(log);
        }

        std::vector<std::string> args = {
            doom_binary,
            "-nosound", "-nomusic", "-nosfx",
            "-runid", "1",
            "-longtics",
            "-iwad", wad_path,
            "-cdemo", demo_path,
            "-statdump", statdump_path,
            "-framedir", framedir
        };

        auto it = frames_map.find(demo);
        if (it != frames_map.end() && !it->second.empty()) {
            args.push_back("-frames");
            args.push_back(it->second);
            if (keepgifframes) {
                args.push_back("-keepgifframes");
            }
        }

        std::vector<char*> argv;
        for (auto& a : args) argv.push_back(&a[0]);
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        perror("execvp failed");
        _exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            std::cerr << "  Warning: DOOM exited with code " << WEXITSTATUS(status)
                      << " for demo " << demo << std::endl;
            return 1;
        }
        return 0;
    } else {
        std::cerr << "Error: fork() failed for demo " << demo << std::endl;
        return 1;
    }
}

// Validate statdump against reference file. Writes result to results_path.
// Returns 1 on mismatch, 0 otherwise.
static int validate_statdump(const std::string& results_path,
                             const std::string& stats_file,
                             const std::string& ref_file,
                             const std::string& demo) {
    std::ofstream results(results_path);
    if (!results) return 0;

    std::ifstream sf(stats_file);
    std::ifstream rf(ref_file);

    if (!sf || !rf) {
        results << "SKIP - " << demo << "\n";
        return 0;
    }

    std::string stats_content((std::istreambuf_iterator<char>(sf)),
                               std::istreambuf_iterator<char>());
    std::string ref_content((std::istreambuf_iterator<char>(rf)),
                             std::istreambuf_iterator<char>());

    if (stats_content == ref_content) {
        results << "OK - " << demo << "\n";
        return 0;
    } else {
        results << "ERROR - " << demo << "\n";
        return 1;
    }
}

DoomResult run_doom(const std::string& doom_binary,
                    const std::string& demos_dir,
                    const std::string& output_dir,
                    const std::unordered_map<std::string, std::string>& frames_map,
                    const std::unordered_map<std::string, int>& maxframes_map,
                    bool keepgifframes,
                    const std::vector<std::string>& demo_order) {
    std::string wad_path = demos_dir + "/doom.wad";
    struct stat st;
    if (stat(wad_path.c_str(), &st) != 0) {
        std::cerr << "Error: doom.wad not found in " << demos_dir << std::endl;
        return {-1, "", ""};
    }

    // Use custom demo order if provided, otherwise alphabetical from directory
    std::vector<std::string> demos = demo_order.empty()
        ? find_demo_files(demos_dir) : demo_order;
    if (demos.empty()) {
        std::cerr << "Warning: No demo files found in " << demos_dir << std::endl;
        return {0, "", ""};
    }

    // Cycle through demos: pick one per run via state file
    std::string parent_dir = output_dir;
    auto slash = parent_dir.find_last_of('/');
    if (slash != std::string::npos) {
        parent_dir = parent_dir.substr(0, slash);
    }
    std::string state_file = parent_dir + "/doom_demo_index.txt";

    size_t raw_idx = read_demo_index(state_file);
    size_t demo_idx = raw_idx % demos.size();
    size_t run_cycle = raw_idx / demos.size();
    const std::string& demo = demos[demo_idx];
    write_demo_index(state_file, raw_idx + 1);

    // Setup output directory (single demo per run, no runs/ subdirectory)
    std::string demo_output_dir = output_dir + "/" + demo;
    mkdir(demo_output_dir.c_str(), 0755);

    // Resolve paths (fork+exec needs absolute paths)
    std::string abs_demo_path = abs_path(demos_dir + "/" + demo + ".lmp");
    if (abs_demo_path.size() > 4) {
        abs_demo_path = abs_demo_path.substr(0, abs_demo_path.size() - 4);
    }
    std::string abs_output_dir = abs_path(output_dir);
    std::string abs_demo_output_dir = abs_path(demo_output_dir);
    std::string statdump_path = abs_demo_output_dir + "/stats.txt";

    // Resolve frame spec (random / cycling / pass-through)
    std::unordered_map<std::string, std::string> effective_frames = frames_map;
    auto fit = effective_frames.find(demo);
    if (fit != effective_frames.end()) {
        fit->second = resolve_frames(fit->second, demo, run_cycle, maxframes_map);
    }

    std::cout << "[" << ts() << "] " << "  Running DOOM demo: " << demo
              << " (" << (demo_idx + 1) << "/" << demos.size() << ")" << std::endl;

    int failures = exec_doom(abs_path(doom_binary), abs_path(wad_path),
                             abs_demo_path, statdump_path,
                             abs_demo_output_dir,
                             abs_output_dir + "/doom.log",
                             demo, effective_frames, keepgifframes);

    failures += validate_statdump(abs_output_dir + "/results.log",
                                  abs_demo_output_dir + "/stats.txt",
                                  abs_path(demos_dir + "/" + demo + ".txt"),
                                  demo);

    std::cout << "[" << ts() << "] " << "  Completed demo: " << demo << std::endl;
    return {failures, demo, demo_output_dir};
}

std::string find_doom_frame(const std::string& demo_dir) {
    DIR* dir = opendir(demo_dir.c_str());
    if (!dir) return "";

    std::string gif_path, jpg_path;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() > 4) {
            std::string ext = name.substr(name.size() - 4);
            if (ext == ".gif" && gif_path.empty())
                gif_path = demo_dir + "/" + name;
            else if (ext == ".jpg" && jpg_path.empty())
                jpg_path = demo_dir + "/" + name;
        }
    }
    closedir(dir);

    // Prefer GIF over JPG
    return !gif_path.empty() ? gif_path : jpg_path;
}

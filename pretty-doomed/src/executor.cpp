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
#include <ctime>

static std::string ts() {
    time_t now = time(nullptr);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return buf;
}

static std::vector<std::string> find_demo_files(const std::string& demos_dir) {
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
            // Store base name without .lmp (doom binary appends it internally)
            demos.push_back(name.substr(0, name.size() - ext.size()));
        }
    }
    closedir(dir);
    std::sort(demos.begin(), demos.end());
    return demos;
}

int run_doom(const std::string& doom_binary,
             const std::string& demos_dir,
             const std::string& output_dir) {
    // Find doom.wad in demos_dir
    std::string wad_path = demos_dir + "/doom.wad";
    struct stat st;
    if (stat(wad_path.c_str(), &st) != 0) {
        std::cerr << "Error: doom.wad not found in " << demos_dir << std::endl;
        return -1;
    }

    // Find demo files
    std::vector<std::string> demos = find_demo_files(demos_dir);
    if (demos.empty()) {
        std::cerr << "Warning: No demo files found in " << demos_dir << std::endl;
        return 0;
    }

    std::string runs_dir = output_dir + "/runs";
    mkdir(runs_dir.c_str(), 0755);

    // Resolve to absolute paths before chdir in child
    auto abs_path = [](const std::string& path) -> std::string {
        char* resolved = realpath(path.c_str(), nullptr);
        if (resolved) {
            std::string result(resolved);
            free(resolved);
            return result;
        }
        return path;
    };

    std::string abs_doom_binary = abs_path(doom_binary);
    std::string abs_wad_path = abs_path(wad_path);
    std::string abs_output_dir = abs_path(output_dir);

    std::string abs_runs_dir = abs_path(runs_dir);
    std::string doom_log = abs_runs_dir + "/doom.log";
    int run_id = 1;
    int failures = 0;

    for (const auto& demo : demos) {
        std::string demo_output_dir = runs_dir + "/" + demo;
        mkdir(demo_output_dir.c_str(), 0755);

        // Resolve the .lmp file path then strip the extension
        // (doom's -cdemo appends .lmp internally)
        std::string abs_demo_path = abs_path(demos_dir + "/" + demo + ".lmp");
        if (abs_demo_path.size() > 4) {
            abs_demo_path = abs_demo_path.substr(0, abs_demo_path.size() - 4);
        }
        std::string statdump_path = abs_path(demo_output_dir) + "/stats.txt";

        std::string abs_demo_output_dir = abs_path(demo_output_dir);

        std::cout << "[" << ts() << "] " << "  Running DOOM demo: " << demo << std::endl;

        pid_t pid = fork();
        if (pid == 0) {
            // Child: redirect stdout/stderr to doom.log
            FILE* log = fopen(doom_log.c_str(), "a");
            if (log) {
                dup2(fileno(log), STDOUT_FILENO);
                dup2(fileno(log), STDERR_FILENO);
                fclose(log);
            }

            std::string run_id_str = std::to_string(run_id);

            execlp(abs_doom_binary.c_str(), abs_doom_binary.c_str(),
                   "-nosound", "-nomusic", "-nosfx",
                   "-runid", run_id_str.c_str(),
                   "-longtics",
                   "-iwad", abs_wad_path.c_str(),
                   "-cdemo", abs_demo_path.c_str(),
                   "-statdump", statdump_path.c_str(),
                   "-framedir", abs_demo_output_dir.c_str(),
                   nullptr);

            // If execlp returns, it failed
            perror("execlp failed");
            _exit(1);
        } else if (pid > 0) {
            // Parent: wait for child
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                std::cerr << "  Warning: DOOM exited with code " << WEXITSTATUS(status)
                          << " for demo " << demo << std::endl;
                failures++;
            }
        } else {
            std::cerr << "Error: fork() failed for demo " << demo << std::endl;
            failures++;
        }

        run_id++;
    }

    // Write results.log: validate statdump against reference files
    std::string results_path = abs_runs_dir + "/results.log";
    std::ofstream results(results_path);
    if (results) {
        for (const auto& demo : demos) {
            std::string stats_file = abs_runs_dir + "/" + demo + "/stats.txt";
            std::string ref_file = abs_path(demos_dir + "/" + demo + ".txt");

            std::ifstream sf(stats_file);
            std::ifstream rf(ref_file);

            if (!sf || !rf) {
                results << "SKIP - " << demo << "\n";
                continue;
            }

            std::string stats_content((std::istreambuf_iterator<char>(sf)),
                                       std::istreambuf_iterator<char>());
            std::string ref_content((std::istreambuf_iterator<char>(rf)),
                                     std::istreambuf_iterator<char>());

            if (stats_content == ref_content) {
                results << "OK - " << demo << "\n";
            } else {
                results << "ERROR - " << demo << "\n";
                failures++;
            }
        }
    }

    std::cout << "[" << ts() << "] " << "  Completed " << demos.size() << " demo(s)" << std::endl;
    return failures;
}

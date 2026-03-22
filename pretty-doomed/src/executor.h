#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <string>
#include <vector>
#include <unordered_map>

// Result from run_doom()
struct DoomResult {
    int failures = 0;
    std::string demo_name;     // e.g., "e1m7-607"
    std::string demo_dir;      // e.g., output_dir + "/e1m7-607"
};

// Find .lmp demo files in a directory. Returns sorted base names (without extension).
std::vector<std::string> find_demo_files(const std::string& demos_dir);

// Run one DOOM demo (cycling through available demos across runs).
// Output goes to output_dir/<demo-name>/{stats.txt, doom.log, *.gif, *.jpg}
// frames_map: demo name -> frames spec. Special values:
//   "-1"             = random frame (requires maxframes_map entry)
//   "400,300,500"    = cycle through frames across runs (no dash ranges)
//   "8000,7992-8025" = pass through (dash ranges produce GIFs)
// maxframes_map: demo name -> total frame count (for random selection).
// demo_order: optional cycle order. If empty, uses alphabetical order from find_demo_files().
DoomResult run_doom(const std::string& doom_binary,
                    const std::string& demos_dir,
                    const std::string& output_dir,
                    const std::unordered_map<std::string, std::string>& frames_map = {},
                    const std::unordered_map<std::string, int>& maxframes_map = {},
                    bool keepgifframes = false,
                    const std::vector<std::string>& demo_order = {});

// Find the first DOOM frame file (prefer .gif over .jpg) in a demo output directory.
std::string find_doom_frame(const std::string& demo_dir);

#endif

#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <string>
#include <vector>
#include <unordered_map>

// Find .lmp demo files in a directory. Returns sorted base names (without extension).
std::vector<std::string> find_demo_files(const std::string& demos_dir);

// Run one DOOM demo (cycling through available demos across runs).
// Output goes to output_dir/<demo-name>/{stats.txt, doom.log, *.gif, *.jpg}
// frames_map: demo name -> frames spec. Special values:
//   "-1"             = random frame (requires maxframes_map entry)
//   "400,300,500"    = cycle through frames across runs (no dash ranges)
//   "8000,7992-8025" = pass through (dash ranges produce GIFs)
// maxframes_map: demo name -> total frame count (for random selection).
int run_doom(const std::string& doom_binary,
             const std::string& demos_dir,
             const std::string& output_dir,
             const std::unordered_map<std::string, std::string>& frames_map = {},
             const std::unordered_map<std::string, int>& maxframes_map = {},
             bool keepgifframes = false);

#endif

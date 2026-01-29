#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <string>
#include <vector>

// Run DOOM for each demo file found in demos_dir.
// Output goes to output_dir/runs/<demo-name>/{stats.txt, frame-XXXXXX.jpg}
int run_doom(const std::string& doom_binary,
             const std::string& demos_dir,
             const std::string& output_dir);

#endif

#ifndef PIPELINE_H
#define PIPELINE_H

#include <string>
#include "config.h"
#include "transcriber.h"

// Process a single WAV file through the pipeline: DSP -> STT -> detect -> DOOM.
// Returns true if a command was detected.
bool process_wav(const std::string& input_file,
                 const std::string& output_dir,
                 const PipelineConfig& cfg,
                 const VariantsMap& variants,
                 Transcriber& stt,
                 const std::string& config_file,
                 const std::string& doom_binary,
                 const std::string& demos_dir);

#endif

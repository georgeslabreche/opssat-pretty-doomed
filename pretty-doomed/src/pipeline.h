#ifndef PIPELINE_H
#define PIPELINE_H

#include <string>
#include "config.h"
#include "transcriber.h"

// Result of the STT stage, passed to the execution stage.
struct PipelineStageResult {
    bool command_detected = false;
    bool trigger = false;           // command detected OR force-triggered
    std::string transcript;
    std::string detection_timestamp; // UTC timestamp at detection time
    std::string ascii_art;           // ASCII art for DOOM banner
};

// Stage 1: DSP -> STT -> Detection -> write scores/summary.
// Must run serially (Transcriber is not thread-safe).
PipelineStageResult process_wav_stt(const std::string& input_file,
                                    const std::string& output_dir,
                                    const PipelineConfig& cfg,
                                    const VariantsMap& variants,
                                    Transcriber& stt,
                                    const std::string& config_file);

// Stage 2: DOOM execution -> Postcard -> results.txt.
// Can run concurrently with the next capture's STT stage.
void process_wav_exec(const PipelineStageResult& stage1,
                      const std::string& output_dir,
                      const PipelineConfig& cfg,
                      const std::string& doom_binary,
                      const std::string& demos_dir,
                      const std::string& sc16_path);

// Combined: runs both stages sequentially (used by sequential mode and file input).
// Returns true if a command was detected.
bool process_wav(const std::string& input_file,
                 const std::string& output_dir,
                 const PipelineConfig& cfg,
                 const VariantsMap& variants,
                 Transcriber& stt,
                 const std::string& config_file,
                 const std::string& doom_binary,
                 const std::string& demos_dir,
                 const std::string& sc16_path = "");

#endif

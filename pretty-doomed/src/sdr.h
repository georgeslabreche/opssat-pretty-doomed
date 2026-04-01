#ifndef SDR_H
#define SDR_H

#include "config.h"

// Configure AD9361 via libiio (hardware FIR or software-only path).
// Creates and destroys its own IIO context. AD9361 register state
// persists in hardware for subsequent flowgraph connections.
bool ad9361_configure(const PipelineConfig& cfg);

// Disable hardware FIR so subsequent experiments are not affected.
// No-op if hardware FIR is not enabled in the config.
// Returns true on success or if cleanup was not needed.
bool ad9361_cleanup_fir(const PipelineConfig& cfg);

#endif

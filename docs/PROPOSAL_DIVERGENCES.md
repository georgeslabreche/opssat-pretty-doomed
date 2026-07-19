# Proposal Divergences

Changes from the [original proposal](PROPOSAL.md) based on testing, hardware constraints, and OPS-SAT PRETTY platform differences from OPS-SAT-1.

## Audio Denoising

**Proposal:** DTLN (Dual-signal Transformation LSTM Network) neural denoiser using TensorFlow Lite as a preprocessing step.

**Implemented:** Dropped. Classical and neural denoising methods (DTLN, spectral subtraction, adaptive filtering, Wiener, ALE) were all evaluated in `sandbox/denoising/`. None reliably handled the non-stationary radio interference in OPS-SAT samples. The pipeline instead relies on bandpass filtering (300-3400 Hz voice band) combined with a robust STT model and fuzzy matching with externalized variant lists.

## STT Engine

**Proposal:** Method TBD (Whisper and Vosk listed as candidates).

**Implemented:** Sherpa-ONNX (zipformer-small, int8 quantized, ~27 MB). Whisper, Vosk, and PocketSphinx were evaluated in `sandbox/speech-to-text/`. Sherpa-ONNX offered the best balance of accuracy, model size (half of Whisper tiny at ~75 MB), and C API simplicity for ARM32 deployment.

## Command Detection

**Proposal:** Method TBD.

**Implemented:** Post-transcription fuzzy matching with Levenshtein distance. The transcription is scanned word-by-word for the wake word ("PRETTY"), call signs, and the command ("DOOM"/"PLAY DOOM"). Known misrecognition patterns (derived from BPE token analysis) are stored in `variants.cfg`. Hotword boosting was evaluated but degraded accuracy with BPE-based models.

## Voice Command Format

**Proposal:** "PRETTY, Play DOOM."

**Implemented:** "[CALL_SIGN], PRETTY PRETTY, PLEASE PLAY DOOM DOOM DOOM." Call signs were added for radio amateur identification and selected based on BPE token count (single-token words like NIGHT, LIGHT transcribe more reliably than multi-token NATO alphabet words); the repetition survives a noisy link.

## SDR Integration

**Proposal:** Steps 2-5 as separate processes with file I/O between them.

**Implemented:** Integrated pipeline. The SDR capture (AD9361 RX, FM demod, filtering) and STT processing run in a single binary (`pretty-doomed`). SDR capture writes WAV and sc16 files for diagnostics, then the WAV is processed through the pipeline in memory. Two processing modes are available: sequential (capture all, process all) and background (process previous capture while next one runs).

## Streaming

**Proposal:** Open question about whether steps 2-5 could be streamed.

**Implemented:** Not streamed. The pipeline operates on complete captures. STT takes ~288s for 22s of audio on ARM32, making streaming impractical. The multi-capture loop with background processing is the compromise: captures run back-to-back to maximize RF time, and processing overlaps with the next capture on the second core.

## Follow-up Commands

**Proposal:** "Restart SEPP" or "Enter Safe Mode" as follow-up experiments.

**Status:** Not implemented. The experiment focuses on the DOOM voice command as a proof of concept. More significant telecommands would require coordination with mission operations.

## Platform

**Proposal:** Referenced OPS-SAT-1 for proof of concept (radio amateur broadcast captured onboard, GNU Radio and TensorFlow Lite ran successfully).

**Implemented:** OPS-SAT PRETTY is the target spacecraft (as originally intended). The SEPP runs Alpine Linux on a dual-core ARM32 with 768 MB RAM and an AD9361 SDR. OPS-SAT-1 heritage informed the design but is a different platform.

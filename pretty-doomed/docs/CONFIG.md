# Configuration Reference

All config keys use semantic prefixes: `dsp_`, `stt_`, `detect_`, `doom_`, `sdr_`.

## Operation

The `operation` key selects what action the pipeline executes when a voice command is detected. All operation-specific config keys are prefixed with the operation name (e.g., `doom_*`). The pipeline checks this value before dispatching to the appropriate handler.

| Key | Default | Description |
|-----|---------|-------------|
| `operation` | doom | Operation to trigger on command detection. Currently only `doom` is implemented. |

Currently supported operations:
- `doom` -- runs a DOOM demo, captures frames, generates a postcard. All `doom_*` config keys apply.

## DSP: Signal Processing

| Key | Default | Description |
|-----|---------|-------------|
| `dsp_lowpass_cutoff` | 3400 | Lowpass filter cutoff frequency in Hz |
| `dsp_lowpass_transition` | 500 | Lowpass transition bandwidth in Hz |
| `dsp_bandpass_low` | 300 | Bandpass lower edge in Hz |
| `dsp_bandpass_high` | 3400 | Bandpass upper edge in Hz |
| `dsp_bandpass_transition` | 100 | Bandpass transition bandwidth in Hz |

## STT: Speech-to-Text

| Key | Default | Description |
|-----|---------|-------------|
| `stt_model_encoder` | (required) | Sherpa-ONNX encoder model path |
| `stt_model_decoder` | (required) | Sherpa-ONNX decoder model path |
| `stt_model_joiner` | (required) | Sherpa-ONNX joiner model path |
| `stt_model_tokens` | (required) | Sherpa-ONNX tokens file path |
| `stt_decoding_method` | modified_beam_search | Decoding method |
| `stt_num_threads` | 1 | Number of inference threads |

## Detection: Command Recognition

| Key | Default | Description |
|-----|---------|-------------|
| `detect_wake_word` | PRETTY | Wake word to listen for |
| `detect_call_signs` | (empty) | Comma-separated call signs to detect |
| `detect_command` | DOOM | Comma-separated commands (e.g., `DOOM,PLAY DOOM`) |
| `detect_fuzzy_max_distance` | 2 | Max Levenshtein distance for fuzzy matching |

Scoring: exact matches = 2 points, fuzzy matches = 1 point.

## DOOM: Execution and Postcard

| Key | Default | Description |
|-----|---------|-------------|
| `doom_frames_<demo>` | (none) | Frame spec per demo: integer, range, `-1` (random), or comma list (cycling) |
| `doom_maxframes_<demo>` | (none) | Total frame count per demo (for random frame selection) |
| `doom_keepgifframes` | false | Keep individual GIF frame JPEGs |
| `doom_demo_order` | (alphabetical) | Comma-separated demo cycling order |
| `doom_force_trigger` | false | Force DOOM launch regardless of detection (for testing) |
| `doom_enable_postcard` | true | Generate DOOM-themed composite postcard |
| `doom_postcard_scale` | 1 | Postcard resolution multiplier (1 for 1x, 2 for 2x, 3 for 3x) |

## SDR: Capture Settings

Used with the `-s` flag.

| Key | Default | Description |
|-----|---------|-------------|
| `sdr_frequency` | 1296000000 | RX frequency in Hz |
| `sdr_rate` | 2400000 | AD9361 sample rate in Hz |
| `sdr_decimation` | 12 | LPF decimation factor |
| `sdr_rf_bandwidth` | 200000 | Analog filter bandwidth in Hz |
| `sdr_gain` | 50 | RX gain in dB |
| `sdr_fm_deviation` | 5000 | FM deviation in Hz |
| `sdr_uri` | local: | IIO URI (`local:` for hardware, `ip:host:port` for emulator) |
| `sdr_duration` | 20 | Capture duration in seconds |
| `sdr_max_iq_mb` | 20 | Max I/Q file size in MiB |
| `sdr_audio_rate` | 16000 | Output audio sample rate in Hz |
| `sdr_timeout_multiplier` | 5 | Timeout = duration * N + 10 seconds |
| `sdr_min_readback` | false | Downgrade sample rate readback mismatch to warning |
| `sdr_enable_spectrogram` | true | Generate spectrogram BMP |
| `sdr_enable_constellation` | true | Generate I/Q constellation BMP |
| `sdr_captures` | 3 | Number of sequential SDR captures |
| `process_mode` | sequential | Processing mode: `sequential` or `background` |

## Variants File (`variants.cfg`)

Maps target words to known misrecognition patterns (BPE token decomposition):

```
DOOM=DO,DU,DUE,DUNE,DUAL,TUBE,TOOM,DUEL,DOOM
PLAY=UPLI,PLY,PLEA,PLATE,PLANE,PLAY
PRETTY=PRITY,PRITI,PRETY,PREETY,PREATY,PRETTY
```

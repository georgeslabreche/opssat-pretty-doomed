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
| `stt_num_threads` | 1 | Number of ONNX Runtime inference threads. Set to 2 on dual-core systems (e.g., OPS-SAT SEPP) to use both cores during STT inference. |
| `stt_concurrent_load` | false | In background mode, load STT model on a background thread concurrently with the first SDR capture. Saves ~16s on the SEPP but increases CPU contention during capture. No effect in sequential mode. |

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

Used with the `-s` flag. AD9361 configuration (`ad9361_configure()` in `sdr.cpp`) runs once before the capture loop by default. Setting `sdr_init_per_capture=true` re-configures the AD9361 from scratch at the beginning of each capture as a defensive measure against IIO driver state issues. The per-capture overhead is negligible for the software-only path, but with hardware FIR enabled `ad9361_set_bb_rate_custom_filter_manual()` adds ~4.5s per capture on ARM32. Note: the GNU Radio flowgraph (IIO buffer connection, signal processing blocks) is still created and destroyed per capture regardless of this setting. The init-once optimization applies to the AD9361 register configuration (LO, gain, sample rate, FIR taps), not the flowgraph lifecycle. GNU Radio `top_block` does not support stop-then-restart, so the flowgraph must be rebuilt each capture.

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
| `sdr_enable_psd` | true | Generate PSD (Power Spectral Density) BMP + CSV per capture, and cross-capture PSD comparison BMP |
| `sdr_keep_sc16` | false | Keep raw I/Q `capture.sc16` files after processing. When false, each sc16 is deleted after all artifacts (spectrogram, constellation, PSD, postcard) are generated, saving ~15 MB per 20s capture. |
| `sdr_captures` | 3 | Number of sequential SDR captures |
| `process_mode` | sequential | Processing mode: `sequential` (all captures then all processing) or `background` (overlap capture N+1 with processing of capture N). Background mode writes all output to the main log with `[cN/tM]` thread tags instead of per-capture `run.log` files. |

## SDR: Hardware FIR Decimation

Uses the AD9361's programmable FIR filter (via `libad9361-iio`) to decimate in hardware before DMA, reducing the sample rate the ARM has to process. When enabled, `sdr_rate` is the ADC rate and `sdr_hw_fir_rate` is the post-FIR rate entering GNU Radio. Adjust `sdr_decimation` accordingly (e.g. 600 kSPS output with 3x software decimation instead of 2.4 MSPS with 12x).

The software FIR (GNU Radio `fir_filter_ccf`, controlled by `sdr_decimation` and `sdr_lpf_cutoff`) always runs regardless of whether the hardware FIR is enabled. The two decimation stages are complementary: the hardware FIR does coarse decimation inside the AD9361 before DMA, the software FIR does fine decimation to reach the effective rate for FM demodulation. With hardware FIR, the software FIR operates at the lower post-FIR rate (e.g. 600 kSPS instead of 2.4 MSPS), so it generates fewer taps and uses less CPU.

The AD9361 minimum baseband rate without the FIR is 2.083 MSPS (25 MSPS / 12). With the FIR (4x additional decimation), the minimum drops to 520.83 kSPS (25 MSPS / 48). See [AD9361 Linux Device Driver](https://wiki.analog.com/resources/tools-software/linux-drivers/iio-transceiver/ad9361).

The FIR is disabled at the end of the run so subsequent experiments are not affected.

### Narrowing stage

The [Run 5 RF-link test](flight/debriefings/run-05-2026-07-03/) showed the FM discriminator being fed the full ~170 kHz effective baseband while the uplink occupies only a few kHz, pushing it below FM threshold (issue #107). With `sdr_narrow_enable=true`, after each capture completes, `capture.wav` is regenerated from the just-written `capture.sc16`: a peak search within ±`sdr_narrow_search` of DC finds the uplink (a 2 kHz guard excludes the AD9361 DC spike; the found offset is logged and doubles as a Doppler measurement), the peak is shifted to DC, and the signal is low-passed to ±`sdr_narrow_bw`/2 and decimated to ~25 kSPS before the unchanged demod chain. The streaming flowgraph and the sc16 tap are untouched, and any failure falls back to the wide audio. A missing key or `false` keeps the chain exactly as flown.

| Key | Default | Description |
|-----|---------|-------------|
| `sdr_narrow_enable` | false | Regenerate audio through the narrowing stage after each capture |
| `sdr_narrow_bw` | 20000 | Total narrowing width in Hz (±10 kHz, validated in #107) |
| `sdr_narrow_search` | 100000 | Peak-search half-width in Hz around DC |

### Hardware FIR

Must be disabled for emulator testing (the IIO emulator does not expose TX channels required by `libad9361-iio`).

| Key | Default | Description |
|-----|---------|-------------|
| `sdr_hw_fir_enable` | false | Enable AD9361 hardware FIR decimation |
| `sdr_hw_fir_rate` | 0 | Post-FIR baseband rate in Hz (e.g. 600000) |
| `sdr_hw_fir_fpass` | 0 | Passband edge frequency in Hz |
| `sdr_hw_fir_fstop` | 0 | Stopband edge frequency in Hz |
| `sdr_hw_fir_wnom_tx` | 0 | TX analog filter bandwidth in Hz |
| `sdr_hw_fir_wnom_rx` | 0 | RX analog filter bandwidth in Hz |

### Default values and signal chain analysis

The default hw FIR values in config.cfg target 600 kSPS with 3x software decimation, giving exactly 200 kHz effective rate (same as v3). All default values are derived from the `libad9361-iio` auto-default formulas for this rate, so we use what the library authors considered appropriate rather than guessing at filter parameters.

**Fpass and Fstop** define the digital FIR filter shape. These do the precise filtering. The auto-default formulas (from `ad9361_set_bb_rate_custom_filter_auto()` in [ad9361_design_taps.c](https://github.com/analogdevicesinc/libad9361-iio/blob/main/ad9361_design_taps.c)) are:

- `Fpass = rate / 3 = 200000`
- `Fstop = Fpass * 1.25 = 250000`

**wnom_tx and wnom_rx** control the AD9361's analog (hardware) filter bandwidth, written to `rf_bandwidth`. The auto-default formulas are:

- `wnom_tx = 1.6 * Fstop = 400000`
- `wnom_rx = 1.4 * Fstop = 350000`

The analog filter is a coarse first pass; the digital FIR provides the precise filtering regardless of the analog bandwidth. One detail worth noting: the AD9361 has minimum analog filter calibration thresholds (~400 kHz for RX, ~1.25 MHz for TX; see `ad9361_rx_bb_analog_filter_calib()` and `ad9361_tx_bb_analog_filter_calib()` in the [AD9361 Linux driver](https://github.com/analogdevicesinc/linux/blob/main/drivers/iio/adc/ad9361.c)). Requesting values below these thresholds is silently clamped by the driver. The auto-default wnom values are close to these minimums, which avoids readback mismatches while reflecting what the hardware actually achieves.

The full signal chain with these defaults:

```
AD9361 ADC (28.8 MSPS internal, HB chain -> 2.4 MSPS at HB1 output)
  -> Analog filter (wnom_rx=350 kHz requested, ~400 kHz actual after driver clamping)
  -> Hardware FIR (Fpass=200 kHz, Fstop=250 kHz, 4x decimation -> 600 kSPS)
  -> DMA to ARM at 600 kSPS
  -> Software FIR (sdr_lpf_cutoff=85 kHz, 3x decimation -> 200 kHz)
  -> FM demod at 200 kHz
  -> Resampler (200 kHz -> 16 kHz, interp=2, decim=25)
  -> Bandpass (300-3400 Hz)
  -> WAV output at 16 kHz
```

The FM voice signal occupies ~17 kHz of bandwidth at baseband (5 kHz deviation + 3.4 kHz audio, Carson's rule). At each stage:

| Stage | Passband | Signal BW | Margin |
|-------|----------|-----------|--------|
| Analog filter | ~400 kHz (actual) | ~17 kHz | ~383 kHz |
| Hardware FIR | 200 kHz (Fstop 250 kHz) | ~17 kHz | 183 kHz |
| Software FIR | 85 kHz (cutoff) | ~17 kHz | 68 kHz |
| Bandpass | 300-3400 Hz | 300-3400 Hz | matched |

The signal is deep inside the passband at every stage. From FM demod onward, the chain is identical to v3 (same effective rate, same resampler ratio, same audio output).

The analog filter (~400 kHz) is wider than the hardware FIR passband (200 kHz). This is expected at low sample rates where the AD9361 driver clamps to minimum calibration values. The hardware FIR provides the precise anti-aliasing. Its Fstop (250 kHz) is 50 kHz below the post-FIR Nyquist (300 kHz). Any residual energy in the 250-300 kHz gap is further rejected by the software FIR (cutoff 85 kHz).

Note: the internal ADC rate (28.8 MSPS), clock chain selection, and actual analog filter bandwidth (~400 kHz) are derived from analysis of the `libad9361-iio` source ([ad9361_calculate_rf_clock_chain.c](https://github.com/analogdevicesinc/libad9361-iio/blob/main/ad9361_calculate_rf_clock_chain.c)) and the AD9361 Linux driver ([ad9361.c](https://github.com/analogdevicesinc/linux/blob/main/drivers/iio/adc/ad9361.c)). These values were confirmed by the v4 EM readback logs (see [changelog/RESULT.md](changelog/RESULT.md)).

## Variants File (`variants.cfg`)

Maps target words to known misrecognition patterns (BPE token decomposition):

```
DOOM=DO,DU,DUE,DUNE,DUAL,TUBE,TOOM,DUEL,DOOM
PLAY=UPLI,PLY,PLEA,PLATE,PLANE,PLAY
PRETTY=PRITY,PRITI,PRETY,PREETY,PREATY,PRETTY
```

# OPS-SAT L-Band Voice Capture Design

## Goal

Capture and recognize a short spoken command (e.g. `"PRETTY, THIS IS <CALL SIGN>, PLAY DOOM"`) transmitted from a ground station on the L-band uplink, using the onboard AD9361 SDR. Maximize probability of success under the following constraints:

- L-band uplink (~1200 MHz)
- Orbit ~520 km LEO
- All processing onboard
- No ground-station coordinates available
- No Doppler correction tables
- ASR (speech-to-text) is the sole discriminator
- Downlinked I/Q files must be < 20 MB
- Simplicity over elegance

## Design Philosophy

- Always demodulate the captured signal, regardless of apparent quality.
- Always feed demodulated audio to ASR.
- Do not attempt to determine whether speech is present using RF-level heuristics.
- Let ASR reject non-speech input. Retry until a valid command is detected.
- Prioritize bandwidth and capture duration over signal fidelity.

## Key Engineering Decisions

### cf32 vs sc16 I/Q Format

cf32 (32-bit float I/Q) does not increase the probability of successful command detection. The AD9361 is a ~12-bit ADC; cf32 stores precision that does not exist in the source data. More critically, cf32 doubles the file size relative to sc16, which halves the capture duration or bandwidth that can be stored within the 20 MB downlink budget. This directly reduces the likelihood that the target signal falls within the captured window.

**Decision:** Use sc16 (int16 I + int16 Q).

### Noise Reduction

Noise reduction must not be applied onboard.

The noise environment is non-stationary (Doppler shift, multipath fading, terrestrial RF interference). Noise reduction algorithms destroy transients and consonants that ASR depends on. ASR models already incorporate internal noise robustness. Audio that sounds clean but has lost speech features performs worse than noisy audio with intact speech content.

Permitted signal conditioning operations:
- DC blocking
- Hard band-limiting to the voice band (~3 kHz)
- Single-pass RMS normalization
- Soft peak limiting (optional)

Operations to avoid:
- Spectral subtraction
- Adaptive noise cancellation
- Time-varying AGC
- ML-based denoising

### Doppler Handling

No Doppler correction is performed. No orbit propagation, ground station coordinates, or frequency tables are required. The capture bandwidth is set wide enough that the signal remains within the passband despite Doppler offset and LO error.

At ~1200 MHz, the expected Doppler magnitude is approximately +/-30 kHz.

## Capture Configuration

### I/Q Recording Parameters

| Parameter | Value |
|-----------|-------|
| Format | sc16 (int16 I/Q) |
| AD9361 hardware rate | 2.4 MSPS |
| Software decimation | 12x |
| Effective sample rate | 200 kSPS (= 2.4 MSPS / 12, the rate at which I/Q data is written to disk) |
| Duration | 20 seconds |
| File size | ~16 MB |
| Nyquist bandwidth | +/-100 kHz |

The +/-100 kHz bandwidth covers Doppler offset, LO error, and FM voice modulation with margin.

### Onboard DSP Chain

```
AD9361 RX (cf32, 2.4 MSPS hardware rate)
  |
  v
Decimating LPF (cutoff ~85 kHz, transition ~15 kHz, decimation 12x -> 200 kSPS)
  |
  +---> head -> complex_to_interleaved_short (scale 8192) -> .sc16 file
  +---> FM demod -> resample (200k -> 16k) -> bandpass (300-3400 Hz)
          -> head -> .wav file -> RMS normalize (-20 dBFS)
```

The AD9361 samples at 2.4 MSPS (within the hardware limit of 2,083,000 – 61,440,000 Hz). The decimating LPF reduces the rate to 200 kSPS in software. The I/Q file and the audio fed to ASR originate from the same LPF-filtered stream. The sc16 scale factor of 8192 maps nominal |1.0| cf32 magnitude to 8192 int16, leaving ~12 dB headroom before rail (32767).

## FM Demodulation

Standard quadrature demodulation is used. Tight PLLs are not required. Distortion is expected when the signal is off-frequency; this is acceptable. ASR requires only 1-2 seconds of intelligible speech within the capture window.

Audio conditioning after demodulation:
- Resample to 16 kHz mono
- Bandpass filter: 300-3400 Hz (at 16 kHz rate)
- Single-pass RMS normalization to -20 dBFS (post-capture)

## ASR Strategy

Fixed recording windows of 10-20 seconds are captured and each window is fed to ASR independently. If ASR detects the target command, execution proceeds. Otherwise the window is discarded and the next capture is processed.

This constitutes intentional Monte Carlo reception: each capture is an independent trial with a non-zero probability of success. Multiple attempts per pass yield high cumulative detection probability.

## Capture Timing

At ~520 km altitude, a typical ground station pass lasts 8-12 minutes with a 3-5 minute window of best SNR near closest approach. This allows dozens of independent 10-20 second capture attempts per pass.

## Constraints Summary

| Constraint | Rationale |
|-----------|-----------|
| Do not store cf32 I/Q | Wastes downlink budget on precision the ADC cannot provide |
| Do not apply noise reduction | Destroys speech features; ASR handles noise internally |
| Do not gate ASR on speech detection | RF-level heuristics are unreliable; let ASR reject non-speech |
| Do not require Doppler tables | Wide capture bandwidth absorbs frequency uncertainty |
| Do not optimize for audio quality | Detection probability depends on capture duration and bandwidth, not fidelity |

## Summary

Capture wide, capture often, and let ASR decide. The system trades signal processing complexity for capture volume, relying on the statistical certainty that repeated independent trials will produce a successful detection.

Recommended minimum configuration: 20 seconds of sc16 I/Q at 200 kSPS effective (AD9361 at 2.4 MSPS, 12x software decimation), channelized to +/-90 kHz, with always-on FM demodulation feeding ASR.

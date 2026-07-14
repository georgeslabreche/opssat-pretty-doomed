# Changelog: v6 to v7

**PRs**: [#113](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/113) (shared DSP chain), [#115](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/115) (narrowing stage), [#120](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/120) (second-stage filtering removal), [#121](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/121) (sc16 replay input), [#122](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/122) (narrowing phase logging). Ground validation behind these changes: [#110](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/110) and [#119](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/119).

v7 carries the flight-pipeline fixes that came out of the [Run 5 RF-link test](../flight/debriefings/run-05-2026-07-03/): the ground station transmitted FM voice, the spacecraft received it at +23 to +26 dB SNR, and the recordings prove the v6 pipeline would not have understood it.

## Narrowing Stage

**Observation**: The v6 capture chain feeds the FM discriminator the full 200 kHz channel. The uplink voice signal is roughly 20 kHz wide, so at the discriminator input more than 90% of the bandwidth is noise, and the discriminator output is dominated by it. Demodulating the Run 5 recordings through the v6 chain on the ground produced noise; narrowing to +/-10 kHz around the found carrier before the discriminator recovered clearly intelligible voice (roughly 9 to 11 dB less noise into the discriminator).

**Change**: A config-gated narrowing stage between the channel LPF and the discriminator ([#115](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/115)). After each capture, the sc16 is scanned with an averaged-PSD peak search (8192-point FFTs, Hann window, +/-`sdr_narrow_search` around DC, a 2 kHz DC guard so the AD9361 spike cannot win), the found peak is shifted to DC, band-limited to +/-`sdr_narrow_bw`/2, decimated to 25 kSPS, and the WAV is regenerated through the same shared demod blocks. New config keys, all defaulting to the as-flown v6 behavior when absent:

```
sdr_narrow_enable=true
sdr_narrow_bw=20000
sdr_narrow_search=100000
```

The flight `config.cfg` ships with narrowing armed (the values above). The streaming flowgraph is untouched; on any failure the wide audio is kept.

**Verification**: Validated on the ground against the actual Run 5 RF recordings, replayed through the SDR emulator into the unmodified flight binary ([#119](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/119)): the peak search lands within a few hundred Hz of the known uplink offset and the regenerated audio carries the voice transmission. A keyer ablation over synthetic commands at flight-like SNR moved detection from 6/8 to 8/8 in combination with the filtering fix below.

## Second-Stage Filtering Removal

**Observation**: The processing pipeline band-pass filtered the audio a second time before speech-to-text, but `capture.wav` is already band-passed by the capture flowgraph, so the stage was redundant for SDR captures. Worse, the STT model is trained on full-band 16 kHz speech: in a keyer ablation at CNR 12 dB, two of eight commands (norman, arctic) were missed with the second filtering and detected without it (8/8).

**Change**: The second-stage filtering is removed outright, not gated ([#120](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/120)). The pipeline now reads the audio, computes raw stats, resamples to 16 kHz if needed, and hands it to STT. The `dsp_*` config keys and `processed.wav` output are gone.

## Shared DSP Chain

**Change**: `capture.cpp` and the ground preview tool now build their audio path from a single module, `src/chain.{h,cpp}` ([#113](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/113)): config-derived rates and ratios, firdes filter designs, and the block chain (channel LPF, optional narrowing LPF, FM discriminator, rational resampler, voice band-pass). Refactor only, no behavior change: characterization tests pin the derived parameters to what v6 computed (97/193 taps, 2/25 resampler; with narrowing 97 taps, decimate by 8, 16/25), and same-config outputs were verified envelope-identical to v6 (GNU Radio scheduling makes bit-exactness unattainable by design).

## SC16 Replay Input

**Change**: New `-r <capture.sc16>` CLI flag runs the full capture-side processing from an sc16 file, with no SDR hardware and no IIO emulator ([#121](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/121)). The post-capture stages are the same code as a live capture: wide demod, config-gated narrowing, RMS normalization, I/Q artifacts, then STT and detection. The `run` script switches to `-r` automatically when an `input/replay.cs16` file exists, which is how the v7 EM validation feeds real flight signal through the ARM32 binary. The flight package must not contain `input/replay.cs16`.

**Verification**: Closed loop on the ground: a live emulator capture of a Run 5 recording with `sdr_keep_sc16=true`, then its `capture.sc16` fed back through `-r`, produced the identical narrowing peak (-8.12988 kHz) and word-identical transcript.

## Narrowing Phase Logging

**Change**: `narrow_rewrite_wav` logs the phase boundaries (scan start, found peak, audio regenerated) and `scripts/plots/plot_log_timeline.py` gained a matching Narrowing phase, so per-capture narrowing time is visible on the Gantt timeline instead of appearing as dead time ([#122](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/122)).

## Packaging

v7 is delivered as a patch package, `exp4023-pretty-DOOMed-v6-to-v7.tar.gz`, containing everything except the `models/` folder: the sherpa-onnx model (~27 MB) is unchanged since v1 and already deployed, so it is not re-uploaded. Extract the patch over the existing v6 installation; the deployed `models/` folder is picked up in place.

The EM validation build (this delivery). The repo `config.cfg` is always the flight configuration (`doom_force_trigger=false`); the EM package flips the flag in the packaged copy only:

```bash
docker compose -f docker-compose.sepp.yml run --rm pretty-doomed-sepp make BUILDDIR=build/sepp package-prepare
make package-demos
sed -i '' 's/doom_force_trigger=false/doom_force_trigger=true/' package/exp4023-pretty-DOOMed-v7/config.cfg
mkdir -p package/exp4023-pretty-DOOMed-v7/input
cp <replay.cs16 from the EM Validation Input section> package/exp4023-pretty-DOOMed-v7/input/replay.cs16
make package-patch PATCH_FROM=v6 PACKAGE_VERSION=v7 \
    PATCH_FILES="pretty-doomed opssat-doom run VERSION config.cfg variants.cfg ascii.txt libs demos assets input"
```

The flight build after EM sign-off is the same without the `sed`, without the `input/replay.cs16` copy, and without `input` in `PATCH_FILES`. In short, the EM tar differs from the flight tar in exactly two ways, neither of which is committed to the repo:

- The packaged `config.cfg` has `doom_force_trigger=true` so the replay run also exercises DOOM and the postcard. Flight ships `false` (the repo value).
- `input/replay.cs16` (the EM validation input below) is included so the `run` script replays it instead of capturing live. Flight ships no `input/`.

## EM Validation Input

The EM run replays real Run 5 signal instead of capturing RF: the pass 2 recording `sdr_20260703_205825` (amplified uplink, voice present) is converted on the ground to what a flight `capture.sc16` contains, complex baseband at the 200 kHz effective rate with the uplink placed at -8 kHz from DC (the offset measured at zenith), and deployed as `input/replay.cs16`:

```bash
cd tools/src
python3 make_emu_replay.py <path-to-raw>/sdr_20260703_205825_1295500000_2500000_1.cs16 \
    --output replay.cs16 --out-rate 200000
```

(The same tool builds SDR-emulator replays at 4.8 MSPS; at `--out-rate 200000` its output is `-r` input. The resampler band-limits to +/-100 kHz, standing in for the flight channel LPF.)

The 1.6 MB size against the ~19 MiB raw recording is expected and representative: the raw file is ESA's wideband dump at 2.5 MSPS, while `capture.sc16` in flight is recorded after the channel LPF at the 200 kHz effective rate, 12.5 times fewer samples. A flight capture at this rate is 0.8 MB per second (16 MB for a 20 s capture); the replay is 2 s because the Run 5 recording is 2 s, and `-r` takes the capture duration from the file.

Ground reference for the EM result, from the x86 build with the flight configuration: narrowing peak at -8.28 kHz (injected -8 kHz plus the recording's Doppler residual), transcript `LIMA ALPHAGIUM` with a call-sign token match, no command detected (the operators were not transmitting the command phrase; the EM run instead exercises DOOM via `doom_force_trigger=true`). `input/replay.cs16` must be deleted from the EM after validation and must never ship to the spacecraft.

## v7 Results

### Local (pre-EM)

The as-shipped EM package (patch extracted over a v6-style installation, `./run`, ARM32 binaries with bundled libraries under QEMU) ran the full sequence in 23.9 s: replay, narrowing peak at -8.27637 kHz (identical to the x86 reference), phonetic-alphabet transcription with a call-sign token match, force-triggered DOOM, postcard. Full write-up in [RESULT.md](RESULT.md#v7-local-validation-pre-em), data in [data/local-v7/](data/local-v7/).

### Engineering Model

EM validation pending. Results will be recorded here and in [RESULT.md](RESULT.md) with data in [data/em-v7/](data/em-v7/) per the existing per-version pattern.

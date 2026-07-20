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

**Verification**: Validated on the ground against the actual Run 5 RF recordings, replayed through the SDR emulator into the unmodified flight binary ([#119](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/119)): the peak search lands within a few hundred Hz of the known uplink offset and the regenerated audio carries the voice transmission. Validated again on the EM against the same real signal (see v7 Results below).

## Second-Stage Filtering Removal

**Observation**: The processing pipeline band-pass filtered the audio a second time before speech-to-text, but `capture.wav` is already band-passed by the capture flowgraph, so the stage was redundant for SDR captures. Worse, the STT model is trained on full-band 16 kHz speech: in a keyer ablation at CNR 12 dB, two of eight commands (norman, arctic) were missed with the second filtering and detected without it (8/8).

**Change**: The second-stage filtering is removed outright, not gated ([#120](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/120)). The pipeline now reads the audio, computes raw stats, resamples to 16 kHz if needed, and hands it to STT. The `dsp_*` config keys and `processed.wav` output are gone.

**Verification**: Re-run on the deployed v7 ARM32 package with the real detection deciding (force trigger off): all eight keyer voice files (four voices, clean and CNR 12 dB) trigger DOOM, reproducing the x86 result on the flight architecture.

## Shared DSP Chain

**Change**: `capture.cpp` and the ground preview tool now build their audio path from a single module, `src/chain.{h,cpp}` ([#113](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/113)): config-derived rates and ratios, firdes filter designs, and the block chain (channel LPF, optional narrowing LPF, FM discriminator, rational resampler, voice band-pass). Refactor only, no behavior change: characterization tests pin the derived parameters to what v6 computed (97/193 taps, 2/25 resampler; with narrowing 97 taps, decimate by 8, 16/25), and same-config outputs were verified envelope-identical to v6 (GNU Radio scheduling makes bit-exactness unattainable by design).

## SC16 Replay Input

**Change**: New `-r <capture.sc16>` CLI flag runs the full capture-side processing from an sc16 file, with no SDR hardware and no IIO emulator ([#121](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/121)). The post-capture stages are the same code as a live capture: wide demod, config-gated narrowing, RMS normalization, I/Q artifacts, then STT and detection. The `run` script switches to `-r` automatically when an `input/replay.cs16` file exists, which is how the v7 EM validation feeds real flight signal through the ARM32 binary. The flight package must not contain `input/replay.cs16`.

**Verification**: Closed loop on the ground: a live emulator capture of a Run 5 recording with `sdr_keep_sc16=true`, then its `capture.sc16` fed back through `-r`, produced the identical narrowing peak (-8.12988 kHz) and word-identical transcript.

## Narrowing Phase Logging

**Change**: `narrow_rewrite_wav` logs the phase boundaries (scan start, found peak, audio regenerated) and the timeline scripts (`scripts/plots/plot_log_timeline.py` in [#122](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/122), `plot_resource.py` in [#123](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/123)) gained a matching Narrowing phase, so per-capture narrowing time is visible on the Gantt timeline instead of appearing as dead time. [#123](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/123) also corrected several phase patterns (replay and postcard-stage lines, the DOOM launch announcement) and the bar rendering so overlapping minimum-width spans cannot blend into colors absent from the legend.

## Packaging

v7 is delivered as a patch package, `exp4023-pretty-DOOMed-v6-to-v7.tar.gz` (8.7 MB), containing only what changed since v6: `pretty-doomed`, `run`, `VERSION`, `config.cfg` (plus `input/replay.cs16` in the EM build below). Extract the patch over the existing v6 installation; everything else is picked up in place.

What was verified unchanged against the archived v6 package (per-file SHA-256): all of `demos/`, `assets/`, `variants.cfg`, `ascii.txt`, and 76 of the 79 bundled libraries. `models/` is unchanged since v1. Two rebuilt artifacts differ only as build noise and are deliberately not shipped: `opssat-doom` (+140 bytes, no `doom/` source change since v2) and `libiio.so.0.25` (a different build of the same version from the Docker image rebuild). The v7 binary was validated end to end on ARM32 against the v6 `libs/` exactly as deployed, including the v6 libiio.

The EM validation build (this delivery) is produced as follows. The repo `config.cfg` is always the flight configuration (`doom_force_trigger=false`); the EM package flips the flag in the packaged copy only:

```bash
docker compose -f docker-compose.sepp.yml run --rm pretty-doomed-sepp make BUILDDIR=build/sepp package-prepare
sed -i '' 's/doom_force_trigger=false/doom_force_trigger=true/' package/exp4023-pretty-DOOMed-v7/config.cfg
mkdir -p package/exp4023-pretty-DOOMed-v7/input
cp <replay.cs16 from the EM Validation Input section> package/exp4023-pretty-DOOMed-v7/input/replay.cs16
make package-patch PATCH_FROM=v6 PACKAGE_VERSION=v7 \
    PATCH_FILES="pretty-doomed run VERSION config.cfg input"
```

The flight build after EM sign-off is the same without the `sed`, without the `input/replay.cs16` copy, and without `input` in `PATCH_FILES`. In short, the EM tar differs from the flight tar in exactly two ways, neither of which is committed to the repo:

- The packaged `config.cfg` has `doom_force_trigger=true` so the replay run also exercises DOOM and the postcard. Flight ships `false` (the repo value).
- `input/replay.cs16` (the EM validation input below) is included so the `run` script replays it instead of capturing live. Flight ships no `input/`.

The flight `config.cfg` additionally carries a comment-only repair: the narrowing keys had been inserted into the middle of the `sdr_timeout_multiplier` comment, splitting it into a stray `sdr_hw_fir_enable=true (captures complete in real-time).` line (parsed as `false`, masked by the correct `sdr_hw_fir_enable=true` later in the file winning last) and duplicating the `sdr_narrow_*` keys. The repair restores the comment, de-duplicates, and gives the narrowing keys their own section. Every parsed value is unchanged, verified by replaying the EM input through the repaired config: 200 kHz effective rate (hardware FIR path), narrowing peak -8.27637 kHz, same transcript family.

Delivered artifacts (SHA-256):

```
d0ce756f47bc6e0476fc82f7fe28f7b170714a2c9b5a0bae0431738d704068d6  pretty-doomed (identical in both tars)
fc231f7476c0e3edefa6434cbe2a8cc36fd0cf2ee204ad5b9628f1ab5a75ea88  exp4023-pretty-DOOMed-v6-to-v7.tar.gz (EM build, validated 2026-07-17)
ab82a74a4790debc6b226ffcd858b2673f9430d8a3bf897ee8f1ef05417927ed  exp4023-pretty-DOOMed-v6-to-v7.tar.gz (flight build)
```

## EM Validation Input

The EM run replays real Run 5 signal instead of capturing RF: the pass 2 recording `sdr_20260703_205825` (amplified uplink, voice present) is converted on the ground to what a flight `capture.sc16` contains, complex baseband at the 200 kHz effective rate with the uplink placed at -8 kHz from DC (the offset measured at zenith), and deployed as `input/replay.cs16`:

```bash
cd tools/src
python3 make_emu_replay.py <path-to-raw>/sdr_20260703_205825_1295500000_2500000_1.cs16 \
    --output replay.cs16 --out-rate 200000
```

(The same tool builds SDR-emulator replays at 4.8 MSPS; at `--out-rate 200000` its output is `-r` input. The resampler band-limits to +/-100 kHz, standing in for the flight channel LPF.)

The exact file as flown to the EM is archived at [data/em-v7/replay.cs16](data/em-v7/replay.cs16) (1.6 MB, SHA-256 `455c15d0370ec4a1140b0e2ff6ab16a0839871c8038637ba9135f6fb49d630bd`), so the EM run can be reproduced anywhere with `-r` without regenerating the conversion.

The 1.6 MB size against the ~19 MiB raw recording is expected and representative: the raw file is ESA's wideband dump at 2.5 MSPS, while `capture.sc16` in flight is recorded after the channel LPF at the 200 kHz effective rate, 12.5 times fewer samples. A flight capture at this rate is 0.8 MB per second (16 MB for a 20 s capture); the replay is 2 s because the Run 5 recording is 2 s, and `-r` takes the capture duration from the file.

Ground reference for the EM result: narrowing peak at -8.27637 kHz (injected -8 kHz plus the recording's Doppler residual), identical on x86 and ARM32; a phonetic-alphabet transcript opening with a call-sign token match (`LIMA ALPHAGIUM` on x86, `LIMA OLFAR FOR VIGILIUM` on ARM32; the tail varies run to run because GNU Radio scheduling is not bit-deterministic, the leading token is stable); no command detected (the operators were not transmitting the command phrase; the EM run instead exercises DOOM via `doom_force_trigger=true`). `input/replay.cs16` must be deleted from the EM after validation and must never ship to the spacecraft.

## v7 Results

### Local (pre-EM)

The as-shipped EM package (patch extracted over a v6-style installation, `./run`, ARM32 binaries with bundled libraries under QEMU) ran the full sequence in 23.9 s: replay, narrowing peak at -8.27637 kHz (identical to the x86 reference), phonetic-alphabet transcription with a call-sign token match, force-triggered DOOM, postcard. Full write-up in [RESULT.md](RESULT.md#v7-local-validation-pre-em), data in [data/local-v7/](data/local-v7/).

### Engineering Model

Validated on the EM on 2026-07-17 (SMILE artifact [`pack-4023_1784280414`](data/em-v7/pack-4023_1784280414/)): the patch extracted over the intact v6 installation and the replay run reproduced the ground reference. Narrowing peak -8.27637 kHz (identical to the last logged digit), transcript word-identical to the QEMU dress rehearsal with the call-sign token match, force-triggered DOOM and postcard, 50.6 s total with the narrowing costing 0.5 s. Full write-up in [RESULT.md](RESULT.md#v7-validation), data in [data/em-v7/](data/em-v7/).

The flight build was then verified live (SMILE artifact [`pack-4023_1784317832`](data/em-v7/pack-4023_1784317832/)): SDR on, nothing transmitted, 6 x 20 s captures at the v6 cadence with full sample counts, narrowing about 1.7 s per capture, no false triggers, sc16 cleanup confirmed. Write-up in [RESULT.md](RESULT.md#v7-flight-package-verification). v7 is verified on the EM, replay and live SDR paths, and cleared for flight.

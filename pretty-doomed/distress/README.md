# Distress call evaluation on the EM

An EM package that transcribes 13 real distress calls with three speech-to-text models in five configurations (issue #142). It is not for flight. It has the layout of the DOOM package: the files are at the root of the experiment folder, and `./run` is the entry point. It is self-contained, with its own binary, libs, and models, so it also runs on a cleared EM. It replaces the v8 install: reinstall the v8 package after the evaluation.

## Contents

| Path | Description |
|---|---|
| `run` | Entry point, as `run` of the DOOM package. Runs each config in its own process and writes the results to `toGround/distress-NNNNN/`. |
| `run.cfg` | The configs and the clips that `run` runs. |
| `config.distress.<model>.cfg` | One pipeline config per model. All use `operation=detect`, so DOOM does not launch. |
| `variants.distress.cfg` | Empty variants file. |
| `VERSION` | Version of the package and of its binary. |
| `pretty-doomed` | The ARM32 binary, built from the same source as v8 plus the keys of #142, with sherpa-onnx 1.13.8 (v8 uses 1.12.21). Added by the package build. |
| `clips/` | The 13 clips as 16 kHz mono WAV. Added by the package build. |
| `libs/` | The shared libraries of the binary (GNU Radio, Boost, and others), the same as the v8 `libs/`. Added by the package build. |
| `models/` | `sherpa-onnx/small/`: the Zipformer small model, the v8 files at the v8 path. `parakeet-110m-int8/` and `parakeet-0.6b-v2-int8/`: the Parakeet models. Added by the package build. |
| `toGround/` | The output folder. |
| `README.md` | This file. |
| `MANIFEST.sha256` | SHA-256 of each file in the package. Added by the package build. |

## Models

| Config | Model | Decoding | Window | Files | Notes |
|---|---|---|---|---|---|
| `zipformer-small-int8` | Zipformer small | `modified_beam_search` | 10 s | 27 MB, the v8 files | The flight model. Baseline. |
| `parakeet-110m-int8` | Parakeet TDT 110M | `nemo_transducer`, `greedy_search` | 10 s | 130 MB | |
| `parakeet-0.6b-v2-int8-w5` | Parakeet TDT 0.6B v2 | `nemo_transducer`, `greedy_search` | 5 s | 631 MB | Fallback with less memory. |
| `parakeet-0.6b-v2-int8` | Parakeet TDT 0.6B v2 | `nemo_transducer`, `greedy_search` | 10 s | the same files | The most accurate model on the ground. |
| `parakeet-0.6b-v2-int8-t2` | Parakeet TDT 0.6B v2 | `nemo_transducer`, `greedy_search` | 10 s | the same files | Two STT threads. It runs last. |

The first four configs use one STT thread, so that the model comparison has the same conditions as the ground comparison. With one STT thread, the second SEPP core is almost idle: only the report card thread uses it. `parakeet-0.6b-v2-int8-t2` uses two STT threads, so both cores transcribe. It shows the speed of the SEPP with both cores. Its transcripts can be different from the one-thread config, because ONNX Runtime splits the computation differently. The three Parakeet 0.6B v2 configs use the same model files.

The peak memory of Parakeet 0.6B v2 on the ground (arm64, VmHWM) is 0.87 GB to 1.39 GB for one clip, and 1.3 GB (5 s) to 1.4 GB (10 s) for all 13 clips. On the SEPP emulator (ARM32) the peak is 869 MiB (10 s) and 821 MiB (5 s). In past runs, the free memory (MemAvailable) at the start of a run is 939 MiB to 970 MiB on the EM and 964 MiB to 968 MiB in flight (see `tools/distress/results/MEMORY.md`). Thus the margin of the 10 s config is only 70 MiB to 100 MiB, and Parakeet 0.6B v2 can run out of memory if other processes use memory. Each config runs in its own process, so a config that runs out of memory does not stop the other configs. The clips that are done before the failure stay in `timing.tsv`.

The binary uses sherpa-onnx 1.13.8, the version of the ground comparison. With the Parakeet models, sherpa-onnx 1.12.21 to 1.13.4 gives an empty text for some windows. 1.13.5 is the first version without this fault.

## Install

Extract the package in the experiment folder, as the DOOM package. The v8 install is not necessary:

```sh
cd <experiment folder>
tar -xzf exp4023-pretty-DOOMed-v<version>.tar.gz   # for example v8-distress-v1
sha256sum -c MANIFEST.sha256
```

## Run

```sh
./run
```

`run.cfg` sets what runs:

| Key | Value | Empty |
|---|---|---|
| `models` | Configs to run, in order, comma-separated: `config.distress.<name>.cfg`. | All five, in the order of the table above. |
| `clips` | Clips to run, by name, comma-separated: `clips/<name>.wav`. | All 13 clips. |

The package comes with all five configs and the two shortest clips, `clips=37977-bainbridge-skiff,56367-netarts-pwc`: 4.0 s of "Mayday, Mayday, Mayday" and 4.3 s of "Mayday, mayday, mayday. Jet skier down.", which every model detects on the ground. Run this first test before the full run. It shows in minutes that each config loads its model, transcribes more than one clip, detects, writes its report cards, and fits in memory. Most of its time is the five model loads. Expected result: exit code 0 for each config in `exit.txt`, `detected` = `yes` for both clips in each `timing.tsv`, and two `report_card.png` per config.

For the full run, leave `clips=` empty. The estimated run time on the EM is 4 to 8 hours for all five configs on the 13 clips. Most of it is the three Parakeet 0.6B v2 configs.

Optional environment variables, which override `run.cfg`:

| Variable | Default | Description |
|---|---|---|
| `DISTRESS_MODELS` | `models` of `run.cfg` | Configs to run, in order, comma-separated. |
| `DISTRESS_CLIP_NAMES` | `clips` of `run.cfg` | Clips to run, by name, comma-separated. |
| `DISTRESS_CLIPS` | `clips` | Directory of 16 kHz mono WAV clips. |
| `DISTRESS_TIMEOUT_S` | 0 (no limit) | Time limit per model in seconds. |

## Report cards

Each config has `report_card=true`. The binary draws one PNG per clip on board, the distress counterpart of the DOOM postcard:

- The verdict: DISTRESS DETECTED or NO DISTRESS PHRASE.
- The config, the decoding, the window length, the STT time, the RTF, and the peak memory.
- The audio spectrogram (0 Hz to 4 kHz) with the STT windows. A red window has an exact distress phrase. An orange window has an approximate match only.
- The exact matches (MATCHED) and the approximate matches with the words that were heard (NEAR).
- A transcript excerpt around the matches.

A separate thread draws the cards while the main thread transcribes the next clip, so the STT time of a clip does not include a card. After the last clip, the process frees its model and writes `stt.done`, and then draws its last cards. `run` starts the next config at `stt.done` when the memory of the previous process is below 200 MiB, so the next model load does not wait for the last cards, and two models are never in memory at the same time. The log lines of the cards (`Report card:`) have the thread ID of that thread. A card is approximately 100 kB to 250 kB, so 65 cards are approximately 13 MB.

## Output

```
toGround/distress-NNNNN/
├── manifest.txt     binary version, models, SHA-256 of the files, system memory and disk
├── resource.csv     CPU and memory every second for the full run (the format of the DOOM runs)
├── process.csv      memory (VmRSS, VmHWM) and threads of the pretty-doomed process every second
├── exit.txt         exit code and run time of each model
├── summary.txt      the configs side by side: wall time, model load, STT time, RTF, peak memory, detections, and STT time per clip
└── <model>/
    ├── pretty-doomed.log
    ├── stt.tsv      model type, decoding, window, threads, load time, peak memory (VmHWM)
    ├── timing.tsv   per clip: audio length, STT time, real-time factor, detection
    └── <clip>/      transcription.txt, scores.txt, summary.txt, report_card.png
```

Each clip is written to `timing.tsv` when it is done, so a run that stops keeps the clips that finished.

Exit codes in `exit.txt`:

| Code | Meaning |
|---|---|
| 0 | Done, at least one clip had a detection. |
| 2 | Done, no clip had a detection. |
| 1 | The model did not load, or the config is wrong. |
| 124 | Stopped by `DISTRESS_TIMEOUT_S`. |
| 137 | Killed, usually because the system ran out of memory. |

## Ground analysis

```sh
cd tools/distress
python3 import_em.py <path to distress-NNNNN>
python3 score.py
.venv/bin/python report.py <path to distress-NNNNN> --label em
```

`import_em.py` writes the EM results to `results/em-<model>/`, and `score.py` scores them next to the ground results. `report.py` writes `reports/em/REPORT.md` with the figures: CPU, free memory, and process memory during the run (from `resource.csv`, the same data as for the DOOM runs, and `process.csv`), peak memory per config, the phases of each config by thread, the report cards of one clip, speed per clip, and accuracy against the ground binary.

## Build

See [docs/BUILDING.md](../docs/BUILDING.md), section "Distress call evaluation package".

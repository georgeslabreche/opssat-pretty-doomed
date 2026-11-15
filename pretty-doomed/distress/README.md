# Distress call evaluation on the EM

An add-on for the EM that transcribes 13 real distress calls with three speech-to-text models in four configurations (issue #142). It is not for flight. It installs next to the v8 install in a `distress/` folder and does not change any v8 file.

## Contents

| Path | Description |
|---|---|
| `run-distress` | Entry script. Runs each model in its own process and writes the results to `toGround/distress-NNNNN/`. |
| `config.distress.<model>.cfg` | One pipeline config per model. All use `operation=detect`, so DOOM does not launch. |
| `variants.distress.cfg` | Empty variants file. |
| `VERSION` | Version of the add-on and of its binary. |
| `pretty-doomed` | The ARM32 binary, built from the same source as v8 plus the keys of #142, with sherpa-onnx 1.13.8 (v8 uses 1.12.21). Added by the package build. |
| `clips/` | The 13 clips as 16 kHz mono WAV. Added by the package build. |
| `models/` | The Parakeet 110M and Parakeet 0.6B v2 models. Added by the package build. The Zipformer small model is the v8 model in `../models/`. |
| `README.md` | This file. |
| `MANIFEST.sha256` | SHA-256 of each file in the package. Added by the package build. |

## Models

| Config | Model | Decoding | Window | Files | Notes |
|---|---|---|---|---|---|
| `zipformer-small-int8` | Zipformer small | `modified_beam_search` | 10 s | v8 files, 27 MB | The flight model. Baseline. |
| `parakeet-110m-int8` | Parakeet TDT 110M | `nemo_transducer`, `greedy_search` | 10 s | 130 MB | |
| `parakeet-0.6b-v2-int8-w5` | Parakeet TDT 0.6B v2 | `nemo_transducer`, `greedy_search` | 5 s | 631 MB | Fallback with less memory. |
| `parakeet-0.6b-v2-int8` | Parakeet TDT 0.6B v2 | `nemo_transducer`, `greedy_search` | 10 s | the same files | The most accurate model on the ground. It runs last. |

All configs use one thread. The two Parakeet 0.6B v2 configs use the same model files.

The peak memory of Parakeet 0.6B v2 on the ground (arm64, VmHWM) is 0.87 GB to 1.39 GB for one clip, and 1.3 GB (5 s) to 1.4 GB (10 s) for all 13 clips. The EM has approximately 990 MB of free memory. Thus Parakeet 0.6B v2 can run out of memory on the EM. Each config runs in its own process, so a config that runs out of memory does not stop the other configs. The clips that are done before the failure stay in `timing.tsv`.

The binary uses sherpa-onnx 1.13.8, the version of the ground comparison. With the Parakeet models, sherpa-onnx 1.12.21 to 1.13.4 gives an empty text for some windows. 1.13.5 is the first version without this fault.

## Install

Extract the package in the experiment folder, next to the v8 files:

```sh
cd <experiment folder>
tar -xzf exp4023-pretty-DOOMed-v<version>.tar.gz   # for example v8-distress-1
sha256sum -c distress/MANIFEST.sha256
```

## Run

```sh
./distress/run-distress
```

Optional environment variables:

| Variable | Default | Description |
|---|---|---|
| `DISTRESS_MODELS` | all four, in the order of the table above | Configs to run, in order. |
| `DISTRESS_CLIPS` | `distress/clips` | Directory of 16 kHz mono WAV clips. |
| `DISTRESS_TIMEOUT_S` | 0 (no limit) | Time limit per model in seconds. |

The estimated run time on the EM is 2 to 3.5 hours for all three models. Most of it is Parakeet 0.6B v2.

## Output

```
toGround/distress-NNNNN/
├── manifest.txt     binary version, models, SHA-256 of the files, system memory and disk
├── resource.csv     CPU and memory every second for the full run
├── exit.txt         exit code and run time of each model
└── <model>/
    ├── pretty-doomed.log
    ├── stt.tsv      model type, decoding, window, threads, load time, peak memory (VmHWM)
    ├── timing.tsv   per clip: audio length, STT time, real-time factor, detection
    └── <clip>/      transcription.txt, scores.txt, summary.txt
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
```

`import_em.py` writes the EM results to `results/em-<model>/`, and `score.py` scores them next to the ground results.

## Build

See [docs/BUILDING.md](../docs/BUILDING.md), section "Distress call evaluation package".

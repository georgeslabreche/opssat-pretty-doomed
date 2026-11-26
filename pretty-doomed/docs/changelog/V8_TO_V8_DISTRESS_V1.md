# Changelog: v8 to v8-distress-v1

**PRs**: [#153](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/153) (EM package for the distress call evaluation). Ticket: [#142](https://github.com/georgeslabreche/opssat-pretty-doomed/issues/142).

v8-distress-v1 is an EM-only package that shows a practical use of on-board voice recognition: the detection of maritime distress calls in recorded radio audio. It runs the v8 binary in directory mode on real US Coast Guard distress calls, with five speech-to-text models, and it does not use the SDR. The flight `config.cfg` and `variants.cfg` do not change, and there is no FM run.

## Distress Package

**Observation**: The v8 package has one STT model (Zipformer small) and the DOOM flow. The distress evaluation needs larger models, a clip set, and one process per model, so that the memory and the time of each model are measured separately.

**Change**: A self-contained package, `exp4023-pretty-DOOMed-v8-distress-v1.tar.gz`, with the layout of the DOOM package: `./run` at the root, `libs/`, the binary, the models, the clips, and `toGround/`. It includes the v8 Zipformer model at its v8 path, so the package runs on an EM that was cleared. `run.cfg` selects the models and the clips, as comma-separated lists. An empty list selects all. The shipped `run.cfg` selects all five models and the two shortest clips:

| Model | Config | Notes |
|---|---|---|
| Zipformer small int8 | `config.distress.zipformer-small-int8.cfg` | The v8 flight model. |
| Parakeet 110M int8 | `config.distress.parakeet-110m-int8.cfg` | |
| Parakeet 0.6B v2 int8 | `config.distress.parakeet-0.6b-v2-int8.cfg` | 10 s windows. |
| Parakeet 0.6B v2 int8, 5 s windows | `config.distress.parakeet-0.6b-v2-int8-w5.cfg` | Less memory. |
| Parakeet 0.6B v2 int8, two threads | `config.distress.parakeet-0.6b-v2-int8-t2.cfg` | `stt_num_threads=2`. |

The binary uses sherpa-onnx 1.13.8. With the Parakeet models, sherpa-onnx 1.12.21 to 1.13.4 gives an empty text for some windows.

## Model Release and Overlap Guard

**Observation**: Parakeet 0.6B v2 needs approximately 730 MiB on ARM32. The EM has approximately 965 MiB free. Two models in memory at the same time do not fit.

**Change**: Each model runs in its own process. After the last clip, the binary releases the model, logs its VmRSS, and writes `stt.done`. The run script starts the next model only after `stt.done` and when the VmRSS of the previous process is less than 200 MiB. The report cards of the previous model can still be in progress.

## Report Cards

**Observation**: The DOOM runs make their plots on board. The distress runs must also show on-board results, not only text files.

**Change**: `report_card=true` makes the binary draw one PNG per clip: the detection result, the model and its timing, a spectrogram with the STT windows (red for a distress phrase, orange for a near match), the matched phrases, and the transcript. A second thread draws the cards, so the transcription of the next clip does not wait. The footer is `OPS-SAT PRETTY ON-BOARD DISTRESS DETECTION`.

## Run Logging and Summary

**Change**: The run writes the data of the DOOM runs and more:

| File | Content |
|---|---|
| `manifest.txt` | Version, clips, system, memory, disk, and the SHA-256 of the binary, the configs, the models, and the clips. |
| `resource.csv` | Per-core CPU and system memory every second, as for the DOOM runs. |
| `process.csv` | VmRSS, VmHWM, and threads of each model process every second. |
| `exit.txt` | Exit code and run time of each model. |
| `summary.txt` | Load time, STT time, RTF, peak memory, and detections per model, and the STT time per clip and model. |
| `<model>/pretty-doomed.log` | The thread-tagged log of the binary, for the thread timeline. |
| `<model>/stt.tsv`, `<model>/timing.tsv` | Model load time, peak memory, and the time and detection of each clip. |

`operation=detect` logs `Command detected (operation=detect)` and does not start DOOM.

## Verification

**Verification**: Unit tests cover the report card data (`test_report_card.cpp`), the `report_card` key, the detect summary, and the process memory parse (`make test`, green). With the flight `config.cfg`, the transcriptions, the scores, and the summaries of 18 samples are byte-identical to v8, so the DOOM flow does not change. On the SEPP emulator (ARM32 under QEMU, user `exp`), Zipformer small, Parakeet 110M, and Parakeet 0.6B v2 (10 s and 5 s windows) complete all 13 clips, the Parakeet 0.6B v2 configs also with a 1 GB memory limit. On the EM, all five models ran on the two shortest clips and detected Mayday in both. Parakeet 0.6B v2 peaked at 729 MiB with 967 MiB free, and the transcripts are byte-identical to the emulator run. Details in [RESULT.md](RESULT.md).

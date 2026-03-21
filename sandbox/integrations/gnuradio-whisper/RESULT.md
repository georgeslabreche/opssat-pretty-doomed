# GNU Radio + Whisper Integration Results

## Status

Superseded by the [`pretty-doomed`](../../../pretty-doomed/) integrated pipeline.

## Purpose

Early prototype combining GNU Radio signal processing with Whisper.cpp for speech-to-text. Tested the concept of a single binary pipeline that filters and transcribes audio.

## Outcome

The prototype demonstrated that GNU Radio DSP and STT can run in a single C++ process. However, the STT engine was changed from Whisper to Sherpa-ONNX for the final pipeline based on the evaluation in [`sandbox/speech-to-text/`](../../speech-to-text/):

- Sherpa-ONNX small (int8): ~27 MB model, better accuracy for short command phrases
- Whisper tiny: ~75 MB model, higher WER on short utterances

The architectural pattern (single binary, in-memory filtering, configurable pipeline) was carried forward into pretty-doomed.

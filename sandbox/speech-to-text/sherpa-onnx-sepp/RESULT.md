# Sherpa-ONNX SEPP Results

## Status

Validated on EM. STT engine integrated into the [`pretty-doomed`](../../../pretty-doomed/) pipeline.

## EM Testing Summary

- **v1** (Feb 2026): Failed on EM. Missing `libsndfile.so.1` dependency.
- **v2** (Feb 2026): Fixed missing library. Successfully executed on EM.

## EM Performance (v2)

From EM run artifacts (`artifacts/toGround/run-000001/`):

- **Model**: zipformer-small-en, int8 quantized (~27 MB)
- **Decoding**: modified_beam_search
- **Input**: 22.86s WAV (georges_opssat_clean.wav)
- **Model load time**: ~26s (08:34:28 to 08:34:54)
- **Inference time**: ~288s (08:34:54 to 08:39:42)
- **Total processing time**: ~314s (~14x realtime on ARM32)

### Transcription output

```
UPSET PLAY DOOM UPSET PLAY DOM OPPOSET PLAY DOOM PRETTY PLAY DOOM
PRETTY PLAY DOOM PRETTY PLAY DOOM UPSET PRETTY PLAY DOOM
```

The input audio contains repeated "PRETTY PLAY DOOM" commands. The model correctly transcribes most instances, with some misrecognitions ("UPSET" for "PRETTY", "DOM" for "DOOM") that are handled by the fuzzy matching in pretty-doomed's `matcher.cpp` via externalized variants.

## Outcome

Sherpa-ONNX is viable on the SEPP. The int8 model fits in memory (~27 MB), produces usable transcriptions, and the misrecognition patterns are predictable and addressable via fuzzy matching. This informed the decision to use Sherpa-ONNX over Whisper (75 MB, slower) and Vosk (50 MB, lower accuracy) in the integrated experiment.

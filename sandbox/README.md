# Sandbox

Standalone experiments that informed the design of the integrated [`pretty-doomed`](../pretty-doomed/) pipeline.

| Experiment | Status | Description |
|-----------|--------|-------------|
| [`gnuradio/signal-processor/`](gnuradio/signal-processor/) | Validated (EM, spacecraft) | GNU Radio DSP filters on ARM32 |
| [`gnuradio/sdr-capture/`](gnuradio/sdr-capture/) | Validated (EM, v8) | AD9361 RX capture, FM demod, I/Q diagnostics |
| [`gnuradio/sdr-loopback/`](gnuradio/sdr-loopback/) | Shelved (v8) | TX/RX loopback. Q channel dropout. See [RESULT.md](gnuradio/sdr-loopback/RESULT.md) |
| [`speech-to-text/sherpa-onnx-sepp/`](speech-to-text/sherpa-onnx-sepp/) | Validated (EM, v2) | Sherpa-ONNX on ARM32. 26s model load, 288s inference for 22s audio |
| [`speech-to-text/`](speech-to-text/) | Complete | STT engine evaluation (Sherpa-ONNX, Vosk, PocketSphinx) |
| [`denoising/`](denoising/) | Dropped | DTLN, spectral subtraction, Wiener, ALE, noisereduce. None were effective on the OPS-SAT radio samples, and a detection-level A/B through the flight pipeline showed the tested denoiser lowering command detection. See [audio-denoiser-noisereduce/](denoising/audio-denoiser-noisereduce/) |
| [`integrations/gnuradio-whisper/`](integrations/gnuradio-whisper/) | Superseded | Early prototype. Replaced by pretty-doomed (Sherpa-ONNX over Whisper) |

### Shared infrastructure

| Directory | Description |
|-----------|-------------|
| [`gnuradio/build-libs-armv7/`](gnuradio/build-libs-armv7/) | Pre-built GNU Radio + gr-iio + libad9361 ARM32 libraries |
| [`gnuradio/README.md`](gnuradio/README.md) | AD9361 SDR reference (libiio, gr-iio, IIO attributes) |
| [`gnuradio/CAPTURE.md`](gnuradio/CAPTURE.md) | SDR capture guidelines (sc16 format, sample rates, decimation) |

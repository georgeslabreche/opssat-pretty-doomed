# Denoising Experiments

These experiments evaluate whether denoising the received audio improves speech-to-text on the noisy radio link. None of the methods tested did.

| Experiment | Method | Verdict |
|---------|--------|---------|
| [`audio-denoiser-dtln/`](audio-denoiser-dtln/) | DTLN deep-learning denoiser (TensorFlow Lite C API) | Works on acoustic noise; not on the OPS-SAT radio samples |
| [`audio-denoiser-classical/`](audio-denoiser-classical/) | Spectral subtraction, adaptive filtering, Wiener filtering | Work on stationary acoustic noise; fail on the OPS-SAT radio samples |
| [`audio-denoiser-ale/`](audio-denoiser-ale/) | Adaptive Line Enhancement | Not effective on the OPS-SAT radio samples |
| [`audio-denoiser-noisereduce/`](audio-denoiser-noisereduce/) | Noisereduce spectral gating, measured on flight-pipeline command detection | Hurts detection: 15/40 without vs 5/40 with, at boundary link qualities |

The first three experiments evaluated audio quality on radio recordings. The last experiment measured command detection through the full flight pipeline on identical simulated radio trips, and on the real Run 5 captures, where denoising erased the call-sign token from the one strong voice capture. Together with the v7 removal of the second-stage band-pass filtering (detection 6/8 to 8/8 without it), no audio preprocessing tried so far has improved recognition over feeding the model raw audio.

The flight pipeline performs no audio denoising. Noise is addressed before the FM discriminator instead, by the narrowing stage.

# Validation Samples

This directory contains validation datasets for testing audio denoising algorithms.

## NOIZEUS Corpus

The NOIZEUS corpus is a standard benchmark for speech enhancement algorithms.

**Reference**: https://ecs.utdallas.edu/loizou/speech/noizeus/

### Download Samples

```bash
cd samples/validation
./download_noizeus.sh
```

This will download:
- **30 clean speech files** (sp01.wav to sp30.wav)
- **480 noisy speech files** (4 noise types: babble, car, exhibition, train × 4 SNR levels × 30 sentences)

### Corpus Structure

After download, you'll have:

```
noizeus/
├── clean/
│   ├── sp01.wav
│   ├── sp02.wav
│   └── ... (30 files)
└── noisy/
    ├── babble_0db/     - Multi-talker babble at 0dB SNR
    ├── babble_5db/     - Multi-talker babble at 5dB SNR
    ├── babble_10db/    - Multi-talker babble at 10dB SNR
    ├── babble_15db/    - Multi-talker babble at 15dB SNR
    ├── car_0db/        - Car interior noise at 0dB SNR
    ├── exhibition_0db/ - Exhibition hall noise at 0dB SNR
    ├── train_0db/      - Train noise at 0dB SNR
    └── ... (4 noise types × 4 SNR levels = 16 directories)
```

### Testing with Spectral Subtraction

From inside the Docker container:

```bash
# Example: Process babble noise at 5dB SNR
./build/spectral_subtraction \
  samples/validation/noizeus/noisy/babble_5db/sp01.wav \
  output/validation/noizeus/denoised/babble_5db/sp01.wav

# Or batch process all samples with spectral subtraction
./batch_validate_spectral_subtraction.sh

# Compare results
# Clean:    samples/validation/noizeus/clean/sp01.wav
# Noisy:    samples/validation/noizeus/noisy/babble_5db/sp01.wav
# Denoised: output/validation/noizeus/denoised/babble_5db/sp01.wav
```

### Evaluation

You can evaluate denoising quality by comparing:
1. **Noisy input** vs **Clean reference** (baseline)
2. **Denoised output** vs **Clean reference** (improvement)

Common metrics:
- **SNR improvement**: Measure signal-to-noise ratio gain
- **PESQ**: Perceptual Evaluation of Speech Quality
- **STOI**: Short-Time Objective Intelligibility
- **Subjective listening**: Does it sound better?

## Notes

- Downloaded files are .gitignored (won't be committed)
- Total download size: ~100-150 MB
- Files are 16kHz sampling rate, mono

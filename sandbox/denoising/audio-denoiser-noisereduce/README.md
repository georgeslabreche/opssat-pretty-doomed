# Noisereduce Spectral Gating: Detection-Level A/B

The earlier experiments in this series ([DTLN](../audio-denoiser-dtln/), [classical methods](../audio-denoiser-classical/), [ALE](../audio-denoiser-ale/)) evaluated denoisers on audio quality against real received radio recordings and dropped them: none were effective on the OPS-SAT radio samples. This experiment instead measures whether denoising before speech-to-text improves command detection through the full flight pipeline, at controlled link qualities on flight-form signal.

It does not: this denoiser lowers the detection rate from 15/40 to 5/40 at the boundary link qualities. Denoising is dropped again, now with detection-level evidence.

## Method

A/B on identical radio trips, where the only difference is the denoiser:

- **Arm A (flight pipeline as-is)**: a clean keyer voice WAV is converted to flight-form baseband at a chosen CNR with [`tools/src/make_fm_iq.py`](../../../tools/README.md) and replayed through the flight binary (`pretty-doomed -r`): narrowing, FM discriminator, STT, matcher.
- **Arm B (denoised)**: arm A's own narrowed `capture.wav` is denoised with [noisereduce](https://github.com/timsainb/noisereduce) stationary spectral gating (`denoise.py`), then re-enters the identical pipeline at STT via `pretty-doomed -i`.

Corpus: the [four shortlisted keyer voices](../../../pretty-doomed/input/keyer/) at the detection boundary (CNR 6 and 9 dB in the 20 kHz narrowing bandwidth), 5 noise seeds per point, 40 radio trips. Detection decided by the real matcher (`doom_force_trigger=false`).

## Results: synthetic radio trips

| Voice | CNR | A: flight pipeline | B: denoised |
|---|---|---|---|
| arctic | 6 | 1/5 | 0/5 |
| arctic | 9 | 5/5 | 1/5 |
| norman | 6 | 2/5 | 0/5 |
| norman | 9 | 4/5 | 0/5 |
| lessac | 6 | 0/5 | 0/5 |
| lessac | 9 | 2/5 | 1/5 |
| l2arctic | 6 | 0/5 | 1/5 |
| l2arctic | 9 | 1/5 | 2/5 |
| **total** | | **15/40** | **5/40** |

Twelve A/B flips favored the raw pipeline; the two favoring the denoiser were single-seed lottery cases on the weakest voice.

## Results: real Run 5 flight captures

The six RF-test recordings converted to flight-form baseband and run through both arms (no command was transmitted; the comparison is transcript fidelity):

| Recording | A: flight pipeline | B: denoised |
|---|---|---|
| 192433 (pre-signal noise) | `THREE` | `I THOUGHT` |
| 192511 | `THE` | `THE FIRST RESPONDED` |
| 192531 | `WHAT HEAVY` | `OF GOOD FAVOUR TO` |
| 205727 | `MISTRESS` | `THESE THINGS` |
| 205805 | `A` | `THREE` |
| **205825 (strong voice)** | **`LIMA OLFAR FOR VIGILIUM`** | **`MEMORY IDEAS`** |

On the one capture carrying a solid voice segment, the raw pipeline recovers the call-sign token; the denoiser erases it.

## Why it fails

The denoised audio sounds cleaner to a human listener, but the recognizer performs worse on it. Spectral gating trades phonetic fidelity for smoothness: it removes consonant energy the recognizer depends on, and over the smoothed audio the model decodes fluent, confident, wrong words. A transcript pair from the boundary:

- norman at CNR 9, arm A: `LEMA ALVA FOUR OF PRETTY PRETTY PLEASED TO PLAY DOOMED DO` (detected)
- the same audio, arm B: `AS AN HOUR FORTY PRINKLY TO PLATO'S DISTANCE` (missed)

Fed the raw noisy audio, the model emits fragmentary but faithful tokens, which is what the fuzzy matcher works with. The v7 second-stage filtering removal showed the same effect (detection went from 6/8 to 8/8 when the extra audio filtering was deleted): no audio preprocessing tried so far has improved recognition over feeding the model raw audio.

## Reproduce

```bash
pip install -r requirements.txt

# Arm A: generate a boundary-CNR input and run the flight pipeline
cd tools/src
python3 make_fm_iq.py ../../pretty-doomed/input/keyer/reference_doom__en_US-norman-medium.wav \
    --output ../../pretty-doomed/input/ab.sc16 --cnr-db 9 --seed 1
cd ../../pretty-doomed
docker compose run --rm pretty-doomed ./build/local/pretty-doomed \
    -r input/ab.sc16 -c config.cfg -f variants.cfg -o toGround/abA -d demos -e /bin/true

# Arm B: denoise arm A's capture.wav, re-enter at STT
python3 ../sandbox/denoising/audio-denoiser-noisereduce/denoise.py toGround/abA toGround/abB-wav
docker compose run --rm pretty-doomed ./build/local/pretty-doomed \
    -i toGround/abB-wav/abA.wav -c config.cfg -f variants.cfg -o toGround/abB -d demos -e /bin/true

# Compare
grep Result: toGround/abA/capture-001/summary.txt toGround/abB/summary.txt
```

See [`pretty-doomed/docs/KEYER_SCREENING.md`](../../../pretty-doomed/docs/KEYER_SCREENING.md) for the screening harness this A/B is built on, including the CNR calibration against the Run 5 captures.

# Sherpa-ONNX Speech-to-Text Evaluation

Evaluate Sherpa-ONNX STT accuracy on OPS-SAT audio samples.

## Quick Start

```bash
# Build and test all models
docker-compose build
docker-compose run --rm sherpa make test-all-models

# Compare decoding methods (greedy vs beam search)
docker-compose run --rm sherpa make compare-all
```

## Models

| Model | Size | Command |
|-------|------|---------|
| zipformer-small-en | ~27 MB | `make test-small` |
| zipformer-en (base) | ~68 MB | `make test-base` |
| zipformer-large-en | ~147 MB | `make test-large` |

All models use int8 quantization.

## Decoding Methods

| Method | Description | Command |
|--------|-------------|---------|
| `greedy_search` | Fast, takes best token at each step | Default |
| `modified_beam_search` | Explores multiple paths, better accuracy | `-d modified_beam_search` |

### Comparison: greedy_search vs modified_beam_search vs hotwords (SMALL model)

**Ground Truth** (22 words - [georges_opssat_transcript.txt](/samples/georges/georges_opssat_transcript.txt)):
```
OPSSAT PLAY DOOM OPSSAT PLAY DOOM OPSSAT PLAY DOOM PRETTY PLAY DOOM PRETTY PLAY DOOM PRETTY PLAY DOOM OPSSAT PRETTY PLAY DOOM
```

#### Word Error Rate (Clean Audio)

| Configuration | WER | Absolute | Relative |
|---------------|-----|----------|----------|
| greedy_search | 27.27% | baseline | baseline |
| modified_beam_search | **22.73%** | **-4.5 pp** | **-17%** |
| beam + hotwords | 31.82% | +4.5 pp | +17% |

*Absolute = percentage point change. Relative = % change in errors.*

#### Transcription Output (Clean Audio)

**greedy_search** (27.27% WER):
```
OPPOSSET PLAY DOOM UPSET PLAIDUM OPPOSET PLAY DOOM PRETTY PLAY DOOM PRETTY PLAY DOOM PRETTY PLAY DOOM UPSET PRETTY PLAY DOOM
```

**modified_beam_search** (22.73% WER):
```
OPPOSSET PLAY DOOM UPSET PLAY DOM OPPOSET PLAY DOOM PRETTY PLAY DOOM PRETTY PLAY DOOM PRETTY PLAY DOOM UPSET PRETTY PLAY DOOM
```

**beam + hotwords** (31.82% WER):
```
OOPSUM UPSET PLAY DOM OPPOD PLAY DOOM PRETTY PLAY DOOM PRETTY PLAY DOOM PRETTY PLAY DOOM UPSET PRETTY PLAY DOOM
```

#### WER Across Noise Levels

| Audio | greedy_search | modified_beam_search | beam + hotwords |
|-------|---------------|----------------------|-----------------|
| Clean | 27.27% | **22.73%** | 31.82% |
| Noisy | 54.55% | **50.00%** | 59.09% |
| Very Noisy | 95.45% | **90.91%** | 90.91% |

**Recommendation:** Use `modified_beam_search` without hotwords for best accuracy.

## Hotwords (Not Recommended for BPE Models)

Testing demonstrated that hotwords **degrade** accuracy with BPE models.

### Why Hotwords Fail with BPE Models

Two separate issues prevent effective hotword boosting:

**Issue 1: Token Format Constraints**

The hotwords file must reference tokens that exist in `tokens.txt`. This model uses BPE (Byte Pair Encoding), meaning whole words like "PRETTY" or "DOOM" do not exist as single tokens—only subword units exist.

**Contents of tokens.txt:**
- Single letters: `P`, `R`, `E`, `T`, `Y`, `D`, `O`, `M`, etc.
- Subword units with word boundary marker: `▁PRE`, `▁PLA`, `▁UP`, `▁DO`, etc.

Sherpa-onnx v1.10.30 treats the `▁` character as a delimiter, causing `▁PRE` to become two tokens (`▁` + `PRE`). Since `PRE` does not exist in the vocabulary, single-letter tokens must be used:

```
P R E T T Y :4.0
P L A Y :3.0
D O O M :3.0

P R E T T Y P L A Y D O O M :5.0

O P S S A T :3.5
U P S E T :3.2

O P S S A T P L A Y D O O M :5.0
U P S E T P L A Y D O O M :4.7
```

**Issue 2: BPE Models Learn Contextual Word Boundaries**

Even if a newer sherpa-onnx version properly parsed the `▁` character, hotword boosting would still likely degrade accuracy. BPE-based transducer models learn contextual word boundaries during training. Externally boosting individual tokens—whether single letters or BPE subwords—disrupts this learned behavior.

In testing, "OPPOSSET" degraded to "OOPSUM" and "OPPOD" when hotwords were enabled. The token-level bias interferes with the model's natural word formation patterns.

**Why Upgrading Sherpa-ONNX Would Not Help**

The `▁` parsing limitation in v1.10.30 is a minor inconvenience, not the root cause of accuracy degradation. The fundamental incompatibility is between:
- Hotword boosting (which rewards specific token sequences)
- BPE models (which rely on learned subword composition)

Boosting tokens like `▁PRE TY` would still disrupt how the model weighs alternative decompositions, potentially causing worse errors than the single-letter approach.

**Recommendation:** Apply intent matching post-transcription (e.g., treat "OPPOSSET", "UPSET", "OP SAT" as equivalent to "OPSSAT").

## Voice Command Design for ASR Robustness

Voice commands should be optimized for ASR robustness given BPE vocabulary constraints.

### Vocabulary Constraints

| Factor | Impact |
|--------|--------|
| No hyphen token | "OPS-SAT" is fragile |
| Acronyms | Letter-by-letter spelling is unreliable |
| Proper nouns | High misrecognition risk |
| Common English words | Strong token support (e.g., `▁PLAY`, `▁PRE`, `TY`) |

### Command Design Principles

- Use **frequent English words** (dedicated tokens exist)
- Ensure **clear word boundaries**
- Avoid **hyphens and acronyms**
- Structure commands as: **wake word + verb + object**

### Recommended Command

**"PRETTY, PLAY DOOM."**

| Word | Rationale |
|------|-----------|
| PRETTY | Maps cleanly to `▁PRE` + `TY`—significantly more reliable than "OPS-SAT" |
| PLAY | Strong verb token (`▁PLAY`) |
| DOOM | Common word with reliable recognition |

The comma naturally inserts a pause, improving segmentation.

**Expected decoding:** `▁PRE TY ▁PLAY ▁D O O M`—optimal for intent matching.

### Commands to Avoid

**"OPS-SAT PLAY DOOM"**
- Hyphen has no token representation
- Acronym recognition is unreliable
- Common misdecodings: "OPS AT", "OP SAT", "OFF SAT", or complete omission

**"OPS SAT PRETTY PLAY DOOM"**
- Excessive low-frequency tokens
- High transcription drift risk

### High-Reliability Command Alternatives

For maximum ASR reliability:

```
"PRETTY, START DOOM."
"PRETTY, RUN DOOM."
```

Shorter commands reduce failure opportunities.

### Intent Matching Strategy

For robust command recognition in flight systems:

| Component | Role | Examples |
|-----------|------|----------|
| **Wake word** | PRETTY | Triggers attention |
| **Intent verb** | PLAY, START, RUN | Specifies action |
| **Asset ID** | DOOM | Identifies target application |

Imperfect ASR outputs such as:
- "pretty play doom"
- "pretty play dum"
- "pretty play do"

Can still reliably trigger the intended action through fuzzy matching:
1. Wake word contains "PRET" or "PRETTY"
2. Verb contains "PLAY", "START", or "RUN"
3. Asset contains "DOO", "DOOM", or "DUM"

## Test Samples

- `georges_opssat_clean.wav` - Clean voice sample
- `georges_opssat_noisy.wav` - Noisy voice sample
- `georges_opssat_very_noisy.wav` - Very noisy voice sample

## Output

Results written to `output/{small,base,large}/` directories.

### Output Directories

| Directory | Description |
|-----------|-------------|
| `output/small/` | greedy_search results |
| `output/small-beam/` | modified_beam_search results |
| `output/small-hotwords/` | beam search + hotwords results |

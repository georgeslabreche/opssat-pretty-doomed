# Distress call clips

Ground truth and model comparison for the distress call evaluation (issue #142). This folder holds the clip manifest, the clip audio, one reference transcript per clip, and the scripts that transcribe the clips with candidate speech-to-text models and score the outputs.

## Contents

| Path | Description |
|---|---|
| `clips.tsv` | Input list: DVIDS ID, short name, and page URL. |
| `build_manifest.py` | Reads each DVIDS page and writes `manifest.tsv`. Python standard library only. |
| `manifest.tsv` | Media URL, SHA-256, length, date taken, location, credit, and license marking per clip. |
| `labels.tsv` | Ground-truth labels per clip: `label` (distress for all 13) and `says_mayday` (whether the word is spoken, set by ear on 2026-11-04). |
| `clips/` | The original MP3 of each clip, as downloaded from DVIDS. |
| `prepare_clips.sh` | Checks each clip in `clips/` against its SHA-256, and converts it to 16 kHz mono WAV. Downloads a clip that is not in `clips/`. |
| `reference/` | One reference transcript per clip, `<id>-<name>.txt`. |
| `audio/` | WAV output of `prepare_clips.sh`. Gitignored. |
| `phrases.txt` | The distress phrase list, fixed before scoring. |
| `models.tsv` | The candidate models: archive, SHA-256, model files, and decoding method. |
| `fetch_models.sh` | Downloads each model in `models.tsv`, checks its SHA-256, and extracts it. |
| `models/` | Output of `fetch_models.sh`. Gitignored. |
| `requirements.txt` | Pinned Python packages for `transcribe.py`. |
| `transcribe.py` | Transcribes each clip with each model. Writes the output, the real-time factor, and the peak memory to `results/`. |
| `scoring.py` | The scoring rules: normalization, alignment, WER per speaker, and phrase matching. Python standard library only. |
| `score.py` | Scores the outputs in `results/` against the references. Writes `per_clip.tsv`, `summary.tsv`, and `RESULTS.md`. |
| `results/` | Model outputs and scores. |
| `tests/` | Unit tests for `scoring.py`. |

## Clips

The 13 clips are real distress calls that the US Coast Guard released on DVIDS. Each clip page carries the DVIDS public-domain marking (`public_domain` column), so the original MP3s are committed in `clips/`. The repo copy keeps the evaluation reproducible if DVIDS removes a clip. The `media_url` column gives the source of each file, and the SHA-256 check makes sure that the committed files are the same files. The WAVs are not committed, because `prepare_clips.sh` makes them again from the MP3s.

## Labels

Every clip is a confirmed distress call, so the clip-level label is distress for all 13. The label comes from the source and needs no listening. A clip is a detection when the detector finds a distress phrase anywhere in it, whether or not the word "mayday" is spoken: "swamped, going down" in the Oahu clip is a distress call. `labels.tsv` also records whether the word "mayday" is spoken, set by ear. It is spoken in 12 clips. The Oahu caller states the emergency without it ("swamped", "going down"). This gives a second, word-level metric, and it shows that a mayday-only detector misses real distress calls. The false-positive rate comes from non-distress sets that are also labeled by their source (MARTTS routine traffic, ATCO2).

The distress phrase list must come from standard distress vocabulary (IMO SMCP, and phrases such as "taking on water", "sinking", "going down", "fire", and "capsized"). Do not tune it to the words in these 13 clips, because the recall would then fit the test set.

The F/V Scandies Rose recording is not in this manifest. It is the last transmission of a crew that was lost. It is for evaluation only, and its audio and transcript must stay out of all public material.

## Usage

```bash
./prepare_clips.sh              # checks clips/ and writes WAVs to tools/distress/audio/
./build_manifest.py             # rebuilds manifest.tsv from clips.tsv
./fetch_models.sh               # downloads and checks the models (about 2.2 GB)
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/python transcribe.py  # writes results/<model>/
python3 score.py                # writes results/per_clip.tsv, summary.tsv, and RESULTS.md
python3 -m unittest discover -s tests
```

`prepare_clips.sh` requires `shasum` and `ffmpeg`, and `curl` for a clip that is not in `clips/`. To add a clip, add a row to `clips.tsv` and run `build_manifest.py`, then `prepare_clips.sh`, and commit the new MP3. A SHA-256 that changes on a page check means that DVIDS replaced a file. Keep the committed file, and record the change in the manifest.

## Reference transcripts

The reference transcripts are only for the word error rate (WER). Recall and the false-positive rate do not use them. Each file starts with a header, then the transcript, one sentence or speaker turn per line:

```
# id: 41096
# name: kupreanof
# status: draft
# source: <how the text was made>

Mayday, Mayday, Mayday.
This is the ...
```

| Header | Values |
|---|---|
| `status` | `draft`: machine output, not checked. `checked`: corrected by ear against the audio. |
| `source` | How the text was made. Set it to the person or method that checked it when `status` is `checked`. |
| `wer` | Optional. `exclude`: the clip is not used for the WER because most of its speech is unintelligible. The clip is still used for recall. |

The drafts are Parakeet TDT 0.6B v2 output. It was the most accurate model in the ground test of 2026-10-01. The drafts are only a starting point: the WER must use only files with `status: checked`. A transcript that is checked only in part must say so in `source`, for example: "caller turns checked by ear, USCG turns draft".

To check a transcript:

1. Listen to the clip, and correct the text. Write what is said, not what is meant. Write "[unintelligible]" for speech that cannot be understood. Write every number as words, the way it is said, for example "channel one six", "forty six sixteen point seven five", or "zero four fifty one". Do not use hyphens in numbers.
2. Start each line with a speaker label: `VESSEL:` for the vessel in distress, `USCG:` for a Coast Guard station, or `OTHER:` for any other station, for example a vessel that relays the call or comes to help.
3. Set `status: checked`.

Before it calculates the WER, the scorer normalizes the reference and the model output in the same way:

- It ignores case, punctuation, and line breaks. A hyphen counts as a space.
- It removes the `[unintelligible]` tags and the speaker labels.
- It removes filler words such as "uh" and "um".
- It changes digits to words, for example "16" to "sixteen" and "40" to "forty".
- It changes the words "oh" and "o" to "zero", because speakers often say "oh" for the digit 0.

The main WER uses the `VESSEL` and `OTHER` turns, that is, all turns that are not from the Coast Guard. The Coast Guard audio in these recordings is recorded at the shore station and is much clearer than the radio audio from the vessels. Thus a WER on all turns makes a model look better than it is on the radio speech that the detector must catch. The scorer also gives the WER on all turns, to show this difference.

The model transcribes the full clip, so its output has no speaker labels. The scorer aligns the model output with the full reference. Each substitution and deletion counts for the speaker of the reference word. Each insertion counts for the speaker of the reference word that comes before it.

The references have no digits, so the digit rule applies only to the model output. A reference with digits is ambiguous, because the scorer cannot know that "16" was said as "one six".

## Scoring

`score.py` gives these metrics for each model:

| Metric | Definition |
|---|---|
| Recall, ship-side | The number of clips where the model output has a phrase from `phrases.txt` in words that align with `VESSEL` or `OTHER` turns. This is the main recall. |
| Recall, whole clip | The number of clips where the model output has a phrase from `phrases.txt` anywhere. The Coast Guard turns often repeat distress words ("vessel taking on water"), so this recall is too high. |
| Mayday, ship-side | Of the clips where "mayday" is spoken (`labels.tsv`), the number where the model output has "mayday" in ship-side words. |
| WER, ship-side | The pooled WER of the `VESSEL` and `OTHER` turns over all clips without `wer: exclude`. This is the main WER. |
| WER, all turns | The pooled WER of all turns over the same clips. |
| Median RTF | The median real-time factor over the clips: processing time divided by audio length. |
| Peak memory | The peak resident memory of the process that loads the model and transcribes all clips. |

A phrase matches a run of whole words in the normalized model output. Only "mayday" also matches with one letter different (with the same first letter, as in the flight matcher), or as two words that make "mayday" when joined ("may day"). The flight matcher allows one letter different for every keyword. That rule is for the flight detector (issue #147). In this comparison it would make "fire" match "five", which is in many position reports.

A model word gets the speaker of the reference word that it aligns with. An extra model word gets the speaker of the reference word before it. This is the same rule as for the WER.

`transcribe.py` cuts each clip into 30 s windows, because the Parakeet models do not accept long audio. It uses one thread, as on the flight. The Zipformer models use `modified_beam_search`, as on the flight, and the Parakeet models use `greedy_search`. The real-time factor and the memory are from the machine that runs the script, not from the payload computer.

The flight binary uses sherpa-onnx 1.12.21. There is no Python package of that version for all platforms, so `requirements.txt` pins 1.13.8. The test of the flight binary itself is issue #148.

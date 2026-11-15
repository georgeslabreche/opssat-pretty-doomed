# Distress call clips

Ground truth for the distress call evaluation (issue #142). This folder holds the clip manifest, the clip audio, and one reference transcript per clip.

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

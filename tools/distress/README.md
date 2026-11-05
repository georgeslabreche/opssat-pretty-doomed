# Distress call clips

Ground truth for the distress call evaluation (issue #142). This folder holds the clip manifest, the fetch script, and one reference transcript per clip. All later results are scored against the reference transcripts.

## Contents

| Path | Description |
|---|---|
| `clips.tsv` | Input list: DVIDS ID, short name, and page URL. |
| `build_manifest.py` | Reads each DVIDS page and writes `manifest.tsv`. Python standard library only. |
| `manifest.tsv` | Media URL, SHA-256, length, date taken, location, credit, and license marking per clip. |
| `clips/` | The original MP3 of each clip, as downloaded from DVIDS. |
| `prepare_clips.sh` | Checks each clip in `clips/` against its SHA-256, and converts it to 16 kHz mono WAV. Downloads a clip that is not in `clips/`. |
| `reference/` | One reference transcript per clip, `<id>-<name>.txt`. |
| `audio/` | WAV output of `prepare_clips.sh`. Gitignored. |

## Clips

The 13 clips are real distress calls that the US Coast Guard released on DVIDS. Each clip page carries the DVIDS public-domain marking (`public_domain` column), so the original MP3s are committed in `clips/`. The repo copy keeps the evaluation reproducible if DVIDS removes a clip. The `media_url` column gives the source of each file, and the SHA-256 check makes sure that the committed files are the same files. The WAVs are not committed, because `prepare_clips.sh` makes them again from the MP3s.

The F/V Scandies Rose recording is not in this manifest. It is the last transmission of a crew that was lost. It is for evaluation only, and its audio and transcript must stay out of all public material.

## Usage

```bash
./prepare_clips.sh              # checks clips/ and writes WAVs to tools/distress/audio/
./build_manifest.py             # rebuilds manifest.tsv from clips.tsv
```

`prepare_clips.sh` requires `shasum` and `ffmpeg`, and `curl` for a clip that is not in `clips/`. To add a clip, add a row to `clips.tsv` and run `build_manifest.py`, then `prepare_clips.sh`, and commit the new MP3. A SHA-256 that changes on a page check means that DVIDS replaced a file. Keep the committed file, and record the change in the manifest.

## Reference transcripts

Each file starts with a header, then the transcript, one sentence or speaker turn per line:

```
# id: 41096
# name: kupreanof
# status: draft
# source: <how the text was made>
# contains_mayday: unchecked

Mayday, Mayday, Mayday.
This is the ...
```

| Header | Values |
|---|---|
| `status` | `draft`: machine output, not checked. `checked`: corrected by ear against the audio. |
| `source` | How the text was made. Set it to the person or method that checked it when `status` is `checked`. |
| `contains_mayday` | `yes`, `no`, or `unchecked`. Set it by ear. It is the ground truth for keyword recall. |

The drafts are Parakeet TDT 0.6B v2 output. It was the most accurate model in the ground test of 2026-10-01. The drafts are only a starting point: the scoring must use only files with `status: checked`.

To check a transcript:

1. Listen to the clip, and correct the text. Write what is said, not what is meant. Write "[unintelligible]" for speech that cannot be understood.
2. Optionally, start a line with `VESSEL:`, `USCG:`, or `OTHER:` to show the speaker. A line without a label counts as speech from any speaker.
3. Set `contains_mayday` and `status: checked`.

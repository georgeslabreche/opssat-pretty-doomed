# Distress call clips

Ground truth for the distress call evaluation (issue #142). This folder holds the clip manifest, the fetch script, and one reference transcript per clip. All later results are scored against the reference transcripts.

## Contents

| Path | Description |
|---|---|
| `clips.tsv` | Input list: DVIDS ID, short name, and page URL. |
| `build_manifest.py` | Reads each DVIDS page and writes `manifest.tsv`. Python standard library only. |
| `manifest.tsv` | Media URL, SHA-256, length, date taken, location, credit, and license marking per clip. |
| `fetch_clips.sh` | Downloads each clip, checks the SHA-256, and converts it to 16 kHz mono WAV. |
| `reference/` | One reference transcript per clip, `<id>-<name>.txt`. |
| `audio/` | Fetch output. Gitignored. |

## Clips

The 13 clips are real distress calls that the US Coast Guard released on DVIDS. Each clip page carries the DVIDS public-domain marking (`public_domain` column). The audio is not committed. `fetch_clips.sh` gets the exact files from the DVIDS media server, and the SHA-256 check makes sure that they are the same files.

The F/V Scandies Rose recording is not in this manifest. It is the last transmission of a crew that was lost. It is for evaluation only, and its audio and transcript must stay out of all public material.

## Usage

```bash
./fetch_clips.sh                # downloads to tools/distress/audio/
./build_manifest.py             # rebuilds manifest.tsv from clips.tsv
```

`fetch_clips.sh` requires `curl`, `shasum`, and `ffmpeg`. Run `build_manifest.py` only to add clips or to check the pages again. A changed SHA-256 means that DVIDS replaced a file, and the reference transcript for that clip must be checked again.

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

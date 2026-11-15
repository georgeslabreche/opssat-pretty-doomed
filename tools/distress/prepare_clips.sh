#!/usr/bin/env bash
# Check the committed distress call clips against manifest.tsv, and convert
# each clip to 16 kHz mono WAV for speech-to-text.
#
# The original MP3s are committed in clips/. A clip that is missing from
# clips/ (for example, a new manifest row) is downloaded from its media URL.
#
# Usage: prepare_clips.sh [wav-output-dir]   (default: tools/distress/audio, gitignored)
# Requires: shasum and ffmpeg (and curl for a missing clip).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
out="${1:-$here/audio}"
mkdir -p "$out" "$here/clips"

fail=0
while IFS=$'\t' read -r id name _page media sha _rest; do
    [ "$id" = "id" ] && continue
    mp3="$here/clips/$id-$name.mp3"
    wav="$out/$id-$name.wav"
    if [ ! -f "$mp3" ]; then
        echo "get  $id $name (not in clips/, downloading)"
        curl -sSfL -A "opssat-pretty-doomed-fetch/1.0" -o "$mp3" "$media"
    fi
    actual="$(shasum -a 256 "$mp3" | cut -d' ' -f1)"
    if [ "$actual" != "$sha" ]; then
        echo "FAIL $id $name: SHA-256 mismatch ($actual)" >&2
        fail=1
        continue
    fi
    if [ ! -f "$wav" ]; then
        ffmpeg -loglevel error -y -i "$mp3" -ac 1 -ar 16000 -c:a pcm_s16le "$wav"
    fi
    echo "ok   $id $name"
done < "$here/manifest.tsv"

exit "$fail"

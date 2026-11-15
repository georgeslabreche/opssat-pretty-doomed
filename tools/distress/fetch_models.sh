#!/usr/bin/env bash
# Download the candidate speech-to-text models in models.tsv from the
# sherpa-onnx releases, check each archive against its SHA-256, and extract it.
#
# Usage: fetch_models.sh [model-dir]   (default: tools/distress/models, gitignored)
# Requires: curl, shasum, and tar. The archives are about 2.2 GB in total.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
out="${1:-$here/models}"
base="https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models"
mkdir -p "$out"

fail=0
while IFS=$'\t' read -r name _type archive sha _rest; do
    [ "$name" = "name" ] && continue
    tarball="$out/$archive.tar.bz2"
    if [ ! -f "$tarball" ]; then
        echo "get  $name"
        curl -sSfL -o "$tarball" "$base/$archive.tar.bz2"
    fi
    actual="$(shasum -a 256 "$tarball" | cut -d' ' -f1)"
    if [ "$actual" != "$sha" ]; then
        echo "FAIL $name: SHA-256 mismatch ($actual)" >&2
        fail=1
        continue
    fi
    if [ ! -d "$out/$archive" ]; then
        tar -xjf "$tarball" -C "$out"
    fi
    echo "ok   $name"
done < "$here/models.tsv"

exit "$fail"

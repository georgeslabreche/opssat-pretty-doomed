#!/usr/bin/env bash
# Ground baseline of the EM package (issue #142): the pretty-doomed binary with
# the distress configs, on the ground, in Docker.
#
# It uses the same binary source, configs, and sherpa-onnx version (1.13.8) as
# the EM package. The comparison of results/binary-<model> with
# results/em-<model> shows the effect of the EM hardware only.
#
# Usage (from tools/distress, after fetch_models.sh and prepare_clips.sh):
#   ./run_binary.sh
# Output: binary-run/ (raw, gitignored), then pretty-doomed/docs/distress/results/binary-<model>/ via import_em.py.
# Environment: DISTRESS_MODELS (default: the five configs of the EM package).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
app="$repo/pretty-doomed"
image="pretty-doomed-local:sherpa-1.13.8"
models="${DISTRESS_MODELS:-zipformer-small-int8 parakeet-110m-int8 parakeet-0.6b-v2-int8-w5 parakeet-0.6b-v2-int8 parakeet-0.6b-v2-int8-t2}"
out="$here/binary-run"
mkdir -p "$out"

if ! docker image inspect "$image" > /dev/null 2>&1; then
    docker build --build-arg SHERPA_VERSION=1.13.8 -t "$image" "$app"
fi

docker run --rm -w /app \
    -v "$app/src:/app/src:ro" -v "$repo/common:/app/common:ro" \
    -v "$app/Makefile:/app/Makefile:ro" -v "$app/VERSION:/app/VERSION:ro" \
    -v "$app/models/sherpa-onnx:/app/models/sherpa-onnx:ro" -v "$app/ascii.txt:/app/ascii.txt:ro" \
    -v "$repo/doom/demos:/app/demos:ro" -v "$repo/doom/build:/app/doom-build:ro" \
    -v "$app/distress:/app/distress-src:ro" \
    -v "$here/models/sherpa-onnx-nemo-parakeet_tdt_transducer_110m-en-36000-int8:/app/models/parakeet-110m-int8:ro" \
    -v "$here/models/sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8:/app/models/parakeet-0.6b-v2-int8:ro" \
    -v "$here/audio:/clips:ro" -v "$out:/out" \
    -e MODELS="$models" \
    "$image" bash -c '
        make BUILDDIR=/tmp/build /tmp/build/pretty-doomed -j4 > /tmp/build.log 2>&1 || { tail -30 /tmp/build.log; exit 1; }
        for m in $MODELS; do
            /tmp/build/pretty-doomed -i /clips -c distress-src/config.distress.$m.cfg \
                -f distress-src/variants.distress.cfg -o /out/$m -d demos \
                -e doom-build/local/opssat-doom 2> /dev/null
            echo "$m exit=$?"
        done'

printf 'models: %s\nversion: ground binary, sherpa-onnx 1.13.8\n' "$models" > "$out/manifest.txt"
python3 "$here/import_em.py" "$out" --label binary

#!/bin/sh
# Convert audio recording to OPS-SAT format (48 kHz, mono, 16-bit PCM)
# Usage: ./convert-sample.sh input.mp3 output.wav

if [ $# -ne 2 ]; then
    echo "Usage: $0 <input> <output.wav>"
    echo "Converts audio to 48 kHz mono 16-bit PCM WAV"
    exit 1
fi

ffmpeg -i "$1" -ar 48000 -ac 1 -sample_fmt s16 "$2"

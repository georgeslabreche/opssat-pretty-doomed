#!/usr/bin/env python3
"""
Render audio to a scrolling-spectrogram video with the audio as the soundtrack.

Built for the recovered capture audio in the flight debriefings, where the
signal is a voice command in a narrow band. The spectrogram is limited to the
voice band (default 0 to 4 kHz) by resampling to 2x that rate, so the frame is
filled with the part of the spectrum that carries the voice rather than mostly
dark high-frequency space. The audio is muxed in.

Two input modes:
  1. A single WAV (positional).
  2. Several per-capture WAVs (--capture-audio) laid out at their capture-window
     offsets (--capture-windows), so one video spans a whole run with the voice
     bursts in their real relative positions and silence in the gaps. The track
     is trimmed to the capture span plus --trim-pad-s before the first capture
     and after the last.

Requires ffmpeg on PATH.

Usage:
  python3 audio_spectrogram_video.py capture.wav --output out.mp4
  python3 audio_spectrogram_video.py --output run.mp4 \
      --capture-audio "c1.wav,c2.wav,c3.wav,c4.wav,c5.wav,c6.wav" \
      --capture-windows "[[9,31],[35,56],[60,82],[86,114],[119,146],[151,173]]" \
      --trim-pad-s 2
"""
import argparse
import json
import subprocess
import sys
import tempfile
import os


def build_capture_track(audios, windows, pad_s, out_wav):
    """Lay each capture WAV at its window start, trimmed to the capture span
    plus pad_s on each side, into a single mono WAV via ffmpeg. Preserves the
    real inter-capture timing, so the gaps between captures (when the SDR was
    not recording) appear as silence."""
    n = min(len(audios), len(windows))
    starts = [windows[i][0] for i in range(n)]
    ends = [windows[i][1] for i in range(n)]
    origin = min(starts) - pad_s          # time zero of the output track
    duration = (max(ends) - min(starts)) + 2 * pad_s
    inputs, filters, labels = [], [], []
    for i in range(n):
        off_ms = int(round((starts[i] - origin) * 1000))
        inputs += ["-i", audios[i]]
        filters.append(f"[{i}]adelay={off_ms}|{off_ms}[a{i}]")
        labels.append(f"[a{i}]")
    fc = ";".join(filters) + ";" + "".join(labels) + \
        f"amix=inputs={n}:normalize=0[m];[m]apad,atrim=0:{duration:.3f}[out]"
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", *inputs,
                    "-filter_complex", fc, "-map", "[out]", out_wav], check=True)
    return duration


def build_concat_track(audios, pad_s, out_wav):
    """Join the capture WAVs end to end (no inter-capture gaps), with pad_s of
    silence prepended and appended, into a single mono WAV via ffmpeg."""
    n = len(audios)
    inputs = []
    for wav in audios:
        inputs += ["-i", wav]
    concat_in = "".join(f"[{i}]" for i in range(n))
    pad_ms = int(round(pad_s * 1000))
    fc = (f"{concat_in}concat=n={n}:v=0:a=1[c];"
          f"[c]adelay={pad_ms}|{pad_ms},apad=pad_dur={pad_s}[out]")
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", *inputs,
                    "-filter_complex", fc, "-map", "[out]", out_wav], check=True)


def main():
    p = argparse.ArgumentParser(description="Audio -> scrolling-spectrogram video with sound")
    p.add_argument("input", nargs="?", help="Input WAV (single-file mode)")
    p.add_argument("--output", required=True, help="Output .mp4")
    p.add_argument("--max-freq", type=int, default=4000,
                   help="Top of the displayed frequency band in Hz (default 4000, the "
                        "voice band). The audio is resampled to 2x this so the "
                        "spectrogram fills the frame instead of showing dark high "
                        "frequencies.")
    p.add_argument("--size", default="900x360", help="Video size WxH (default 900x360)")
    p.add_argument("--capture-audio", default=None,
                   help="Comma-separated per-capture WAVs, laid out at their "
                        "--capture-windows offsets into one run-length track.")
    p.add_argument("--capture-windows", default=None,
                   help="JSON list of [start_offset_s, end_offset_s] per capture, "
                        "the same reference used by animate_pointing.py. Required "
                        "with --capture-audio.")
    p.add_argument("--trim-pad-s", type=float, default=2.0,
                   help="Seconds of silence before the first capture and after the "
                        "last capture in the assembled track (default 2.0).")
    p.add_argument("--concat", action="store_true",
                   help="Join the --capture-audio WAVs end to end with no gaps "
                        "between captures (only start/end padding), instead of "
                        "placing them at their --capture-windows offsets. Use this "
                        "for a spectrogram of the voice content without the silent "
                        "inter-capture gaps. --capture-windows is not needed.")
    args = p.parse_args()

    tmp = None
    if args.capture_audio:
        audios = [a for a in args.capture_audio.split(",") if a]
        tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False).name
        if args.concat:
            build_concat_track(audios, args.trim_pad_s, tmp)
            print(f"Concatenated {len(audios)} captures end to end "
                  f"(+/-{args.trim_pad_s:g}s pad).")
        else:
            if not args.capture_windows:
                raise SystemExit("--capture-audio needs --capture-windows (or --concat).")
            windows = json.loads(args.capture_windows)
            dur = build_capture_track(audios, windows, args.trim_pad_s, tmp)
            print(f"Assembled {min(len(audios), len(windows))} captures into a "
                  f"{dur:.1f}s track at real offsets (+/-{args.trim_pad_s:g}s pad).")
        source = tmp
    elif args.input:
        source = args.input
    else:
        raise SystemExit("Provide a WAV positionally or use --capture-audio.")

    rate = 2 * args.max_freq
    # Resample to the voice band for both the spectrogram image and the muxed
    # audio; showspectrum then spans 0 to max-freq, filling the frame.
    fc = (f"[0:a]aresample={rate},asplit=2[viz][snd];"
          f"[viz]showspectrum=s={args.size}:mode=combined:slide=scroll:"
          f"color=intensity:scale=log:fscale=lin,format=yuv420p[v]")
    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-i", source,
           "-filter_complex", fc, "-map", "[v]", "-map", "[snd]",
           "-c:v", "libx264", "-c:a", "aac", "-shortest", args.output]
    print(f"Writing {args.output} (spectrogram 0-{args.max_freq} Hz, {args.size})...")
    subprocess.run(cmd, check=True)
    if tmp:
        os.remove(tmp)
    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

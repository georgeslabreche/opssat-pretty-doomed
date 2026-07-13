#!/usr/bin/env python3
"""
Summarize and compare carrier detections across several recordings.

Companion to detect_carrier.py: that tool characterizes one clip; this one reads
the carrier.json files it produced for a set of clips (for example the snapshots
of a single pass, or two passes of an RF-link test) and tabulates them side by
side so the trend across the pass is visible: how the absolute carrier frequency
walks with Doppler, how the SNR changes, and where the carrier keys on and off.

Inputs may be carrier.json files or directories searched recursively for them.
When a file sits in a directory named like the SDR capture
(sdr_YYYYMMDD_HHMMSS_...), the timestamp is parsed from that name and used to
order the clips and label the time axis.

Produces (in --output-dir, default the common parent of the inputs):
  - carriers_summary.txt    Human-readable comparison table
  - carriers_summary.json   Machine-readable list of per-clip metrics
  - carriers_summary.svg    Offset-vs-time and SNR-vs-time across the clips

Usage:
  python summarize_carriers.py analysis/ --target-freq 1296.0e6
  python summarize_carriers.py a/carrier.json b/carrier.json --output-dir out
"""
import argparse
import glob
import json
import os
import re
import sys

# A real narrowband carrier stands well above the floor and is spectrally
# narrow; a wide, low-SNR "strongest peak" is just the noise/spur background.
DETECT_SNR_DB = 15.0
DETECT_BW3_HZ = 5000.0

TS_RE = re.compile(r"sdr_(\d{8})_(\d{6})_")


def find_jsons(inputs):
    """Expand files/directories into a de-duplicated list of carrier.json paths."""
    paths = []
    for inp in inputs:
        if os.path.isdir(inp):
            paths.extend(glob.glob(os.path.join(inp, "**", "carrier.json"), recursive=True))
        elif os.path.isfile(inp):
            paths.append(inp)
        else:
            print(f"Warning: {inp} not found, skipping", file=sys.stderr)
    # stable de-dup
    seen, out = set(), []
    for p in paths:
        rp = os.path.abspath(p)
        if rp not in seen:
            seen.add(rp)
            out.append(p)
    return out


def clip_label(json_path):
    """Derive a short label and sort key from the enclosing capture dir name."""
    name = os.path.basename(os.path.dirname(os.path.abspath(json_path)))
    m = TS_RE.search(name + "_")
    if m:
        date, tod = m.group(1), m.group(2)
        secs = int(tod[:2]) * 3600 + int(tod[2:4]) * 60 + int(tod[4:])
        return f"{tod[:2]}:{tod[2:4]}:{tod[4:]}", (date, secs), secs
    return name, (name, 0), 0


def load_clip(json_path, target_freq):
    with open(json_path) as f:
        r = json.load(f)
    label, sortkey, secs = clip_label(json_path)
    tgt = target_freq if target_freq is not None else r.get("target_freq_hz")
    off_tgt = r.get("carrier_offset_from_target_hz")
    if off_tgt is None and tgt is not None and r.get("carrier_freq_hz") is not None:
        off_tgt = r["carrier_freq_hz"] - tgt
    detected = (r.get("snr_db", 0) >= DETECT_SNR_DB
                and r.get("bw_3db_hz", 1e9) <= DETECT_BW3_HZ)
    return {
        "label": label,
        "sortkey": sortkey,
        "secs": secs,
        "capture": os.path.basename(os.path.dirname(os.path.abspath(json_path))),
        "carrier_freq_hz": r.get("carrier_freq_hz"),
        "offset_from_target_hz": off_tgt,
        "snr_db": r.get("snr_db"),
        "bw_3db_hz": r.get("bw_3db_hz"),
        "on_fraction": r.get("on_fraction"),
        "keying_100ms": r.get("keying_100ms", ""),
        "drift_hz_per_s": r.get("drift_hz_per_s"),
        "detected": detected,
    }


def format_table(clips):
    lines = []
    lines.append("Carrier comparison across clips")
    lines.append("=" * 92)
    hdr = (f"{'time':>9}  {'det':>3}  {'abs freq (MHz)':>14}  {'vs tgt (kHz)':>12}  "
           f"{'SNR dB':>6}  {'3dB Hz':>7}  {'on%':>4}  {'drift Hz/s':>10}  keying")
    lines.append(hdr)
    lines.append("-" * 92)
    for c in clips:
        af = f"{c['carrier_freq_hz']/1e6:.4f}" if c["carrier_freq_hz"] is not None else "-"
        ot = f"{c['offset_from_target_hz']/1e3:+.1f}" if c["offset_from_target_hz"] is not None else "-"
        snr = f"{c['snr_db']:.1f}" if c["snr_db"] is not None else "-"
        bw = f"{c['bw_3db_hz']:.0f}" if c["bw_3db_hz"] is not None else "-"
        onf = f"{100*c['on_fraction']:.0f}" if c["on_fraction"] is not None else "-"
        dr = f"{c['drift_hz_per_s']:+.0f}" if c["drift_hz_per_s"] is not None else "-"
        det = "yes" if c["detected"] else "no"
        lines.append(f"{c['label']:>9}  {det:>3}  {af:>14}  {ot:>12}  {snr:>6}  "
                     f"{bw:>7}  {onf:>4}  {dr:>10}  {c['keying_100ms']}")
    lines.append("-" * 92)
    n_det = sum(1 for c in clips if c["detected"])
    lines.append(f"{n_det}/{len(clips)} clips show a narrowband carrier "
                 f"(SNR >= {DETECT_SNR_DB:.0f} dB and -3 dB width <= {DETECT_BW3_HZ:.0f} Hz).")
    return "\n".join(lines)


def plot_summary(clips, save_path, title):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    det = [c for c in clips if c["detected"] and c["offset_from_target_hz"] is not None]
    t0 = clips[0]["secs"]
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(9, 7), sharex=True)

    xs = [c["secs"] - t0 for c in det]
    ys = [c["offset_from_target_hz"] / 1e3 for c in det]
    ax1.plot(xs, ys, "o-", color="#1f77b4")
    for c, x, y in zip(det, xs, ys):
        ax1.annotate(c["label"], (x, y), textcoords="offset points",
                     xytext=(6, 4), fontsize=8)
    ax1.axhline(0, color="0.6", lw=0.8, ls="--")
    ax1.set_ylabel("carrier offset from target (kHz)")
    ax1.set_title(title)
    ax1.grid(True, alpha=0.3)

    sx = [c["secs"] - t0 for c in clips]
    sy = [c["snr_db"] for c in clips]
    colors = ["#2ca02c" if c["detected"] else "#d62728" for c in clips]
    ax2.scatter(sx, sy, c=colors, zorder=3)
    ax2.plot(sx, sy, color="0.7", lw=0.8, zorder=2)
    ax2.set_ylabel("SNR over floor (dB)")
    ax2.set_xlabel(f"seconds since first clip ({clips[0]['label']})")
    ax2.grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(save_path)
    plt.close(fig)


def main():
    p = argparse.ArgumentParser(description="Compare carrier detections across clips")
    p.add_argument("inputs", nargs="+", help="carrier.json files or directories to search")
    p.add_argument("--target-freq", type=float, default=None,
                   help="Expected uplink frequency in Hz, overrides per-file target")
    p.add_argument("--output-dir", help="Output directory (default: common parent of inputs)")
    p.add_argument("--title", default="Carrier across clips", help="Plot title")
    args = p.parse_args()

    jsons = find_jsons(args.inputs)
    if not jsons:
        print("Error: no carrier.json files found", file=sys.stderr)
        return 1

    clips = sorted((load_clip(j, args.target_freq) for j in jsons),
                   key=lambda c: c["sortkey"])

    table = format_table(clips)
    print("\n" + table + "\n")

    out_dir = args.output_dir or os.path.commonpath([os.path.dirname(os.path.abspath(j)) for j in jsons])
    os.makedirs(out_dir, exist_ok=True)

    txt = os.path.join(out_dir, "carriers_summary.txt")
    with open(txt, "w") as f:
        f.write(table + "\n")
    print(f"  {txt}")

    js = os.path.join(out_dir, "carriers_summary.json")
    with open(js, "w") as f:
        json.dump([{k: v for k, v in c.items() if k != "sortkey"} for c in clips], f, indent=2)
    print(f"  {js}")

    svg = os.path.join(out_dir, "carriers_summary.svg")
    plot_summary(clips, svg, args.title)
    print(f"  {svg}")

    return 0


if __name__ == "__main__":
    sys.exit(main())

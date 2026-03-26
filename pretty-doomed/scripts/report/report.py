#!/usr/bin/env python3
"""
Generate an HTML report for an SDR capture or loopback run directory.

Processes all sc16 and WAV files, generates plots, and produces a single
HTML page with embedded SVGs for easy review.

Usage:
  python report.py /data/capture-artifacts/pack-4023_.../chg/toGround/run-000001 --output-dir /output
  python report.py /data/loopback-artifacts/run-000001 --output-dir /output --loopback --input-wav /data/samples/input.wav
"""
import argparse
import glob
import json
import os
import sys
import textwrap
from datetime import datetime

from output_dir import resolve_output_dir
from iq import read_sc16, compute_psd, compute_signal_stats, read_wav_mono
from plots import (fig_to_svg, plot_spectrogram, plot_psd, plot_constellation,
                   plot_iq_amplitude, plot_audio_spectrogram, plot_audio_waveform,
                   plot_loopback, plot_psd_comparison)
from animation import render_animation

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


# ---------------------------------------------------------------------------
# HTML template
# ---------------------------------------------------------------------------

HTML_TEMPLATE = textwrap.dedent("""\
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>PRETTY SDR Report — {title}</title>
<style>
  body {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif;
         max-width: 1200px; margin: 0 auto; padding: 20px; background: #fafafa; color: #333; }}
  h1 {{ border-bottom: 2px solid #333; padding-bottom: 8px; }}
  h2 {{ margin-top: 40px; color: #555; }}
  h3 {{ color: #666; }}
  .capture {{ border: 1px solid #ddd; border-radius: 8px; padding: 20px; margin: 20px 0;
              background: white; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }}
  .stats {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
            gap: 10px; margin: 15px 0; }}
  .stat {{ background: #f5f5f5; padding: 8px 12px; border-radius: 4px; font-size: 14px; }}
  .stat .label {{ color: #888; font-size: 12px; }}
  .stat .value {{ font-weight: 600; font-size: 16px; }}
  .plots {{ display: grid; grid-template-columns: 1fr; gap: 10px; }}
  .plots svg {{ width: 100%; height: auto; }}
  .side-by-side {{ display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }}
  .log {{ background: #f8f8f8; border: 1px solid #eee; border-radius: 4px; padding: 12px;
          font-family: monospace; font-size: 12px; white-space: pre-wrap; max-height: 300px;
          overflow-y: auto; }}
  .badge {{ display: inline-block; padding: 2px 8px; border-radius: 4px; font-size: 12px;
            font-weight: 600; }}
  .badge-pass {{ background: #d4edda; color: #155724; }}
  .badge-warn {{ background: #fff3cd; color: #856404; }}
  .badge-fail {{ background: #f8d7da; color: #721c24; }}
  .tabs {{ margin: 20px 0; }}
  .tab-bar {{ display: flex; gap: 0; border-bottom: 2px solid #ddd; }}
  .tab-btn {{ padding: 10px 20px; border: 1px solid transparent; border-bottom: none;
              background: none; cursor: pointer; font-size: 14px; font-weight: 500;
              color: #666; border-radius: 6px 6px 0 0; margin-bottom: -2px; }}
  .tab-btn:hover {{ background: #f0f0f0; }}
  .tab-btn.active {{ background: white; border-color: #ddd; color: #333;
                     border-bottom: 2px solid white; }}
  .tab-panel {{ display: none; padding: 10px 0; }}
  .tab-panel.active {{ display: block; }}
  footer {{ margin-top: 40px; padding-top: 10px; border-top: 1px solid #ddd;
            color: #999; font-size: 12px; }}
</style>
</head>
<body>
<h1>PRETTY SDR Report</h1>
<p><strong>{title}</strong> — generated {timestamp}</p>
{content}
<footer>OPS-SAT PRETTY Experiment — Ground Analysis Tools</footer>
<script>
document.querySelectorAll('.tab-btn').forEach(btn => {{
  btn.addEventListener('click', () => {{
    const tabs = btn.closest('.tabs');
    tabs.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    tabs.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'));
    btn.classList.add('active');
    tabs.querySelector('#' + btn.dataset.tab).classList.add('active');
  }});
}});
</script>
</body>
</html>
""")


def stat_html(label, value):
    return f'<div class="stat"><div class="label">{label}</div><div class="value">{value}</div></div>'


def badge(text, level):
    return f'<span class="badge badge-{level}">{text}</span>'


# ---------------------------------------------------------------------------
# Report builders
# ---------------------------------------------------------------------------

def process_capture_dir(capture_dir, sample_rate, output_dir, no_video=False):
    """Process a single capture directory (sc16 + wav + log). Returns HTML fragment."""
    sc16_files = sorted(glob.glob(os.path.join(capture_dir, "*.sc16")))
    wav_files = sorted(glob.glob(os.path.join(capture_dir, "*.wav")))
    log_files = sorted(glob.glob(os.path.join(capture_dir, "*.log")))

    name = os.path.basename(capture_dir)
    svg_dir = os.path.join(output_dir, name)
    os.makedirs(svg_dir, exist_ok=True)

    parts = [f'<div class="capture"><h3>{name}</h3>']

    for sc16 in sc16_files:
        print(f"  Processing {sc16}...")
        iq = read_sc16(sc16)
        freqs, psd_db = compute_psd(iq, sample_rate)
        stats = compute_signal_stats(iq, sample_rate, freqs, psd_db)

        snr_badge = badge(f"SNR {stats['snr_estimate_db']:.1f} dB",
                          "pass" if stats['snr_estimate_db'] > 10 else
                          "warn" if stats['snr_estimate_db'] > 3 else "fail")

        parts.append(f'<h4>{os.path.basename(sc16)} {snr_badge}</h4>')
        parts.append('<div class="stats">')
        parts.append(stat_html("Duration", f"{stats['duration_s']:.2f} s"))
        parts.append(stat_html("RMS Level", f"{stats['rms_dbfs']:.1f} dBFS"))
        parts.append(stat_html("Peak Level", f"{stats['peak_dbfs']:.1f} dBFS"))
        parts.append(stat_html("Crest Factor", f"{stats['crest_factor_db']:.1f} dB"))
        parts.append(stat_html("SNR Estimate", f"{stats['snr_estimate_db']:.1f} dB"))
        parts.append(stat_html("Noise Floor", f"{stats['noise_floor_db']:.1f} dB"))
        parts.append(stat_html("Freq Offset", f"{stats['freq_offset_hz']:.0f} Hz"))
        parts.append(stat_html("Occupied BW", f"{stats['occupied_bw_hz']:.0f} Hz"))
        parts.append(stat_html("DC Offset (I/Q)", f"{stats['dc_offset_i']:.4f} / {stats['dc_offset_q']:.4f}"))
        parts.append(stat_html("RMS I / Q", f"{stats['rms_i']:.4f} ({stats['rms_i_dbfs']:.1f} dBFS) / {stats['rms_q']:.4f} ({stats['rms_q_dbfs']:.1f} dBFS)"))
        zero_i_pct = stats['zero_fraction_i'] * 100
        zero_q_pct = stats['zero_fraction_q'] * 100
        zero_level = "fail" if max(zero_i_pct, zero_q_pct) > 10 else "warn" if max(zero_i_pct, zero_q_pct) > 1 else "pass"
        parts.append(stat_html("Zero Fraction (I/Q)", f"{zero_i_pct:.2f}% / {zero_q_pct:.2f}% {badge('Q dropout' if zero_q_pct > 10 else 'OK', zero_level)}"))
        parts.append(stat_html("Gain Imbalance", f"{stats['gain_imbalance_db']:.2f} dB"))
        parts.append(stat_html("Phase Imbalance", f"{stats['phase_imbalance_deg']:.2f}°"))
        parts.append('</div>')

        parts.append('<div class="plots">')
        parts.append(plot_spectrogram(iq, sample_rate, save_path=os.path.join(svg_dir, "spectrogram.svg")))
        parts.append(plot_psd(freqs, psd_db, stats=stats, save_path=os.path.join(svg_dir, "psd.svg")))
        parts.append(plot_iq_amplitude(iq, sample_rate, save_path=os.path.join(svg_dir, "iq_amplitude.svg")))
        parts.append('</div>')
        parts.append('<div class="side-by-side">')
        parts.append(plot_constellation(iq, save_path=os.path.join(svg_dir, "constellation.svg")))
        parts.append('</div>')

        # PSD evolution animation (with audio if available)
        if not no_video:
            print(f"  Rendering PSD animation...")
            mp4_path = os.path.join(svg_dir, "psd_evolution.mp4")
            wav_for_audio = wav_files[0] if wav_files else None
            if render_animation(iq, sample_rate, mp4_path, audio_path=wav_for_audio):
                rel_path = f"{name}/psd_evolution.mp4"
                parts.append(f'<video controls width="100%" style="margin:10px 0">'
                             f'<source src="{rel_path}" type="video/mp4">'
                             f'PSD evolution animation</video>')

        # Save stats
        with open(os.path.join(svg_dir, "iq_stats.json"), "w") as f:
            json.dump(stats, f, indent=2)

    for wav in wav_files:
        print(f"  Processing {wav}...")
        samples, sr = read_wav_mono(wav)
        parts.append(f'<h4>{os.path.basename(wav)}</h4>')
        parts.append('<div class="plots">')
        parts.append(plot_audio_spectrogram(samples, sr, save_path=os.path.join(svg_dir, "audio_spectrogram.svg")))
        parts.append(plot_audio_waveform(samples, sr, save_path=os.path.join(svg_dir, "audio_waveform.svg")))
        parts.append('</div>')

    for log in log_files:
        try:
            with open(log, "r") as f:
                log_text = f.read()
            if log_text.strip():
                parts.append(f'<details><summary>{os.path.basename(log)}</summary>')
                parts.append(f'<div class="log">{log_text}</div></details>')
        except Exception:
            pass

    parts.append('</div>')
    return "\n".join(parts)


def build_capture_report(run_dir, sample_rate, output_dir, no_video=False):
    """Build report for an sdr-capture run directory."""
    content_parts = []

    # Check for capture subdirectories (capture-001, capture-002, ...)
    capture_dirs = sorted(glob.glob(os.path.join(run_dir, "capture-*")))
    if len(capture_dirs) > 1:
        # Multiple captures: wrap in tabs
        tab_group = f"cap-{id(capture_dirs)}"
        content_parts.append('<div class="tabs">')
        content_parts.append('<div class="tab-bar">')
        for i, cd in enumerate(capture_dirs):
            name = os.path.basename(cd)
            tab_id = f"tab-{tab_group}-{name}"
            active = " active" if i == 0 else ""
            content_parts.append(f'<button class="tab-btn{active}" data-tab="{tab_id}">{name}</button>')
        content_parts.append('</div>')
        for i, cd in enumerate(capture_dirs):
            name = os.path.basename(cd)
            tab_id = f"tab-{tab_group}-{name}"
            active = " active" if i == 0 else ""
            content_parts.append(f'<div id="{tab_id}" class="tab-panel{active}">')
            content_parts.append(process_capture_dir(cd, sample_rate, output_dir, no_video))
            content_parts.append('</div>')
        content_parts.append('</div>')
    elif capture_dirs:
        content_parts.append(process_capture_dir(capture_dirs[0], sample_rate, output_dir, no_video))
    else:
        # Flat structure (sc16/wav files directly in run dir)
        content_parts.append(process_capture_dir(run_dir, sample_rate, output_dir, no_video))

    # Summary file
    summary_path = os.path.join(run_dir, "summary.txt")
    if os.path.isfile(summary_path):
        with open(summary_path, "r") as f:
            summary = f.read()
        content_parts.append(f'<h2>Run Summary</h2><div class="log">{summary}</div>')

    # Run log
    run_log = os.path.join(run_dir, "run.log")
    if os.path.isfile(run_log):
        with open(run_log, "r") as f:
            log_text = f.read()
        content_parts.append(f'<details><summary>run.log</summary><div class="log">{log_text}</div></details>')

    # PSD comparison across captures
    all_sc16 = sorted(glob.glob(os.path.join(run_dir, "**", "*.sc16"), recursive=True))
    if len(all_sc16) > 1:
        print("  Generating PSD comparison...")
        def label_fn(path):
            return os.path.basename(os.path.dirname(path))
        svg = plot_psd_comparison(all_sc16, sample_rate,
                                  save_path=os.path.join(output_dir, "psd_comparison.svg"),
                                  label_fn=label_fn)
        content_parts.insert(0, '<h2>PSD Comparison</h2>' + svg)

    return "\n".join(content_parts)


def has_capture_data(run_dir):
    """Check if a run directory has sc16 or wav files to process."""
    for pattern in ("**/*.sc16", "**/*.wav"):
        if glob.glob(os.path.join(run_dir, pattern), recursive=True):
            return True
    return False


def get_run_label(run_dir):
    """Extract run label from config.cfg diagnostic override comment, or directory name."""
    config_path = os.path.join(run_dir, "config.cfg")
    if os.path.isfile(config_path):
        try:
            with open(config_path, "r") as f:
                for line in f:
                    if line.startswith("# Diagnostic overrides (run:"):
                        label = line.split("(run:")[1].rstrip(")\n ").strip()
                        if label:
                            return label
        except Exception:
            pass
    return os.path.basename(run_dir)


def build_loopback_report(run_dir, sample_rate, output_dir, input_wav=None,
                          asset_prefix="", no_video=False):
    """Build report for an sdr-loopback run directory."""
    content_parts = []

    sc16_files = sorted(glob.glob(os.path.join(run_dir, "*.sc16")))
    wav_files = sorted(glob.glob(os.path.join(run_dir, "*.wav")))

    # I/Q analysis
    for sc16 in sc16_files:
        print(f"  Processing {sc16}...")
        iq = read_sc16(sc16)
        freqs, psd_db = compute_psd(iq, sample_rate)
        stats = compute_signal_stats(iq, sample_rate, freqs, psd_db)

        snr_badge = badge(f"SNR {stats['snr_estimate_db']:.1f} dB",
                          "pass" if stats['snr_estimate_db'] > 10 else
                          "warn" if stats['snr_estimate_db'] > 3 else "fail")

        content_parts.append(f'<div class="capture"><h3>{os.path.basename(sc16)} {snr_badge}</h3>')
        content_parts.append('<div class="stats">')
        content_parts.append(stat_html("Duration", f"{stats['duration_s']:.2f} s"))
        content_parts.append(stat_html("RMS Level", f"{stats['rms_dbfs']:.1f} dBFS"))
        content_parts.append(stat_html("SNR Estimate", f"{stats['snr_estimate_db']:.1f} dB"))
        content_parts.append(stat_html("Freq Offset", f"{stats['freq_offset_hz']:.0f} Hz"))
        content_parts.append(stat_html("Occupied BW", f"{stats['occupied_bw_hz']:.0f} Hz"))
        content_parts.append(stat_html("DC Offset (I/Q)", f"{stats['dc_offset_i']:.4f} / {stats['dc_offset_q']:.4f}"))
        content_parts.append(stat_html("RMS I / Q", f"{stats['rms_i']:.4f} ({stats['rms_i_dbfs']:.1f} dBFS) / {stats['rms_q']:.4f} ({stats['rms_q_dbfs']:.1f} dBFS)"))
        zero_i_pct = stats['zero_fraction_i'] * 100
        zero_q_pct = stats['zero_fraction_q'] * 100
        zero_level = "fail" if max(zero_i_pct, zero_q_pct) > 10 else "warn" if max(zero_i_pct, zero_q_pct) > 1 else "pass"
        content_parts.append(stat_html("Zero Fraction (I/Q)", f"{zero_i_pct:.2f}% / {zero_q_pct:.2f}% {badge('Q dropout' if zero_q_pct > 10 else 'OK', zero_level)}"))
        content_parts.append(stat_html("Gain Imbalance", f"{stats['gain_imbalance_db']:.2f} dB"))
        content_parts.append(stat_html("Phase Imbalance", f"{stats['phase_imbalance_deg']:.2f}°"))
        content_parts.append('</div>')
        content_parts.append('<div class="plots">')
        content_parts.append(plot_spectrogram(iq, sample_rate, save_path=os.path.join(output_dir, "spectrogram.svg")))
        content_parts.append(plot_psd(freqs, psd_db, stats=stats, save_path=os.path.join(output_dir, "psd.svg")))
        content_parts.append(plot_iq_amplitude(iq, sample_rate, save_path=os.path.join(output_dir, "iq_amplitude.svg")))
        content_parts.append('</div>')
        content_parts.append('<div class="side-by-side">')
        content_parts.append(plot_constellation(iq, save_path=os.path.join(output_dir, "constellation.svg")))
        content_parts.append('</div>')

        # PSD evolution animation (with audio if available)
        if not no_video:
            print(f"  Rendering PSD animation...")
            mp4_path = os.path.join(output_dir, "psd_evolution.mp4")
            wav_for_audio = wav_files[0] if wav_files else None
            if render_animation(iq, sample_rate, mp4_path, audio_path=wav_for_audio):
                video_src = f"{asset_prefix}psd_evolution.mp4"
                content_parts.append(f'<video controls width="100%" style="margin:10px 0">'
                                     f'<source src="{video_src}" type="video/mp4">'
                                     f'PSD evolution animation</video>')

        content_parts.append('</div>')

        with open(os.path.join(output_dir, "iq_stats.json"), "w") as f:
            json.dump(stats, f, indent=2)

    # Audio analysis
    for wav in wav_files:
        print(f"  Processing {wav}...")
        samples, sr = read_wav_mono(wav)
        content_parts.append(f'<div class="capture"><h3>{os.path.basename(wav)}</h3>')
        content_parts.append('<div class="plots">')
        content_parts.append(plot_audio_spectrogram(samples, sr, save_path=os.path.join(output_dir, "audio_spectrogram.svg")))
        content_parts.append(plot_audio_waveform(samples, sr, save_path=os.path.join(output_dir, "audio_waveform.svg")))
        content_parts.append('</div>')

        # Loopback comparison if input WAV provided
        if input_wav and os.path.isfile(input_wav):
            print(f"  Comparing with input {input_wav}...")
            input_samples, input_sr = read_wav_mono(input_wav)
            overlay_svg, corr_svg, peak_val, peak_lag = plot_loopback(
                input_samples, input_sr, samples, sr,
                overlay_save_path=os.path.join(output_dir, "loopback_overlay.svg"),
                corr_save_path=os.path.join(output_dir, "loopback_correlation.svg"))

            corr_level = "pass" if peak_val >= 0.7 else "warn" if peak_val >= 0.3 else "fail"
            corr_text = "PASS" if peak_val >= 0.7 else "WARN" if peak_val >= 0.3 else "FAIL"
            content_parts.append(f'<h4>Loopback Quality {badge(f"{corr_text}: {peak_val:.3f}", corr_level)}</h4>')
            content_parts.append('<div class="plots">')
            content_parts.append(overlay_svg)
            content_parts.append(corr_svg)
            content_parts.append('</div>')

        content_parts.append('</div>')

    # Summary and logs
    summary_path = os.path.join(run_dir, "summary.txt")
    if os.path.isfile(summary_path):
        with open(summary_path, "r") as f:
            content_parts.append(f'<h2>Run Summary</h2><div class="log">{f.read()}</div>')

    run_log = os.path.join(run_dir, "run.log")
    if os.path.isfile(run_log):
        with open(run_log, "r") as f:
            content_parts.append(f'<details><summary>run.log</summary><div class="log">{f.read()}</div></details>')

    return "\n".join(content_parts)


def build_tabbed_capture_report(run_dirs, sample_rate, output_dir, no_video=False):
    """Build tabbed report for multiple capture run directories."""
    # Filter to runs that have data files
    run_dirs = [rd for rd in run_dirs if has_capture_data(rd)]
    if not run_dirs:
        return "<p>No runs with capture data found.</p>"

    parts = ['<div class="tabs">', '<div class="tab-bar">']

    for i, rd in enumerate(run_dirs):
        run_name = os.path.basename(rd)
        label = get_run_label(rd)
        tab_id = f"tab-{run_name}"
        active = " active" if i == 0 else ""
        parts.append(f'<button class="tab-btn{active}" data-tab="{tab_id}">'
                     f'{run_name}<br><small>{label}</small></button>')
    parts.append('</div>')

    for i, rd in enumerate(run_dirs):
        run_name = os.path.basename(rd)
        tab_id = f"tab-{run_name}"
        active = " active" if i == 0 else ""
        run_output = os.path.join(output_dir, run_name)
        os.makedirs(run_output, exist_ok=True)

        print(f"\nProcessing {run_name} ({get_run_label(rd)})...")
        content = build_capture_report(rd, sample_rate, run_output, no_video)
        parts.append(f'<div id="{tab_id}" class="tab-panel{active}">')
        parts.append(content)
        parts.append('</div>')

    parts.append('</div>')
    return "\n".join(parts)


def build_tabbed_loopback_report(run_dirs, sample_rate, output_dir, input_wav=None,
                                 no_video=False):
    """Build tabbed report for multiple loopback run directories."""
    parts = ['<div class="tabs">', '<div class="tab-bar">']

    # Tab buttons
    for i, rd in enumerate(run_dirs):
        run_name = os.path.basename(rd)
        label = get_run_label(rd)
        tab_id = f"tab-{run_name}"
        active = " active" if i == 0 else ""
        parts.append(f'<button class="tab-btn{active}" data-tab="{tab_id}">'
                     f'{run_name}<br><small>{label}</small></button>')
    parts.append('</div>')

    # Tab panels
    for i, rd in enumerate(run_dirs):
        run_name = os.path.basename(rd)
        tab_id = f"tab-{run_name}"
        active = " active" if i == 0 else ""
        run_output = os.path.join(output_dir, run_name)
        os.makedirs(run_output, exist_ok=True)

        print(f"\nProcessing {run_name} ({get_run_label(rd)})...")
        content = build_loopback_report(rd, sample_rate, run_output, input_wav,
                                        asset_prefix=f"{run_name}/",
                                        no_video=no_video)
        parts.append(f'<div id="{tab_id}" class="tab-panel{active}">')
        parts.append(content)
        parts.append('</div>')

    parts.append('</div>')
    return "\n".join(parts)


def main():
    parser = argparse.ArgumentParser(description="Generate HTML report for SDR capture/loopback run")
    parser.add_argument("run_dir", help="Path to run directory or parent of run-* directories")
    parser.add_argument("--output-dir", default=".", help="Output directory for report.html")
    parser.add_argument("--sample-rate", type=int, default=200000,
                        help="I/Q sample rate in Hz (default: 200000 = effective rate after decimation)")
    parser.add_argument("--loopback", action="store_true", help="Treat as loopback run (flat file structure)")
    parser.add_argument("--input-wav", help="Input WAV for loopback comparison")
    parser.add_argument("--no-video", action="store_true", help="Skip PSD evolution video generation")
    args = parser.parse_args()

    if not os.path.isdir(args.run_dir):
        print(f"Error: {args.run_dir} is not a directory", file=sys.stderr)
        return 1

    output_dir = resolve_output_dir(args.output_dir, args.run_dir)
    title = os.path.basename(args.run_dir)
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    print(f"Generating report for {args.run_dir}...")

    if args.loopback:
        # Check for multi-run directory (contains run-* subdirs)
        run_dirs = sorted(p for p in glob.glob(os.path.join(args.run_dir, "run-[0-9]*"))
                          if os.path.isdir(p))
        if run_dirs:
            content = build_tabbed_loopback_report(run_dirs, args.sample_rate,
                                                    output_dir, args.input_wav,
                                                    args.no_video)
        else:
            content = build_loopback_report(args.run_dir, args.sample_rate,
                                            output_dir, args.input_wav,
                                            no_video=args.no_video)
    else:
        # Check for multi-run directory (contains run-* subdirs)
        run_dirs = sorted(p for p in glob.glob(os.path.join(args.run_dir, "run-[0-9]*"))
                          if os.path.isdir(p))
        if run_dirs:
            content = build_tabbed_capture_report(run_dirs, args.sample_rate,
                                                   output_dir, args.no_video)
        else:
            content = build_capture_report(args.run_dir, args.sample_rate,
                                           output_dir, args.no_video)

    html = HTML_TEMPLATE.format(title=title, timestamp=timestamp, content=content)

    output_path = os.path.join(output_dir, "index.html")
    with open(output_path, "w") as f:
        f.write(html)

    print(f"\nReport: {output_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

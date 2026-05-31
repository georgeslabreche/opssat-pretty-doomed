#!/usr/bin/env python3
"""
Animated version of the 3D pointing figure produced by plot_pointing.py.

Shows the spacecraft moving along its trajectory with the antenna boresight
arrow updating at each frame, plus a live "pointing error" readout in the title
that highlights the experiment moment.

By default the script interpolates between UKF samples (which are typically at
10 s cadence) so the animation is smooth at higher frame rates. Spacecraft
position is computed from the TLE at every interpolated timestamp; attitude
quaternions are SLERPed between adjacent UKF samples.

Output format is inferred from the file extension:
  - .gif  -> Pillow GIF writer (no external dependencies)
  - .mp4  -> ffmpeg writer (requires ffmpeg on PATH)

Reuses helper functions from plot_pointing.py.

Example (smooth GIF at 20 fps with 1 s interpolation):
  python3 animate_pointing.py \
      --ukf-csv ../../docs/flight/data/run-02-2026-05-22/ukf-attitude.csv \
      --tle1 "1 58023U 23155H ..." --tle2 "2 58023 97.5692 ..." \
      --target-lat 51.208333 --target-lon 16.160278 --target-name Legnica \
      --exp-time 2026-05-22T21:52:21Z \
      --output animated-pointing.gif --fps 20 --interp-dt 1.0

Example (MP4 at 30 fps with 0.5 s interpolation, rotating view):
  python3 animate_pointing.py ... --output animated-pointing.mp4 \
      --fps 30 --interp-dt 0.5 --rotate

Dependencies: numpy, scipy, sgp4, matplotlib, pillow (GIF), ffmpeg (MP4).
"""
import argparse
import json
import os
import sys
from datetime import datetime, timedelta, timezone

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401
from scipy.spatial.transform import Rotation, Slerp, RotationSpline

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from plot_pointing import (  # noqa: E402
    WGS84_A, lla_to_ecef, sc_ecef, eci_to_ecef,
    angle_between, body_axis_in_eci, load_ukf_csv, sun_unit_eci, is_sunlit,
)


def interpolate_attitude(samples, dt_seconds, mode="cubic"):
    """Sub-sample the UKF series at dt_seconds. Returns a list of
    (datetime, quaternion_xyzw) tuples spanning the input.

    mode = "cubic": uses scipy's RotationSpline, which is C1-continuous in
    angular velocity (smooth across sample boundaries).
    mode = "slerp": linear SLERP between adjacent samples (constant angular
    velocity within each segment, velocity discontinuity at sample boundaries)."""
    if dt_seconds is None or dt_seconds <= 0:
        return samples

    times = [t for t, _ in samples]
    quats = np.array([q for _, q in samples])
    t0 = times[0]
    t_seconds = np.array([(t - t0).total_seconds() for t in times])
    rotations = Rotation.from_quat(quats)

    span = t_seconds[-1] - t_seconds[0]
    n = int(np.ceil(span / dt_seconds)) + 1
    t_new = np.linspace(t_seconds[0], t_seconds[-1], n)

    if mode == "cubic":
        spline = RotationSpline(t_seconds, rotations)
        rot_new = spline(t_new)
    else:
        slerp = Slerp(t_seconds, rotations)
        rot_new = slerp(t_new)

    q_new = rot_new.as_quat()

    interp = []
    for ts, q in zip(t_new, q_new):
        t = t0 + timedelta(seconds=float(ts))
        interp.append((t, tuple(q)))
    return interp


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--ukf-csv", required=True)
    p.add_argument("--tle1", required=True)
    p.add_argument("--tle2", required=True)
    p.add_argument("--target-lat", type=float, required=True)
    p.add_argument("--target-lon", type=float, required=True)
    p.add_argument("--target-name", default="Target")
    p.add_argument("--exp-time", required=True)
    p.add_argument("--output", required=True,
                   help="Output file. Extension picks the writer (.gif or .mp4).")
    p.add_argument("--fps", type=int, default=20, help="Frames per second (default 20)")
    p.add_argument("--interp-dt", type=float, default=1.0,
                   help="Interpolation timestep in seconds between UKF samples. "
                        "Default 1.0 (gives smooth motion at typical fps). Set 0 to disable.")
    p.add_argument("--interp-mode", choices=["cubic", "slerp"], default="cubic",
                   help="Quaternion interpolation: 'cubic' (RotationSpline, smooth "
                        "angular velocity) or 'slerp' (linear, piecewise-constant velocity).")
    p.add_argument("--rotate", action="store_true",
                   help="Slowly rotate the view azimuth across the animation")
    p.add_argument("--dpi", type=int, default=100, help="DPI of output frames (default 100)")
    p.add_argument("--capture-windows", default=None,
                   help="JSON list of [start_offset_s, end_offset_s] capture windows "
                        "relative to exp-time. Default: 6 contiguous 22s windows from -65s.")
    args = p.parse_args()

    exp_time = datetime.fromisoformat(args.exp_time.replace("Z", "+00:00"))
    target_ecef = lla_to_ecef(args.target_lat, args.target_lon, 0)
    target_km = target_ecef / 1000

    if args.capture_windows:
        capture_windows = json.loads(args.capture_windows)
    else:
        capture_windows = [(-65 + 22 * i, -65 + 22 * (i + 1)) for i in range(6)]
    n_caps = len(capture_windows)
    R_earth_km = WGS84_A / 1000.0

    raw_samples = load_ukf_csv(args.ukf_csv)
    samples = interpolate_attitude(raw_samples, args.interp_dt, mode=args.interp_mode)
    print(f"Loaded {len(raw_samples)} UKF samples; interpolated to {len(samples)} frames.")

    # Precompute per-frame data: SC position (km), the three body axes in ECEF,
    # and the +X antenna pointing error.
    times = [t for t, _ in samples]
    traj = np.array([sc_ecef(t, args.tle1, args.tle2) for t in times]) / 1000

    # Body axes to draw: (label, body-frame unit vector, colour, linewidth).
    AXIS_DEFS = [
        ("+X (antenna)", np.array([1.0, 0.0, 0.0]), "#cc0000", 3.2),
        ("+Y",           np.array([0.0, 1.0, 0.0]), "#7030a0", 1.8),
        ("+Z",           np.array([0.0, 0.0, 1.0]), "#1f6fd0", 1.8),
    ]
    axes_ecef = {name: [] for name, _, _, _ in AXIS_DEFS}
    angles_deg = []  # +X antenna pointing error to target
    sunlit = []      # spacecraft sunlit (True) or in Earth's shadow (False)
    for (t, q), r_sc in zip(samples, traj):
        sc_to_t_u = target_km - r_sc
        sc_to_t_u = sc_to_t_u / np.linalg.norm(sc_to_t_u)
        for name, axv, _, _ in AXIS_DEFS:
            axes_ecef[name].append(eci_to_ecef(body_axis_in_eci(q, body_axis=axv), t))
        angles_deg.append(angle_between(axes_ecef["+X (antenna)"][-1], sc_to_t_u))
        sunlit.append(is_sunlit(r_sc, eci_to_ecef(sun_unit_eci(t), t), R_earth_km))
    axes_ecef = {k: np.array(v) for k, v in axes_ecef.items()}
    sunlit = np.array(sunlit)

    # Which capture (1-based) is running at each frame, or 0 if none.
    def _capture_at(t):
        off = (t - exp_time).total_seconds()
        for idx, (s, e) in enumerate(capture_windows, start=1):
            if s <= off < e:
                return idx
        return 0
    cap_idx = [_capture_at(t) for t in times]

    # Set up the figure
    fig = plt.figure(figsize=(10, 7))
    ax = fig.add_subplot(111, projection="3d")

    # Regional meridians and parallels around the target only, kept light to
    # avoid drawing a full sphere wireframe.
    t_lat = np.rad2deg(np.arcsin(target_ecef[2] / np.linalg.norm(target_ecef)))
    t_lon = np.rad2deg(np.arctan2(target_ecef[1], target_ecef[0]))
    for lon in np.arange(t_lon - 35, t_lon + 40, 5):
        lats = np.linspace(t_lat - 18, t_lat + 25, 60)
        pts = np.array([lla_to_ecef(la, lon, 0) / 1000 for la in lats])
        ax.plot(pts[:, 0], pts[:, 1], pts[:, 2],
                color="#4a6c8c", linewidth=0.4, alpha=0.7)
    for lat in np.arange(t_lat - 18, t_lat + 26, 5):
        lons = np.linspace(t_lon - 35, t_lon + 40, 60)
        pts = np.array([lla_to_ecef(lat, lo, 0) / 1000 for lo in lons])
        ax.plot(pts[:, 0], pts[:, 1], pts[:, 2],
                color="#4a6c8c", linewidth=0.4, alpha=0.7)

    # Full trajectory (static), split by sunlit vs Earth-shadow so the eclipse
    # exit shows as a colour change on the orbit (the sun-sensor behaviour Max
    # described happens on eclipse exit).
    traj_lit = np.where(sunlit[:, None], traj, np.nan)
    traj_dark = np.where(~sunlit[:, None], traj, np.nan)
    ax.plot(traj_lit[:, 0], traj_lit[:, 1], traj_lit[:, 2],
            color="#e8a200", linewidth=2.2, alpha=0.9, label="trajectory (sunlit)")
    ax.plot(traj_dark[:, 0], traj_dark[:, 1], traj_dark[:, 2],
            color="#3a3a3a", linewidth=2.2, alpha=0.9,
            label="trajectory (in Earth's shadow)")

    # Target (static)
    ax.scatter(*target_km, color="green", s=120, marker="^",
               zorder=10, edgecolor="black", linewidth=1, label=args.target_name)
    ax.text(target_km[0] + 200, target_km[1] - 200, target_km[2] + 200,
            args.target_name, fontsize=10, color="darkgreen", weight="bold")

    # Set view limits
    pad = 3000
    avg_traj = traj.mean(axis=0)
    cx, cy, cz = (avg_traj + target_km) / 2
    ax.set_xlim(cx - pad, cx + pad)
    ax.set_ylim(cy - pad, cy + pad)
    ax.set_zlim(cz - pad, cz + pad)
    ax.set_xlabel("X (km, ECEF)")
    ax.set_ylabel("Y (km, ECEF)")
    ax.set_zlabel("Z (km, ECEF)")

    # Dynamic artists - placeholders updated each frame
    sc_marker = ax.scatter([0], [0], [0], color="red", s=80, marker="o",
                           zorder=10, edgecolor="black", linewidth=1)
    axis_lines = {}
    for name, axv, col, lw in AXIS_DEFS:
        line, = ax.plot([0, 0], [0, 0], [0, 0], color=col, linewidth=lw, label=name)
        axis_lines[name] = line
    los_line, = ax.plot([0, 0], [0, 0], [0, 0], color="green",
                        linewidth=1.5, linestyle="--",
                        label=f"Line of sight to {args.target_name}")
    fig.suptitle(f"OPS-SAT PRETTY attitude vs {args.target_name}",
                 fontsize=13, weight="bold")
    fig.subplots_adjust(top=0.78)
    clock_text = fig.text(0.5, 0.935, "", ha="center", fontsize=11)
    status_text = fig.text(0.5, 0.892, "", ha="center", fontsize=15, weight="bold")
    target_text = fig.text(0.5, 0.852, "", ha="center", fontsize=12,
                           weight="bold", color="#c00000")
    sun_text = fig.text(0.5, 0.812, "", ha="center", fontsize=11, weight="bold")

    ax.legend(loc="upper right", fontsize=8)

    arrow_len = 1500  # km

    def update(i):
        t = times[i]
        r_sc = traj[i]

        sc_marker._offsets3d = ([r_sc[0]], [r_sc[1]], [r_sc[2]])
        for name, axv, col, lw in AXIS_DEFS:
            p1 = r_sc + arrow_len * axes_ecef[name][i]
            axis_lines[name].set_data_3d([r_sc[0], p1[0]], [r_sc[1], p1[1]],
                                         [r_sc[2], p1[2]])
        los_line.set_data_3d([r_sc[0], target_km[0]],
                             [r_sc[1], target_km[1]],
                             [r_sc[2], target_km[2]])

        clock_text.set_text(f"{t.strftime('%Y-%m-%d %H:%M:%S')} UTC      "
                            f"+X antenna pointing error: {angles_deg[i]:.1f}°")
        is_exp = abs((t - exp_time).total_seconds()) < max(args.interp_dt, 5.0)
        ci = cap_idx[i]
        if ci > 0:
            status_text.set_text(f"● RECORDING  ·  CAPTURE {ci} OF {n_caps}")
            status_text.set_color("white")
            status_text.set_bbox(dict(facecolor="#c00000", edgecolor="none",
                                      boxstyle="round,pad=0.4"))
        else:
            status_text.set_text("no capture running")
            status_text.set_color("#888888")
            status_text.set_bbox(None)
        target_text.set_text("TARGET MOMENT" if is_exp else "")

        if sunlit[i]:
            sun_text.set_text("Spacecraft SUNLIT")
            sun_text.set_color("#d08000")
        else:
            sun_text.set_text("Spacecraft IN EARTH'S SHADOW")
            sun_text.set_color("#1f3f7f")

        if args.rotate:
            ax.view_init(elev=18, azim=-50 + i * (60.0 / len(samples)))
        else:
            ax.view_init(elev=18, azim=-50)

        return tuple(axis_lines.values()) + (sc_marker, los_line, clock_text,
                                             status_text, target_text, sun_text)

    anim = animation.FuncAnimation(
        fig, update, frames=len(samples),
        interval=int(1000 / args.fps), blit=False,
    )

    ext = os.path.splitext(args.output)[1].lower()
    print(f"Writing {args.output} ({len(samples)} frames at {args.fps} fps)...")
    if ext == ".gif":
        anim.save(args.output, writer="pillow", fps=args.fps, dpi=args.dpi)
    elif ext == ".mp4":
        writer = animation.FFMpegWriter(fps=args.fps, codec="libx264",
                                        bitrate=2400,
                                        extra_args=["-pix_fmt", "yuv420p"])
        anim.save(args.output, writer=writer, dpi=args.dpi)
    else:
        raise SystemExit(f"Unsupported output extension: {ext}. Use .gif or .mp4.")
    plt.close()
    print("Done.")


if __name__ == "__main__":
    main()

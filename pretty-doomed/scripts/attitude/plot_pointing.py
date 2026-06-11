#!/usr/bin/env python3
"""
Pointing analysis and visualisation for OPS-SAT PRETTY attitude vs a ground target.

Takes a UKF attitude CSV (quaternions at fixed cadence), an OPS-SAT PRETTY TLE,
a target ground station latitude/longitude, and an experiment timestamp. Produces:

  1. pointing-vs-time.png    Pointing error vs time plot, with ground-station
                              elevation overlay and capture windows shaded.
  2. pointing-3d.png         3D figure of the spacecraft trajectory, the
                              antenna boresight at the experiment time, the
                              ground target, and the line-of-sight to it.

The UKF body axis assumed to be the antenna-facing direction is Body+X, which
the operator (TU Graz) confirmed is the patch-antenna face. If your data uses a
different convention, change BODY_AXIS.

The quaternion convention assumed is body-to-inertial: the quaternion is applied
directly to a body unit vector to obtain its inertial representation. Set
INVERT_QUAT=True if your UKF stores the inertial-to-body rotation instead. For
the OPS-SAT PRETTY 2026-05-22 dataset, body-to-inertial places the +X antenna on
the target at the experiment moment.

Inputs (CLI):
  --ukf-csv PATH            UKF attitude CSV with columns time, ukf_X x, ukf_X y, ukf_X z, ukf_X k
  --tle1 STRING             TLE line 1 (e.g. "1 58023U 23155H ...")
  --tle2 STRING             TLE line 2 (e.g. "2 58023 97.5692 ...")
  --target-lat FLOAT        Target latitude in degrees
  --target-lon FLOAT        Target longitude in degrees
  --target-name STRING      Target display name (default: "Target")
  --exp-time ISO8601        Experiment timestamp (e.g. 2026-05-22T21:52:21Z)
  --output-dir PATH         Directory to write plots (default: cwd)
  --capture-windows JSON    Optional list of [start_offset_s, end_offset_s] relative
                            to experiment time, to shade as capture windows on the
                            time plot. Default: 6 captures of 22s starting -65s.

Example:
  python3 plot_pointing.py \
      --ukf-csv ../../docs/flight/data/run-02-2026-05-22/ukf-attitude.csv \
      --tle1 "1 58023U 23155H   26146.25162962  .00006247  00000+0  26398-3 0  9995" \
      --tle2 "2 58023  97.5692 230.6408 0006006 238.8860 121.1798 15.42269368141976" \
      --target-lat 51.208333 --target-lon 16.160278 --target-name Legnica \
      --exp-time 2026-05-22T21:52:21Z \
      --output-dir slides/2026-05-26/

Dependencies: numpy, scipy, sgp4, matplotlib.
"""
import argparse
import csv
import json
from datetime import datetime, timezone, timedelta

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401 - needed for 3d projection
from scipy.spatial.transform import Rotation
from sgp4.api import Satrec, jday


# WGS84
WGS84_A = 6378137.0
WGS84_E2 = 6.69437999014e-3

# Default body axis the antenna faces (UKF body frame). +X is the patch-antenna
# face, confirmed by the operator (TU Graz) for OPS-SAT PRETTY.
BODY_AXIS = np.array([1.0, 0.0, 0.0])

# Default quaternion interpretation. False if the UKF quaternion is body-to-
# inertial (applied directly to a body vector to get its inertial representation);
# True if it is inertial-to-body (the inverse is applied). For the OPS-SAT PRETTY
# 2026-05-22 dataset, body-to-inertial (False) is what places the +X antenna on
# the target at the experiment moment.
INVERT_QUAT = False


def lla_to_ecef(lat_deg, lon_deg, alt_m=0.0):
    lat = np.deg2rad(lat_deg)
    lon = np.deg2rad(lon_deg)
    N = WGS84_A / np.sqrt(1 - WGS84_E2 * np.sin(lat) ** 2)
    return np.array([
        (N + alt_m) * np.cos(lat) * np.cos(lon),
        (N + alt_m) * np.cos(lat) * np.sin(lon),
        (N * (1 - WGS84_E2) + alt_m) * np.sin(lat),
    ])


def gmst_rad(t_utc):
    """Greenwich Mean Sidereal Time in radians (IAU 1982 / Meeus). The full
    polynomial already uses the complete elapsed time (including the fraction of
    the day), so no separate time-of-day term is added. Sub-arcminute accuracy
    at LEO scales."""
    j2000 = datetime(2000, 1, 1, 12, 0, 0, tzinfo=timezone.utc)
    days = (t_utc - j2000).total_seconds() / 86400.0
    T = days / 36525.0
    gmst_deg = (
        280.46061837
        + 360.98564736629 * days
        + 0.000387933 * T ** 2
        - T ** 3 / 38710000.0
    )
    return np.deg2rad(gmst_deg % 360.0)


def teme_to_ecef(r_teme, t_utc):
    """Rotation from TEME (SGP4 output) to ECEF, ignoring polar motion and
    nutation corrections (sub-km level error at LEO; fine for pointing audits)."""
    theta = gmst_rad(t_utc)
    c, s = np.cos(theta), np.sin(theta)
    return np.array([[c, s, 0], [-s, c, 0], [0, 0, 1]]) @ r_teme


def sc_ecef(t_utc, tle1, tle2):
    sat = Satrec.twoline2rv(tle1, tle2)
    jd, fr = jday(
        t_utc.year, t_utc.month, t_utc.day,
        t_utc.hour, t_utc.minute, t_utc.second + t_utc.microsecond / 1e6,
    )
    e, r_teme_km, _ = sat.sgp4(jd, fr)
    if e != 0:
        raise RuntimeError(f"SGP4 propagation error code {e} at {t_utc.isoformat()}")
    return teme_to_ecef(np.array(r_teme_km) * 1000.0, t_utc)


def angle_between(a, b):
    cos_a = np.clip(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b)), -1, 1)
    return np.rad2deg(np.arccos(cos_a))


def load_ukf_csv(path):
    samples = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            t = datetime.fromisoformat(row["time"].replace("Z", "+00:00"))
            q = (
                float(row["ukf_X x"]),
                float(row["ukf_X y"]),
                float(row["ukf_X z"]),
                float(row["ukf_X k"]),
            )
            samples.append((t, q))
    return samples


def body_axis_in_eci(q_xyzw, body_axis=BODY_AXIS, invert=INVERT_QUAT):
    rot = Rotation.from_quat(q_xyzw)
    if invert:
        rot = rot.inv()
    return rot.apply(body_axis)


def ecef_to_eci(v_ecef, t_utc):
    theta = gmst_rad(t_utc)
    c, s = np.cos(theta), np.sin(theta)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]]) @ v_ecef


def eci_to_ecef(v_eci, t_utc):
    theta = gmst_rad(t_utc)
    c, s = np.cos(theta), np.sin(theta)
    return np.array([[c, s, 0], [-s, c, 0], [0, 0, 1]]) @ v_eci


def pointing_error_deg(t_utc, q_xyzw, r_target_ecef, tle1, tle2):
    """Return (pointing_error_deg, elevation_from_target_deg, slant_range_km)."""
    r_sc = sc_ecef(t_utc, tle1, tle2)
    sc_to_t = r_target_ecef - r_sc
    sc_to_t_u = sc_to_t / np.linalg.norm(sc_to_t)
    sc_to_t_eci = ecef_to_eci(sc_to_t_u, t_utc)
    body_eci = body_axis_in_eci(q_xyzw)
    err_deg = angle_between(body_eci, sc_to_t_eci)
    local_up = r_target_ecef / np.linalg.norm(r_target_ecef)
    elev = np.rad2deg(np.arcsin(np.dot(r_sc - r_target_ecef, local_up) / np.linalg.norm(r_sc - r_target_ecef)))
    slant_km = np.linalg.norm(sc_to_t) / 1000
    return err_deg, elev, slant_km


def sun_unit_eci(t_utc):
    """Low-precision Sun unit vector in an Earth-centred inertial (equatorial)
    frame, good to ~0.01 deg (Astronomical Almanac)."""
    days = (t_utc - datetime(2000, 1, 1, 12, 0, 0, tzinfo=timezone.utc)
            ).total_seconds() / 86400.0
    L = np.deg2rad((280.460 + 0.9856474 * days) % 360.0)
    g = np.deg2rad((357.528 + 0.9856003 * days) % 360.0)
    lam = L + np.deg2rad(1.915) * np.sin(g) + np.deg2rad(0.020) * np.sin(2 * g)
    eps = np.deg2rad(23.439 - 4e-7 * days)
    return np.array([np.cos(lam),
                     np.cos(eps) * np.sin(lam),
                     np.sin(eps) * np.sin(lam)])


def is_sunlit(r_sat_km, sun_hat_ecef, radius_km):
    """Cylindrical-shadow test: eclipsed only if on the anti-Sun side and within
    one Earth radius of the Sun-Earth line."""
    proj = np.dot(r_sat_km, sun_hat_ecef)
    perp = np.linalg.norm(r_sat_km - proj * sun_hat_ecef)
    return not (proj < 0 and perp < radius_km)


def plot_pointing_panels(samples, exp_time, tle1, tle2, target_ecef, target_name,
                         out_path, offsets_s=(-50, 0, 50, 100)):
    """2x2 grid of 3D attitude scenes at four times around the experiment, in the
    same style as animate_pointing: light regional grid, body triad with the +X
    antenna highlighted, sunlit/shadow trajectory, and line of sight to target."""
    times = [t for t, _ in samples]
    traj = np.array([sc_ecef(t, tle1, tle2) for t in times]) / 1000.0
    target_km = target_ecef / 1000.0
    R_earth_km = WGS84_A / 1000.0

    sunlit = np.array([
        is_sunlit(traj[i], eci_to_ecef(sun_unit_eci(times[i]), times[i]), R_earth_km)
        for i in range(len(times))
    ])
    traj_lit = np.where(sunlit[:, None], traj, np.nan)
    traj_dark = np.where(~sunlit[:, None], traj, np.nan)

    axis_defs = [("+X (antenna)", np.array([1., 0, 0]), "#cc0000", 3.0),
                 ("+Y", np.array([0, 1., 0]), "#7030a0", 1.6),
                 ("+Z", np.array([0, 0, 1.]), "#1f6fd0", 1.6)]

    t_lat = np.rad2deg(np.arcsin(target_ecef[2] / np.linalg.norm(target_ecef)))
    t_lon = np.rad2deg(np.arctan2(target_ecef[1], target_ecef[0]))

    fig = plt.figure(figsize=(12, 9))
    pad, arrow_len = 3000, 1300

    for p, off in enumerate(offsets_s):
        target_t = exp_time + timedelta(seconds=off)
        idx = int(np.argmin([abs((t - target_t).total_seconds()) for t in times]))
        r, ti, q = traj[idx], times[idx], samples[idx][1]

        ax = fig.add_subplot(2, 2, p + 1, projection="3d")
        for lon in np.arange(t_lon - 35, t_lon + 40, 5):
            lats = np.linspace(t_lat - 18, t_lat + 25, 60)
            pts = np.array([lla_to_ecef(la, lon, 0) / 1000 for la in lats])
            ax.plot(pts[:, 0], pts[:, 1], pts[:, 2], color="#4a6c8c", linewidth=0.4, alpha=0.7)
        for lat in np.arange(t_lat - 18, t_lat + 26, 5):
            lons = np.linspace(t_lon - 35, t_lon + 40, 60)
            pts = np.array([lla_to_ecef(lat, lo, 0) / 1000 for lo in lons])
            ax.plot(pts[:, 0], pts[:, 1], pts[:, 2], color="#4a6c8c", linewidth=0.4, alpha=0.7)

        ax.plot(traj_lit[:, 0], traj_lit[:, 1], traj_lit[:, 2], color="#e8a200",
                linewidth=2.0, alpha=0.9, label="trajectory (sunlit)")
        ax.plot(traj_dark[:, 0], traj_dark[:, 1], traj_dark[:, 2], color="#3a3a3a",
                linewidth=2.0, alpha=0.9, label="trajectory (in Earth's shadow)")
        ax.scatter(*target_km, color="green", s=70, marker="^", zorder=10,
                   edgecolor="black", linewidth=0.8, label=target_name)
        ax.scatter(*r, color="red", s=40, marker="o", zorder=11,
                   edgecolor="black", linewidth=0.8)

        los_u = target_km - r
        los_u = los_u / np.linalg.norm(los_u)
        err_x = 0.0
        for name, axv, col, lw in axis_defs:
            b = eci_to_ecef(body_axis_in_eci(q, body_axis=axv), ti)
            p1 = r + arrow_len * b
            ax.plot([r[0], p1[0]], [r[1], p1[1]], [r[2], p1[2]],
                    color=col, linewidth=lw, label=name, zorder=12)
            if name.startswith("+X"):
                err_x = angle_between(b, los_u)
        ax.plot([r[0], target_km[0]], [r[1], target_km[1]], [r[2], target_km[2]],
                color="green", linewidth=1.3, linestyle="--",
                label=f"Line of sight to {target_name}")

        cx, cy, cz = (r + target_km) / 2
        ax.set_xlim(cx - pad, cx + pad)
        ax.set_ylim(cy - pad, cy + pad)
        ax.set_zlim(cz - pad, cz + pad)
        ax.set_xticklabels([])
        ax.set_yticklabels([])
        ax.set_zticklabels([])
        ax.tick_params(length=0)
        ax.set_box_aspect((1, 1, 1))
        ax.view_init(elev=18, azim=-50)
        dt = (ti - exp_time).total_seconds()
        ax.set_title(f"{ti.strftime('%H:%M:%S')} UTC ({dt:+.0f} s)\n"
                     f"+X antenna to {target_name}: {err_x:.0f}°", fontsize=10)
        if p == 0:
            ax.legend(loc="upper left", fontsize=7)

    fig.suptitle(f"+X antenna tracking {target_name} across the capture window",
                 fontsize=13, weight="bold")
    fig.subplots_adjust(left=0.02, right=0.98, top=0.91, bottom=0.02,
                        wspace=0.04, hspace=0.16)
    plt.savefig(out_path, dpi=140)
    plt.close()
    print(f"Saved {out_path} (4 panels)")


def plot_pointing_vs_time(samples, exp_time, tle1, tle2, target_ecef, target_name,
                          capture_windows, out_path):
    times = [t for t, _ in samples]
    quats = [q for _, q in samples]
    angles = []
    elevs = []
    for t, q in zip(times, quats):
        err, elev, _ = pointing_error_deg(t, q, target_ecef, tle1, tle2)
        angles.append(err)
        elevs.append(elev)

    t_seconds = [(t - exp_time).total_seconds() for t in times]

    fig, ax1 = plt.subplots(figsize=(8, 4.2))
    ax1.plot(t_seconds, angles, "o-", color="#B22222",
             label=f"+X antenna to {target_name} angle", markersize=4)
    ax1.axvline(0, color="gray", linestyle="--", alpha=0.6,
                label=f"Experiment time {exp_time.strftime('%H:%M:%S')}")
    ax1.set_xlabel(f"Seconds from experiment time ({exp_time.strftime('%H:%M:%S')} UTC)")
    ax1.set_ylabel("Pointing error (deg)", color="#B22222")
    ax1.tick_params(axis="y", labelcolor="#B22222")
    ax1.set_ylim(0, max(40, max(angles) * 1.1))
    ax1.grid(alpha=0.3)

    # Sunlit vs Earth-shadow indicator: shade contiguous in-shadow spans and
    # mark the eclipse-exit transition (relevant to the sun-sensor behaviour).
    R_earth_km = WGS84_A / 1000.0
    sunlit = np.array([
        is_sunlit(sc_ecef(t, tle1, tle2) / 1000.0,
                  eci_to_ecef(sun_unit_eci(t), t), R_earth_km)
        for t in times
    ])
    ts = np.array(t_seconds)
    in_shadow = ~sunlit
    shaded = False
    i = 0
    while i < len(in_shadow):
        if in_shadow[i]:
            j = i
            while j + 1 < len(in_shadow) and in_shadow[j + 1]:
                j += 1
            ax1.axvspan(ts[i], ts[j], color="#5a7da0", alpha=0.18,
                        label="Earth shadow" if not shaded else None)
            shaded = True
            i = j + 1
        else:
            i += 1
    for k in range(1, len(sunlit)):
        if sunlit[k] and not sunlit[k - 1]:
            ax1.axvline(ts[k], color="#3a5a7a", linestyle=":", linewidth=1.2, alpha=0.9)
            ax1.annotate("eclipse exit", xy=(ts[k], 0.5),
                         xycoords=("data", "axes fraction"), rotation=90,
                         va="center", ha="right", fontsize=7, color="#3a5a7a")
            break

    ax2 = ax1.twinx()
    ax2.plot(t_seconds, elevs, "s-", color="steelblue", markersize=3,
             label=f"S/C elevation from {target_name}")
    ax2.set_ylabel(f"Elevation from {target_name} (deg)", color="steelblue")
    ax2.tick_params(axis="y", labelcolor="steelblue")
    ax2.set_ylim(0, max(20, max(elevs) * 1.1))

    # Combined legend for both axes
    h1, l1 = ax1.get_legend_handles_labels()
    h2, l2 = ax2.get_legend_handles_labels()
    ax1.legend(h1 + h2, l1 + l2, loc="upper left", fontsize=8)

    ax1.set_title("Pointing error and ground-station elevation across capture window")

    # Shade each capture window and label with just its index. "Captures" header
    # is placed once above the band of windows.
    for i, (s, e) in enumerate(capture_windows):
        label = "Capture window" if i == 0 else None
        ax1.axvspan(s, e, alpha=0.10, color="green", label=label)
        cx = (s + e) / 2
        ax1.annotate(f"{i+1}", xy=(cx, 0.94), xycoords=("data", "axes fraction"),
                     ha="center", va="top", fontsize=8, color="darkgreen")
    if capture_windows:
        s0 = capture_windows[0][0]
        eN = capture_windows[-1][1]
        ax1.annotate("Captures", xy=((s0 + eN) / 2, 1.0), xycoords=("data", "axes fraction"),
                     ha="center", va="top", fontsize=9, color="darkgreen", weight="bold")

    plt.tight_layout()
    plt.savefig(out_path, dpi=120)
    plt.close()
    print(f"Saved {out_path}")
    return angles


def plot_pointing_3d(samples, exp_time, tle1, tle2, target_ecef, target_name, out_path):
    times = [t for t, _ in samples]
    traj = np.array([sc_ecef(t, tle1, tle2) for t in times]) / 1000  # km
    closest_idx = int(np.argmin([abs((t - exp_time).total_seconds()) for t in times]))
    r_sc = traj[closest_idx]
    q_exp = samples[closest_idx][1]
    t_exp_samp = times[closest_idx]

    body_eci = body_axis_in_eci(q_exp)
    body_ecef = eci_to_ecef(body_eci, t_exp_samp)

    target_km = target_ecef / 1000
    sc_to_t = target_km - r_sc
    sc_to_t_u = sc_to_t / np.linalg.norm(sc_to_t)
    err_deg = angle_between(body_ecef, sc_to_t_u)

    fig = plt.figure(figsize=(10, 7))
    ax = fig.add_subplot(111, projection="3d")

    # Earth wireframe -- darker, more visible
    u, v = np.mgrid[0:2 * np.pi:48j, 0:np.pi:24j]
    R = WGS84_A / 1000
    ex = R * np.cos(u) * np.sin(v)
    ey = R * np.sin(u) * np.sin(v)
    ez = R * np.cos(v)
    ax.plot_wireframe(ex, ey, ez, color="#5a7da0", linewidth=0.6, alpha=0.7)

    # Finer meridians/parallels around the region of interest
    t_lat = np.rad2deg(np.arcsin(target_ecef[2] / np.linalg.norm(target_ecef)))
    t_lon = np.rad2deg(np.arctan2(target_ecef[1], target_ecef[0]))
    for lon in np.arange(t_lon - 40, t_lon + 50, 5):
        lats = np.linspace(t_lat - 20, t_lat + 30, 100)
        pts = np.array([lla_to_ecef(la, lon, 0) / 1000 for la in lats])
        ax.plot(pts[:, 0], pts[:, 1], pts[:, 2], color="#4a6c8c", linewidth=0.5, alpha=0.8)
    for lat in np.arange(t_lat - 20, t_lat + 31, 5):
        lons = np.linspace(t_lon - 40, t_lon + 50, 100)
        pts = np.array([lla_to_ecef(lat, lo, 0) / 1000 for lo in lons])
        ax.plot(pts[:, 0], pts[:, 1], pts[:, 2], color="#4a6c8c", linewidth=0.5, alpha=0.8)
    # Highlight equator and prime meridian
    for lat in [0.0]:
        lons = np.linspace(-180, 180, 200)
        pts = np.array([lla_to_ecef(lat, lo, 0) / 1000 for lo in lons])
        ax.plot(pts[:, 0], pts[:, 1], pts[:, 2], color="#2a4a6c", linewidth=1.0, alpha=0.9)
    for lon in [0.0]:
        lats = np.linspace(-90, 90, 100)
        pts = np.array([lla_to_ecef(la, lon, 0) / 1000 for la in lats])
        ax.plot(pts[:, 0], pts[:, 1], pts[:, 2], color="#2a4a6c", linewidth=1.0, alpha=0.9)

    # Trajectory
    ax.plot(traj[:, 0], traj[:, 1], traj[:, 2], color="#B22222", linewidth=2.5,
            label="S/C trajectory")

    # Body-axis arrows along the trajectory (every 5th sample)
    arrow_len = 1200  # km, for visibility
    for i in range(0, len(samples), 5):
        qi = samples[i][1]
        ti = times[i]
        bi_eci = body_axis_in_eci(qi)
        bi_ecef = eci_to_ecef(bi_eci, ti)
        p0 = traj[i]
        p1 = p0 + arrow_len * bi_ecef
        ax.plot([p0[0], p1[0]], [p0[1], p1[1]], [p0[2], p1[2]],
                color="gray", linewidth=0.8, alpha=0.6)

    # Highlighted arrow at experiment time
    p0 = r_sc
    p1 = p0 + 1800 * body_ecef
    ax.plot([p0[0], p1[0]], [p0[1], p1[1]], [p0[2], p1[2]],
            color="black", linewidth=3,
            label=f"Antenna boresight at {t_exp_samp.strftime('%H:%M:%S')} UTC")

    # Target
    ax.scatter(*target_km, color="green", s=120, marker="^",
               zorder=10, edgecolor="black", linewidth=1)
    ax.text(target_km[0] + 200, target_km[1] - 200, target_km[2] + 200, target_name,
            fontsize=10, color="darkgreen", weight="bold")

    # Spacecraft marker
    ax.scatter(*r_sc, color="red", s=80, marker="o",
               zorder=10, edgecolor="black", linewidth=1)
    ax.text(r_sc[0] + 150, r_sc[1], r_sc[2] + 200, "S/C",
            fontsize=10, color="darkred", weight="bold")

    # Line of sight
    ax.plot([r_sc[0], target_km[0]], [r_sc[1], target_km[1]], [r_sc[2], target_km[2]],
            color="green", linewidth=1.5, linestyle="--",
            label=f"Direction to {target_name}")

    # Zoom on relevant region
    pad = 3000
    xc, yc, zc = (r_sc + target_km) / 2
    ax.set_xlim(xc - pad, xc + pad)
    ax.set_ylim(yc - pad, yc + pad)
    ax.set_zlim(zc - pad, zc + pad)
    ax.set_xlabel("X (km, ECEF)")
    ax.set_ylabel("Y (km, ECEF)")
    ax.set_zlabel("Z (km, ECEF)")
    # Title reports the sample where err_deg is actually computed (closest UKF
    # sample to the scheduled experiment time).
    ax.set_title(f"Attitude over {target_name} region\n"
                 f"Pointing error at {t_exp_samp.strftime('%Y-%m-%d %H:%M:%S')} UTC: "
                 f"{err_deg:.1f}° (+X antenna to target; scheduled exp: "
                 f"{exp_time.strftime('%H:%M:%S')})")
    ax.legend(loc="upper right", fontsize=9)
    ax.view_init(elev=18, azim=-50)

    plt.tight_layout()
    plt.savefig(out_path, dpi=140)
    plt.close()
    print(f"Saved {out_path} (pointing error at experiment time: {err_deg:.2f}°)")
    return err_deg


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--ukf-csv", required=True)
    p.add_argument("--tle1", required=True)
    p.add_argument("--tle2", required=True)
    p.add_argument("--target-lat", type=float, default=None)
    p.add_argument("--target-lon", type=float, default=None)
    p.add_argument("--target-ecef", default=None,
                   help="Target ECEF position as 'x,y,z' in metres. Overrides "
                        "--target-lat / --target-lon when set. Use when the "
                        "operator specifies the target directly in ECEF.")
    p.add_argument("--target-name", default="Target")
    p.add_argument("--exp-time", required=True,
                   help="Experiment timestamp in ISO 8601 (e.g. 2026-05-22T21:52:21Z)")
    p.add_argument("--output-dir", default=".")
    p.add_argument("--capture-windows", default=None,
                   help=("Optional JSON list of [start_offset_s, end_offset_s] "
                         "pairs for capture window shading. Default: 6x22s windows "
                         "starting -65s before exp_time. Read the actual capture "
                         "start times from the run's pretty-doomed.log; they are "
                         "not guaranteed to straddle exp_time."))
    p.add_argument("--panel-offsets", default="-50,0,50,100",
                   help="Comma-separated offsets in seconds from exp_time for the "
                        "four 3D panels (default: -50,0,50,100).")
    args = p.parse_args()

    exp_time = datetime.fromisoformat(args.exp_time.replace("Z", "+00:00"))
    if args.target_ecef:
        target_ecef = np.array([float(v) for v in args.target_ecef.split(",")])
    elif args.target_lat is not None and args.target_lon is not None:
        target_ecef = lla_to_ecef(args.target_lat, args.target_lon, 0)
    else:
        raise SystemExit("Provide either --target-ecef or both --target-lat and --target-lon")

    if args.capture_windows:
        windows = json.loads(args.capture_windows)
    else:
        # Default: 6 captures, each ~22s, starting 65s before exp_time
        windows = [(-65 + 22 * i, -65 + 22 * (i + 1)) for i in range(6)]

    samples = load_ukf_csv(args.ukf_csv)

    angles = plot_pointing_vs_time(
        samples, exp_time, args.tle1, args.tle2, target_ecef, args.target_name,
        windows, f"{args.output_dir}/pointing-vs-time.png",
    )
    plot_pointing_panels(
        samples, exp_time, args.tle1, args.tle2, target_ecef, args.target_name,
        f"{args.output_dir}/pointing-3d.png",
        offsets_s=tuple(float(v) for v in args.panel_offsets.split(",")),
    )

    closest = min(samples, key=lambda s: abs((s[0] - exp_time).total_seconds()))
    err, elev, slant = pointing_error_deg(closest[0], closest[1], target_ecef,
                                          args.tle1, args.tle2)
    print(f"\nSummary at sample closest to {exp_time.isoformat()}:")
    print(f"  Sample time:       {closest[0].isoformat()}")
    print(f"  Pointing error:    {err:.2f} deg")
    print(f"  Elevation from {args.target_name}: {elev:.2f} deg")
    print(f"  Slant range:       {slant:.1f} km")
    print(f"  Min over window:   {min(angles):.2f} deg")


if __name__ == "__main__":
    main()

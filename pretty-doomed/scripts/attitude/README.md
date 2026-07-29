# Attitude / pointing analysis

Scripts to verify spacecraft pointing against a ground target from UKF attitude telemetry, and to produce visualisations for briefings.

Used originally to confirm OPS-SAT PRETTY pointing for the 2026-05-22 voice-uplink attempt over Legnica, Poland.

## Files

| File | Purpose |
|---|---|
| `plot_pointing.py` | Static figures: a four-panel 3D plot showing the body triad over the orbit at four timestamps around the experiment, and a pointing-error / ground-station-elevation vs. time plot with the capture windows, the Earth-shadow region, and the eclipse-exit marker. |
| `animate_pointing.py` | Animated two-panel figure: a 3D scene where the spacecraft moves along its trajectory with the body triad updating each frame (trajectory coloured by sunlit vs. Earth-shadow, capture window shown as a recording banner, experiment moment flagged), and a boresight scope panel that plots the target at a radius equal to the +X pointing error, fading green to amber to red. Outputs GIF or MP4. |
| `README.md` | This file. |
| `requirements.txt` | Python dependencies. |

## Install

```bash
python3 -m pip install -r requirements.txt
```

For MP4 output, `ffmpeg` must be on `PATH`. On macOS install via `brew install ffmpeg`; on Debian/Ubuntu, `apt install ffmpeg`.

## Inputs

All scripts take the same inputs:

- **UKF attitude CSV** via `--ukf-csv`, with columns `time, ukf_X x, ukf_X y, ukf_X z, ukf_X k`. Time is ISO-8601, `Z`-terminated. The quaternion is `(x, y, z, w)` with `k` as the scalar component. Sampling cadence is irrelevant; gaps are interpolated. Sample datasets for the flight runs are committed under `../../docs/flight/data/run-NN-YYYY-MM-DD/ukf-attitude.csv`.
- **TLE** via `--tle1` and `--tle2`, the standard two-line format. Used by SGP4 to compute spacecraft position. Fetch the latest from Celestrak at `https://celestrak.org/NORAD/elements/gp.php?NAME=PRETTY&FORMAT=TLE`.
- **Target position**, either as latitude and longitude via `--target-lat` and `--target-lon` in decimal degrees, or directly as `--target-ecef "x,y,z"` in metres. The ECEF form overrides lat/lon and matches how mission planning specifies the commanded target (Run 3 was specified this way).
- **Experiment timestamp** via `--exp-time`, in ISO-8601, e.g. `2026-05-22T21:52:21Z`. Used to centre time plots and highlight the experiment moment in the animation.
- **Capture windows** via `--capture-windows`, a JSON list of `[start_offset_s, end_offset_s]` pairs relative to `--exp-time`. Defaults to six contiguous 22 s windows starting at -65 s, which matched Run 2; read the actual capture start times from the run's `pretty-doomed.log`, they are not guaranteed to straddle `--exp-time` (Run 3's ran +10 to +141 s).

`plot_pointing.py` additionally takes `--panel-offsets`, comma-separated offsets in seconds from `--exp-time` for the four 3D panels (default `-50,0,50,100`; Run 3 used `10,75,141,301`). Each panel snaps to the nearest telemetry sample.

## Conventions

- The antenna boresight is along **Body+X** in the UKF body frame. This matches the patch-antenna face confirmed by the operator for OPS-SAT PRETTY. The default sits in the `BODY_AXIS` constant in `plot_pointing.py`.
- The quaternion is **body-to-inertial**: applied directly to a body unit vector, it yields that vector in the inertial frame. The default sits in `INVERT_QUAT = False`. Set to `True` if your UKF stores inertial-to-body instead.
- Greenwich Mean Sidereal Time uses the Meeus / IAU 1982 polynomial form with full elapsed days, so no separate time-of-day term is added.
- SGP4 output is in TEME; this is rotated to ECEF by GMST only, ignoring polar motion and nutation. Sub-km accuracy at LEO altitudes, fine for pointing audits.

## Usage

### Static figures

```bash
python3 plot_pointing.py \
  --ukf-csv ../../docs/flight/data/run-02-2026-05-22/ukf-attitude.csv \
  --tle1 "1 58023U 23155H   26146.25162962  .00006247  00000+0  26398-3 0  9995" \
  --tle2 "2 58023  97.5692 227.6492 0003371 102.5844 257.5770 15.23600387145017" \
  --target-lat 51.208333 --target-lon 16.160278 --target-name Legnica \
  --exp-time 2026-05-22T21:52:21Z \
  --output-dir ./out
```

Produces:

- `out/pointing-vs-time.png`: the +X antenna pointing error and the ground-station elevation across the capture window, with the six capture windows shaded green, the Earth-shadow region shaded blue-gray, and a marker at eclipse exit.
- `out/pointing-3d.png`: a 2x2 grid of 3D scenes at four timestamps around the experiment, each showing the body triad with +X highlighted, the line-of-sight to the target, and the sunlit vs. shadow trajectory.

### Animation, smooth MP4

```bash
python3 animate_pointing.py \
  --ukf-csv ../../docs/flight/data/run-02-2026-05-22/ukf-attitude.csv \
  --tle1 "1 58023U 23155H ..." \
  --tle2 "2 58023 97.5692 ..." \
  --target-lat 51.208333 --target-lon 16.160278 --target-name Legnica \
  --exp-time 2026-05-22T21:52:21Z \
  --output out/pointing-animated.mp4 \
  --fps 20 --interp-dt 1.0 --interp-mode cubic
```

### Animation, slimmer GIF

```bash
python3 animate_pointing.py ... \
  --output out/pointing-animated.gif \
  --fps 15 --interp-dt 2.0 --dpi 70
```

## Animation options

| Flag | Default | Effect |
|---|---|---|
| `--fps` | 20 | Frames per second in the output file. Higher = faster playback. |
| `--interp-dt` | 1.0 | Time step in seconds between interpolated frames. Smaller = smoother but more frames and a bigger file. |
| `--interp-mode` | `cubic` | `cubic` uses scipy's `RotationSpline` for C¹-continuous angular velocity, the smoothest option. `slerp` uses piecewise-linear SLERP between samples, with constant velocity per segment and jumps at boundaries. |
| `--rotate` | off | Slowly rotates the view azimuth across the animation. |
| `--trim-to-captures` | off | Trim the animation to the capture span: from the first capture start minus `--trim-pad-s` to the last capture end plus `--trim-pad-s`, instead of the full telemetry span. The `--capture-audio` offsets follow the trimmed start. |
| `--trim-pad-s` | 2.0 | Padding in seconds before the first capture and after the last when `--trim-to-captures` is set. |
| `--realtime` | off | Play back at real wall-clock rate (1 telemetry second = 1 video second) by forcing `--interp-dt` to `1/fps`. Needed for `--capture-audio` to line up with the capture windows. |
| `--capture-audio` | none | Comma-separated WAV paths, one per capture window, muxed into the MP4 as a soundtrack (requires ffmpeg), each placed at its capture-window start. Use with `--realtime` so each capture's audio plays while that capture is happening, silent in the gaps. |
| `--dpi` | 100 | Render DPI. Lower = smaller file but blurrier. |
| `--arrow-len` | 1500 km | Length of the body-axis arrows drawn at the spacecraft. |

The capture windows (see Inputs) drive the recording banner.

### Real-time animation with per-capture audio

```bash
python3 animate_pointing.py \
  --ukf-csv ../../pretty-doomed/docs/flight/data/run-06-2026-07-27/ukf-attitude.csv \
  --tle1 "..." --tle2 "..." \
  --target-ecef "3149143.1,598008.8,5495694.5" --target-name Oslo \
  --exp-time 2026-07-27T10:50:53Z \
  --capture-windows "[[9,31],[35,56],[60,82],[86,114],[119,146],[151,173]]" \
  --trim-to-captures --trim-pad-s 2 --realtime --fps 10 \
  --capture-audio "cap1.wav,cap2.wav,cap3.wav,cap4.wav,cap5.wav,cap6.wav" \
  --output pointing-animated.mp4
```

At `--realtime` the video plays at wall-clock rate, so each capture's recovered audio is heard at the moment that capture occurred. `--trim-to-captures` clips it to the capture span plus `--trim-pad-s` on each side (about 2.8 minutes for the Run 6 example) instead of the full telemetry span. Without `--capture-audio` the output is silent; without `--realtime` the audio placement will not line up.

## Output interpretation

- **Pointing error**: angle in degrees between the +X antenna boresight in the inertial frame and the line-of-sight to the target. A planned target track minimises this at the experiment moment.
- **Elevation**: spacecraft elevation as seen from the target location, in degrees above the local horizon. Low elevations imply long slant range, higher atmospheric losses, and sensitivity to local obstructions.
- **Sunlit vs. Earth-shadow**: computed from a low-precision solar ephemeris and a cylindrical-shadow model. The trajectory is coloured gold where the spacecraft is sunlit and gray where it is in Earth's shadow; the same flag is shown as text on the animation.

## Limitations

- Polar motion, nutation, and Earth orientation parameters are ignored in the TEME→ECEF conversion. Sub-arcsecond effects, negligible at the precision we care about.
- The animation does not visualise the pointing-error curve directly. The +X antenna error is shown as text above the figure each frame, and as the red curve on `pointing-vs-time.png`.
- The body triad and the quaternion handling are tuned to the project's confirmed convention. If you re-use these scripts with a different UKF, set `BODY_AXIS` and `INVERT_QUAT` in `plot_pointing.py` to match.

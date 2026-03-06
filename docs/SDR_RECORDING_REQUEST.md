# Guide: How to Request an In-Orbit SDR Recording

In general, requesting an SDR recording from OPS-SAT PRETTY follows the below steps:

1. Experimenter registers an experiment for the OPS-SAT PRETTY satellite, which may be done from the OPS-SAT web page
2. Experimenter discusses experiment with the OPS-SAT team. Discussion follows topics like relevant targets, background and goal for experiment. This will help the OPS-SAT team support the experiment and help the experimenter reach the experiment goals.
3. Experimenter sends a recording request to the OPS-SAT team, including relevant recording configuration parameters.
4. The OPS-SAT team checks the request, and if valid, the request will be forwarded to the spacecraft operator.
5. The OPS-SAT team shares the downlinked recordings with the experimenter.

## Background Information on the SDR

Please refer to the SDR design page for design information.

## Required Configuration Information for a Recording Request

Below follows an overview of relevant configurations for a recording request. Default values are also stated, where a default recording is set to record in the GPS L5 band.

| Category | Parameter | Type [unit] | Explanation |
|----------|-----------|-------------|-------------|
| General | intent and goal of recording | text | What is the experiment trying to achieve. Helps the OPS-SAT team offer guidance and helps check if the proposed recording will help achieve the goal |
| General | constraints for where to record | text | Country/region/location of interest, multiple possible. Optional: execution time UTC. See: [Recording Where and When](#recording-where-and-when) |
| SDR base settings | center_frequency_Hz | uint [Hz] | Hardware limits (allowed range): 70,000,000 Hz - 6,000,000,000 Hz. Optimized range (recommended, warning if outside): 400,000,000 Hz - 1,800,000,000 Hz. Step size: 1 Hz. Default: 1,176,500,000 Hz |
| SDR base settings | rf_bandwidth_Hz | uint [Hz] | Limits: 200,000 Hz - 56,000,000 Hz. Step size: 1 Hz. Default: 20,000,000 Hz |
| SDR base settings | sampling_frequency_Hz | uint [Hz] | Limits: 2,083,000 Hz - 61,440,000 Hz. Step size: TBD. Default: 12,000,000 Hz |
| SDR channel setting | channel_mode | str | Value: {single, dual}. Default: single. See: [SDR Channel Settings](#sdr-channel-settings) |
| SDR gain control setting | gain_control_mode | str | Value: {fast_attack, slow_attack, manual}. Default: fast_attack. See: [Gain Control Mode](#gain-control-mode) |
| SDR gain control setting | hardware_gain | int [dB] | Limits: -3 dB to 71 dB [1]. Step size: 1 dB |
| SDR gain control setting | manual_gain_comment | text | Specify additional information for hardware_gain field (required if value not given) |
| SDR recording duration | recording_duration | str | Value: {standard, custom}. Default: standard |
| SDR recording duration | recording_duration_sec | float [s] | Default duration: leave field empty, duration decided by standard filesize. Custom duration: required field. See: [Recording Duration and File Size](#recording-duration-and-file-size) |
| Additional information | spacecraft telemetry request | text | Optional field to specify telemetry requests. Contact OPS-SAT team for questions regarding available data. |
| Additional information | spacecraft pointing mode | str | Value: {patch_nadir, target, inertial}. Default: patch_nadir. See: [Spacecraft Pointing](#spacecraft-pointing) |
| Additional information | spacecraft pointing request | text | Optional field for adding pointing information (e.g. lat/lon coordinates in target mode) |
| Additional information | other requests | text | Any other requests for the requested recording |

[1] Limits: 70 MHz - < 1.3 GHz: (-1) dB - 73 dB | 1.3 GHz - < 4 GHz: (-3) dB - 71 dB | 4 GHz - 6 GHz: (-10) - 62 dB

**Note:** If AGC is enabled, the hardware_gain cannot be set on the SDR.

## Recording Where and When

Recording requirements may describe one or multiple areas/locations of interest, or be given via the signal requirement. ESA and TUG follow the concept that execution times will be determined by the satellite operator based on the given requirements. It is only for specific cases that a fixed execution time can be accepted, where this execution time is given in UTC.

Requirement examples:
- Record ADSB over Frankfurt a.M. airport
- Record ADSB over any international airport in Europe

Considerations for execution time:
1. It is preferred by the operators that the experimenter provides a request without a specific time constraint
2. If a specific time constraint is needed:
   - The requested time must be sufficiently in the future to have time to schedule the recording
   - Execution at the requested time cannot be guaranteed, as operational constraints have priority
   - At least 3-4 potential execution times have to be provided, where one of these may be selected for experiment execution

## SDR Channel Settings

The OPS-SAT PRETTY satellite SDR has two patch antennas, each of which is connected to one RX channel of the SDR. For single channel, only channel 1 is recorded. For dual channel, channel 1 and 2 are recorded. In dual channel mode, the parallel channels may not be assigned different settings.

## Gain Control Mode

The OPS-SAT PRETTY satellite SDR supports self-calibration and automatic gain control (AGC) systems to maintain high performance across varying temperatures and input signal conditions. It offers three main gain control modes, one of which must be selected for the request:

- **fast_attack**: Fast attack AGC for rapidly changing signals (default). The system automatically adjusts the analog gain to optimize signal levels for the ADC, preventing saturation and maximizing dynamic range (same for slow_attack).
- **slow_attack**: Slow attack AGC for slowly varying signals. The system automatically adjusts the analog gain to optimize signal levels for the ADC, preventing saturation and maximizing dynamic range (same for fast_attack).
- **manual**: Manual mode for fixed, user-defined gain.
  - Gain value (dB) has to be explicitly set, or the experimenter states information in the text field (e.g. expected signal strength) to allow ESA to determine gain
  - Value defines the amount of analog amplification applied to the incoming signal before it reaches the ADC
  - Represents how much the hardware amplifies or attenuates the input, not expected signal level
- **(hybrid)**: Hybrid mode TBD

## Recording Duration and File Size

Recording durations are in practice limited by the downlink speeds from the satellite to the ground station. To reduce the amount of data to be downlinked, the default limit on recordings is 25 MB. For experimenters who require longer recordings, we offer the following:
- A long recording is made on the satellite
- Only the data representing the first 25 MB is downlinked and provided to the experimenter
- The experimenter judges if the recording is worth downlinking based on the snippet and informs ESA
- It is now possible to:
  - Downlink the remaining parts of the recording
  - Delete the recording and re-run the recording for a new sample
  - Delete the recording and request new settings for a new sample

For the SDR recording settings, it is possible to select the following recording durations: {standard, custom}. If **standard** is selected, the recording will be limited by the default recording size limit of 25 MB. For **custom** recording durations, the recording duration in seconds is required (floating point value).

The recorded file size is given by:

```
filesize_B = num_channels * bytes_per_IQ_pair * duration_sec * sampling_frequency_Hz
```

- `filesize_B`: File size in Bytes
- `num_channels`: 1 for single and 2 for dual
- `bytes_per_IQ_pair`: 4 Bytes. Gives Bytes used for one RX channel sample (2 Byte signed I value, 2 Byte signed Q value)
- `duration_sec`: recording duration in seconds
- `sampling_frequency_Hz`: sampling frequency in Hz

## Spacecraft Pointing

Specific spacecraft pointing may be requested for a recording request. The pointing request text field is to supply additional information to the specific pointing mode, or to specify any other requests or information.

Spacecraft pointing modes:
1. **patch_nadir**: the spacecraft points the patch antennas to nadir (default)
2. **target**: the spacecraft points at a specific target. Latitude and longitude values are required in the text field.
   - `target_pointing_lat_degree`: float [deg]
   - `target_pointing_lon_degree`: float [deg]
3. **other**: if any other pointing modes are required, these should be specified and discussed with the team.

## Results and File Format

All recording request files are shared with the experimenter after execution and downlink. Shared files may include SDR IQ files `sdr_*.cs16` and auxiliary files with telemetry, metadata, etc.

The SDR binary stream is composed of 16 Bit / 2 Byte little-endian interleaved signed integers (two's complement). The interleaving is applied first to the channels, and then for each channel to the I and Q samples which results in the following stream structure:

```
Single channel: RX0_I_t1, RX0_Q_t1, RX0_I_t2, RX0_Q_t2, RX0_I_t3, RX0_Q_t3, ...
Dual channel : RX0_I_t1, RX0_Q_t1, RX1_I_t1, RX1_Q_t1, RX0_I_t2, RX0_Q_t2, RX1_I_t2, RX1_Q_t2, ...
```

- `RX<x>`: Receiver. May be RX0 for receiver 1 and RX1 for receiver 2 (only in dual channel mode)
- `I/Q`: In-phase or Quadrature sample. One I or Q sample is signed 16 Bit / 2 Byte.
- `t<x>`: timestamp `<x>`

### SDR Sample File Naming Convention

Example filename: `sdr_20250606_104739_1090000000_38400000_12.cs16`

| Part | Explanation | From example filename |
|------|-------------|----------------------|
| 1 | SDR recording | sdr |
| 2 | Date in YYYYMMDD | 20250825 |
| 3 | UTC time in HHmmss | 175239 |
| 4 | Center frequency of recording (Hz) | 1090000000 |
| 5 | Sample frequency of recording (Hz) | 38400000 |
| 6 | Channels used: 1=single channel, 12=dual channel | 12 |
| 7 | Binary file type | cs16 |

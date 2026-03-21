# Signal Processor Results

## Status

Validated on EM and spacecraft. Confirmed GNU Radio DSP filters run correctly on ARM32. Superseded by the [`pretty-doomed`](../../../pretty-doomed/) integrated pipeline.

## Purpose

Early experiment to validate that GNU Radio compiles, links, and executes correctly on the OPS-SAT PRETTY SEPP (Alpine Linux, ARM32, musl). Tests lowpass filter, bandpass filter, and power squelch on a WAV file.

## EM/FM Testing Summary

- **v1** (Jan 2026): Initial deployment. Failed due to missing shared libraries (expected since system-wide GNU Radio was not installed).
- **v2** (Jan 2026): Bundled shared libraries with the package. Successfully executed on both EM and FM. GNU Radio pipeline processed audio correctly: lowpass at 3400 Hz, bandpass 300-3400 Hz, power squelch.

## Outcome

GNU Radio is viable on the SEPP. The DSP filter chain (lowpass, bandpass) and the library bundling approach were carried forward into sdr-capture and eventually into pretty-doomed.

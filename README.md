# PRETTY DOOMed

First voice command sent to a spacecraft — playing DOOM from orbit via amateur radio.

## What

A radio amateur transmits a voice command to ESA's OPS-SAT PRETTY spacecraft on 1296 MHz. The onboard SDR captures the RF signal, FM demodulates, filters, transcribes speech, detects the command, and launches a DOOM demo playback. Frame captures, level stats, I/Q diagnostics, and transcription are downlinked.

```
RF (1296 MHz) --> SDR Capture --> Narrow --> FM Demod --> Filter --> Resample --> STT --> Match --> DOOM --> Downlink
```

## Flight results

**This is the first time a satellite has been commanded by voice.** On 2026-07-27 a spoken command transmitted from the ground was received on board OPS-SAT PRETTY, recognized by the onboard speech-to-text, and acted on, launching DOOM. It is a significant operational first: not a pre-scripted trigger but a voice command sent over amateur radio, detected and executed as a flight operation autonomously in orbit.

It happened on two passes with a radio-amateur team in Oslo transmitting:

- **Run 6** (morning, TTS keyer): three of six captures detected the command and launched DOOM. [Debriefing](pretty-doomed/docs/flight/debriefings/run-06-2026-07-27/).
- **Run 7** (evening, live human voice over a microphone, no keyer): the detection reproduced on an independent pass. [Debriefing](pretty-doomed/docs/flight/debriefings/run-07-2026-07-27/).

These followed the [Run 5 RF-link test](pretty-doomed/docs/flight/debriefings/run-05-2026-07-03/), which recorded the uplink as raw I/Q and drove the v7 narrowing stage that recovers the voice from the noise floor. Full flight record and per-run analysis: [`pretty-doomed/docs/flight/`](pretty-doomed/docs/flight/).

## Repository

| Directory | Description |
|-----------|-------------|
| [`pretty-doomed/`](pretty-doomed/) | Voice-command-to-DOOM pipeline (C++17, GNU Radio, Sherpa-ONNX) |
| [`doom/`](doom/) | Headless DOOM engine with JPEG/GIF frame capture |
| [`common/`](common/) | Shared C++ headers (logging, config, IIO, audio, spectrogram, constellation) |
| [`tools/`](tools/) | Ground-side visualization: HTML reports, spectrograms, I/Q analysis |
| [`sandbox/`](sandbox/) | Experiments: SDR capture/loopback, signal processing, denoising, STT evaluation |
| [`docs/`](docs/) | Project proposal and reference material |

## Quick Start

```bash
cd pretty-doomed

# Build
docker-compose build
docker-compose run --rm pretty-doomed make all doom

# Download models (see models/README.md)
# ...

# Run (file input + SDR captures)
docker-compose run --rm pretty-doomed ./run

# Test
docker-compose run --rm pretty-doomed make test
```

See [`pretty-doomed/README.md`](pretty-doomed/README.md) for full build, run, and deployment docs.

## Voice Command Format

```
<CALL_SIGN>, PRETTY PRETTY, PLEASE PLAY DOOM DOOM DOOM.
```

Transmissions open with the operator's call sign (amateur radio identification rules); detection is order-insensitive and one surviving DOOM is enough. See [`pretty-doomed/README.md`](pretty-doomed/README.md#voice-command-format).

## Output

Each run produces a numbered directory downlinked to ground with transcription, detection scores, captured audio, and DOOM artifacts (if command detected). SDR capture runs also include I/Q data, spectrograms, and constellation plots per capture. See [`pretty-doomed/docs/DESIGN.md`](pretty-doomed/docs/DESIGN.md) for the full output structure.

## Sandbox

Earlier experiments that informed the final pipeline design:

- **Denoising** — [`sandbox/denoising/`](sandbox/denoising/) — DTLN, spectral subtraction, adaptive filtering, Wiener, ALE
- **STT evaluation** — [`sandbox/speech-to-text/`](sandbox/speech-to-text/) — Vosk, Sherpa-ONNX, PocketSphinx comparison
- **Signal processing** — [`sandbox/gnuradio/`](sandbox/gnuradio/) — GNU Radio lowpass/bandpass/squelch for ARM32
- **SDR apps** — [`sandbox/gnuradio/sdr-capture/`](sandbox/gnuradio/sdr-capture/) and [`sdr-loopback/`](sandbox/gnuradio/sdr-loopback/) — standalone SDR experiments (validated on EM, loopback shelved due to DMA contention)
- **Integration** — [`sandbox/integrations/`](sandbox/integrations/) — GNU Radio + Whisper end-to-end prototype

**Finding:** Classical and neural denoising methods all struggle with the non-stationary radio interference in OPS-SAT samples. The pipeline instead relies on bandpass filtering + a robust STT model with fuzzy matching.

## Documentation

Start at the [documentation map](docs/README.md), which indexes everything and explains the monorepo layout. Direct links to the most-visited docs: [flight results](pretty-doomed/docs/flight/), [building](pretty-doomed/docs/BUILDING.md), [design](pretty-doomed/docs/DESIGN.md), [testing](pretty-doomed/docs/TESTING.md), [config](pretty-doomed/docs/CONFIG.md).

## References

- [OPS-SAT](https://opssat.esa.int/)
- [Sherpa-ONNX](https://github.com/k2-fsa/sherpa-onnx)
- [GNU Radio](https://wiki.gnuradio.org/)
- [Project Proposal](docs/PROPOSAL.md)
- [SEPP Reference](docs/SEPP.md)

## Acknowledgements

[Georges Labrèche](https://github.com/georgeslabreche) is the experiment's principal investigator and project lead. Ólafur Waage co-designed and co-developed the experiment. Vladimir Zelenevskiy, Maximilian Henkel, and the entire OPS-SAT mission operation teams at ESA/ESOC and TU Graz planned and executed spacecraft operations that made the experiment a success.

This experiment depends on the amateur radio community, who make up its ground segment. Any licensed radio amateur with a capable station can transmit a recognized command and elicit a downlinked artifact.

### Oslo Group of the Norwegian Radio Relay League (NRRL)

Call sign **[LA4O](https://www.qrz.com/db/LA4O)**, founded 1923 (Oslogruppen av NRRL), a radio amateur club in Oslo, Norway. The group ran the July 2026 campaign and sent the voice commands that launched DOOM on orbit, the first voice commanding of a satellite: [Run 6](pretty-doomed/docs/flight/debriefings/run-06-2026-07-27/) (TTS keyer) and [Run 7](pretty-doomed/docs/flight/debriefings/run-07-2026-07-27/) (live human voice). The club board approved the group's participation and made the station, the premises, and the roof available.

- **[LB6AJ, Eskil Hadland](https://www.qrz.com/db/LB6AJ)** — antenna and power amplifier procurement, amplifier integration and bias-control concept; the 3 July rooftop transmissions; I/Q and DSP analysis and the channel-narrowing proposal; speech-model and voice screening; keyer preparation and operating instructions; remote support from Steigen during both 27 July passes. Principal author of the campaign report.
- **[LB9BJ, Jon Bergli Heier](https://www.qrz.com/db/LB9BJ)** — pass predictions and Look4Sat tracking; recorded the 3 July operator voice keyer and operated the station; took over the technical lead in July; final-pass operation and the ground report to collaborators; transmitting operator for the live-microphone evening pass of 27 July; author of the photographic and operational record of both 27 July passes.
- **[LB5SK, Magne Helander](https://www.qrz.com/db/LB5SK)** — provided the IC-9700; planning; ground-station setup and operation on both the 3 July and 27 July passes; established SD-card voice-keyer playback and operated the keyer during the final campaign.
- **[LA7WRA, Peter Petrov](https://www.qrz.com/db/LA7WRA)** — early spacecraft research, antenna and link planning, and the azimuth and elevation plan; ground-station setup and operation; supplied the antenna tripod; 3 July field operation.
- **[LA7IJ, Truls Johansen](https://www.qrz.com/db/LA7IJ)** — ground-station preparation and operation on both the 3 July and 27 July passes; RF power-meter procurement; audio, video, and photographic documentation of the transmissions.
- **[LB0PI, Jan Olav Aasterud](https://www.qrz.com/db/LB0PI)** — discussions and planning; ground-station setup; the 3 July rooftop campaign and the 27 July evening pass.
- **[LA4CIA, Lewi](https://www.qrz.com/db/LA4CIA)** — project group and technical coordination from 17 June.
- **[LB2KK, Per Thomas Jahr](https://www.qrz.com/db/LB2KK)** — project group and technical coordination from 4 June.

### Legnica, Poland

The radio amateur operators who conducted the first four campaign runs from Legnica, Poland (Runs 1 to 4, April to June 2026), transmitting on 23 cm. The June runs used upgraded 100 W directional equipment.

- **[SQ6RDP, Wojciech Siłko](https://www.qrz.com/db/SQ6RDP)**
- **[SQ6QV, Tomasz Salwach](https://www.qrz.com/db/SQ6QV)**

Marcin Jasiukowicz was the team's contact person for the collaboration.

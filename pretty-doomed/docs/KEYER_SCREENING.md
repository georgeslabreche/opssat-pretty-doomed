# FM channel simulator: keyer and human-voice screening through the flight binary

## Background

The keyer is the radio team's transmitter-side rig: it keys up the transmitter and plays a pre-recorded voice message on loop for the duration of a pass. The message follows the [Voice Command Format](../README.md#voice-command-format): the club call sign, then the repeated wake word and command, e.g. "LIMA ALFA FOUR OSCAR, PRETTY PRETTY, PLEASE PLAY DOOM DOOM DOOM".

The candidate messages are synthesized with [Piper](https://github.com/rhasspy/piper), an open-source neural text-to-speech engine, so the exact transmission is reproducible on both ends. Piper voice names identify the public dataset each model was trained on (`<lang>_<REGION>-<name>-<quality>`): `lessac` is the Blizzard 2013 audiobook corpus (single professional narrator), `arctic` is CMU ARCTIC (phonetically balanced single-speaker corpus), `l2arctic` is L2-ARCTIC (deliberately non-native, accented English, which is consistent with it being the weakest voice in every screening), and `norman` is a contributed single-speaker dataset. Models are published at [rhasspy/piper-voices](https://huggingface.co/rhasspy/piper-voices). The radio team screened the full Piper English catalog against the same [sherpa-onnx model that is flown](../models/README.md) and shortlisted these four; this document screens that shortlist through the full flight pipeline over a controlled radio link.

## Method

Tool: `tools/src/make_fm_iq.py`. Clean voice WAV, FM-modulated onto 200 kHz flight-form baseband (deviation 5 kHz, carrier at -8 kHz), AWGN to the target CNR (defined in the 20 kHz narrowing bandwidth), replayed through the flight binary with `pretty-doomed -r`: narrowing, peak search, FM discriminator, STT, and the real matcher decide (v7 pipeline, `doom_force_trigger=false`). Inputs regenerable by `--seed`.

## Measured vs assumed

What the simulation takes from the [Run 5 flight captures](flight/debriefings/run-05-2026-07-03/), and what it assumes:

**Measured:**

1. The carrier offset (-8 kHz at zenith) and Doppler behavior.
2. The CNR scale anchor (below).
3. The character of the noise floor itself: the off-carrier floor of the flight-form replay is flat (5th-95th percentile spread 3.7 dB, band tilt -0.1 dB across 200 kHz), which is what supports using white noise at all.

**Assumed:**

1. The FM deviation (default 5 kHz). This is the flight demodulator's configured `sdr_fm_deviation`, and it is consistent with the received signal fitting the validated +/-10 kHz narrowing bandwidth, but the transmitter's actual setting has not been confirmed; the transmitted source recording and deviation setting have been requested for a precise calibration.
2. The noise is synthetic white Gaussian by default. The real capture additionally contains narrowband features that AWGN does not reproduce: a cluster near +25 kHz at roughly 27 dB over the floor, and the AD9361 DC spike. These sit outside the +/-10 kHz narrowing window and do not affect voice demodulation, but during carrier-off periods the peak search can lock onto such features (observed on emulator replays of keying pauses). `--noise-file` substitutes noise cut from a real capture when that matters.
3. No fading or scintillation (the Run 5 spectrum shows keying and Doppler, little else).
4. Linear Doppler drift.
5. Peak-amplitude normalization maps the recording level to full deviation, so recordings with transient peaks get under-deviated FM, which biases the human-recording results pessimistic.

## Calibration anchor

The simulator's peak-over-floor (`analyze_carrier`, 8192-pt FFT at 200 kHz) tracks CNR linearly at +21.8 dB offset. The archived Run 5 flight-form replay (`docs/changelog/data/em-v7/replay.cs16`) measures 28.6 dB on the same metric, placing the Run 5 link conditions at roughly **CNR 7** on this scale. Injected carriers are found by the flight binary's own peak search within one FFT bin of -8 kHz.

## Keyer voices: detection rate (5 noise seeds per point)

| Voice | CNR 6 | CNR 9 | CNR 12 | CNR 15 | Clean |
|---|---|---|---|---|---|
| arctic | 1/5 | 5/5 | 5/5 | 5/5 | yes |
| norman | 1/5 | 4/5 | 4/5 | 5/5 | yes |
| lessac | 0/5 | 1/5 | 4/5 | 3/5 | yes |
| l2arctic | 0/5 | 1/5 | 0/5 | 3/5 | yes |

Detection floors: roughly CNR 9 (arctic, norman), CNR 12 (lessac), above 15 (l2arctic). Run 5 conditions (~CNR 7) sit below every floor: the planned transmitter-side improvements (amplifier at spec, narrower filter, pointing) need to buy about 5 dB for reliable detection. The arctic/norman ranking agrees with the radio team's bare-model screening; the earlier full-pipeline ranking that demoted norman was distorted by the since-removed second-stage filtering (#120).

### Single-realization transcripts (seed 4023)

- `arctic_clean` **DETECTED**: `LIMA ALVE AFORE OSCAR PRETTY PRETTY PLEASE PLAY DOOM DOOM DOOM`
- `arctic_cnr12` miss: `LENA OWL THE FOUR OSCA PRETTY PRETTY PLEASE PLAY DOONES IN`
- `arctic_cnr15` **DETECTED**: `LENA OWL BEFORE OSCA PRETTY PRETTY PLEASE PLAY DOOM DOOM DO`
- `arctic_cnr3` miss: `AND WE WERE BEFORE OFFICE OF THE SAME`
- `arctic_cnr6` miss: `SLEMA HOW THE FOUR OSCA FOR THESE PUDDING PLACE SLAVE IN SEASON`
- `arctic_cnr9` **DETECTED**: `LENA OWL BEFORE OSCA FOR THE PRETTY PLEASED PLAY DUN ZOO`
- `l2arctic_clean` **DETECTED**: `WE MAY HAVE A FORE US FOR PRETTY PRETTY PLACE THEY DOOM DOON DOOM`
- `l2arctic_cnr12` miss: `WE MAY HAVE A FOROSS OF TREATED READY PLACES I DUNGUINE`
- `l2arctic_cnr15` **DETECTED**: `WE MAY HAVE A PHILOSER PRETTY PRETTY PLACE WHERE DOOM LUNGDON`
- `l2arctic_cnr3` miss: `AND ALL THE FORESTS OF THOSE WHOM`
- `l2arctic_cnr6` miss: `HE LED OUT BEFORE US OF THE STUDY PLACES IZZIES`
- `l2arctic_cnr9` miss: `WE MIGHT HAVE A FOROSS OF FREE TO READY PLACES I DUNG UNDUNE`
- `lessac_clean` **DETECTED**: `LEMA ELVA FOSKER PRETTY PRETTY PLEASE PLAY DOOM DOOMED DOOM`
- `lessac_cnr12` **DETECTED**: `LENA ELVA FOR US FOR PRETTY PRETTY PLEASE PLAY DOOMED INDEED`
- `lessac_cnr15` **DETECTED**: `LEMA ELVA FOR US FOR PRETTY PRETTY PLEASE PLAY DOOM DOONE DOONES`
- `lessac_cnr3` miss: `YOU KNOW BEFORE YOU`
- `lessac_cnr6` miss: `LENA ELD BEFORE OUR PRETTY CITY'S LEAVES LAY THROUGH THESE`
- `lessac_cnr9` miss: `LENA ELVA FOR US SO PRETTY PRETTY PLEASE SLAVE IN THESE`
- `norman_clean` **DETECTED**: `LIMA ALFAR FORE OSCAR PRETTY PRETTY PLEASE PLAY DOOM DOOM DOOM`
- `norman_cnr12` **DETECTED**: `LEMA ALVA FOUR OFF SO PRETTY PRETTY PLEASE PLAY DOON DO`
- `norman_cnr15` **DETECTED**: `LEMA ALVA FOUR OSCO PRETTY PRETTY PLEASE PLAY DOOM DOO DO`
- `norman_cnr3` miss: `AND THAT HOWEVER`
- `norman_cnr6` miss: `LEMA ALBAF WORE OFF THE PRETTY PRINTITIES WAITING VIZI`
- `norman_cnr9` miss: `LEMA ALVA FOUR OFF THE PRETTY PRETTY SEASON ZOOZOO`

### Multi-seed transcripts

- `arctic_c12_s1` **DETECTED**: `LENA OWL THE FLOOR OSCA PRETTY PRETTY PLEASE PLAY DUNE ZUMVIN`
- `arctic_c12_s2` **DETECTED**: `SLEMA OWL BEFORE OSCA PRETTY PRETTY PLEASE PLAY DOON DOOM`
- `arctic_c12_s3` **DETECTED**: `SLEEMA OWL BEFORE OSCA PRETTY PRETTY PLEASED PLAY DUNE SOON DO`
- `arctic_c12_s4` **DETECTED**: `LEMA OWL BEFORE OSCA PRETTY PRETTY PLEASED PLAY DOON DO`
- `arctic_c12_s5` **DETECTED**: `LEMA OWL BEFORE OSCA FOR THESE PRETTY PLEASE PLAY DUNE SOON DOOM`
- `arctic_c15_s1` **DETECTED**: `LENA OWL BEFORE OSCAR PRETTY PRETTY PLEASE PLAY DOON DO`
- `arctic_c15_s2` **DETECTED**: `SLEMA OWL FOR OSCAR PRETTY PRETTY PLEASE PLAY DOON DOOM DOONE`
- `arctic_c15_s3` **DETECTED**: `SLEEMA OWL BEFORE OSCA PRETTY PRETTY PLEASE PLAY DOONE DOOM`
- `arctic_c15_s4` **DETECTED**: `LEMA OWL BEFORE OSCAR PRETTY PRETTY PLEASE PLAY DOON DO`
- `arctic_c15_s5` **DETECTED**: `LEMA ALVA FOUR OSCA PRETTY PRETTY PLEASE PLAY DUNE SOON DOOM`
- `arctic_c6_s1` miss: `LENA HOW THE FLOOR OFF SUCH PRETTY PLEASED WAYS IN ZOOZY`
- `arctic_c6_s2` miss: `BLEMA HOW THE FOUR OFFICE OF READY PRETTY POLICE PLAY IN JULY`
- `arctic_c6_s3` **DETECTED**: `LE MAU BEFORE ALSO THESE PRETTY PLACED PLAY DOONE DO`
- `arctic_c6_s4` miss: `LEAVE MY HOUSE BEFORE OFFICE FOR THESE PRETTY PLEASED SLAVES IN SOON BE`
- `arctic_c6_s5` miss: `SLEEP OUR HOUR BEFORE OFFICE READY FORTY FLEET SLAVES IN`
- `arctic_c9_s1` **DETECTED**: `LENA OWL BEFORE OSCA PRETTY PRETTY PLEASE PLAY DUNE ZOO`
- `arctic_c9_s2` **DETECTED**: `SLEEMA OWL BEFORE OSCA PRETTY PRETTY PLEASED SLAY YOU DO`
- `arctic_c9_s3` **DETECTED**: `SLEEMA OWL BEFORE OSCA FOR THESE PRETTY PLEASED PLAY DOOMED SOON DO`
- `arctic_c9_s4` **DETECTED**: `LEMA OWL BEFORE OSCA FOR THESE PRETTY PLEASED PLAY DOONE SOON DO`
- `arctic_c9_s5` **DETECTED**: `LEMA ALVIFORE OSCA FOR THESE PRETTY PLEASE PLAY DOON DOOM`
- `l2arctic_c12_s1` miss: `WE MAY HAVE A FOREST OF PRETTY THREADY PLACES SAYS IN DUNGHIM`
- `l2arctic_c12_s2` miss: `YOU MAY HAVE A FOROSS OF PRETTY THREADY PLACE FORE DUNGUE`
- `l2arctic_c12_s3` miss: `WE MAY HAVE A FOROSS OF PRETTY PRETTY PLACE SLEIGH UNDUNGUE`
- `l2arctic_c12_s4` miss: `WE MIGHT HAVE A FOROSS OF PRETTY THREADY PLACES SAY DUNDUNE`
- `l2arctic_c12_s5` miss: `WE MAY HAVE A PHILOSS OR PRETTY THREADY PLACES I DUNGHIEN`
- `l2arctic_c15_s1` miss: `WE MAY HAVE A FOREST OR PRETTY THREADY PLACES SAY DUMDOUN`
- `l2arctic_c15_s2` **DETECTED**: `YOU MAY HAVE A PAROSS OF PRETTY FRETTY PLACE WHERE DOOM DOOM`
- `l2arctic_c15_s3` miss: `WE MAY HAVE A FOROSS OF PRETTY PRETTY PLACES SAY DUNDUNE`
- `l2arctic_c15_s4` **DETECTED**: `WE MAY HAVE A FOROSS OF PRETTY PRETTY PLACE ZE DOOM DOOM`
- `l2arctic_c15_s5` **DETECTED**: `WE MAY HAVE A FEROCER PRETTY PRETTY PLACES SAY YOU DO`
- `l2arctic_c6_s1` miss: `YOU MAY HAVE A PHILOSOPHERTY THIRTY THREE`
- `l2arctic_c6_s2` miss: `YOU MAY HAVE A FELLOW FERRY PLACE IN GENIUS`
- `l2arctic_c6_s3` miss: `YOU MAY HAVE A FEROCER SECRETLY SAYS`
- `l2arctic_c6_s4` miss: `WE MAY HAVE A PHILOSOPH OF HIS PRETTY PLACE IN THIS WAY`
- `l2arctic_c6_s5` miss: `YOU MAY HAVE A PHILOSOPHERS OF THE CITY`
- `l2arctic_c9_s1` miss: `WE MIGHT HAVE A FOREST OF PRETTY THREADY PLACES AS YOU DOING`
- `l2arctic_c9_s2` miss: `YOU MAY HAVE A FOROSS OF PRETTY FREE PLACE FOR GINGDOM`
- `l2arctic_c9_s3` miss: `WE MIGHT HAVE A FOREST OF PRETTY PURTY PLACES LIKE YOONGOONDUNE`
- `l2arctic_c9_s4` miss: `WE MIGHT HAVE A FOREST OF PRETTY STUDY PLACES SAYS IN DOING THEM`
- `l2arctic_c9_s5` **DETECTED**: `WE MIGHT HAVE A PHILOSS OF PRETTY THREADY PLACE FOR I DOOMED IN`
- `lessac_c12_s1` **DETECTED**: `LEMA ELVA FOR US FOR PRETTY PRETTY PLEASE PLAYTHEW DOOMED THESE`
- `lessac_c12_s2` **DETECTED**: `LIMA ELVA FOR US FOR PRETTY PRETTY FLEECE PLAY DO YOU`
- `lessac_c12_s3` **DETECTED**: `LEMA ELVA FOR US FOR PRETTY PRETTY POLICE PLAY YOU DO`
- `lessac_c12_s4` **DETECTED**: `LENA ELVA FOR US FOR PRETTY PRETTY FLEECE PLAY DO YOU BE`
- `lessac_c12_s5` miss: `LEMA ELVA FOR US FOR PRETTY PRETTY PLEASE PLAY LU`
- `lessac_c15_s1` **DETECTED**: `LEMA ELVA FOR US FOR PRETTY PRETTY PLEASE PLAY DOOMED YOU DO`
- `lessac_c15_s2` miss: `LEMA ELVA FOR US FOR PRETTY PRETTY FLEECE PLAY LITTLE DOONES`
- `lessac_c15_s3` miss: `LEMA ELVA FOR US FOR PRETTY PRETTY POLICE PLAY DOONES`
- `lessac_c15_s4` **DETECTED**: `LEMA ELVA FOR US FOR PRETTY PRETTY FLEECE PLAY DO YOU DO`
- `lessac_c15_s5` **DETECTED**: `LEMA ELVA FOR US FOR PRETTY PRETTY PLEASE PLAY DOONE DOOM`
- `lessac_c6_s1` miss: `WE KNOW BEFORE OURSELF IN PRETTY SLAVES INDIANS IS`
- `lessac_c6_s2` miss: `LENA ALL THE THOROPHYS AND CITIES IN THE SPRING SCENES`
- `lessac_c6_s3` miss: `WHOM THEY ELSE A PHILOSOP AND SIMPSY FLAMES YOU`
- `lessac_c6_s4` miss: `LENA ELVA FOR US THE CITY SLAVE YOUNG GEE`
- `lessac_c6_s5` miss: `WE KNOW BEFORE YOU`
- `lessac_c9_s1` miss: `LENA ELVA FOR US SO PRETTY PRETTY PLEASE PLAY HOME DOING THESE`
- `lessac_c9_s2` miss: `LEMA ELVA FOR US HER PRETTY PRETTY FLEECE LAY THESE`
- `lessac_c9_s3` **DETECTED**: `LEMA ELVA FOR US HER PRETTY PRETTY POLICE SLAY YOU DO`
- `lessac_c9_s4` miss: `LENA ELVA FOR US SO PRETTY PRETTY FLEE SLAVE YOU`
- `lessac_c9_s5` miss: `LENA ELVA FLORA FOR PRETTY PRETTY PLEASE SLAVE YOU DEAR`
- `norman_c12_s1` **DETECTED**: `LEMA ALBER FOR PRETTY PRETTY PLEASED PLAY DOOM DO THEE`
- `norman_c12_s2` **DETECTED**: `LEMA ALVA FOUR OFF THE PRETTY PRETTY PLEASE PLAY DOOM DOZEN`
- `norman_c12_s3` **DETECTED**: `LEMA ALVA FOUR OFFER PRETTY PRETTY PLEASE PLAY DOOM DOOM`
- `norman_c12_s4` miss: `LEMA ALVA FOUR OSTER PRETTY PRETTY PLEASE PLAY DOOMSON`
- `norman_c12_s5` **DETECTED**: `LEMA ALVA FOUR OFF SO PRETTY PRETTY PLEASED TO PLAY DOOM DOOM DOES`
- `norman_c15_s1` **DETECTED**: `LEMA ALVA FOUR OSCAL PRETTY PRETTY PLEAS PLAY DOOM DO`
- `norman_c15_s2` **DETECTED**: `WE MAY ALBER FOR US THE PRETTY PRETTY PLEASE PLAY DOOM DOO DO`
- `norman_c15_s3` **DETECTED**: `LEMA ALFAR FOR PRETTY PRETTY PLEASE PLAY DOOM DOO DO YOU`
- `norman_c15_s4` **DETECTED**: `LEMA ALVA FOUR OSTER PRETTY PRETTY PLEASE PLAY DOOM DOO DO`
- `norman_c15_s5` **DETECTED**: `LEMA ALVA FOUR OFF SO PRETTY PRETTY PLEASE PLAY DOOM DOO DO YOU`
- `norman_c6_s1` **DETECTED**: `WE MAY PERFORE AUGHT FOR PRETTY PRETTY FEET FLAGOON DO`
- `norman_c6_s2` miss: `WE MAY THEREFORE OFF THE PRETTY CITY IN DUTY`
- `norman_c6_s3` miss: `LEMA ALVA BORE OFF THE PRETTY PRETTY SEASONSIE`
- `norman_c6_s4` miss: `LENA ALBER FOUR OFF THE PRETTY PRETTY THIEFLY IN BEING TREES`
- `norman_c6_s5` miss: `LENA ALVA FOUR OFF THE PRETTY PRETTY THIEVES PLAYED IN DREAMS`
- `norman_c9_s1` **DETECTED**: `LEMA ALVA FOUR OF PRETTY PRETTY PLEASED TO PLAY DOOM DOZEN`
- `norman_c9_s2` miss: `LE MALVA FOUR OFF THE PRETTY PRETTY SEASON DUTY`
- `norman_c9_s3` **DETECTED**: `LEMA ALVA FOUR OFF THE PRETTY PRETTY FEET PLAY DUN ZOOZI`
- `norman_c9_s4` **DETECTED**: `LEMA ALVA FOUR OFF THE PRETTY PRETTY PLEASED PLAY DOONE DOOM`
- `norman_c9_s5` **DETECTED**: `LEMA ALVA FOUR OXEL PRETTY PRETTY PLEASED PLAY DOON DOOM`

## Human sample recordings

The clean sample recordings (`pretty-doomed/input/samples/`): georges 01-02, oli 01-04, vlad 01-06. oli 05-10 are excluded (already noised at the source). These speak the old single-shot phrase (PRETTY, THIS IS X, PLAY DOOM), one DOOM per recording.

| Recording | Clean | CNR 12 | CNR 9 |
|---|---|---|---|
| georges_01 | yes | - | - |
| georges_02 | yes | - | - |
| oli_01 | - | - | - |
| oli_02 | - | - | - |
| oli_03 | - | yes | - |
| oli_04 | yes | - | - |
| vlad_01 | - | - | - |
| vlad_02 | yes | yes | - |
| vlad_03 | yes | - | yes |
| vlad_04 | yes | - | - |
| vlad_05 | yes | - | - |
| vlad_06 | yes | yes | - |
| **total** | **8/12** | **3/12** | **1/12** |

Far below the keyer voices, and the transcripts show the mechanism: one spoken DOOM means one garbled word ends the attempt, while the keyer's repeated wake word and command give the matcher several chances per transmission. This is the empirical case for the repeated-command message design. Caveat: the tool normalizes by peak amplitude, so recordings with clicks get under-deviated FM; absolute human-voice numbers are pessimistic.

### Transcripts

- `georges_01` (clean) **DETECTED**: `PRETTY THIS IS GEORGE PLAY DOOM PLAY DOOM PLAY DOOM PRETTY THIS IS GEORGE PLAY DOOM PLAY DOOM PLAY DOOM`
- `georges_01` (cnr12) miss: `FRENCHES MISSUS GEORGE CLAYBEAN BLAZING SLAVEY CRICKS WITH HIS GORGE CLAYBEAM CLAY DIN PARASIANS`
- `georges_01` (cnr9) miss: `A FRIEND WITH JOY CLAVING BLAZING CLAZING PRISON WITH A SWORD CLAVIE CLOTHING PEREMS`
- `georges_02` (clean) **DETECTED**: `PRETTY THIS IS PAPA PLAY DOOM PRETTY THIS IS ROMEO PLAY DOOM PRETTY THIS IS SIERRA PLAY DOOM PRETTY THIS IS TANGO PLAY DOOM PRETTY THIS IS WHISKY PLAY DOOM PRETTY THIS IS YANKEE PLAY DOOM PRETTY THIS IS ZULU PLAY DOOM`
- `georges_02` (cnr12) miss: `PRETTY SISTERS PAPA PLACID PRETTY MISSUS ROMILE CLAYPOON PRETTYCHIELLA PLAIN PRISSY MISSUS TANGLE PLAYBEAM PRETTY DISGUSTICE PLATIN PRETTY MISSUS JERSEY PLAYBEAM PRISTIE MISSUS LUDY PLAYBEAM`
- `georges_02` (cnr9) miss: `PRIS CITIZENS CITY OF SOPHIAN PRETTY WITH ASTRIA CLAVING CITIES LITTLE TANGLE PLAGUE PITY DISAGUSTUS CLOTHING PRETTY WITH ANXIETY PLAGUE PRISON WITH A ZOODY CLOTHING`
- `oli_01` (clean) miss: `PRETTY THIS IS ONLY PLAY TOM`
- `oli_01` (cnr12) miss: `THINKING IT IS ONLY LABEL`
- `oli_01` (cnr9) miss: `IS AN ONLY PLACES`
- `oli_02` (clean) miss: `PRETTY THIS IS ONLY PLAY TOO`
- `oli_02` (cnr12) miss: `CITIES OF HOLY SLAVES`
- `oli_02` (cnr9) miss: `SAYS IT IS ONLY SLAVES`
- `oli_03` (clean) miss: `PRITI THIS IS ONLY A PLAY TOM`
- `oli_03` (cnr12) **DETECTED**: `THEY SEE THIS ONLY PLAY DO`
- `oli_03` (cnr9) miss: `THIS IS ONLY PAY TO`
- `oli_04` (clean) **DETECTED**: `PRETTY THIS IS ONLY PRAY DOOM`
- `oli_04` (cnr12) miss: `SAYS HE THAT IS ONLY FREEDOM`
- `oli_04` (cnr9) miss: `SAYS HE THAT IS ONLY THREATEN`
- `vlad_01` (clean) miss: `PRETTY THIS IS BLOOD'S PLAY TO`
- `vlad_01` (cnr12) miss: `SAYS YOU MISSUS BLOOD'S LATER`
- `vlad_01` (cnr9) miss: `SEE THIS IS LOG SLAVES`
- `vlad_02` (clean) **DETECTED**: `PRETTILYSES BLOOD PLAY DOOM PLAY DOOM PLAY DOME`
- `vlad_02` (cnr12) **DETECTED**: `RIDICULOUS OF BLOOD PLAYBUME DOOM SLAYDOM`
- `vlad_02` (cnr9) miss: `THE RIDICULOUS OF BLOOD FLAGOONS LABUM SLAYDOM`
- `vlad_03` (clean) **DETECTED**: `PRETTY THIS BLOOD PLAY DOME PLAY DOME PLAY DOME`
- `vlad_03` (cnr12) miss: `THREE TO THIS IS LOVE SLAYDOM LAYDOM SLAYDOM`
- `vlad_03` (cnr9) **DETECTED**: `THREE CURIOUS AS LOVE SLAYDOM LAYDOM SLEE DOOM`
- `vlad_04` (clean) **DETECTED**: `PRETTY THIS IS ONE TWO THREE PLAY DOOM PRETTY THIS IS ALPHA BRAVO CHARLIE PLAY DOOM`
- `vlad_04` (cnr12) miss: `PRECIOUSNESS IS ONE THINGS BLAZING FACES AS IF I FAS BRAVO SARRIED SLAVES`
- `vlad_04` (cnr9) miss: `THREE PIECES ONCE IN PLACES SPACES OF CYPHOS BRAVO SARRIED SLAVES`
- `vlad_05` (clean) **DETECTED**: `PRETTY THIS IS E HO SIERRA OSCAR CHARLI BLAY DOOM`
- `vlad_05` (cnr12) miss: `THREE DAYS THIS IS HERO SIERRA ORCHALISLAY DUMAS`
- `vlad_05` (cnr9) miss: `THESE YEARS FOR SIERRA OR SCARCE ARIS LAY DUMAS`
- `vlad_06` (clean) **DETECTED**: `PRETTIETH AS BLOOD PLAY DOME`
- `vlad_06` (cnr12) **DETECTED**: `THE VICTUOUS AS BLOOD SLAY DOME`
- `vlad_06` (cnr9) miss: `OF ITS BLOOD'S LAYS`

## Reproduce

The four shortlisted keyer voice WAVs are committed in `pretty-doomed/input/keyer/` (`reference_doom__en_US-{arctic,norman,lessac,l2arctic}-medium.wav`).

```bash
cd tools/src
python3 make_fm_iq.py ../../pretty-doomed/input/keyer/reference_doom__en_US-arctic-medium.wav \
    --output ../../pretty-doomed/input/out.sc16 --cnr-db 12 --seed <n>
docker compose run --rm pretty-doomed ./build/local/pretty-doomed \
    -r input/out.sc16 -c config.cfg -f variants.cfg -o toGround/x -d demos -e /bin/true
```

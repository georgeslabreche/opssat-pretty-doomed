# PRETTY DOOMED

> **Note:** This is a historical document from the early project phase. The final implementation diverges from this proposal in several areas. See [PROPOSAL_DIVERGENCES.md](PROPOSAL_DIVERGENCES.md) for a summary of what changed and why.

## Overview

* **Goal:** First voice command sent to a spacecraft.  
* **Spacecraft:** ESA PRETTY \[1\].  
* **Required Payloads:** SDR and SEPP.  
* **Key Software Dependencies:** GNU Radio, Tensorflow Lite.

## How It’ll Work

1. Radio amateur sends voice telecommand (TC) *“PRETTY, Play DOOM.”*  
2. SDR captures the voice TC signal.  
3. Voice TC is processed by GNU Radio and saved as an audio file.  
4. The audio file is inputted into an audio denoiser model and then the output is saved in a denoised audio file.  
5. The denoised audio file is processed to recognize whether or not the *“PRETTY, Play DOOM”* command was issued. The method used to accomplish this transcription is TBD.  
6. If the *“PRETTY, Play DOOM”* command is detected then play one of the preloaded DOOM demo files.  
7. Downlink all audio and DOOM related artifacts.  
8. Repeat the experiment but with a more significant TC, e.g. *“PRETTY, Restart SEPP”* or *“PRETTY, Enter Safe Mode.”*

## Proof of Concept

1. [This radio amateur broadcast](../samples/SDRSharp_20240110_180622Z_73841Hz_AF.wav) (\~30s, 4.6 MB) was captured onboard OPS-SAT-1 and [denoised](../samples/SDRSharp_20240110_180622Z_73841Hz_AF_DENOISED.wav) on the ground with an online denoiser \[3\].  
2. Tensorflow Lite models can be trained to denoise audio files \[4\].  
3. GNU Radio and Tensorflow Lite successfully ran on OPS-SAT-1.

## Open Questions

1. What does PRETTY’s SEPP environment look like? Can we install and run all the required dependencies, i.e. GNU Radio and Tensorflow Lite?  
2. Can steps 2–5 be processed as a stream instead of the i/o steps of writing and reading a file in between steps?

## Next Steps

1. Experiment with pretrained models in \[4\] to denoiser [The radio amateur broadcast](https://drive.google.com/file/d/1Az7KVefi5BUen4lWc9arIUYixhlocESn/view?usp=drive_link) (\~30s, 4.6 MB) that was captured onboard OPS-SAT-1.  
2. Experiment with automatic speech recognition (ASR) on the [sample denoised](../samples/SDRSharp_20240110_180622Z_73841Hz_AF_DENOISED.wav) audio file. The following can run on small devices like Raspberry Pi:  
   1. Whisper: [GitHub \- ggml-org/whisper.cpp: Port of OpenAI's Whisper model in C/C++](https://github.com/ggml-org/whisper.cpp)   
   2. Vosk API: [GitHub \- alphacep/vosk-api: Offline speech recognition API for Android, iOS, Raspberry Pi and servers with Python, Java, C\# and Node](https://github.com/alphacep/vosk-api)  
3. Analyze ground track and reach out to radio amateur groups accordingly.

## References

\[1\] ESA PRETTY Mission [https://www.esa.int/Enabling\_Support/Space\_Engineering\_Technology/Technology\_CubeSats/PRETTY](https://www.esa.int/Enabling_Support/Space_Engineering_Technology/Technology_CubeSats/PRETTY)

\[2\] ESA OPS-SAT [https://www.esa.int/Enabling\_Support/Operations/OPS-SAT](https://www.esa.int/Enabling_Support/Operations/OPS-SAT) 

\[3\] Noise Reducer. Remove Background Noise From Audio – A Perfect Noise Reducer & Audio Enhancer [https://noise-reducer.com/](https://noise-reducer.com/)

\[4\] Dual-signal Transformation LSTM Network, [https://github.com/breizhn/DTLN](https://github.com/breizhn/DTLN)


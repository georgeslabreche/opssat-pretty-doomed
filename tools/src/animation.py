"""Shared animation functions for PSD evolution videos."""
import glob
import os
import subprocess
import tempfile

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.patches import Rectangle
from matplotlib.ticker import FuncFormatter
import numpy as np


def find_wav_sibling(sc16_path):
    """Find a WAV file in the same directory as the sc16 file."""
    parent = os.path.dirname(sc16_path) or "."
    wavs = sorted(glob.glob(os.path.join(parent, "*.wav")))
    return wavs[0] if wavs else None


def mux_audio(video_path, audio_path, output_path):
    """Mux audio into video with ffmpeg. Returns True on success."""
    cmd = [
        "ffmpeg", "-y",
        "-i", video_path,
        "-i", audio_path,
        "-c:v", "copy",
        "-c:a", "aac", "-b:a", "128k",
        "-shortest",
        "-movflags", "+faststart",
        output_path,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.returncode == 0


def render_animation(iq, sample_rate, save_path, audio_path=None, fft_size=4096):
    """Render PSD evolution MP4 with spectrogram reveal + animated PSD.

    If audio_path is provided, muxes the audio into the final video.
    Returns the save_path on success, None if file too short.
    """
    target_frames = 500
    segment_samples = max(fft_size, len(iq) // target_frames)
    num_segments = len(iq) // segment_samples

    if num_segments < 2:
        return None

    duration = len(iq) / sample_rate
    fps = num_segments / duration

    # Precompute PSD frames
    window = np.hanning(fft_size)
    freqs = np.linspace(-sample_rate / 2, sample_rate / 2, fft_size)
    freqs_khz = freqs / 1e3

    psd_frames = []
    for i in range(num_segments):
        seg = iq[i * segment_samples : (i + 1) * segment_samples]
        n_sub = max(1, len(seg) // fft_size)
        truncated = seg[: n_sub * fft_size].reshape(n_sub, fft_size)
        spectra = np.fft.fftshift(np.fft.fft(truncated * window, axis=1), axes=1)
        psd_db = 10 * np.log10(np.mean(np.abs(spectra) ** 2, axis=0) + 1e-20)
        psd_frames.append(psd_db)

    psd_all = np.array(psd_frames)
    psd_min = np.min(psd_all) - 3
    psd_max = np.max(psd_all) + 3

    # Build figure
    fig, (ax_spec, ax_psd) = plt.subplots(2, 1, figsize=(10, 7),
                                           gridspec_kw={"height_ratios": [1, 1.2]})

    # Spectrogram with Fs=sample_rate so time axis is in seconds
    Pxx, _, _, im = ax_spec.specgram(iq, NFFT=1024, Fs=sample_rate, noverlap=512,
                                     cmap="viridis", scale="dB", Fc=0)
    spec_db = 10 * np.log10(Pxx + 1e-20)
    im.set_clim(vmin=np.percentile(spec_db, 2), vmax=np.percentile(spec_db, 99.5))
    # Relabel frequency axis to kHz
    ax_spec.yaxis.set_major_formatter(FuncFormatter(lambda x, _: f"{x / 1e3:.0f}"))
    ax_spec.set_ylabel("Frequency (kHz)")
    ax_spec.set_title("PSD Evolution")
    ax_spec.set_xlim(0, duration)

    # White overlay covering the "future" — coordinates in seconds
    ylim = ax_spec.get_ylim()
    overlay = ax_spec.add_patch(
        Rectangle((0, ylim[0]), duration, ylim[1] - ylim[0],
                  facecolor="white", alpha=0.75, zorder=5))
    cursor = ax_spec.axvline(0, color="red", linewidth=2, alpha=0.9, zorder=6)

    # PSD plot
    line, = ax_psd.plot(freqs_khz, psd_frames[0], linewidth=0.8, color="steelblue")
    ax_psd.set_xlim(freqs_khz[0], freqs_khz[-1])
    ax_psd.set_ylim(psd_min, psd_max)
    ax_psd.set_xlabel("Frequency (kHz)")
    ax_psd.set_ylabel("Power (dBFS)")
    ax_psd.grid(True, alpha=0.3)
    time_text = ax_psd.text(0.02, 0.95, "", transform=ax_psd.transAxes,
                            fontsize=10, verticalalignment="top",
                            bbox=dict(boxstyle="round,pad=0.3", facecolor="white", alpha=0.8))
    fig.tight_layout()

    def update(frame):
        t_sec = (frame + 0.5) * segment_samples / sample_rate
        overlay.set_x(t_sec)
        overlay.set_width(max(0, duration - t_sec))
        cursor.set_xdata([t_sec, t_sec])
        line.set_ydata(psd_frames[frame])
        time_text.set_text(f"t = {t_sec:.3f} s")

    anim = animation.FuncAnimation(fig, update, frames=num_segments,
                                   interval=1000 / fps, blit=False)
    writer = animation.FFMpegWriter(fps=fps, bitrate=2000,
                                    extra_args=["-pix_fmt", "yuv420p"])

    if audio_path and os.path.isfile(audio_path):
        tmp_fd, tmp_video = tempfile.mkstemp(suffix=".mp4")
        os.close(tmp_fd)
        try:
            anim.save(tmp_video, writer=writer)
            plt.close(fig)
            if mux_audio(tmp_video, audio_path, save_path):
                return save_path
            else:
                os.rename(tmp_video, save_path)
                tmp_video = None
                return save_path
        finally:
            if tmp_video and os.path.exists(tmp_video):
                os.unlink(tmp_video)
    else:
        anim.save(save_path, writer=writer)
        plt.close(fig)
        return save_path

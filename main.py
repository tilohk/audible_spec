"""
Image-to-Sound Synthesizer
==========================

Converts a grayscale image into a manipulable spectrogram and reconstructs
audio from that spectrogram using Griffin-Lim.

Pipeline:
    image -> processing -> logarithmic frequency mapping -> magnitude spectrogram
          -> Griffin-Lim -> WAV

Usage:
    python main.py

Edit the parameters in the CONFIG section below.
"""

from pathlib import Path

import numpy as np
from PIL import Image, ImageOps, ImageEnhance, ImageFilter
import librosa
import soundfile as sf
import matplotlib.pyplot as plt
from scipy.ndimage import gaussian_filter
from pillow_heif import register_heif_opener
from matplotlib.ticker import FixedLocator, FuncFormatter


register_heif_opener()


# ============================================================
# CONFIG
# ============================================================

# -----------------------------
# File Settings
# -----------------------------

IMAGE_PATH = Path("data/images/IMG_2198.HEIC")
OUTPUT_NAME = "testneu1"
OUTPUT_DIR = Path("data/output")

OUTPUT_WAV = OUTPUT_DIR / f"{OUTPUT_NAME}.wav"
OUTPUT_SPECTROGRAM = OUTPUT_DIR / f"{OUTPUT_NAME}_spec.png"


# -----------------------------
# Image Settings
# -----------------------------

# Number of time slices in the generated spectrogram.
# Higher = potentially longer/more detailed sound.
WIDTH = 1024

# Image height is derived from N_FFT below.
# Keeping this derived avoids a mismatch between image rows
# and STFT frequency bins.

# Gamma:
# < 1.0 = emphasizes darker image areas
# 1.0    = neutral
# > 1.0 = emphasizes brighter image areas
GAMMA = 1.0

# Optional Gaussian blur in image pixels.
# 0.0 = disabled.
BLUR = 0.0

# Contrast multiplier.
# 1.0 = unchanged
# > 1.0 = stronger contrast
CONTRAST = 1.5

# Invert black/white mapping.
INVERT = True

# Optional horizontal flip.
FLIP_HORIZONTAL = False

# Optional vertical flip.
# Normally we flip vertically because image coordinates start at the top.
FLIP_VERTICAL = True


# -----------------------------
# Frequency Settings
# -----------------------------

SAMPLE_RATE = 44100

N_FFT = 4096

FMIN = 80.0
FMAX = 8000.0

# "log" is recommended for musical use.
# "linear" is available for comparison.
FREQUENCY_SCALE = "log"


# -----------------------------
# Time Settings
# -----------------------------

# If WIDTH is fixed, this controls the relation between
# image columns and STFT frames through HOP_LENGTH.
HOP_LENGTH = 32

# The actual output duration is approximately:
# WIDTH * HOP_LENGTH / SAMPLE_RATE
#
# With WIDTH=1024, HOP_LENGTH=256, SAMPLE_RATE=44100:
# approximately 5.95 seconds.


# -----------------------------
# Dynamic / Amplitude Settings
# -----------------------------

# Image brightness is mapped to this dynamic range.
#
# 40 dB = more dense / less contrast
# 60 dB = good starting point
# 80 dB = more detailed / quieter regions preserved
DYNAMIC = 50.0

# Final audio peak.
OUTPUT_GAIN = 0.95


# -----------------------------
# Reconstruction Settings
# -----------------------------

GRIFFIN_LIM_ITERATIONS = 128

RANDOM_SEED = 35

# Griffin-Lim momentum.
GRIFFIN_LIM_MOMENTUM = 0.8


# -----------------------------
# Spectrogram Processing
# -----------------------------

# Additional smoothness directly on the magnitude spectrogram.
# 0.0 = disabled.
SPECTRUM_BLUR_TIME = 0.0
SPECTRUM_BLUR_FREQ = 0.0


# -----------------------------
# Visualization
# -----------------------------

SAVE_VISUALIZATION = True

FIGURE_DPI = 150


# ============================================================
# HELPERS
# ============================================================

def validate_config():
    """Validate parameters before doing expensive processing."""

    if not IMAGE_PATH.exists():
        raise FileNotFoundError(
            f"Image not found: {IMAGE_PATH.resolve()}"
        )

    if SAMPLE_RATE <= 0:
        raise ValueError("SAMPLE_RATE must be > 0.")

    if N_FFT <= 0:
        raise ValueError("N_FFT must be > 0.")

    if HOP_LENGTH <= 0:
        raise ValueError("HOP_LENGTH must be > 0.")

    if WIDTH <= 0:
        raise ValueError("WIDTH must be > 0.")

    if FMIN <= 0:
        raise ValueError("FMIN must be > 0.")

    nyquist = SAMPLE_RATE / 2.0

    if FMAX > nyquist:
        raise ValueError(
            f"FMAX ({FMAX}) cannot exceed Nyquist frequency "
            f"({nyquist})."
        )

    if FMIN >= FMAX:
        raise ValueError("FMIN must be smaller than FMAX.")

    if GAMMA <= 0:
        raise ValueError("GAMMA must be > 0.")

    if DYNAMIC <= 0:
        raise ValueError("DYNAMIC must be > 0.")

    if FREQUENCY_SCALE not in {"linear", "log"}:
        raise ValueError(
            'FREQUENCY_SCALE must be "linear" or "log".'
        )


def load_and_process_image():
    """
    Load the image, convert to grayscale, resize to STFT dimensions,
    and apply creative image processing.
    """

    spec_height = N_FFT // 2 + 1

    img = Image.open(IMAGE_PATH).convert("L")

    # Resize to the exact spectrogram dimensions.
    img = img.resize(
        (WIDTH, spec_height),
        Image.Resampling.LANCZOS
    )

    if INVERT:
        img = ImageOps.invert(img)

    if CONTRAST != 1.0:
        img = ImageEnhance.Contrast(img).enhance(CONTRAST)

    if BLUR > 0:
        img = img.filter(
            ImageFilter.GaussianBlur(radius=BLUR)
        )

    if FLIP_HORIZONTAL:
        img = ImageOps.mirror(img)

    pixels = np.asarray(img).astype(np.float32) / 255.0

    # Gamma shaping.
    pixels = np.clip(pixels, 0.0, 1.0)
    pixels = pixels ** GAMMA

    if FLIP_VERTICAL:
        pixels = np.flipud(pixels)

    return pixels


def build_frequency_axis():
    """Return the STFT frequency bins."""

    return np.linspace(
        0.0,
        SAMPLE_RATE / 2.0,
        N_FFT // 2 + 1
    )


def apply_frequency_mapping(pixels, frequencies):
    """
    Map image rows to actual STFT frequencies.

    The image has one row for every STFT frequency bin.
    Frequencies outside FMIN/FMAX are muted.

    For musical use, the image is interpreted along a logarithmic
    frequency axis: the image's vertical coordinate is first mapped
    from log(FMIN) to log(FMAX), then interpolated onto the actual
    linear STFT frequency bins.
    """

    source_height, width = pixels.shape

    target = np.zeros_like(pixels)

    valid = (
        (frequencies >= FMIN) &
        (frequencies <= FMAX)
    )

    target_freqs = frequencies[valid]

    if FREQUENCY_SCALE == "linear":
        source_positions = np.linspace(
            FMIN,
            FMAX,
            source_height
        )

        for column in range(width):
            target[valid, column] = np.interp(
                target_freqs,
                source_positions,
                pixels[:, column],
                left=0.0,
                right=0.0
            )

    else:
        # Logarithmic mapping.
        #
        # Image row 0 corresponds to FMIN.
        # Image row height-1 corresponds to FMAX.
        #
        # The log interpolation makes equal vertical distances
        # correspond approximately to equal musical intervals.

        log_fmin = np.log(FMIN)
        log_fmax = np.log(FMAX)

        image_log_frequencies = np.linspace(
            log_fmin,
            log_fmax,
            source_height
        )

        target_log_frequencies = np.log(
            np.maximum(target_freqs, FMIN)
        )

        for column in range(width):
            target[valid, column] = np.interp(
                target_log_frequencies,
                image_log_frequencies,
                pixels[:, column],
                left=0.0,
                right=0.0
            )

    return target


def image_to_magnitude(pixels):
    """
    Convert normalized image values into a linear-amplitude
    magnitude spectrogram using a dB dynamic range.
    """

    frequencies = build_frequency_axis()

    mapped = apply_frequency_mapping(
        pixels,
        frequencies
    )

    # Optional spectrogram-domain smoothing.
    if SPECTRUM_BLUR_TIME > 0 or SPECTRUM_BLUR_FREQ > 0:
        mapped = gaussian_filter(
            mapped,
            sigma=(
                SPECTRUM_BLUR_FREQ,
                SPECTRUM_BLUR_TIME
            )
        )

    mapped = np.clip(mapped, 0.0, 1.0)

    threshold = 0.15
    mapped = np.maximum(mapped - threshold, 0.0)
    mapped /= (1.0 - threshold)

    mapped = mapped ** 1.5

    # Map:
    # pixel = 1.0 -> 0 dB
    # pixel = 0.0 -> -DYNAMIC dB
    db = -DYNAMIC + mapped * DYNAMIC

    magnitude = librosa.db_to_amplitude(
        db,
        ref=1.0
    )

    return magnitude, frequencies


def reconstruct_audio(magnitude):
    """Reconstruct audio from magnitude using Griffin-Lim."""

    audio = librosa.griffinlim(
        magnitude,
        n_iter=GRIFFIN_LIM_ITERATIONS,
        hop_length=HOP_LENGTH,
        win_length=N_FFT,
        n_fft=N_FFT,
        window="hann",
        center=True,
        momentum=GRIFFIN_LIM_MOMENTUM,
        init="random",
        random_state=RANDOM_SEED
    )

    # Normalize.
    peak = np.max(np.abs(audio))

    if peak > 0:
        audio = audio / peak

    audio *= OUTPUT_GAIN

    return audio


def save_spectrogram(magnitude, frequencies):
    """
    Save a visualization of the generated magnitude spectrogram.
    """

    if not SAVE_VISUALIZATION:
        return

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Convert to dB for visualization.
    magnitude_db = librosa.amplitude_to_db(
        magnitude,
        ref=np.max(magnitude)
    )

    duration = (
        magnitude.shape[1] *
        HOP_LENGTH /
        SAMPLE_RATE
    )

    fig, ax = plt.subplots(figsize=(12, 7))

    img = ax.imshow(
        magnitude_db,
        origin="lower",
        aspect="auto",
        extent=[
            0,
            duration,
            0,
            SAMPLE_RATE / 2
        ],
        cmap="magma",
        vmin=-DYNAMIC,
        vmax=0
    )

    # Display frequency axis logarithmically.
    ax.set_yscale("log")
    ax.set_ylim(FMIN, FMAX)

    tick_candidates = np.array([
        20, 30, 40, 50, 60, 70, 80, 90,
        100, 150, 200, 250, 300, 400, 500, 600, 700, 800,
        1000, 1500, 2000, 2500, 3000, 4000, 5000,
        6000, 7000, 8000, 10000, 12000, 15000, 20000
    ])

    freq_ticks = tick_candidates[
        (tick_candidates >= FMIN) &
        (tick_candidates <= FMAX)
        ]

    ax.yaxis.set_major_locator(FixedLocator(freq_ticks))
    ax.yaxis.set_major_formatter(
        FuncFormatter(lambda y, _: f"{int(y):,}".replace(",", ".") + " Hz")
    )

    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Frequency (Hz)")
    ax.set_title(
        f"Image-to-Sound Spectrogram — {OUTPUT_NAME}"
    )

    fig.colorbar(
        img,
        ax=ax,
        label="Amplitude (dB)"
    )

    fig.tight_layout()

    fig.savefig(
        OUTPUT_SPECTROGRAM,
        dpi=FIGURE_DPI
    )

    plt.close(fig)


def save_audio(audio):
    """Write the reconstructed audio as WAV."""

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    sf.write(
        OUTPUT_WAV,
        audio,
        SAMPLE_RATE,
        subtype="PCM_24"
    )


# ============================================================
# MAIN
# ============================================================

def main():

    validate_config()

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    print("=" * 60)
    print("IMAGE-TO-SOUND")
    print("=" * 60)

    print(f"Input image:       {IMAGE_PATH}")
    print(f"Output WAV:        {OUTPUT_WAV}")
    print(f"Output spectrum:   {OUTPUT_SPECTROGRAM}")
    print()
    print(f"Sample rate:       {SAMPLE_RATE} Hz")
    print(f"N_FFT:             {N_FFT}")
    print(f"Hop length:        {HOP_LENGTH}")
    print(f"Frequency range:   {FMIN} - {FMAX} Hz")
    print(f"Frequency scale:   {FREQUENCY_SCALE}")
    print(f"Dynamic range:     {DYNAMIC} dB")
    print(f"Gamma:             {GAMMA}")
    print(f"Griffin-Lim:       {GRIFFIN_LIM_ITERATIONS} iterations")
    print()

    duration = WIDTH * HOP_LENGTH / SAMPLE_RATE

    print(f"Approx. duration:  {duration:.2f} s")
    print()

    print("1/4 Loading and processing image...")
    pixels = load_and_process_image()

    print("2/4 Converting image to magnitude spectrogram...")
    magnitude, frequencies = image_to_magnitude(pixels)

    print("3/4 Reconstructing audio with Griffin-Lim...")
    audio = reconstruct_audio(magnitude)

    print("4/4 Saving files...")
    save_audio(audio)
    save_spectrogram(magnitude, frequencies)

    print()
    print("Done.")
    print(f"WAV: {OUTPUT_WAV.resolve()}")

    if SAVE_VISUALIZATION:
        print(
            f"Spectrogram: "
            f"{OUTPUT_SPECTROGRAM.resolve()}"
        )


if __name__ == "__main__":
    main()

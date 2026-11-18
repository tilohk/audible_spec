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

This file is also the reference implementation for the C++ plugin:
every pipeline stage is a separate function taking a `Config`, so
`reference/export_fixtures.py` can dump each intermediate result.
"""

from collections.abc import Mapping
from dataclasses import dataclass, fields, replace
from pathlib import Path
from typing import Any, Optional

import librosa
import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf
from matplotlib.ticker import FixedLocator, FuncFormatter
from PIL import Image, ImageEnhance, ImageFilter, ImageOps
from pillow_heif import register_heif_opener
from scipy.ndimage import gaussian_filter

register_heif_opener()


# ============================================================
# CONFIG
# ============================================================

# -----------------------------
# File Settings
# -----------------------------

# Default input is a generated test image (reference/make_test_images.py).
# Put your own images into data/images/ (git-ignored) and point IMAGE_PATH there.
IMAGE_PATH = Path("reference/test_images/scene_portrait.heic")
OUTPUT_NAME = "testneu1"
OUTPUT_DIR = Path("data/output")


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

# Values below this (0..1, after frequency mapping) are treated as silence.
# Higher = removes more low-level noise from the image.
NOISE_FLOOR = 0.15

# Exponent applied after the noise floor.
# > 1.0 = quieter mid-tones / more contrast in the spectrum.
CURVE = 1.5

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
# CONFIG OBJECT
# ============================================================


@dataclass(frozen=True)
class Config:
    """All parameters of one render. Field names are the lower-case CONFIG names."""

    image_path: Path
    output_name: str
    output_dir: Path
    width: int
    gamma: float
    blur: float
    contrast: float
    invert: bool
    flip_horizontal: bool
    flip_vertical: bool
    sample_rate: int
    n_fft: int
    fmin: float
    fmax: float
    frequency_scale: str
    hop_length: int
    dynamic: float
    noise_floor: float
    curve: float
    output_gain: float
    griffin_lim_iterations: int
    random_seed: int
    griffin_lim_momentum: float
    spectrum_blur_time: float
    spectrum_blur_freq: float
    save_visualization: bool
    figure_dpi: int

    @property
    def spec_height(self) -> int:
        return self.n_fft // 2 + 1

    @property
    def duration(self) -> float:
        return self.width * self.hop_length / self.sample_rate

    @property
    def output_wav(self) -> Path:
        return self.output_dir / f"{self.output_name}.wav"

    @property
    def output_spectrogram(self) -> Path:
        return self.output_dir / f"{self.output_name}_spec.png"

    def with_params(self, params: Mapping[str, Any]) -> "Config":
        """Return a copy with `params` applied. Keys may be lower or upper case."""
        names = {f.name for f in fields(self)}
        updates: dict[str, Any] = {}
        for key, value in params.items():
            name = key.lower()
            if name not in names:
                raise KeyError(f"Unknown parameter: {key}")
            if name in {"image_path", "output_dir"}:
                value = Path(value)
            updates[name] = value
        return replace(self, **updates)

    def to_dict(self) -> dict[str, Any]:
        """JSON-friendly dict (paths as strings)."""
        result = {}
        for f in fields(self):
            value = getattr(self, f.name)
            result[f.name] = str(value) if isinstance(value, Path) else value
        return result


def default_config() -> Config:
    """Build a Config from the CONFIG constants at the top of this file."""
    return Config(
        image_path=IMAGE_PATH,
        output_name=OUTPUT_NAME,
        output_dir=OUTPUT_DIR,
        width=WIDTH,
        gamma=GAMMA,
        blur=BLUR,
        contrast=CONTRAST,
        invert=INVERT,
        flip_horizontal=FLIP_HORIZONTAL,
        flip_vertical=FLIP_VERTICAL,
        sample_rate=SAMPLE_RATE,
        n_fft=N_FFT,
        fmin=FMIN,
        fmax=FMAX,
        frequency_scale=FREQUENCY_SCALE,
        hop_length=HOP_LENGTH,
        dynamic=DYNAMIC,
        noise_floor=NOISE_FLOOR,
        curve=CURVE,
        output_gain=OUTPUT_GAIN,
        griffin_lim_iterations=GRIFFIN_LIM_ITERATIONS,
        random_seed=RANDOM_SEED,
        griffin_lim_momentum=GRIFFIN_LIM_MOMENTUM,
        spectrum_blur_time=SPECTRUM_BLUR_TIME,
        spectrum_blur_freq=SPECTRUM_BLUR_FREQ,
        save_visualization=SAVE_VISUALIZATION,
        figure_dpi=FIGURE_DPI,
    )


@dataclass
class RenderResult:
    """Every intermediate stage of one render (used for C++ test fixtures)."""

    resized: np.ndarray  # uint8 grayscale, resized, before any processing
    pixels: np.ndarray  # float32 0..1 after image processing
    frequencies: np.ndarray  # STFT bin frequencies in Hz
    mapped: np.ndarray  # float32 0..1 after frequency mapping
    magnitude: np.ndarray  # linear magnitude spectrogram
    raw_audio: np.ndarray  # Griffin-Lim output before normalization
    audio: np.ndarray  # final normalized audio


# ============================================================
# HELPERS
# ============================================================


def validate_config(cfg: Config):
    """Validate parameters before doing expensive processing."""

    if not cfg.image_path.exists():
        raise FileNotFoundError(f"Image not found: {cfg.image_path.resolve()}")

    if cfg.sample_rate <= 0:
        raise ValueError("SAMPLE_RATE must be > 0.")

    if cfg.n_fft <= 0:
        raise ValueError("N_FFT must be > 0.")

    if cfg.hop_length <= 0:
        raise ValueError("HOP_LENGTH must be > 0.")

    if cfg.width <= 0:
        raise ValueError("WIDTH must be > 0.")

    if cfg.fmin <= 0:
        raise ValueError("FMIN must be > 0.")

    nyquist = cfg.sample_rate / 2.0

    if cfg.fmax > nyquist:
        raise ValueError(f"FMAX ({cfg.fmax}) cannot exceed Nyquist frequency ({nyquist}).")

    if cfg.fmin >= cfg.fmax:
        raise ValueError("FMIN must be smaller than FMAX.")

    if cfg.gamma <= 0:
        raise ValueError("GAMMA must be > 0.")

    if cfg.dynamic <= 0:
        raise ValueError("DYNAMIC must be > 0.")

    if not 0.0 <= cfg.noise_floor < 1.0:
        raise ValueError("NOISE_FLOOR must be in [0, 1).")

    if cfg.curve <= 0:
        raise ValueError("CURVE must be > 0.")

    if cfg.frequency_scale not in {"linear", "log"}:
        raise ValueError('FREQUENCY_SCALE must be "linear" or "log".')


def load_resized_image(cfg: Config) -> Image.Image:
    """Load the image as grayscale and resize it to the STFT dimensions."""

    img = Image.open(cfg.image_path).convert("L")

    # Resize to the exact spectrogram dimensions.
    return img.resize((cfg.width, cfg.spec_height), Image.Resampling.LANCZOS)


def process_image(img: Image.Image, cfg: Config) -> np.ndarray:
    """Apply the creative image processing to an already resized image."""

    if cfg.invert:
        img = ImageOps.invert(img)

    if cfg.contrast != 1.0:
        img = ImageEnhance.Contrast(img).enhance(cfg.contrast)

    if cfg.blur > 0:
        img = img.filter(ImageFilter.GaussianBlur(radius=cfg.blur))

    if cfg.flip_horizontal:
        img = ImageOps.mirror(img)

    pixels = np.asarray(img).astype(np.float32) / 255.0

    # Gamma shaping.
    pixels = np.clip(pixels, 0.0, 1.0)
    pixels = pixels**cfg.gamma

    if cfg.flip_vertical:
        pixels = np.flipud(pixels)

    return pixels


def load_and_process_image(cfg: Config) -> np.ndarray:
    """
    Load the image, convert to grayscale, resize to STFT dimensions,
    and apply creative image processing.
    """

    return process_image(load_resized_image(cfg), cfg)


def build_frequency_axis(cfg: Config) -> np.ndarray:
    """Return the STFT frequency bins."""

    return np.linspace(0.0, cfg.sample_rate / 2.0, cfg.n_fft // 2 + 1)


def apply_frequency_mapping(pixels: np.ndarray, frequencies: np.ndarray, cfg: Config) -> np.ndarray:
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

    valid = (frequencies >= cfg.fmin) & (frequencies <= cfg.fmax)

    target_freqs = frequencies[valid]

    if cfg.frequency_scale == "linear":
        source_positions = np.linspace(cfg.fmin, cfg.fmax, source_height)

        for column in range(width):
            target[valid, column] = np.interp(
                target_freqs, source_positions, pixels[:, column], left=0.0, right=0.0
            )

    else:
        # Logarithmic mapping.
        #
        # Image row 0 corresponds to FMIN.
        # Image row height-1 corresponds to FMAX.
        #
        # The log interpolation makes equal vertical distances
        # correspond approximately to equal musical intervals.

        log_fmin = np.log(cfg.fmin)
        log_fmax = np.log(cfg.fmax)

        image_log_frequencies = np.linspace(log_fmin, log_fmax, source_height)

        target_log_frequencies = np.log(np.maximum(target_freqs, cfg.fmin))

        for column in range(width):
            target[valid, column] = np.interp(
                target_log_frequencies,
                image_log_frequencies,
                pixels[:, column],
                left=0.0,
                right=0.0,
            )

    return target


def shape_magnitude(mapped: np.ndarray, cfg: Config) -> np.ndarray:
    """
    Convert frequency-mapped values (0..1) into a linear-amplitude
    magnitude spectrogram using a dB dynamic range.
    """

    # Optional spectrogram-domain smoothing.
    if cfg.spectrum_blur_time > 0 or cfg.spectrum_blur_freq > 0:
        mapped = gaussian_filter(mapped, sigma=(cfg.spectrum_blur_freq, cfg.spectrum_blur_time))

    mapped = np.clip(mapped, 0.0, 1.0)

    mapped = np.maximum(mapped - cfg.noise_floor, 0.0)
    mapped /= 1.0 - cfg.noise_floor

    mapped = mapped**cfg.curve

    # Map:
    # pixel = 1.0 -> 0 dB
    # pixel = 0.0 -> -DYNAMIC dB
    db = -cfg.dynamic + mapped * cfg.dynamic

    return librosa.db_to_amplitude(db, ref=1.0)


def image_to_magnitude(pixels: np.ndarray, cfg: Config):
    """
    Convert normalized image values into a linear-amplitude
    magnitude spectrogram using a dB dynamic range.
    """

    frequencies = build_frequency_axis(cfg)
    mapped = apply_frequency_mapping(pixels, frequencies, cfg)
    return shape_magnitude(mapped, cfg), frequencies


def griffin_lim(magnitude: np.ndarray, cfg: Config) -> np.ndarray:
    """Estimate a phase with Griffin-Lim and return the raw (unnormalized) audio."""

    return librosa.griffinlim(
        magnitude,
        n_iter=cfg.griffin_lim_iterations,
        hop_length=cfg.hop_length,
        win_length=cfg.n_fft,
        n_fft=cfg.n_fft,
        window="hann",
        center=True,
        momentum=cfg.griffin_lim_momentum,
        init="random",
        random_state=cfg.random_seed,
    )


def normalize_audio(audio: np.ndarray, cfg: Config) -> np.ndarray:
    """Peak-normalize and apply OUTPUT_GAIN."""

    peak = np.max(np.abs(audio))

    if peak > 0:
        audio = audio / peak

    audio *= cfg.output_gain

    return audio


def reconstruct_audio(magnitude: np.ndarray, cfg: Config) -> np.ndarray:
    """Reconstruct audio from magnitude using Griffin-Lim."""

    return normalize_audio(griffin_lim(magnitude, cfg), cfg)


def render(cfg: Config) -> RenderResult:
    """Run the whole pipeline and keep every intermediate stage."""

    validate_config(cfg)

    resized = load_resized_image(cfg)
    pixels = process_image(resized, cfg)
    frequencies = build_frequency_axis(cfg)
    mapped = apply_frequency_mapping(pixels, frequencies, cfg)
    magnitude = shape_magnitude(mapped, cfg)
    raw_audio = griffin_lim(magnitude, cfg)
    audio = normalize_audio(raw_audio.copy(), cfg)

    return RenderResult(
        resized=np.asarray(resized),
        pixels=pixels,
        frequencies=frequencies,
        mapped=mapped,
        magnitude=magnitude,
        raw_audio=raw_audio,
        audio=audio,
    )


def save_spectrogram(magnitude: np.ndarray, frequencies: np.ndarray, cfg: Config):
    """
    Save a visualization of the generated magnitude spectrogram.
    """

    if not cfg.save_visualization:
        return

    cfg.output_dir.mkdir(parents=True, exist_ok=True)

    # Convert to dB for visualization.
    magnitude_db = librosa.amplitude_to_db(magnitude, ref=np.max(magnitude))

    duration = magnitude.shape[1] * cfg.hop_length / cfg.sample_rate

    fig, ax = plt.subplots(figsize=(12, 7))

    img = ax.imshow(
        magnitude_db,
        origin="lower",
        aspect="auto",
        extent=[0, duration, 0, cfg.sample_rate / 2],
        cmap="magma",
        vmin=-cfg.dynamic,
        vmax=0,
    )

    # Display frequency axis logarithmically.
    ax.set_yscale("log")
    ax.set_ylim(cfg.fmin, cfg.fmax)

    tick_candidates = np.array(
        [
            20, 30, 40, 50, 60, 70, 80, 90,
            100, 150, 200, 250, 300, 400, 500, 600, 700, 800,
            1000, 1500, 2000, 2500, 3000, 4000, 5000,
            6000, 7000, 8000, 10000, 12000, 15000, 20000,
        ]
    )  # fmt: skip

    freq_ticks = tick_candidates[(tick_candidates >= cfg.fmin) & (tick_candidates <= cfg.fmax)]

    ax.yaxis.set_major_locator(FixedLocator(freq_ticks))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda y, _: f"{int(y):,}".replace(",", ".") + " Hz"))

    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Frequency (Hz)")
    ax.set_title(f"Image-to-Sound Spectrogram — {cfg.output_name}")

    fig.colorbar(img, ax=ax, label="Amplitude (dB)")

    fig.tight_layout()

    fig.savefig(cfg.output_spectrogram, dpi=cfg.figure_dpi)

    plt.close(fig)


def save_audio(audio: np.ndarray, cfg: Config):
    """Write the reconstructed audio as WAV."""

    cfg.output_dir.mkdir(parents=True, exist_ok=True)

    sf.write(cfg.output_wav, audio, cfg.sample_rate, subtype="PCM_24")


# ============================================================
# MAIN
# ============================================================


def main(cfg: Optional[Config] = None):
    cfg = cfg or default_config()

    validate_config(cfg)

    cfg.output_dir.mkdir(parents=True, exist_ok=True)

    print("=" * 60)
    print("IMAGE-TO-SOUND")
    print("=" * 60)

    print(f"Input image:       {cfg.image_path}")
    print(f"Output WAV:        {cfg.output_wav}")
    print(f"Output spectrum:   {cfg.output_spectrogram}")
    print()
    print(f"Sample rate:       {cfg.sample_rate} Hz")
    print(f"N_FFT:             {cfg.n_fft}")
    print(f"Hop length:        {cfg.hop_length}")
    print(f"Frequency range:   {cfg.fmin} - {cfg.fmax} Hz")
    print(f"Frequency scale:   {cfg.frequency_scale}")
    print(f"Dynamic range:     {cfg.dynamic} dB")
    print(f"Noise floor/curve: {cfg.noise_floor} / {cfg.curve}")
    print(f"Gamma:             {cfg.gamma}")
    print(f"Griffin-Lim:       {cfg.griffin_lim_iterations} iterations")
    print()

    print(f"Approx. duration:  {cfg.duration:.2f} s")
    print()

    print("1/4 Loading and processing image...")
    pixels = load_and_process_image(cfg)

    print("2/4 Converting image to magnitude spectrogram...")
    magnitude, frequencies = image_to_magnitude(pixels, cfg)

    print("3/4 Reconstructing audio with Griffin-Lim...")
    audio = reconstruct_audio(magnitude, cfg)

    print("4/4 Saving files...")
    save_audio(audio, cfg)
    save_spectrogram(magnitude, frequencies, cfg)

    print()
    print("Done.")
    print(f"WAV: {cfg.output_wav.resolve()}")

    if cfg.save_visualization:
        print(f"Spectrogram: {cfg.output_spectrogram.resolve()}")


if __name__ == "__main__":
    main()

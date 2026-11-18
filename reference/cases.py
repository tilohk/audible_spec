"""Fixture cases shared by export_fixtures.py and the reference tests.

Each case is a set of overrides on top of `main.default_config()`. The sizes
are deliberately small (n_fft 1024, a few hundred columns, few Griffin-Lim
iterations) so the fixtures stay small in git and the tests run fast, while
still covering every code path of the pipeline.
"""

from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
FIXTURE_DIR = REPO_ROOT / "reference" / "fixtures"

# Settings common to all cases.
_SMALL = {
    "n_fft": 1024,
    "hop_length": 128,
    "griffin_lim_iterations": 32,
    "save_visualization": False,
}

CASES = {
    # Default creative settings, log frequency scale, near-binary RGBA shapes
    # (hard edges; alpha must be ignored by the grayscale conversion).
    "shapes_rgba_log_default": {
        **_SMALL,
        "image_path": "reference/test_images/shapes_rgba.png",
        "width": 256,
    },
    # Continuous-tone RGB image, default settings: exercises gamma, noise floor,
    # curve and dynamic range, which a near-binary image cannot.
    "gradient_log_default": {
        **_SMALL,
        "image_path": "reference/test_images/gradient_rgb.png",
        "width": 256,
    },
    # Every optional processing step switched on, linear frequency scale.
    "gradient_linear_all_options": {
        **_SMALL,
        "image_path": "reference/test_images/gradient_rgb.png",
        "width": 256,
        "frequency_scale": "linear",
        "fmin": 40.0,
        "fmax": 12000.0,
        "gamma": 0.6,
        "blur": 1.5,
        "contrast": 1.2,
        "invert": False,
        "flip_horizontal": True,
        "dynamic": 70.0,
        "noise_floor": 0.1,
        "curve": 1.2,
        "spectrum_blur_time": 1.0,
        "spectrum_blur_freq": 2.0,
        "griffin_lim_momentum": 0.99,
        "random_seed": 7,
    },
    # Photo-like generated scene encoded as HEIC (portrait), default settings.
    "heic_scene_default": {
        **_SMALL,
        "image_path": "reference/test_images/scene_portrait.heic",
        "width": 384,
    },
}

# Arrays written per case, in pipeline order (see main.RenderResult).
STAGES = ["resized", "pixels", "frequencies", "mapped", "magnitude", "raw_audio", "audio"]

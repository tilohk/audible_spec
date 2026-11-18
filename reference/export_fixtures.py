"""Export golden test fixtures from the Python reference implementation.

Usage (from the repository root):
    venv/bin/python reference/export_fixtures.py

For every case in reference/cases.py this writes reference/fixtures/<case>/:
    params.json       full Config used for the render
    <stage>.npy       every intermediate stage (see cases.STAGES)
    metrics.json      shapes, Griffin-Lim spectral convergence, library versions

The C++ tests (phase 2) load these files. Never edit them by hand: change
main.py or cases.py and re-run this script.
"""

import json
import os
import platform
import shutil
import sys
from pathlib import Path

os.environ.setdefault("MPLBACKEND", "Agg")

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import librosa  # noqa: E402
import numpy as np  # noqa: E402
import PIL  # noqa: E402
import scipy  # noqa: E402
from cases import CASES, FIXTURE_DIR, REPO_ROOT, STAGES  # noqa: E402

import main  # noqa: E402


def spectral_convergence(audio: np.ndarray, magnitude: np.ndarray, cfg: main.Config) -> float:
    """||(|STFT(audio)| - M)||_F / ||M||_F with the same STFT settings as Griffin-Lim."""
    rebuilt = np.abs(
        librosa.stft(
            audio,
            n_fft=cfg.n_fft,
            hop_length=cfg.hop_length,
            win_length=cfg.n_fft,
            window="hann",
            center=True,
        )
    )
    frames = min(rebuilt.shape[1], magnitude.shape[1])
    diff = rebuilt[:, :frames] - magnitude[:, :frames]
    return float(np.linalg.norm(diff) / np.linalg.norm(magnitude[:, :frames]))


def config_for(case_params: dict) -> main.Config:
    return main.default_config().with_params(case_params)


def export_case(name: str, params: dict) -> dict:
    cfg = config_for(params)
    result = main.render(cfg)

    out = FIXTURE_DIR / name
    out.mkdir(parents=True, exist_ok=True)

    for stage in STAGES:
        np.save(out / f"{stage}.npy", getattr(result, stage))

    (out / "params.json").write_text(json.dumps(cfg.to_dict(), indent=2) + "\n")

    metrics = {
        "shapes": {stage: list(getattr(result, stage).shape) for stage in STAGES},
        "dtypes": {stage: str(getattr(result, stage).dtype) for stage in STAGES},
        "griffin_lim_spectral_convergence": spectral_convergence(result.raw_audio, result.magnitude, cfg),
        "duration_seconds": cfg.duration,
        "versions": {
            "python": platform.python_version(),
            "numpy": np.__version__,
            "librosa": librosa.__version__,
            "scipy": scipy.__version__,
            "pillow": PIL.__version__,
        },
    }
    (out / "metrics.json").write_text(json.dumps(metrics, indent=2) + "\n")
    return metrics


def main_cli():
    os.chdir(REPO_ROOT)  # image paths in the cases are relative to the repo root

    # Remove fixture folders of cases that no longer exist in cases.py.
    if FIXTURE_DIR.is_dir():
        for stale in sorted(p for p in FIXTURE_DIR.iterdir() if p.is_dir() and p.name not in CASES):
            shutil.rmtree(stale)
            print(f"removed stale fixture {stale.name}")

    for name, params in CASES.items():
        metrics = export_case(name, params)
        size_kb = sum(f.stat().st_size for f in (FIXTURE_DIR / name).iterdir()) / 1024
        print(
            f"{name:26s} spectrogram {metrics['shapes']['magnitude']}  "
            f"SC={metrics['griffin_lim_spectral_convergence']:.4f}  {size_kb:,.0f} KB"
        )


if __name__ == "__main__":
    main_cli()

import json

import numpy as np
import pytest
from cases import CASES, FIXTURE_DIR, STAGES
from export_fixtures import config_for, spectral_convergence

import main


def test_default_config_is_valid():
    main.validate_config(main.default_config())


def test_with_params_accepts_upper_and_lower_case():
    cfg = main.default_config().with_params({"GAMMA": 0.5, "n_fft": 2048, "image_path": "x.png"})
    assert cfg.gamma == 0.5
    assert cfg.n_fft == 2048
    assert cfg.spec_height == 1025
    assert str(cfg.image_path) == "x.png"


def test_with_params_rejects_unknown_keys():
    with pytest.raises(KeyError):
        main.default_config().with_params({"not_a_param": 1})


@pytest.mark.parametrize(
    "override",
    [{"fmax": 30000.0}, {"fmin": 9000.0}, {"noise_floor": 1.0}, {"curve": 0.0}, {"frequency_scale": "mel"}],
)
def test_invalid_configs_are_rejected(override):
    with pytest.raises(ValueError):
        main.validate_config(main.default_config().with_params(override))


@pytest.mark.parametrize("case", sorted(CASES))
def test_fixtures_match_current_reference(case):
    """main.py must reproduce the committed fixtures exactly.

    If this fails after an intentional change to main.py, regenerate the
    fixtures with reference/export_fixtures.py and review the diff.
    """
    case_dir = FIXTURE_DIR / case
    assert case_dir.is_dir(), f"missing fixture dir {case_dir}; run reference/export_fixtures.py"

    cfg = config_for(CASES[case])
    stored_params = json.loads((case_dir / "params.json").read_text())
    assert stored_params == cfg.to_dict(), "cases.py changed; regenerate the fixtures"

    result = main.render(cfg)
    for stage in STAGES:
        expected = np.load(case_dir / f"{stage}.npy")
        np.testing.assert_array_equal(getattr(result, stage), expected, err_msg=f"stage {stage}")


@pytest.mark.parametrize("case", sorted(CASES))
def test_griffin_lim_converges(case):
    """Sanity bound: the reconstruction must resemble the target spectrogram."""
    case_dir = FIXTURE_DIR / case
    metrics = json.loads((case_dir / "metrics.json").read_text())
    cfg = config_for(CASES[case])
    sc = spectral_convergence(np.load(case_dir / "raw_audio.npy"), np.load(case_dir / "magnitude.npy"), cfg)
    assert sc == pytest.approx(metrics["griffin_lim_spectral_convergence"], rel=1e-6)
    assert sc < 0.6

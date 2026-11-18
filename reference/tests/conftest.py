import os
import sys
from pathlib import Path

import pytest

os.environ.setdefault("MPLBACKEND", "Agg")

REFERENCE_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REFERENCE_DIR.parent))  # main.py
sys.path.insert(0, str(REFERENCE_DIR))  # cases.py, export_fixtures.py


@pytest.fixture(autouse=True)
def _run_from_repo_root(monkeypatch):
    """Image paths in configs and cases are relative to the repository root."""
    monkeypatch.chdir(REFERENCE_DIR.parent)

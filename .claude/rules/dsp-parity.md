---
paths:
  - "plugin/Source/dsp/**"
  - "plugin/Tests/**"
  - "reference/**"
  - "main.py"
---

# DSP parity rules (C++ port ↔ Python reference)

- `main.py` is the source of truth. Port it function by function, keeping the names recognisable (`load_and_process_image` → `ImageProcessor::process`, and so on), and note the Python function in a one-line comment above each port.
- `dsp/` code has no JUCE GUI or plugin dependencies. Only `juce_core`, `juce_dsp` and `juce_graphics` (for image decoding) may be used, so that `render_cli` and the tests can link it standalone.
- Every stage gets a test (`plugin/Tests/DspTests.cpp`) that feeds it the *Python* output of the previous stage from `reference/fixtures/` and compares the result:
  - 8-bit image stages (grayscale, LANCZOS resize, invert, contrast, blur, mirror): byte-identical. HEIC decoding may differ by a few levels (≤ 3) because the decoder is different (ImageIO on macOS, libheif/libde265 on Windows/Linux). PNG is exact on every platform;
  - pixels, mapped, magnitude: max abs error ≤ 1e-4 (currently bit-exact up to ~5e-7);
  - Griffin-Lim: the C++ code reproduces librosa's random initial phase exactly (NumPy `RandomState` = MT19937). The spectral convergence must be ≤ Python's (both measured with the C++ STFT, plus 1e-5 for float32 rounding), and the waveform must match within 0.1 % of peak.
- Fixtures are generated only by `reference/export_fixtures.py`. Never edit them by hand or loosen a tolerance to make a test pass. If you need to, stop and explain the discrepancy to the owner.
- The values `threshold = 0.15` and the exponent `1.5` in `image_to_magnitude` become the render parameters `noiseFloor` and `curve`, with those values as defaults.
- Resize with the same filter as Python (PIL LANCZOS) or document the measured difference. Image-resize differences are the most likely source of parity failures.

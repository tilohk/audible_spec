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
- Every stage gets a test that loads its fixture from `reference/fixtures/` and compares the result:
  - processed pixels and the mapped magnitude: max abs error ≤ 1e-4;
  - Griffin-Lim: compare the **spectral convergence** against the target magnitude, never the samples (the phase is random and the RNGs differ).
- Fixtures are generated only by `reference/export_fixtures.py`. Never edit them by hand or loosen a tolerance to make a test pass. If you need to, stop and explain the discrepancy to the owner.
- The values `threshold = 0.15` and the exponent `1.5` in `image_to_magnitude` become the render parameters `noiseFloor` and `curve`, with those values as defaults.
- Resize with the same filter as Python (PIL LANCZOS) or document the measured difference. Image-resize differences are the most likely source of parity failures.

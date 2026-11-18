# Reference fixtures

`main.py` is the reference implementation. These fixtures freeze its output so the C++ port can be tested stage by stage.

```bash
venv/bin/python reference/make_test_images.py  # creates missing test images (--force recreates all; then re-export)
venv/bin/python reference/export_fixtures.py   # regenerate after changing main.py or cases.py
venv/bin/python -m pytest -q reference/tests    # checks main.py still matches the fixtures
```

## Layout

`fixtures/<case>/`, with cases defined in [cases.py](cases.py):

| File | Content | dtype |
|---|---|---|
| `params.json` | the full `Config` used | — |
| `resized.npy` | grayscale image after LANCZOS resize, before processing (rows = frequency, row 0 = top of image) | uint8 |
| `pixels.npy` | after invert / contrast / blur / flips / gamma (row 0 = low frequency) | float32 |
| `frequencies.npy` | STFT bin centre frequencies in Hz | float64 |
| `mapped.npy` | after log/linear frequency mapping | float32 |
| `magnitude.npy` | linear magnitude spectrogram fed to Griffin-Lim | float32 |
| `raw_audio.npy` | Griffin-Lim output before normalization | float32 |
| `audio.npy` | final peak-normalized audio | float32 |
| `metrics.json` | shapes, Griffin-Lim spectral convergence, library versions | — |

All `.npy` files are plain NumPy v1 format (little-endian, C order).

## Notes for the C++ port

- **Grayscale:** PIL's `convert("L")` drops alpha and uses `L = R*299/1000 + G*587/1000 + B*114/1000` (integer, rounded). `shapes_rgba.png` has semi- and fully transparent areas that must still read as their RGB value.
- **HEIC:** `scene_portrait.heic` (756×1008, portrait) is lossy. The C++ decoder (macOS ImageIO) may differ from pillow-heif/libde265 by a few levels per pixel, so compare decoded HEIC with a tolerance. It carries no rotation metadata, so EXIF/HEIF orientation handling is **not** covered by the fixtures.
- **Resize** is PIL LANCZOS (a=3) with PIL's own antialiasing for downscaling. Expect small differences here. Test the stages *after* `resized.npy` by starting from the fixture, so they are independent of the decoder and resizer.
- **Griffin-Lim** uses a random initial phase (NumPy `RandomState`), so C++ audio can't match sample for sample. Compare its spectral convergence with `metrics.json` instead.

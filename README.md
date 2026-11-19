# audible_spec

An instrument plugin that turns an image into sound. The image is read as a spectrogram (left → right is time, bottom → top is frequency, brightness is loudness). The phase is reconstructed with Griffin-Lim, and you play the result from MIDI like a sampler.

- **Formats:** VST3 (macOS, Windows, Linux), AU (macOS), LV2 (Linux), plus a standalone app. Built with JUCE 8
- **Tested in:** Ableton Live 12 (macOS)
- **Validation:** passes `pluginval --strictness-level 5` (VST3 + AU) and `auval`
- **License:** [GNU AGPL v3](LICENSE)

```text
image → grayscale → gamma / contrast / blur / invert / flip
      → frequency mapping (log or linear) → magnitude spectrogram
      → Griffin-Lim → sample → played from MIDI (envelope, pitch, loop, filter)
```

## Download and install

**macOS, one line in Terminal** (downloads the latest release, installs VST3, AU and the app, nothing to unblock):

```bash
curl -fsSL https://raw.githubusercontent.com/tilohk/audible_spec/main/packaging/install-macos.sh | bash
```

Then in Ableton Live: **Settings → Plug-Ins**, hold **Option (⌥)** and click **Rescan**.

**Or download the zip** for your system from the [latest release](https://github.com/tilohk/audible_spec/releases/latest). Each zip contains `INSTALL.txt` with the exact steps; the macOS zip also contains `install-macos.sh` (run it with `bash install-macos.sh` in Terminal). By hand:

| System | Copy | To |
|---|---|---|
| macOS (Apple Silicon + Intel, 11+) | `audible_spec.vst3` / `audible_spec.component` | `~/Library/Audio/Plug-Ins/VST3/` / `~/Library/Audio/Plug-Ins/Components/` |
| Windows 10/11 (64-bit) | `audible_spec.vst3` | `C:\Program Files\Common Files\VST3\` |
| Linux (x86-64) | `audible_spec.vst3` / `audible_spec.lv2` | `~/.vst3/` / `~/.lv2/` |

`~/Library` is the Library folder inside your home folder, not the system-wide `/Library`. The macOS build is not notarized by Apple: after copying by hand, run this once, otherwise macOS blocks the plugin and Live may hang while scanning:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/audible_spec.vst3 ~/Library/Audio/Plug-Ins/Components/audible_spec.component
```

If Live scanned the plugin before it was unblocked, it remembers it as failed: rescan with **Option (⌥)** held.

Windows and Linux builds are compiled and tested automatically, but have not been tried in a DAW yet. Reports are welcome.

## Using the plugin

1. **Load it** on a MIDI track (Instruments → Plug-Ins → audible_spec). A built-in test image is rendered right away, so it makes a sound immediately.
2. **Load an image:** drag one onto the image area, or click **Load image**. Supported formats are PNG, JPEG, HEIC/HEIF and GIF, plus TIFF, BMP and WebP on macOS.
3. **Shape the render:** click **View render settings**. The image preview updates live while you change the image settings. The sound changes only when you press **Render**, because rendering takes a few seconds. The bar shows the sound length and an estimate of the render time.
4. **Start from somewhere else:**
   - **Render preset:** three built-ins (Clean, Dark / Texture, Bright / Percussive) and your own (*Save current render settings as preset…*).
   - **Dice:** sets random render settings and keeps length and quality.
5. **Play** it from MIDI. C3 plays the sound at its original pitch.
6. **Edit the sample** in the spectrogram:
   - Drag the teal **start/end** markers to set the region that plays.
   - Drag inside the region to move it without changing its length.
7. **Drag WAV to track** puts the rendered sound on an audio track.

Folding the render settings (**Hide render settings**) makes the window smaller when you only want to play.

### Render settings (applied on Render)

| Group | Setting | What it does |
|---|---|---|
| Image | Gamma | < 1 makes dark areas louder, > 1 pushes them down (1 = neutral) |
| | Contrast, Blur | Contrast and smoothing of the image before conversion |
| | Invert, Flip H, Flip V | Swap black/white, play backwards, upside down |
| Frequency | Lowest, Highest | Frequency range the image height is mapped to |
| | Scale | Logarithmic (equal height = equal musical interval, recommended) or linear |
| | FFT size | Frequency resolution: larger is finer in pitch but blurrier in time |
| Time and dynamics | Length | Sound length in seconds |
| | Range | Dynamic range in dB from the brightest to the darkest pixel (60 dB is a good start) |
| | Noise floor, Curve | Pixels below the floor are silent; the curve shapes the loudness of the rest |
| Reconstruction | Iterations, Momentum | Griffin-Lim quality: more iterations sound cleaner but render slower |
| | Seed | Starting phase. Another seed gives a slightly different texture |
| | Peak level | Output level of the rendered sample |

Hop length and spectral smoothing are not shown in the UI. They keep their defaults, or a preset's values.

The image contains no phase information, so Griffin-Lim estimates it. That's why the result has its typical slightly smeared, "spectral" character.

### Playback (real-time, automatable)

| Group | Controls |
|---|---|
| Envelope | Attack, Decay, Sustain, Release |
| Pitch | Transpose (±24 semitones), Tune (cents), Key Tracking on/off |
| Sample | Start, End, loop crossfade, Loop on/off, Reverse |
| Filter | Low-pass, High-pass, Slope 12 or 24 dB/octave |
| Output | Gain, Mono/Poly |

Note velocity always scales the volume. Pitch bend is ±2 semitones.

### Ideas for Usage

- Short or narrow spectrogram samples work well as one-shots; long, wide samples as atmospheric textures.
- Layer the same image with different render settings on several tracks.
- Render a narrow frequency band per track (Lowest/Highest) and mix the bands.
- Drag the WAV to an audio track and warp, slice or resample it further.

## Python reference

`main.py` is the original script and remains the **reference implementation**. The C++ code is tested against fixtures exported from it, and up to the magnitude spectrogram they match within about 1e-6. You can still use it on its own:

```bash
python3 -m venv venv
venv/bin/python -m pip install -r requirements-dev.txt
venv/bin/python main.py            # settings: the CONFIG section at the top of main.py
```

It writes a WAV and a spectrogram PNG to `data/output/`. Put your own images into `data/images/` (git-ignored) and set `IMAGE_PATH`. The render preset files in `presets/` use the same `KEY = value` format as the CONFIG section.

There is also a command-line renderer built from the C++ code:

```bash
plugin/build/RenderCli_artefacts/Debug/render_cli reference/test_images/scene_portrait.heic out.wav --set n_fft=2048
```

## Development

Building needs CMake, Ninja and a C++20 compiler; JUCE is downloaded by CMake. On Windows and Linux, HEIC support comes from libheif via [vcpkg](https://vcpkg.io) (see [`.github/workflows/build.yml`](.github/workflows/build.yml) for the exact commands). Every push is built and tested on macOS, Windows and Linux; pushing a tag like `v1.0.0` publishes the zips as a GitHub Release.

Work follows the phase plan in [docs/PLAN.md](docs/PLAN.md). The rules are in [CLAUDE.md](CLAUDE.md) and `.claude/rules/`. The `.githooks/pre-commit` hook checks formatting and the real-time safety lint, and runs the build and tests. Never bypass it.

## License

audible_spec is free software under the [GNU Affero General Public License v3](LICENSE): you may use, study, change and share it, and anything you distribute based on it must be released under the same license with its source code. It uses [JUCE](https://juce.com) under the AGPLv3, and on Windows/Linux [libheif](https://github.com/strukturag/libheif) and [libde265](https://github.com/strukturag/libde265) (LGPLv3) and [FFTW](https://www.fftw.org) (GPLv2+).

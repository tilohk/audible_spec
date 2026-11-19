# Plan: "Image-to-Sound" instrument plugin (VST3 + AU, JUCE/C++)

## Context
`main.py` turns an image into a log-frequency magnitude spectrogram and rebuilds audio with Griffin-Lim. Right now it is a script you configure by editing constants, and you drag the WAV into Ableton yourself. The goal is a plugin you load in Ableton that:
- takes an image (drag & drop),
- has **render parameters** (image + spectrogram + Griffin-Lim) that apply when you click **Render**, because rendering is expensive,
- has **real-time playback parameters** (envelope, pitch, loop, filter…) that apply immediately,
- plays the rendered sound as an **instrument / sampler** from MIDI, and lets you drag the WAV out to a track.

Decisions already made: instrument/sampler, port the DSP to C++ with JUCE, render button plus real-time playback params, and plenty of scaffolding because you're new to C++.

The Python script stays as the **reference implementation**: the C++ port is checked against it.

---

## Parameter split

**Render parameters (applied on Render).** All existing CONFIG values, plus two that are currently hard-coded:

| Group | Params |
|---|---|
| Image | Gamma, Contrast, Blur, Invert, Flip H, Flip V |
| Frequency | FMin, FMax, Scale (log/linear), FFT size (1024–8192) |
| Time | **Duration in seconds** (replaces WIDTH; WIDTH = duration·sr/hop), Hop length |
| Dynamics | Dynamic range (dB), **Noise floor** (now hard-coded `0.15`), **Curve** (now hard-coded `1.5`) |
| Spectral smoothing | Spectrum blur time / freq |
| Reconstruction | Griffin-Lim iterations, Momentum, Seed |

Hop length and spectral smoothing (time/freq) are not shown in the UI since the 6c revision: they keep their defaults (or a preset's values).

Gamma, contrast, blur, invert and flip are cheap, so the **processed-image preview updates live** while you turn them. Only the audio waits for Render. The UI also shows an estimated render time and duration.

**Real-time parameters** (automatable in Ableton, no re-render):
Output gain · ADSR · Transpose (±24 semitones) / key-tracking on/off · Fine tune · Sample start / end · Loop on/off + loop start/end (relative to the sample region) + crossfade · Reverse · Low-pass / high-pass filter + slope (12/24 dB/oct) · Polyphony (mono/poly). Velocity always scales the level (no parameter).

## UI layout (as built, revised after phase 6)
```
┌───────────────────────────────────────────────────────────────────────┐
│ [Load image]  <image name>                               audible_spec  │
├──────────────── RENDER (bar always visible) ───────────────────────────┤
│ [View/Hide render settings] Render preset [▾] [dice] 2.0 s, ~4 s [Cancel][Render] │
│ ▓▓▓▓▓▓▓░░░░░░░░ progress (full width; shows the error if a render fails) │
│ (foldable) Image | Frequency | Time and dynamics | Reconstruction       │
├────────────────────────────────────────────────────────────────────────┤
│ [Image drop zone / processed preview] │ [Spectrogram + playhead]       │
│                                        │ start/end + loop handles;      │
│                                        │ drag inside start..end = move  │
├──────────────── PLAYBACK (real-time) ──────────────────────────────────┤
│ Envelope: Attack Decay Sustain Release │ Pitch: Transp. Tune [key]│ Output │
│ Sample: Start End X-fade [loop][rev]   │ Filter: LP HP [12/24]   │ Gain   │
│                                        │                         │ [Poly] │
│                                        │                         │ [Drag] │
└────────────────────────────────────────────────────────────────────────┘
```
The dice randomizes the render settings within useful ranges (length, iterations, momentum and peak level stay).
The render settings start folded; unfolding grows the window by their height, and the state is saved in the project.
Sample start never passes the end (markers and knobs); dragging inside the region moves it without changing its length. The loop markers are fractions of the region, so the loop always stays inside it and moves with it.

---

## Architecture

```
image_to_audio_plugin/
  main.py, presets/, data/        ← keep as is (Python reference)
  reference/                      ← NEW: Python helper to export test fixtures
  plugin/
    CMakeLists.txt                ← JUCE via CMake FetchContent; builds VST3 + AU + Standalone
    Source/
      dsp/  (no JUCE GUI dependencies, unit-testable)
        RenderParams.h            ← one struct with all render params + validate() (port of validate_config)
        ImageProcessor.{h,cpp}    ← load_and_process_image
        FrequencyMapper.{h,cpp}   ← build_frequency_axis + apply_frequency_mapping
        MagnitudeBuilder.{h,cpp}  ← image_to_magnitude (+ gaussian spectrum blur)
        GriffinLim.{h,cpp}        ← STFT/ISTFT with Hann window, center=True, fast GL with momentum
        Renderer.{h,cpp}          ← runs the whole pipeline; supports cancel and progress callbacks
      image/HeicLoader.mm         ← macOS ImageIO for HEIC (JUCE can't decode HEIC)
      PluginProcessor.{h,cpp}     ← APVTS params, MIDI → Synthesiser, state save/load
      SamplerVoice.{h,cpp}        ← plays the rendered buffer: pitch, ADSR, loop, reverse, filter
      RenderJob.{h,cpp}           ← background thread (never on the audio thread)
      ui/  ImageView, SpectrogramView, RenderPanel, PlaybackPanel, DragOutButton
    Tests/                        ← compares against the Python fixtures
```

Main points:
- **Threading:** rendering runs on a background `juce::Thread`. When it finishes, the new buffer is swapped in atomically, so the audio thread never blocks. Clicking Render again cancels the job in progress.
- **FFT:** `juce::dsp::FFT` (real-only transform) is enough. With hop 32 and 128 iterations the C++ version should take well under the 3–10 s you saw in Python, but the render-button design keeps working even for long durations.
- **Sample rate:** render at a fixed internal rate (44.1 kHz). The voice resamples to the host rate and the played note with Lagrange/Hermite interpolation.
- **State in the Ableton project:** save the image bytes, render params, real-time params **and the rendered audio** (FLAC-compressed) in the plugin state, so a reopened project plays immediately without re-rendering.
- **Griffin-Lim parity:** with random phase, the audio won't match librosa sample for sample. Parity is checked on the **magnitude spectrogram** (should match within ~1e-4), and the audio is checked with **spectral convergence** against the target magnitude.

---

## Build phases (each ends with something you can run)

0. **Tooling.** Install Xcode + CMake (`brew install cmake`), run `git init`, add a `.gitignore` (venv, build, data/output). Build a "hello" JUCE plugin through CMake and load it in Ableton (VST3 + AU) to prove the toolchain works.
1. **Python reference fixtures.** Add `reference/export_fixtures.py`, which imports the functions from `main.py` (refactored so `main()` reads a params dict instead of globals). For 2–3 test images it dumps the processed pixels, the mapped magnitude and the audio as `.npy`/`.wav`.
2. **C++ DSP core + command-line tool.** Port the functions one by one, test each against its fixture, then build a `render_cli image.png out.wav` tool. Compare the speed with Python.
3. **Minimal instrument.** PluginProcessor + SamplerVoice play a hard-coded rendered buffer from MIDI, using the real-time params through the APVTS generic editor (no custom UI yet).
4. **Render job.** Image drop → background render → atomic swap, with progress and cancel.
5. **Custom UI.** Image preview (live image params), spectrogram view with playhead and loop handles, panels following the layout above, drag-out WAV (`performExternalDragDropOfFiles`).
6. **State & presets.** Save/load the plugin state; convert `presets/*.txt` (Clean, Dark/Texture, Bright/Percussive) into built-in render presets plus saving your own render presets.
7. **Hardening & packaging.** Run `pluginval` and `auval -a`, test in Ableton (multiple instances, project reload, sample-rate change, freeze/bounce). Code signing and notarization come later if you want to share the plugin.

8. **Distribution.** Open source under the AGPLv3 (the license JUCE is used under). Windows and Linux builds: image decoding with JUCE (PNG/JPEG/GIF) plus libheif/libde265 via vcpkg for HEIC, and FFTW for a fast FFT (JUCE has none there); macOS keeps ImageIO and Apple's vDSP FFT. LV2 on Linux, universal macOS binary, version 1.0.0. GitHub Actions builds and tests all three platforms on every push and publishes zips as a GitHub Release for `v*` tags. No Apple notarization yet (users remove the quarantine flag once).

Later options (from the README's roadmap): stereo/panning from image columns, Mel/Bark scales, batch rendering, slicing into multiple bands.

---

## Verification
- **Phase 1–2:** C++ unit tests match the Python fixtures (pixels and magnitude within tolerance; GL spectral convergence ≤ Python's). Run `render_cli` on `reference/test_images/scene_portrait.heic` and A/B it against the WAV `main.py` renders with the same settings, by ear and by the spectrogram PNG.
- **Phase 3+:** Run the Standalone build for quick tests, then load VST3 and AU in Ableton: play MIDI, automate real-time params, render while notes play (no glitches), save and reopen the project (the sound is restored without re-rendering), drag the WAV to an audio track.
- **Release:** `pluginval --strictness-level 5` and `auval -v aumu I2Sd Tlhk` pass on the Release build:
  ```bash
  /Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 5 --validate-in-process --validate ~/Library/Audio/Plug-Ins/VST3/audible_spec.vst3
  /Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 5 --validate-in-process --validate ~/Library/Audio/Plug-Ins/Components/audible_spec.component
  ```
  The AU run prints "Current program is -1": a warning about the host-side preset number of a plugin with a single program, not a failure.

## Release checklist (phase 7, manual in Ableton Live 12)
Build: Release (`plugin/build-release`), installed into `~/Library/Audio/Plug-Ins`. Quit and restart Live first.

| # | Check | Expected |
|---|---|---|
| 1 | Load **VST3** and **AU** version on two MIDI tracks | Both open, play MIDI at the right pitch |
| 2 | **Multiple instances**: 3 tracks, a different image + render on each | Each keeps its own sound; rendering one does not disturb the others |
| 3 | **Render while notes play** (held chord, loop on) | No clicks or dropouts; new notes use the new sound |
| 4 | **Automation**: draw Transpose, Low-pass, Sample Start/End and Filter Slope | Follows smoothly; start after end never breaks playback |
| 5 | **Save project, quit Live, reopen** | Same image, render settings, parameters, fold state; sound plays without rendering |
| 6 | **Sample-rate change** (Preferences → Audio: 44.1 → 48 → 96 kHz) while a note plays | Pitch stays correct, no crash |
| 7 | **Freeze** and **Flatten** a track; **Export audio** of a clip | Frozen/exported audio matches what you hear |
| 8 | **Drag WAV** to an audio track | Clip appears and plays the rendered sound |
| 9 | Close and reopen the plugin window several times, also during a render | No crash; progress continues |
| 10 | Render presets: load each built-in, save one of your own, use the dice | All load; your preset appears under "Your render presets" |

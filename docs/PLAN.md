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

Gamma, contrast, blur, invert and flip are cheap, so the **processed-image preview updates live** while you turn them. Only the audio waits for Render. The UI also shows an estimated render time and duration.

**Real-time parameters** (automatable in Ableton, no re-render):
Output gain · ADSR · Root note / key-tracking on/off · Fine tune · Sample start / end · Loop on/off + loop start/end + crossfade · Reverse · Low-pass / high-pass filter · Velocity → volume amount · Polyphony (mono/poly).

## Rough UI layout (first mockup)
```
┌──────────────────────────────────────────────────────────────────┐
│ [Image drop zone / processed preview] │ [Spectrogram + playhead] │
│                                        │  start/end/loop handles  │
├──────────────── RENDER ────────────────┴──────────────────────────┤
│ Image: Gamma Contrast Blur [Inv][FlipH][FlipV]                   │
│ Freq:  FMin FMax [log|lin] FFT   Time: Duration Hop               │
│ Dyn:   Range Floor Curve  Smooth T/F   GL: Iter Mom Seed          │
│ Preset ▾     ~6.0 s · est. 1.2 s     [ RENDER ] ▓▓▓▓░░ [Cancel]   │
├──────────────── PLAYBACK (real-time) ─────────────────────────────┤
│ A D S R   Root Tune [Key trk]  [Loop][Rev] XFade   LP HP  Gain    │
│                                            [⇩ Drag WAV to track]  │
└──────────────────────────────────────────────────────────────────┘
```

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
6. **State & presets.** Save/load the plugin state; convert `presets/*.txt` (Clean, Dark/Texture, Bright/Percussive) into built-in presets plus user preset saving.
7. **Hardening & packaging.** Run `pluginval` and `auval -a`, test in Ableton (multiple instances, project reload, sample-rate change, freeze/bounce). Code signing and notarization come later if you want to share the plugin.

Later options (from the README's roadmap): stereo/panning from image columns, Mel/Bark scales, batch rendering, slicing into multiple bands.

---

## Verification
- **Phase 1–2:** C++ unit tests match the Python fixtures (pixels and magnitude within tolerance; GL spectral convergence ≤ Python's). Run `render_cli` on `reference/test_images/scene_portrait.heic` and A/B it against the WAV `main.py` renders with the same settings, by ear and by the spectrogram PNG.
- **Phase 3+:** Run the Standalone build for quick tests, then load VST3 and AU in Ableton: play MIDI, automate real-time params, render while notes play (no glitches), save and reopen the project (the sound is restored without re-rendering), drag the WAV to an audio track.
- **Release:** `pluginval --strictness-level 5` and `auval -v aumu ....` pass.

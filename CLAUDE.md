# Image-to-Sound — JUCE instrument plugin

Turns an image into a spectrogram and resynthesises it (Griffin-Lim) into a sample that is played from MIDI inside a DAW (Ableton Live 12, VST3 + AU, macOS).

- `main.py` — the **Python reference implementation**. It defines what "correct" means for the DSP.
- `reference/` — Python scripts that export golden fixtures from `main.py` for the C++ tests.
- `plugin/` — the JUCE/C++ plugin (CMake). `plugin/Source/dsp/` is pure DSP with no GUI dependencies.
- `docs/PLAN.md` — the approved architecture and phase plan. Read it before starting a phase.

The owner is new to C++ and plugins. Explain non-obvious C++/JUCE choices briefly in commit messages or in the reply, not in long code comments.

## Phase gates (the definition of done)

Work happens one phase at a time, on a branch named `phase-N-<slug>`. A phase is done only when **every** gate item is true and the owner has confirmed it in chat. Never start the next phase on your own.

| Phase | Gate (all must hold) |
|---|---|
| 0 Tooling | `cmake` + `clang-format` installed; empty plugin builds as VST3 + AU + Standalone; `auval -v aumu <code> <mfr>` passes; owner loaded it in Ableton |
| 1 Fixtures | `main.py` reads a params dict (behaviour unchanged: same WAV for the same params + seed); `reference/export_fixtures.py` writes fixtures for ≥2 images; fixtures committed |
| 2 DSP core | Every `dsp/` stage has a test against its fixture (pixels/magnitude max abs error ≤ 1e-4); GL spectral convergence ≤ Python's; `render_cli` works on a HEIC; timing vs Python reported |
| 3 Instrument | MIDI plays the buffer at the right pitch; every real-time param is in APVTS and automatable; RT-safety lint clean; no allocations in `processBlock` (checked) |
| 4 Render job | Render runs off the audio thread, can be cancelled, and swaps in without clicks while notes play |
| 5 UI | Layout from `docs/PLAN.md`; live image preview; drag-out WAV works into an Ableton track |
| 6 State/presets | Project save → reopen restores the sound without re-render; 3 built-in presets load |
| 7 Release | `pluginval --strictness-level 5` passes; `auval` passes; manual Ableton checklist in `docs/PLAN.md` done |

At the end of each phase: report the gate items one by one, with evidence (command output), and ask the owner to confirm before merging into `main`.

## Commands

```bash
# configure once (Release for speed tests, Debug for development)
cmake -S plugin -B plugin/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build plugin/build          # also installs VST3 + AU into ~/Library/Audio/Plug-Ins
ctest --test-dir plugin/build --output-on-failure   # C++ DSP vs Python fixtures
auval -v aumu I2Sd Tlhk             # Apple AU validation
# Release build for timing; reuses the JUCE checkout of the debug build
cmake -S plugin -B plugin/build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_JUCE=$PWD/plugin/build/_deps/juce-src
cmake --build plugin/build-release --target RenderCli
plugin/build-release/RenderCli_artefacts/Release/render_cli reference/test_images/scene_portrait.heic data/output/cpp.wav [--set n_fft=2048 ...]
open "plugin/build/ImageToSound_artefacts/Debug/Standalone/Image To Sound.app"
venv/bin/python -m pip install -r requirements-dev.txt   # ruff + pytest
# Python reference (always use the venv)
venv/bin/python main.py
venv/bin/python reference/export_fixtures.py
```

## Workflow rules

- **Hooks are part of the gate.** `.claude/hooks/` formats code, runs the real-time safety lint and runs build + tests before a turn ends. `.githooks/pre-commit` runs the same checks on commit. Never bypass them (`--no-verify`, editing hooks to make them pass, disabling them). If a hook is wrong, say so and propose the fix to the owner.
- **Tests before "done".** Never report a step as working without having run it. If something could not be verified (e.g. it needs Ableton), say so explicitly.
- **Python is the reference.** Do not change DSP behaviour in `main.py` to make C++ tests pass. If the reference is wrong, stop and discuss it.
- **Do not hand-edit generated or source assets:** `data/images/`, `reference/fixtures/` (regenerate with the script), `plugin/build/`, `venv/`.
- **Small commits**, conventional style (`feat(dsp): …`, `test: …`, `fix(ui): …`, `chore: …`). Commit only when the owner asks. Never commit to `main` directly once it has history; never force-push.
- **Dependencies:** JUCE pinned to a tagged version in CMake via FetchContent. Adding any other dependency needs the owner's OK.
- Path-specific rules live in `.claude/rules/` (real-time audio, DSP parity, UI).

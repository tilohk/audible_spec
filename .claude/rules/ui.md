---
paths:
  - "plugin/Source/ui/**"
  - "plugin/Source/PluginEditor.*"
---

# UI rules

- The UI runs on the message thread. It must never call into rendering directly or wait for it: start a `RenderJob` and get progress through an atomic plus a `juce::Timer` (or `AsyncUpdater`).
- Connect controls to parameters only through `APVTS` attachments (`SliderAttachment`, `ButtonAttachment`, `ComboBoxAttachment`). Never store a parameter value in the editor.
- The editor can be destroyed and recreated at any time (Ableton does this). Keep all state in the processor.
- Render parameters and real-time parameters go in visually separate panels, as in the layout in `docs/PLAN.md`. Render params change nothing audible until Render is clicked.
- `paint()` must be cheap: cache the spectrogram and preview images as `juce::Image` and redraw them only when the data changes.

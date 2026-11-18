#pragma once

#include "audio/SamplerEngine.h"
#include "dsp/Matrix.h"
#include "dsp/RenderParams.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>

namespace its
{
// Renders an image to a SampleBuffer on a background thread (never the audio thread).
// Starting a new render cancels the one in progress.
class RenderJob final : private juce::Thread
{
public:
    // What a finished render delivers. On failure `sample` is null and `error` says why.
    struct Output
    {
        std::unique_ptr<SampleBuffer> sample;
        std::shared_ptr<const FloatMatrix>
            spectrogram; // picture for the UI (see display::spectrogramDisplay)
        juce::String error;
    };

    // Called on the render thread when a render finishes (not when it is cancelled).
    using Callback = std::function<void (Output output)>;

    static constexpr int spectrogramRows = 256, spectrogramMaxCols = 1024;

    explicit RenderJob (Callback onFinished);
    ~RenderJob() override;

    // Message thread. Cancels a running render, then starts rendering this image.
    void start (juce::MemoryBlock imageData, const RenderParams& params);
    void cancel();

    float getProgress() const noexcept { return progress.load(); } // 0..1
    bool isRendering() const noexcept { return rendering.load(); }

private:
    void run() override;

    Callback onFinished;
    juce::MemoryBlock pendingImage;
    RenderParams pendingParams;
    std::atomic<float> progress{0.0f};
    std::atomic<bool> rendering{false};
};
} // namespace its

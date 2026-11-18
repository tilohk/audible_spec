#pragma once

#include "audio/SamplerEngine.h"
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
    // Called on the render thread when a render finishes (not when it is cancelled).
    // On failure `sample` is null and `error` says why.
    using Callback = std::function<void (std::unique_ptr<SampleBuffer> sample, const juce::String& error)>;

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

#include "RenderJob.h"

#include "dsp/Renderer.h"
#include "image/ImageDecoder.h"

namespace its
{
RenderJob::RenderJob (Callback callback)
    : juce::Thread ("Image To Sound render"), onFinished (std::move (callback))
{
}

RenderJob::~RenderJob()
{
    cancel();
}

void RenderJob::cancel()
{
    // Griffin-Lim checks threadShouldExit() once per iteration, so this returns quickly.
    stopThread (10000);
    rendering.store (false);
}

void RenderJob::start (juce::MemoryBlock imageData, const RenderParams& params)
{
    cancel();
    pendingImage = std::move (imageData); // safe: the thread is not running
    pendingParams = params;
    progress.store (0.0f);
    rendering.store (true);
    startThread (juce::Thread::Priority::low);
}

void RenderJob::run()
{
    auto finish = [this] (std::unique_ptr<SampleBuffer> sample, const juce::String& error)
    {
        rendering.store (false);
        if (onFinished)
            onFinished (std::move (sample), error);
    };

    if (auto error = pendingParams.validate(); error.isNotEmpty())
        return finish (nullptr, error);

    juce::String error;
    auto decoded = decodeImageData (pendingImage.getData(), pendingImage.getSize(), error);
    if (!decoded)
        return finish (nullptr, error);

    auto result = render (*decoded, pendingParams,
                          [this] (int done, int total)
                          {
                              progress.store ((float)done / (float)juce::jmax (1, total));
                              return !threadShouldExit();
                          });
    if (!result || threadShouldExit())
    {
        rendering.store (false); // cancelled: no callback
        return;
    }

    auto sample = std::make_unique<SampleBuffer>();
    sample->samples = std::move (result->audio);
    sample->sampleRate = pendingParams.sampleRate;
    progress.store (1.0f);
    finish (std::move (sample), {});
}
} // namespace its

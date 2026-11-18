#include "RenderJob.h"

#include "dsp/Renderer.h"
#include "image/ImageDecoder.h"
#include "ui/DisplayData.h"

namespace its
{
RenderJob::RenderJob (Callback callback)
    : juce::Thread ("audible_spec render"), onFinished (std::move (callback))
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
    auto finish = [this] (Output output)
    {
        rendering.store (false);
        if (onFinished)
            onFinished (std::move (output));
    };
    auto fail = [&finish] (const juce::String& error) { finish (Output{nullptr, nullptr, error}); };

    if (auto error = pendingParams.validate(); error.isNotEmpty())
        return fail (error);

    juce::String error;
    auto decoded = decodeImageData (pendingImage.getData(), pendingImage.getSize(), error);
    if (!decoded)
        return fail (error);

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

    Output output;
    output.sample = std::make_unique<SampleBuffer>();
    output.sample->samples = std::move (result->audio);
    output.sample->sampleRate = pendingParams.sampleRate;
    output.spectrogram = std::make_shared<const FloatMatrix> (
        display::spectrogramDisplay (result->magnitude, pendingParams, spectrogramRows, spectrogramMaxCols));
    progress.store (1.0f);
    finish (std::move (output));
}
} // namespace its

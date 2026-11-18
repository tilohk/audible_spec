#include "Renderer.h"

#include "ImageProcessor.h"
#include "Spectrogram.h"

namespace its
{
std::optional<RenderResult> render (const RgbaImage& decoded, const RenderParams& params,
                                    const ProgressCallback& progress)
{
    jassert (params.validate().isEmpty());

    RenderResult r;
    r.resized = resizeForSpectrogram (decoded, params);
    r.pixels = processImage (r.resized, params);
    r.frequencies = buildFrequencyAxis (params);
    r.mapped = applyFrequencyMapping (r.pixels, r.frequencies, params);
    r.magnitude = shapeMagnitude (r.mapped, params);

    // Give the caller a chance to cancel before the expensive stage starts.
    if (progress && !progress (0, params.griffinLimIterations))
        return std::nullopt;

    auto raw = griffinLim (r.magnitude, params, progress);
    if (!raw)
        return std::nullopt;

    r.rawAudio = std::move (*raw);
    r.audio = normalizeAudio (r.rawAudio, params.outputGain);
    return r;
}
} // namespace its

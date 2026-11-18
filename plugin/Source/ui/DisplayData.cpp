#include "DisplayData.h"

#include "dsp/ImageProcessor.h"

#include <algorithm>
#include <cmath>

namespace its::display
{
int widthForDuration (double seconds, const RenderParams& params)
{
    return std::max (16, (int)std::lround (seconds * params.sampleRate / params.hopLength));
}

double estimateRenderSeconds (const RenderParams& params)
{
    const double fftCost = params.nFft * std::log2 ((double)params.nFft) / (4096.0 * 12.0);
    return 0.1 + 2.4 * (params.width / 1024.0) * fftCost * (params.griffinLimIterations / 128.0);
}

FloatMatrix spectrogramDisplay (const FloatMatrix& magnitude, const RenderParams& params, int rows,
                                int maxCols)
{
    const int bins = magnitude.rows, frames = magnitude.cols;
    if (bins < 2 || frames < 1 || rows < 2)
        return {};

    const int cols = std::min (frames, std::max (1, maxCols));
    FloatMatrix out (rows, cols, 0.0f);
    const double nyquist = params.sampleRate / 2.0;
    const double ratio = params.fMax / params.fMin;
    const auto dynamic = (float)params.dynamic;

    for (int r = 0; r < rows; ++r)
    {
        const double f = params.fMin * std::pow (ratio, (double)r / (rows - 1));
        const double bin = std::clamp (f / nyquist * (bins - 1), 0.0, (double)(bins - 1));
        const int b0 = (int)bin, b1 = std::min (b0 + 1, bins - 1);
        const auto t = (float)(bin - b0);

        for (int c = 0; c < cols; ++c)
        {
            const int f0 = (int)((long)c * frames / cols),
                      f1 = std::max (f0 + 1, (int)((long)(c + 1) * frames / cols));
            float peak = 0.0f;
            for (int fr = f0; fr < f1; ++fr)
                peak = std::max (peak, magnitude (b0, fr) * (1.0f - t) + magnitude (b1, fr) * t);
            const float db = 20.0f * std::log10 (std::max (peak, 1e-9f));
            out (r, c) = std::clamp ((db + dynamic) / dynamic, 0.0f, 1.0f);
        }
    }
    return out;
}

ByteImage previewBase (const RgbaImage& image, int maxWidth, int maxHeight)
{
    if (image.width <= 0 || image.height <= 0)
        return {};
    const double scale = std::min ({1.0, (double)maxWidth / image.width, (double)maxHeight / image.height});
    const int w = std::max (1, (int)std::lround (image.width * scale));
    const int h = std::max (1, (int)std::lround (image.height * scale));
    return pil::resizeLanczos (pil::toGrayscale (image), w, h);
}

FloatMatrix previewPixels (const ByteImage& base, const RenderParams& params)
{
    if (base.empty())
        return {};
    auto scaled = params;
    scaled.blur = params.blur * base.rows / (double)params.specHeight(); // blur is in spectrogram pixels
    return processImage (base, scaled);
}
} // namespace its::display

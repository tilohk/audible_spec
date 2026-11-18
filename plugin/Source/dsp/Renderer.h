#pragma once

#include "GriffinLim.h"
#include "Matrix.h"
#include "RenderParams.h"

#include <optional>
#include <vector>

namespace its
{
// Every intermediate stage of one render (mirrors main.RenderResult).
struct RenderResult
{
    ByteImage resized;
    FloatMatrix pixels;
    std::vector<double> frequencies;
    FloatMatrix mapped;
    FloatMatrix magnitude;
    std::vector<float> rawAudio;
    std::vector<float> audio;
};

// main.render without file loading: runs the whole pipeline on a decoded image.
// Returns std::nullopt if `progress` cancelled the Griffin-Lim stage.
// The params must be valid (see RenderParams::validate).
std::optional<RenderResult> render (const RgbaImage& decoded, const RenderParams& params,
                                    const ProgressCallback& progress = {});
} // namespace its

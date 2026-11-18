#pragma once

#include <algorithm>

// Rules for editing the played region (sample start/end, fractions 0..1) from the UI:
// start never passes end, and the region can be moved as a whole without changing its length.
namespace its::region
{
inline constexpr float minLength = 0.01f; // 1 % of the sample

inline float clampStart (float start, float end)
{
    return std::clamp (start, 0.0f, std::max (0.0f, end - minLength));
}

inline float clampEnd (float start, float end)
{
    return std::clamp (end, std::min (1.0f, start + minLength), 1.0f);
}

// How far the region [start, end] can move by `delta` while staying inside 0..1.
inline float limitMove (float start, float end, float delta)
{
    return std::clamp (delta, -start, 1.0f - end);
}
} // namespace its::region

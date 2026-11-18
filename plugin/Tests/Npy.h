#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace its::test
{
// Minimal reader for NumPy .npy files (format v1/v2, little-endian, C order)
// with dtypes |u1, <f4 and <f8. Values are returned as double.
struct NpyArray
{
    std::vector<int> shape;
    juce::String dtype;
    std::vector<double> values;

    int rows() const { return shape.empty() ? 0 : shape[0]; }
    int cols() const { return shape.size() < 2 ? 1 : shape[1]; }
};

// Throws std::runtime_error with a readable message on failure.
NpyArray loadNpy (const juce::File& file);
} // namespace its::test

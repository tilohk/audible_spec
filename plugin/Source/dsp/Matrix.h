#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace its
{
// Row-major 2-D array, laid out like a NumPy array of shape (rows, cols).
// For images: rows = y (top to bottom), cols = x.
// For spectrograms: rows = frequency bins, cols = time frames.
template <typename T> struct Matrix
{
    int rows = 0;
    int cols = 0;
    std::vector<T> data;

    Matrix() = default;
    Matrix (int numRows, int numCols, T initialValue = T{})
        : rows (numRows), cols (numCols), data ((size_t)numRows * (size_t)numCols, initialValue)
    {
    }

    T& operator() (int row, int col) { return data[(size_t)row * (size_t)cols + (size_t)col]; }
    const T& operator() (int row, int col) const { return data[(size_t)row * (size_t)cols + (size_t)col]; }

    T* rowPtr (int row) { return data.data() + (size_t)row * (size_t)cols; }
    const T* rowPtr (int row) const { return data.data() + (size_t)row * (size_t)cols; }

    bool empty() const { return data.empty(); }
};

using ByteImage = Matrix<std::uint8_t>; // 8-bit grayscale, like a PIL "L" image
using FloatMatrix = Matrix<float>;

// 8-bit RGBA pixels, not premultiplied (like PIL "RGBA").
struct RgbaImage
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // width * height * 4, row-major
};
} // namespace its

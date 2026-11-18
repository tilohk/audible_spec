#pragma once

#include "../dsp/Matrix.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace its
{
// Decodes PNG, JPEG, HEIC, TIFF, ... with macOS ImageIO into 8-bit RGBA
// (not premultiplied). Like PIL, it returns the stored pixel values without
// colour management. Orientation metadata (EXIF/HEIF rotation) is not applied.
// Returns std::nullopt and sets `error` on failure.
std::optional<RgbaImage> decodeImageFile (const juce::File& file, juce::String& error);
std::optional<RgbaImage> decodeImageData (const void* data, size_t size, juce::String& error);
} // namespace its

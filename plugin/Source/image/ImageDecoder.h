#pragma once

#include "../dsp/Matrix.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace its
{
// Decodes an image into 8-bit RGBA (not premultiplied). Like PIL, it returns the stored
// pixel values without colour management; orientation metadata (EXIF/HEIF rotation) is
// not applied.
//  - macOS (ImageDecoder.cpp): ImageIO, so PNG, JPEG, HEIC/HEIF, TIFF, BMP, GIF, WebP.
//  - Windows/Linux (ImageDecoderPortable.cpp): PNG, JPEG, GIF (JUCE) and HEIC/HEIF (libheif).
// Returns std::nullopt and sets `error` on failure.
std::optional<RgbaImage> decodeImageFile (const juce::File& file, juce::String& error);
std::optional<RgbaImage> decodeImageData (const void* data, size_t size, juce::String& error);
} // namespace its

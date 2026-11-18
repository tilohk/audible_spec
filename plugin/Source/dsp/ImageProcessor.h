#pragma once

#include "Matrix.h"
#include "RenderParams.h"

namespace its
{
// Bit-exact ports of the Pillow operations used by main.py, plus the
// float stage of main.process_image. All 8-bit functions follow Pillow
// 11.x's C implementation (libImaging) so results match byte for byte.
namespace pil
{
// Image.convert("L"): ITU-R 601 luma, alpha ignored. (libImaging/Convert.c, L24)
ByteImage toGrayscale (const RgbaImage& rgba);

// Image.resize((width, height), Image.Resampling.LANCZOS). (libImaging/Resample.c)
ByteImage resizeLanczos (const ByteImage& in, int outWidth, int outHeight);

// ImageOps.invert
ByteImage invert (const ByteImage& in);

// ImageEnhance.Contrast(img).enhance(factor). (ImageEnhance.py + libImaging/Blend.c)
ByteImage contrast (const ByteImage& in, double factor);

// ImageFilter.GaussianBlur(radius): 3 passes of extended box blur. (libImaging/BoxBlur.c)
ByteImage gaussianBlur (const ByteImage& in, double radius);

// ImageOps.mirror
ByteImage mirror (const ByteImage& in);
} // namespace pil

// main.load_resized_image without the file loading: grayscale + resize to (width, spec height).
ByteImage resizeForSpectrogram (const RgbaImage& decoded, const RenderParams& params);

// main.process_image: invert, contrast, blur, mirror, then float32 0..1, gamma, vertical flip.
FloatMatrix processImage (const ByteImage& resized, const RenderParams& params);
} // namespace its

#pragma once

#include "dsp/Matrix.h"
#include "dsp/RenderParams.h"

// Pure helpers that turn render data into things the UI shows. No GUI code here,
// so they are unit-tested in plugin/Tests.
namespace its::display
{
// Sound length in seconds -> number of STFT frames (image columns), at least 16.
int widthForDuration (double seconds, const RenderParams& params);

// Rough render time on this machine (calibrated: 1024 frames, n_fft 4096, 128 iterations = 2.4 s).
double estimateRenderSeconds (const RenderParams& params);

// Spectrogram picture from the linear-frequency magnitude: `rows` log-spaced rows from
// FMIN to FMAX (row 0 = lowest), at most `maxCols` columns (peak of merged frames),
// values 0..1 over the render's dynamic range.
FloatMatrix spectrogramDisplay (const FloatMatrix& magnitude, const RenderParams& params, int rows,
                                int maxCols);

// The same picture computed from audio (STFT with the render's n_fft/hop), e.g. for a sound
// restored from a project, where the render's magnitude is not available.
FloatMatrix analyseSpectrogram (const std::vector<float>& audio, const RenderParams& params, int rows,
                                int maxCols);

// Grayscale + LANCZOS downscale to fit maxWidth x maxHeight (aspect kept). Done once per image.
ByteImage previewBase (const RgbaImage& image, int maxWidth, int maxHeight);

// The render's image processing applied to the preview (blur scaled to the preview size).
// Same orientation as processImage: row 0 = lowest frequency.
FloatMatrix previewPixels (const ByteImage& base, const RenderParams& params);
} // namespace its::display

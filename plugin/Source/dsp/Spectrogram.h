#pragma once

#include "Matrix.h"
#include "RenderParams.h"

#include <vector>

namespace its
{
// main.build_frequency_axis: np.linspace(0, sr / 2, n_fft // 2 + 1)
std::vector<double> buildFrequencyAxis (const RenderParams& params);

// main.apply_frequency_mapping: maps image rows (FMIN..FMAX, log or linear)
// onto the linear STFT bins; bins outside FMIN..FMAX are silent.
FloatMatrix applyFrequencyMapping (const FloatMatrix& pixels, const std::vector<double>& frequencies,
                                   const RenderParams& params);

// main.shape_magnitude: optional Gaussian smoothing, noise floor, curve,
// then dB range -> linear magnitude.
FloatMatrix shapeMagnitude (const FloatMatrix& mapped, const RenderParams& params);

// scipy.ndimage.gaussian_filter(m, sigma=(sigmaRows, sigmaCols)), mode="reflect", truncate=4.0
FloatMatrix gaussianFilter2D (const FloatMatrix& m, double sigmaRows, double sigmaCols);
} // namespace its

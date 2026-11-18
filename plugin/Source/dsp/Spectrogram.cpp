#include "Spectrogram.h"

#include <algorithm>
#include <cmath>

namespace its
{
namespace
{
// np.linspace(start, stop, num) with endpoint=True
std::vector<double> linspace (double start, double stop, int num)
{
    std::vector<double> y ((size_t)num);
    if (num == 1)
    {
        y[0] = start;
        return y;
    }
    const double step = (stop - start) / (num - 1);
    for (int i = 0; i < num; ++i)
        y[(size_t)i] = i * step + start;
    y[(size_t)num - 1] = stop;
    return y;
}

// np.interp for a single x with left=right=0, increasing xp
double interp (double x, const std::vector<double>& xp, const float* fp, size_t fpStride)
{
    // Exact float comparisons are intentional: they mirror NumPy's implementation.
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
    const size_t n = xp.size();
    if (x > xp[n - 1] || x < xp[0])
        return 0.0;
    if (x == xp[n - 1])
        return fp[(n - 1) * fpStride];

    // binary search: xp[j] <= x < xp[j + 1]
    const auto it = std::upper_bound (xp.begin(), xp.end(), x);
    const size_t j = (size_t)(it - xp.begin()) - 1;
    const double y0 = fp[j * fpStride], y1 = fp[(j + 1) * fpStride];
    if (x == xp[j])
        return y0;
    const double slope = (y1 - y0) / (xp[j + 1] - xp[j]);
    double result = slope * (x - xp[j]) + y0;
    if (std::isnan (result))
    {
        result = slope * (x - xp[j + 1]) + y1;
        if (std::isnan (result) && y0 == y1)
            result = y0;
    }
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE
    return result;
}
} // namespace

std::vector<double> buildFrequencyAxis (const RenderParams& params)
{
    return linspace (0.0, params.sampleRate / 2.0, params.nFft / 2 + 1);
}

FloatMatrix applyFrequencyMapping (const FloatMatrix& pixels, const std::vector<double>& frequencies,
                                   const RenderParams& params)
{
    const int sourceHeight = pixels.rows;
    FloatMatrix target (pixels.rows, pixels.cols, 0.0f); // np.zeros_like(pixels)

    std::vector<size_t> validRows;
    std::vector<double> targetPositions;
    const bool isLog = params.frequencyScale == FrequencyScale::log;
    for (size_t i = 0; i < frequencies.size(); ++i)
    {
        const double f = frequencies[i];
        if (f >= params.fMin && f <= params.fMax)
        {
            validRows.push_back (i);
            targetPositions.push_back (isLog ? std::log (std::max (f, params.fMin)) : f);
        }
    }

    const auto sourcePositions = isLog
                                     ? linspace (std::log (params.fMin), std::log (params.fMax), sourceHeight)
                                     : linspace (params.fMin, params.fMax, sourceHeight);

    for (int column = 0; column < pixels.cols; ++column)
    {
        const float* columnStart = pixels.data.data() + column;
        for (size_t k = 0; k < validRows.size(); ++k)
            target ((int)validRows[k], column) =
                (float)interp (targetPositions[k], sourcePositions, columnStart, (size_t)pixels.cols);
    }
    return target;
}

namespace
{
// scipy.ndimage._filters._gaussian_kernel1d (order 0)
std::vector<double> gaussianKernel (double sigma, int radius)
{
    const double sigma2 = sigma * sigma;
    std::vector<double> phi ((size_t)(2 * radius + 1));
    double sum = 0.0;
    for (int x = -radius; x <= radius; ++x)
    {
        const double v = std::exp (-0.5 / sigma2 * (double)x * (double)x);
        phi[(size_t)(x + radius)] = v;
        sum += v;
    }
    for (auto& v : phi)
        v /= sum;
    return phi;
}

// scipy "reflect" mode (half-sample symmetric): d c b a | a b c d | d c b a
int reflectIndex (int i, int n)
{
    if (n == 1)
        return 0;
    const int period = 2 * n;
    i %= period;
    if (i < 0)
        i += period;
    return i < n ? i : period - 1 - i;
}

// NI_Correlate1D (symmetric kernel path) along one axis; float32 in/out, double accumulation.
void correlateAxis (FloatMatrix& m, const std::vector<double>& weights, bool alongRows)
{
    const int radius = (int)weights.size() / 2;
    const double* fw = weights.data() + radius; // centre
    const int length = alongRows ? m.rows : m.cols;
    const int lines = alongRows ? m.cols : m.rows;

    std::vector<double> line ((size_t)(length + 2 * radius));
    for (int l = 0; l < lines; ++l)
    {
        auto at = [&] (int i) -> float& { return alongRows ? m (i, l) : m (l, i); };
        for (int i = -radius; i < length + radius; ++i)
            line[(size_t)(i + radius)] = (double)at (reflectIndex (i, length));

        for (int i = 0; i < length; ++i)
        {
            const double* iline = line.data() + i + radius;
            double acc = iline[0] * fw[0];
            for (int jj = -radius; jj < 0; ++jj)
                acc += (iline[jj] + iline[-jj]) * fw[jj];
            at (i) = (float)acc;
        }
    }
}
} // namespace

FloatMatrix gaussianFilter2D (const FloatMatrix& m, double sigmaRows, double sigmaCols)
{
    FloatMatrix out = m;
    constexpr double truncate = 4.0;
    // axes are processed in order (0 = rows/frequency, 1 = cols/time); sigma 0 skips the axis
    if (sigmaRows > 1e-15)
        correlateAxis (out, gaussianKernel (sigmaRows, (int)(truncate * sigmaRows + 0.5)), true);
    if (sigmaCols > 1e-15)
        correlateAxis (out, gaussianKernel (sigmaCols, (int)(truncate * sigmaCols + 0.5)), false);
    return out;
}

FloatMatrix shapeMagnitude (const FloatMatrix& mappedIn, const RenderParams& params)
{
    FloatMatrix mapped = mappedIn;
    if (params.spectrumBlurTime > 0 || params.spectrumBlurFreq > 0)
        mapped = gaussianFilter2D (mapped, params.spectrumBlurFreq, params.spectrumBlurTime);

    // NumPy float32 arithmetic with Python-float scalars (NEP 50: scalar cast to float32)
    const float noiseFloor = (float)params.noiseFloor;
    const float divisor = (float)(1.0 - params.noiseFloor);
    const float curve = (float)params.curve;
    const float dynamic = (float)params.dynamic;
    const float negDynamic = (float)-params.dynamic;

    for (auto& v : mapped.data)
    {
        float x = std::clamp (v, 0.0f, 1.0f);
        x = std::max (x - noiseFloor, 0.0f);
        x /= divisor;
        x = std::pow (x, curve);
        const float db = negDynamic + x * dynamic;
        // librosa.db_to_amplitude(db, ref=1.0) = (1.0 * 10 ** (0.1 * db)) ** 0.5
        const float power = 1.0f * std::pow (10.0f, 0.1f * db);
        v = std::sqrt (power);
    }
    return mapped;
}
} // namespace its

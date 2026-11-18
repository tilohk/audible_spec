#include "GriffinLim.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <limits>
#include <random>

namespace its
{
Stft::Stft (int nFftIn, int hopLength)
    : nFft (nFftIn), hop (hopLength),
      fft (std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (nFftIn))))
{
    jassert (juce::isPowerOfTwo (nFft));
    // scipy.signal.get_window("hann", n_fft, fftbins=True): periodic Hann
    window.resize ((size_t)nFft);
    for (int n = 0; n < nFft; ++n)
        window[(size_t)n] = (float)(0.5 - 0.5 * std::cos (2.0 * M_PI * n / nFft));
    fftBuffer.resize ((size_t)nFft * 2);
}

Stft::~Stft() = default;

void Stft::forward (const float* signal, int length, ComplexSpectrogram& out)
{
    const int pad = nFft / 2;
    const int frames = numFramesFor (length);
    out.numBins = numBins();
    out.numFrames = frames;
    out.data.resize ((size_t)frames * (size_t)out.numBins);

    // center=True, pad_mode="constant": zeros on both sides
    padded.assign ((size_t)(length + 2 * pad), 0.0f);
    std::copy (signal, signal + length, padded.begin() + pad);

    for (int f = 0; f < frames; ++f)
    {
        const float* src = padded.data() + (size_t)f * (size_t)hop;
        for (int n = 0; n < nFft; ++n)
            fftBuffer[(size_t)n] = src[n] * window[(size_t)n];
        fft->performRealOnlyForwardTransform (fftBuffer.data(), true);

        auto* dst = out.frame (f);
        for (int k = 0; k < out.numBins; ++k)
            dst[k] = {fftBuffer[(size_t)(2 * k)], fftBuffer[(size_t)(2 * k + 1)]};
    }
}

void Stft::inverse (const ComplexSpectrogram& spec, std::vector<float>& out)
{
    const int frames = spec.numFrames;
    const int pad = nFft / 2;
    const int fullLength = nFft + hop * (frames - 1);

    // Sum of squared windows (librosa.filters.window_sumsquare), cached per frame count
    if (windowSumFrames != frames)
    {
        windowSumSquare.assign ((size_t)fullLength, 0.0f);
        for (int f = 0; f < frames; ++f)
            for (int n = 0; n < nFft; ++n)
                windowSumSquare[(size_t)(f * hop + n)] += window[(size_t)n] * window[(size_t)n];
        windowSumFrames = frames;
    }

    padded.assign ((size_t)fullLength, 0.0f);
    for (int f = 0; f < frames; ++f)
    {
        const auto* src = spec.frame (f);
        for (int k = 0; k < spec.numBins; ++k)
        {
            fftBuffer[(size_t)(2 * k)] = src[k].real();
            fftBuffer[(size_t)(2 * k + 1)] = src[k].imag();
        }
        fft->performRealOnlyInverseTransform (fftBuffer.data()); // scaled by 1/N

        float* dst = padded.data() + (size_t)f * (size_t)hop;
        for (int n = 0; n < nFft; ++n)
            dst[n] += fftBuffer[(size_t)n] * window[(size_t)n];
    }

    constexpr float tiny = std::numeric_limits<float>::min();
    for (int i = 0; i < fullLength; ++i)
        if (windowSumSquare[(size_t)i] > tiny)
            padded[(size_t)i] /= windowSumSquare[(size_t)i];

    // center=True: drop n_fft/2 samples at both ends
    const int length = signalLengthFor (frames);
    out.assign (padded.begin() + pad, padded.begin() + pad + length);
}

double spectralConvergence (const std::vector<float>& audio, const FloatMatrix& magnitude,
                            const RenderParams& params)
{
    Stft stft (params.nFft, params.hopLength);
    ComplexSpectrogram rebuilt;
    stft.forward (audio.data(), (int)audio.size(), rebuilt);

    const int frames = std::min (rebuilt.numFrames, magnitude.cols);
    double diff2 = 0.0, ref2 = 0.0;
    for (int f = 0; f < frames; ++f)
    {
        const auto* fr = rebuilt.frame (f);
        for (int k = 0; k < magnitude.rows; ++k)
        {
            const double m = magnitude (k, f);
            const double d = (double)std::abs (fr[k]) - m;
            diff2 += d * d;
            ref2 += m * m;
        }
    }
    return std::sqrt (diff2) / std::sqrt (ref2);
}

std::optional<std::vector<float>> griffinLim (const FloatMatrix& magnitude, const RenderParams& params,
                                              const ProgressCallback& progress)
{
    const int bins = magnitude.rows, frames = magnitude.cols;
    jassert (bins == params.nFft / 2 + 1);

    Stft stft (params.nFft, params.hopLength);

    // Random initial phase, identical to librosa:
    //   rng = np.random.RandomState(seed); angles = phasor(2*pi*rng.random(S.shape)); angles *= S
    // RandomState(int) seeds the standard MT19937 (same as std::mt19937), and random()
    // builds each double from two 32-bit draws. The array is filled in C order
    // (bin-major, shape = (bins, frames)). phasor is computed in float64, stored as complex64.
    std::mt19937 rng ((std::mt19937::result_type)params.randomSeed);
    auto randomSample = [&rng]
    {
        const auto a = rng() >> 5, b = rng() >> 6;
        return ((double)a * 67108864.0 + (double)b) / 9007199254740992.0;
    };

    ComplexSpectrogram angles;
    angles.numBins = bins;
    angles.numFrames = frames;
    angles.data.resize ((size_t)bins * (size_t)frames);
    for (int k = 0; k < bins; ++k)
        for (int f = 0; f < frames; ++f)
        {
            const double phase = 2.0 * M_PI * randomSample();
            const std::complex<float> unit ((float)std::cos (phase), (float)std::sin (phase));
            angles.frame (f)[k] = unit * magnitude (k, f);
        }

    const float momentumFactor = (float)(params.griffinLimMomentum / (1.0 + params.griffinLimMomentum));
    constexpr float eps = std::numeric_limits<float>::min();

    ComplexSpectrogram rebuilt, previous;
    std::vector<float> inverse;
    bool havePrevious = false;

    for (int iteration = 0; iteration < params.griffinLimIterations; ++iteration)
    {
        stft.inverse (angles, inverse);
        stft.forward (inverse.data(), (int)inverse.size(), rebuilt);

        for (int f = 0; f < frames; ++f)
        {
            auto* a = angles.frame (f);
            const auto* r = rebuilt.frame (f);
            const auto* p = havePrevious ? previous.frame (f) : nullptr;
            for (int k = 0; k < bins; ++k)
            {
                std::complex<float> v = r[k];
                if (p != nullptr)
                    v -= momentumFactor * p[k];
                v /= std::abs (v) + eps;
                a[k] = v * magnitude (k, f);
            }
        }

        std::swap (rebuilt, previous);
        havePrevious = true;

        if (progress && !progress (iteration + 1, params.griffinLimIterations))
            return std::nullopt;
    }

    stft.inverse (angles, inverse);
    return inverse;
}

std::vector<float> normalizeAudio (std::vector<float> audio, double outputGain)
{
    float peak = 0.0f;
    for (auto v : audio)
        peak = std::max (peak, std::abs (v));
    const float gain = (float)outputGain;
    for (auto& v : audio)
    {
        if (peak > 0)
            v = v / peak;
        v *= gain;
    }
    return audio;
}
} // namespace its

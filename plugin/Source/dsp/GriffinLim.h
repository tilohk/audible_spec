#pragma once

#include "Matrix.h"
#include "RenderParams.h"

#include <complex>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace juce::dsp
{
class FFT;
}

namespace its
{
// Complex spectrogram stored frame by frame: index = frame * numBins + bin.
struct ComplexSpectrogram
{
    int numBins = 0;
    int numFrames = 0;
    std::vector<std::complex<float>> data;

    std::complex<float>* frame (int f) { return data.data() + (size_t)f * (size_t)numBins; }
    const std::complex<float>* frame (int f) const { return data.data() + (size_t)f * (size_t)numBins; }
};

// STFT / inverse STFT with the same conventions as librosa.stft / librosa.istft:
// periodic Hann window (win_length = n_fft), center=True with zero padding,
// inverse normalised by the summed squared window.
class Stft
{
public:
    Stft (int nFft, int hopLength);
    ~Stft();

    int numBins() const { return nFft / 2 + 1; }

    // Number of frames for a signal of the given length (center=True).
    int numFramesFor (int signalLength) const { return 1 + signalLength / hop; }

    // Output length of inverse() for the given number of frames.
    int signalLengthFor (int numFrames) const { return hop * (numFrames - 1); }

    void forward (const float* signal, int length, ComplexSpectrogram& out);
    void inverse (const ComplexSpectrogram& spec, std::vector<float>& out);

private:
    int nFft, hop;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, fftBuffer, padded, windowSumSquare;
    int windowSumFrames = -1;
};

// ||(|STFT(audio)| - M)||_F / ||M||_F (lower is better), as in reference/export_fixtures.py
double spectralConvergence (const std::vector<float>& audio, const FloatMatrix& magnitude,
                            const RenderParams& params);

// Called once per iteration with (iterationsDone, totalIterations). Return false to cancel.
using ProgressCallback = std::function<bool (int, int)>;

// librosa.griffinlim(magnitude, n_iter, hop_length, win_length=n_fft, n_fft, window="hann",
// center=True, momentum, init="random", random_state=seed). The random initial phase uses
// std::mt19937, so the audio differs from librosa's but converges the same way.
// Returns std::nullopt if cancelled.
std::optional<std::vector<float>> griffinLim (const FloatMatrix& magnitude, const RenderParams& params,
                                              const ProgressCallback& progress = {});

// main.normalize_audio: peak-normalize, then apply OUTPUT_GAIN.
std::vector<float> normalizeAudio (std::vector<float> audio, double outputGain);
} // namespace its

// Tests for the C++ DSP core against the Python reference fixtures
// (reference/fixtures, written by reference/export_fixtures.py).
//
// Each stage is fed with the *Python* output of the previous stage, so a
// mismatch points at exactly one stage.

#include "Npy.h"

#include "dsp/GriffinLim.h"
#include "dsp/ImageProcessor.h"
#include "dsp/RenderParams.h"
#include "dsp/Renderer.h"
#include "dsp/Spectrogram.h"
#include "image/ImageDecoder.h"

#include <juce_dsp/juce_dsp.h>

#include <random>

namespace its::test
{
namespace
{
const juce::File repoRoot{ITS_REPO_ROOT};

juce::File fixtureRoot()
{
    return repoRoot.getChildFile ("reference/fixtures");
}

// Tolerances from the phase-2 gate (CLAUDE.md / .claude/rules/dsp-parity.md)
constexpr double stageTolerance = 1e-4;

struct Diff
{
    double maxAbs = 0.0;
    double meanAbs = 0.0;
    bool shapeOk = true;
};

template <typename T>
Diff compare (const std::vector<T>& actual, int rows, int cols, const NpyArray& expected)
{
    Diff d;
    d.shapeOk = expected.rows() == rows && expected.cols() == cols && expected.values.size() == actual.size();
    if (!d.shapeOk)
        return d;
    double sum = 0.0;
    for (size_t i = 0; i < actual.size(); ++i)
    {
        const double e = std::abs ((double)actual[i] - expected.values[i]);
        d.maxAbs = std::max (d.maxAbs, e);
        sum += e;
    }
    d.meanAbs = actual.empty() ? 0.0 : sum / (double)actual.size();
    return d;
}

template <typename T> Matrix<T> toMatrix (const NpyArray& a)
{
    Matrix<T> m (a.rows(), a.cols());
    for (size_t i = 0; i < m.data.size(); ++i)
        m.data[i] = (T)a.values[i];
    return m;
}

std::vector<float> toVector (const NpyArray& a)
{
    return {a.values.begin(), a.values.end()};
}

juce::String describe (const Diff& d)
{
    return "max abs " + juce::String (d.maxAbs, 9) + ", mean abs " + juce::String (d.meanAbs, 9);
}
} // namespace

class FftTests final : public juce::UnitTest
{
public:
    FftTests() : juce::UnitTest ("STFT / FFT conventions", "dsp") {}

    void runTest() override
    {
        beginTest ("JUCE real FFT matches a naive DFT (numpy.fft.rfft convention)");
        {
            constexpr int order = 5, n = 1 << order;
            juce::dsp::FFT fft (order);
            std::mt19937 rng (1);
            std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
            std::vector<float> x (n), buf (2 * n);
            for (auto& v : x)
                v = dist (rng);
            std::copy (x.begin(), x.end(), buf.begin());
            fft.performRealOnlyForwardTransform (buf.data(), true);

            double maxErr = 0.0;
            for (int k = 0; k <= n / 2; ++k)
            {
                std::complex<double> ref;
                for (int t = 0; t < n; ++t)
                    ref += (double)x[(size_t)t] * std::polar (1.0, -2.0 * M_PI * k * t / n);
                maxErr = std::max (maxErr, std::abs (ref - std::complex<double> (buf[(size_t)(2 * k)],
                                                                                 buf[(size_t)(2 * k + 1)])));
            }
            logMessage ("  max error vs DFT: " + juce::String (maxErr, 9));
            expectLessThan (maxErr, 1e-4);
        }

        beginTest ("istft(stft(x)) reconstructs x");
        {
            Stft stft (1024, 128);
            std::mt19937 rng (2);
            std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
            std::vector<float> x ((size_t)(128 * 99));
            for (auto& v : x)
                v = dist (rng);

            ComplexSpectrogram spec;
            stft.forward (x.data(), (int)x.size(), spec);
            std::vector<float> y;
            stft.inverse (spec, y);

            expectEquals ((int)y.size(), (int)x.size());
            double maxErr = 0.0;
            for (size_t i = 0; i < std::min (x.size(), y.size()); ++i)
                maxErr = std::max (maxErr, (double)std::abs (x[i] - y[i]));
            logMessage ("  max reconstruction error: " + juce::String (maxErr, 9));
            expectLessThan (maxErr, 1e-5);
        }
    }
};

class FixtureTests final : public juce::UnitTest
{
public:
    FixtureTests() : juce::UnitTest ("Python reference fixtures", "dsp") {}

    void runTest() override
    {
        auto dirs = fixtureRoot().findChildFiles (juce::File::findDirectories, false);
        dirs.sort();

        beginTest ("fixtures exist");
        expectGreaterOrEqual (dirs.size(), 2, "run reference/export_fixtures.py");

        for (auto& dir : dirs)
        {
            try
            {
                runCase (dir);
            }
            catch (const std::exception& e)
            {
                expect (false, dir.getFileName() + ": " + e.what());
            }
        }
    }

private:
    NpyArray stage (const juce::File& dir, const char* name)
    {
        return loadNpy (dir.getChildFile (juce::String (name) + ".npy"));
    }

    void expectStage (const juce::String& label, const Diff& d, double tolerance)
    {
        expect (d.shapeOk, label + ": shape mismatch");
        logMessage ("  " + label + ": " + describe (d));
        expectLessOrEqual (d.maxAbs, tolerance, label);
    }

    void runCase (const juce::File& dir)
    {
        const auto name = dir.getFileName();
        const auto json = juce::JSON::parse (dir.getChildFile ("params.json"));
        const auto metrics = juce::JSON::parse (dir.getChildFile ("metrics.json"));
        const auto params = RenderParams::fromJson (json);

        beginTest (name + ": params");
        expect (params.validate().isEmpty(), params.validate());

        // --- decode + grayscale + LANCZOS resize (from the image file) ---
        beginTest (name + ": decode + grayscale + resize");
        {
            const auto imageFile = repoRoot.getChildFile (json["image_path"].toString());
            juce::String error;
            auto decoded = decodeImageFile (imageFile, error);
            expect (decoded.has_value(), error);
            if (decoded)
            {
                const auto resized = resizeForSpectrogram (*decoded, params);
                const auto d = compare (resized.data, resized.rows, resized.cols, stage (dir, "resized"));
                // PNG decodes losslessly -> must be byte-identical. HEIC is lossy and Apple's
                // decoder may differ from libde265 by a few levels.
                const bool lossy = imageFile.hasFileExtension ("heic;heif");
                // (Also exact with the Windows/Linux decoder: measured 0 levels for the RGBA PNG.)
                expectStage ("resized (8-bit levels)", d, lossy ? 3.0 : 0.0);
            }
        }

        // --- image processing (from Python's resized.npy) ---
        beginTest (name + ": process_image");
        const auto resized = toMatrix<std::uint8_t> (stage (dir, "resized"));
        const auto pixels = processImage (resized, params);
        expectStage ("pixels", compare (pixels.data, pixels.rows, pixels.cols, stage (dir, "pixels")),
                     stageTolerance);

        beginTest (name + ": frequency axis");
        const auto freqs = buildFrequencyAxis (params);
        expectStage ("frequencies (Hz)", compare (freqs, (int)freqs.size(), 1, stage (dir, "frequencies")),
                     1e-9);

        beginTest (name + ": apply_frequency_mapping");
        const auto pyPixels = toMatrix<float> (stage (dir, "pixels"));
        const auto mapped = applyFrequencyMapping (pyPixels, freqs, params);
        expectStage ("mapped", compare (mapped.data, mapped.rows, mapped.cols, stage (dir, "mapped")),
                     stageTolerance);

        beginTest (name + ": shape_magnitude");
        const auto pyMapped = toMatrix<float> (stage (dir, "mapped"));
        const auto magnitude = shapeMagnitude (pyMapped, params);
        expectStage ("magnitude",
                     compare (magnitude.data, magnitude.rows, magnitude.cols, stage (dir, "magnitude")),
                     stageTolerance);

        beginTest (name + ": normalize_audio");
        const auto pyRaw = toVector (stage (dir, "raw_audio"));
        const auto audio = normalizeAudio (pyRaw, params.outputGain);
        expectStage ("audio", compare (audio, (int)audio.size(), 1, stage (dir, "audio")), 1e-6);

        // --- STFT parity: our STFT measures Python's audio like librosa does ---
        beginTest (name + ": STFT matches librosa (spectral convergence of Python's audio)");
        const auto pyMagnitude = toMatrix<float> (stage (dir, "magnitude"));
        const double pySc = (double)metrics["griffin_lim_spectral_convergence"];
        const double scOfPython = spectralConvergence (pyRaw, pyMagnitude, params);
        logMessage ("  Python SC " + juce::String (pySc, 6) + ", measured by C++ STFT " +
                    juce::String (scOfPython, 6));
        expectWithinAbsoluteError (scOfPython, pySc, 1e-4);

        // --- Griffin-Lim: same seed -> same initial phase as librosa (NumPy RandomState) ---
        beginTest (name + ": Griffin-Lim converges at least as well as librosa");
        const auto cppAudio = griffinLim (pyMagnitude, params);
        expect (cppAudio.has_value());
        if (cppAudio)
        {
            expectEquals ((int)cppAudio->size(), (int)pyRaw.size(), "audio length");
            const double sc = spectralConvergence (*cppAudio, pyMagnitude, params);
            logMessage ("  C++ SC " + juce::String (sc, 6) + " vs Python SC " + juce::String (scOfPython, 6) +
                        " (both measured with the C++ STFT)");
            // Same instrument for both; 1e-5 absorbs float32 rounding (FFT libraries differ).
            expectLessOrEqual (sc, scOfPython + 1e-5, "spectral convergence");

            // With an identical start the waveform itself must match librosa's.
            float peak = 0.0f;
            for (auto v : pyRaw)
                peak = std::max (peak, std::abs (v));
            const auto d = compare (*cppAudio, (int)cppAudio->size(), 1, stage (dir, "raw_audio"));
            logMessage ("  waveform vs librosa: max abs error " + juce::String (d.maxAbs / peak * 100.0, 4) +
                        " % of peak");
            expectLessOrEqual (d.maxAbs / peak, 1e-3, "waveform max error relative to peak");
        }
    }
};

class CancelTests final : public juce::UnitTest
{
public:
    CancelTests() : juce::UnitTest ("Griffin-Lim cancel + progress", "dsp") {}

    void runTest() override
    {
        beginTest ("progress is reported and cancel stops early");
        RenderParams p;
        p.nFft = 256;
        p.hopLength = 64;
        p.griffinLimIterations = 20;
        FloatMatrix magnitude (p.specHeight(), 16, 0.5f);

        int calls = 0;
        auto result = griffinLim (magnitude, p,
                                  [&] (int done, int total)
                                  {
                                      ++calls;
                                      expectEquals (total, 20);
                                      return done < 5;
                                  });
        expect (!result.has_value(), "cancelled render returns nullopt");
        expectEquals (calls, 5);

        auto full = griffinLim (magnitude, p);
        expect (full.has_value());
    }
};

static FftTests fftTests;
static FixtureTests fixtureTests;
static CancelTests cancelTests;
} // namespace its::test

#pragma once

#include <juce_core/juce_core.h>

namespace its
{
enum class FrequencyScale
{
    linear,
    log
};

// All parameters that affect rendering. Port of main.py's Config
// (without the file/visualization fields). Defaults = main.py defaults.
struct RenderParams
{
    // Image
    int width = 1024; // number of STFT frames (image columns)
    double gamma = 1.0;
    double blur = 0.0;
    double contrast = 1.5;
    bool invert = true;
    bool flipHorizontal = false;
    bool flipVertical = true;

    // Frequency
    int sampleRate = 44100;
    int nFft = 4096;
    double fMin = 80.0;
    double fMax = 8000.0;
    FrequencyScale frequencyScale = FrequencyScale::log;

    // Time
    int hopLength = 32;

    // Dynamics
    double dynamic = 50.0;
    double noiseFloor = 0.15;
    double curve = 1.5;
    double outputGain = 0.95;

    // Griffin-Lim
    int griffinLimIterations = 128;
    int randomSeed = 35;
    double griffinLimMomentum = 0.8;

    // Spectrogram smoothing (Gaussian sigma in bins / frames)
    double spectrumBlurTime = 0.0;
    double spectrumBlurFreq = 0.0;

    int specHeight() const { return nFft / 2 + 1; }
    double durationSeconds() const { return (double)width * hopLength / sampleRate; }

    // Port of validate_config() (minus the file check). Empty string = valid.
    juce::String validate() const;

    // Reads the snake_case keys written by reference/export_fixtures.py
    // (params.json). Unknown keys are ignored; missing keys keep defaults.
    static RenderParams fromJson (const juce::var& json);

    // The same snake_case keys as fromJson (a juce::DynamicObject).
    juce::var toJson() const;
};
} // namespace its

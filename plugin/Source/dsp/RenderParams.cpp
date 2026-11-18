#include "RenderParams.h"

namespace its
{
juce::String RenderParams::validate() const
{
    // Port of main.validate_config
    if (sampleRate <= 0)
        return "SAMPLE_RATE must be > 0.";
    if (nFft <= 0 || !juce::isPowerOfTwo (nFft))
        return "N_FFT must be a power of two > 0.";
    if (hopLength <= 0)
        return "HOP_LENGTH must be > 0.";
    if (width <= 0)
        return "WIDTH must be > 0.";
    if (fMin <= 0)
        return "FMIN must be > 0.";
    if (fMax > sampleRate / 2.0)
        return "FMAX cannot exceed the Nyquist frequency.";
    if (fMin >= fMax)
        return "FMIN must be smaller than FMAX.";
    if (gamma <= 0)
        return "GAMMA must be > 0.";
    if (dynamic <= 0)
        return "DYNAMIC must be > 0.";
    if (noiseFloor < 0.0 || noiseFloor >= 1.0)
        return "NOISE_FLOOR must be in [0, 1).";
    if (curve <= 0)
        return "CURVE must be > 0.";
    if (griffinLimIterations < 0)
        return "GRIFFIN_LIM_ITERATIONS must be >= 0.";
    return {};
}

RenderParams RenderParams::fromJson (const juce::var& json)
{
    RenderParams p;
    auto get = [&json] (const char* key) { return json.getProperty (key, juce::var()); };
    auto setInt = [&] (const char* key, int& target)
    {
        if (auto v = get (key); !v.isVoid())
            target = (int)v;
    };
    auto setDouble = [&] (const char* key, double& target)
    {
        if (auto v = get (key); !v.isVoid())
            target = (double)v;
    };
    auto setBool = [&] (const char* key, bool& target)
    {
        if (auto v = get (key); !v.isVoid())
            target = (bool)v;
    };

    setInt ("width", p.width);
    setDouble ("gamma", p.gamma);
    setDouble ("blur", p.blur);
    setDouble ("contrast", p.contrast);
    setBool ("invert", p.invert);
    setBool ("flip_horizontal", p.flipHorizontal);
    setBool ("flip_vertical", p.flipVertical);
    setInt ("sample_rate", p.sampleRate);
    setInt ("n_fft", p.nFft);
    setDouble ("fmin", p.fMin);
    setDouble ("fmax", p.fMax);
    if (auto scale = get ("frequency_scale"); !scale.isVoid())
        p.frequencyScale = scale.toString() == "linear" ? FrequencyScale::linear : FrequencyScale::log;
    setInt ("hop_length", p.hopLength);
    setDouble ("dynamic", p.dynamic);
    setDouble ("noise_floor", p.noiseFloor);
    setDouble ("curve", p.curve);
    setDouble ("output_gain", p.outputGain);
    setInt ("griffin_lim_iterations", p.griffinLimIterations);
    setInt ("random_seed", p.randomSeed);
    setDouble ("griffin_lim_momentum", p.griffinLimMomentum);
    setDouble ("spectrum_blur_time", p.spectrumBlurTime);
    setDouble ("spectrum_blur_freq", p.spectrumBlurFreq);
    return p;
}

juce::var RenderParams::toJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("width", width);
    o->setProperty ("gamma", gamma);
    o->setProperty ("blur", blur);
    o->setProperty ("contrast", contrast);
    o->setProperty ("invert", invert);
    o->setProperty ("flip_horizontal", flipHorizontal);
    o->setProperty ("flip_vertical", flipVertical);
    o->setProperty ("sample_rate", sampleRate);
    o->setProperty ("n_fft", nFft);
    o->setProperty ("fmin", fMin);
    o->setProperty ("fmax", fMax);
    o->setProperty ("frequency_scale", frequencyScale == FrequencyScale::linear ? "linear" : "log");
    o->setProperty ("hop_length", hopLength);
    o->setProperty ("dynamic", dynamic);
    o->setProperty ("noise_floor", noiseFloor);
    o->setProperty ("curve", curve);
    o->setProperty ("output_gain", outputGain);
    o->setProperty ("griffin_lim_iterations", griffinLimIterations);
    o->setProperty ("random_seed", randomSeed);
    o->setProperty ("griffin_lim_momentum", griffinLimMomentum);
    o->setProperty ("spectrum_blur_time", spectrumBlurTime);
    o->setProperty ("spectrum_blur_freq", spectrumBlurFreq);
    return juce::var (o);
}
} // namespace its

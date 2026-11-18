#include "Parameters.h"

namespace its::param
{
namespace
{
using Float = juce::AudioParameterFloat;
using Attributes = juce::AudioParameterFloatAttributes;

juce::NormalisableRange<float> timeRange (float maxMs)
{
    juce::NormalisableRange<float> range (0.0f, maxMs, 0.1f);
    range.setSkewForCentre (maxMs / 20.0f); // fine control for short times
    return range;
}

juce::NormalisableRange<float> frequencyRange()
{
    juce::NormalisableRange<float> range (20.0f, 20000.0f, 1.0f);
    range.setSkewForCentre (1000.0f);
    return range;
}

std::unique_ptr<Float> percent (const char* id, const juce::String& name, float defaultValue)
{
    return std::make_unique<Float> (
        juce::ParameterID{id, 1}, name, juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), defaultValue,
        Attributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v * 100.0f, 1); })
            .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue() / 100.0f; }));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Output
    layout.add (std::make_unique<Float> (juce::ParameterID{gain, 1}, "Output Gain",
                                         juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f,
                                         Attributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID{playMode, 1}, "Play Mode",
                                                              juce::StringArray{"Mono", "Poly"}, 1));

    // Envelope
    layout.add (std::make_unique<Float> (juce::ParameterID{attack, 1}, "Attack", timeRange (5000.0f), 5.0f,
                                         Attributes().withLabel ("ms")));
    layout.add (std::make_unique<Float> (juce::ParameterID{decay, 1}, "Decay", timeRange (5000.0f), 200.0f,
                                         Attributes().withLabel ("ms")));
    layout.add (percent (sustain, "Sustain", 1.0f));
    layout.add (std::make_unique<Float> (juce::ParameterID{release, 1}, "Release", timeRange (10000.0f),
                                         200.0f, Attributes().withLabel ("ms")));

    // Pitch
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID{transpose, 1}, "Transpose", -24, 24, 0,
        juce::AudioParameterIntAttributes().withLabel ("st").withStringFromValueFunction (
            [] (int v, int) { return v > 0 ? "+" + juce::String (v) : juce::String (v); })));
    layout.add (
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID{keyTrack, 1}, "Key Tracking", true));
    layout.add (std::make_unique<Float> (juce::ParameterID{fineTune, 1}, "Fine Tune",
                                         juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                                         Attributes().withLabel ("ct")));

    // Sample region (fractions of the rendered sample); loop start/end are fractions of that region
    layout.add (percent (sampleStart, "Sample Start", 0.0f));
    layout.add (percent (sampleEnd, "Sample End", 1.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID{loopOn, 1}, "Loop", false));
    layout.add (percent (loopStart, "Loop Start", 0.0f));
    layout.add (percent (loopEnd, "Loop End", 1.0f));
    layout.add (std::make_unique<Float> (juce::ParameterID{loopCrossfade, 1}, "Loop Crossfade",
                                         timeRange (500.0f), 10.0f, Attributes().withLabel ("ms")));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID{reverse, 1}, "Reverse", false));

    // Filters
    layout.add (std::make_unique<Float> (juce::ParameterID{lowpass, 1}, "Low-pass", frequencyRange(),
                                         20000.0f, Attributes().withLabel ("Hz")));
    layout.add (std::make_unique<Float> (juce::ParameterID{highpass, 1}, "High-pass", frequencyRange(), 20.0f,
                                         Attributes().withLabel ("Hz")));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID{filterSlope, 1}, "Filter Slope", juce::StringArray{"12 dB", "24 dB"}, 0));
    return layout;
}
} // namespace its::param

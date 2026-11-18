#include "PluginProcessor.h"

namespace
{
struct SineSound final : juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SineVoice final : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<SineSound*> (sound) != nullptr;
    }

    // RT-SAFE BEGIN
    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
    {
        level = velocity * 0.25f;
        angle = 0.0;
        const auto hz = juce::MidiMessage::getMidiNoteInHertz (midiNote);
        delta = hz / getSampleRate() * juce::MathConstants<double>::twoPi;

        adsr.setSampleRate (getSampleRate());
        adsr.setParameters ({0.01f, 0.1f, 0.8f, 0.3f});
        adsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
            adsr.noteOff();
        else
        {
            adsr.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (!isVoiceActive())
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto sample = static_cast<float> (std::sin (angle)) * level * adsr.getNextSample();
            for (int ch = 0; ch < output.getNumChannels(); ++ch)
                output.addSample (ch, startSample + i, sample);

            angle += delta;

            if (!adsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }
    // RT-SAFE END

private:
    juce::ADSR adsr;
    double angle = 0.0, delta = 0.0;
    float level = 0.0f;
};
} // namespace

ImageToSoundProcessor::ImageToSoundProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    gainDbParam = apvts.getRawParameterValue ("gain");

    for (int i = 0; i < 8; ++i)
        synth.addVoice (new SineVoice());
    synth.addSound (new SineSound());
}

juce::AudioProcessorValueTreeState::ParameterLayout ImageToSoundProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID{"gain", 1}, "Output Gain", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f),
        -6.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

void ImageToSoundProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    gain.reset (sampleRate, 0.02);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainDbParam->load()));
}

bool ImageToSoundProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

// RT-SAFE BEGIN
void ImageToSoundProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Note: juce::Synthesiser takes an internal lock here. Acceptable for the
    // phase-0 test synth; the phase-3 sampler uses its own lock-free voices.
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    gain.setTargetValue (juce::Decibels::decibelsToGain (gainDbParam->load()));
    gain.applyGain (buffer, buffer.getNumSamples());
}
// RT-SAFE END

juce::AudioProcessorEditor* ImageToSoundProcessor::createEditor()
{
    // Generic slider UI until the custom editor arrives in phase 5.
    return new juce::GenericAudioProcessorEditor (*this);
}

void ImageToSoundProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ImageToSoundProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ImageToSoundProcessor();
}

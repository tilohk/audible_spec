#pragma once

#include "RenderJob.h"
#include "audio/SamplerEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

// The Image To Sound instrument: plays the rendered image as a sampler from MIDI.
// Phase 3: renders the built-in default image on load. Loading your own image and
// the Render button follow in phases 4 and 5.
class ImageToSoundProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    ImageToSoundProcessor();
    ~ImageToSoundProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    void timerCallback() override; // message thread: frees samples the audio thread released
    its::PlaybackSettings readSettings() const noexcept;

    // Raw parameter values, cached once and read lock-free on the audio thread.
    struct ParameterPointers
    {
        std::atomic<float>*gain, *attack, *decay, *sustain, *release, *rootNote, *keyTrack, *fineTune,
            *sampleStart, *sampleEnd, *loopOn, *loopStart, *loopEnd, *loopCrossfade, *reverse, *lowpass,
            *highpass, *velocityAmount, *playMode;
    } p{};

    its::SamplerEngine engine;
    its::RenderJob renderJob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImageToSoundProcessor)
};

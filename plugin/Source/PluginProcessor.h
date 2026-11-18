#pragma once

#include "RenderJob.h"
#include "audio/SamplerEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

// The Image To Sound instrument: renders an image to a sound in the background and
// plays it as a sampler from MIDI.
//
// Render settings are not host parameters (automating them makes no sense); they live
// in the "RENDER" child of the plugin state and are applied when a render starts.
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

    // ---- Rendering (message thread only) ----
    struct RenderStatus
    {
        bool rendering = false;
        float progress = 0.0f; // 0..1
        juce::String message;  // last result or error, for the UI
        bool isError = false;
    };

    // Reads the file and starts rendering it. Returns false (and sets `error`) if unreadable.
    bool loadImageFile (const juce::File& file, juce::String& error);
    void startRender(); // current image + current render settings; cancels a running render
    void cancelRender();
    RenderStatus getRenderStatus() const;
    juce::String getImageName() const { return imageName; }
    juce::ValueTree getRenderSettings(); // "RENDER" child of the state, snake_case properties
    its::RenderParams getRenderParams();

    // Image file types the plugin accepts: wildcards for file choosers, extensions for drag and drop.
    static constexpr const char* imageWildcard =
        "*.png;*.jpg;*.jpeg;*.heic;*.heif;*.tif;*.tiff;*.bmp;*.gif;*.webp";
    static constexpr const char* imageExtensions = "png;jpg;jpeg;heic;heif;tif;tiff;bmp;gif;webp";

private:
    void onRenderFinished (std::unique_ptr<its::SampleBuffer> sample,
                           const juce::String& error); // render thread
    void ensureRenderSettings();
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

    juce::MemoryBlock imageData;              // message thread
    juce::String imageName;                   // message thread
    mutable juce::CriticalSection statusLock; // render thread <-> message thread (never the audio thread)
    juce::String renderingImageName;          // snapshot taken when a render starts
    double renderStartTime = 0.0;
    juce::String statusMessage;
    bool statusIsError = false;

    its::RenderJob renderJob; // last member: destroyed (and joined) first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImageToSoundProcessor)
};

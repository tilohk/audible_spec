#pragma once

#include "RenderJob.h"
#include "audio/SamplerEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

// The audible_spec instrument: renders an image to a sound in the background and
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
    its::RenderParams getRenderParams(); // width derived from the "duration" property (seconds)

    // Resets the render settings to defaults, applies the preset, and says to press Render.
    void applyPreset (const juce::String& presetText);
    juce::String getPresetName(); // stored with the render settings (saved in the project)

    // Sets the visible render settings (except length and the reconstruction quality) to random
    // values within musically useful ranges and clears the preset name. Press Render to hear them.
    void randomizeRenderSettings (juce::Random& random);

    // Whether the editor shows the render settings (UI state, saved in the project).
    bool isRenderPanelOpen() const { return apvts.state.getProperty ("render_panel_open", false); }
    void setRenderPanelOpen (bool open) { apvts.state.setProperty ("render_panel_open", open, nullptr); }

    // ---- Data for the UI (message thread) ----
    struct RenderedData
    {
        std::shared_ptr<const its::FloatMatrix> spectrogram; // display::spectrogramDisplay
        std::shared_ptr<const its::SampleBuffer> audio;      // copy of the last rendered sound
        juce::MemoryBlock encodedSound;                      // the same sound as FLAC, for the project state
        int generation = 0;                                  // increments with every finished render
    };
    RenderedData getRenderedData() const;
    const its::ByteImage& getPreviewBase() const { return previewBase; } // downscaled grayscale image
    int getImageGeneration() const { return imageGeneration; }           // increments when the image changes
    float getPlayheadPosition() const noexcept { return engine.getPlayheadPosition(); }

    // Image file types the plugin accepts: wildcards for file choosers, extensions for drag and drop.
    static constexpr const char* imageWildcard =
        "*.png;*.jpg;*.jpeg;*.heic;*.heif;*.tif;*.tiff;*.bmp;*.gif;*.webp";
    static constexpr const char* imageExtensions = "png;jpg;jpeg;heic;heif;tif;tiff;bmp;gif;webp";

private:
    void onRenderFinished (its::RenderJob::Output output); // render thread
    void ensureRenderSettings();
    static void ensureRenderSettingsIn (juce::ValueTree& state);
    void setImage (juce::MemoryBlock data, const juce::String& name); // updates preview too
    bool restoreSound (const juce::MemoryBlock& flac);                // project load: play without render
    void timerCallback() override; // message thread: frees samples the audio thread released
    its::PlaybackSettings readSettings() const noexcept;

    // Raw parameter values, cached once and read lock-free on the audio thread.
    struct ParameterPointers
    {
        std::atomic<float>*gain, *attack, *decay, *sustain, *release, *transpose, *keyTrack, *fineTune,
            *sampleStart, *sampleEnd, *loopOn, *loopStart, *loopEnd, *loopCrossfade, *reverse, *lowpass,
            *highpass, *filterSlope, *playMode;
    } p{};

    its::SamplerEngine engine;

    mutable juce::CriticalSection
        imageLock;               // image data/name may be read by getStateInformation on any thread
    juce::MemoryBlock imageData; // written on the message thread under imageLock
    juce::String imageName;      // written on the message thread under imageLock
    its::ByteImage previewBase;  // message thread
    int imageGeneration = 0;     // message thread
    RenderedData rendered;       // guarded by statusLock
    mutable juce::CriticalSection statusLock; // render thread <-> message thread (never the audio thread)
    juce::String renderingImageName;          // snapshot taken when a render starts
    double renderStartTime = 0.0;
    juce::String statusMessage;
    bool statusIsError = false;

    its::RenderJob renderJob; // last member: destroyed (and joined) first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImageToSoundProcessor)
};

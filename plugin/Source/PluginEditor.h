#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Phase-4 working UI: load/drop an image, render settings, Render/Cancel with progress,
// and the generic sliders for the real-time parameters. Replaced by the designed UI in phase 5.
class ImageToSoundEditor final : public juce::AudioProcessorEditor,
                                 public juce::FileDragAndDropTarget,
                                 private juce::Timer
{
public:
    explicit ImageToSoundEditor (ImageToSoundProcessor& processor);
    ~ImageToSoundEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void chooseImage();
    void loadImage (const juce::File& file);
    void buildRenderSettingsPanel();

    ImageToSoundProcessor& owner;

    juce::TextButton loadButton{"Load image..."}, renderButton{"Render"}, cancelButton{"Cancel"};
    juce::Label imageLabel, statusLabel, durationLabel;
    juce::Label renderHeading{{}, "Render settings (applied when you press Render)"};
    juce::Label playbackHeading{{}, "Playback (real-time, automatable)"};
    double progressValue = 0.0;
    juce::ProgressBar progressBar{progressValue};
    juce::PropertyPanel renderPanel;
    juce::GenericAudioProcessorEditor playbackPanel;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool dragHighlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImageToSoundEditor)
};

#pragma once

#include "PluginProcessor.h"
#include "ui/Panels.h"
#include "ui/Theme.h"
#include "ui/Views.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// audible_spec editor (layout agreed in phase 5): header, image preview + spectrogram,
// render settings with Render/Cancel, playback controls with drag-out.
class ImageToSoundEditor final : public juce::AudioProcessorEditor,
                                 public juce::FileDragAndDropTarget,
                                 private juce::Timer,
                                 private juce::ValueTree::Listener
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

    // Runs one UI refresh immediately (used by the snapshot tool and tests).
    void refreshNow() { timerCallback(); }

    // Folds/unfolds the render settings; the window shrinks/grows by their height.
    void setRenderPanelOpen (bool open);

private:
    static constexpr int margin = 14, headerHeight = 30, gap = 12, playbackHeight = 236;
    static constexpr int minPreviewHeight = 170, defaultPreviewHeight = 230;
    int fixedHeight() const; // everything except the previews
    void updateSizeLimits();

    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void chooseImage();
    void loadImage (const juce::File& file);
    void bindRenderSettings();
    juce::File writeDragFile();
    void rebuildPresetMenu();
    void presetChosen();
    void savePresetDialog();

    ImageToSoundProcessor& owner;
    its::ui::LookAndFeel lookAndFeel; // declared first: outlives every child component

    juce::Label title{{}, "audible_spec"}, imageLabel;
    juce::TextButton loadButton{"Load image"};
    juce::Random random;                   // seeded from the clock: every click gives new settings
    std::vector<juce::String> presetTexts; // render presets; index = combo item id - 1
    its::ui::ImagePreview preview;
    its::ui::SpectrogramView spectrogram;
    its::ui::RenderPanel renderPanel;
    its::ui::PlaybackPanel playbackPanel;
    juce::TooltipWindow tooltips{this};
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::ValueTree renderSettings;
    int shownImageGeneration = -1, shownRenderGeneration = -1;
    int previewDirtyTicks = -1; // > 0: recompute preview after this many ticks (debounce)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImageToSoundEditor)
};

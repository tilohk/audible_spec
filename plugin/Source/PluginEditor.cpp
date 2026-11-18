#include "PluginEditor.h"

#include "Presets.h"
#include "ui/DisplayData.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace colours = its::ui::colours;

ImageToSoundEditor::ImageToSoundEditor (ImageToSoundProcessor& p)
    : juce::AudioProcessorEditor (p), owner (p), spectrogram (p.apvts), playbackPanel (p.apvts)
{
    title.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, colours::text);
    imageLabel.setFont (juce::FontOptions (13.0f));
    imageLabel.setColour (juce::Label::textColourId, colours::textSecondary);
    for (auto* c :
         std::initializer_list<juce::Component*>{&title, &imageLabel, &loadButton, &presetLabel, &presetBox,
                                                 &preview, &spectrogram, &renderPanel, &playbackPanel})
        addAndMakeVisible (c);

    presetLabel.setFont (juce::FontOptions (13.0f));
    presetLabel.setColour (juce::Label::textColourId, colours::textSecondary);
    presetLabel.setJustificationType (juce::Justification::centredRight);
    presetBox.setTextWhenNothingSelected ("Choose a preset");
    presetBox.onChange = [this] { presetChosen(); };
    rebuildPresetMenu();

    loadButton.onClick = [this] { chooseImage(); };
    preview.onClick = [this] { chooseImage(); };
    renderPanel.onRender = [this] { owner.startRender(); };
    renderPanel.onCancel = [this] { owner.cancelRender(); };
    playbackPanel.onDragOut = [this] { return writeDragFile(); };

    // Set after all children exist: this rebuilds their value boxes etc. with the theme.
    setLookAndFeel (&lookAndFeel);

    bindRenderSettings();
    setResizable (true, true);
    setResizeLimits (900, 680, 1800, 1300);
    setSize (980, 720);
    timerCallback();
    startTimerHz (30);
}

ImageToSoundEditor::~ImageToSoundEditor()
{
    stopTimer();
    renderSettings.removeListener (this);
    setLookAndFeel (nullptr);
}

void ImageToSoundEditor::bindRenderSettings()
{
    renderSettings.removeListener (this);
    renderSettings = owner.getRenderSettings();
    renderSettings.addListener (this);
    renderPanel.bind (renderSettings);
    previewDirtyTicks = 1;
}

void ImageToSoundEditor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    previewDirtyTicks = 3; // ~0.1 s after the last change
}

void ImageToSoundEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void ImageToSoundEditor::resized()
{
    auto area = getLocalBounds().reduced (14);

    auto header = area.removeFromTop (30);
    title.setBounds (header.removeFromLeft (150));
    loadButton.setBounds (header.removeFromRight (120).reduced (0, 2));
    header.removeFromRight (16);
    presetBox.setBounds (header.removeFromRight (210).reduced (0, 2));
    presetLabel.setBounds (header.removeFromRight (56));
    imageLabel.setBounds (header);
    area.removeFromTop (10);

    const int previewHeight = juce::jmax (180, (area.getHeight() - 30) * 30 / 100);
    auto previews = area.removeFromTop (previewHeight);
    preview.setBounds (previews.removeFromLeft (previewHeight * 4 / 3));
    previews.removeFromLeft (12);
    spectrogram.setBounds (previews);
    area.removeFromTop (12);

    playbackPanel.setBounds (area.removeFromBottom (140));
    area.removeFromBottom (12);
    renderPanel.setBounds (area);
}

void ImageToSoundEditor::timerCallback()
{
    if (owner.getRenderSettings() != renderSettings) // host loaded a different state
        bindRenderSettings();

    const auto params = owner.getRenderParams();
    const auto problem = params.validate();
    const auto status = owner.getRenderStatus();
    const auto estimate =
        problem.isEmpty() ? juce::String (params.durationSeconds(), 2) + " s sound, ~" +
                                juce::String (its::display::estimateRenderSeconds (params), 1) + " s render"
                          : juce::String ("Check settings");
    renderPanel.setStatus (status.rendering, status.progress,
                           problem.isEmpty() ? status.message : "Invalid settings: " + problem,
                           status.isError || problem.isNotEmpty(), estimate);
    imageLabel.setText (owner.getImageName(), juce::dontSendNotification);

    // New render -> new spectrogram picture.
    const auto rendered = owner.getRenderedData();
    if (rendered.generation != shownRenderGeneration && rendered.spectrogram != nullptr)
    {
        shownRenderGeneration = rendered.generation;
        spectrogram.setSpectrogram (*rendered.spectrogram);
    }
    spectrogram.setPlayhead (owner.getPlayheadPosition());
    spectrogram.setInfo (
        rendered.audio != nullptr
            ? juce::String ((double)rendered.audio->samples.size() / rendered.audio->sampleRate, 2) + " s"
            : juce::String());
    spectrogram.repaint(); // markers follow automation

    // Live preview of the image processing (debounced).
    if (owner.getImageGeneration() != shownImageGeneration)
    {
        shownImageGeneration = owner.getImageGeneration();
        previewDirtyTicks = 1;
    }
    if (previewDirtyTicks > 0 && --previewDirtyTicks == 0)
        preview.setPixels (its::display::previewPixels (owner.getPreviewBase(), params));
}

juce::File ImageToSoundEditor::writeDragFile()
{
    const auto rendered = owner.getRenderedData();
    if (rendered.audio == nullptr || rendered.audio->samples.empty())
        return {};

    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Image To Sound");
    folder.createDirectory();
    const auto base =
        juce::File::createLegalFileName (owner.getImageName().upToLastOccurrenceOf (".", false, false));
    auto file =
        folder.getChildFile ((base.isEmpty() ? juce::String ("image") : base) + " - image to sound.wav");
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (rendered.audio->sampleRate)
                                                   .withNumChannels (1)
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
        return {};
    const float* channel = rendered.audio->samples.data();
    writer->writeFromFloatArrays (&channel, 1, (int)rendered.audio->samples.size());
    writer.reset();
    return file;
}

void ImageToSoundEditor::chooseImage()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Choose an image", juce::File(),
                                                       ImageToSoundProcessor::imageWildcard);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  if (auto file = chooser.getResult(); file.existsAsFile())
                                      loadImage (file);
                              });
}

void ImageToSoundEditor::loadImage (const juce::File& file)
{
    juce::String error;
    if (!owner.loadImageFile (file, error))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Image to sound",
                                                error);
}

bool ImageToSoundEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 &&
           juce::File (files[0]).hasFileExtension (ImageToSoundProcessor::imageExtensions);
}

void ImageToSoundEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    preview.setDropHighlight (true);
}

void ImageToSoundEditor::fileDragExit (const juce::StringArray&)
{
    preview.setDropHighlight (false);
}

void ImageToSoundEditor::filesDropped (const juce::StringArray& files, int, int)
{
    preview.setDropHighlight (false);
    if (!files.isEmpty())
        loadImage (juce::File (files[0]));
}

// ---------------------------------------------------------------- presets

namespace
{
constexpr int saveItemId = 10000;
}

void ImageToSoundEditor::rebuildPresetMenu()
{
    presetBox.clear (juce::dontSendNotification);
    presetTexts.clear();

    auto addSection = [this] (const juce::String& heading, const std::vector<its::presets::Preset>& list)
    {
        if (list.empty())
            return;
        presetBox.addSectionHeading (heading);
        for (auto& p : list)
        {
            presetTexts.push_back (p.text);
            presetBox.addItem (p.name, (int)presetTexts.size());
        }
    };
    addSection ("Built-in", its::presets::builtIns());
    addSection ("Your presets", its::presets::userPresets());
    presetBox.addSeparator();
    presetBox.addItem ("Save current settings as preset...", saveItemId);

    // Show the last applied preset, if it is still in the list.
    for (int i = 0; i < presetBox.getNumItems(); ++i)
        if (presetBox.getItemText (i) == owner.getPresetName())
            presetBox.setSelectedItemIndex (i, juce::dontSendNotification);
}

void ImageToSoundEditor::presetChosen()
{
    const int id = presetBox.getSelectedId();
    if (id == saveItemId)
    {
        rebuildPresetMenu(); // put the selection back to the current preset
        savePresetDialog();
        return;
    }
    if (id >= 1 && id <= (int)presetTexts.size())
        owner.applyPreset (presetTexts[(size_t)id - 1]);
}

void ImageToSoundEditor::savePresetDialog()
{
    auto* window = new juce::AlertWindow (
        "Save preset", "Name for the current render settings:", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", owner.getPresetName().isNotEmpty() ? owner.getPresetName() : "My preset");
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<ImageToSoundEditor> safeThis (this);
    window->enterModalState (
        true,
        juce::ModalCallbackFunction::create (
            [safeThis, window] (int result)
            {
                if (safeThis == nullptr || result != 1)
                    return;
                juce::String error;
                const auto name = window->getTextEditorContents ("name").trim();
                if (!its::presets::saveUserPreset (safeThis->owner.getRenderSettings(), name, error))
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            "Save preset", error);
                    return;
                }
                safeThis->rebuildPresetMenu();
                for (int i = 0; i < safeThis->presetBox.getNumItems(); ++i)
                    if (safeThis->presetBox.getItemText (i) == name)
                        safeThis->presetBox.setSelectedItemIndex (i, juce::dontSendNotification);
            }),
        true);
}

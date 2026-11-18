#include "PluginEditor.h"

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
    for (auto* c : std::initializer_list<juce::Component*>{&title, &imageLabel, &loadButton, &preview,
                                                           &spectrogram, &renderPanel, &playbackPanel})
        addAndMakeVisible (c);

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

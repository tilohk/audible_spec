#include "PluginEditor.h"

ImageToSoundEditor::ImageToSoundEditor (ImageToSoundProcessor& p)
    : juce::AudioProcessorEditor (p), owner (p), playbackPanel (p)
{
    for (auto* c : std::initializer_list<juce::Component*>{
             &loadButton, &renderButton, &cancelButton, &imageLabel, &statusLabel, &durationLabel,
             &renderHeading, &playbackHeading, &progressBar, &renderPanel, &playbackPanel})
        addAndMakeVisible (c);

    loadButton.onClick = [this] { chooseImage(); };
    renderButton.onClick = [this] { owner.startRender(); };
    cancelButton.onClick = [this] { owner.cancelRender(); };

    for (auto* heading : {&renderHeading, &playbackHeading})
        heading->setFont (juce::FontOptions (15.0f, juce::Font::bold));
    imageLabel.setFont (juce::FontOptions (15.0f));
    durationLabel.setJustificationType (juce::Justification::centredRight);

    buildRenderSettingsPanel();
    setSize (1000, 700);
    setResizable (true, true);
    setResizeLimits (800, 500, 2000, 1600);
    timerCallback();
    startTimerHz (15);
}

ImageToSoundEditor::~ImageToSoundEditor()
{
    stopTimer();
}

void ImageToSoundEditor::buildRenderSettingsPanel()
{
    auto tree = owner.getRenderSettings();
    auto v = [&tree] (const char* id) { return tree.getPropertyAsValue (id, nullptr); };
    using Slider = juce::SliderPropertyComponent;
    using Choice = juce::ChoicePropertyComponent;
    using Toggle = juce::BooleanPropertyComponent;

    juce::Array<juce::PropertyComponent*> image, frequency, time, dynamics, reconstruction;
    image.add (new Slider (v ("gamma"), "Gamma", 0.1, 4.0, 0.01, 0.5));
    image.add (new Slider (v ("contrast"), "Contrast", 0.0, 4.0, 0.01));
    image.add (new Slider (v ("blur"), "Blur (px)", 0.0, 10.0, 0.1));
    image.add (new Toggle (v ("invert"), "Invert", "Invert brightness"));
    image.add (new Toggle (v ("flip_horizontal"), "Flip horizontal", "Play backwards in time"));
    image.add (new Toggle (v ("flip_vertical"), "Flip vertical", "Top of image = high pitch"));

    frequency.add (new Slider (v ("fmin"), "Lowest frequency (Hz)", 20.0, 2000.0, 1.0, 0.4));
    frequency.add (new Slider (v ("fmax"), "Highest frequency (Hz)", 500.0, 22050.0, 1.0, 0.4));
    frequency.add (
        new Choice (v ("frequency_scale"), "Frequency scale", {"Logarithmic", "Linear"}, {"log", "linear"}));
    frequency.add (
        new Choice (v ("n_fft"), "FFT size", {"1024", "2048", "4096", "8192"}, {1024, 2048, 4096, 8192}));

    time.add (new Slider (v ("width"), "Length (frames)", 16.0, 8192.0, 1.0, 0.5));
    time.add (new Choice (v ("hop_length"), "Hop length", {"16", "32", "64", "128", "256", "512"},
                          {16, 32, 64, 128, 256, 512}));

    dynamics.add (new Slider (v ("dynamic"), "Dynamic range (dB)", 10.0, 120.0, 0.5));
    dynamics.add (new Slider (v ("noise_floor"), "Noise floor", 0.0, 0.95, 0.01));
    dynamics.add (new Slider (v ("curve"), "Curve", 0.1, 4.0, 0.01, 0.5));
    dynamics.add (new Slider (v ("spectrum_blur_time"), "Smoothing time (frames)", 0.0, 20.0, 0.1));
    dynamics.add (new Slider (v ("spectrum_blur_freq"), "Smoothing frequency (bins)", 0.0, 20.0, 0.1));
    dynamics.add (new Slider (v ("output_gain"), "Normalised peak", 0.05, 1.0, 0.01));

    reconstruction.add (
        new Slider (v ("griffin_lim_iterations"), "Griffin-Lim iterations", 1.0, 512.0, 1.0, 0.5));
    reconstruction.add (new Slider (v ("griffin_lim_momentum"), "Momentum", 0.0, 0.99, 0.01));
    reconstruction.add (new Slider (v ("random_seed"), "Random seed", 0.0, 9999.0, 1.0));

    renderPanel.addSection ("Image", image);
    renderPanel.addSection ("Frequency", frequency);
    renderPanel.addSection ("Time", time);
    renderPanel.addSection ("Dynamics", dynamics);
    renderPanel.addSection ("Reconstruction", reconstruction);
}

void ImageToSoundEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    if (dragHighlight)
    {
        g.setColour (juce::Colours::orange);
        g.drawRect (getLocalBounds(), 4);
    }
}

void ImageToSoundEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto top = area.removeFromTop (32);
    loadButton.setBounds (top.removeFromLeft (130));
    top.removeFromLeft (8);
    cancelButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (8);
    renderButton.setBounds (top.removeFromRight (110));
    top.removeFromRight (8);
    imageLabel.setBounds (top);

    area.removeFromTop (8);
    auto progressRow = area.removeFromTop (22);
    durationLabel.setBounds (progressRow.removeFromRight (230));
    progressBar.setBounds (progressRow);
    area.removeFromTop (4);
    statusLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);

    auto left = area.removeFromLeft (area.getWidth() / 2 - 6);
    area.removeFromLeft (12);
    renderHeading.setBounds (left.removeFromTop (24));
    renderPanel.setBounds (left);
    playbackHeading.setBounds (area.removeFromTop (24));
    playbackPanel.setBounds (area);
}

void ImageToSoundEditor::timerCallback()
{
    const auto status = owner.getRenderStatus();
    progressValue = status.rendering ? (double)status.progress : (status.isError ? 0.0 : 1.0);
    renderButton.setEnabled (!status.rendering);
    cancelButton.setEnabled (status.rendering);
    statusLabel.setText (status.message, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId,
                           status.isError ? juce::Colours::orangered
                                          : getLookAndFeel().findColour (juce::Label::textColourId));
    imageLabel.setText ("Image: " + owner.getImageName() + "   (drop an image file here)",
                        juce::dontSendNotification);

    const auto params = owner.getRenderParams();
    const auto problem = params.validate();
    durationLabel.setText (problem.isEmpty()
                               ? "Sound length: " + juce::String (params.durationSeconds(), 2) + " s"
                               : "Invalid: " + problem,
                           juce::dontSendNotification);
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
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Image To Sound",
                                                error);
}

bool ImageToSoundEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 &&
           juce::File (files[0]).hasFileExtension (ImageToSoundProcessor::imageExtensions);
}

void ImageToSoundEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    dragHighlight = true;
    repaint();
}

void ImageToSoundEditor::fileDragExit (const juce::StringArray&)
{
    dragHighlight = false;
    repaint();
}

void ImageToSoundEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHighlight = false;
    repaint();
    if (!files.isEmpty())
        loadImage (juce::File (files[0]));
}

#include "Panels.h"

#include "Parameters.h"
#include "Theme.h"

namespace its::ui
{
namespace
{
void styleHeading (juce::Label& label)
{
    label.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, colours::text);
}

void styleSecondary (juce::Label& label, float size = 12.5f)
{
    label.setFont (juce::FontOptions (size));
    label.setColour (juce::Label::textColourId, colours::textSecondary);
}
} // namespace

// ================================================================== RenderPanel

struct RenderPanel::Row final : public juce::Component, private juce::Value::Listener
{
    enum class Kind
    {
        slider,
        choice,
        toggles
    };

    explicit Row (Kind k) : kind (k)
    {
        styleSecondary (label);
        addAndMakeVisible (label);
        if (kind == Kind::slider)
        {
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18);
            addAndMakeVisible (slider);
        }
        else if (kind == Kind::choice)
        {
            combo.onChange = [this]
            {
                const int index = combo.getSelectedItemIndex();
                if (juce::isPositiveAndBelow (index, choiceValues.size()))
                    choiceValue = choiceValues[index];
            };
            addAndMakeVisible (combo);
            choiceValue.addListener (this);
        }
    }

    ~Row() override { choiceValue.removeListener (this); }

    void bind (juce::ValueTree& settings)
    {
        if (kind == Kind::slider)
            slider.getValueObject().referTo (settings.getPropertyAsValue (id, nullptr));
        else if (kind == Kind::choice)
        {
            choiceValue.referTo (settings.getPropertyAsValue (id, nullptr));
            valueChanged (choiceValue);
        }
        else
            for (size_t i = 0; i < toggleButtons.size(); ++i)
                toggleButtons[i]->getToggleStateValue().referTo (
                    settings.getPropertyAsValue (toggleIds[i], nullptr));
    }

    void valueChanged (juce::Value&) override
    {
        for (int i = 0; i < choiceValues.size(); ++i)
            if (choiceValues[i] == choiceValue.getValue())
                combo.setSelectedItemIndex (i, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        if (kind == Kind::toggles)
        {
            const int w = area.getWidth() / juce::jmax (1, (int)toggleButtons.size());
            for (auto& t : toggleButtons)
                t->setBounds (area.removeFromLeft (w).reduced (2, 2));
            return;
        }
        label.setBounds (area.removeFromLeft (juce::jmin (84, area.getWidth() / 3)));
        (kind == Kind::slider ? (juce::Component&)slider : (juce::Component&)combo)
            .setBounds (area.reduced (0, 2));
    }

    Kind kind;
    juce::Identifier id{"unset"};
    juce::Label label;
    juce::Slider slider;
    juce::ComboBox combo;
    juce::Array<juce::var> choiceValues;
    juce::Value choiceValue;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggleButtons;
    std::vector<juce::Identifier> toggleIds;
};

RenderPanel::Row& RenderPanel::addSlider (Group& group, const char* id, const juce::String& name, double min,
                                          double max, double step, double skewCentre,
                                          const juce::String& suffix)
{
    auto row = std::make_unique<Row> (Row::Kind::slider);
    row->id = id;
    row->label.setText (name, juce::dontSendNotification);
    row->slider.setRange (min, max, step);
    if (skewCentre > 0.0)
        row->slider.setSkewFactorFromMidPoint (skewCentre);
    row->slider.setTextValueSuffix (suffix);
    group.rows.push_back (row.get());
    addAndMakeVisible (*row);
    rows.push_back (std::move (row));
    return *rows.back();
}

RenderPanel::Row& RenderPanel::addChoice (Group& group, const char* id, const juce::String& name,
                                          juce::StringArray labels, juce::Array<juce::var> values)
{
    auto row = std::make_unique<Row> (Row::Kind::choice);
    row->id = id;
    row->label.setText (name, juce::dontSendNotification);
    row->combo.addItemList (labels, 1);
    row->choiceValues = values;
    group.rows.push_back (row.get());
    addAndMakeVisible (*row);
    rows.push_back (std::move (row));
    return *rows.back();
}

RenderPanel::Row& RenderPanel::addToggles (Group& group,
                                           std::vector<std::pair<const char*, juce::String>> toggles)
{
    auto row = std::make_unique<Row> (Row::Kind::toggles);
    for (auto& [id, name] : toggles)
    {
        row->toggleIds.emplace_back (id);
        row->toggleButtons.push_back (std::make_unique<juce::ToggleButton> (name));
        row->addAndMakeVisible (*row->toggleButtons.back());
    }
    group.rows.push_back (row.get());
    addAndMakeVisible (*row);
    rows.push_back (std::move (row));
    return *rows.back();
}

RenderPanel::RenderPanel()
{
    groups.resize (4);
    auto& image = groups[0];
    auto& frequency = groups[1];
    auto& time = groups[2];
    auto& reconstruction = groups[3];
    image.title = "Image";
    frequency.title = "Frequency";
    time.title = "Time and dynamics";
    reconstruction.title = "Reconstruction";

    addSlider (image, "gamma", "Gamma", 0.1, 4.0, 0.01, 1.0);
    addSlider (image, "contrast", "Contrast", 0.0, 4.0, 0.01);
    addSlider (image, "blur", "Blur", 0.0, 10.0, 0.1, 0.0, " px");
    addToggles (image, {{"invert", "Invert"}, {"flip_horizontal", "Flip H"}, {"flip_vertical", "Flip V"}});

    addSlider (frequency, "fmin", "Lowest", 20.0, 2000.0, 1.0, 200.0, " Hz");
    addSlider (frequency, "fmax", "Highest", 500.0, 22050.0, 1.0, 6000.0, " Hz");
    addChoice (frequency, "frequency_scale", "Scale", {"Logarithmic", "Linear"}, {"log", "linear"});
    addChoice (frequency, "n_fft", "FFT size", {"1024", "2048", "4096", "8192"}, {1024, 2048, 4096, 8192});

    addSlider (time, "duration", "Length", 0.05, 20.0, 0.01, 2.0, " s");
    addChoice (time, "hop_length", "Hop", {"16", "32", "64", "128", "256", "512"},
               {16, 32, 64, 128, 256, 512});
    addSlider (time, "dynamic", "Range", 10.0, 120.0, 0.5, 0.0, " dB");
    addSlider (time, "noise_floor", "Noise floor", 0.0, 0.95, 0.01);
    addSlider (time, "curve", "Curve", 0.1, 4.0, 0.01, 1.0);
    addSlider (time, "spectrum_blur_time", "Smooth time", 0.0, 20.0, 0.1);
    addSlider (time, "spectrum_blur_freq", "Smooth freq.", 0.0, 20.0, 0.1);

    addSlider (reconstruction, "griffin_lim_iterations", "Iterations", 1.0, 512.0, 1.0, 64.0);
    addSlider (reconstruction, "griffin_lim_momentum", "Momentum", 0.0, 0.99, 0.01);
    addSlider (reconstruction, "random_seed", "Seed", 0.0, 9999.0, 1.0);
    addSlider (reconstruction, "output_gain", "Peak level", 0.05, 1.0, 0.01);

    styleHeading (heading);
    styleSecondary (hint);
    styleSecondary (estimateLabel);
    estimateLabel.setJustificationType (juce::Justification::centredRight);
    styleSecondary (statusLabel);
    renderButton.setColour (juce::TextButton::buttonColourId, colours::accent);
    renderButton.onClick = [this]
    {
        if (onRender)
            onRender();
    };
    cancelButton.onClick = [this]
    {
        if (onCancel)
            onCancel();
    };
    for (auto* c : std::initializer_list<juce::Component*>{&heading, &hint, &progress, &estimateLabel,
                                                           &statusLabel, &renderButton, &cancelButton})
        addAndMakeVisible (c);
    progress.setPercentageDisplay (false);
}

RenderPanel::~RenderPanel() = default;

void RenderPanel::bind (juce::ValueTree renderSettings)
{
    tree = renderSettings;
    for (auto& row : rows)
        row->bind (tree);
}

void RenderPanel::setStatus (bool rendering, float progress01, const juce::String& message, bool isError,
                             const juce::String& estimate)
{
    progressValue = rendering ? (double)progress01 : 0.0; // the status line reports the result
    renderButton.setEnabled (!rendering);
    cancelButton.setEnabled (rendering);
    statusLabel.setText (message, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, isError ? colours::error : colours::textSecondary);
    estimateLabel.setText (estimate, juce::dontSendNotification);
}

void RenderPanel::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // group titles
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.setColour (colours::accentLight);
    for (auto& group : groups)
        if (!group.rows.empty())
            g.drawText (group.title, group.rows.front()->getBounds().translated (0, -20).withHeight (18),
                        juce::Justification::left);
}

void RenderPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    auto top = area.removeFromTop (22);
    heading.setBounds (top.removeFromLeft (70));
    hint.setBounds (top);

    statusLabel.setBounds (area.removeFromBottom (20));
    auto bottom = area.removeFromBottom (28);
    renderButton.setBounds (bottom.removeFromRight (100));
    bottom.removeFromRight (8);
    cancelButton.setBounds (bottom.removeFromRight (80));
    bottom.removeFromRight (8);
    estimateLabel.setBounds (bottom.removeFromRight (150));
    bottom.removeFromRight (8);
    progress.setBounds (bottom.withSizeKeepingCentre (bottom.getWidth(), 10));
    area.removeFromBottom (6);

    constexpr int gap = 18, rowHeight = 24;
    const int columnWidth = (area.getWidth() - gap * 3) / 4;
    for (size_t i = 0; i < groups.size(); ++i)
    {
        auto column = area.withX (area.getX() + (int)i * (columnWidth + gap)).withWidth (columnWidth);
        column.removeFromTop (24); // group title
        for (auto* row : groups[i].rows)
            row->setBounds (column.removeFromTop (rowHeight));
    }
}

// ================================================================== PlaybackPanel

struct PlaybackPanel::Knob final : public juce::Component
{
    Knob (juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& name)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        styleSecondary (label, 12.0f);
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
        attachment =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
        if (auto* p = state.getParameter (id))
        {
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
            if (p->getLabel().isNotEmpty())
                slider.setTextValueSuffix (" " + p->getLabel());
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        label.setBounds (area.removeFromTop (16));
        slider.setBounds (area);
    }

    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class PlaybackPanel::DragOutButton final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit DragOutButton (PlaybackPanel& p) : owner (p)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag the rendered sound onto an audio track");
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (isMouseOver() ? colours::panelRaised.brighter (0.1f) : colours::panelRaised);
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (colours::accent);
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Drag WAV to track", bounds, juce::Justification::centred);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (started || e.getDistanceFromDragStart() < 4 || !owner.onDragOut)
            return;
        started = true;
        if (auto file = owner.onDragOut(); file.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({file.getFullPathName()}, false,
                                                                        this);
    }

    void mouseUp (const juce::MouseEvent&) override { started = false; }

private:
    PlaybackPanel& owner;
    bool started = false;
};

PlaybackPanel::PlaybackPanel (juce::AudioProcessorValueTreeState& state)
{
    const std::pair<const char*, const char*> knobDefs[] = {
        {param::attack, "Attack"},           {param::decay, "Decay"},      {param::sustain, "Sustain"},
        {param::release, "Release"},         {param::rootNote, "Root"},    {param::fineTune, "Tune"},
        {param::loopCrossfade, "X-fade"},    {param::lowpass, "Low-pass"}, {param::highpass, "High-pass"},
        {param::velocityAmount, "Velocity"}, {param::gain, "Gain"},
    };
    for (auto& [id, name] : knobDefs)
    {
        knobs.push_back (std::make_unique<Knob> (state, id, name));
        addAndMakeVisible (*knobs.back());
    }

    const std::pair<const char*, const char*> toggleDefs[] = {
        {param::loopOn, "Loop"}, {param::reverse, "Reverse"}, {param::keyTrack, "Key track"}};
    for (auto& [id, name] : toggleDefs)
    {
        toggles.push_back (std::make_unique<juce::ToggleButton> (name));
        addAndMakeVisible (*toggles.back());
        toggleAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            state, id, *toggles.back()));
    }

    playMode.addItemList ({"Mono", "Poly"}, 1);
    addAndMakeVisible (playMode);
    playModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state, param::playMode, playMode);

    dragOut = std::make_unique<DragOutButton> (*this);
    addAndMakeVisible (*dragOut);
    styleHeading (heading);
    addAndMakeVisible (heading);
}

PlaybackPanel::~PlaybackPanel() = default;

void PlaybackPanel::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
}

void PlaybackPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    heading.setBounds (area.removeFromTop (22));

    // Right side: toggles in one column, play mode + drag-out in the next.
    auto side = area.removeFromRight (250);
    area.removeFromRight (12);
    auto toggleColumn = side.removeFromLeft (110);
    side.removeFromLeft (10);
    const int rowHeight = 26;
    for (auto& t : toggles)
        t->setBounds (toggleColumn.removeFromTop (rowHeight).reduced (0, 2));
    playMode.setBounds (side.removeFromTop (rowHeight).reduced (0, 2));
    side.removeFromTop (6);
    dragOut->setBounds (side.removeFromTop (44));

    const int knobWidth = area.getWidth() / (int)knobs.size();
    for (auto& k : knobs)
        k->setBounds (area.removeFromLeft (knobWidth).withHeight (juce::jmin (area.getHeight(), 92)));
}
} // namespace its::ui

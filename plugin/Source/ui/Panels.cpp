#include "Panels.h"

#include "Parameters.h"
#include "Region.h"
#include "Theme.h"

namespace its::ui
{
namespace
{
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

// A die (five pips) that randomizes the render settings.
class RenderPanel::DiceButton final : public juce::Button
{
public:
    DiceButton() : juce::Button ("Randomize render settings")
    {
        setTooltip ("Randomize the render settings (length and quality stay). Press Render to hear them.");
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (down ? colours::accentDark
                          : (over ? colours::panelRaised.brighter (0.08f) : colours::panelRaised));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (over ? colours::accent : colours::border);
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

        const auto die = bounds.withSizeKeepingCentre (17.0f, 17.0f);
        g.setColour (colours::text);
        g.drawRoundedRectangle (die, 3.5f, 1.4f);
        const float r = 1.6f, inset = 4.6f;
        const auto c = die.getCentre();
        const float l = die.getX() + inset, t = die.getY() + inset, rr = die.getRight() - inset,
                    b = die.getBottom() - inset;
        for (auto p : {juce::Point<float> (l, t), juce::Point<float> (rr, t), c, juce::Point<float> (l, b),
                       juce::Point<float> (rr, b)})
            g.fillEllipse (p.x - r, p.y - r, 2.0f * r, 2.0f * r);
    }
};

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
    addSlider (time, "dynamic", "Range", 10.0, 120.0, 0.5, 0.0, " dB");
    addSlider (time, "noise_floor", "Noise floor", 0.0, 0.95, 0.01);
    addSlider (time, "curve", "Curve", 0.1, 4.0, 0.01, 1.0);
    // Hop and spectral smoothing are not shown: they keep their defaults (or a preset's values).

    addSlider (reconstruction, "griffin_lim_iterations", "Iterations", 1.0, 512.0, 1.0, 64.0);
    addSlider (reconstruction, "griffin_lim_momentum", "Momentum", 0.0, 0.99, 0.01);
    addSlider (reconstruction, "random_seed", "Seed", 0.0, 9999.0, 1.0);
    addSlider (reconstruction, "output_gain", "Peak level", 0.05, 1.0, 0.01);

    foldButton.setColour (juce::TextButton::buttonColourId, colours::panel);
    foldButton.onClick = [this]
    {
        setExpanded (!expanded);
        if (onExpandedChanged)
            onExpandedChanged (expanded);
    };
    styleSecondary (presetLabel, 13.0f);
    presetLabel.setJustificationType (juce::Justification::centredRight);
    presetBox.setTextWhenNothingSelected ("Choose a render preset");
    styleSecondary (hint);
    styleSecondary (estimateLabel);
    estimateLabel.setJustificationType (juce::Justification::centredRight);
    styleSecondary (errorLabel);
    errorLabel.setColour (juce::Label::textColourId, colours::error);
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
    diceButton = std::make_unique<DiceButton>();
    diceButton->onClick = [this]
    {
        if (onRandomize)
            onRandomize();
    };
    addAndMakeVisible (*diceButton);
    for (auto* c :
         std::initializer_list<juce::Component*>{&foldButton, &presetLabel, &presetBox, &hint, &progress,
                                                 &estimateLabel, &errorLabel, &renderButton, &cancelButton})
        addAndMakeVisible (c);
    progress.setPercentageDisplay (false);
    setExpanded (true);
}

RenderPanel::~RenderPanel() = default;

void RenderPanel::setExpanded (bool shouldBeExpanded)
{
    expanded = shouldBeExpanded;
    foldButton.setButtonText (expanded ? "Hide render settings" : "View render settings");
    hint.setVisible (expanded);
    for (auto& row : rows)
        row->setVisible (expanded);
    resized();
    repaint();
}

void RenderPanel::bind (juce::ValueTree renderSettings)
{
    tree = renderSettings;
    for (auto& row : rows)
        row->bind (tree);
}

void RenderPanel::setStatus (bool rendering, float progress01, const juce::String& message, bool isError,
                             const juce::String& estimate)
{
    progressValue = rendering ? (double)progress01 : 0.0;
    renderButton.setEnabled (!rendering);
    cancelButton.setEnabled (rendering);
    // Only errors are shown (in place of the progress bar); other results are visible elsewhere.
    const bool showError = isError && !rendering && message.isNotEmpty();
    errorLabel.setText (showError ? message : juce::String(), juce::dontSendNotification);
    errorLabel.setVisible (showError);
    progress.setVisible (!showError);
    estimateLabel.setText (estimate, juce::dontSendNotification);
}

void RenderPanel::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    if (!expanded)
        return;

    g.setColour (colours::border);
    g.fillRect (12, barHeight - 1, getWidth() - 24, 1); // divider between bar and settings

    // group titles
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.setColour (colours::caption);
    for (auto& group : groups)
        if (!group.rows.empty())
            g.drawText (group.title, group.rows.front()->getBounds().translated (0, -20).withHeight (18),
                        juce::Justification::left);
}

void RenderPanel::resized()
{
    // Bar: [fold] [Render preset: combo] ........ [estimate] [Cancel] [Render]
    //      [progress (full width), or the last error ...........................]
    auto bar = getLocalBounds().removeFromTop (barHeight).reduced (12, 8);
    auto top = bar.removeFromTop (30);
    foldButton.setBounds (top.removeFromLeft (170));
    top.removeFromLeft (12);
    presetLabel.setBounds (top.removeFromLeft (92));
    top.removeFromLeft (6);
    presetBox.setBounds (top.removeFromLeft (220).reduced (0, 2));
    top.removeFromLeft (8);
    diceButton->setBounds (top.removeFromLeft (34).reduced (0, 1));
    renderButton.setBounds (top.removeFromRight (100));
    top.removeFromRight (8);
    cancelButton.setBounds (top.removeFromRight (80));
    top.removeFromRight (8);
    estimateLabel.setBounds (top);
    bar.removeFromTop (4);
    progress.setBounds (bar.withSizeKeepingCentre (bar.getWidth(), 6));
    errorLabel.setBounds (bar);

    if (!expanded)
        return;

    auto area = getLocalBounds().withTrimmedTop (barHeight).reduced (12, 6);
    hint.setBounds (area.removeFromTop (20));
    constexpr int gap = 18, rowHeight = 24;
    const int columnWidth = (area.getWidth() - gap * 3) / 4;
    for (size_t i = 0; i < groups.size(); ++i)
    {
        auto column = area.withX (area.getX() + (int)i * (columnWidth + gap)).withWidth (columnWidth);
        column.removeFromTop (22); // group title
        for (auto* row : groups[i].rows)
            row->setBounds (column.removeFromTop (rowHeight));
    }
}

// ================================================================== PlaybackPanel

namespace
{
// A rotary slider whose value can be limited by a rule (used so sample start never passes end).
class ConstrainedSlider final : public juce::Slider
{
public:
    std::function<double (double)> constrain;

    double snapValue (double value, DragMode mode) override
    {
        return constrain ? constrain (value) : juce::Slider::snapValue (value, mode);
    }

    double getValueFromText (const juce::String& text) override
    {
        const auto value = juce::Slider::getValueFromText (text);
        return constrain ? constrain (value) : value;
    }
};

enum Icon
{
    keyboardIcon,
    loopIcon,
    reverseIcon
};
} // namespace

struct PlaybackPanel::Knob final : public juce::Component
{
    Knob (juce::AudioProcessorValueTreeState& apvts, const char* id, const juce::String& name)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        styleSecondary (label, 12.0f);
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
        attachment =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, slider);
        if (auto* p = apvts.getParameter (id))
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

    ConstrainedSlider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A square toggle that shows an icon instead of text (the name is in the tooltip).
class PlaybackPanel::IconToggle final : public juce::ToggleButton
{
public:
    explicit IconToggle (int iconToDraw) : icon (iconToDraw) {}
    explicit IconToggle (const juce::String& label) : icon (-1) { setButtonText (label); }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = getToggleState();
        g.setColour (on ? colours::accentDark
                        : (over ? colours::panelRaised.brighter (0.08f) : colours::panelRaised));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (on ? colours::accent : colours::border);
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

        const auto colour = on ? colours::text : colours::textSecondary;
        g.setColour (colour);
        if (icon < 0) // text instead of an icon
        {
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (getButtonText(), bounds, juce::Justification::centred, false);
            return;
        }
        const auto box = bounds.withSizeKeepingCentre (18.0f, 14.0f);
        juce::Path path;
        switch (icon)
        {
        case keyboardIcon:
        {
            g.drawRoundedRectangle (box, 1.5f, 1.2f);
            const float keyWidth = box.getWidth() / 3.0f;
            for (int k = 1; k < 3; ++k)
                g.fillRect (box.getX() + (float)k * keyWidth - 0.5f, box.getY(), 1.0f, box.getHeight());
            for (int k = 1; k < 3; ++k) // black keys
                g.fillRect (box.getX() + (float)k * keyWidth - keyWidth * 0.28f, box.getY(), keyWidth * 0.56f,
                            box.getHeight() * 0.58f);
            return;
        }
        case loopIcon:
        {
            const auto c = box.getCentre();
            const float r = 6.5f;
            path.addCentredArc (c.x, c.y, r, r, 0.0f, 0.5f, juce::MathConstants<float>::twoPi - 0.3f, true);
            g.strokePath (path, juce::PathStrokeType (1.6f));
            const float endAngle = juce::MathConstants<float>::twoPi - 0.3f;
            const juce::Point<float> tip (c.x + r * std::sin (endAngle), c.y - r * std::cos (endAngle));
            juce::Path head;
            head.addTriangle (tip.x - 4.0f, tip.y - 3.5f, tip.x + 3.0f, tip.y, tip.x - 4.0f, tip.y + 3.5f);
            g.fillPath (head);
            return;
        }
        case reverseIcon:
        default:
        {
            // a "play" triangle pointing left, plus a bar
            const float y0 = box.getY(), y1 = box.getBottom(), cy = box.getCentreY();
            path.addTriangle (box.getX(), cy, box.getRight() - 4.0f, y0, box.getRight() - 4.0f, y1);
            g.fillPath (path);
            g.fillRect (box.getRight() - 2.0f, y0, 2.0f, box.getHeight());
            return;
        }
        }
    }

private:
    int icon;
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
        g.drawFittedText ("Drag WAV to track", bounds.toNearestInt().reduced (6, 0),
                          juce::Justification::centred, 2);
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

// A choice parameter shown as toggle buttons (one per choice), e.g. the filter slope 12 / 24.
// The buttons are children of the panel (so they lay out like the other toggles).
class PlaybackPanel::ChoiceButtons
{
public:
    ChoiceButtons (juce::RangedAudioParameter& parameter, std::vector<IconToggle*> choiceButtons)
        : buttons (std::move (choiceButtons)),
          attachment (parameter, [this] (float value) { show (juce::roundToInt (value)); })
    {
        for (size_t i = 0; i < buttons.size(); ++i)
        {
            buttons[i]->setClickingTogglesState (false);
            buttons[i]->onClick = [this, i] { attachment.setValueAsCompleteGesture ((float)i); };
        }
        attachment.sendInitialUpdate();
    }

private:
    void show (int index)
    {
        for (size_t i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState ((int)i == index, juce::dontSendNotification);
    }

    std::vector<IconToggle*> buttons;
    juce::ParameterAttachment attachment;
};

PlaybackPanel::Knob& PlaybackPanel::addKnob (const char* id, const juce::String& name)
{
    knobs.push_back (std::make_unique<Knob> (state, id, name));
    addAndMakeVisible (*knobs.back());
    return *knobs.back();
}

PlaybackPanel::IconToggle& PlaybackPanel::addToggle (const char* id, int icon, const juce::String& tooltip)
{
    toggles.push_back (std::make_unique<IconToggle> (icon));
    auto& t = *toggles.back();
    t.setTooltip (tooltip);
    t.setTitle (tooltip); // accessibility name
    addAndMakeVisible (t);
    toggleAttachments.push_back (
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id, t));
    return t;
}

PlaybackPanel::PlaybackPanel (juce::AudioProcessorValueTreeState& apvts) : state (apvts)
{
    groups.resize (4);
    auto& envelope = groups[0];
    auto& pitch = groups[1];
    auto& sample = groups[2];
    auto& filter = groups[3];
    envelope.title = "Envelope";
    pitch.title = "Pitch";
    sample.title = "Sample";
    filter.title = "Filter";

    for (auto [id, name] : {std::pair{param::attack, "Attack"}, std::pair{param::decay, "Decay"},
                            std::pair{param::sustain, "Sustain"}, std::pair{param::release, "Release"}})
        envelope.knobs.push_back (&addKnob (id, name));

    pitch.knobs = {&addKnob (param::transpose, "Transpose"), &addKnob (param::fineTune, "Tune")};
    pitch.extras = {&addToggle (param::keyTrack, keyboardIcon, "Key tracking")};

    auto& startKnob = addKnob (param::sampleStart, "Start");
    auto& endKnob = addKnob (param::sampleEnd, "End");
    // Start never passes end (and the other way round); same rule as the spectrogram markers.
    startKnob.slider.constrain = [&endKnob] (double v)
    { return (double)region::clampStart ((float)v, (float)endKnob.slider.getValue()); };
    endKnob.slider.constrain = [&startKnob] (double v)
    { return (double)region::clampEnd ((float)startKnob.slider.getValue(), (float)v); };
    sample.knobs = {&startKnob, &endKnob, &addKnob (param::loopCrossfade, "X-fade")};
    sample.extras = {&addToggle (param::loopOn, loopIcon, "Loop"),
                     &addToggle (param::reverse, reverseIcon, "Reverse")};

    filter.knobs = {&addKnob (param::lowpass, "Low-pass"), &addKnob (param::highpass, "High-pass")};
    std::vector<IconToggle*> slopeButtons;
    for (auto* text : {"12", "24"})
    {
        toggles.push_back (std::make_unique<IconToggle> (juce::String (text)));
        auto& b = *toggles.back();
        b.setTooltip ("Filter slope: " + juce::String (text) + " dB per octave (both filters)");
        b.setTitle (b.getTooltip());
        addAndMakeVisible (b);
        slopeButtons.push_back (&b);
        filter.extras.push_back (&b);
    }
    filterSlope = std::make_unique<ChoiceButtons> (*state.getParameter (param::filterSlope), slopeButtons);

    gainKnob = &addKnob (param::gain, "Gain");

    playMode.addItemList ({"Mono", "Poly"}, 1);
    addAndMakeVisible (playMode);
    playModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state, param::playMode, playMode);

    dragOut = std::make_unique<DragOutButton> (*this);
    addAndMakeVisible (*dragOut);
}

PlaybackPanel::~PlaybackPanel() = default;

void PlaybackPanel::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    auto card = [&g] (juce::Rectangle<int> bounds, const juce::String& title)
    {
        g.setColour (colours::panelRaised.withAlpha (0.45f));
        g.fillRoundedRectangle (bounds.toFloat(), 6.0f);
        g.setColour (colours::caption);
        g.drawText (title, bounds.reduced (10, 3).removeFromTop (14), juce::Justification::left);
    };
    for (auto& group : groups)
        card (group.bounds, group.title);
    card (outputBounds, "Output");
}

void PlaybackPanel::resized()
{
    auto area = getLocalBounds().reduced (10, 10);
    constexpr int gap = 10, titleHeight = 14;

    // Output column on the right: gain knob, play mode, drag-out.
    outputBounds = area.removeFromRight (150);
    area.removeFromRight (gap);
    {
        auto column = outputBounds.reduced (10, 3);
        column.removeFromTop (titleHeight);
        gainKnob->setBounds (
            column.removeFromTop (juce::jmin (84, column.getHeight() / 2))); // same size as the group knobs
        column.removeFromTop (6);
        playMode.setBounds (column.removeFromTop (26));
        column.removeFromTop (8);
        dragOut->setBounds (column.removeFromTop (juce::jmin (52, column.getHeight())));
    }

    // Two rows of groups on a 7-slot grid: [4 slots | 3 slots] (a group's extras take one slot).
    const int rowHeight = (area.getHeight() - gap) / 2;
    const int slotWidth = (area.getWidth() - gap - 4 * 10) / 7;
    for (size_t i = 0; i < groups.size(); ++i)
    {
        auto& group = groups[i];
        const int row = (int)i / 2;
        const int slots = (int)group.knobs.size() + (group.extras.empty() ? 0 : 1);
        const int x = area.getX() + (i % 2 == 0 ? 0 : 4 * slotWidth + 20 + gap);
        group.bounds = {x, area.getY() + row * (rowHeight + gap), slots * slotWidth + 20, rowHeight};

        auto inner = group.bounds.reduced (10, 3);
        inner.removeFromTop (titleHeight);
        for (auto* knob : group.knobs)
            knob->setBounds (inner.removeFromLeft (slotWidth));
        if (group.extras.empty())
            continue;

        // Extras stacked and centred vertically in the remaining slot.
        auto slot = inner.removeFromLeft (slotWidth).reduced (8, 0);
        const int itemHeight = 28, itemGap = 6;
        const int total = (int)group.extras.size() * itemHeight + ((int)group.extras.size() - 1) * itemGap;
        auto column = slot.withSizeKeepingCentre (juce::jmin (slot.getWidth(), 72), total);
        for (auto* extra : group.extras)
        {
            auto itemBounds = column.removeFromTop (itemHeight);
            if (dynamic_cast<IconToggle*> (extra) != nullptr) // toggles: square-ish
                itemBounds =
                    itemBounds.withSizeKeepingCentre (juce::jmin (itemBounds.getWidth(), 44), itemHeight);
            extra->setBounds (itemBounds);
            column.removeFromTop (itemGap);
        }
    }
}
} // namespace its::ui

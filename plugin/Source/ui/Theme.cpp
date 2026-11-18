#include "Theme.h"

namespace its::ui
{
juce::Colour heatColour (float v)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    static const juce::Colour stops[] = {juce::Colour (0xff0d0c14), juce::Colour (0xff26215c),
                                         colours::accent, juce::Colour (0xffeeedfe)};
    const float scaled = v * 3.0f;
    const int i = juce::jmin (2, (int)scaled);
    return stops[i].interpolatedWith (stops[i + 1], scaled - (float)i);
}

LookAndFeel::LookAndFeel()
{
    using namespace colours;
    setColourScheme ({background, panel, panelRaised, border, text, accent, text, panelRaised, text});

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, border);
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, panelRaised);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accentDark);
    setColour (juce::TextEditor::backgroundColourId, panelRaised);
    setColour (juce::TextEditor::outlineColourId, border);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::ProgressBar::foregroundColourId, accent);
    setColour (juce::TextButton::buttonColourId, panelRaised);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, panelRaised);
    setColour (juce::ComboBox::outlineColourId, border);
    setColour (juce::ComboBox::arrowColourId, textSecondary);
    setColour (juce::PopupMenu::backgroundColourId, panelRaised);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDark);
    setColour (juce::ProgressBar::foregroundColourId, accent);
    setColour (juce::ProgressBar::backgroundColourId, panelRaised);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                    float start, float end, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float track = juce::jmax (2.5f, radius * 0.16f);
    const float arcRadius = radius - track / 2.0f;
    const float angle = start + pos * (end - start);

    juce::Path background, value;
    background.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, start, end, true);
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, start, angle, true);
    const juce::PathStrokeType stroke (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (colours::border);
    g.strokePath (background, stroke);
    g.setColour (colours::accent);
    g.strokePath (value, stroke);

    const float inner = arcRadius - track * 1.4f;
    g.setColour (colours::panelRaised);
    g.fillEllipse (juce::Rectangle<float> (inner * 2.0f, inner * 2.0f).withCentre (centre));
    const auto tip = centre.getPointOnCircumference (inner * 0.85f, angle);
    g.setColour (colours::text);
    g.drawLine ({centre.getPointOnCircumference (inner * 0.25f, angle), tip}, 2.0f);
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                    float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearBar && style != juce::Slider::LinearHorizontal)
        return LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, slider);

    // Compact bar: rounded track, violet fill up to the value.
    const auto bounds =
        juce::Rectangle<int> (x, y, width, height).toFloat().withSizeKeepingCentre ((float)width, 6.0f);
    g.setColour (colours::panelRaised);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (slider.isEnabled() ? colours::accent : colours::textMuted);
    g.fillRoundedRectangle (bounds.withRight (juce::jlimit (bounds.getX(), bounds.getRight(), sliderPos)),
                            3.0f);
    g.setColour (colours::text);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({sliderPos, bounds.getCentreY()}));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                                        bool over, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = colour;
    if (!button.isEnabled())
        fill = fill.withMultipliedAlpha (0.4f);
    else if (down)
        fill = fill.darker (0.15f);
    else if (over)
        fill = fill.brighter (0.12f);
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (colours::border);
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool over, bool)
{
    // Pill: filled violet when on.
    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = button.getToggleState();
    g.setColour (on ? colours::accentDark
                    : (over ? colours::panelRaised.brighter (0.08f) : colours::panelRaised));
    g.fillRoundedRectangle (bounds, bounds.getHeight() / 2.0f);
    g.setColour (on ? colours::accent : colours::border);
    g.drawRoundedRectangle (bounds, bounds.getHeight() / 2.0f, 1.0f);
    g.setColour (on ? colours::text : colours::textSecondary);
    g.setFont (juce::FontOptions (12.5f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (6, 0),
                      juce::Justification::centred, 1);
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (juce::FontOptions (12.0f));
    label->setColour (juce::Label::textColourId, colours::textSecondary);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return label;
}
} // namespace its::ui

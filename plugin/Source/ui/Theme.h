#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace its::ui
{
// Dark theme with a violet accent (decided in phase 5).
namespace colours
{
inline const juce::Colour background{0xff17161c};
inline const juce::Colour panel{0xff201f27};
inline const juce::Colour panelRaised{0xff2a2932};
inline const juce::Colour border{0xff36353f};
inline const juce::Colour text{0xffecebf2};
inline const juce::Colour textSecondary{0xff9c9aa8};
inline const juce::Colour textMuted{0xff6d6b78};
inline const juce::Colour accent{0xff7f77dd};      // violet 400
inline const juce::Colour accentLight{0xffafa9ec}; // violet 200
inline const juce::Colour accentDark{0xff3c3489};  // violet 800
inline const juce::Colour region{0xff5dcaa5};      // start/end markers (teal)
inline const juce::Colour loop{0xffef9f27};        // loop region (amber)
inline const juce::Colour error{0xfff09595};
} // namespace colours

// Spectrogram / preview colour map: near-black -> violet -> pale violet.
juce::Colour heatColour (float value01);

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isMouseOverButton,
                           bool isButtonDown) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }
    juce::Font getPopupMenuFont() override { return juce::FontOptions (13.5f); }
};
} // namespace its::ui

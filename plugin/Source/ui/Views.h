#pragma once

#include "dsp/Matrix.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace its::ui
{
// The processed image (live while render settings change). Click to choose a file.
class ImagePreview final : public juce::Component
{
public:
    void setPixels (const FloatMatrix& pixels); // row 0 = lowest frequency (as processImage)
    void setDropHighlight (bool shouldHighlight);
    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Image image;
    bool dropHighlight = false;
};

// The rendered spectrogram with playhead and draggable start/end and loop markers.
class SpectrogramView final : public juce::Component
{
public:
    explicit SpectrogramView (juce::AudioProcessorValueTreeState& state);

    void setSpectrogram (const FloatMatrix& display01); // rows log-spaced, row 0 = lowest
    void setPlayhead (float fraction);                  // -1 = hidden
    void setInfo (const juce::String& text);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class Marker
    {
        none,
        start,
        end,
        loopStart,
        loopEnd
    };
    juce::RangedAudioParameter* parameterFor (Marker) const;
    float valueOf (Marker) const;
    Marker markerAt (float x) const;
    juce::Rectangle<float> plotArea() const;

    juce::RangedAudioParameter *start, *end, *loopStart, *loopEnd, *loopOn;
    juce::Image image;
    float playhead = -1.0f;
    juce::String info;
    Marker dragging = Marker::none, hovering = Marker::none;
};
} // namespace its::ui

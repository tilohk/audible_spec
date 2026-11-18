#include "Views.h"

#include "Region.h"

#include "Parameters.h"
#include "Theme.h"

namespace its::ui
{
// ------------------------------------------------------------------ ImagePreview

void ImagePreview::setPixels (const FloatMatrix& pixels)
{
    if (pixels.empty())
    {
        image = {};
        repaint();
        return;
    }
    image = juce::Image (juce::Image::RGB, pixels.cols, pixels.rows, false);
    const juce::Image::BitmapData bitmap (image, juce::Image::BitmapData::writeOnly);
    for (int r = 0; r < pixels.rows; ++r)
        for (int c = 0; c < pixels.cols; ++c)
        {
            // draw row 0 (lowest frequency) at the bottom, like the spectrogram
            const auto level = (juce::uint8)juce::jlimit (0, 255, (int)(pixels (r, c) * 255.0f + 0.5f));
            bitmap.setPixelColour (c, pixels.rows - 1 - r, juce::Colour (level, level, level));
        }
    repaint();
}

void ImagePreview::setDropHighlight (bool shouldHighlight)
{
    dropHighlight = shouldHighlight;
    repaint();
}

void ImagePreview::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, 6.0f);

    if (image.isValid())
    {
        const auto target = bounds.reduced (4.0f);
        g.drawImage (image, target, juce::RectanglePlacement::centred);
    }

    // hint strip at the bottom
    auto hint = bounds.removeFromBottom (22.0f).reduced (4.0f, 2.0f);
    g.setColour (colours::background.withAlpha (0.75f));
    g.fillRoundedRectangle (hint, 4.0f);
    g.setColour (colours::textSecondary);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Drop an image here or click to choose", hint, juce::Justification::centred);

    const float dash[] = {5.0f, 4.0f};
    juce::Path outline;
    outline.addRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f);
    juce::Path dashed;
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dash, 2);
    g.setColour (dropHighlight ? colours::accent : colours::border.brighter (0.2f));
    if (dropHighlight)
        g.strokePath (outline, juce::PathStrokeType (2.5f));
    else
        g.fillPath (dashed);
}

void ImagePreview::mouseUp (const juce::MouseEvent& e)
{
    if (onClick && e.mouseWasClicked())
        onClick();
}

// ------------------------------------------------------------------ SpectrogramView

SpectrogramView::SpectrogramView (juce::AudioProcessorValueTreeState& state)
    : start (state.getParameter (param::sampleStart)), end (state.getParameter (param::sampleEnd)),
      loopStart (state.getParameter (param::loopStart)), loopEnd (state.getParameter (param::loopEnd)),
      loopOn (state.getParameter (param::loopOn))
{
    jassert (start && end && loopStart && loopEnd && loopOn);
}

void SpectrogramView::setSpectrogram (const FloatMatrix& d)
{
    if (d.empty())
        return;
    image = juce::Image (juce::Image::RGB, d.cols, d.rows, false);
    const juce::Image::BitmapData bitmap (image, juce::Image::BitmapData::writeOnly);
    for (int r = 0; r < d.rows; ++r)
        for (int c = 0; c < d.cols; ++c)
            bitmap.setPixelColour (c, d.rows - 1 - r, heatColour (d (r, c)));
    repaint();
}

void SpectrogramView::setPlayhead (float fraction)
{
    if (!juce::exactlyEqual (fraction, playhead))
    {
        playhead = fraction;
        repaint();
    }
}

void SpectrogramView::setInfo (const juce::String& text)
{
    if (text != info)
    {
        info = text;
        repaint();
    }
}

juce::Rectangle<float> SpectrogramView::plotArea() const
{
    return getLocalBounds().toFloat().reduced (1.0f);
}

juce::RangedAudioParameter* SpectrogramView::parameterFor (Marker m) const
{
    switch (m)
    {
    case Marker::start:
        return start;
    case Marker::end:
        return end;
    case Marker::loopStart:
        return loopStart;
    case Marker::loopEnd:
        return loopEnd;
    case Marker::region:
    case Marker::none:
        break;
    }
    return nullptr;
}

std::vector<juce::RangedAudioParameter*> SpectrogramView::parametersMovedBy (Marker m) const
{
    if (m != Marker::region)
    {
        if (auto* p = parameterFor (m))
            return {p};
        return {};
    }
    return {start, end}; // the loop is relative to the region and moves with it
}

void SpectrogramView::setValue (Marker m, float fraction)
{
    if (auto* p = parameterFor (m))
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.0f, 1.0f, fraction)));
}

float SpectrogramView::fractionAt (float x) const
{
    const auto area = plotArea();
    return juce::jlimit (0.0f, 1.0f, (x - area.getX()) / area.getWidth());
}

float SpectrogramView::valueOf (Marker m) const
{
    auto* p = parameterFor (m);
    return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
}

float SpectrogramView::positionOf (Marker m) const
{
    if (m != Marker::loopStart && m != Marker::loopEnd)
        return valueOf (m);
    const float s = valueOf (Marker::start), e = valueOf (Marker::end);
    return s + juce::jlimit (0.0f, 1.0f, valueOf (m)) * juce::jmax (0.0f, e - s);
}

SpectrogramView::Marker SpectrogramView::markerAt (float x) const
{
    const auto area = plotArea();
    const bool loopVisible = loopOn->getValue() > 0.5f;
    Marker best = Marker::none;
    float bestDistance = 7.0f; // px
    for (auto m : {Marker::start, Marker::end, Marker::loopStart, Marker::loopEnd})
    {
        if (!loopVisible && (m == Marker::loopStart || m == Marker::loopEnd))
            continue;
        const float d = std::abs (area.getX() + positionOf (m) * area.getWidth() - x);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = m;
        }
    }
    if (best == Marker::none)
    {
        const float f = fractionAt (x);
        if (f > valueOf (Marker::start) && f < valueOf (Marker::end))
            best = Marker::region;
    }
    return best;
}

void SpectrogramView::paint (juce::Graphics& g)
{
    const auto area = plotArea();
    g.setColour (juce::Colour (0xff0d0c14));
    g.fillRoundedRectangle (area, 6.0f);
    if (image.isValid())
        g.drawImage (image, area.reduced (1.0f), juce::RectanglePlacement::stretchToFit);
    else
    {
        g.setColour (colours::textSecondary);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Rendering...", area, juce::Justification::centred);
    }

    auto xOf = [&area] (float fraction) { return area.getX() + fraction * area.getWidth(); };

    // outside the start..end region: dimmed
    const float sx = xOf (valueOf (Marker::start)), ex = xOf (valueOf (Marker::end));
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (area.getX(), area.getY(), sx, area.getBottom()));
    g.fillRect (
        juce::Rectangle<float>::leftTopRightBottom (ex, area.getY(), area.getRight(), area.getBottom()));

    g.setFont (juce::FontOptions (11.5f));
    if (loopOn->getValue() > 0.5f)
    {
        const float lx = xOf (positionOf (Marker::loopStart)), rx = xOf (positionOf (Marker::loopEnd));
        g.setColour (colours::loop.withAlpha (0.14f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (lx, area.getY(), rx, area.getBottom()));
        g.setColour (colours::loop);
        for (auto [x, m] : {std::pair{lx, Marker::loopStart}, std::pair{rx, Marker::loopEnd}})
            g.fillRect (x - (hovering == m ? 1.5f : 0.75f), area.getY(), hovering == m ? 3.0f : 1.5f,
                        area.getHeight());
        g.drawText ("loop", juce::Rectangle<float> (lx + 4.0f, area.getY() + 3.0f, 60.0f, 14.0f),
                    juce::Justification::left);
    }

    g.setColour (colours::region);
    for (auto [x, m] : {std::pair{sx, Marker::start}, std::pair{ex, Marker::end}})
        g.fillRect (x - (hovering == m ? 1.5f : 1.0f), area.getY(), hovering == m ? 3.0f : 2.0f,
                    area.getHeight());
    g.drawText ("start", juce::Rectangle<float> (sx + 4.0f, area.getBottom() - 17.0f, 60.0f, 14.0f),
                juce::Justification::left);

    if (playhead >= 0.0f)
    {
        g.setColour (colours::text);
        g.fillRect (xOf (playhead) - 0.75f, area.getY(), 1.5f, area.getHeight());
    }

    if (info.isNotEmpty())
    {
        g.setColour (colours::textSecondary);
        g.drawText (info, area.reduced (6.0f, 3.0f).removeFromTop (14.0f), juce::Justification::right);
    }
}

void SpectrogramView::mouseMove (const juce::MouseEvent& e)
{
    const auto m = markerAt (e.position.x);
    if (m != hovering)
    {
        hovering = m;
        setMouseCursor (m == Marker::none     ? juce::MouseCursor::NormalCursor
                        : m == Marker::region ? juce::MouseCursor::DraggingHandCursor
                                              : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
}

void SpectrogramView::mouseDown (const juce::MouseEvent& e)
{
    dragging = markerAt (e.position.x);
    dragDownFraction = fractionAt (e.position.x);
    dragStartValues[0] = valueOf (Marker::start);
    dragStartValues[1] = valueOf (Marker::end);
    gestureParameters = parametersMovedBy (dragging);
    for (auto* p : gestureParameters)
        p->beginChangeGesture();
}

void SpectrogramView::mouseDrag (const juce::MouseEvent& e)
{
    const float f = fractionAt (e.position.x);
    const float s0 = dragStartValues[0], e0 = dragStartValues[1];
    switch (dragging)
    {
    case Marker::start:
        setValue (Marker::start, region::clampStart (f, valueOf (Marker::end)));
        break;
    case Marker::end:
        setValue (Marker::end, region::clampEnd (valueOf (Marker::start), f));
        break;
    case Marker::loopStart:
    case Marker::loopEnd:
    {
        // Loop markers are fractions of the region and stay inside it.
        const float rs = valueOf (Marker::start), re = valueOf (Marker::end);
        const float relative = re > rs ? (f - rs) / (re - rs) : 0.0f;
        if (dragging == Marker::loopStart)
            setValue (dragging, region::clampStart (relative, valueOf (Marker::loopEnd)));
        else
            setValue (dragging, region::clampEnd (valueOf (Marker::loopStart), relative));
        break;
    }
    case Marker::region:
    {
        const float delta = region::limitMove (s0, e0, f - dragDownFraction);
        setValue (Marker::start, s0 + delta);
        setValue (Marker::end, e0 + delta);
        break;
    }
    case Marker::none:
        return;
    }
    repaint();
}

void SpectrogramView::mouseUp (const juce::MouseEvent&)
{
    for (auto* p : gestureParameters)
        p->endChangeGesture();
    gestureParameters.clear();
    dragging = Marker::none;
}
} // namespace its::ui

// Tests for the UI data helpers (ui/DisplayData).

#include "dsp/ImageProcessor.h"
#include "ui/DisplayData.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace its::test
{
class DisplayTests final : public juce::UnitTest
{
public:
    DisplayTests() : juce::UnitTest ("UI display data", "ui") {}

    void runTest() override
    {
        beginTest ("sound length in seconds maps to frames");
        {
            RenderParams p; // 44.1 kHz, hop 32
            expectEquals (display::widthForDuration (1024.0 * 32.0 / 44100.0, p), 1024);
            expectEquals (display::widthForDuration (2.0, p), 2756);
            p.hopLength = 128;
            expectEquals (display::widthForDuration (2.0, p), 689);
            expectEquals (display::widthForDuration (0.0, p), 16); // never below 16 frames
        }

        beginTest ("render time estimate grows with the work");
        {
            RenderParams p;
            const double base = display::estimateRenderSeconds (p);
            expectWithinAbsoluteError (base, 2.5, 0.01); // calibrated default
            auto longer = p;
            longer.width *= 2;
            auto moreIterations = p;
            moreIterations.griffinLimIterations *= 2;
            expectGreaterThan (display::estimateRenderSeconds (longer), base * 1.9);
            expectGreaterThan (display::estimateRenderSeconds (moreIterations), base * 1.9);
        }

        beginTest ("spectrogram picture: a pure tone appears at the right log-frequency row");
        {
            RenderParams p;
            p.nFft = 1024; // 513 bins, 43 Hz apart
            p.fMin = 100.0;
            p.fMax = 10000.0;
            p.dynamic = 60.0;
            FloatMatrix magnitude (p.specHeight(), 50, 0.0f);
            const int bin = (int)std::lround (1000.0 / (p.sampleRate / 2.0) * (p.specHeight() - 1)); // ~1 kHz
            for (int f = 0; f < magnitude.cols; ++f)
                magnitude (bin, f) = 1.0f;

            const auto d = display::spectrogramDisplay (magnitude, p, 101, 20);
            expectEquals (d.rows, 101);
            expectEquals (d.cols, 20); // 50 frames merged into 20 columns
            int brightest = 0;
            for (int r = 0; r < d.rows; ++r)
                if (d (r, 0) > d (brightest, 0))
                    brightest = r;
            // 1 kHz is halfway between 100 Hz and 10 kHz on a log axis -> row ~50
            expectWithinAbsoluteError (brightest, 50, 2);
            expectWithinAbsoluteError ((double)d (brightest, 0), 1.0, 0.05);
            float minimum = 1.0f;
            for (auto v : d.data)
                minimum = std::min (minimum, v);
            expectEquals ((double)minimum, 0.0); // silence = bottom of the range
        }

        beginTest ("preview applies the same image processing as the render");
        {
            RgbaImage image;
            image.width = 300;
            image.height = 200;
            image.pixels.resize ((size_t)(300 * 200 * 4));
            for (size_t i = 0; i < image.pixels.size(); i += 4)
            {
                const auto x = (int)((i / 4) % 300);
                image.pixels[i] = image.pixels[i + 1] = image.pixels[i + 2] = (std::uint8_t)(x * 255 / 299);
                image.pixels[i + 3] = 255;
            }
            const auto base = display::previewBase (image, 150, 150);
            expectEquals (base.cols, 150);
            expectEquals (base.rows, 100); // aspect kept

            RenderParams p;
            p.gamma = 0.5;
            p.contrast = 1.3;
            const auto pixels = display::previewPixels (base, p);
            const auto reference = processImage (base, p); // blur 0: identical
            expect (pixels.data == reference.data);
            expectGreaterThan (pixels (0, 0), pixels (0, 149)); // inverted by default: dark side bright
        }
    }
};

static DisplayTests displayTests;
} // namespace its::test

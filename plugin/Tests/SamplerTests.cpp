// Tests for the playback engine (pitch, envelope, loop, reverse, voices, sample swap,
// real-time safety) and the parameter layout.

#include "AllocationCounter.h"

#include "Parameters.h"
#include "audio/SamplerEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

namespace its::test
{
namespace
{
constexpr double hostRate = 48000.0;
constexpr int blockSize = 512;

std::unique_ptr<SampleBuffer> makeSine (double frequency, double seconds, double sampleRate = 44100.0,
                                        float amplitude = 0.5f)
{
    auto s = std::make_unique<SampleBuffer>();
    s->sampleRate = sampleRate;
    s->samples.resize ((size_t)(seconds * sampleRate));
    for (size_t i = 0; i < s->samples.size(); ++i)
        s->samples[i] = amplitude * (float)std::sin (2.0 * M_PI * frequency * (double)i / sampleRate);
    return s;
}

struct Event
{
    int sample;
    juce::MidiMessage message;
};

// Runs the engine for `total` samples in host-sized blocks, returns channel 0.
std::vector<float> run (SamplerEngine& engine, const PlaybackSettings& s, int total,
                        const std::vector<Event>& events)
{
    std::vector<float> out;
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += blockSize)
    {
        const int n = std::min (blockSize, total - pos);
        buffer.setSize (2, n, false, false, true);
        midi.clear();
        for (auto& e : events)
            if (e.sample >= pos && e.sample < pos + n)
                midi.addEvent (e.message, e.sample - pos);
        engine.process (buffer, midi, s);
        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + n);
    }
    return out;
}

int ms (double milliseconds)
{
    return (int)(milliseconds * 0.001 * hostRate);
}

// Frequency from positive-going zero crossings (linear interpolation) in [from, to).
double estimateFrequency (const std::vector<float>& x, int from, int to)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (int i = std::max (1, from); i < std::min ((int)x.size(), to); ++i)
        if (x[(size_t)i - 1] < 0.0f && x[(size_t)i] >= 0.0f)
        {
            const double t = (double)(i - 1) + x[(size_t)i - 1] / (x[(size_t)i - 1] - x[(size_t)i]);
            if (first < 0)
                first = t;
            last = t;
            ++crossings;
        }
    return crossings < 2 ? 0.0 : (crossings - 1) * hostRate / (last - first);
}

double rms (const std::vector<float>& x, int from, int to)
{
    double sum = 0.0;
    for (int i = from; i < to; ++i)
        sum += (double)x[(size_t)i] * x[(size_t)i];
    return std::sqrt (sum / std::max (1, to - from));
}

float maxStep (const std::vector<float>& x, int from, int to)
{
    float m = 0.0f;
    for (int i = std::max (1, from); i < to; ++i)
        m = std::max (m, std::abs (x[(size_t)i] - x[(size_t)i - 1]));
    return m;
}

PlaybackSettings defaults()
{
    PlaybackSettings s;
    s.attackMs = 1.0f;
    s.decayMs = 1.0f;
    s.sustain = 1.0f;
    s.releaseMs = 20.0f;
    s.lowpassHz = 20000.0f;
    s.highpassHz = 20.0f;
    return s;
}

// Engine with a sample already adopted by the audio thread.
void load (SamplerEngine& engine, std::unique_ptr<SampleBuffer> sample)
{
    engine.prepare (hostRate, blockSize);
    engine.setSample (std::move (sample));
    run (engine, defaults(), blockSize, {});
}

juce::MidiMessage on (int note, juce::uint8 velocity = 127)
{
    return juce::MidiMessage::noteOn (1, note, velocity);
}
juce::MidiMessage off (int note)
{
    return juce::MidiMessage::noteOff (1, note);
}
} // namespace

class SamplerPitchTests final : public juce::UnitTest
{
public:
    SamplerPitchTests() : juce::UnitTest ("Sampler: pitch", "sampler") {}

    void runTest() override
    {
        // 441 Hz sine rendered at 44.1 kHz, played back at 48 kHz: sample-rate conversion is part of the
        // test.
        auto check =
            [this] (const juce::String& label, PlaybackSettings s, std::vector<Event> events, double expected)
        {
            beginTest (label);
            SamplerEngine engine;
            load (engine, makeSine (441.0, 2.0));
            const auto out = run (engine, s, ms (500), events);
            const double f = estimateFrequency (out, ms (50), ms (500));
            logMessage ("  measured " + juce::String (f, 2) + " Hz, expected " + juce::String (expected, 2) +
                        " Hz");
            expectWithinAbsoluteError (f, expected, expected * 0.002);
        };

        auto s = defaults();
        check ("root note plays at the original pitch", s, {{0, on (60)}}, 441.0);
        check ("octave up", s, {{0, on (72)}}, 882.0);
        check ("octave down", s, {{0, on (48)}}, 220.5);
        check ("fifth up", s, {{0, on (67)}}, 441.0 * std::exp2 (7.0 / 12.0));

        auto rootE = s;
        rootE.rootNote = 64;
        check ("root note parameter", rootE, {{0, on (64)}}, 441.0);

        auto tuned = s;
        tuned.fineTuneCents = 100.0f;
        check ("fine tune +100 cents = one semitone", tuned, {{0, on (60)}}, 441.0 * std::exp2 (1.0 / 12.0));

        auto fixed = s;
        fixed.keyTrack = false;
        check ("key tracking off keeps the pitch", fixed, {{0, on (72)}}, 441.0);

        check ("pitch bend up = +2 semitones", s,
               {{0, juce::MidiMessage::pitchWheel (1, 16383)}, {0, on (60)}},
               441.0 * std::exp2 ((2.0 * 8191.0 / 8192.0) / 12.0));
    }
};

class SamplerBehaviourTests final : public juce::UnitTest
{
public:
    SamplerBehaviourTests() : juce::UnitTest ("Sampler: playback behaviour", "sampler") {}

    void runTest() override
    {
        beginTest ("velocity amount scales the level");
        {
            auto s = defaults();
            SamplerEngine a, b;
            load (a, makeSine (441.0, 1.0));
            load (b, makeSine (441.0, 1.0));
            const auto loud = run (a, s, ms (200), {{0, on (60, 127)}});
            const auto soft = run (b, s, ms (200), {{0, on (60, 64)}});
            const double ratio = rms (soft, ms (50), ms (200)) / rms (loud, ms (50), ms (200));
            expectWithinAbsoluteError (ratio, 64.0 / 127.0, 0.01);

            s.velocityAmount = 0.0f;
            SamplerEngine c;
            load (c, makeSine (441.0, 1.0));
            const auto flat = run (c, s, ms (200), {{0, on (60, 64)}});
            expectWithinAbsoluteError (rms (flat, ms (50), ms (200)) / rms (loud, ms (50), ms (200)), 1.0,
                                       0.01);
        }

        beginTest ("note off releases to silence and frees the voice");
        {
            auto s = defaults();
            s.releaseMs = 50.0f;
            SamplerEngine engine;
            load (engine, makeSine (441.0, 2.0));
            const auto out = run (engine, s, ms (400), {{0, on (60)}, {ms (100), off (60)}});
            expectGreaterThan (rms (out, ms (50), ms (100)), 0.2);
            expectLessThan (rms (out, ms (200), ms (400)), 1e-5);
            expectEquals (engine.getNumActiveVoices(), 0);
        }

        beginTest ("without loop the voice ends with the sample");
        {
            auto s = defaults();
            SamplerEngine engine;
            load (engine, makeSine (441.0, 0.1));
            const auto out = run (engine, s, ms (300), {{0, on (60)}});
            expectGreaterThan (rms (out, ms (20), ms (80)), 0.2);
            expectLessThan (rms (out, ms (120), ms (300)), 1e-6);
            expectEquals (engine.getNumActiveVoices(), 0);
            // the 1.5 ms end fade avoids a click
            expectLessThan ((double)maxStep (out, ms (80), ms (120)), 0.06);
        }

        beginTest ("loop keeps the note sounding, crossfade avoids clicks");
        {
            auto s = defaults();
            s.loop = true;
            // Sample: 4410 samples, 100 per cycle. Loop 1323..3573 = 22.5 cycles, so the
            // waveform flips sign at the loop point (worst case for clicks).
            s.loopStart = 1323.0f / 4410.0f;
            s.loopEnd = 3573.0f / 4410.0f;
            s.loopCrossfadeMs = 10.0f;
            SamplerEngine engine;
            load (engine, makeSine (441.0, 0.1));
            const auto out = run (engine, s, ms (600), {{0, on (60)}});
            expectGreaterThan (rms (out, ms (400), ms (600)), 0.2);
            const float step = maxStep (out, ms (20), ms (600));
            logMessage ("  largest sample step with crossfade: " + juce::String (step, 4));
            expectLessThan ((double)step, 0.06); // a 441 Hz sine at 0.5 has steps of ~0.029

            s.loopCrossfadeMs = 0.0f;
            SamplerEngine hard;
            load (hard, makeSine (441.0, 0.1));
            const auto clicky = run (hard, s, ms (600), {{0, on (60)}});
            const float hardStep = maxStep (clicky, ms (20), ms (600));
            logMessage ("  largest sample step without crossfade: " + juce::String (hardStep, 4));
            expectGreaterThan ((double)hardStep, 0.2); // proves the test loop really has a discontinuity
        }

        beginTest ("reverse plays the sample backwards");
        {
            auto ramp = std::make_unique<SampleBuffer>();
            ramp->sampleRate = hostRate;
            ramp->samples.resize ((size_t)ms (200));
            for (size_t i = 0; i < ramp->samples.size(); ++i)
                ramp->samples[i] = (float)i / (float)ramp->samples.size();
            auto s = defaults();
            s.reverse = true;
            s.attackMs = 0.0f;
            SamplerEngine engine;
            load (engine, std::move (ramp));
            const auto out = run (engine, s, ms (250), {{0, on (60)}});
            expectGreaterThan (out[(size_t)ms (10)], 0.9f);
            expectLessThan (out[(size_t)ms (150)], 0.3f);
        }

        beginTest ("sample start skips the beginning");
        {
            auto half = makeSine (441.0, 1.0);
            std::fill (half->samples.begin(), half->samples.begin() + (long)half->samples.size() / 2, 0.0f);
            auto s = defaults();
            s.start = 0.5f;
            SamplerEngine engine;
            load (engine, std::move (half));
            const auto out = run (engine, s, ms (100), {{0, on (60)}});
            expectGreaterThan (rms (out, ms (5), ms (100)), 0.2);
        }

        beginTest ("mono mode: a new note replaces the old one");
        {
            auto s = defaults();
            s.mono = true;
            SamplerEngine engine;
            load (engine, makeSine (441.0, 2.0));
            const auto out = run (engine, s, ms (500), {{0, on (60)}, {ms (100), on (72)}});
            expectEquals (engine.getNumActiveVoices(), 1);
            expectWithinAbsoluteError (estimateFrequency (out, ms (150), ms (500)), 882.0, 2.0);
        }

        beginTest ("polyphony is limited, stolen notes fade out");
        {
            auto s = defaults();
            SamplerEngine engine;
            load (engine, makeSine (441.0, 2.0));
            std::vector<Event> chord;
            for (int i = 0; i < 20; ++i)
                chord.push_back ({i * 10, on (40 + i)});
            run (engine, s, ms (100), chord);
            expectEquals (engine.getNumActiveVoices(), SamplerEngine::maxPolyphony);
        }

        beginTest ("a new sample is used for new notes; held notes keep the old one");
        {
            auto s = defaults();
            SamplerEngine engine;
            load (engine, makeSine (441.0, 2.0));
            auto out = run (engine, s, ms (200), {{0, on (60)}});
            engine.setSample (makeSine (882.0, 2.0)); // swap while the note is held
            const auto after = run (engine, s, ms (300), {{ms (100), on (60)}});
            out.insert (out.end(), after.begin(), after.end());

            const int swapAt = ms (200);
            expectWithinAbsoluteError (estimateFrequency (out, swapAt, swapAt + ms (90)), 441.0, 1.0);
            expectLessThan ((double)maxStep (out, swapAt - ms (20), swapAt + ms (90)), 0.04); // no click
            expectEquals (engine.getNumActiveVoices(), 2);

            run (engine, s, ms (100), {{0, off (60)}});
            run (engine, s, ms (100), {});
            engine.collectGarbage(); // old sample is freed here (message thread)
            expectEquals (engine.getNumActiveVoices(), 0);
        }
    }
};

class SamplerRealtimeTests final : public juce::UnitTest
{
public:
    SamplerRealtimeTests() : juce::UnitTest ("Sampler: real-time safety", "sampler") {}

    void runTest() override
    {
        beginTest ("process() never allocates (notes, CCs, pitch bend, parameter changes, sample swap)");

        SamplerEngine engine;
        load (engine, makeSine (441.0, 0.5));

        // Everything that allocates happens before counting starts.
        juce::AudioBuffer<float> buffer (2, blockSize);
        std::vector<juce::MidiBuffer> blocks (64);
        for (int b = 0; b < 64; ++b)
        {
            for (int i = 0; i < 8; ++i)
                blocks[(size_t)b].addEvent (on (30 + (b * 7 + i * 5) % 60, (juce::uint8)(20 + i * 10)),
                                            i * 60);
            blocks[(size_t)b].addEvent (off (30 + (b * 3) % 60), 300);
            blocks[(size_t)b].addEvent (juce::MidiMessage::pitchWheel (1, (b * 997) % 16384), 400);
            if (b % 16 == 15)
                blocks[(size_t)b].addEvent (juce::MidiMessage::allNotesOff (1), 500);
        }
        std::vector<PlaybackSettings> settings (64, defaults());
        for (int b = 0; b < 64; ++b)
        {
            auto& s = settings[(size_t)b];
            s.loop = b % 2 == 0;
            s.reverse = b % 3 == 0;
            s.mono = b % 5 == 0;
            s.loopStart = 0.2f;
            s.loopEnd = 0.7f;
            s.gainDb = -6.0f + (float)(b % 10);
            s.lowpassHz = 2000.0f + 200.0f * (float)b;
            s.start = (float)(b % 4) * 0.1f;
        }
        auto replacement = makeSine (660.0, 0.3);

        long allocations = 0;
        for (int b = 0; b < 64; ++b)
        {
            if (b == 32)
                engine.setSample (std::move (replacement)); // producer side; not counted
            ScopedAllocationCounter counter;
            engine.process (buffer, blocks[(size_t)b], settings[(size_t)b]);
            allocations += counter.count();
        }
        logMessage ("  allocations during 64 process() calls: " + juce::String (allocations));
        expectEquals ((int)allocations, 0);

        beginTest ("the allocation counter itself works");
        {
            ScopedAllocationCounter counter;
            auto* p = new int (1);
            delete p;
            expectEquals ((int)counter.count(), 1);
        }
    }
};

class ParameterTests final : public juce::UnitTest
{
public:
    ParameterTests() : juce::UnitTest ("Parameters", "plugin") {}

    void runTest() override
    {
        beginTest ("every real-time parameter exists and is automatable");

        struct Dummy final : juce::AudioProcessor
        {
            const juce::String getName() const override { return "dummy"; }
            void prepareToPlay (double, int) override {}
            void releaseResources() override {}
            void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
            double getTailLengthSeconds() const override { return 0; }
            bool acceptsMidi() const override { return true; }
            bool producesMidi() const override { return false; }
            juce::AudioProcessorEditor* createEditor() override { return nullptr; }
            bool hasEditor() const override { return false; }
            int getNumPrograms() override { return 1; }
            int getCurrentProgram() override { return 0; }
            void setCurrentProgram (int) override {}
            const juce::String getProgramName (int) override { return {}; }
            void changeProgramName (int, const juce::String&) override {}
            void getStateInformation (juce::MemoryBlock&) override {}
            void setStateInformation (const void*, int) override {}
        } processor;

        juce::AudioProcessorValueTreeState apvts (processor, nullptr, "PARAMETERS", param::createLayout());
        expectEquals (processor.getParameters().size(), param::allIds.size());
        for (auto& id : param::allIds)
        {
            auto* parameter = apvts.getParameter (id);
            expect (parameter != nullptr, "missing parameter " + id);
            if (parameter != nullptr)
                expect (parameter->isAutomatable(), id + " must be automatable");
        }
    }
};

static SamplerPitchTests samplerPitchTests;
static SamplerBehaviourTests samplerBehaviourTests;
static SamplerRealtimeTests samplerRealtimeTests;
static ParameterTests parameterTests;
} // namespace its::test

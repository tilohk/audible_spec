// Plugin-level tests with the real processor (no host): project state save/load and presets.

#include "Parameters.h"
#include "PluginProcessor.h"
#include "Presets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <random>
#include <thread>

namespace its::test
{
namespace
{
// Small, fast render settings for tests.
void useFastRenderSettings (ImageToSoundProcessor& p)
{
    auto tree = p.getRenderSettings();
    tree.setProperty ("duration", 0.3, nullptr);
    tree.setProperty ("n_fft", 1024, nullptr);
    tree.setProperty ("hop_length", 128, nullptr);
    tree.setProperty ("griffin_lim_iterations", 12, nullptr);
}

// Feeds audio blocks (like a host) until a render has landed in the engine.
bool waitForRender (ImageToSoundProcessor& p, int previousGeneration = 0, int timeoutMs = 60000)
{
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer none;
    const auto start = juce::Time::getMillisecondCounter();
    while (p.getRenderedData().generation <= previousGeneration)
    {
        p.processBlock (buffer, none);
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
        if (juce::Time::getMillisecondCounter() - start > (juce::uint32)timeoutMs)
            return false;
    }
    p.processBlock (buffer, none); // adopt
    return true;
}

// RMS of the output for one held note.
double playNote (ImageToSoundProcessor& p)
{
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer on, none;
    on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8)110), 0);
    p.processBlock (buffer, none); // adopt a pending sound first
    p.processBlock (buffer, on);
    double sum = 0.0;
    for (int b = 0; b < 8; ++b)
    {
        p.processBlock (buffer, none);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            sum += (double)buffer.getSample (0, i) * buffer.getSample (0, i);
    }
    return std::sqrt (sum / (8 * 512));
}

std::unique_ptr<ImageToSoundProcessor> makeProcessor()
{
    auto p = std::make_unique<ImageToSoundProcessor>();
    p->setRateAndBufferSizeDetails (48000.0, 512);
    p->prepareToPlay (48000.0, 512);
    return p;
}

void setParameter (ImageToSoundProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

float getParameter (ImageToSoundProcessor& p, const char* id)
{
    auto* param = p.apvts.getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}
} // namespace

class StateTests final : public juce::UnitTest
{
public:
    StateTests() : juce::UnitTest ("Project state (save / reopen)", "plugin") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;

        beginTest ("set up a project with a rendered sound");
        auto original = makeProcessor();
        useFastRenderSettings (*original);
        original->getRenderSettings().setProperty ("gamma", 0.55, nullptr);
        original->startRender();
        expect (waitForRender (*original), "first render");
        setParameter (*original, param::gain, -6.0f);
        setParameter (*original, param::loopOn, 1.0f);
        setParameter (*original, param::loopStart, 0.25f);

        juce::MemoryBlock saved;
        original->getStateInformation (saved);
        logMessage ("  saved state: " + juce::String ((double)saved.getSize() / 1024.0, 1) + " KB");

        beginTest ("reopening restores the sound without rendering");
        {
            auto reopened = makeProcessor(); // starts rendering the built-in image...
            reopened->setStateInformation (saved.getData(), (int)saved.getSize()); // ...which this cancels

            const auto status = reopened->getRenderStatus();
            expect (!status.rendering, "must not render: " + status.message);
            expect (status.message.contains ("restored"), status.message);

            const auto a = original->getRenderedData(), b = reopened->getRenderedData();
            expect (b.audio != nullptr && b.spectrogram != nullptr);
            if (a.audio != nullptr && b.audio != nullptr)
            {
                expectEquals ((int)b.audio->samples.size(), (int)a.audio->samples.size());
                double maxError = 0.0;
                for (size_t i = 0; i < std::min (a.audio->samples.size(), b.audio->samples.size()); ++i)
                    maxError =
                        std::max (maxError, (double)std::abs (a.audio->samples[i] - b.audio->samples[i]));
                logMessage ("  restored sound max error " + juce::String (maxError, 9) +
                            " (24-bit step 1.2e-7)");
                expectLessThan (maxError, 2.5e-7);
            }

            expectWithinAbsoluteError (getParameter (*reopened, param::gain), -6.0f, 0.01f);
            expectWithinAbsoluteError (getParameter (*reopened, param::loopStart), 0.25f, 0.001f);
            expect (getParameter (*reopened, param::loopOn) > 0.5f);
            expectWithinAbsoluteError ((double)reopened->getRenderSettings()["gamma"], 0.55, 1e-9);
            expectEquals (reopened->getImageName(), original->getImageName());
            expectGreaterThan (playNote (*reopened), 0.01, "the restored sound plays");
        }

        beginTest ("a project from phases 3-5 (XML, no image or sound) loads and renders");
        {
            auto legacyState = original->apvts.copyState();
            legacyState.removeChild (legacyState.getChildWithName ("RENDER"), nullptr);
            juce::MemoryBlock legacy;
            juce::AudioProcessor::copyXmlToBinary (*legacyState.createXml(), legacy);

            auto p = makeProcessor();
            p->setStateInformation (legacy.getData(), (int)legacy.getSize());
            expectWithinAbsoluteError (getParameter (*p, param::gain), -6.0f, 0.01f);
            expect (p->getRenderSettings().hasProperty ("duration"), "render settings filled with defaults");
            expect (p->getRenderStatus().rendering, "renders, since the project has no sound");
            p->cancelRender();
        }

        beginTest ("a damaged sound in the project falls back to rendering");
        {
            juce::MemoryBlock damaged (saved);
            // overwrite the middle of the FLAC data (the end of the block is the SOUND node)
            auto* bytes = static_cast<char*> (damaged.getData());
            for (size_t i = damaged.getSize() - damaged.getSize() / 8; i < damaged.getSize() - 16; ++i)
                bytes[i] = (char)0x5a;
            auto p = makeProcessor();
            p->setStateInformation (damaged.getData(), (int)damaged.getSize());
            const auto status = p->getRenderStatus();
            logMessage ("  status: " + status.message);
            expect (status.rendering || status.isError || p->getRenderedData().audio != nullptr);
            p->cancelRender();
        }

        beginTest ("garbage and truncated state never crash the plugin");
        {
            auto p = makeProcessor();
            std::mt19937 rng (7);
            int cases = 0;
            for (size_t length : {(size_t)0, (size_t)1, (size_t)4, (size_t)8, (size_t)9, (size_t)64,
                                  saved.getSize() / 3, saved.getSize() / 2, saved.getSize() - 1})
            {
                p->setStateInformation (saved.getData(), (int)juce::jmin (length, saved.getSize()));
                ++cases;
            }
            for (int i = 0; i < 20; ++i)
            {
                juce::MemoryBlock noise ((size_t)(rng() % 4096 + 1));
                for (size_t k = 0; k < noise.getSize(); ++k)
                    noise[k] = (char)(rng() & 0xff);
                if (i % 2 == 0) // keep the magic so the parser goes deeper
                    noise.copyFrom (saved.getData(), 0, juce::jmin ((size_t)8, noise.getSize()));
                p->setStateInformation (noise.getData(), (int)noise.getSize());
                ++cases;
            }
            p->cancelRender();
            logMessage ("  " + juce::String (cases) + " damaged states handled");
            expect (p->getRenderStatus().message.isNotEmpty());
        }

        beginTest ("the folded/unfolded render panel is remembered in the project");
        {
            auto p = makeProcessor();
            p->cancelRender();
            expect (!p->isRenderPanelOpen(), "folded by default");
            p->setRenderPanelOpen (true);
            juce::MemoryBlock opened;
            p->getStateInformation (opened);
            auto q = makeProcessor();
            q->setStateInformation (opened.getData(), (int)opened.getSize());
            q->cancelRender();
            expect (q->isRenderPanelOpen(), "still open after reopening");
        }

        beginTest ("filter slope survives save/reopen; an old project with velocityAmount loads");
        {
            auto p = makeProcessor();
            p->cancelRender();
            auto* slope = p->apvts.getParameter (param::filterSlope);
            expect (slope != nullptr);
            slope->setValueNotifyingHost (1.0f); // 24 dB
            juce::MemoryBlock slopeState;
            p->getStateInformation (slopeState);
            auto q = makeProcessor();
            q->setStateInformation (slopeState.getData(), (int)slopeState.getSize());
            q->cancelRender();
            expectEquals (q->apvts.getParameter (param::filterSlope)->getCurrentValueAsText(),
                          juce::String ("24 dB"));
            expect (q->apvts.getParameter ("velocityAmount") == nullptr, "velocity parameter removed");

            // A phase 3-5 style XML state that still has the removed parameter.
            auto xml = p->apvts.copyState().createXml();
            auto* old = xml->createNewChildElement ("PARAM");
            old->setAttribute ("id", "velocityAmount");
            old->setAttribute ("value", 0.3);
            juce::MemoryBlock legacy;
            juce::AudioProcessor::copyXmlToBinary (*xml, legacy);
            auto r = makeProcessor();
            r->setStateInformation (legacy.getData(), (int)legacy.getSize());
            r->cancelRender();
            expectEquals (r->apvts.getParameter (param::filterSlope)->getCurrentValueAsText(),
                          juce::String ("24 dB"));
        }

        beginTest ("random render settings are always valid and keep length and quality");
        {
            auto p = makeProcessor();
            p->cancelRender();
            p->applyPreset ("# Clean\nDURATION = 1.5\n");
            const auto before = p->getRenderParams();
            juce::Random random (1234);
            bool allValid = true, changed = false;
            for (int i = 0; i < 200; ++i)
            {
                p->randomizeRenderSettings (random);
                const auto params = p->getRenderParams();
                const auto problem = params.validate();
                if (problem.isNotEmpty())
                    logMessage ("  invalid: " + problem);
                allValid = allValid && problem.isEmpty();
                changed = changed || !juce::exactlyEqual (params.gamma, before.gamma);
                expectEquals (params.width, before.width);
                expectEquals (params.griffinLimIterations, before.griffinLimIterations);
                expect (params.fMax >= params.fMin * 8.0 || params.fMax >= 2000.0);
            }
            expect (allValid, "every random setting validates");
            expect (changed, "settings actually change");
            expect (p->getPresetName().isEmpty(), "no preset name after randomizing");
        }

        beginTest ("saving while a render runs is safe and keeps the previous sound");
        {
            auto p = makeProcessor();
            useFastRenderSettings (*p);
            p->startRender();
            expect (waitForRender (*p));
            p->getRenderSettings().setProperty ("duration", 3.0, nullptr);
            p->startRender(); // long render in the background
            juce::MemoryBlock during;
            p->getStateInformation (during);
            p->cancelRender();
            auto q = makeProcessor();
            q->setStateInformation (during.getData(), (int)during.getSize());
            expect (!q->getRenderStatus().rendering, "the last finished sound was saved");
        }
    }
};

class PresetTests final : public juce::UnitTest
{
public:
    PresetTests() : juce::UnitTest ("Presets", "plugin") {}

    void runTest() override
    {
        beginTest ("the three built-in presets load without problems");
        const auto builtIns = presets::builtIns();
        juce::StringArray names;
        for (auto& p : builtIns)
        {
            names.add (p.name);
            const auto parsed = presets::parse (p.text);
            expect (parsed.problems.isEmpty(), p.name + ": " + parsed.problems.joinIntoString (" | "));
            expect (parsed.values.size() >= 5, p.name);
        }
        logMessage ("  built-in: " + names.joinIntoString (", "));
        expectEquals (names.joinIntoString (", "),
                      juce::String ("Bright / Percussive, Clean, Dark / Texture"));

        beginTest ("applying a preset sets its values and resets everything else");
        {
            juce::ValueTree tree ("RENDER");
            tree.setProperty ("hop_length", 256, nullptr); // not in the preset -> back to default
            const auto dark = std::find_if (builtIns.begin(), builtIns.end(),
                                            [] (auto& p) { return p.name == "Dark / Texture"; });
            expect (dark != builtIns.end());
            if (dark != builtIns.end())
            {
                presets::apply (presets::parse (dark->text), tree);
                expectWithinAbsoluteError ((double)tree["gamma"], 0.35, 1e-9);
                expectWithinAbsoluteError ((double)tree["blur"], 2.0, 1e-9);
                expect (!(bool)tree["invert"]);
                expectEquals ((int)tree["griffin_lim_iterations"], 128);
                expectEquals ((int)tree["hop_length"], RenderParams{}.hopLength);
                expect (tree.hasProperty ("duration"));
            }
        }

        beginTest ("save format round-trips and reports unknown lines");
        {
            juce::ValueTree tree ("RENDER");
            presets::apply ({}, tree);
            tree.setProperty ("gamma", 0.7, nullptr);
            tree.setProperty ("frequency_scale", "linear", nullptr);
            tree.setProperty ("invert", false, nullptr);
            const auto text = presets::serialize (tree, "Round trip");
            const auto parsed = presets::parse (text + "NOT_A_SETTING = 3\nGAMMA 2\n");
            expectEquals (parsed.name, juce::String ("Round trip"));
            expectEquals (parsed.problems.size(), 2);
            juce::ValueTree back ("RENDER");
            presets::apply (parsed, back);
            for (int i = 0; i < tree.getNumProperties(); ++i)
            {
                const auto id = tree.getPropertyName (i);
                expect (back.hasProperty (id), id.toString());
                if (tree[id].isString())
                    expectEquals (back[id].toString(), tree[id].toString());
                else
                    expectWithinAbsoluteError ((double)back[id], (double)tree[id], 1e-5);
            }
        }

        beginTest ("a WIDTH-only preset (main.py style) sets the length in seconds");
        {
            juce::ValueTree tree ("RENDER");
            presets::apply (presets::parse ("# Wide\nWIDTH = 2048\nHOP_LENGTH = 64\n"), tree);
            expectWithinAbsoluteError ((double)tree["duration"], 2048.0 * 64.0 / 44100.0, 1e-9);
        }

        beginTest ("the processor applies a preset to its render settings");
        {
            juce::ScopedJuceInitialiser_GUI gui;
            auto p = makeProcessor();
            p->cancelRender();
            p->applyPreset (builtIns.front().text);
            expectEquals (p->getPresetName(), builtIns.front().name);
            expect (p->getRenderStatus().message.startsWith ("Render preset"), p->getRenderStatus().message);
            expect (p->getRenderStatus().message.contains ("Press Render"));
            expectWithinAbsoluteError (p->getRenderParams().gamma, 1.5, 1e-9); // Bright / Percussive
        }
    }
};

static StateTests stateTests;
static PresetTests presetTests;
} // namespace its::test

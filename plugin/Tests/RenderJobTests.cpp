// Tests for background rendering (RenderJob) and the hand-over of a freshly rendered
// sound to the playback engine while notes are playing.

#include "AllocationCounter.h"

#include "RenderJob.h"
#include "audio/SamplerEngine.h"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace its::test
{
namespace
{
const juce::File repoRoot{ITS_REPO_ROOT};

juce::MemoryBlock imageBytes (const char* name)
{
    juce::MemoryBlock data;
    repoRoot.getChildFile ("reference/test_images").getChildFile (name).loadFileAsData (data);
    return data;
}

RenderParams smallParams()
{
    RenderParams p;
    p.nFft = 1024;
    p.hopLength = 128;
    p.width = 64;
    p.griffinLimIterations = 8;
    return p;
}

RenderParams bigParams() // many seconds of work
{
    RenderParams p;
    p.nFft = 4096;
    p.hopLength = 32;
    p.width = 4096;
    p.griffinLimIterations = 500;
    return p;
}

// Collects RenderJob callbacks (which arrive on the render thread).
struct Collector
{
    std::mutex mutex;
    std::condition_variable cv;
    int calls = 0;
    std::unique_ptr<SampleBuffer> sample;
    std::shared_ptr<const FloatMatrix> spectrogram;
    juce::String error;
    std::thread::id thread;
    SamplerEngine* forwardTo = nullptr;

    RenderJob::Callback callback()
    {
        return [this] (RenderJob::Output out)
        {
            if (forwardTo != nullptr && out.sample != nullptr)
                forwardTo->setSample (std::move (out.sample)); // like the processor: render thread -> engine
            const std::lock_guard<std::mutex> lock (mutex);
            ++calls;
            sample = std::move (out.sample);
            spectrogram = out.spectrogram;
            error = out.error;
            thread = std::this_thread::get_id();
            cv.notify_all();
        };
    }

    bool waitForCalls (int n, int timeoutMs)
    {
        std::unique_lock<std::mutex> lock (mutex);
        return cv.wait_for (lock, std::chrono::milliseconds (timeoutMs), [&] { return calls >= n; });
    }

    int numCalls()
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return calls;
    }
};

void sleepMs (int ms)
{
    std::this_thread::sleep_for (std::chrono::milliseconds (ms));
}
} // namespace

class RenderJobTests final : public juce::UnitTest
{
public:
    RenderJobTests() : juce::UnitTest ("Render job (background rendering)", "render") {}

    void runTest() override
    {
        beginTest ("renders on a background thread and reports progress");
        {
            Collector c;
            RenderJob job (c.callback());
            const auto params = smallParams();
            job.start (imageBytes ("gradient_rgb.png"), params);
            expect (c.waitForCalls (1, 20000), "render did not finish");
            expect (c.thread != std::this_thread::get_id(), "callback must come from the render thread");
            expect (c.error.isEmpty(), c.error);
            expect (c.sample != nullptr);
            expect (c.spectrogram != nullptr && c.spectrogram->rows == RenderJob::spectrogramRows,
                    "spectrogram picture for the UI");
            if (c.sample != nullptr)
            {
                expectEquals ((int)c.sample->samples.size(), params.hopLength * (params.width - 1));
                expect (juce::exactlyEqual (c.sample->sampleRate, (double)params.sampleRate));
            }
            sleepMs (50);
            expect (!job.isRendering());
            expect (juce::exactlyEqual (job.getProgress(), 1.0f));
        }

        beginTest ("cancel stops a long render quickly and reports nothing");
        {
            Collector c;
            RenderJob job (c.callback());
            job.start (imageBytes ("scene_portrait.heic"), bigParams());
            for (int i = 0; i < 200 && job.getProgress() <= 0.0f; ++i)
                sleepMs (10); // wait until Griffin-Lim is running
            expect (job.isRendering());

            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            job.cancel();
            const auto cancelMs = juce::Time::getMillisecondCounterHiRes() - t0;
            logMessage ("  cancel took " + juce::String (cancelMs, 1) + " ms at progress " +
                        juce::String (job.getProgress() * 100.0f, 1) + " %");
            expectLessThan (cancelMs, 1000.0);
            sleepMs (100);
            expectEquals (c.numCalls(), 0);
            expect (!job.isRendering());
        }

        beginTest ("starting a new render cancels the one in progress");
        {
            Collector c;
            RenderJob job (c.callback());
            job.start (imageBytes ("scene_portrait.heic"), bigParams());
            sleepMs (100);
            const auto small = smallParams();
            job.start (imageBytes ("gradient_rgb.png"), small);
            expect (c.waitForCalls (1, 20000));
            sleepMs (300);
            expectEquals (c.numCalls(), 1, "only the second render may report");
            if (c.sample != nullptr)
                expectEquals ((int)c.sample->samples.size(), small.hopLength * (small.width - 1));
        }

        beginTest ("unreadable image and invalid settings are reported as errors");
        {
            Collector c;
            RenderJob job (c.callback());
            const char garbage[] = "this is not an image";
            job.start (juce::MemoryBlock (garbage, sizeof (garbage)), smallParams());
            expect (c.waitForCalls (1, 5000));
            expect (c.sample == nullptr);
            expect (c.error.isNotEmpty());
            logMessage ("  error text: " + c.error);

            auto bad = smallParams();
            bad.fMin = 9000.0;
            bad.fMax = 100.0;
            job.start (imageBytes ("gradient_rgb.png"), bad);
            expect (c.waitForCalls (2, 5000));
            expect (c.sample == nullptr);
            expect (c.error.contains ("FMIN"), c.error);
        }
    }
};

class LiveSwapTests final : public juce::UnitTest
{
public:
    LiveSwapTests() : juce::UnitTest ("Render while playing (end to end)", "render") {}

    void runTest() override
    {
        beginTest ("a sound rendered while a note is held swaps in without touching that note");

        constexpr double hostRate = 48000.0;
        constexpr int block = 256;
        auto makeSample = []
        {
            auto s = std::make_unique<SampleBuffer>();
            s->sampleRate = 44100.0;
            s->samples.resize (44100 * 3);
            for (size_t i = 0; i < s->samples.size(); ++i)
                s->samples[i] = 0.4f * (float)std::sin (2.0 * M_PI * 330.0 * (double)i / 44100.0);
            return s;
        };

        // Two identical engines hold the same note. Only `live` gets the new render.
        SamplerEngine reference, live;
        PlaybackSettings settings;
        juce::AudioBuffer<float> refOut (2, block), liveOut (2, block);
        juce::MidiBuffer noteOn, empty, secondNote;
        noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8)100), 0);
        secondNote.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8)100), 0);
        for (auto* e : {&reference, &live})
        {
            e->prepare (hostRate, block);
            e->setSample (makeSample());
        }

        Collector c;
        c.forwardTo = &live;
        RenderJob job (c.callback());

        bool identical = true;
        long allocations = 0;
        int blocks = 0;
        auto processBoth = [&] (const juce::MidiBuffer& midi)
        {
            reference.process (refOut, midi, settings);
            {
                ScopedAllocationCounter counter;
                live.process (liveOut, midi, settings);
                allocations += counter.count();
            }
            for (int i = 0; i < block; ++i)
                identical =
                    identical && juce::exactlyEqual (refOut.getSample (0, i), liveOut.getSample (0, i));
            ++blocks;
        };

        processBoth (noteOn);
        job.start (imageBytes ("gradient_rgb.png"), smallParams()); // renders while we keep playing
        while (c.numCalls() == 0 && blocks < 4000)
        {
            processBoth (empty);
            std::this_thread::sleep_for (std::chrono::microseconds (500));
        }
        expectEquals (c.numCalls(), 1, "render finished during playback");
        for (int i = 0; i < 20; ++i)
            processBoth (empty); // the audio thread adopts the new sound here

        logMessage ("  " + juce::String (blocks) + " blocks played while rendering and swapping");
        expect (identical, "the held note must be bit-identical to playback without a swap");
        expectEquals ((int)allocations, 0, "allocations in process() during the swap");

        // A new note now uses the new sound in `live` but not in `reference`.
        processBoth (secondNote);
        bool differs = false;
        for (int i = 0; i < 10; ++i)
        {
            reference.process (refOut, empty, settings);
            live.process (liveOut, empty, settings);
            for (int s = 0; s < block; ++s)
                differs = differs || !juce::exactlyEqual (refOut.getSample (0, s), liveOut.getSample (0, s));
        }
        expect (differs, "new notes must use the newly rendered sound");
    }
};

static RenderJobTests renderJobTests;
static LiveSwapTests liveSwapTests;
} // namespace its::test

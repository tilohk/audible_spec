#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace its
{
// A rendered sound (mono) at the sample rate it was rendered at.
struct SampleBuffer
{
    std::vector<float> samples;
    double sampleRate = 44100.0;
};

// Values of the real-time parameters, read once per block by the processor.
struct PlaybackSettings
{
    float gainDb = 0.0f;
    float attackMs = 5.0f, decayMs = 200.0f, sustain = 1.0f, releaseMs = 200.0f;
    int rootNote = 60;
    bool keyTrack = true;
    float fineTuneCents = 0.0f;
    float start = 0.0f, end = 1.0f; // fractions of the sample
    bool loop = false;
    float loopStart = 0.0f, loopEnd = 1.0f; // fractions of the sample
    float loopCrossfadeMs = 10.0f;
    bool reverse = false;
    float lowpassHz = 20000.0f, highpassHz = 20.0f;
    float velocityAmount = 1.0f;
    bool mono = false;
};

// Single-producer / single-consumer queue of owned pointers, lock-free.
template <typename T, int Capacity> class PointerQueue
{
public:
    bool push (T* item) noexcept
    {
        auto scope = fifo.write (1);
        if (scope.blockSize1 == 0)
            return false;
        items[(size_t)scope.startIndex1] = item;
        return true;
    }

    T* pop() noexcept
    {
        auto scope = fifo.read (1);
        return scope.blockSize1 == 0 ? nullptr : items[(size_t)scope.startIndex1];
    }

    int size() const noexcept { return fifo.getNumReady(); }

private:
    juce::AbstractFifo fifo{Capacity};
    std::array<T*, (size_t)Capacity> items{};
};

// Polyphonic sample player for the rendered sound. Lock-free and allocation-free
// on the audio thread (see .claude/rules/realtime-audio.md).
//
// Threads:
//  - setSample():       one producer thread at a time (the render thread)
//  - collectGarbage():  message thread (frees samples the audio thread let go of)
//  - prepare():         before playback starts (no concurrent process())
//  - process():         audio thread
//
// A new sample is used for new notes immediately; voices still playing an older
// sample keep it until they finish, so swapping never clicks.
class SamplerEngine
{
public:
    static constexpr int maxPolyphony = 16;  // notes sounding at once
    static constexpr int voicePoolSize = 24; // extra voices for fade-outs of stolen notes
    static constexpr float pitchBendRangeSemitones = 2.0f;

    SamplerEngine();
    ~SamplerEngine();

    void prepare (double sampleRate, int maxBlockSize);

    // Returns false if the hand-over queue is full (try again later).
    bool setSample (std::unique_ptr<SampleBuffer> sample);
    void collectGarbage();
    bool hasSample() const noexcept { return sampleAvailable.load(); }

    void process (juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi,
                  const PlaybackSettings& settings);
    int getNumActiveVoices() const noexcept;

private:
    struct Voice
    {
        const SampleBuffer* sample = nullptr;
        int note = -1;
        float velocityGain = 1.0f;
        double position = 0.0;
        juce::ADSR adsr;
        bool active = false;
        bool killing = false; // fast fade-out (stolen / mono retrigger)
        int killRemaining = 0;
        std::uint64_t startOrder = 0;
    };

    struct Region
    {
        double start, end, loopStart, loopEnd, crossfade;
        bool loop;
    };

    void startBlock (const PlaybackSettings& settings);
    void renderVoices (juce::AudioBuffer<float>& output, int startSample, int numSamples,
                       const PlaybackSettings& settings);
    void renderVoice (Voice& voice, float* out, int numSamples, const PlaybackSettings& settings);
    // Parses raw MIDI bytes (no juce::MidiMessage, so nothing is allocated for long messages).
    void handleMidi (const juce::uint8* data, int numBytes, const PlaybackSettings& settings);
    void noteOn (int note, float velocity, const PlaybackSettings& settings);
    void noteOff (int note);
    void killVoice (Voice& voice);
    double pitchIncrement (const Voice& voice, const PlaybackSettings& settings) const;
    Region regionFor (const SampleBuffer& sample, const PlaybackSettings& settings) const;
    void retireUnusedSamples();

    double hostSampleRate = 44100.0;
    int maxBlock = 512;
    std::array<Voice, (size_t)voicePoolSize> voices;
    std::uint64_t noteCounter = 0;
    float pitchBend = 0.0f; // -1..1

    const SampleBuffer* current = nullptr;         // owned; used for new notes
    std::array<const SampleBuffer*, 8> draining{}; // owned; still used by sounding voices
    PointerQueue<SampleBuffer, 8> incoming;        // producer -> audio thread
    PointerQueue<const SampleBuffer, 32> retired;  // audio thread -> message thread
    std::atomic<bool> sampleAvailable{false};

    std::vector<float> mix;
    juce::dsp::StateVariableTPTFilter<float> lowpass, highpass;
    bool lowpassActive = false, highpassActive = false;
    juce::SmoothedValue<float> gain;
    juce::ADSR::Parameters adsrParameters;
    int killFadeSamples = 220;
    int endFadeSamples = 64;
};
} // namespace its

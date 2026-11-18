#include "SamplerEngine.h"

#include <cmath>

namespace its
{
namespace
{
// 4-point cubic Hermite interpolation; indices outside the sample clamp to its edges.
inline float readInterpolated (const float* data, int length, double position) noexcept
{
    const auto index = (int)std::floor (position);
    const auto t = (float)(position - index);
    auto at = [data, length] (int i) noexcept { return data[juce::jlimit (0, length - 1, i)]; };
    const float xm1 = at (index - 1), x0 = at (index), x1 = at (index + 1), x2 = at (index + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}
} // namespace

SamplerEngine::SamplerEngine()
{
    lowpass.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    highpass.setType (juce::dsp::StateVariableTPTFilterType::highpass);
}

SamplerEngine::~SamplerEngine()
{
    // No audio thread is running any more: free everything we own.
    delete current;
    for (auto* s : draining)
        delete s;
    while (auto* s = incoming.pop())
        delete s;
    collectGarbage();
}

void SamplerEngine::prepare (double sampleRate, int maxBlockSize)
{
    hostSampleRate = sampleRate;
    maxBlock = juce::jmax (1, maxBlockSize);
    mix.assign ((size_t)maxBlock, 0.0f);

    for (auto& v : voices)
    {
        v.adsr.setSampleRate (sampleRate);
        v.adsr.reset();
        v.active = false;
    }

    const juce::dsp::ProcessSpec spec{sampleRate, (juce::uint32)maxBlock, 1};
    lowpass.prepare (spec);
    highpass.prepare (spec);

    gain.reset (sampleRate, 0.02);
    gain.setCurrentAndTargetValue (1.0f);

    killFadeSamples = juce::jmax (1, (int)(0.005 * sampleRate)); // 5 ms
    endFadeSamples = juce::jmax (1, (int)(0.0015 * sampleRate)); // 1.5 ms
}

bool SamplerEngine::setSample (std::unique_ptr<SampleBuffer> sample)
{
    auto* raw = sample.release();
    if (raw == nullptr || incoming.push (raw))
        return raw != nullptr;
    delete raw; // queue full
    return false;
}

void SamplerEngine::collectGarbage()
{
    while (auto* s = retired.pop())
        delete s;
}

int SamplerEngine::getNumActiveVoices() const noexcept
{
    int n = 0;
    for (auto& v : voices)
        n += v.active ? 1 : 0;
    return n;
}

// RT-SAFE BEGIN
void SamplerEngine::process (juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi,
                             const PlaybackSettings& settings)
{
    startBlock (settings);

    const int total = output.getNumSamples();
    int position = 0;
    for (const auto metadata : midi)
    {
        const int eventPosition = juce::jlimit (0, total, metadata.samplePosition);
        if (eventPosition > position)
        {
            renderVoices (output, position, eventPosition - position, settings);
            position = eventPosition;
        }
        handleMidi (metadata.data, metadata.numBytes, settings);
    }
    if (position < total)
        renderVoices (output, position, total - position, settings);
}

void SamplerEngine::startBlock (const PlaybackSettings& settings)
{
    // Adopt newly rendered samples. The previous one keeps playing in its voices.
    while (incoming.size() > 0)
    {
        const SampleBuffer** freeSlot = nullptr;
        for (auto& slot : draining)
            if (slot == nullptr)
                freeSlot = &slot;
        if (current != nullptr && freeSlot == nullptr)
            break; // all draining slots busy: adopt next block

        auto* next = incoming.pop();
        if (current != nullptr)
            *freeSlot = current;
        current = next;
        sampleAvailable.store (true);
    }
    retireUnusedSamples();

    adsrParameters = {settings.attackMs * 0.001f, settings.decayMs * 0.001f, settings.sustain,
                      settings.releaseMs * 0.001f};
    for (auto& v : voices)
        if (v.active && !v.killing)
            v.adsr.setParameters (adsrParameters);

    // A filter at its end stop (low-pass 20 kHz, high-pass 20 Hz) is bypassed, so the
    // default settings leave the sound untouched. Re-enabling starts from a clean state.
    const auto maxCutoff = (float)(0.45 * hostSampleRate);
    const bool wantLowpass = settings.lowpassHz < 19990.0f;
    const bool wantHighpass = settings.highpassHz > 20.5f;
    if (wantLowpass && !lowpassActive)
        lowpass.reset();
    if (wantHighpass && !highpassActive)
        highpass.reset();
    lowpassActive = wantLowpass;
    highpassActive = wantHighpass;
    lowpass.setCutoffFrequency (juce::jlimit (20.0f, maxCutoff, settings.lowpassHz));
    highpass.setCutoffFrequency (juce::jlimit (20.0f, maxCutoff, settings.highpassHz));
    gain.setTargetValue (juce::Decibels::decibelsToGain (settings.gainDb, -60.0f));
}

void SamplerEngine::retireUnusedSamples()
{
    for (auto& slot : draining)
    {
        if (slot == nullptr)
            continue;
        bool inUse = false;
        for (auto& v : voices)
            inUse = inUse || (v.active && v.sample == slot);
        if (!inUse && retired.push (slot))
            slot = nullptr;
    }
}

void SamplerEngine::renderVoices (juce::AudioBuffer<float>& output, int startSample, int numSamples,
                                  const PlaybackSettings& settings)
{
    while (numSamples > 0)
    {
        const int chunk = juce::jmin (numSamples, maxBlock);
        std::fill (mix.begin(), mix.begin() + chunk, 0.0f);

        for (auto& v : voices)
            if (v.active)
                renderVoice (v, mix.data(), chunk, settings);

        for (int i = 0; i < chunk; ++i)
        {
            float y = mix[(size_t)i];
            if (lowpassActive)
                y = lowpass.processSample (0, y);
            if (highpassActive)
                y = highpass.processSample (0, y);
            y *= gain.getNextValue();
            for (int ch = 0; ch < output.getNumChannels(); ++ch)
                output.setSample (ch, startSample + i, y);
        }
        startSample += chunk;
        numSamples -= chunk;
    }
}

SamplerEngine::Region SamplerEngine::regionFor (const SampleBuffer& sample,
                                                const PlaybackSettings& settings) const
{
    const auto length = (double)sample.samples.size();
    Region r{};
    r.start = juce::jlimit (0.0, length - 2.0, (double)settings.start * length);
    r.end = juce::jlimit (r.start + 1.0, length, (double)settings.end * length);
    r.loopStart = juce::jlimit (r.start, r.end, (double)settings.loopStart * length);
    r.loopEnd = juce::jlimit (r.start, r.end, (double)settings.loopEnd * length);
    r.loop = settings.loop && (r.loopEnd - r.loopStart) >= 4.0;

    // The crossfade reads before loopStart (forward) or after loopEnd (reverse).
    const double loopLength = r.loopEnd - r.loopStart;
    const double room = settings.reverse ? length - r.loopEnd : r.loopStart;
    r.crossfade = juce::jlimit (0.0, juce::jmin (loopLength * 0.5, room),
                                (double)settings.loopCrossfadeMs * 0.001 * sample.sampleRate);
    return r;
}

double SamplerEngine::pitchIncrement (const Voice& voice, const PlaybackSettings& settings) const
{
    const double semitones = (settings.keyTrack ? (double)(voice.note - settings.rootNote) : 0.0) +
                             settings.fineTuneCents / 100.0 + (double)(pitchBend * pitchBendRangeSemitones);
    return std::exp2 (semitones / 12.0) * voice.sample->sampleRate / hostSampleRate;
}

void SamplerEngine::renderVoice (Voice& voice, float* out, int numSamples, const PlaybackSettings& settings)
{
    const auto& sample = *voice.sample;
    const int length = (int)sample.samples.size();
    if (length < 4)
    {
        voice.active = false;
        return;
    }

    const float* data = sample.samples.data();
    const Region r = regionFor (sample, settings);
    const double loopLength = r.loopEnd - r.loopStart;
    const double increment = pitchIncrement (voice, settings);
    const bool reverse = settings.reverse;

    for (int i = 0; i < numSamples; ++i)
    {
        // Wrap inside the loop, or stop at the end of the region.
        if (r.loop)
        {
            if (!reverse && voice.position >= r.loopEnd)
                voice.position = r.loopStart + std::fmod (voice.position - r.loopStart, loopLength);
            else if (reverse && voice.position < r.loopStart)
                voice.position = r.loopEnd - std::fmod (r.loopEnd - voice.position, loopLength);
        }
        else if ((!reverse && voice.position >= r.end) || (reverse && voice.position < r.start))
        {
            voice.active = false;
            return;
        }

        float x = readInterpolated (data, length, voice.position);

        // Linear crossfade into the other end of the loop. (Equal-power would boost by up to
        // +3 dB, because both ends of a loop of the same sound are correlated.)
        if (r.loop && r.crossfade > 0.0)
        {
            const double fadeStart = reverse ? r.loopStart + r.crossfade : r.loopEnd - r.crossfade;
            const double into = reverse ? fadeStart - voice.position : voice.position - fadeStart;
            if (into > 0.0)
            {
                const auto t = (float)(into / r.crossfade);
                const double other = reverse ? voice.position + loopLength : voice.position - loopLength;
                x = x * (1.0f - t) + readInterpolated (data, length, other) * t;
            }
        }

        float g = voice.velocityGain * voice.adsr.getNextSample();

        // Short fade at the natural end of the sample (no loop) to avoid a click.
        if (!r.loop)
        {
            const double remaining =
                (reverse ? voice.position - r.start : r.end - voice.position) / increment;
            if (remaining < endFadeSamples)
                g *= (float)(remaining / endFadeSamples);
        }

        if (voice.killing)
        {
            g *= (float)voice.killRemaining / (float)killFadeSamples;
            if (--voice.killRemaining <= 0)
                voice.active = false;
        }

        out[i] += x * g;
        voice.position += reverse ? -increment : increment;

        if (!voice.active || !voice.adsr.isActive())
        {
            voice.active = false;
            return;
        }
    }
}

void SamplerEngine::handleMidi (const juce::uint8* data, int numBytes, const PlaybackSettings& settings)
{
    if (numBytes < 1)
        return;
    const int status = data[0] & 0xf0;
    const int d1 = numBytes > 1 ? data[1] : 0;
    const int d2 = numBytes > 2 ? data[2] : 0;

    if (status == 0x90 && d2 > 0)
        noteOn (d1, (float)d2 / 127.0f, settings);
    else if (status == 0x80 || (status == 0x90 && d2 == 0))
        noteOff (d1);
    else if (status == 0xe0)
        pitchBend = (float)((d2 << 7 | d1) - 8192) / 8192.0f;
    else if (status == 0xb0 && (d1 == 120 || d1 == 123)) // all sound off / all notes off
        for (auto& v : voices)
            if (v.active && !v.killing)
                d1 == 120 ? killVoice (v) : v.adsr.noteOff();
}

void SamplerEngine::noteOn (int note, float velocity, const PlaybackSettings& settings)
{
    if (current == nullptr || current->samples.size() < 4)
        return;

    // Enforce the polyphony limit by fading out the oldest sounding note(s).
    const int limit = settings.mono ? 1 : maxPolyphony;
    int sounding = 0;
    Voice* oldest = nullptr;
    for (auto& v : voices)
        if (v.active && !v.killing)
        {
            ++sounding;
            if (oldest == nullptr || v.startOrder < oldest->startOrder)
                oldest = &v;
        }
    if (settings.mono)
    {
        for (auto& v : voices)
            if (v.active && !v.killing)
                killVoice (v);
    }
    else if (sounding >= limit && oldest != nullptr)
    {
        killVoice (*oldest);
    }

    // Free voice, or else the oldest one that is already fading out.
    Voice* voice = nullptr;
    for (auto& v : voices)
        if (!v.active)
        {
            voice = &v;
            break;
        }
    if (voice == nullptr)
        for (auto& v : voices)
            if (voice == nullptr || v.startOrder < voice->startOrder)
                voice = &v;

    voice->sample = current;
    voice->note = note;
    voice->velocityGain = 1.0f - settings.velocityAmount + settings.velocityAmount * velocity;
    const Region r = regionFor (*current, settings);
    voice->position = settings.reverse ? r.end - 1.0 : r.start;
    voice->adsr.setParameters (adsrParameters);
    voice->adsr.reset();
    voice->adsr.noteOn();
    voice->active = true;
    voice->killing = false;
    voice->startOrder = ++noteCounter;
}

void SamplerEngine::noteOff (int note)
{
    for (auto& v : voices)
        if (v.active && !v.killing && v.note == note)
            v.adsr.noteOff();
}

void SamplerEngine::killVoice (Voice& voice)
{
    voice.killing = true;
    voice.killRemaining = killFadeSamples;
}
// RT-SAFE END
} // namespace its

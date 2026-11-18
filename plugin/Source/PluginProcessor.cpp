#include "PluginProcessor.h"

#include "BinaryData.h"
#include "Parameters.h"

namespace param = its::param;

ImageToSoundProcessor::ImageToSoundProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", param::createLayout()),
      renderJob (
          [this] (std::unique_ptr<its::SampleBuffer> sample, const juce::String& error)
          {
              // Render thread: the only producer for the engine's hand-over queue.
              jassert (error.isEmpty());
              juce::ignoreUnused (error);
              if (sample != nullptr)
                  engine.setSample (std::move (sample));
          })
{
    auto raw = [this] (const char* id)
    {
        auto* value = apvts.getRawParameterValue (id);
        jassert (value != nullptr);
        return value;
    };
    p = {raw (param::gain),        raw (param::attack),         raw (param::decay),    raw (param::sustain),
         raw (param::release),     raw (param::rootNote),       raw (param::keyTrack), raw (param::fineTune),
         raw (param::sampleStart), raw (param::sampleEnd),      raw (param::loopOn),   raw (param::loopStart),
         raw (param::loopEnd),     raw (param::loopCrossfade),  raw (param::reverse),  raw (param::lowpass),
         raw (param::highpass),    raw (param::velocityAmount), raw (param::playMode)};

    // Until phase 4 adds image loading: render the built-in test image with the default settings.
    renderJob.start (
        juce::MemoryBlock (BinaryData::scene_portrait_heic, (size_t)BinaryData::scene_portrait_heicSize),
        its::RenderParams{});
    startTimerHz (5);
}

ImageToSoundProcessor::~ImageToSoundProcessor()
{
    stopTimer();
    renderJob.cancel();
}

void ImageToSoundProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
}

bool ImageToSoundProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

double ImageToSoundProcessor::getTailLengthSeconds() const
{
    return p.release->load() / 1000.0;
}

// RT-SAFE BEGIN
its::PlaybackSettings ImageToSoundProcessor::readSettings() const noexcept
{
    its::PlaybackSettings s;
    s.gainDb = p.gain->load();
    s.attackMs = p.attack->load();
    s.decayMs = p.decay->load();
    s.sustain = p.sustain->load();
    s.releaseMs = p.release->load();
    s.rootNote = juce::roundToInt (p.rootNote->load());
    s.keyTrack = p.keyTrack->load() > 0.5f;
    s.fineTuneCents = p.fineTune->load();
    s.start = p.sampleStart->load();
    s.end = p.sampleEnd->load();
    s.loop = p.loopOn->load() > 0.5f;
    s.loopStart = p.loopStart->load();
    s.loopEnd = p.loopEnd->load();
    s.loopCrossfadeMs = p.loopCrossfade->load();
    s.reverse = p.reverse->load() > 0.5f;
    s.lowpassHz = p.lowpass->load();
    s.highpassHz = p.highpass->load();
    s.velocityAmount = p.velocityAmount->load();
    s.mono = juce::roundToInt (p.playMode->load()) == 0;
    return s;
}

void ImageToSoundProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    engine.process (buffer, midi, readSettings());
}
// RT-SAFE END

void ImageToSoundProcessor::timerCallback()
{
    engine.collectGarbage();
}

juce::AudioProcessorEditor* ImageToSoundProcessor::createEditor()
{
    // Generic slider UI until the custom editor arrives in phase 5.
    return new juce::GenericAudioProcessorEditor (*this);
}

void ImageToSoundProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ImageToSoundProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ImageToSoundProcessor();
}

#include "PluginProcessor.h"

#include "BinaryData.h"
#include "Parameters.h"
#include "PluginEditor.h"

namespace param = its::param;

ImageToSoundProcessor::ImageToSoundProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", param::createLayout()),
      renderJob ([this] (std::unique_ptr<its::SampleBuffer> sample, const juce::String& error)
                 { onRenderFinished (std::move (sample), error); })
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

    ensureRenderSettings();

    // Start with the built-in test image so the instrument makes a sound right away.
    imageData =
        juce::MemoryBlock (BinaryData::scene_portrait_heic, (size_t)BinaryData::scene_portrait_heicSize);
    imageName = "Built-in test image";
    startRender();
    startTimerHz (5);
}

ImageToSoundProcessor::~ImageToSoundProcessor()
{
    stopTimer();
    renderJob.cancel();
}

// ---------------------------------------------------------------- rendering

void ImageToSoundProcessor::ensureRenderSettings()
{
    // Add any missing render setting (new state, or a project saved by an older version).
    auto tree = apvts.state.getOrCreateChildWithName ("RENDER", nullptr);
    const auto defaults = its::RenderParams{}.toJson();
    for (auto& property : defaults.getDynamicObject()->getProperties())
        if (!tree.hasProperty (property.name))
            tree.setProperty (property.name, property.value, nullptr);
}

juce::ValueTree ImageToSoundProcessor::getRenderSettings()
{
    ensureRenderSettings();
    return apvts.state.getChildWithName ("RENDER");
}

its::RenderParams ImageToSoundProcessor::getRenderParams()
{
    auto tree = getRenderSettings();
    auto* object = new juce::DynamicObject();
    for (int i = 0; i < tree.getNumProperties(); ++i)
        object->setProperty (tree.getPropertyName (i), tree.getProperty (tree.getPropertyName (i)));
    return its::RenderParams::fromJson (juce::var (object));
}

bool ImageToSoundProcessor::loadImageFile (const juce::File& file, juce::String& error)
{
    juce::MemoryBlock data;
    if (!file.existsAsFile() || !file.loadFileAsData (data) || data.isEmpty())
    {
        error = "Cannot read " + file.getFileName();
        return false;
    }
    imageData = std::move (data);
    imageName = file.getFileName();
    startRender();
    return true;
}

void ImageToSoundProcessor::startRender()
{
    const auto params = getRenderParams();
    if (auto problem = params.validate(); problem.isNotEmpty())
    {
        const juce::ScopedLock sl (statusLock);
        statusMessage = "Invalid settings: " + problem;
        statusIsError = true;
        return;
    }
    renderJob.cancel(); // the old render must not report under the new name
    {
        const juce::ScopedLock sl (statusLock);
        statusMessage = "Rendering " + imageName + " ...";
        statusIsError = false;
        renderingImageName = imageName;
        renderStartTime = juce::Time::getMillisecondCounterHiRes();
    }
    renderJob.start (imageData, params);
}

void ImageToSoundProcessor::cancelRender()
{
    renderJob.cancel();
    const juce::ScopedLock sl (statusLock);
    statusMessage = "Render cancelled";
    statusIsError = false;
}

void ImageToSoundProcessor::onRenderFinished (std::unique_ptr<its::SampleBuffer> sample,
                                              const juce::String& error)
{
    // Render thread. It is the only producer for the engine's hand-over queue.
    juce::String name;
    double startTime;
    {
        const juce::ScopedLock sl (statusLock);
        name = renderingImageName;
        startTime = renderStartTime;
    }

    juce::String message;
    bool isError = error.isNotEmpty();
    if (sample != nullptr)
    {
        const double seconds = (double)sample->samples.size() / sample->sampleRate;
        const double took = (juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0;
        message = name + ": " + juce::String (seconds, 2) + " s of audio, rendered in " +
                  juce::String (took, 1) + " s";
        if (!engine.setSample (std::move (sample)))
        {
            message = "Render finished, but the audio thread is not running to pick it up";
            isError = true;
        }
    }
    else
    {
        message = "Render failed: " + error;
    }

    const juce::ScopedLock sl (statusLock);
    statusMessage = message;
    statusIsError = isError;
}

ImageToSoundProcessor::RenderStatus ImageToSoundProcessor::getRenderStatus() const
{
    RenderStatus s;
    s.rendering = renderJob.isRendering();
    s.progress = renderJob.getProgress();
    const juce::ScopedLock sl (statusLock);
    s.message = statusMessage;
    s.isError = statusIsError;
    return s;
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
    return new ImageToSoundEditor (*this);
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
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            ensureRenderSettings(); // projects saved before phase 4 have no RENDER node
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ImageToSoundProcessor();
}

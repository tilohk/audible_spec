#include "PluginProcessor.h"

#include "BinaryData.h"
#include "Parameters.h"
#include "PluginEditor.h"
#include "Presets.h"
#include "audio/AudioCodec.h"
#include "image/ImageDecoder.h"
#include "ui/DisplayData.h"

namespace param = its::param;

ImageToSoundProcessor::ImageToSoundProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", param::createLayout()),
      renderJob ([this] (its::RenderJob::Output output) { onRenderFinished (std::move (output)); })
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
    setImage (
        juce::MemoryBlock (BinaryData::scene_portrait_heic, (size_t)BinaryData::scene_portrait_heicSize),
        "Built-in test image");
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
    ensureRenderSettingsIn (apvts.state);
}

void ImageToSoundProcessor::ensureRenderSettingsIn (juce::ValueTree& state)
{
    // Add any missing render setting (new state, or a project saved by an older version).
    auto tree = state.getOrCreateChildWithName ("RENDER", nullptr);
    const its::RenderParams defaultParams;
    const auto defaults = defaultParams.toJson();
    for (auto& property : defaults.getDynamicObject()->getProperties())
        if (!tree.hasProperty (property.name))
            tree.setProperty (property.name, property.value, nullptr);

    // The UI edits the sound length in seconds; "width" (frames) is derived from it.
    if (!tree.hasProperty ("duration"))
    {
        const auto width = (int)tree.getProperty ("width", defaultParams.width);
        const auto hop = (int)tree.getProperty ("hop_length", defaultParams.hopLength);
        const auto rate = (int)tree.getProperty ("sample_rate", defaultParams.sampleRate);
        tree.setProperty ("duration", (double)width * hop / rate, nullptr);
    }
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
    auto params = its::RenderParams::fromJson (juce::var (object));
    params.width = its::display::widthForDuration ((double)tree.getProperty ("duration"), params);
    return params;
}

void ImageToSoundProcessor::applyPreset (const juce::String& presetText)
{
    const auto parsed = its::presets::parse (presetText);
    auto tree = getRenderSettings();
    its::presets::apply (parsed, tree);
    tree.setProperty ("preset_name", parsed.name, nullptr);

    const juce::ScopedLock sl (statusLock);
    statusMessage = "Preset \"" + parsed.name + "\" loaded. Press Render to hear it.";
    if (parsed.problems.size() > 0)
        statusMessage << " (" << parsed.problems.size() << " unknown line(s) skipped)";
    statusIsError = false;
}

juce::String ImageToSoundProcessor::getPresetName()
{
    return getRenderSettings()["preset_name"].toString();
}

void ImageToSoundProcessor::setImage (juce::MemoryBlock data, const juce::String& name)
{
    juce::String error;
    if (auto decoded = its::decodeImageData (data.getData(), data.getSize(), error))
        previewBase = its::display::previewBase (*decoded, 480, 360);
    else
        previewBase = {};
    const juce::ScopedLock sl (imageLock);
    imageData = std::move (data);
    imageName = name;
    ++imageGeneration;
}

bool ImageToSoundProcessor::loadImageFile (const juce::File& file, juce::String& error)
{
    juce::MemoryBlock data;
    if (!file.existsAsFile() || !file.loadFileAsData (data) || data.isEmpty())
    {
        error = "Couldn't read " + file.getFileName() + ".";
        return false;
    }
    juce::String decodeError;
    if (!its::decodeImageData (data.getData(), data.getSize(), decodeError))
    {
        error = file.getFileName() + " isn't an image this plugin can read.";
        return false;
    }
    setImage (std::move (data), file.getFileName());
    startRender();
    return true;
}

ImageToSoundProcessor::RenderedData ImageToSoundProcessor::getRenderedData() const
{
    const juce::ScopedLock sl (statusLock);
    return rendered;
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

void ImageToSoundProcessor::onRenderFinished (its::RenderJob::Output output)
{
    auto& sample = output.sample;
    const auto& error = output.error;
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
    std::shared_ptr<const its::SampleBuffer> audioCopy;
    juce::MemoryBlock encoded;
    if (sample != nullptr)
    {
        audioCopy = std::make_shared<const its::SampleBuffer> (*sample); // for the UI (drag-out)
        encoded = its::codec::encodeFlac (*sample);                      // for the project state
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
    if (audioCopy != nullptr)
    {
        rendered.audio = std::move (audioCopy);
        rendered.spectrogram = std::move (output.spectrogram);
        rendered.encodedSound = std::move (encoded);
        ++rendered.generation;
    }
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

// ---------------------------------------------------------------- project state
//
// Format (version 1): "ITS1" magic, int32 version, then a binary ValueTree:
//   PARAMETERS (apvts state)
//     RENDER  (render settings)
//     IMAGE   name, data (the image file as loaded)
//     SOUND   sample_rate, flac (24-bit FLAC of the rendered sound)
// Projects from phases 3-5 stored plain XML via copyXmlToBinary; they still load.

namespace
{
constexpr juce::uint32 stateMagic = 0x31535449; // "ITS1" little-endian
constexpr int stateVersion = 1;
} // namespace

void ImageToSoundProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    ensureRenderSettingsIn (state);

    {
        const juce::ScopedLock sl (imageLock);
        juce::ValueTree image ("IMAGE");
        image.setProperty ("name", imageName, nullptr);
        image.setProperty ("data", juce::var (imageData), nullptr);
        state.appendChild (image, nullptr);
    }
    {
        const juce::ScopedLock sl (statusLock);
        if (rendered.audio != nullptr && !rendered.encodedSound.isEmpty())
        {
            juce::ValueTree sound ("SOUND");
            sound.setProperty ("sample_rate", rendered.audio->sampleRate, nullptr);
            sound.setProperty ("flac", juce::var (rendered.encodedSound), nullptr);
            state.appendChild (sound, nullptr);
        }
    }

    juce::MemoryOutputStream out (destData, false);
    out.writeInt ((int)stateMagic);
    out.writeInt (stateVersion);
    state.writeToStream (out);
}

void ImageToSoundProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ValueTree state;
    if (sizeInBytes > 8 && juce::ByteOrder::littleEndianInt (data) == stateMagic)
    {
        const int version = (int)juce::ByteOrder::littleEndianInt (static_cast<const char*> (data) + 4);
        if (version <= stateVersion)
            state =
                juce::ValueTree::readFromData (static_cast<const char*> (data) + 8, (size_t)sizeInBytes - 8);
    }
    else if (auto xml = getXmlFromBinary (data, sizeInBytes)) // phases 3-5
    {
        state = juce::ValueTree::fromXml (*xml);
    }

    if (!state.isValid() || !state.hasType (apvts.state.getType()))
    {
        const juce::ScopedLock sl (statusLock);
        statusMessage = "The saved plugin state couldn't be read, so default settings are used.";
        statusIsError = true;
        return;
    }

    // Image and sound are not part of the parameter state.
    auto image = state.getChildWithName ("IMAGE");
    auto sound = state.getChildWithName ("SOUND");
    state.removeChild (image, nullptr);
    state.removeChild (sound, nullptr);
    apvts.replaceState (state);
    ensureRenderSettings();

    renderJob.cancel(); // e.g. the render of the built-in image started in the constructor

    bool restoredImage = false;
    if (auto* block = image["data"].getBinaryData(); block != nullptr && !block->isEmpty())
    {
        juce::String error;
        if (its::decodeImageData (block->getData(), block->getSize(), error))
        {
            setImage (*block, image["name"].toString());
            restoredImage = true;
        }
    }

    if (auto* flac = sound["flac"].getBinaryData(); flac != nullptr && restoreSound (*flac))
        return; // plays right away, no render

    // No usable sound in the project (older project or damaged data): render instead.
    startRender();
    if (!restoredImage && image.isValid())
    {
        const juce::ScopedLock sl (statusLock);
        statusMessage =
            "The image saved in this project couldn't be read. Rendering the current image instead.";
        statusIsError = true;
    }
}

bool ImageToSoundProcessor::restoreSound (const juce::MemoryBlock& flac)
{
    auto decoded = its::codec::decodeFlac (flac);
    if (decoded == nullptr || decoded->samples.size() < 4)
        return false;

    auto copy = std::make_shared<const its::SampleBuffer> (*decoded);
    auto picture = std::make_shared<const its::FloatMatrix> (
        its::display::analyseSpectrogram (copy->samples, getRenderParams(), its::RenderJob::spectrogramRows,
                                          its::RenderJob::spectrogramMaxCols));

    // The render thread is stopped (see setStateInformation), so this thread is now the only producer.
    if (!engine.setSample (std::move (decoded)))
        return false;

    const juce::ScopedLock sl (statusLock);
    rendered.audio = std::move (copy);
    rendered.spectrogram = std::move (picture);
    rendered.encodedSound = flac;
    ++rendered.generation;
    statusMessage = imageName + ": restored from the project (" +
                    juce::String ((double)rendered.audio->samples.size() / rendered.audio->sampleRate, 2) +
                    " s, no render needed)";
    statusIsError = false;
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ImageToSoundProcessor();
}

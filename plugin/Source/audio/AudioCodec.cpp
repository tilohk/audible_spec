#include "AudioCodec.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace its::codec
{
juce::MemoryBlock encodeFlac (const SampleBuffer& sound)
{
    juce::MemoryBlock block;
    if (sound.samples.empty())
        return block;

    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (block, false);
    juce::FlacAudioFormat flac;
    auto writer = flac.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                    .withSampleRate (sound.sampleRate)
                                                    .withNumChannels (1)
                                                    .withBitsPerSample (24));
    if (writer == nullptr)
        return {};
    const float* channel = sound.samples.data();
    if (!writer->writeFromFloatArrays (&channel, 1, (int)sound.samples.size()))
        return {};
    writer.reset(); // flushes into `block`
    return block;
}

std::unique_ptr<SampleBuffer> decodeFlac (const juce::MemoryBlock& data)
{
    if (data.isEmpty())
        return nullptr;
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> reader (
        flac.createReaderFor (new juce::MemoryInputStream (data, false), true));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > (1 << 28))
        return nullptr;

    juce::AudioBuffer<float> buffer ((int)reader->numChannels, (int)reader->lengthInSamples);
    if (!reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true))
        return nullptr;

    auto sound = std::make_unique<SampleBuffer>();
    sound->sampleRate = reader->sampleRate;
    sound->samples.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples());
    return sound;
}
} // namespace its::codec

#pragma once

#include "SamplerEngine.h"

#include <juce_core/juce_core.h>

namespace its::codec
{
// Rendered sound <-> 24-bit FLAC (lossless at 24 bit, ~half the size of raw PCM).
// Used to store the sound in the DAW project so it plays without re-rendering.
juce::MemoryBlock encodeFlac (const SampleBuffer& sound);

// Returns nullptr if the data is not a readable FLAC stream.
std::unique_ptr<SampleBuffer> decodeFlac (const juce::MemoryBlock& data);
} // namespace its::codec

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace its::param
{
// Real-time playback parameters (automatable in the DAW, no re-render).
// IDs are part of saved projects: never rename one, add a new ID instead.
inline constexpr const char* gain = "gain";
inline constexpr const char* attack = "attack";
inline constexpr const char* decay = "decay";
inline constexpr const char* sustain = "sustain";
inline constexpr const char* release = "release";
inline constexpr const char* rootNote = "rootNote";
inline constexpr const char* keyTrack = "keyTrack";
inline constexpr const char* fineTune = "fineTune";
inline constexpr const char* sampleStart = "sampleStart";
inline constexpr const char* sampleEnd = "sampleEnd";
inline constexpr const char* loopOn = "loopOn";
inline constexpr const char* loopStart = "loopStart";
inline constexpr const char* loopEnd = "loopEnd";
inline constexpr const char* loopCrossfade = "loopCrossfade";
inline constexpr const char* reverse = "reverse";
inline constexpr const char* lowpass = "lowpass";
inline constexpr const char* highpass = "highpass";
inline constexpr const char* velocityAmount = "velocityAmount";
inline constexpr const char* playMode = "playMode";

inline const juce::StringArray allIds{gain,     attack,    decay,          sustain,       release,
                                      rootNote, keyTrack,  fineTune,       sampleStart,   sampleEnd,
                                      loopOn,   loopStart, loopEnd,        loopCrossfade, reverse,
                                      lowpass,  highpass,  velocityAmount, playMode};

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace its::param

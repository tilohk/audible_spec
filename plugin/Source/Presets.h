#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

// Render-setting presets in the same text format as presets/*.txt (Python CONFIG syntax):
//
//   # Dark / Texture        <- first comment line = preset name
//   GAMMA = 0.35
//   INVERT = False
//
// A preset only changes render settings; they apply when Render is pressed.
namespace its::presets
{
struct Preset
{
    juce::String name;
    juce::String text;
    bool builtIn = false;
    juce::File file; // user presets only
};

struct Parsed
{
    juce::String name;
    juce::NamedValueSet values; // lower-case render setting ids -> values
    juce::StringArray problems; // lines that could not be used
};

Parsed parse (const juce::String& text);

// Resets every render setting to its default, then applies the preset's values.
void apply (const Parsed& preset, juce::ValueTree& renderSettings);

juce::String serialize (const juce::ValueTree& renderSettings, const juce::String& name);

std::vector<Preset> builtIns(); // presets/*.txt embedded in the plugin
juce::File userFolder();        // ~/Library/Application Support/Image To Sound/Presets
std::vector<Preset> userPresets();
bool saveUserPreset (const juce::ValueTree& renderSettings, const juce::String& name, juce::String& error);
} // namespace its::presets

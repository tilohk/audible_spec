#include "Presets.h"

#include "BinaryData.h"
#include "dsp/RenderParams.h"

namespace its::presets
{
namespace
{
// Render setting ids in file order, plus the UI's "duration" (seconds).
juce::StringArray knownKeys()
{
    juce::StringArray keys;
    const auto json = RenderParams{}.toJson(); // keep the object alive while iterating
    for (auto& property : json.getDynamicObject()->getProperties())
        keys.add (property.name.toString());
    keys.add ("duration");
    return keys;
}

juce::var parseValue (juce::String text)
{
    text = text.trim();
    if (text == "True" || text == "true")
        return true;
    if (text == "False" || text == "false")
        return false;
    if ((text.startsWithChar ('"') && text.endsWithChar ('"')) ||
        (text.startsWithChar ('\'') && text.endsWithChar ('\'')))
        return text.substring (1, text.length() - 1);
    if (text.containsOnly ("+-0123456789") && text.containsAnyOf ("0123456789"))
        return text.getIntValue();
    if (text.containsOnly ("+-0123456789.eE") && text.containsAnyOf ("0123456789"))
        return text.getDoubleValue();
    return {};
}

juce::String formatValue (const juce::var& v)
{
    if (v.isBool())
        return (bool)v ? "True" : "False";
    if (v.isString())
        return "\"" + v.toString() + "\"";
    if (v.isInt() || v.isInt64())
        return juce::String ((juce::int64)v);
    return juce::String ((double)v, 6).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
}
} // namespace

Parsed parse (const juce::String& text)
{
    Parsed result;
    const auto keys = knownKeys();
    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.trim();
        if (line.isEmpty())
            continue;
        if (line.startsWithChar ('#'))
        {
            if (result.name.isEmpty())
                result.name = line.substring (1).trim();
            continue;
        }
        const auto key = line.upToFirstOccurrenceOf ("=", false, false).trim().toLowerCase();
        const auto value = parseValue (
            line.fromFirstOccurrenceOf ("=", false, false).upToFirstOccurrenceOf ("#", false, false));
        if (!line.contains ("=") || !keys.contains (key) || value.isVoid())
        {
            result.problems.add (line);
            continue;
        }
        result.values.set (juce::Identifier (key), value);
    }
    return result;
}

void apply (const Parsed& preset, juce::ValueTree& renderSettings)
{
    const RenderParams defaults;
    const auto json = defaults.toJson(); // keep the object alive while iterating
    for (auto& property : json.getDynamicObject()->getProperties())
        renderSettings.setProperty (property.name, property.value, nullptr);
    renderSettings.setProperty ("duration", defaults.durationSeconds(), nullptr);

    for (auto& property : preset.values)
        renderSettings.setProperty (property.name, property.value, nullptr);

    // A preset written by main.py may give WIDTH (frames) instead of the duration in seconds.
    if (preset.values.contains ("width") && !preset.values.contains ("duration"))
        renderSettings.setProperty ("duration",
                                    (double)renderSettings["width"] * (double)renderSettings["hop_length"] /
                                        (double)renderSettings["sample_rate"],
                                    nullptr);
}

juce::String serialize (const juce::ValueTree& renderSettings, const juce::String& name)
{
    juce::String text = "# " + name + "\n";
    for (auto& key : knownKeys())
        if (renderSettings.hasProperty (key))
            text << key.toUpperCase() << " = " << formatValue (renderSettings[juce::Identifier (key)])
                 << "\n";
    return text;
}

std::vector<Preset> builtIns()
{
    std::vector<Preset> list;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const juce::String file = BinaryData::originalFilenames[i];
        if (!file.endsWithIgnoreCase (".txt"))
            continue;
        int size = 0;
        const auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
        Preset p;
        p.text = juce::String::fromUTF8 (data, size);
        p.name = parse (p.text).name;
        if (p.name.isEmpty())
            p.name = file.upToLastOccurrenceOf (".", false, false);
        p.builtIn = true;
        list.push_back (p);
    }
    std::sort (list.begin(), list.end(), [] (auto& a, auto& b) { return a.name < b.name; });
    return list;
}

juce::File userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support/audible_spec/Presets");
}

std::vector<Preset> userPresets()
{
    std::vector<Preset> list;
    for (auto& file : userFolder().findChildFiles (juce::File::findFiles, false, "*.txt"))
    {
        Preset p;
        p.text = file.loadFileAsString();
        p.name = parse (p.text).name;
        if (p.name.isEmpty())
            p.name = file.getFileNameWithoutExtension();
        p.file = file;
        list.push_back (p);
    }
    std::sort (list.begin(), list.end(), [] (auto& a, auto& b) { return a.name < b.name; });
    return list;
}

bool saveUserPreset (const juce::ValueTree& renderSettings, const juce::String& name, juce::String& error)
{
    const auto cleanName = name.trim();
    if (cleanName.isEmpty())
    {
        error = "Enter a name for the preset.";
        return false;
    }
    auto folder = userFolder();
    if (!folder.createDirectory())
    {
        error = "Couldn't create " + folder.getFullPathName() + ".";
        return false;
    }
    auto file = folder.getChildFile (juce::File::createLegalFileName (cleanName) + ".txt");
    if (!file.replaceWithText (serialize (renderSettings, cleanName)))
    {
        error = "Couldn't write " + file.getFullPathName() + ".";
        return false;
    }
    return true;
}
} // namespace its::presets

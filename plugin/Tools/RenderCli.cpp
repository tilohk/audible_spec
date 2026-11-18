// render_cli: renders an image to a WAV with the C++ DSP core (same pipeline as main.py).
//
//   render_cli <image> <out.wav> [--params params.json] [--set key=value ...]
//
// Keys are main.py's lower-case CONFIG names, e.g. --set n_fft=2048 --set gamma=0.5

#include "dsp/RenderParams.h"
#include "dsp/Renderer.h"
#include "image/ImageDecoder.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
int fail (const juce::String& message)
{
    std::fprintf (stderr, "render_cli: %s\n", message.toRawUTF8());
    return 1;
}

juce::var parseValue (const juce::String& text)
{
    if (text == "true" || text == "True")
        return true;
    if (text == "false" || text == "False")
        return false;
    if (text.containsOnly ("0123456789-"))
        return text.getIntValue();
    if (text.containsOnly ("0123456789.-eE"))
        return text.getDoubleValue();
    return text;
}

double secondsSince (juce::int64 startTicks)
{
    return juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
}
} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    if (args.size() < 2)
        return fail ("usage: render_cli <image> <out.wav> [--params params.json] [--set key=value ...]");

    const juce::File imageFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[0]);
    const juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[1]);

    auto* settings = new juce::DynamicObject();
    juce::var json (settings);
    for (int i = 2; i < args.size(); ++i)
    {
        if (args[i] == "--params" && i + 1 < args.size())
        {
            auto loaded =
                juce::JSON::parse (juce::File::getCurrentWorkingDirectory().getChildFile (args[++i]));
            if (auto* obj = loaded.getDynamicObject())
                for (auto& prop : obj->getProperties())
                    settings->setProperty (prop.name, prop.value);
            else
                return fail ("cannot parse " + args[i]);
        }
        else if (args[i] == "--set" && i + 1 < args.size())
        {
            const auto kv = args[++i];
            settings->setProperty (kv.upToFirstOccurrenceOf ("=", false, false).toLowerCase(),
                                   parseValue (kv.fromFirstOccurrenceOf ("=", false, false)));
        }
        else
        {
            return fail ("unknown argument " + args[i]);
        }
    }

    const auto params = its::RenderParams::fromJson (json);
    if (auto error = params.validate(); error.isNotEmpty())
        return fail (error);

    std::printf ("image      %s\n", imageFile.getFullPathName().toRawUTF8());
    std::printf ("spectrum   %d bins x %d frames, n_fft %d, hop %d, %d Griffin-Lim iterations\n",
                 params.specHeight(), params.width, params.nFft, params.hopLength,
                 params.griffinLimIterations);

    const auto t0 = juce::Time::getHighResolutionTicks();
    juce::String error;
    auto decoded = its::decodeImageFile (imageFile, error);
    if (!decoded)
        return fail (error);
    const double decodeSeconds = secondsSince (t0);

    const auto t1 = juce::Time::getHighResolutionTicks();
    int lastPercent = -1;
    auto result = its::render (*decoded, params,
                               [&lastPercent] (int done, int total)
                               {
                                   const int percent = 100 * done / total;
                                   if (percent / 10 != lastPercent / 10)
                                       std::printf ("  griffin-lim %3d%%\n", percent);
                                   lastPercent = percent;
                                   return true;
                               });
    const double renderSeconds = secondsSince (t1);
    if (!result)
        return fail ("render cancelled");

    const double sc = its::spectralConvergence (result->rawAudio, result->magnitude, params);

    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (outFile);
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate ((double)params.sampleRate)
                                                   .withNumChannels (1)
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
        return fail ("cannot write " + outFile.getFullPathName());
    const float* channel = result->audio.data();
    writer->writeFromFloatArrays (&channel, 1, (int)result->audio.size());
    writer.reset();

    std::printf ("decode     %.3f s\n", decodeSeconds);
    std::printf ("render     %.3f s\n", renderSeconds);
    std::printf ("duration   %.2f s audio, spectral convergence %.4f\n",
                 (double)result->audio.size() / params.sampleRate, sc);
    std::printf ("wrote      %s\n", outFile.getFullPathName().toRawUTF8());
    return 0;
}

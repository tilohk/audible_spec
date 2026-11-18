// UiSnapshot: builds the real processor + editor off-screen, waits for the first render,
// plays a note, and writes a PNG of the editor. Run by ctest as a UI smoke test; the
// PNG is for reviewing the layout without opening a DAW.
//
//   UiSnapshot <out.png>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: UiSnapshot <out.png>\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    auto* messageManager = juce::MessageManager::getInstance();

    ImageToSoundProcessor processor;
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    auto set = [&processor] (const char* id, float value)
    {
        auto* p = processor.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    set (its::param::loopOn, 1.0f);
    set (its::param::loopStart, 0.35f);
    set (its::param::loopEnd, 0.8f);
    set (its::param::sampleStart, 0.05f);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi, none;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8)100), 0);

    // Wait for the background render, feeding audio blocks so the engine adopts the sound.
    const auto start = juce::Time::getMillisecondCounter();
    while (processor.getRenderedData().generation == 0)
    {
        processor.processBlock (buffer, none);
        messageManager->runDispatchLoopUntil (20);
        if (juce::Time::getMillisecondCounter() - start > 60000)
        {
            std::fprintf (stderr, "UiSnapshot: render did not finish: %s\n",
                          processor.getRenderStatus().message.toRawUTF8());
            return 1;
        }
    }
    processor.processBlock (buffer, none); // adopt the new sound
    processor.processBlock (buffer, midi);
    for (int i = 0; i < 25; ++i)
        processor.processBlock (buffer, none); // playhead moves a bit

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* itsEditor = dynamic_cast<ImageToSoundEditor*> (editor.get());
    if (itsEditor == nullptr)
        return 1;
    for (int i = 0; i < 5; ++i)
        itsEditor->refreshNow(); // spectrogram, status, debounced preview

    const auto snapshot = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    if (!stream.openedOk() || !png.writeImageToStream (snapshot, stream))
        return 1;
    std::printf ("UiSnapshot: %dx%d editor, status \"%s\" -> %s\n", editor->getWidth(), editor->getHeight(),
                 processor.getRenderStatus().message.toRawUTF8(), out.getFullPathName().toRawUTF8());
    return 0;
}

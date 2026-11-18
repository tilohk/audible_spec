#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace its::ui
{
// Render settings (bound to the "RENDER" state node), Render/Cancel, progress and status.
class RenderPanel final : public juce::Component
{
public:
    RenderPanel();
    ~RenderPanel() override;

    void bind (juce::ValueTree renderSettings); // can be called again after a state reload
    void setStatus (bool rendering, float progress, const juce::String& message, bool isError,
                    const juce::String& estimate);

    std::function<void()> onRender, onCancel;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row; // a labelled compact slider, toggle row or choice box
    struct Group
    {
        juce::String title;
        std::vector<Row*> rows;
    };
    Row& addSlider (Group&, const char* id, const juce::String& name, double min, double max, double step,
                    double skewCentre = 0.0, const juce::String& suffix = {});
    Row& addChoice (Group&, const char* id, const juce::String& name, juce::StringArray labels,
                    juce::Array<juce::var> values);
    Row& addToggles (Group&, std::vector<std::pair<const char*, juce::String>> toggles);

    std::vector<std::unique_ptr<Row>> rows;
    std::vector<Group> groups;
    juce::ValueTree tree;

    juce::Label heading{{}, "Render"}, hint{{}, "applies when you press Render"};
    double progressValue = 0.0;
    juce::ProgressBar progress{progressValue};
    juce::Label estimateLabel, statusLabel;
    juce::TextButton renderButton{"Render"}, cancelButton{"Cancel"};
};

// Real-time playback parameters: knobs, toggles, play mode and the drag-out source.
class PlaybackPanel final : public juce::Component
{
public:
    explicit PlaybackPanel (juce::AudioProcessorValueTreeState& state);
    ~PlaybackPanel() override;

    // Returns the file to drag into the DAW (written on demand), or an invalid File.
    std::function<juce::File()> onDragOut;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob;
    class DragOutButton;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> toggleAttachments;
    juce::ComboBox playMode;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> playModeAttachment;
    std::unique_ptr<DragOutButton> dragOut;
    juce::Label heading{{}, "Playback"};
};
} // namespace its::ui

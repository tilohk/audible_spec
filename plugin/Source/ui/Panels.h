#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace its::ui
{
// Render section: an always-visible bar (fold toggle, render preset, Render/Cancel, progress,
// status) and a foldable body with the render settings (bound to the "RENDER" state node).
class RenderPanel final : public juce::Component
{
public:
    RenderPanel();
    ~RenderPanel() override;

    void bind (juce::ValueTree renderSettings); // can be called again after a state reload
    void setStatus (bool rendering, float progress, const juce::String& message, bool isError,
                    const juce::String& estimate);

    void setExpanded (bool shouldBeExpanded); // does not call onExpandedChanged
    bool isExpanded() const { return expanded; }
    static constexpr int barHeight = 66, bodyHeight = 226; // body: hint + group titles + 7 rows
    int getPreferredHeight() const { return barHeight + (expanded ? bodyHeight : 0); }

    juce::ComboBox& getPresetBox() { return presetBox; } // filled by the editor

    std::function<void()> onRender, onCancel;
    std::function<void (bool expanded)> onExpandedChanged; // user clicked the fold toggle

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

    bool expanded = true;
    juce::TextButton foldButton;
    juce::Label presetLabel{{}, "Render preset"}, hint{{}, "Settings apply when you press Render"};
    juce::ComboBox presetBox;
    double progressValue = 0.0;
    juce::ProgressBar progress{progressValue};
    juce::Label estimateLabel, errorLabel;
    juce::TextButton renderButton{"Render"}, cancelButton{"Cancel"};
};

// Real-time playback parameters in groups (Envelope, Pitch, Sample, Filter) plus an output
// column (gain, play mode, drag-out source).
class PlaybackPanel final : public juce::Component
{
public:
    explicit PlaybackPanel (juce::AudioProcessorValueTreeState& apvts);
    ~PlaybackPanel() override;

    // Returns the file to drag into the DAW (written on demand), or an invalid File.
    std::function<juce::File()> onDragOut;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob;
    class DragOutButton;
    class IconToggle;
    struct Group
    {
        juce::String title;
        std::vector<juce::Component*> knobs;  // one slot each
        std::vector<juce::Component*> extras; // stacked in one extra slot (toggles, choice)
        juce::Rectangle<int> bounds;          // set in resized(), painted as a card
    };

    Knob& addKnob (const char* id, const juce::String& name);
    IconToggle& addToggle (const char* id, int icon, const juce::String& tooltip);

    juce::AudioProcessorValueTreeState& state;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<IconToggle>> toggles;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> toggleAttachments;
    class ChoiceButtons;
    juce::ComboBox playMode;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> playModeAttachment;
    std::unique_ptr<ChoiceButtons> filterSlope; // declared after `toggles`: its buttons live there
    std::unique_ptr<DragOutButton> dragOut;
    Knob* gainKnob = nullptr;
    std::vector<Group> groups; // row 1: envelope, pitch; row 2: sample, filter
    juce::Rectangle<int> outputBounds;
};
} // namespace its::ui

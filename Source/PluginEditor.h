#pragma once
#include "PluginProcessor.h"

class TopBrassLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    TopBrassLookAndFeel();
    juce::Image roster;
    float animationTime = 0;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override;
};
class TopBrassEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer,
                             private juce::MidiKeyboardState::Listener {
public:
    explicit TopBrassEditor(TopBrassProcessor&);
    ~TopBrassEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    struct Strip {
        juce::ToggleButton enabled, doubleTongue;
        juce::Slider level, pan;
        juce::ComboBox octave, channel, tenorSource, tubaVoice;
        juce::Label levelLabel, panLabel, octaveLabel, channelLabel;
        std::unique_ptr<ButtonAttachment> enabledAttachment, doubleTongueAttachment;
        std::unique_ptr<SliderAttachment> levelAttachment, panAttachment;
        std::unique_ptr<ComboAttachment> octaveAttachment, channelAttachment, tenorSourceAttachment, tubaVoiceAttachment;
    };
    TopBrassProcessor& instrument;
    TopBrassLookAndFeel look;
    juce::Image stage, logo;
    std::array<Strip, 6> strips;
    juce::ComboBox articulation, factoryPreset, saxVoice, playingMode, lowestKey;
    juce::Slider output, release, tightness, tone, keyNoise, airflow, hornFeel, tempo, tenorDetailLevel, humanize;
    juce::Label articulationLabel, outputLabel, releaseLabel, tightnessLabel, toneLabel, keyNoiseLabel, airflowLabel, playingModeLabel, lowestKeyLabel, feelLabel, tempoLabel, tenorDetailLabel, humanizeLabel;
    juce::ToggleButton tenorMidi;
    std::unique_ptr<ButtonAttachment> tenorMidiAttachment;
    juce::TextButton tenorBreath { "Tenor breath" }, tenorKeys { "Tenor keys" };
    juce::TextButton panicButton { "All notes off" };
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    std::unique_ptr<ComboAttachment> articulationAttachment, saxVoiceAttachment, playingModeAttachment, lowestKeyAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment, releaseAttachment, tightnessAttachment, toneAttachment, keyNoiseAttachment, airflowAttachment, feelAttachment, tempoAttachment, tenorDetailAttachment, humanizeAttachment;
    juce::TextButton detailsButton { "Show configuration" };
    bool showDetails = false;
    juce::String status;
    int keyboardBottom = -1;
    bool sectionKeyboard = false;
    int rosterHeight() const noexcept;
    void updatePlayingKeys();
    void timerCallback() override;
    void handleNoteOn(juce::MidiKeyboardState*, int, int, float) override;
    void handleNoteOff(juce::MidiKeyboardState*, int, int, float) override;
};

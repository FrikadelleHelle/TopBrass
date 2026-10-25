#pragma once
#include "Engine.h"
#include "SectionPlayer.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include <optional>

class TopBrassProcessor final : public juce::AudioProcessor {
public:
    TopBrassProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "TopBrass"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Pop section"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorValueTreeState parameters;
    void enqueueTenorDetail(topbrass::TenorDetail) noexcept;
    void enqueueNote(int channel, int note, float velocity) noexcept;
    std::atomic<float> peak { 0 };
    std::atomic<int> voiceCount { 0 };
    std::atomic<float> performanceBpm { 120 };
    std::atomic<bool> usingHostTempo { false };
    const std::string& bankError() const { return loadError; }
    std::size_t bankBytes() const { return bank->bytes(); }
    static juce::StringArray factoryPresetNames();
    static juce::StringArray saxVoiceNames();
    static juce::StringArray tenorSourceNames();
    static juce::StringArray tubaVoiceNames();
    bool warTubaAvailable() const { return bank->isMapped(topbrass::warTubaInstrument,0) && bank->isMapped(topbrass::warTubaInstrument,1) && bank->isMapped(topbrass::warTubaInstrument,2); }
    void loadFactoryPreset(int index);
    void panic() { panicRequested.store(true); }
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
private:
    struct PartParameters {
        std::atomic<float> *enabled, *level, *pan, *octave, *channel, *doubleTongue, *tenorSource;
    };
    std::string loadError;
    std::shared_ptr<const topbrass::Bank> bank;
    topbrass::Engine engine;
    std::array<PartParameters, topbrass::partCount> partParameters;
    std::atomic<float> *articulation, *master, *release, *tightness, *tone, *saxVoice, *keyNoise, *airflow, *playingMode, *lowestKey, *hornFeel, *freeTempo, *tenorDetailDb, *tenorDetailMidi, *tubaVoice, *humanize;
    std::array<std::array<bool,2>,16> detailControllerDown {};
    bool detailMidiEnabled = false;
    std::atomic<bool> panicRequested { false };
    struct UiNote { int channel = 1, note = 60; float velocity = 0; std::optional<topbrass::TenorDetail> detail; };
    juce::AbstractFifo noteFifo { 128 };
    std::array<UiNote, 128> uiNotes {};
    topbrass::SectionPlayer section;
    topbrass::SectionConfig performanceConfig;
    topbrass::Settings blockSettings;
    void renderSegment(float* left, float* right, int frames) noexcept;
    int targetForPart(int part, int note) const noexcept;
    topbrass::Settings currentSettings() const noexcept;
    void dispatch(const juce::MidiMessage&) noexcept;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TopBrassProcessor)
};

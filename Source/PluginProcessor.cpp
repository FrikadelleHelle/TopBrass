#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <mutex>

namespace {
std::shared_ptr<const topbrass::Bank> acquireBank(std::string& error) {
    static std::mutex mutex;
    static std::weak_ptr<const topbrass::Bank> shared;
    const std::lock_guard<std::mutex> lock(mutex);
    if (auto loaded = shared.lock()) return loaded;
    auto loaded = std::make_shared<topbrass::Bank>();
    const auto resource = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory().getSiblingFile("Resources").getChildFile("TopBrass.bank");
    if (loaded->load(resource.getFullPathName().toStdString(), error)
        && loaded->loadKeyNoises(resource.getSiblingFile("TopBrass.keys").getFullPathName().toStdString(), error)
        && loaded->loadAirflow(resource.getSiblingFile("TopBrass.air").getFullPathName().toStdString(), error)
        && loaded->loadTenorDetails(resource.getSiblingFile("TopBrass.tenor").getFullPathName().toStdString(),error)) shared = loaded;
    return loaded;
}
constexpr std::array<const char*, topbrass::partCount> names { "Saxophone", "Trumpet", "Trombone", "Tuba", "Saxophone II", "Trumpet II" };
constexpr std::array<float, topbrass::partCount> levels { -5, -7, -8, -10, -8, -10 };
constexpr std::array<float, topbrass::partCount> pans { -0.3f, 0.3f, -0.1f, 0.1f, -.55f, .55f };
constexpr std::array<int, topbrass::partCount> octaves { 0, 0, -1, -2, 0, 0 };
}
juce::AudioProcessorValueTreeState::ParameterLayout TopBrassProcessor::makeParameters() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto addPart = [&layout](int i) {
        const auto index = static_cast<std::size_t>(i);
        const juce::String prefix = "part" + juce::String(i) + ".";
        const juce::String label = names[index];
        const int version = i < 4 ? 1 : 4;
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID(prefix + "enabled", version), label + " on", i < 4));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(prefix + "level", version), label + " level", juce::NormalisableRange<float>(-48, 6, 0.1f), levels[index]));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(prefix + "pan", version), label + " pan", juce::NormalisableRange<float>(-1, 1, 0.01f), pans[index]));
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID(prefix + "octave", version), label + " octave", -2, 2, octaves[index]));
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID(prefix + "channel", version), label + " MIDI channel, 0 is all", 0, 16, 0));
    };
    for (int i = 0; i < 4; ++i) addPart(i);
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("articulation", 1), "Articulation", juce::StringArray { "Short / punch", "Sustain", "Fall" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("master", 1), "Output", juce::NormalisableRange<float>(-48, 0, 0.1f), -3));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("release", 1), "Release ms", juce::NormalisableRange<float>(5, 500, 1, 0.5f), 65));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("tone", 1), "Tone", juce::NormalisableRange<float>(-1, 1, 0.01f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("tightness", 1), "Attack accent", juce::NormalisableRange<float>(0, 1, 0.01f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("saxVoice", 1), "Sax voice", saxVoiceNames(), 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("baritoneKeys", 1), "Baritone keys", juce::NormalisableRange<float>(0, 1, 0.01f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("baritoneAir", 1), "Baritone airflow", juce::NormalisableRange<float>(0, 1, 0.01f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("playingMode",1),"Playing mode",juce::StringArray{"Layered","Section relay","Section groove"},0));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID("lowestKey",1),"Lowest playing key",36,96,60));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("hornFeel",1),"Section accents",juce::NormalisableRange<float>(0,1,.01f),.4f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("freeTempo",1),"Standalone tempo",juce::NormalisableRange<float>(40,240,1),120));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("part1.doubleTongue",2),"Trumpet double tongue",false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("part2.doubleTongue",2),"Trombone double tongue",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("tenorDetailDb",3),"Tenor detail level",juce::NormalisableRange<float>(-48,36,.1f),0));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("tenorDetailMidi",3),"Tenor detail MIDI",false));
    for (int i = 4; i < topbrass::partCount; ++i) addPart(i);
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("part5.doubleTongue", 4), "Trumpet II double tongue", false));
    for (int part : {0, 4})
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("part"+juce::String(part)+".tenorSource",5),
            juce::String(names[static_cast<std::size_t>(part)])+" tenor source",tenorSourceNames(),0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("part3.tubaVoice",6),"Tuba voice",tubaVoiceNames(),0));
    const auto humanizeText=juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float value,int){return juce::String(value*100,0)+"%";})
        .withValueFromStringFunction([](const juce::String& value){return value.getFloatValue()/100;});
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("humanize",7),"Humanize",
        juce::NormalisableRange<float>(0,1,.01f),.35f,humanizeText));
    return layout;
}
TopBrassProcessor::TopBrassProcessor()
    : AudioProcessor(BusesProperties().withOutput("Stereo", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "TopBrassState", makeParameters()), bank(acquireBank(loadError)), engine(*bank) {
    for (int i = 0; i < topbrass::partCount; ++i) {
        const auto prefix = "part" + juce::String(i) + ".";
        partParameters[static_cast<std::size_t>(i)] = { parameters.getRawParameterValue(prefix + "enabled"), parameters.getRawParameterValue(prefix + "level"),
            parameters.getRawParameterValue(prefix + "pan"), parameters.getRawParameterValue(prefix + "octave"), parameters.getRawParameterValue(prefix + "channel"), (i == 1 || i == 2 || i == 5) ? parameters.getRawParameterValue(prefix + "doubleTongue") : nullptr,
            topbrass::isSaxPart(i) ? parameters.getRawParameterValue(prefix+"tenorSource") : nullptr };
    }
    articulation = parameters.getRawParameterValue("articulation");
    master = parameters.getRawParameterValue("master");
    release = parameters.getRawParameterValue("release");
    tightness = parameters.getRawParameterValue("tightness");
    tone = parameters.getRawParameterValue("tone");
    saxVoice = parameters.getRawParameterValue("saxVoice");
    keyNoise = parameters.getRawParameterValue("baritoneKeys");
    airflow = parameters.getRawParameterValue("baritoneAir");
    tenorDetailDb = parameters.getRawParameterValue("tenorDetailDb");
    tenorDetailMidi = parameters.getRawParameterValue("tenorDetailMidi");
    playingMode=parameters.getRawParameterValue("playingMode");lowestKey=parameters.getRawParameterValue("lowestKey");
    hornFeel=parameters.getRawParameterValue("hornFeel");freeTempo=parameters.getRawParameterValue("freeTempo");
    tubaVoice=parameters.getRawParameterValue("part3.tubaVoice");
    humanize=parameters.getRawParameterValue("humanize");
}
juce::StringArray TopBrassProcessor::factoryPresetNames() {
    return { "Punchy funk", "Warm soul", "Mellow section", "Tender brass", "Baritone funk", "Smoky baritone", "Baritone growl",
             "Uncolored section", "Uncolored tenor", "Uncolored baritone", "Uncolored trumpet", "Uncolored trombone", "Uncolored tuba", "Baritone with keys", "Baritone close detail", "Section pocket", "Double-tongued trumpet", "Double-tongued trombone", "Alternate tenor", "War Tuba staccato", "War Tuba staccatissimo" };
}
juce::StringArray TopBrassProcessor::saxVoiceNames() {
    return { "Tenor", "Baritone", "Baritone marcato", "Baritone subtone", "Baritone growl" };
}
juce::StringArray TopBrassProcessor::tenorSourceNames() {
    return {"Original tenor","Alternate tenor"};
}
juce::StringArray TopBrassProcessor::tubaVoiceNames() {
    return {"Original tuba","War staccato","War staccatissimo"};
}
void TopBrassProcessor::loadFactoryPreset(int index) {
    struct Preset { int art; float color, accent, releaseMs; std::array<float, 4> db; unsigned mask; int sax = 0, saxOctave = 0; float keys = 0, air = 0; int mode = 0; unsigned tongueMask = 0, alternateSaxes = 0; int tuba = 0; };
    static constexpr std::array<Preset, 21> presets {{
        {0, 0.45f, 1.0f, 45, {-5, -4, -8, -14}, 15},
        {1, -0.35f, 0.25f, 130, {-3, -9, -8, -16}, 15},
        {1, -0.75f, 0.0f, 200, {-5, -13, -7, -13}, 15},
        {1, -0.5f, 0.05f, 180, {-8, -5, -9, -18}, 6},
        {0, 0.35f, 0.8f, 65, {-3, -7, -10, -16}, 15, 2, -1},
        {1, -0.25f, 0.1f, 160, {-3, -8, -8, -15}, 1, 3, -1},
        {1, 0.2f, 0.4f, 90, {-3, -7, -8, -15}, 1, 4, -1},
        {1, 0, 0, 100, {-5, -7, -8, -10}, 15},
        {1, 0, 0, 100, {-5, -7, -8, -10}, 1},
        {1, 0, 0, 100, {-5, -7, -8, -10}, 1, 1, -1},
        {1, 0, 0, 100, {-5, -7, -8, -10}, 2},
        {1, 0, 0, 100, {-5, -7, -8, -10}, 4},
        {1, 0, 0, 100, {-5, -7, -8, -10}, 8},
        {1, 0, 0, 100, {-5, -7, -8, -10}, 1, 1, -1, .5f},
        {1, 0, 0, 100, {-5, -7, -8, -10}, 1, 1, -1, .5f, .5f},
        {0, 0, 0, 45, {-5, -7, -8, -13}, 15, 0, 0, 0, 0, 2},
        {0, 0, 0, 65, {-5, -7, -8, -13}, 2, 0, 0, 0, 0, 0, 2},
        {0, 0, 0, 65, {-5, -7, -8, -13}, 4, 0, 0, 0, 0, 0, 4},
        {1, 0, 0, 100, {-5, -7, -8, -10}, 1, 0, 0, 0, 0, 0, 0, 1},
        {0, 0, 0, 65, {-5, -7, -8, -18}, 8, 0, 0, 0, 0, 0, 0, 0, 1},
        {0, 0, 0, 65, {-5, -7, -8, -18}, 8, 0, 0, 0, 0, 0, 0, 0, 2}
    }};
    if (index < 0 || index >= static_cast<int>(presets.size())) return;
    const auto& preset = presets[static_cast<std::size_t>(index)];
    const auto set = [this](const juce::String& id, float value) {
        auto* parameter = parameters.getParameter(id);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        parameter->endChangeGesture();
    };
    set("part3.tubaVoice",static_cast<float>(preset.tuba));
    set("playingMode",static_cast<float>(preset.mode));
    set("humanize",(index<=6 || index==15) ? .35f : 0.0f);
    set("part1.doubleTongue",(preset.tongueMask&2)?1:0);set("part2.doubleTongue",(preset.tongueMask&4)?1:0);
    set("part5.doubleTongue", 0);
    for (int part : {0, 4}) set("part"+juce::String(part)+".tenorSource",(preset.alternateSaxes&(1u<<part))?1:0);
    if(preset.mode!=0){set("lowestKey",60);set("hornFeel",.4f);set("freeTempo",120);}
    set("saxVoice", static_cast<float>(preset.sax));
    set("baritoneKeys", preset.keys); set("baritoneAir", preset.air);
    set("articulation", static_cast<float>(preset.art)); set("tone", preset.color);
    set("tightness", preset.accent); set("release", preset.releaseMs); set("master", -3);
    for (std::size_t i = 0; i < topbrass::partCount; ++i) {
        const auto prefix = "part" + juce::String(static_cast<int>(i)) + ".";
        set(prefix + "enabled", (preset.mask & (1u << i)) != 0 ? 1.0f : 0.0f);
        set(prefix + "level", i < preset.db.size() ? preset.db[i] : levels[i]); set(prefix + "pan", pans[i]);
        set(prefix + "octave", static_cast<float>(i == 0 ? preset.saxOctave : octaves[i])); set(prefix + "channel", 0);
    }
    panic();
}
void TopBrassProcessor::prepareToPlay(double sampleRate, int) {
    engine.setSettings(currentSettings());
    engine.prepare(sampleRate);
    section.prepare(sampleRate);
    detailControllerDown={};detailMidiEnabled=false;
}
void TopBrassProcessor::releaseResources() { engine.allNotesOff(0, true); section.reset(); }
bool TopBrassProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    return layout.getMainInputChannelSet().isDisabled() && layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
void TopBrassProcessor::dispatch(const juce::MidiMessage& msg) noexcept {
    if (msg.isController()) {
        const int controller=msg.getControllerNumber();
        auto& down=detailControllerDown[static_cast<std::size_t>(msg.getChannel()-1)];
        if (controller==120 || controller==121 || controller==123) down={};
        if (detailMidiEnabled && (controller==20 || controller==21)) {
            const auto index=static_cast<std::size_t>(controller-20);
            const bool pressed=msg.getControllerValue()>0;
            const bool routed=performanceConfig.mode!=topbrass::PlayingMode::layered || blockSettings.parts[0].channel==0 || blockSettings.parts[0].channel==msg.getChannel();
            if (pressed&&!down[index]&&routed) engine.triggerTenorDetail(index==0?topbrass::TenorDetail::breath:topbrass::TenorDetail::keys,msg.getChannel());
            down[index]=pressed;
            return;
        }
    }
    if (performanceConfig.mode != topbrass::PlayingMode::layered) {
        if (msg.isNoteOn()) { section.noteOn(msg.getChannel(),msg.getNoteNumber(),msg.getFloatVelocity()); return; }
        if (msg.isNoteOff()) { section.noteOff(msg.getChannel(),msg.getNoteNumber()); return; }
        if (msg.isController()) {
            section.controller(msg.getChannel(),msg.getControllerNumber(),msg.getControllerValue());
            if (msg.getControllerNumber()==64) return;
        }
    }
    if (msg.isNoteOn()) engine.noteOn(msg.getChannel(),msg.getNoteNumber(),msg.getFloatVelocity());
    else if (msg.isNoteOff()) engine.noteOff(msg.getChannel(),msg.getNoteNumber());
    else if (msg.isController()) engine.controller(msg.getChannel(),msg.getControllerNumber(),msg.getControllerValue());
    else if (msg.isPitchWheel()) engine.pitchWheel(msg.getChannel(),msg.getPitchWheelValue());
}
int TopBrassProcessor::targetForPart(int part, int note) const noexcept {
    const auto range=engine.partKeyRange(part);
    int target=note+12*blockSettings.parts[static_cast<std::size_t>(part)].octave;
    while(target<range.low)target+=12;
    while(target>range.high)target-=12;
    return target>=range.low&&target<=range.high?target:-1;
}
void TopBrassProcessor::renderSegment(float* left,float* right,int frames) noexcept {
    if(performanceConfig.mode==topbrass::PlayingMode::layered){engine.render(left,right,frames);return;}
    struct Context { TopBrassProcessor* processor;float* left;float* right;int cursor; } context{this,left,right,0};
    section.advance(frames,&context,[](void* pointer,const topbrass::SectionEvent& event){
        auto& c=*static_cast<Context*>(pointer);auto& p=*c.processor;
        p.engine.render(c.left+c.cursor,c.right+c.cursor,event.offset-c.cursor);c.cursor=event.offset;
        if(event.on)p.engine.noteOnPart(event.part,event.channel,event.note,p.targetForPart(event.part,event.note),event.velocity,
            p.performanceConfig.mode==topbrass::PlayingMode::groove?topbrass::NoteDynamics::strikeAndPressure:topbrass::NoteDynamics::pressure);
        else p.engine.noteOffPart(event.part,event.channel,event.note,p.performanceConfig.mode==topbrass::PlayingMode::groove);
    });
    engine.render(left+context.cursor,right+context.cursor,frames-context.cursor);
}
topbrass::Settings TopBrassProcessor::currentSettings() const noexcept {
    topbrass::Settings next;
    for (std::size_t i = 0; i < partParameters.size(); ++i) {
        const auto& p = partParameters[i];
        next.parts[i] = { p.enabled->load() > 0.5f, juce::Decibels::decibelsToGain(p.level->load()), p.pan->load(),
            static_cast<int>(p.octave->load()), static_cast<int>(p.channel->load()) };
        next.parts[i].doubleTongue = p.doubleTongue != nullptr && p.doubleTongue->load() > .5f;
        if (p.tenorSource) next.parts[i].tenorSource = static_cast<topbrass::TenorSource>(static_cast<int>(p.tenorSource->load()));
    }
    next.tubaVoice=static_cast<topbrass::TubaVoice>(static_cast<int>(tubaVoice->load()));
    next.humanize = humanize->load();
    next.keyNoise = keyNoise->load();
    next.airflowAmount = airflow->load();
    next.tenorDetailGain = juce::Decibels::decibelsToGain(tenorDetailDb->load());
    next.saxVoice = static_cast<topbrass::SaxVoice>(static_cast<int>(saxVoice->load()));
    const int art = static_cast<int>(articulation->load());
    next.articulation = art == 0 ? topbrass::Articulation::shortNote : art == 1 ? topbrass::Articulation::sustain : topbrass::Articulation::fall;
    next.gain = juce::Decibels::decibelsToGain(master->load());
    next.releaseMs = release->load(); next.tightness = tightness->load(); next.tone = tone->load();
    return next;
}
void TopBrassProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    if (audio.getNumChannels() < 2) return;
    blockSettings=currentSettings();engine.setSettings(blockSettings);
    const bool detailEnabled=tenorDetailMidi->load()>.5f;
    if (detailEnabled!=detailMidiEnabled) detailControllerDown={};
    detailMidiEnabled=detailEnabled;
    performanceConfig={static_cast<topbrass::PlayingMode>(static_cast<int>(playingMode->load())),static_cast<int>(lowestKey->load()),0,hornFeel->load()};
    for(std::size_t part=0;part<blockSettings.parts.size();++part)if(blockSettings.parts[part].enabled)performanceConfig.enabled|=1u<<part;
    if(section.configure(performanceConfig))engine.allNotesOff(0,true);
    if(performanceConfig.mode!=topbrass::PlayingMode::layered){
        double bpm=freeTempo->load();bool running=false;std::optional<double> ppq;usingHostTempo.store(false);
        if(auto* playhead=getPlayHead())if(const auto position=playhead->getPosition()){
            if(const auto hostBpm=position->getBpm();hostBpm&&std::isfinite(*hostBpm)&&*hostBpm>=20&&*hostBpm<=400){bpm=*hostBpm;usingHostTempo.store(true);}
            running=position->getIsPlaying();if(const auto hostPpq=position->getPpqPosition())ppq=*hostPpq;
        }
        performanceBpm.store(static_cast<float>(bpm));
        if(section.clock(bpm,running,ppq))engine.allNotesOff(0,true);
    }
    {
        const auto read = noteFifo.read(noteFifo.getNumReady());
        read.forEach([this](int index) {
            const auto& note = uiNotes[static_cast<std::size_t>(index)];
            if (note.detail) { engine.triggerTenorDetail(*note.detail); return; }
            if (note.velocity > 0) dispatch(juce::MidiMessage::noteOn(note.channel,note.note,note.velocity));
            else dispatch(juce::MidiMessage::noteOff(note.channel,note.note));
        });
    }
    if (panicRequested.exchange(false)) { engine.allNotesOff(0,true); section.reset(); detailControllerDown={}; }
    auto* left = audio.getWritePointer(0);
    auto* right = audio.getWritePointer(1);
    int cursor = 0;
    for (const auto metadata : midi) {
        const int offset = std::clamp(metadata.samplePosition, cursor, audio.getNumSamples());
        renderSegment(left + cursor, right + cursor, offset - cursor);
        if (metadata.numBytes <= 3) dispatch(metadata.getMessage());
        cursor = offset;
    }
    renderSegment(left + cursor, right + cursor, audio.getNumSamples() - cursor);
    peak.store(std::max(audio.getMagnitude(0, 0, audio.getNumSamples()), audio.getMagnitude(1, 0, audio.getNumSamples())));
    voiceCount.store(engine.activeVoices());
    midi.clear();
}
void TopBrassProcessor::enqueueNote(int channel, int note, float velocity) noexcept {
    if (noteFifo.getFreeSpace() == 0) { panicRequested.store(true); return; }
    const auto write = noteFifo.write(1);
    write.forEach([&](int index) { uiNotes[static_cast<std::size_t>(index)] = { channel, note, velocity, {} }; });
}
void TopBrassProcessor::enqueueTenorDetail(topbrass::TenorDetail kind) noexcept {
    if (noteFifo.getFreeSpace()==0) {panicRequested.store(true);return;}
    const auto write=noteFifo.write(1);
    write.forEach([&](int index){uiNotes[static_cast<std::size_t>(index)]={1,0,0,kind};});
}
void TopBrassProcessor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = parameters.copyState().createXml()) copyXmlToBinary(*xml, dest);
}
void TopBrassProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size); xml && xml->hasTagName(parameters.state.getType())) {
        auto state=juce::ValueTree::fromXml(*xml);
        if (!state.getChildWithProperty("id","humanize").isValid()) {
            juce::ValueTree amount("PARAM");
            amount.setProperty("id","humanize",nullptr);
            amount.setProperty("value",0.0f,nullptr);
            state.addChild(amount,-1,nullptr);
        }
        parameters.replaceState(state);
        panicRequested.store(true);
    }
}
juce::AudioProcessorEditor* TopBrassProcessor::createEditor() { return new TopBrassEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TopBrassProcessor(); }

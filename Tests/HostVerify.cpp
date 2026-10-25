#include "PluginProcessor.h"
#include <iostream>
#include <chrono>
#include <cmath>
#include <numeric>
#include <stdexcept>
namespace {
void check(bool good, const char* message) { if (!good) throw std::runtime_error(message); }
template<typename Condition> void waitForUI(Condition condition,const char* message) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!condition()) {
        check(std::chrono::steady_clock::now()<deadline,message);
        juce::Timer::callPendingTimersSynchronously();
        if(!condition()) juce::Thread::sleep(10);
    }
}
void set(TopBrassProcessor& p, const juce::String& id, float value) {
    auto* parameter = p.parameters.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
std::unique_ptr<juce::FileOutputStream> freshStream(const juce::File& file) {
    auto stream = file.createOutputStream();
    check(stream != nullptr && stream->setPosition(0) && stream->truncate().wasOk(), "Artifact output must overwrite previous contents");
    return stream;
}
void checkHumanizeParameter() {
    TopBrassProcessor processor;
    auto* amount=processor.parameters.getParameter("humanize");
    check(amount!=nullptr,"Humanize must be an automatable processor parameter");
    check(amount->getParameterIndex()==50 && amount->getVersionHint()==7,"Humanize must append after existing host parameters");
    check(std::abs(amount->convertFrom0to1(amount->getDefaultValue())-.35f)<1e-6f,"Fresh instruments start subtly humanized");
    processor.loadFactoryPreset(8);
    check(processor.parameters.getRawParameterValue("humanize")->load()==0,"Uncolored reference preset must be unhumanized");
    processor.loadFactoryPreset(0);
    check(processor.parameters.getRawParameterValue("humanize")->load()>.3f,"Musical presets should retain a subtle human touch");
}
void checkHumanizeModes(const juce::File& output) {
    TopBrassProcessor bankOwner;
    check(bankOwner.bankError().empty(),"Humanize integration fixture must load its real sample bank");
    const auto play=[](int mode,float amount,int blockSize) {
        TopBrassProcessor processor;
        processor.loadFactoryPreset(8);
        set(processor,"playingMode",static_cast<float>(mode));set(processor,"humanize",amount);
        set(processor,"hornFeel",0);set(processor,"freeTempo",120);
        processor.prepareToPlay(48000,blockSize);
        juce::AudioBuffer<float> audio(2,192000);audio.clear();
        juce::MidiBuffer midi;
        for(int offset=0;offset<audio.getNumSamples();offset+=blockSize) {
            const int count=std::min(blockSize,audio.getNumSamples()-offset);
            juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(),2,offset,count);
            if(offset<=128 && offset+count>128)midi.addEvent(juce::MidiMessage::noteOn(1,60,.9f),128-offset);
            processor.processBlock(block,midi);
        }
        return audio;
    };
    for(int mode : {0,1,2}) {
        const auto dry=play(mode,0,512), wet=play(mode,1,512), split=play(mode,1,31);
        float difference=0,partitionError=0;
        for(int channel=0;channel<2;++channel)for(int frame=0;frame<wet.getNumSamples();++frame) {
            difference=std::max(difference,std::abs(wet.getSample(channel,frame)-dry.getSample(channel,frame)));
            partitionError=std::max(partitionError,std::abs(wet.getSample(channel,frame)-split.getSample(channel,frame)));
        }
        check(difference>1e-5f,"Humanize must affect the real processor in Layered, Relay and Groove");
        check(partitionError<1e-6f,"Humanized processor audio must be independent of host block size");
        check(wet.getMagnitude(0,128)==0,"Humanization must never move live MIDI ahead of its input timestamp");
        if(output!=juce::File()) {
            const auto folder=output.getChildFile("humanize");folder.createDirectory();
            for(const auto& name : {juce::String("neutral"),juce::String("humanized")}) {
                const auto& audio=name=="neutral"?dry:wet;
                std::unique_ptr<juce::OutputStream> stream=freshStream(folder.getChildFile(juce::String(mode)+"-"+name+".wav"));
                const auto options=juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(2).withBitsPerSample(24);
                auto writer=juce::WavAudioFormat{}.createWriterFor(stream,options);
                check(writer!=nullptr && writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples()),"Humanize comparison render");
            }
        }
    }
}
void checkInitialGain() {
    const auto render = [](bool warm) {
        TopBrassProcessor processor;
        processor.loadFactoryPreset(8);
        set(processor, "master", -18);
        processor.prepareToPlay(48000, 512);
        juce::AudioBuffer<float> audio(2, 512); juce::MidiBuffer midi;
        if (warm) for (int i = 0; i < 8; ++i) processor.processBlock(audio, midi);
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        processor.processBlock(audio, midi);
        return audio;
    };
    const auto cold = render(false), warm = render(true);
    float error = 0;
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < 512; ++frame)
            error = std::max(error, std::abs(cold.getSample(channel, frame) - warm.getSample(channel, frame)));
    check(error < 1.0e-7f, "First note must use prepared output gain without an unintended startup ramp");
}
void checkOriginalPlayback(const juce::File& root) {
    const auto manifest = juce::JSON::parse(root.getChildFile("assets/samples.lock.json"));
    const auto* entries = manifest.getArray();
    check(entries != nullptr, "Read pinned original source manifest");
    juce::var selected;
    int layer = 0;
    for (const auto& entry : *entries)
        if (static_cast<int>(entry["instrument"]) == 0 && static_cast<int>(entry["articulation"]) == 0
            && static_cast<int>(entry["root"]) == 60 && static_cast<int>(entry["layer"]) > layer) {
            selected = entry; layer = static_cast<int>(entry["layer"]);
        }
    check(layer > 0, "Find native C4 tenor original");
    const auto source = root.getChildFile("assets/cache").getChildFile(selected["repo"].toString().fromLastOccurrenceOf("/", false, false))
        .getChildFile(selected["path"].toString());
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
    check(reader != nullptr && reader->numChannels == 2, "Independently decode original stereo WAV");
    juce::AudioBuffer<float> original(2, 4096);
    check(reader->read(&original, 0, 4096, 0, true, true), "Read original attack");
    TopBrassProcessor processor; processor.loadFactoryPreset(8);
    set(processor, "master", -12); set(processor, "part0.level", -18); set(processor, "part0.pan", 0);
    processor.prepareToPlay(reader->sampleRate, 512);
    float error = 0;
    for (int offset = 0; offset < 4096; offset += 512) {
        juce::AudioBuffer<float> block(2, 512); juce::MidiBuffer midi;
        if (offset == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        processor.processBlock(block, midi);
        for (int channel = 0; channel < 2; ++channel)
            for (int frame = 0; frame < 512; ++frame)
                error = std::max(error, std::abs(block.getSample(channel, frame)
                    - original.getSample(channel, offset+frame)*juce::Decibels::decibelsToGain(-30.0f)));
    }
    std::cout << "Original stereo WAV vs actual processor, frame-zero note: max error=" << error << '\n';
    check(error < 1.0e-6f, "Neutral processor attack must preserve original stereo recording at the intended gain");
}
void checkRecordedKeys(const juce::File& root) {
    const auto catalog = juce::JSON::parse(root.getChildFile("assets/cache/bear-details/catalog.json"));
    const auto* zones = catalog["zones"].getArray();
    check(zones != nullptr, "Read original mechanical mappings");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::array<juce::AudioBuffer<float>, 2> originals;
    double rate = 0;
    for (int trigger = 0; trigger < 2; ++trigger) {
        juce::String source;
        for (const auto& zone : *zones) {
            const auto op = zone["opcodes"];
            if (zone["kind"].toString() == (trigger ? "key-open" : "key-close") && op["lokey"].toString() == "48"
                && op["lorand"].toString().getDoubleValue() == 0) { source = zone["source"].toString(); break; }
        }
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(root.getChildFile("assets/cache/karoryfer.bear-sax").getChildFile(source)));
        check(reader != nullptr && reader->numChannels == 1, "Decode original mechanical WAV independently");
        if (trigger == 0) rate = reader->sampleRate;
        check(rate == reader->sampleRate, "Mechanical source rates must match this fixture");
        originals[static_cast<std::size_t>(trigger)].setSize(1, static_cast<int>(reader->lengthInSamples));
        check(reader->read(&originals[static_cast<std::size_t>(trigger)], 0, static_cast<int>(reader->lengthInSamples), 0, true, false), "Read original key recording");
    }
    TopBrassProcessor dry, detailed;
    for (auto* processor : {&dry, &detailed}) {
        processor->loadFactoryPreset(9);
        set(*processor, "part0.octave", 0); set(*processor, "part0.level", -12); set(*processor, "master", -12); set(*processor, "part0.pan", 0);
        set(*processor, "tone", .8f); set(*processor, "tightness", 1);
        set(*processor, "baritoneKeys", processor == &detailed ? 1 : 0);
        processor->prepareToPlay(rate, 512);
    }
    const int off = ((originals[0].getNumSamples()+1023)/512)*512;
    float worst = 0;
    for (int offset = 0; offset < off+4096; offset += 512) {
        juce::AudioBuffer<float> plain(2,512), decorated(2,512); juce::MidiBuffer midi;
        if (offset == 0) {
            midi.addEvent(juce::MidiMessage::controllerEvent(1,1,127),0);
            midi.addEvent(juce::MidiMessage::pitchWheel(1,16383),0);
            midi.addEvent(juce::MidiMessage::noteOn(1,48,1.0f),0);
        }
        if (offset == off) midi.addEvent(juce::MidiMessage::noteOff(1,48),0);
        auto copy = midi; dry.processBlock(plain,midi); detailed.processBlock(decorated,copy);
        for (int frame = 0; frame < 512; ++frame) {
            const int absolute = offset+frame, trigger = absolute >= off ? 1 : 0, position = trigger ? absolute-off : absolute;
            const auto& source = originals[static_cast<std::size_t>(trigger)];
            const float expected = position < source.getNumSamples() ? source.getSample(0,position)*juce::Decibels::decibelsToGain(-36.0f)*.70710678f : 0;
            for (int channel = 0; channel < 2; ++channel)
                worst = std::max(worst,std::abs(decorated.getSample(channel,frame)-plain.getSample(channel,frame)-expected));
        }
    }
    std::cout << "Original key WAV vs actual processor difference: max error=" << worst << '\n';
    check(worst < 1e-6f,"Recorded close/open must retain calibrated original PCM despite musical Tone, Accent, CC1 and pitch wheel");
}
void checkRecordedAirflow(const juce::File& root) {
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(root.getChildFile("assets/cache/karoryfer.bear-sax/Samples/noise_breath.wav")));
    check(reader != nullptr && reader->numChannels == 1, "Decode original airflow WAV independently");
    juce::AudioBuffer<float> original(1,4096); check(reader->read(&original,0,4096,0,true,false), "Read original airflow attack");
    TopBrassProcessor plain, detailed;
    for (auto* processor : {&plain,&detailed}) {
        processor->loadFactoryPreset(9); set(*processor,"part0.octave",0); set(*processor,"part0.level",-12); set(*processor,"part0.pan",0); set(*processor,"master",-12);
        set(*processor,"tone",.8f); set(*processor,"tightness",1); set(*processor,"baritoneAir",processor==&detailed?1:0); processor->prepareToPlay(reader->sampleRate,512);
    }
    float worst=0;
    for (int offset=0;offset<4096;offset+=512) {
        juce::AudioBuffer<float> a(2,512),b(2,512); juce::MidiBuffer midi;
        if (offset==0) { midi.addEvent(juce::MidiMessage::controllerEvent(1,1,127),0); midi.addEvent(juce::MidiMessage::pitchWheel(1,16383),0); midi.addEvent(juce::MidiMessage::noteOn(1,50,.2f),0); }
        auto copy=midi; plain.processBlock(a,midi); detailed.processBlock(b,copy);
        for(int frame=0;frame<512;++frame) {
            const float envelope=std::min(1.0f,static_cast<float>((offset+frame+1)/(reader->sampleRate*.005)));
            const float expected=original.getSample(0,offset+frame)*envelope*.4f*juce::Decibels::decibelsToGain(-24.0f)*.70710678f;
            for(int channel=0;channel<2;++channel)worst=std::max(worst,std::abs(b.getSample(channel,frame)-a.getSample(channel,frame)-expected));
        }
    }
    std::cout<<"Original airflow WAV vs actual processor difference: max error="<<worst<<'\n';
    check(worst<1e-6f,"Recorded airflow must preserve original PCM and calibration independently of musical Tone, Accent, CC1 and pitch bend");
}
juce::AudioBuffer<float> sectionPerformance(int mode,int blockSize,const juce::File& output = {},float velocity=.9f,bool sustain=false) {
    TopBrassProcessor processor;processor.loadFactoryPreset(15);set(processor,"playingMode",static_cast<float>(mode));set(processor,"part3.enabled",0);
    if(sustain)set(processor,"articulation",1);
    processor.prepareToPlay(48000,blockSize);
    constexpr int length=48000*6;juce::AudioBuffer<float> audio(2,length);audio.clear();juce::MidiBuffer timeline,midi;
    if(sustain)timeline.addEvent(juce::MidiMessage::controllerEvent(1,11,127),0);
    for(int bar=0;bar<2;++bar)for(int voice=0;voice<3;++voice){
        const int note=std::array<std::array<int,3>,2>{{{{60,64,67}},{{62,65,69}}}}[static_cast<std::size_t>(bar)][static_cast<std::size_t>(voice)];
        timeline.addEvent(juce::MidiMessage::noteOn(1,note,velocity),bar*96000+voice*48);
        timeline.addEvent(juce::MidiMessage::noteOff(1,note),bar*96000+91200);
    }
    std::vector<double> timings;timings.reserve(static_cast<std::size_t>(length/blockSize+1));
    for(int offset=0;offset<length;offset+=blockSize){const int count=std::min(blockSize,length-offset);std::array<float*,2> ptr{audio.getWritePointer(0)+offset,audio.getWritePointer(1)+offset};juce::AudioBuffer<float> block(ptr.data(),2,count);midi.clear();midi.addEvents(timeline,offset,count,-offset);const auto start=std::chrono::steady_clock::now();processor.processBlock(block,midi);timings.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());}
    check(processor.voiceCount.load()==0,"Section release must leave no pending or sounding notes after the phrase");
    if(output!=juce::File{}){
        std::unique_ptr<juce::OutputStream> stream=freshStream(output);auto writer=juce::WavAudioFormat{}.createWriterFor(stream,juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
        check(writer!=nullptr&&writer->writeFromAudioSampleBuffer(audio,0,length),"Write performed section audition");
        check(processor.parameters.copyState().createXml()->writeTo(output.getSiblingFile(output.getFileName()+".parameters.xml")),"Write section playing-mode parameters");
        std::sort(timings.begin(),timings.end());juce::DynamicObject::Ptr metrics=new juce::DynamicObject;
        metrics->setProperty("mode",mode);metrics->setProperty("block_size",blockSize);metrics->setProperty("sample_rate",48000);metrics->setProperty("mean_us",std::accumulate(timings.begin(),timings.end(),0.0)/timings.size());metrics->setProperty("p99_us",timings[static_cast<std::size_t>(timings.size()*.99)]);metrics->setProperty("max_us",timings.back());metrics->setProperty("deadline_us",blockSize/48000.0*1e6);
        output.getSiblingFile(output.getFileName()+".performance.json").replaceWithText(juce::JSON::toString(juce::var(metrics.get())));
    }
    return audio;
}
class SectionTestClock final : public juce::AudioPlayHead {
public:
    PositionInfo position;
    juce::Optional<PositionInfo> getPosition() const override { return position; }
};
void checkGrooveGates() {
    for (int tempo : {120,240}) for (int articulation : {0,1,2}) {
        TopBrassProcessor processor;
        processor.loadFactoryPreset(15);
        set(processor,"hornFeel",1);set(processor,"freeTempo",static_cast<float>(tempo));
        set(processor,"release",500);set(processor,"articulation",static_cast<float>(articulation));
        processor.prepareToPlay(48000,128);
        const int beatFrames=48000*60/tempo;
        const int gap=beatFrames*2/5, gapLength=beatFrames/20;
        juce::AudioBuffer<float> audio(2,beatFrames*2/3);audio.clear();
        juce::MidiBuffer midi;
        for (int offset=0;offset<audio.getNumSamples();offset+=128) {
            const int count=std::min(128,audio.getNumSamples()-offset);
            juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(),2,offset,count);
            if(offset==0) {
                midi.addEvent(juce::MidiMessage::controllerEvent(1,64,127),0);
                for(int note:{60,64,67})midi.addEvent(juce::MidiMessage::noteOn(1,note,.9f),0);
            }
            processor.processBlock(block,midi);
            if(offset>=gap && offset+count<=gap+gapLength)
                check(processor.voiceCount.load()==0,"Groove gate must clear recorded short, fall and sustain tails before the next hit, despite pedal and a long release setting");
        }
        check(audio.getMagnitude(0,beatFrames/5)>.001f,"Opening groove ensemble must sound");
        check(audio.getMagnitude(gap,gapLength)<1e-7f,"Groove must have a silent gap between ensemble hits");
        check(audio.getMagnitude(beatFrames/2,beatFrames/8)>.001f,"The following groove ensemble hit must still sound");
    }
}
void checkSectionPlaying() {
    TopBrassProcessor processor;processor.loadFactoryPreset(15);processor.prepareToPlay(48000,512);
    juce::AudioBuffer<float> block(2,512);juce::MidiBuffer midi;
    for(int note:{59,75})midi.addEvent(juce::MidiMessage::noteOn(1,note,1.0f),0);
    processor.processBlock(block,midi);check(block.getMagnitude(0,512)==0,"Outside the fifteen-key window must be silent in section mode");
    midi.addEvent(juce::MidiMessage::noteOn(1,60,1.0f),128);processor.processBlock(block,midi);
    check(block.getMagnitude(0,128)==0&&block.getMagnitude(128,384)>0,"The first routed lead must preserve its input timestamp without lookahead latency");
    processor.panic();processor.processBlock(block,midi);check(processor.voiceCount.load()==0,"Panic must cancel all section scheduling");
    for(int i=0;i<100;++i){processor.processBlock(block,midi);check(block.getMagnitude(0,512)==0,"No generated notes may appear after panic");}
    set(processor,"part0.enabled",0);set(processor,"part2.enabled",0);set(processor,"part3.enabled",0);
    processor.prepareToPlay(48000,512);midi.addEvent(juce::MidiMessage::noteOn(1,60,1.0f),0);processor.processBlock(block,midi);
    const int trumpetVoices=processor.voiceCount.load();
    midi.addEvent(juce::MidiMessage::noteOff(1,60),0);midi.addEvent(juce::MidiMessage::noteOn(1,64,1.0f),0);processor.processBlock(block,midi);
    check(processor.voiceCount.load()==trumpetVoices && block.getMagnitude(0,512)>0,"A groove note replacement must sound immediately with its prior short tail already faded");
    midi.addEvent(juce::MidiMessage::noteOff(1,64),0);for(int i=0;i<150;++i)processor.processBlock(block,midi);check(processor.voiceCount.load()==0,"Groove note release must leave no generated voices held");
    SectionTestClock clock;clock.position.setBpm(90);clock.position.setPpqPosition(0);clock.position.setIsPlaying(true);
    TopBrassProcessor hosted;hosted.loadFactoryPreset(15);hosted.setPlayHead(&clock);hosted.prepareToPlay(48000,512);
    midi.addEvent(juce::MidiMessage::noteOn(1,60,1.0f),0);hosted.processBlock(block,midi);check(hosted.usingHostTempo.load()&&hosted.performanceBpm.load()==90,"Section playing must follow the supplied host tempo");
    clock.position.setPpqPosition(512.0*90/(48000*60));hosted.processBlock(block,midi);check(hosted.voiceCount.load()>0,"A continuous host clock must retain the held performance");
    clock.position.setPpqPosition(8);hosted.processBlock(block,midi);check(hosted.voiceCount.load()==0&&block.getMagnitude(0,512)==0,"Seeking must cancel held and pending section notes");
    hosted.setPlayHead(nullptr);
    const auto a=sectionPerformance(2,512),b=sectionPerformance(2,31);
    float error=0;for(int channel=0;channel<2;++channel)for(int frame=0;frame<a.getNumSamples();++frame)error=std::max(error,std::abs(a.getSample(channel,frame)-b.getSample(channel,frame)));
    check(error<1e-6f,"The actual section audio must be independent of host block partitioning");
    const auto layered=sectionPerformance(0,512);
    check(a.getMagnitude(36000,4800)>layered.getMagnitude(36000,4800)+.001f,"Held-section mode must produce audible syncopated section hits beyond the original short notes");
    std::cout<<"Section processor block-partition error="<<error<<'\n';
}
void renderPreset(TopBrassProcessor& processor, int preset, const juce::File& directory) {
    processor.loadFactoryPreset(preset); processor.prepareToPlay(48000, 512);
    const bool sustained = processor.parameters.getRawParameterValue("articulation")->load() > 0.5f;
    const int beat = static_cast<int>(48000 * 60.0 / 112);
    const bool doubled = processor.parameters.getRawParameterValue("part1.doubleTongue")->load()>.5f || processor.parameters.getRawParameterValue("part2.doubleTongue")->load()>.5f;
    const bool section = processor.parameters.getRawParameterValue("playingMode")->load()>.5f;
    const int spacing = sustained || doubled ? beat : beat / 2;
    const int noteLength = sustained ? beat * 4 / 5 : beat / 3;
    const std::array<int, 16> notes {55, 56, 58, 60, 62, 60, 58, 56, 55, 55, 58, 60, 62, 60, 58, 55};
    const int length = 15 * spacing + noteLength + 48000;
    juce::AudioBuffer<float> audio(2, length); audio.clear();
    juce::MidiBuffer timeline, blockMidi;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const int on = static_cast<int>(i) * spacing;
        const int note = notes[i] + (section ? 5 : 0);
        const float velocity = doubled ? .4f+.17f*static_cast<float>(i%4) : .7f+.09f*static_cast<float>(i%4);
        timeline.addEvent(juce::MidiMessage::noteOn(1, note, velocity), on);
        timeline.addEvent(juce::MidiMessage::noteOff(1, note), on + noteLength);
    }
    for (int offset = 0; offset < length; offset += 512) {
        const int count = std::min(512, length - offset);
        std::array<float*, 2> channels { audio.getWritePointer(0) + offset, audio.getWritePointer(1) + offset };
        juce::AudioBuffer<float> block(channels.data(), 2, count);
        blockMidi.clear(); blockMidi.addEvents(timeline, offset, count, -offset);
        processor.processBlock(block, blockMidi);
    }
    check(audio.getMagnitude(0, length) > 0, "Factory preset demo must contain audio");
    const auto name = TopBrassProcessor::factoryPresetNames()[preset].toLowerCase().replaceCharacter(' ', '-') + ".wav";
    std::unique_ptr<juce::OutputStream> stream = freshStream(directory.getChildFile(name));
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(2).withBitsPerSample(24);
    auto writer = juce::WavAudioFormat{}.createWriterFor(stream, options);
    check(writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, length), "Factory preset demo write");
    check(processor.parameters.copyState().createXml()->writeTo(directory.getChildFile(name+".parameters.xml")), "Factory preset parameter snapshot write");
}
void renderScore(const juce::File& score, int articulation, int solo, const juce::File& directory) {
    auto input = score.createInputStream();
    juce::MidiFile file;
    check(input != nullptr && file.readFrom(*input, false), "Read section evaluation MIDI");
    file.convertTimestampTicksToSeconds();
    juce::MidiBuffer timeline;
    for (int track = 0; track < file.getNumTracks(); ++track) {
        const auto* sequence = file.getTrack(track);
        for (int event = 0; event < sequence->getNumEvents(); ++event) {
            const auto& message = sequence->getEventPointer(event)->message;
            if (!message.isMetaEvent())
                timeline.addEvent(message, static_cast<int>(std::llround(message.getTimeStamp() * 48000)));
        }
    }
    TopBrassProcessor processor;
    set(processor, "articulation", static_cast<float>(articulation));
    set(processor, "tone", 0); set(processor, "tightness", 0);
    set(processor, "release", articulation == 1 ? 140 : 65); set(processor, "master", -3);
    const std::array<float, 4> levels {-6, -7, -8, -13}, pans {-0.25f, 0.25f, -0.1f, 0.1f};
    for (int part = 0; part < 4; ++part) {
        const auto prefix = "part" + juce::String(part) + ".";
        set(processor, prefix + "enabled", solo < 0 || part == solo ? 1 : 0);
        set(processor, prefix + "channel", static_cast<float>(part + 1));
        set(processor, prefix + "octave", 0);
        set(processor, prefix + "level", levels[static_cast<std::size_t>(part)]);
        set(processor, prefix + "pan", pans[static_cast<std::size_t>(part)]);
    }
    processor.prepareToPlay(48000, 512);
    const int length = static_cast<int>(std::ceil(file.getLastTimestamp() * 48000)) + 96000;
    juce::AudioBuffer<float> audio(2, length); audio.clear();
    juce::MidiBuffer midi;
    for (int offset = 0; offset < length; offset += 512) {
        const int count = std::min(512, length - offset);
        std::array<float*, 2> channels {audio.getWritePointer(0) + offset, audio.getWritePointer(1) + offset};
        juce::AudioBuffer<float> block(channels.data(), 2, count);
        midi.clear(); midi.addEvents(timeline, offset, count, -offset);
        processor.processBlock(block, midi);
        if (offset == 0 && articulation == 2)
            check(processor.voiceCount.load() == 1, "Fall control must select one recorded fall through the processor");
    }
    const float peak = audio.getMagnitude(0, length);
    check(std::isfinite(peak) && peak > 0 && peak < 0.95f, "Section example must have audio below safety knee");
    const std::array<juce::String, 4> names {"tenor-sax", "trumpet", "trombone", "tuba"};
    const auto suffix = solo < 0 ? juce::String("section") : names[static_cast<std::size_t>(solo)];
    const auto name = score.getFileNameWithoutExtension() + "-" + suffix + ".wav";
    std::unique_ptr<juce::OutputStream> stream = freshStream(directory.getChildFile(name));
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(2).withBitsPerSample(24);
    auto writer = juce::WavAudioFormat{}.createWriterFor(stream, options);
    check(writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, length), "Section example write");
    auto state = processor.parameters.copyState().createXml();
    check(state->writeTo(directory.getChildFile(name + ".parameters.xml")), "Section parameter manifest write");
    std::cout << name << ": peak=" << peak << ", samples=" << length << '\n';
}
}
void checkTenorDetails(const juce::File& root) {
    juce::AudioFormatManager formats;formats.registerBasicFormats();
    const auto source=root.getChildFile("assets/cache/MTG.SoloSax/MTG Solo Saxophones/Samples/ten_b_01.flac");
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
    check(reader!=nullptr&&reader->sampleRate==48000,"Read independently decoded tenor breath source");
    juce::AudioBuffer<float> original(1,2048);check(reader->read(&original,0,2048,0,true,false),"Read breath frames");
    const float gain=juce::Decibels::decibelsToGain(-24.f)*std::cos(juce::MathConstants<float>::pi*.25f);
    for(int mode=0;mode<3;++mode) {
        TopBrassProcessor processor;processor.loadFactoryPreset(8);set(processor,"master",-12);set(processor,"part0.level",-12);set(processor,"part0.pan",0);set(processor,"playingMode",static_cast<float>(mode));
        processor.prepareToPlay(48000,512);juce::AudioBuffer<float> audio(2,512);juce::MidiBuffer midi;processor.processBlock(audio,midi);
        set(processor,"tenorDetailMidi",1);midi.addEvent(juce::MidiMessage::controllerEvent(1,20,127),128);processor.processBlock(audio,midi);
        float worst=0;
        for(int channel=0;channel<2;++channel)for(int frame=0;frame<512;++frame) {
            const float expected=frame<128?0:original.getSample(0,frame-128)*gain;
            worst=std::max(worst,std::abs(audio.getSample(channel,frame)-expected));
        }
        check(worst<1e-8f,"Explicit detail CC must retain source PCM, MIDI offset and level in every playing mode");
        midi.addEvent(juce::MidiMessage::controllerEvent(1,20,127),0);processor.processBlock(audio,midi);
        check(processor.voiceCount.load()==1,"Held nonzero controller must not retrigger details");
        midi.addEvent(juce::MidiMessage::controllerEvent(1,20,0),0);midi.addEvent(juce::MidiMessage::controllerEvent(1,20,127),64);processor.processBlock(audio,midi);
        check(processor.voiceCount.load()==2,"Controller rearm must trigger another recorded take");
        processor.panic();processor.processBlock(audio,midi);check(audio.getMagnitude(0,512)==0&&processor.voiceCount.load()==0,"Panic must stop explicit details");
        processor.enqueueTenorDetail(topbrass::TenorDetail::breath);processor.processBlock(audio,midi);check(audio.getMagnitude(0,512)>0,"UI detail trigger must bypass section-note routing");
        set(processor,"part0.enabled",0);processor.processBlock(audio,midi);processor.processBlock(audio,midi);check(audio.getMagnitude(0,512)==0,"Muted tenor detail must stop");
    }
    TopBrassProcessor routed;routed.loadFactoryPreset(8);set(routed,"tenorDetailMidi",1);set(routed,"part0.channel",2);routed.prepareToPlay(48000,512);
    juce::AudioBuffer<float> channelAudio(2,512);juce::MidiBuffer channelMidi;routed.processBlock(channelAudio,channelMidi);
    channelMidi.addEvent(juce::MidiMessage::controllerEvent(1,21,127),0);routed.processBlock(channelAudio,channelMidi);check(routed.voiceCount.load()==0,"Layered detail must honor sax channel routing");
    channelMidi.addEvent(juce::MidiMessage::controllerEvent(2,21,127),0);routed.processBlock(channelAudio,channelMidi);check(routed.voiceCount.load()==1,"CC21 and its per-channel edge state must trigger the key sound");
    channelMidi.addEvent(juce::MidiMessage::controllerEvent(2,123,0),0);routed.processBlock(channelAudio,channelMidi);routed.processBlock(channelAudio,channelMidi);check(routed.voiceCount.load()==0,"All notes off must clear explicit details");
    channelMidi.addEvent(juce::MidiMessage::controllerEvent(2,21,127),0);routed.processBlock(channelAudio,channelMidi);check(routed.voiceCount.load()==1,"All notes off must rearm detail controllers");
    TopBrassProcessor disabled;disabled.loadFactoryPreset(8);disabled.prepareToPlay(48000,512);
    juce::AudioBuffer<float> audio(2,512);juce::MidiBuffer midi;disabled.processBlock(audio,midi);
    midi.addEvent(juce::MidiMessage::controllerEvent(1,20,127),0);disabled.processBlock(audio,midi);check(audio.getMagnitude(0,512)==0,"Detail MIDI must default off");
}

int main(int argc, char** argv) {
    const juce::ScopedJuceInitialiser_GUI gui;
    try {
        checkHumanizeParameter(); checkHumanizeModes(argc>1?juce::File(juce::String::fromUTF8(argv[1])):juce::File()); checkInitialGain(); checkGrooveGates(); checkSectionPlaying();
        if (argc > 2) {
            const auto root = juce::File(juce::String::fromUTF8(argv[2])).getParentDirectory();
            checkOriginalPlayback(root); checkRecordedKeys(root); checkRecordedAirflow(root); checkTenorDetails(root);
        }
        TopBrassProcessor p;
        check(p.bankError().empty(), p.bankError().c_str());
        check(p.bankBytes() > 100000000, "Bundled samples must load from this executable");
        auto* art = dynamic_cast<juce::AudioParameterChoice*>(p.parameters.getParameter("articulation"));
        check(art != nullptr && art->choices == juce::StringArray{"Short / punch","Sustain","Fall"}
              && art->convertTo0to1(1)==.5f && art->convertTo0to1(2)==1,
              "Adding recorded tonguing must preserve existing articulation automation");
        check(p.parameters.getParameter("freeTempo")->getParameterIndex()==31
              && p.parameters.getParameter("part1.doubleTongue")->getParameterIndex()==32
              && p.parameters.getParameter("part2.doubleTongue")->getParameterIndex()==33
              && p.parameters.getParameter("tenorDetailDb")->getParameterIndex()==34
              && p.parameters.getParameter("tenorDetailMidi")->getParameterIndex()==35,
              "New controls must append after the original parameter list");
        check(p.parameters.getParameter("part1.doubleTongue")->getVersionHint()==2
              && p.parameters.getParameter("part2.doubleTongue")->getVersionHint()==2,
              "New AU parameters need a later version hint to preserve Logic automation order");
        check(p.parameters.getParameter("tenorDetailDb")->getVersionHint()==3 && p.parameters.getParameter("tenorDetailMidi")->getVersionHint()==3,"Tenor details must sort after existing version2 AU parameters");
        check(p.parameters.getParameter("part4.enabled")->getParameterIndex() == 36 && p.parameters.getParameter("part5.enabled")->getParameterIndex() == 41 && p.parameters.getParameter("part5.doubleTongue")->getVersionHint() == 4, "Additional players must append after all original controls");
        check(p.parameters.getParameter("part0.tenorSource")->getParameterIndex()==47
              && p.parameters.getParameter("part4.tenorSource")->getParameterIndex()==48
              && p.parameters.getParameter("part0.tenorSource")->getVersionHint()==5
              && p.parameters.getParameter("part4.tenorSource")->getVersionHint()==5,
              "Per-player tenor sources must append after all existing automation");
        check(p.parameters.getParameter("part3.tubaVoice")->getParameterIndex()==49 && p.parameters.getParameter("part3.tubaVoice")->getVersionHint()==6,
              "Tuba voice must append after all existing automation with a later AU hint");
        check(p.warTubaAvailable(),"Production resources must include every War Tuba capability");
        for (int i = 0; i < TopBrassProcessor::factoryPresetNames().size(); ++i) {
            p.loadFactoryPreset(i);
            p.prepareToPlay(48000, 512);
            juce::AudioBuffer<float> presetAudio(2, 512); juce::MidiBuffer presetMidi;
            presetMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
            p.processBlock(presetAudio, presetMidi);
            check(presetAudio.getMagnitude(0, 0, 512) > 0, "Every factory preset must produce audio through the processor");
        }
        p.loadFactoryPreset(0);
        p.prepareToPlay(48000, 512);
        juce::AudioBuffer<float> audio(2, 512); juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 128);
        p.processBlock(audio, midi);
        check(audio.getMagnitude(0, 0, 128) == 0, "MIDI note must not sound before its sample offset");
        check(audio.getMagnitude(0, 128, 384) > 0, "Timestamped MIDI note must sound");
        set(p,"humanize",.73f);set(p,"part3.tubaVoice",2);set(p,"part0.tenorSource",1);set(p,"part4.tenorSource",1);set(p,"part4.enabled",1);set(p,"part5.enabled",1);set(p,"part4.channel",3);set(p,"part5.pan",.55f);set(p,"part5.doubleTongue",1);
        set(p,"tenorDetailDb",18);set(p,"tenorDetailMidi",1);set(p,"part1.doubleTongue",1);set(p,"part2.doubleTongue",1);set(p,"playingMode",2);set(p,"lowestKey",72);set(p,"hornFeel",.8f);set(p,"freeTempo",111);set(p, "baritoneAir", .57f); set(p, "baritoneKeys", .63f); set(p, "part0.enabled", 0); set(p, "part1.pan", -0.75f); set(p, "part2.octave", 1); set(p, "part3.channel", 4); set(p, "articulation", 2); set(p, "saxVoice", 4);
        juce::MemoryBlock state; p.getStateInformation(state);
        TopBrassProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        check(restored.bankBytes() == p.bankBytes(), "Restored instance bank size");
        for (auto id : {"humanize", "part3.tubaVoice", "part0.tenorSource", "part4.tenorSource", "part4.enabled", "part5.enabled", "part4.channel", "part5.pan", "part5.doubleTongue", "part0.enabled", "part1.pan", "part2.octave", "part3.channel", "articulation", "tone", "saxVoice", "baritoneKeys", "baritoneAir", "playingMode", "lowestKey", "hornFeel", "freeTempo", "part1.doubleTongue", "part2.doubleTongue", "tenorDetailDb", "tenorDetailMidi"})
            check(restored.parameters.getRawParameterValue(id)->load() == p.parameters.getRawParameterValue(id)->load(), "State roundtrip must preserve each control");
        auto oldState = p.parameters.copyState();
        const auto saxChild = oldState.getChildWithProperty("id", "saxVoice");
        check(saxChild.isValid(), "Serialized sax parameter must exist before removing it for legacy-state test");
        oldState.removeChild(saxChild, nullptr);
        oldState.removeChild(oldState.getChildWithProperty("id", "baritoneKeys"), nullptr);
        oldState.removeChild(oldState.getChildWithProperty("id", "baritoneAir"), nullptr);
        for(const auto* id:{"humanize","part3.tubaVoice","part0.tenorSource","part4.tenorSource","playingMode","lowestKey","hornFeel","freeTempo","part1.doubleTongue","part2.doubleTongue","tenorDetailDb","tenorDetailMidi"})oldState.removeChild(oldState.getChildWithProperty("id",id),nullptr);
        for (int part : {4, 5}) for (const auto* field : {"enabled", "level", "pan", "octave", "channel", "doubleTongue"})
            oldState.removeChild(oldState.getChildWithProperty("id", "part" + juce::String(part) + "." + field), nullptr);
        juce::MemoryBlock legacyState;
        juce::AudioProcessor::copyXmlToBinary(*oldState.createXml(), legacyState);
        restored.setStateInformation(legacyState.getData(), static_cast<int>(legacyState.getSize()));
        check(restored.parameters.getRawParameterValue("saxVoice")->load() == 0,
              "Loading a version 0.1 state must restore tenor even after selecting a baritone voice");
        check(restored.parameters.getRawParameterValue("part3.channel")->load() == 4,
              "Legacy state migration must preserve existing routing");
        check(restored.parameters.getRawParameterValue("baritoneKeys")->load() == 0, "Old projects must restore zero recorded-key amount after loading a new detailed patch");
        check(restored.parameters.getRawParameterValue("baritoneAir")->load() == 0, "Old projects must restore zero airflow");
        const auto* detailLevel=restored.parameters.getParameter("tenorDetailDb");
        check(restored.parameters.getRawParameterValue("tenorDetailDb")->load()==detailLevel->convertFrom0to1(detailLevel->getDefaultValue()) && restored.parameters.getRawParameterValue("tenorDetailMidi")->load()==0,"Old projects must restore the declared detail level default and keep MIDI triggering disabled");
        check(restored.parameters.getRawParameterValue("playingMode")->load()==0&&restored.parameters.getRawParameterValue("lowestKey")->load()==60,"Legacy projects must restore layered mode and the default playing window");
        check(restored.parameters.getRawParameterValue("part1.doubleTongue")->load()==0&&restored.parameters.getRawParameterValue("part2.doubleTongue")->load()==0,"Old projects must restore ordinary brass tonguing");
        check(restored.parameters.getRawParameterValue("part4.enabled")->load() == 0 && restored.parameters.getRawParameterValue("part5.enabled")->load() == 0, "Old projects must disable additional players after loading a six-player patch");
        check(restored.parameters.getRawParameterValue("part0.tenorSource")->load()==0 && restored.parameters.getRawParameterValue("part4.tenorSource")->load()==0,"Old projects must restore both original tenor sources after loading an alternate patch");
        check(restored.parameters.getRawParameterValue("part3.tubaVoice")->load()==0,"An old state must restore Original tuba after a War selection");
        check(restored.parameters.getRawParameterValue("humanize")->load()==0,"Legacy projects must restore zero humanization after a humanized patch");
        for(int preset:{19,20}) {
            p.loadFactoryPreset(preset);
            check(p.parameters.getRawParameterValue("part3.tubaVoice")->load()==preset-18 && p.parameters.getRawParameterValue("part3.enabled")->load()==1,
                  "War presets must select their playable tuba voice");
        }
        p.loadFactoryPreset(18);
        check(p.parameters.getRawParameterValue("part0.tenorSource")->load()==1 && p.parameters.getRawParameterValue("part4.tenorSource")->load()==0,"Alternate tenor preset must choose its source only for the primary sax");
        set(p, "part4.enabled", 1); set(p, "part5.enabled", 1);
        p.loadFactoryPreset(0);
        check(p.parameters.getRawParameterValue("part4.enabled")->load() == 0 && p.parameters.getRawParameterValue("part5.enabled")->load() == 0, "Factory presets must reset additional players to off");
        check(p.parameters.getRawParameterValue("part0.tenorSource")->load()==0 && p.parameters.getRawParameterValue("part4.tenorSource")->load()==0,"Existing factory presets must restore original tenor sources");
        check(p.parameters.getRawParameterValue("part3.tubaVoice")->load()==0,"Existing presets must reset the tuba voice to Original");
        const char invalid[] = "invalid state";
        restored.setStateInformation(invalid, sizeof(invalid));
        check(restored.parameters.getRawParameterValue("part3.channel")->load() == 4, "Invalid state must not alter valid state");
        set(p,"playingMode",0);
        p.panic(); p.processBlock(audio, midi); check(p.voiceCount.load() == 0, "Panic must terminate voices");
        p.enqueueNote(1, 60, 0.9f); p.processBlock(audio, midi); check(p.voiceCount.load() > 0, "Onscreen keyboard note must sound");
        p.enqueueNote(1, 60, 0); for (int i = 0; i < 100; ++i) p.processBlock(audio, midi);
        check(p.voiceCount.load() == 0, "Onscreen note release");
        for (int i = 0; i < 200; ++i) p.enqueueNote(1, 48 + i % 24, 1);
        p.processBlock(audio, midi); check(p.voiceCount.load() == 0, "Queue overflow must drain before panic, leaving no hung notes");
        TopBrassProcessor preview; preview.loadFactoryPreset(15);
        std::unique_ptr<juce::AudioProcessorEditor> editor(preview.createEditor());
        check(editor != nullptr, "Editor creation");
        editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        editor->setSize(1140, 700);
        juce::Button* disclosure = nullptr;
        int rosterCards = 0;
        for (auto* child : editor->getChildren()) {
            if (child->getProperties().contains("rosterIndex")) {
                auto* card = dynamic_cast<juce::ToggleButton*>(child);
                check(card != nullptr && card->isVisible(), "Roster players must be visible");
                const int slot = static_cast<int>(child->getProperties()["rosterIndex"]);
                card->setToggleState(true, juce::sendNotificationSync);
                check(preview.parameters.getRawParameterValue("part"+juce::String(slot)+".enabled")->load() == 1, "Recruit card enables real audio player");
                card->setToggleState(false, juce::sendNotificationSync);
                check(preview.parameters.getRawParameterValue("part"+juce::String(slot)+".enabled")->load() == 0, "Dismiss card disables real audio player");
                ++rosterCards;
            }
            if (auto* button = dynamic_cast<juce::TextButton*>(child); button && button->getButtonText() == "Show configuration") disclosure = button;
            if (child->getName().endsWith(" MIDI channel")) check(!child->isVisible(), "Routing hidden initially");
        }
        check(rosterCards == 6 && disclosure != nullptr, "Six roster cards and configuration disclosure");
        for (int slot : {0, 1, 4, 5}) set(preview, "part"+juce::String(slot)+".enabled", 1);
        if (argc > 1) {
            auto stream = freshStream(juce::File(juce::String::fromUTF8(argv[1])).getChildFile("plugin-roster.png"));
            check(juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), *stream), "Roster snapshot");
        }
        const auto still = editor->createComponentSnapshot(juce::Rectangle<int>(28, 96, editor->getWidth()-56, 234));
        waitForUI([&]{
            const auto animated=editor->createComponentSnapshot(juce::Rectangle<int>(28,96,editor->getWidth()-56,234));
            for(int y=0;y<still.getHeight();++y)for(int x=0;x<still.getWidth();++x)
                if(still.getPixelAt(x,y)!=animated.getPixelAt(x,y))return true;
            return false;
        },"Idle animation must change the rendered scene between timer frames");
        for (int slot=0; slot<6; ++slot)
            check(preview.parameters.getRawParameterValue("part"+juce::String(slot)+".enabled")->load() == ((slot==2 || slot==3)?0:1), "Visual animation must preserve player selection");
        if (argc > 1) {
            const auto frames = juce::File(juce::String::fromUTF8(argv[1])).getChildFile("scene-frames"); frames.createDirectory();
            for (int frame=0; frame<24; ++frame) {
                juce::Thread::sleep(100);
                juce::Timer::callPendingTimersSynchronously();
                auto stream = freshStream(frames.getChildFile("frame-"+juce::String(frame).paddedLeft('0',2)+".png"));
                check(juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),*stream), "Animated scene preview frame");
            }
        }
        disclosure->onClick();
        juce::ComboBox* tubaControl=nullptr;
        for(auto* child:editor->getChildren())if(child->getName()=="Tuba voice")tubaControl=dynamic_cast<juce::ComboBox*>(child);
        check(tubaControl && tubaControl->isVisible() && tubaControl->isItemEnabled(2) && tubaControl->isItemEnabled(3),"Expanded tuba voice must expose both available War styles");
        tubaControl->setSelectedId(3,juce::sendNotificationSync);
        check(preview.parameters.getRawParameterValue("part3.tubaVoice")->load()==2,"Tuba voice UI must control the playable parameter");
        int sourceControls=0;
        for(auto* child:editor->getChildren()) if(child->getName().endsWith(" tenor source")) {
            auto* combo=dynamic_cast<juce::ComboBox*>(child);
            check(combo && combo->isVisible() && combo->isEnabled(),"Both tenor source controls must be visible and enabled in expanded Tenor configuration");
            combo->setSelectedId(2,juce::sendNotificationSync);++sourceControls;
        }
        check(sourceControls==2 && preview.parameters.getRawParameterValue("part0.tenorSource")->load()==1
              && preview.parameters.getRawParameterValue("part4.tenorSource")->load()==1,"Each source control must change its own playable sax parameter");
        const auto sourceControlsEnabled=[&](bool enabled) {
            for(auto* child:editor->getChildren()) if(child->getName().endsWith(" tenor source") && child->isEnabled()!=enabled) return false;
            return true;
        };
        set(preview,"saxVoice",1);
        waitForUI([&]{return sourceControlsEnabled(false);},"Baritone styles must disable the inactive tenor source controls");
        check(preview.parameters.getRawParameterValue("part0.tenorSource")->load()==1
              && preview.parameters.getRawParameterValue("part4.tenorSource")->load()==1,"Baritone styles must retain stored tenor source choices");
        set(preview,"saxVoice",0);
        waitForUI([&]{return sourceControlsEnabled(true);},"Returning to Tenor must enable both source controls");
        const auto* display=juce::Desktop::getInstance().getDisplays().getDisplayForRect(editor->getScreenBounds());
        check(display!=nullptr && editor->getHeight()<=display->userBounds.getHeight()-32,"Expanded configuration must fit the display work area with window chrome");
        const int expandedHeight=editor->getHeight();
        for(int height:{editor->getConstrainer()->getMinimumHeight(),expandedHeight}) {
            editor->setSize(1140,height);
            juce::Component* keys=nullptr;
            for(auto* child:editor->getChildren()) if(dynamic_cast<juce::MidiKeyboardComponent*>(child)) keys=child;
            check(keys!=nullptr,"Keyboard must remain available");
            for(auto* child:editor->getChildren()) if(child->isVisible()) {
                check(editor->getLocalBounds().contains(child->getBounds()),"Every expanded control must fit inside the editor");
                if(child!=keys)check(!child->getBounds().intersects(keys->getBounds()),"Configuration controls must not overlap the keyboard");
            }
        }
        editor->setSize(1140,expandedHeight);
        for (auto* child : editor->getChildren())
            if (child->getName().endsWith(" MIDI channel")) check(child->isVisible(), "Routing revealed on request");
        int namedTongueControls = 0;
        for (auto* child : editor->getChildren())
            if (child->getName().endsWith("double tongue")) {
                auto* handler = child->getAccessibilityHandler();
                check(handler != nullptr && handler->getTitle() == child->getName(),
                      "Brass tongue switches must expose distinct instrument names to accessibility clients");
                ++namedTongueControls;
            }
        check(namedTongueControls == 3, "All brass tongue switches must be accessible");
        editor->setSize(1140, expandedHeight);
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        if (argc > 1) {
            juce::File output(juce::String::fromUTF8(argv[1])); output.createDirectory();
            auto stream = freshStream(output.getChildFile("plugin-ui.png"));
            check(juce::PNGImageFormat().writeImageToStream(image, *stream), "UI snapshot write");
            const auto modes=output.getChildFile("playing-modes");modes.createDirectory();
            sectionPerformance(0,512,modes.getChildFile("layered.wav"));sectionPerformance(1,512,modes.getChildFile("section-relay.wav"));sectionPerformance(2,128,modes.getChildFile("section-groove.wav"));
            sectionPerformance(2,128,modes.getChildFile("section-soft.wav"),.45f,true);sectionPerformance(2,128,modes.getChildFile("section-strong.wav"),.95f,true);
            const auto demos = output.getChildFile("preset-demos"); demos.createDirectory();
            for (int i = 0; i < TopBrassProcessor::factoryPresetNames().size(); ++i) renderPreset(p, i, demos);
            if (argc > 2) {
                const juce::File scores(juce::String::fromUTF8(argv[2]));
                const auto performances = output.getChildFile("section-performances"); performances.createDirectory();
                for (const auto& name : {"funk-four-parts", "soul-four-parts"})
                    for (int solo = -1; solo < 4; ++solo)
                        renderScore(scores.getChildFile(juce::String(name) + ".mid"), juce::String(name).startsWith("soul") ? 1 : 0, solo, performances);
                for (int solo : {1,2}) renderScore(scores.getChildFile("fall-audition.mid"),2,solo,performances);
            }
        }
        p.releaseResources();
        std::cout << "PASS: bundled bank, MIDI sample offsets, state roundtrip, invalid-state preservation, panic, keyboard FIFO, queue overflow, editor snapshot\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

#include "PluginEditor.h"
#include <BinaryData.h>
#include <cmath>
namespace {
const juce::Colour background { 0xff100f0d }, panel { 0xff211e18 }, brass { 0xffcda864 }, ink { 0xffedf0ec }, muted { 0xffa39780 };
constexpr std::array<const char*, 6> names { "Saxophone", "Trumpet", "Trombone", "Tuba", "Saxophone II", "Trumpet II" };
constexpr std::array<const char*, 6> ranks { "SERGEANT", "LIEUTENANT", "CAPTAIN", "MAJOR", "STAFF SERGEANT", "COLONEL" };
void label(juce::Label& item, const juce::String& text) {
    item.setText(text, juce::dontSendNotification);
    item.setColour(juce::Label::textColourId, muted);
    item.setFont(juce::FontOptions(13));
}
void slider(juce::Slider& item, const juce::String& name, const juce::String& suffix) {
    item.setName(name);
    item.setSliderStyle(juce::Slider::LinearHorizontal);
    item.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 24);
    item.setTextValueSuffix(suffix);
}
}
TopBrassLookAndFeel::TopBrassLookAndFeel() {
    setColour(juce::Slider::thumbColourId, brass);
    setColour(juce::Slider::trackColourId, brass);
    setColour(juce::Slider::backgroundColourId, background.brighter(0.15f));
    setColour(juce::Slider::textBoxTextColourId, ink);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, background);
    setColour(juce::ComboBox::backgroundColourId, background);
    setColour(juce::ComboBox::textColourId, ink);
    setColour(juce::ComboBox::outlineColourId, muted.withAlpha(0.25f));
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, ink);
    setColour(juce::TextButton::buttonColourId, panel);
    setColour(juce::TextButton::textColourOffId, ink);
}
void TopBrassLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool hovered, bool) {
    if (button.getProperties().contains("rosterIndex")) {
        const int index = static_cast<int>(button.getProperties()["rosterIndex"]);
        const auto bounds = button.getLocalBounds();
        const bool active = button.getToggleState();
        const int floor = bounds.getHeight()-76;
        const float phase = animationTime * 1.8f + static_cast<float>(index)*1.37f;
        const float sway = std::sin(phase) * (active ? 1.6f : .5f);
        if (active || hovered || button.hasKeyboardFocus(true)) {
            g.setColour(brass.withAlpha(hovered ? .24f : .12f));
            g.fillEllipse(bounds.getWidth()*.18f, static_cast<float>(floor-9), bounds.getWidth()*.64f, 10.0f);
        }
        if (roster.isValid()) {
            juce::Graphics::ScopedSaveState spriteState(g);
            g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
            const int cell = roster.getWidth() / 6;
            const int imageHeight = floor-5;
            const int imageWidth = std::min(bounds.getWidth()-8,imageHeight*cell/roster.getHeight());
            const float breath = 1.0f + std::sin(phase*.8f)*.006f;
            g.addTransform(juce::AffineTransform(1.0f, sway / static_cast<float>(floor), -sway,
                                                0.0f, breath, static_cast<float>(floor)*(1.0f-breath)));
            g.setOpacity(active ? 1.0f : (hovered ? .8f : .48f));
            g.drawImage(roster, (bounds.getWidth()-imageWidth)/2, floor-imageHeight, imageWidth, imageHeight,
                        index*cell, 0, cell, roster.getHeight());
        }
        g.setColour(active ? brass : muted);
        if (hovered || button.hasKeyboardFocus(true))
            g.drawHorizontalLine(floor+2, 14.0f, static_cast<float>(bounds.getWidth()-14));
        g.setFont(juce::FontOptions(12, juce::Font::bold));
        g.drawText(ranks[static_cast<std::size_t>(index)], 3, bounds.getHeight()-72, bounds.getWidth()-6, 22, juce::Justification::centred);
        g.setColour(ink); g.setFont(juce::FontOptions(15));
        g.drawText(names[static_cast<std::size_t>(index)], 3, bounds.getHeight()-49, bounds.getWidth()-6, 22, juce::Justification::centred);
        g.setColour(active ? brass : muted); g.setFont(juce::FontOptions(11));
        g.drawText(active ? "IN FORMATION" : "CLICK TO RECRUIT", 3, bounds.getHeight()-26, bounds.getWidth()-6, 20, juce::Justification::centred);
        return;
    }
    const bool on = button.getToggleState();
    auto bounds = button.getLocalBounds().toFloat();
    const float alpha = button.isEnabled() ? 1.0f : .35f;
    g.setColour((on ? brass : background.brighter(hovered ? 0.3f : 0.1f)).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(bounds, 6);
    g.setColour((on ? background : muted).withMultipliedAlpha(alpha));
    g.setFont(juce::FontOptions(button.getHeight()<35?13.0f:19.0f, juce::Font::bold));
    g.drawText(button.getButtonText(), bounds.reduced(12, 0), juce::Justification::centredLeft);
    g.fillEllipse(bounds.getRight() - 22, bounds.getCentreY() - 4, 8, 8);
}
TopBrassEditor::TopBrassEditor(TopBrassProcessor& p) : AudioProcessorEditor(p), instrument(p) {
    setLookAndFeel(&look);
    look.roster = juce::ImageCache::getFromMemory(BinaryData::ranksprites_png, BinaryData::ranksprites_pngSize);
    logo = juce::ImageCache::getFromMemory(BinaryData::topbrasslogo_png, BinaryData::topbrasslogo_pngSize);
    stage = juce::ImageCache::getFromMemory(BinaryData::rankstage_png, BinaryData::rankstage_pngSize);
    for (std::size_t i = 0; i < strips.size(); ++i) {
        auto& s = strips[i];
        const auto prefix = "part" + juce::String(static_cast<int>(i)) + ".";
        s.enabled.setButtonText(names[i]);
        s.enabled.getProperties().set("rosterIndex", static_cast<int>(i));
        s.enabled.setName(juce::String(ranks[i]) + ", " + names[i] + " enabled");
        s.enabled.setTooltip(juce::String(ranks[i]) + " / " + names[i] + ": recruit or dismiss this player");
        slider(s.level, juce::String(names[i]) + " level", " dB");
        slider(s.pan, juce::String(names[i]) + " pan", "");
        label(s.levelLabel, "Level"); label(s.panLabel, "Pan");
        label(s.octaveLabel, "Octave"); label(s.channelLabel, "MIDI channel");
        for (int octave = -2; octave <= 2; ++octave)
            s.octave.addItem((octave > 0 ? "+" : "") + juce::String(octave), octave + 3);
        s.channel.addItem("All channels", 1);
        for (int channel = 1; channel <= 16; ++channel) s.channel.addItem(juce::String(channel), channel + 1);
        s.octave.setName(juce::String(names[i]) + " octave");
        s.channel.setName(juce::String(names[i]) + " MIDI channel");
        s.channel.setTooltip("Used in Layered mode. Section modes route notes directly to the enabled players.");
        s.enabledAttachment = std::make_unique<ButtonAttachment>(p.parameters, prefix + "enabled", s.enabled);
        s.levelAttachment = std::make_unique<SliderAttachment>(p.parameters, prefix + "level", s.level);
        s.panAttachment = std::make_unique<SliderAttachment>(p.parameters, prefix + "pan", s.pan);
        s.octaveAttachment = std::make_unique<ComboAttachment>(p.parameters, prefix + "octave", s.octave);
        s.channelAttachment = std::make_unique<ComboAttachment>(p.parameters, prefix + "channel", s.channel);
        for (juce::Component* c : std::array<juce::Component*, 9> { &s.enabled, &s.level, &s.pan, &s.octave, &s.channel, &s.levelLabel, &s.panLabel, &s.octaveLabel, &s.channelLabel }) addAndMakeVisible(c);
        if (i == 1 || i == 2 || i == 5) {
            s.doubleTongue.setButtonText("Double tongue");
            s.doubleTongue.setName(juce::String(names[i])+" double tongue");
            s.doubleTongue.setTitle(juce::String(names[i])+" double tongue");
            s.doubleTongue.setTooltip("Recorded double attacks for Short / punch, at their original timing. This horn only. Groove gates can shorten the recorded figure; Layered and Relay keep its natural ending.");
            s.doubleTongueAttachment = std::make_unique<ButtonAttachment>(p.parameters,prefix+"doubleTongue",s.doubleTongue);
            addAndMakeVisible(s.doubleTongue);
        }
        if (topbrass::isSaxPart(static_cast<int>(i))) {
            s.tenorSource.addItemList(TopBrassProcessor::tenorSourceNames(),1);
            s.tenorSource.setName(juce::String(names[i])+" tenor source");
            s.tenorSource.setTooltip("Original uses the VCSL stereo tenor. Alternate uses MTG mono notes with authored neighboring-note variation. Held notes finish with the original recording. Short gates sustained recordings; no dedicated staccato samples. Baritone styles override this choice.");
            s.tenorSourceAttachment=std::make_unique<ComboAttachment>(p.parameters,prefix+"tenorSource",s.tenorSource);
            addAndMakeVisible(s.tenorSource);
        }
        if(i==3) {
            s.tubaVoice.addItemList(TopBrassProcessor::tubaVoiceNames(),1);
            s.tubaVoice.setName("Tuba voice");
            s.tubaVoice.setTooltip("Original retains VSCO. War uses condenser tuba recordings. Its two choices differ under Short: staccato or staccatissimo. Sustain shares authored loops and recorded file decays; Fall is synthesized. Source sustain gain is preserved, with no source EQ or LFOs. War has one recorded sustain force.");
            s.tubaVoice.setItemEnabled(2,p.warTubaAvailable());s.tubaVoice.setItemEnabled(3,p.warTubaAvailable());
            s.tubaVoiceAttachment=std::make_unique<ComboAttachment>(p.parameters,"part3.tubaVoice",s.tubaVoice);
            addAndMakeVisible(s.tubaVoice);
        }
    }
    saxVoice.addItemList(TopBrassProcessor::saxVoiceNames(), 1);
    saxVoice.setName("Sax voice");
    saxVoice.setTooltip("Tenor and baritone follow Articulation. Named baritone styles override it. Growl is a finite low-register recording.");
    saxVoiceAttachment = std::make_unique<ComboAttachment>(p.parameters, "saxVoice", saxVoice);
    addAndMakeVisible(saxVoice);
    factoryPreset.addItemList(TopBrassProcessor::factoryPresetNames(), 1);
    factoryPreset.setName("Starting point");
    factoryPreset.setTextWhenNothingSelected("Load a starting point");
    factoryPreset.onChange = [this] {
        const int index = factoryPreset.getSelectedItemIndex();
        if (index >= 0) instrument.loadFactoryPreset(index);
        factoryPreset.setSelectedId(0, juce::dontSendNotification);
    };
    slider(humanize,"Humanize", "");
    label(humanizeLabel,"Humanize");
    humanize.setTooltip("All modes: tiny independent player delays and gentle, slow level drift. Zero is exact timing and steady gain; full amount adds at most 8 ms delay and 0.6 dB level variation. Live notes are never moved before your input.");
    humanizeAttachment=std::make_unique<SliderAttachment>(p.parameters,"humanize",humanize);
    addAndMakeVisible(humanize);addAndMakeVisible(humanizeLabel);
    slider(tone, "Tone, warm to bright", "");
    label(toneLabel, "Tone: warm to bright");
    toneAttachment = std::make_unique<SliderAttachment>(p.parameters, "tone", tone);
    addAndMakeVisible(factoryPreset); addAndMakeVisible(tone); addAndMakeVisible(toneLabel);
    articulation.addItemList({ "Short / punch", "Sustain", "Fall" }, 1);
    articulation.setName("Articulation");
    articulation.setTooltip("Groove gates all articulations for separated hits. In Layered and Relay, short notes keep their recorded endings and sustain follows key release. Fall uses recorded trumpet/trombone falls and a pitch drop for saxophone/tuba.");
    slider(output, "Output level", " dB"); slider(release, "Release time", " ms"); slider(tightness, "Attack accent", "");
    release.setTooltip("Fade for baritone and forced stops. Tenor, trumpet, trombone and tuba sustains use recorded endings. Short notes finish naturally in Layered and Relay. Groove uses its own short gate fade.");
    label(articulationLabel, "Articulation"); label(outputLabel, "Output"); label(releaseLabel, "Release"); label(tightnessLabel, "Attack accent");
    articulationAttachment = std::make_unique<ComboAttachment>(p.parameters, "articulation", articulation);
    outputAttachment = std::make_unique<SliderAttachment>(p.parameters, "master", output);
    releaseAttachment = std::make_unique<SliderAttachment>(p.parameters, "release", release);
    tightnessAttachment = std::make_unique<SliderAttachment>(p.parameters, "tightness", tightness);
    slider(keyNoise, "Baritone keys", "");
    keyNoise.setTooltip("Recorded baritone key-close and key-open sounds. 1.00 uses the source mapping's level. Does not affect tenor or the brass instruments.");
    label(keyNoiseLabel, "Baritone keys");
    keyNoiseAttachment = std::make_unique<SliderAttachment>(p.parameters, "baritoneKeys", keyNoise);
    addAndMakeVisible(keyNoise); addAndMakeVisible(keyNoiseLabel);
    slider(airflow, "Baritone airflow", ""); label(airflowLabel, "Baritone airflow");
    airflow.setTooltip("Recorded air under sounding baritone notes. Follows CC2/CC11 expression. Does not play phrase inhalations or affect the other instruments.");
    airflowAttachment = std::make_unique<SliderAttachment>(p.parameters, "baritoneAir", airflow);
    addAndMakeVisible(airflow); addAndMakeVisible(airflowLabel);
    slider(tenorDetailLevel,"Tenor detail level"," dB");label(tenorDetailLabel,"Tenor detail level");
    tenorDetailLevel.setTooltip("Gain for explicitly triggered recorded tenor breath/key sounds. 0 dB retains source level before sax level/pan and output. Recordings are quiet; raise this to audition them. No automatic breathing is added.");
    tenorBreath.setTooltip("Play the next recorded MTG tenor breath sound at its original pitch and duration. Explicit one-shot, not automatic phrase breathing.");
    tenorKeys.setTooltip("Play the next recorded tenor key sound at original pitch and duration. Explicit one-shot, separate from baritone fingering noises.");
    tenorBreath.onClick=[this]{instrument.enqueueTenorDetail(topbrass::TenorDetail::breath);};
    tenorKeys.onClick=[this]{instrument.enqueueTenorDetail(topbrass::TenorDetail::keys);};
    tenorMidi.setButtonText("MIDI CC20 / 21");tenorMidi.setName("Tenor detail MIDI");
    tenorMidi.setTooltip("Enable explicit tenor detail triggers: CC20 breath, CC21 keys. Send 0 then a nonzero value per trigger. Follows the sax channel filter in Layered mode. Disabled by default.");
    tenorMidiAttachment=std::make_unique<ButtonAttachment>(p.parameters,"tenorDetailMidi",tenorMidi);addAndMakeVisible(tenorMidi);
    tenorDetailAttachment=std::make_unique<SliderAttachment>(p.parameters,"tenorDetailDb",tenorDetailLevel);
    for(juce::Component* c:std::array<juce::Component*,4>{&tenorBreath,&tenorKeys,&tenorDetailLabel,&tenorDetailLevel})addAndMakeVisible(c);
    playingMode.addItemList({"Layered","Section relay","Section groove"},1);playingMode.setName("Playing mode");
    playingMode.setTooltip("Layered plays enabled instruments together. Relay passes your notes between players. Groove plays tight syncopated hits with all enabled horns while keys are held.");
    playingModeAttachment=std::make_unique<ComboAttachment>(p.parameters,"playingMode",playingMode);
    for(int note=36;note<=96;++note)lowestKey.addItem("MIDI "+juce::String(note),note-35);
    lowestKey.setName("Lowest playing key");lowestKeyAttachment=std::make_unique<ComboAttachment>(p.parameters,"lowestKey",lowestKey);
    slider(hornFeel,"Section accents","");slider(tempo,"Standalone tempo"," BPM");
    hornFeel.setTooltip("Section modes: shared accents and gate-length contours. Humanize controls player timing and slow level drift in every mode.");
    tempo.setTooltip("Uses the host tempo when available; this supplies tempo in the standalone or without host tempo.");
    feelAttachment=std::make_unique<SliderAttachment>(p.parameters,"hornFeel",hornFeel);tempoAttachment=std::make_unique<SliderAttachment>(p.parameters,"freeTempo",tempo);
    label(playingModeLabel,"Playing mode");label(lowestKeyLabel,"15-key window");label(feelLabel,"Section accents");label(tempoLabel,"Tempo");
    for(juce::Component* c:std::array<juce::Component*,8>{&playingMode,&lowestKey,&hornFeel,&tempo,&playingModeLabel,&lowestKeyLabel,&feelLabel,&tempoLabel})addAndMakeVisible(c);
    panicButton.onClick = [this] { instrument.panic(); keyboardState.allNotesOff(0); };
    for (juce::Component* c : std::array<juce::Component*, 10> { &articulation, &output, &release, &tightness, &articulationLabel, &outputLabel, &releaseLabel, &tightnessLabel, &panicButton, &keyboard }) addAndMakeVisible(c);
    keyboardState.addListener(this);
    keyboard.setAvailableRange(24, 96);
    keyboard.setLowestVisibleKey(48);
    keyboard.setKeyWidth(19);
    keyboard.setVelocity(0.85f, true);
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, brass.withAlpha(0.7f));
    setResizable(true, true);
    detailsButton.onClick = [this] {
        showDetails = !showDetails;
        detailsButton.setButtonText(showDetails ? "Hide configuration" : "Show configuration");
        setResizeLimits(1080, showDetails ? 830 : 700, 1500, 1250);
        const auto* display=juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());
        const int available=display?static_cast<int>(display->userBounds.getHeight())-60:900;
        setSize(getWidth(), showDetails ? std::clamp(available,830,900) : 700);
        if(auto* window=dynamic_cast<juce::ResizableWindow*>(getTopLevelComponent()); window&&display)
            window->setTopLeftPosition(window->getBounds().constrainedWithin(display->userBounds.toNearestInt()).getPosition());
        resized();
    };
    addAndMakeVisible(detailsButton);
    setResizeLimits(1080, 700, 1500, 1250);
    setSize(1140, 700);
    timerCallback();
    startTimerHz(15);
}
TopBrassEditor::~TopBrassEditor() {
    keyboardState.allNotesOff(0);
    keyboardState.removeListener(this);
    setLookAndFeel(nullptr);
}
void TopBrassEditor::handleNoteOn(juce::MidiKeyboardState*, int channel, int note, float velocity) { instrument.enqueueNote(channel, note, velocity); }
void TopBrassEditor::handleNoteOff(juce::MidiKeyboardState*, int channel, int note, float) { instrument.enqueueNote(channel, note, 0); }
void TopBrassEditor::updatePlayingKeys() {
    const bool section=instrument.parameters.getRawParameterValue("playingMode")->load()>.5f;
    const int bottom=section?static_cast<int>(instrument.parameters.getRawParameterValue("lowestKey")->load()):24;
    if(section!=sectionKeyboard||bottom!=keyboardBottom){
        keyboard.setAvailableRange(bottom,section?bottom+topbrass::sectionKeyCount-1:96);keyboard.setLowestVisibleKey(section?bottom:48);
        if(!section)keyboard.setKeyWidth(19);sectionKeyboard=section;keyboardBottom=bottom;
    }
    if(section&&keyboard.getWidth()>0&&keyboard.getTotalKeyboardWidth()>0)
        keyboard.setKeyWidth(keyboard.getKeyWidth()*static_cast<float>(keyboard.getWidth())/keyboard.getTotalKeyboardWidth());
}
void TopBrassEditor::timerCallback() {
    look.animationTime = static_cast<float>(std::fmod(juce::Time::getMillisecondCounterHiRes() / 1000.0, 3600.0));
    keyNoise.setEnabled(instrument.parameters.getRawParameterValue("saxVoice")->load() > 0.5f
        && instrument.parameters.getRawParameterValue("part0.enabled")->load() > 0.5f);
    airflow.setEnabled(keyNoise.isEnabled());
    const bool tenor=instrument.parameters.getRawParameterValue("saxVoice")->load()<.5f && instrument.parameters.getRawParameterValue("part0.enabled")->load()>.5f;
    tenorBreath.setEnabled(tenor);tenorKeys.setEnabled(tenor);tenorDetailLevel.setEnabled(tenor);tenorMidi.setEnabled(tenor);
    const bool section=instrument.parameters.getRawParameterValue("playingMode")->load()>.5f;
    const int bottom=static_cast<int>(instrument.parameters.getRawParameterValue("lowestKey")->load());
    release.setEnabled(instrument.parameters.getRawParameterValue("playingMode")->load()<1.5f);
    lowestKey.setEnabled(section);hornFeel.setEnabled(section);tempo.setEnabled(section&&!instrument.usingHostTempo.load());
    tempoLabel.setText(section&&instrument.usingHostTempo.load()?"Host "+juce::String(instrument.performanceBpm.load(),0)+" BPM":"Tempo",juce::dontSendNotification);
    lowestKeyLabel.setText(section?"MIDI "+juce::String(bottom)+"–"+juce::String(bottom+topbrass::sectionKeyCount-1):"15-key window",juce::dontSendNotification);
    for(auto& strip:strips)strip.channel.setEnabled(!section);
    for(std::size_t i : {1u,2u,5u})strips[i].doubleTongue.setEnabled(instrument.parameters.getRawParameterValue("articulation")->load()<.5f);
    for(std::size_t i : {0u,4u})strips[i].tenorSource.setEnabled(instrument.parameters.getRawParameterValue("saxVoice")->load()<.5f);
    updatePlayingKeys();
    status = instrument.bankError().empty()
        ? juce::String(instrument.voiceCount.load()) + " sample voices  /  " + juce::String(static_cast<double>(instrument.bankBytes()) / 1048576.0, 0) + " MB samples"
        : juce::String(instrument.bankError());
    if(instrument.parameters.getRawParameterValue("part3.tubaVoice")->load()>.5f && !instrument.warTubaAvailable())
        status="Selected War Tuba recordings are unavailable in this bank";
    repaint();
}
int TopBrassEditor::rosterHeight() const noexcept {
    return showDetails?std::clamp(getHeight()-710,120,190):310;
}
void TopBrassEditor::paint(juce::Graphics& g) {
    g.fillAll(background);
    const int sceneHeight = rosterHeight()-64;
    if (stage.isValid()) {
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.drawImage(stage, 28, 90, getWidth()-56, sceneHeight, 0, 0, stage.getWidth(), stage.getHeight()*9/10);
    }
    for (int side : {0,1}) {
        const float x = side == 0 ? 52.0f : static_cast<float>(getWidth()-54);
        for (int ember=0; ember<7; ++ember) {
            const float life = std::fmod(look.animationTime*.35f+static_cast<float>(ember)*.143f,1.0f);
            g.setColour(brass.withAlpha((1.0f-life)*.5f));
            const float drift=std::sin(look.animationTime+static_cast<float>(ember))*5.0f;
            g.fillRect(static_cast<int>(x+drift),static_cast<int>(90+sceneHeight*.74f-life*sceneHeight*.35f),2,2);
        }
    }
    g.setColour(brass); g.setFont(juce::FontOptions(32, juce::Font::bold));
    g.drawImageWithin(logo, 28, 12, 48, 48, juce::RectanglePlacement::centred);
    g.drawText("T O P B R A S S", 88, 16, 400, 44, juce::Justification::centredLeft);
    g.setColour(muted); g.setFont(juce::FontOptions(12));
    g.drawText("ASSEMBLE YOUR SECTION  /  CLICK A PLAYER TO TOGGLE", 30, 61, 650, 22, juce::Justification::centredLeft);
    g.setColour(panel); g.fillRect(getWidth()-235, 31, 205, 5);
    g.setColour(brass); g.fillRect(getWidth()-235, 31, static_cast<int>(205*std::clamp(instrument.peak.load(),0.0f,1.0f)), 5);
    g.setColour(muted); g.setFont(juce::FontOptions(11));
    g.drawFittedText(status, 28, getHeight()-24, getWidth()-56, 20, juce::Justification::centredLeft, 1);
    if (showDetails) {
        g.setColour(panel); g.fillRect(24, 497-(310-rosterHeight()), getWidth()-48, 376);
        g.setColour(brass); g.setFont(juce::FontOptions(12));
        g.drawText("PLAYER MIX & ROUTING", 36, 502-(310-rosterHeight()), 300, 22, juce::Justification::centredLeft);
    }
}
void TopBrassEditor::resized() {
    humanizeLabel.setBounds(getWidth()-530,15,265,20);
    humanize.setBounds(getWidth()-530,38,265,28);
    const int shift=310-rosterHeight();
    const int width = (getWidth()-60)/6;
    for (std::size_t i=0; i<strips.size(); ++i) {
        auto& s=strips[i];
        const int x=28+static_cast<int>(i)*width, w=width-8;
        s.enabled.setBounds(x, 96, width, rosterHeight());
        for (juce::Component* c : std::array<juce::Component*,8>{&s.level,&s.pan,&s.octave,&s.channel,&s.levelLabel,&s.panLabel,&s.octaveLabel,&s.channelLabel}) c->setVisible(showDetails);
        s.doubleTongue.setVisible(showDetails && (i==1 || i==2 || i==5));
        s.tenorSource.setVisible(showDetails && topbrass::isSaxPart(static_cast<int>(i)));
        s.tubaVoice.setVisible(showDetails && i==3);
        s.levelLabel.setBounds(x+4,530-shift,w-8,18); s.level.setBounds(x+4,550-shift,w-8,26);
        s.panLabel.setBounds(x+4,578-shift,w-8,18); s.pan.setBounds(x+4,598-shift,w-8,26);
        s.octaveLabel.setBounds(x+4,626-shift,w-8,18); s.octave.setBounds(x+4,646-shift,w-8,26);
        s.channelLabel.setBounds(x+4,674-shift,w-8,18); s.channel.setBounds(x+4,694-shift,w-8,26);
        s.doubleTongue.setBounds(x+4,728-shift,w-8,26);
        s.tenorSource.setBounds(x+4,728-shift,w-8,26);
        s.tubaVoice.setBounds(x+4,728-shift,w-8,26);
    }
    factoryPreset.setBounds(28,426-shift,200,30);
    playingModeLabel.setBounds(244,411-shift,170,16); playingMode.setBounds(244,431-shift,170,28);
    articulationLabel.setBounds(430,411-shift,155,16); articulation.setBounds(430,431-shift,155,28);
    outputLabel.setBounds(605,411-shift,195,16); output.setBounds(605,431-shift,195,28);
    detailsButton.setBounds(getWidth()-218,431-shift,190,28);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&saxVoice,&tone,&toneLabel,&release,&releaseLabel,&tightness,&tightnessLabel,&keyNoise,&keyNoiseLabel,&airflow,&airflowLabel,&tenorDetailLevel,&tenorDetailLabel,&tenorBreath,&tenorKeys,&tenorMidi,&lowestKey,&lowestKeyLabel,&hornFeel,&feelLabel,&tempo,&tempoLabel}) c->setVisible(showDetails);
    const int col=(getWidth()-76)/4;
    saxVoice.setBounds(36,765-shift,col,28);
    toneLabel.setBounds(48+col,749-shift,col,18); tone.setBounds(48+col,769-shift,col,26);
    tightnessLabel.setBounds(60+2*col,749-shift,col,18); tightness.setBounds(60+2*col,769-shift,col,26);
    releaseLabel.setBounds(72+3*col,749-shift,col,18); release.setBounds(72+3*col,769-shift,col,26);
    keyNoiseLabel.setBounds(36,801-shift,col,18); keyNoise.setBounds(36,821-shift,col,26);
    airflowLabel.setBounds(48+col,801-shift,col,18); airflow.setBounds(48+col,821-shift,col,26);
    lowestKeyLabel.setBounds(60+2*col,801-shift,col,18); lowestKey.setBounds(60+2*col,821-shift,col,26);
    feelLabel.setBounds(72+3*col,801-shift,col,18); hornFeel.setBounds(72+3*col,821-shift,col,26);
    tenorBreath.setBounds(28,885-shift,120,28); tenorKeys.setBounds(158,885-shift,120,28); tenorMidi.setBounds(288,885-shift,155,28);
    tenorDetailLabel.setBounds(460,868-shift,220,18); tenorDetailLevel.setBounds(460,888-shift,220,26);
    tempoLabel.setBounds(700,868-shift,170,18); tempo.setBounds(700,888-shift,170,26);
    panicButton.setBounds(getWidth()-163,getHeight()-138,135,26);
    keyboard.setBounds(28,getHeight()-101,getWidth()-56,70);
    updatePlayingKeys();
}

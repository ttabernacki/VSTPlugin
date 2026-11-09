#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
const int kVolumeCcs[] = { 11, 7, 4, 2, -1 };
}

juce::StringArray SteelwaveProcessor::setNames()
{
    juce::StringArray s;
    for (int i = 0; i < sw::kSets; ++i)
        s.add ((i < sw::kChordSets ? "Chords " + juce::String (i + 1) : "Single " + juce::String (i - sw::kChordSets + 1)) + ": "
               + sw::setName (i));
    return s;
}

juce::StringArray SteelwaveProcessor::keyNames()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

APVTS::ParameterLayout SteelwaveProcessor::createLayout()
{
    using namespace juce;
    using Range = NormalisableRange<float>;
    using Attr = AudioParameterFloatAttributes;
    APVTS::ParameterLayout l;
    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };
    auto pctIn = [] (const String& t) { return t.getFloatValue() / 100.0f; };
    auto num = [] (const String& t) { return t.getFloatValue(); };
    auto msIn = [] (const String& t) { return t.getFloatValue() / 1000.0f; };

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "set", 1 }, "Set", setNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", keyNames(), 4));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 }, "Glide", Range (0.03f, 1.2f, 0.0f, 0.5f), 0.25f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 1000.0f)) + " ms"; })
                                                      .withValueFromStringFunction (msIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "strum", 1 }, "Strum", Range (0.0f, 60.0f), 12.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })
                                                      .withValueFromStringFunction (num)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "hardness", 1 }, "Pick", Range (0.0f, 1.0f), 0.75f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "brightness", 1 }, "Brightness", Range (0.0f, 1.0f), 0.5f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "sustain", 1 }, "Sustain", Range (0.25f, 2.0f, 0.0f, 0.6f), 1.0f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "barnoise", 1 }, "Bar noise", Range (0.0f, 1.0f), 0.5f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibrate", 1 }, "Vibrato rate", Range (3.0f, 8.0f), 5.5f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " Hz"; })
                                                      .withValueFromStringFunction (num)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibdepth", 1 }, "Vibrato depth", Range (0.0f, 0.5f), 0.15f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " ct"; })
                                                      .withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "sympathetic", 1 }, "Sympathetic", Range (0.0f, 1.0f), 0.5f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "behindbar", 1 }, "Behind bar", Range (0.0f, 1.0f), 0.5f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pickup", 1 }, "Pickup", StringArray { "Single coil", "Humbucker" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "volcc", 1 }, "Volume pedal",
                                                   StringArray { "CC11 Expression", "CC7 Volume", "CC4 Foot", "CC2 Breath", "Off" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "voltype", 1 }, "Pedal type",
                                                   StringArray { "Passive 250k", "Passive 500k", "Active (no tone loss)" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "level", 1 }, "Level", Range (-24.0f, 12.0f), 0.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (v > 0 ? "+" : "") + String (v, 1) + " dB"; })
                                                      .withValueFromStringFunction (num)));
    // the Controls page: how the instrument is played
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pedalkeys", 1 }, "Pedal keys",
                                                   StringArray { "Off", "On (MIDI 36-42)" }, 1));
    {
        Range r (0.3f, 3.0f);
        r.setSkewForCentre (1.0f);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "pedalspeed", 1 }, "Pedal speed", r, 1.0f,
                                                      Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + "x"; })
                                                          .withValueFromStringFunction (num)));
    }
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "modwheel", 1 }, "Mod wheel",
                                                   StringArray { "Vibrato", "A+B pedals", "Bar slide (2 frets)", "Off" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "aftertouch", 1 }, "Aftertouch", StringArray { "Vibrato", "Off" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "swellbelow", 1 }, "Auto swell", Range (0.0f, 1.0f), 0.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) {
                                                      return v <= 0.0f ? String ("Off") : "vel < " + String (roundToInt (v * 127.0f)); })
                                                      .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue() / 127.0f; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "swelltime", 1 }, "Swell time", Range (0.1f, 3.0f, 0.0f, 0.5f), 0.6f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " s"; })
                                                      .withValueFromStringFunction (num)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "autovib", 1 }, "Auto vibrato", Range (0.0f, 1.0f), 0.0f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibdelay", 1 }, "Vibrato delay", Range (0.0f, 2.0f), 0.5f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " s"; })
                                                      .withValueFromStringFunction (num)));

    // slides, timing, MPE
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "slidein", 1 }, "Slide-in", Range (0.0f, 1.0f), 0.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) {
                                                      return v <= 0.0f ? String ("Off") : v >= 1.0f ? String ("Always") : "vel < " + String (roundToInt (v * 127.0f)); })
                                                      .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue() / 127.0f; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "slidefrom", 1 }, "Slide from", Range (0.5f, 3.0f), 1.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " st"; })
                                                      .withValueFromStringFunction (num)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "slidetime", 1 }, "Slide time", Range (0.03f, 0.6f, 0.0f, 0.6f), 0.15f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 1000.0f)) + " ms"; })
                                                      .withValueFromStringFunction (msIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "glidefeel", 1 }, "Glide feel", Range (0.0f, 1.0f), 1.0f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "barrange", 1 }, "Bar range", Range (1.0f, 12.0f, 1.0f), 2.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " frets"; })
                                                      .withValueFromStringFunction (num)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "timing", 1 }, "Timing", StringArray { "Natural", "Tight (live)" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mpe", 1 }, "MPE", StringArray { "Off", "On (channels 2-16)" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "bendrange", 1 }, "MPE bend range", StringArray { "12 st", "24 st", "48 st" }, 2));

    // the Advanced page: the model's calibration. Each knob's default sits at 12 o'clock.
    for (const auto& a : swadv::params())
    {
        Range r (a.min, a.max);
        const float mid = 0.5f * (a.min + a.max);
        if (std::abs (a.def - mid) > 1e-4f * (a.max - a.min))
            r.setSkewForCentre (a.def);
        const auto* ap = &a;
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { a.id, 1 }, a.name, r, a.def,
                                                      Attr().withStringFromValueFunction ([ap] (float v, int) { return swadv::format (*ap, v); })
                                                          .withValueFromStringFunction (num)));
    }
    return l;
}

SteelwaveProcessor::SteelwaveProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pSet = apvts.getRawParameterValue ("set");
    pKey = apvts.getRawParameterValue ("key");
    pGlide = apvts.getRawParameterValue ("glide");
    pStrum = apvts.getRawParameterValue ("strum");
    pHard = apvts.getRawParameterValue ("hardness");
    pBright = apvts.getRawParameterValue ("brightness");
    pSustain = apvts.getRawParameterValue ("sustain");
    pNoise = apvts.getRawParameterValue ("barnoise");
    pVibRate = apvts.getRawParameterValue ("vibrate");
    pVibDepth = apvts.getRawParameterValue ("vibdepth");
    pSym = apvts.getRawParameterValue ("sympathetic");
    pPickup = apvts.getRawParameterValue ("pickup");
    pVolCc = apvts.getRawParameterValue ("volcc");
    pLevel = apvts.getRawParameterValue ("level");
    pBehind = apvts.getRawParameterValue ("behindbar");
    pVolType = apvts.getRawParameterValue ("voltype");
    pPedalKeys = apvts.getRawParameterValue ("pedalkeys");
    pPedalSpeed = apvts.getRawParameterValue ("pedalspeed");
    pModWheel = apvts.getRawParameterValue ("modwheel");
    pAftertouch = apvts.getRawParameterValue ("aftertouch");
    pSwellBelow = apvts.getRawParameterValue ("swellbelow");
    pSwellTime = apvts.getRawParameterValue ("swelltime");
    pAutoVib = apvts.getRawParameterValue ("autovib");
    pVibDelay = apvts.getRawParameterValue ("vibdelay");
    for (auto& t : uiTravel)
        t = 0.0f;
    clearMidiMap();
    for (const auto& a : swadv::params())
        pAdv.push_back (apvts.getRawParameterValue (a.id));
    for (auto& s : uiSemis)
        s = 0.0f;
}

bool SteelwaveProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet().isDisabled()
           && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

sw::EngineParams SteelwaveProcessor::readParams() const
{
    sw::EngineParams p;
    p.keyPc = juce::jlimit (0, 11, (int) pKey->load());
    p.glide = pGlide->load();
    p.strumMs = pStrum->load();
    p.hardness = pHard->load();
    p.brightness = pBright->load();
    p.sustain = pSustain->load();
    p.barNoise = pNoise->load();
    p.vibRate = pVibRate->load();
    p.vibDepth = pVibDepth->load();
    p.sympathetic = pSym->load();
    p.pickup = (int) pPickup->load() == 0 ? sw::Pickup::SingleCoil : sw::Pickup::Humbucker;
    p.volumeCc = kVolumeCcs[juce::jlimit (0, 4, (int) pVolCc->load())];
    p.levelDb = pLevel->load();
    p.behindBar = pBehind->load();
    p.volumePedal = (sw::EngineParams::VolumePedal) juce::jlimit (0, 2, (int) pVolType->load());
    p.pedalKeys = (int) pPedalKeys->load() == 1;
    p.pedalSpeed = pPedalSpeed->load();
    p.modWheel = (sw::EngineParams::ModWheel) juce::jlimit (0, 3, (int) pModWheel->load());
    p.aftertouchVibrato = (int) pAftertouch->load() == 0;
    p.swellBelow = pSwellBelow->load();
    p.swellTime = pSwellTime->load();
    p.autoVibrato = pAutoVib->load();
    p.vibDelay = pVibDelay->load();
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    p.slideInBelow = raw ("slidein");
    p.slideFrom = raw ("slidefrom");
    p.slideTime = raw ("slidetime");
    p.glideScaling = raw ("glidefeel");
    p.barRange = raw ("barrange");
    p.tight = (int) raw ("timing") == 1;
    p.mpe = (int) raw ("mpe") == 1;
    p.mpeBendRange = 12.0 * std::exp2 (juce::jlimit (0, 2, (int) raw ("bendrange")));
    const auto& adv = swadv::params();
    for (size_t i = 0; i < adv.size(); ++i)
        p.*(adv[i].field) = pAdv[i]->load();
    return p;
}

void SteelwaveProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    engine.prepare (sampleRate);
    engine.setParams (readParams());
    lastSetParam_ = (int) pSet->load();
    engine.setSet (lastSetParam_);
    prepared_ = true;
}

int SteelwaveProcessor::paramIndex (const juce::String& id) const
{
    const auto& ps = getParameters();
    for (int i = 0; i < ps.size(); ++i)
        if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (ps[i]))
            if (w->paramID == id)
                return i;
    return -1;
}

void SteelwaveProcessor::handle (const juce::MidiMessage& m)
{
    const int ch = m.getChannel();
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity(), ch);
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber(), ch);
    else if (m.isPitchWheel())
        engine.pitchWheel ((m.getPitchWheelValue() - 8192) / 8191.0, ch);
    else if (m.isChannelPressure())
        engine.aftertouch (m.getChannelPressureValue() / 127.0, ch);
    else if (m.isAftertouch()) // polyphonic pressure: the bar is one bar, so the latest key's pressure
        engine.aftertouch (m.getAfterTouchValue() / 127.0, ch);
    else if (m.isProgramChange())
        pendingProgram = m.getProgramChangeNumber();
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        const bool mpeMember = (int) apvts.getRawParameterValue ("mpe")->load() == 1 && ch >= 2;
        const bool learnable = cc != 0 && cc != 32 && cc < 120 && ! mpeMember;
        // MIDI learn: the next CC moved is assigned to the waiting control
        if (learnable && learnTarget.load() >= 0)
        {
            const int target = learnTarget.exchange (-1);
            for (auto& c : ccMap) // a control answers one CC
                if (c.load() == target)
                    c = -1;
            ccMap[(size_t) cc] = target;
        }
        if (learnable)
            if (const int pi = ccMap[(size_t) cc].load(); pi >= 0 && pi < getParameters().size())
            {
                getParameters()[pi]->setValueNotifyingHost (m.getControllerValue() / 127.0f);
                return; // a learned CC drives its control, not its usual job
            }
        engine.controller (cc, m.getControllerValue() / 127.0, ch);
    }
}

void SteelwaveProcessor::applyPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) swpresets::presets().size()))
        return;
    const auto& pr = swpresets::presets()[(size_t) index];
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (swpresets::isRig (r->paramID))
                continue;
            float v = r->getDefaultValue();
            for (const auto& [id, plain] : pr.values)
                if (r->paramID == id)
                    v = r->convertTo0to1 (plain);
            r->beginChangeGesture();
            r->setValueNotifyingHost (v);
            r->endChangeGesture();
        }
    currentProgram = index;
}

void SteelwaveProcessor::setCurrentProgram (int index)
{
    applyPreset (index);
}

void SteelwaveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (! prepared_ || n < 1)
    {
        buffer.clear();
        return;
    }
    engine.setParams (readParams());
    // the Set parameter moved (UI, automation, preset): follow it. Keyswitches go the other way, below.
    const int setParam = (int) pSet->load();
    if (setParam != lastSetParam_)
    {
        engine.setSet (setParam);
        lastSetParam_ = setParam;
    }

    // rendered straight into the first channel (mono instrument), then copied. With no output
    // channels the engine still runs, so notes and time stay in step.
    float* out = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0) : nullptr;
    auto render = [&] (int from, int to) {
        if (out != nullptr)
            return engine.process (out + from, to - from);
        float tmp[256];
        for (int i = from; i < to; i += 256)
            engine.process (tmp, std::min (256, to - i));
    };
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos)
        {
            render (pos, at);
            pos = at;
        }
        handle (meta.getMessage());
    }
    if (pos < n)
        render (pos, n);
    for (int c = 1; c < buffer.getNumChannels(); ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, n);

    // a program change arrived: switch preset (after this block, so the block plays out as it was)
    if (const int prog = pendingProgram.exchange (-1); prog >= 0)
        applyPreset (prog);

    // a keyswitch changed the set: show it on the Set parameter too
    if (engine.currentSet() != lastSetParam_)
    {
        lastSetParam_ = engine.currentSet();
        if (auto* p = apvts.getParameter ("set"))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) lastSetParam_));
    }

    uiSet = engine.currentSet();
    uiSlot = engine.chordSlot();
    uiBar = engine.bar();
    uiWheel = (float) engine.wheel();
    uiVolume = (float) engine.volume();
    // lit on screen: within 30 dB of the loudest string (not the faint sympathetic halo)
    double loudest = 1e-8;
    for (int s = 0; s < sw::kStrings; ++s)
        loudest = std::max (loudest, engine.stringLevel (s));
    int bits = 0;
    for (int s = 0; s < sw::kStrings; ++s)
    {
        if (engine.stringLevel (s) > loudest * 1e-3)
            bits |= 1 << s;
        uiSemis[(size_t) s] = (float) engine.stringSemis (s);
    }
    uiSounding = bits;
    for (int k = 0; k < sw::kControls; ++k)
        uiTravel[(size_t) k] = (float) engine.controlTravel (k);
}

// Bypassed, an instrument is silent; notes still in the engine are let go so nothing is stuck on return.
void SteelwaveProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();
    if (prepared_)
        engine.allNotesOff();
}

juce::AudioProcessorEditor* SteelwaveProcessor::createEditor()
{
    return new SteelwaveEditor (*this);
}

void SteelwaveProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    // MIDI learn assignments, by parameter ID (so they survive new parameters being added)
    juce::ValueTree map ("MIDIMAP");
    const auto& ps = getParameters();
    for (int cc = 0; cc < 128; ++cc)
        if (const int pi = ccMap[(size_t) cc].load(); pi >= 0 && pi < ps.size())
            if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (ps[pi]))
                map.setProperty ("cc" + juce::String (cc), w->paramID, nullptr);
    state.removeChild (state.getChildWithName ("MIDIMAP"), nullptr);
    state.appendChild (map, nullptr);
    state.setProperty ("program", currentProgram.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void SteelwaveProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            clearMidiMap();
            const auto map = state.getChildWithName ("MIDIMAP");
            for (int cc = 0; cc < 128; ++cc)
                if (map.hasProperty ("cc" + juce::String (cc)))
                    ccMap[(size_t) cc] = paramIndex (map["cc" + juce::String (cc)].toString());
            currentProgram = (int) state.getProperty ("program", 0);
            state.removeChild (map, nullptr);
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SteelwaveProcessor();
}

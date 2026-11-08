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

void SteelwaveProcessor::handle (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())
        engine.pitchWheel ((m.getPitchWheelValue() - 8192) / 8191.0);
    else if (m.isController())
        engine.controller (m.getControllerNumber(), m.getControllerValue() / 127.0);
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        engine.allNotesOff();
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
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void SteelwaveProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SteelwaveProcessor();
}

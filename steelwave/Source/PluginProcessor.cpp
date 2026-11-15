#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
const int kVolumeCcs[] = { 11, 7, 4, 2, -1 };
}

juce::StringArray SteelwaveProcessor::voicingNames()
{
    juce::StringArray s;
    for (int i = 0; i < sw::kChordSets; ++i)
        s.add (sw::setName (i));
    return s;
}
juce::StringArray SteelwaveProcessor::scaleNames()
{
    juce::StringArray s;
    for (int i = sw::kChordSets; i < sw::kSmartSet; ++i)
        s.add (sw::setName (i));
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

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", modeNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "voicing", 1 }, "Chord voicing", voicingNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Single-note set", scaleNames(), 0));
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

    // smart notes and tuning
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "autobend", 1 }, "Auto bend", StringArray { "Off", "On" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "legatoslide", 1 }, "Legato slide", StringArray { "Off", "On" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "wheel", 1 }, "Pitch wheel", StringArray { "Melody note", "Chord move" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chordmove", 1 }, "Chord changes",
                                                    StringArray { "Re-pick", "Move when legato", "Always move" }, 2));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "sweeten", 1 }, "Sweeten", Range (0.0f, 1.0f), 0.6f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));

    // picking patterns, tempo, touch
    {
        StringArray pats, rates;
        for (int i = 0; i < sw::kPatterns; ++i)
            pats.add (sw::patternName (i));
        for (int i = 0; i < sw::kPatternRates; ++i)
            rates.add (sw::patternRateName (i));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pattern", 1 }, "Picking pattern", pats, 0));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "patrate", 1 }, "Pattern rate", rates, 1));
    }
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "tempsync", 1 }, "Tempo sync", StringArray { "Off", "On" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "velcurve", 1 }, "Velocity curve", Range (-1.0f, 1.0f), 0.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) {
                                                      return std::abs (v) < 0.05f ? String ("Linear") : (v < 0 ? "Light " : "Heavy ") + String (roundToInt (std::abs (v) * 100.0f)) + " %"; })
                                                      .withValueFromStringFunction ([] (const String& t) {
                                                          const float x = t.getFloatValue() / 100.0f;
                                                          return t.containsIgnoreCase ("light") ? -x : x; })));

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
    pMode = apvts.getRawParameterValue ("mode");
    pVoicing = apvts.getRawParameterValue ("voicing");
    pScale = apvts.getRawParameterValue ("scale");
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
    currentProgram = calibratedProgram();
    for (const auto& a : swadv::params())
        pAdv.push_back (apvts.getRawParameterValue (a.id));
    for (auto& s : uiSemis)
        s = 0.0f;
    for (int i = 0; i < kSources; ++i)
    {
        capLo[(size_t) i] = 1e9f;
        capHi[(size_t) i] = -1e9f;
        capLast[(size_t) i] = 0.0f;
    }
    phrase_.reserve (64);
    loadCalibration();
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
    p.autoBend = (int) raw ("autobend") == 1;
    p.legatoSlide = (int) raw ("legatoslide") == 1;
    p.sweeten = raw ("sweeten");
    p.wheelMelody = (int) raw ("wheel") == 0;
    p.chordMove = (int) raw ("chordmove");
    p.pattern = juce::jlimit (0, sw::kPatterns - 1, (int) raw ("pattern"));
    p.patternRate = juce::jlimit (0, sw::kPatternRates - 1, (int) raw ("patrate"));
    if ((int) raw ("tempsync") == 1) // vibrato, swells and slides land on the host's beat
    {
        p.vibRate = sw::syncRate (p.vibRate, bpm_);
        p.swellTime = sw::syncSeconds (p.swellTime, bpm_);
        p.glide = sw::syncSeconds (p.glide, bpm_);
        p.slideTime = sw::syncSeconds (p.slideTime, bpm_);
        if (p.vibDelay > 0.02)
            p.vibDelay = sw::syncSeconds (p.vibDelay, bpm_);
    }
    const auto& adv = swadv::params();
    for (size_t i = 0; i < adv.size(); ++i)
        p.*(adv[i].field) = pAdv[i]->load();
    return p;
}

void SteelwaveProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sampleRate_ = sampleRate;
    engine.prepare (sampleRate);
    engine.setParams (readParams());
    lastSetParam_ = engineSet();
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
    const auto& cal = calAudio_;
    const bool wiz = wizardStep.load() >= 0;
    const bool mpeOn = (int) apvts.getRawParameterValue ("mpe")->load() == 1;
    // a pitch value -1..1 through the slider's real travel (and a little dead zone at rest)
    auto pitch = [&] (float w) {
        const float mid = cal.pitchMid, up = cal.pitchHi - mid, dn = mid - cal.pitchLo;
        float x = w >= mid ? (up > 0.05f ? (w - mid) / up : w - mid) : (dn > 0.05f ? (w - mid) / dn : w - mid);
        x = juce::jlimit (-1.0f, 1.0f, x);
        return std::abs (x) < 0.015f ? 0.0f : x;
    };
    auto span = [] (float v, float lo, float hi) { return hi - lo > 0.05f ? juce::jlimit (0.0f, 1.0f, (v - lo) / (hi - lo)) : v; };
    if (m.isNoteOn())
    {
        if (wiz)
        {
            capture (kSrcVelocity, m.getFloatVelocity());
            ++capNotes;
        }
        const float curve = apvts.getRawParameterValue ("velcurve")->load();
        engine.noteOn (m.getNoteNumber(), sw::shapeVelocity (m.getFloatVelocity(), curve, cal.velLo, cal.velHi), ch);
    }
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber(), ch);
    else if (m.isPitchWheel())
    {
        const float w = (float) ((m.getPitchWheelValue() - 8192) / 8191.0);
        if (wiz)
            capture (kSrcPitch, w);
        const bool member = mpeOn && ch >= 2;
        engine.pitchWheel (member || cal.pitchCc >= 0 ? w : pitch (w), ch);
    }
    else if (m.isChannelPressure() || m.isAftertouch()) // poly pressure: the bar is one bar, so the latest key's pressure
    {
        const float v = (m.isChannelPressure() ? m.getChannelPressureValue() : m.getAfterTouchValue()) / 127.0f;
        if (wiz)
            capture (kSrcPressure, v);
        engine.aftertouch (cal.atHi > 0.05f ? juce::jmin (1.0f, v / cal.atHi) : v, ch);
    }
    else if (m.isProgramChange())
        pendingProgram = m.getProgramChangeNumber();
    else if (m.isController())
    {
        int cc = m.getControllerNumber();
        float v = m.getControllerValue() / 127.0f;
        if (wiz && cc != 64 && cc < 120)
            capture (cc, v);
        const bool mpeMember = mpeOn && ch >= 2;
        // the player's own sliders and pedal, as found by controller setup
        if (! mpeMember)
        {
            if (cc == cal.pitchCc)
            {
                engine.pitchWheel (pitch (v * 2.0f - 1.0f), ch);
                return;
            }
            const int volCc = kVolumeCcs[juce::jlimit (0, 4, (int) pVolCc->load())];
            if (cc == cal.exprCc && volCc >= 0)
                cc = volCc;
            else if (cc == cal.modCc)
                cc = 1;
            if (cc == 1)
                v = span (v, cal.modLo, cal.modHi);
            else if (cc == volCc)
                v = span (v, cal.exprLo, cal.exprHi);
        }
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
                getParameters()[pi]->setValueNotifyingHost (v);
                return; // a learned CC drives its control, not its usual job
            }
        engine.controller (cc, v, ch);
    }
}

// --- mode ------------------------------------------------------------------------------------------
int SteelwaveProcessor::engineSet() const
{
    const int mode = (int) pMode->load();
    if (mode == 1)
        return juce::jlimit (0, sw::kChordSets - 1, (int) pVoicing->load());
    if (mode == 2)
        return sw::kChordSets + juce::jlimit (0, sw::kSingleSets - 1, (int) pScale->load());
    return sw::kSmartSet;
}

void SteelwaveProcessor::setEngineSet (int set)
{
    auto put = [this] (const char* id, int v) {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
    };
    if (set >= sw::kSmartSet)
        put ("mode", 0);
    else if (set < sw::kChordSets)
    {
        put ("voicing", set);
        put ("mode", 1);
    }
    else
    {
        put ("scale", set - sw::kChordSets);
        put ("mode", 2);
    }
}

// --- controller setup ----------------------------------------------------------------------------
juce::File SteelwaveProcessor::settingsDir()
{
    if (const char* env = std::getenv ("STEELWAVE_SETTINGS_DIR"); env != nullptr && *env != 0)
        return juce::File (juce::String (env));
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Steelwave");
}

SteelwaveProcessor::ControllerCal SteelwaveProcessor::calibration() const
{
    const juce::SpinLock::ScopedLockType l (calLock_);
    return calMsg_;
}

void SteelwaveProcessor::setCalibration (const ControllerCal& c, bool save)
{
    {
        const juce::SpinLock::ScopedLockType l (calLock_);
        calMsg_ = c;
    }
    ++calVersion_;
    if (! save)
        return;
    juce::XmlElement x ("SteelwaveController");
    x.setAttribute ("pitchCc", c.pitchCc);
    x.setAttribute ("modCc", c.modCc);
    x.setAttribute ("exprCc", c.exprCc);
    x.setAttribute ("pitchLo", c.pitchLo);
    x.setAttribute ("pitchHi", c.pitchHi);
    x.setAttribute ("pitchMid", c.pitchMid);
    x.setAttribute ("modLo", c.modLo);
    x.setAttribute ("modHi", c.modHi);
    x.setAttribute ("exprLo", c.exprLo);
    x.setAttribute ("exprHi", c.exprHi);
    x.setAttribute ("atHi", c.atHi);
    x.setAttribute ("velLo", c.velLo);
    x.setAttribute ("velHi", c.velHi);
    x.setAttribute ("asked", c.asked);
    settingsDir().createDirectory();
    x.writeTo (settingsDir().getChildFile ("Controller.xml"));
}

void SteelwaveProcessor::loadCalibration()
{
    ControllerCal c;
    if (const auto x = juce::parseXML (settingsDir().getChildFile ("Controller.xml")); x != nullptr && x->hasTagName ("SteelwaveController"))
    {
        auto f = [&] (const char* id, float d) { return (float) x->getDoubleAttribute (id, d); };
        c.pitchCc = x->getIntAttribute ("pitchCc", -1);
        c.modCc = x->getIntAttribute ("modCc", 1);
        c.exprCc = x->getIntAttribute ("exprCc", -1);
        c.pitchLo = f ("pitchLo", -1);
        c.pitchHi = f ("pitchHi", 1);
        c.pitchMid = f ("pitchMid", 0);
        c.modLo = f ("modLo", 0);
        c.modHi = f ("modHi", 1);
        c.exprLo = f ("exprLo", 0);
        c.exprHi = f ("exprHi", 1);
        c.atHi = f ("atHi", 1);
        c.velLo = f ("velLo", 0);
        c.velHi = f ("velHi", 1);
        c.asked = x->getBoolAttribute ("asked", false);
    }
    setCalibration (c, false);
}

void SteelwaveProcessor::capture (int source, float v)
{
    auto& lo = capLo[(size_t) source];
    auto& hi = capHi[(size_t) source];
    if (v < lo.load())
        lo = v;
    if (v > hi.load())
        hi = v;
    capLast[(size_t) source] = v;
}

void SteelwaveProcessor::startCapture (int step)
{
    for (int i = 0; i < kSources; ++i)
    {
        capLo[(size_t) i] = 1e9f;
        capHi[(size_t) i] = -1e9f;
    }
    capNotes = 0;
    wizardStep = step;
}

int SteelwaveProcessor::bestSource (int step, const std::vector<int>& exclude) const
{
    if (step == WizAftertouch)
        return capHi[kSrcPressure].load() > 0.05f ? kSrcPressure : -1;
    if (step == WizVelocity)
        return capNotes.load() >= 3 ? kSrcVelocity : -1;
    int best = -1;
    float bestRange = 0.15f; // moved at least this far (of the whole range)
    for (int i = 0; i < 120; ++i)
    {
        if (i == 64 || std::find (exclude.begin(), exclude.end(), i) != exclude.end())
            continue;
        const float r = capHi[(size_t) i].load() - capLo[(size_t) i].load();
        if (r > bestRange)
        {
            bestRange = r;
            best = i;
        }
    }
    if (step == WizPitch) // the pitch wheel itself wins if it moved (its range is -1..1: half it to compare)
        if (const float r = 0.5f * (capHi[kSrcPitch].load() - capLo[kSrcPitch].load()); r > 0.15f && r >= bestRange * 0.8f)
            return kSrcPitch;
    return best;
}

void SteelwaveProcessor::finishStep (int step, int source)
{
    auto c = calibration();
    const float lo = source >= 0 ? capLo[(size_t) source].load() : 0.0f, hi = source >= 0 ? capHi[(size_t) source].load() : 1.0f;
    // a little in from what was reached, so the ends are reachable without straining
    auto inset = [] (float& a, float& b) { const float d = 0.02f * (b - a); a += d; b -= d; };
    if (source >= 0 && hi > lo)
        switch (step)
        {
            case WizPitch:
                if (source == kSrcPitch)
                {
                    c.pitchCc = -1;
                    c.pitchLo = std::min (lo, -0.2f);
                    c.pitchHi = std::max (hi, 0.2f);
                    c.pitchMid = 0.0f;
                }
                else
                {
                    c.pitchCc = source;
                    c.pitchLo = lo * 2.0f - 1.0f;
                    c.pitchHi = hi * 2.0f - 1.0f;
                    c.pitchMid = 0.5f * (c.pitchLo + c.pitchHi);
                }
                inset (c.pitchLo, c.pitchHi);
                break;
            case WizMod:
                c.modCc = source;
                c.modLo = lo;
                c.modHi = hi;
                inset (c.modLo, c.modHi);
                break;
            case WizExpr:
            {
                c.exprLo = lo;
                c.exprHi = hi;
                inset (c.exprLo, c.exprHi);
                // a pedal on one of the volume CCs is set as the volume pedal; any other is passed on as it
                const int volIdx[] = { 11, 7, 4, 2 };
                c.exprCc = source;
                for (int i = 0; i < 4; ++i)
                    if (volIdx[i] == source)
                    {
                        c.exprCc = -1;
                        if (auto* p = apvts.getParameter ("volcc"))
                            p->setValueNotifyingHost (p->convertTo0to1 ((float) i));
                    }
                if (c.exprCc >= 0 && (int) pVolCc->load() == 4) // volume pedal was off: on, at CC11
                    if (auto* p = apvts.getParameter ("volcc"))
                        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
                break;
            }
            case WizAftertouch: c.atHi = std::max (0.2f, hi * 0.95f); break;
            case WizVelocity:
                c.velLo = std::min (lo, 0.3f);
                c.velHi = std::max (hi, 0.5f);
                break;
            default: break;
        }
    c.asked = true;
    setCalibration (c, true);
}

// --- A/B compare --------------------------------------------------------------------------------
std::unique_ptr<juce::XmlElement> SteelwaveProcessor::captureSound() const
{
    auto xml = std::make_unique<juce::XmlElement> ("Sound");
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (! swpresets::isRig (r->paramID))
                xml->setAttribute (r->paramID, (double) r->getValue());
    xml->setAttribute ("_program", currentProgram.load());
    xml->setAttribute ("_user", userPreset);
    return xml;
}

void SteelwaveProcessor::applySound (const juce::XmlElement& xml)
{
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (! swpresets::isRig (r->paramID) && xml.hasAttribute (r->paramID))
            {
                r->beginChangeGesture();
                r->setValueNotifyingHost ((float) xml.getDoubleAttribute (r->paramID));
                r->endChangeGesture();
            }
    currentProgram = xml.getIntAttribute ("_program", -1);
    userPreset = xml.getStringAttribute ("_user");
}

void SteelwaveProcessor::abSwitch (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == abActive_)
        return;
    abSlot_[abActive_] = captureSound();
    if (abSlot_[slot] != nullptr)
        applySound (*abSlot_[slot]);
    abActive_ = slot;
}

void SteelwaveProcessor::abCopyToOther()
{
    abSlot_[1 - abActive_] = captureSound();
}

// --- favourites ---------------------------------------------------------------------------------
juce::StringArray SteelwaveProcessor::favourites() const
{
    juce::StringArray a;
    a.addLines (settingsDir().getChildFile ("Favourites.txt").loadFileAsString());
    a.removeEmptyStrings();
    return a;
}

bool SteelwaveProcessor::isFavourite (const juce::String& name) const
{
    return favourites().contains (name);
}

void SteelwaveProcessor::toggleFavourite (const juce::String& name)
{
    if (name.isEmpty() || name == "-")
        return;
    auto a = favourites();
    if (a.contains (name))
        a.removeString (name);
    else
        a.add (name);
    settingsDir().createDirectory();
    settingsDir().getChildFile ("Favourites.txt").replaceWithText (a.joinIntoString ("\n"));
}

// --- audition -------------------------------------------------------------------------------------
// A short phrase that shows off the sound in the mode you're in (notes as the keyboard would send them).
void SteelwaveProcessor::buildPhrase()
{
    phrase_.clear();
    phraseNext_ = 0;
    phraseClock_ = 0;
    const double fs = sampleRate_;
    auto ev = [&] (double t, int note, float vel) { if (phrase_.size() < phrase_.capacity()) phrase_.push_back ({ (int64_t) (t * fs), note, vel }); };
    const int set = engine.currentSet(), key = juce::jlimit (0, 11, (int) pKey->load());
    if (set < sw::kChordSets) // I, IV, V, I: moves between them
    {
        const int keys[] = { 60, 65, 67, 60 };
        for (int i = 0; i < 4; ++i)
        {
            ev (0.9 * i, keys[i], i == 0 ? 0.75f : 0.65f);
            ev (0.9 * (i + 1) + (i == 3 ? 1.2 : 0.05), keys[i], 0.0f);
        }
    }
    else if (set == sw::kSmartSet) // the key's chord, then a melody note bent up a step and back
    {
        const int r = 60 + key - (key > 5 ? 12 : 0);
        for (int n : { r - 12, r - 5, r, r + 4 })
            ev (0.02 * (n - r + 12) / 4.0, n, 0.6f);
        ev (1.1, r + 9, 0.6f);  // a 6th on top...
        ev (1.8, r + 11, 0.6f); // ...bent up to the 7th
        ev (2.4, r + 11, 0.0f);
        ev (3.2, r + 9, 0.0f);
        for (int n : { r - 12, r - 5, r, r + 4 })
            ev (3.6, n, 0.0f);
    }
    else // single notes: up the scale and a held note
    {
        const int keys[] = { 60, 62, 64, 65, 67 };
        for (int i = 0; i < 5; ++i)
        {
            ev (0.32 * i, keys[i], 0.7f);
            ev (0.32 * (i + 1) + (i == 4 ? 1.2 : 0.0), keys[i], 0.0f);
        }
    }
    std::stable_sort (phrase_.begin(), phrase_.end(), [] (const PhraseEvent& a, const PhraseEvent& b) { return a.at < b.at; });
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
    userPreset.clear();
}

int SteelwaveProcessor::calibratedProgram()
{
    const auto& ps = swpresets::presets();
    for (size_t i = 0; i < ps.size(); ++i)
        if (juce::String (ps[i].name) == "Calibrated")
            return (int) i;
    return 0;
}

// --- the user's own presets: one small XML file each, in the user's application data folder ---
juce::File SteelwaveProcessor::userPresetDir()
{
    return settingsDir().getChildFile ("Presets");
}

juce::StringArray SteelwaveProcessor::userPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : userPresetDir().findChildFiles (juce::File::findFiles, false, "*.swpreset"))
        names.add (f.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

bool SteelwaveProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty() || ! userPresetDir().createDirectory())
        return false;
    juce::XmlElement xml ("SteelwavePreset");
    xml.setAttribute ("name", clean);
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (! swpresets::isRig (r->paramID))
                xml.setAttribute (r->paramID, (double) r->convertFrom0to1 (r->getValue()));
    if (! xml.writeTo (userPresetDir().getChildFile (clean + ".swpreset")))
        return false;
    userPreset = clean;
    currentProgram = -1;
    return true;
}

bool SteelwaveProcessor::loadUserPreset (const juce::String& name)
{
    const auto xml = juce::parseXML (userPresetDir().getChildFile (name + ".swpreset"));
    if (xml == nullptr || ! xml->hasTagName ("SteelwavePreset"))
        return false;
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (swpresets::isRig (r->paramID))
                continue;
            // a setting added since the preset was saved keeps its default
            const float v = xml->hasAttribute (r->paramID) ? r->convertTo0to1 ((float) xml->getDoubleAttribute (r->paramID)) : r->getDefaultValue();
            r->beginChangeGesture();
            r->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
            r->endChangeGesture();
        }
    userPreset = name;
    currentProgram = -1;
    return true;
}

bool SteelwaveProcessor::deleteUserPreset (const juce::String& name)
{
    const bool ok = userPresetDir().getChildFile (name + ".swpreset").deleteFile();
    if (ok && userPreset == name)
        userPreset.clear();
    return ok;
}

juce::String SteelwaveProcessor::presetName() const
{
    if (userPreset.isNotEmpty())
        return userPreset;
    const int i = currentProgram.load();
    return juce::isPositiveAndBelow (i, (int) swpresets::presets().size()) ? juce::String (swpresets::presets()[(size_t) i].name) : juce::String ("-");
}

void SteelwaveProcessor::setCurrentProgram (int index)
{
    applyPreset (index);
}

void SteelwaveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    if (! prepared_ || n < 1)
    {
        buffer.clear();
        return;
    }
    // the host's tempo and beat position (picking patterns, tempo sync)
    bool playing = false;
    double ppq = 0.0;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
        {
            if (const auto b = pos->getBpm())
                bpm_ = *b;
            playing = pos->getIsPlaying();
            if (const auto q = pos->getPpqPosition())
                ppq = *q;
            else
                playing = false;
        }
    engine.setTransport (bpm_, ppq, playing);
    // controller setup changed: take the new calibration (never wait for the lock)
    if (const int v = calVersion_.load(); v != calAudioVersion_)
    {
        const juce::SpinLock::ScopedTryLockType l (calLock_);
        if (l.isLocked())
        {
            calAudio_ = calMsg_;
            calAudioVersion_ = v;
        }
    }
    engine.setParams (readParams());
    // Mode / Voicing / Scale moved (UI, automation, preset): follow it. Keyswitches go the other way, below.
    const int setParam = engineSet();
    if (setParam != lastSetParam_)
    {
        engine.setSet (setParam);
        lastSetParam_ = setParam;
    }
    if (auditionRequest.exchange (false))
    {
        engine.allNotesOff();
        buildPhrase();
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
    // the audition phrase's events due in this block, merged with the incoming MIDI in time order
    auto nextPhrase = [&] () -> int { // sample offset of the next phrase event in this block, or n
        if (phraseNext_ >= phrase_.size())
            return n;
        const int64_t at = phrase_[phraseNext_].at - phraseClock_;
        return at < n ? (int) juce::jmax ((int64_t) 0, at) : n;
    };
    auto playPhrase = [&] (int upTo) {
        while (phraseNext_ < phrase_.size() && phrase_[phraseNext_].at - phraseClock_ <= upTo)
        {
            const auto& e = phrase_[phraseNext_++];
            if (e.vel > 0.0f)
                engine.noteOn (e.note, e.vel);
            else
                engine.noteOff (e.note);
        }
    };
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        for (int p = nextPhrase(); p <= at && p < n; p = nextPhrase())
        {
            if (p > pos)
            {
                render (pos, p);
                pos = p;
            }
            playPhrase (p);
        }
        if (at > pos)
        {
            render (pos, at);
            pos = at;
        }
        handle (meta.getMessage());
    }
    for (int p = nextPhrase(); p < n; p = nextPhrase())
    {
        if (p > pos)
        {
            render (pos, p);
            pos = p;
        }
        playPhrase (p);
    }
    if (pos < n)
        render (pos, n);
    if (phraseNext_ < phrase_.size())
        phraseClock_ += n;
    for (int c = 1; c < buffer.getNumChannels(); ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, n);

    // a program change arrived: switch preset (after this block, so the block plays out as it was)
    if (const int prog = pendingProgram.exchange (-1); prog >= 0)
        applyPreset (prog);

    // a keyswitch changed the set: show it on Mode / Voicing / Scale too
    if (engine.currentSet() != lastSetParam_)
    {
        lastSetParam_ = engine.currentSet();
        setEngineSet (lastSetParam_);
    }

    uiSet = engine.currentSet();
    uiSlot = engine.chordSlot();
    uiSmartRoot = engine.smartRoot();
    uiSmartQual = engine.smartQuality();
    uiBar = engine.bar();
    uiWheel = (float) engine.wheel();
    uiVolume = (float) engine.volume();
    uiMoveCombo = engine.lastMoveCombo();
    uiMoveFrets = engine.lastMoveFrets();
    uiPatternStep = engine.patternRunning() ? engine.patternStepIndex() : -1;
    uiBpm = (float) bpm_;
    uiPlaying = playing;
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

    // CPU: the share of the block's real time spent making it
    const double used = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const double load = used / (n / sampleRate_);
    cpuSmooth_ += 0.1 * (load - cpuSmooth_);
    uiCpu = (float) cpuSmooth_;
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
    state.setProperty ("userPreset", userPreset, nullptr);
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
            // sessions from before Mode / Voicing / Scale: the one Set parameter (0-7 chords, 8-11 single, 12 smart)
            for (int i = 0; i < state.getNumChildren(); ++i)
                if (auto c = state.getChild (i); c.hasType ("PARAM") && c["id"].toString() == "set")
                {
                    const int set = (int) c["value"];
                    auto put = [&state] (const char* id, int v) {
                        auto p = state.getChildWithProperty ("id", id);
                        if (! p.isValid())
                        {
                            p = juce::ValueTree ("PARAM");
                            p.setProperty ("id", id, nullptr);
                            state.appendChild (p, nullptr);
                        }
                        p.setProperty ("value", v, nullptr);
                    };
                    put ("mode", set >= sw::kSmartSet ? 0 : set < sw::kChordSets ? 1 : 2);
                    if (set < sw::kChordSets)
                        put ("voicing", set);
                    else if (set < sw::kSmartSet)
                        put ("scale", set - sw::kChordSets);
                    state.removeChild (c, nullptr);
                    break;
                }
            currentProgram = (int) state.getProperty ("program", calibratedProgram());
            userPreset = state.getProperty ("userPreset", "").toString();
            state.removeChild (map, nullptr);
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SteelwaveProcessor();
}

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cstdlib>

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
const int kPedalCcs[] = { 11, 7, 4, 2, -1 };
const double kMemberRanges[] = { 12, 24, 48, 96 };
const double kMasterRanges[] = { 2, 12, 24 };
} // namespace

APVTS::ParameterLayout ScrapeCelloProcessor::createLayout()
{
    using namespace juce;
    using Range = NormalisableRange<float>;
    using Attr = AudioParameterFloatAttributes;
    APVTS::ParameterLayout l;
    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };
    auto pctIn = [] (const String& t) { return t.getFloatValue() / 100.0f; };
    auto num = [] (const String& t) { return t.getFloatValue(); };
    auto addPct = [&] (const char* id, const char* name, float def) {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, Range (0.0f, 1.0f), def,
                                                      Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    };
    // the bow
    addPct ("pressure", "Bow pressure", 0.45f);
    addPct ("pressurerange", "Pressure reach (aftertouch)", 1.0f);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "position", 1 }, "Bow position", Range (0.0f, 1.0f), 0.35f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) {
                                                      return v < 0.2f ? String ("Sul tasto ") + String (roundToInt (v * 100.0f))
                                                           : v > 0.75f ? String ("Ponticello ") + String (roundToInt (v * 100.0f))
                                                                       : String (roundToInt (v * 100.0f)) + " %"; })
                                                      .withValueFromStringFunction (pctIn)));
    addPct ("speed", "Bow speed", 0.55f);
    addPct ("grit", "Grit (rosin)", 0.4f);
    // the string and body
    addPct ("brightness", "Brightness", 0.6f);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "ring", 1 }, "Ring after the bow", Range (0.0f, 3.0f), 1.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + "x"; })
                                                      .withValueFromStringFunction (num)));
    addPct ("body", "Body", 1.0f);
    // the left hand
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibdepth", 1 }, "Vibrato depth", Range (0.0f, 0.6f), 0.18f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " ct"; })
                                                      .withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibrate", 1 }, "Vibrato rate", Range (3.0f, 8.0f), 5.6f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " Hz"; })
                                                      .withValueFromStringFunction (num)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibdelay", 1 }, "Vibrato delay", Range (0.0f, 2.0f), 0.45f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " s"; })
                                                      .withValueFromStringFunction (num)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 }, "Glide", Range (0.01f, 1.0f, 0.0f, 0.5f), 0.12f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 1000.0f)) + " ms"; })
                                                      .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue() / 1000.0f; })));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Voices", StringArray { "Poly (4 bows)", "Mono (legato glides)" }, 0));
    // controllers and MPE
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pedalcc", 1 }, "Bow speed pedal",
                                                   StringArray { "CC11 Expression", "CC7 Volume", "CC4 Foot", "CC2 Breath", "Off" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mpe", 1 }, "MPE", StringArray { "Off", "On" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "memberrange", 1 }, "MPE note bend range",
                                                   StringArray { "12 st", "24 st", "48 st", "96 st" }, 2));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "masterrange", 1 }, "Pitch wheel range", StringArray { "2 st", "12 st", "24 st" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "velcurve", 1 }, "Velocity curve", Range (-1.0f, 1.0f), 0.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) {
                                                      return std::abs (v) < 0.05f ? String ("Linear") : (v < 0 ? "Light " : "Heavy ") + String (roundToInt (std::abs (v) * 100.0f)) + " %"; })
                                                      .withValueFromStringFunction ([] (const String& t) {
                                                          const float x = t.getFloatValue() / 100.0f;
                                                          return t.containsIgnoreCase ("light") ? -x : x; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "level", 1 }, "Level", Range (-24.0f, 12.0f), 0.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })
                                                      .withValueFromStringFunction (num)));
    return l;
}

ScrapeCelloProcessor::ScrapeCelloProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    clearMidiMap();
    for (int i = 0; i < kSources; ++i)
    {
        capLo[(size_t) i] = 1e9f;
        capHi[(size_t) i] = -1e9f;
        capLast[(size_t) i] = 0.0f;
    }
    phrase_.reserve (256);
    loadCalibration();
}

bool ScrapeCelloProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet().isDisabled() && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

sc::CelloParams ScrapeCelloProcessor::readParams() const
{
    auto raw = [this] (const char* id) { return (double) apvts.getRawParameterValue (id)->load(); };
    sc::CelloParams p;
    p.pressure = raw ("pressure");
    p.pressureRange = raw ("pressurerange");
    p.position = raw ("position");
    p.speed = raw ("speed");
    p.grit = raw ("grit");
    p.brightness = raw ("brightness");
    p.ring = raw ("ring");
    p.body = raw ("body");
    p.vibDepth = raw ("vibdepth");
    p.vibRate = raw ("vibrate");
    p.vibDelay = raw ("vibdelay");
    p.glide = raw ("glide");
    p.mono = (int) raw ("mode") == 1;
    p.pedalCc = kPedalCcs[juce::jlimit (0, 4, (int) raw ("pedalcc"))];
    p.mpe = (int) raw ("mpe") == 1;
    p.memberBendRange = kMemberRanges[juce::jlimit (0, 3, (int) raw ("memberrange"))];
    p.masterBendRange = kMasterRanges[juce::jlimit (0, 2, (int) raw ("masterrange"))];
    p.level = std::pow (10.0, raw ("level") / 20.0);
    return p;
}

void ScrapeCelloProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRate_ = sampleRate;
    cello_->prepare (sampleRate);
    cello_->setParams (readParams());
    prepared_ = true;
}

int ScrapeCelloProcessor::paramIndex (const juce::String& id) const
{
    const auto& ps = getParameters();
    for (int i = 0; i < ps.size(); ++i)
        if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (ps[i]))
            if (w->paramID == id)
                return i;
    return -1;
}

void ScrapeCelloProcessor::send (std::initializer_list<int> bytes)
{
    uint8_t d[3] = {};
    int n = 0;
    for (int b : bytes)
        if (n < 3) d[n++] = (uint8_t) b;
    cello_->midi (d, n);
}

// MIDI, fitted to the player's controllers, then on to the engine
void ScrapeCelloProcessor::handle (const juce::MidiMessage& m)
{
    const int ch = m.getChannel(), c0 = ch - 1;
    const auto& cal = calAudio_;
    const bool wiz = wizardStep.load() >= 0;
    const bool mpeOn = (int) apvts.getRawParameterValue ("mpe")->load() == 1;
    const bool member = mpeOn && ch >= 2;
    auto pitch = [&] (float w) {
        const float mid = cal.pitchMid, up = cal.pitchHi - mid, dn = mid - cal.pitchLo;
        float x = w >= mid ? (up > 0.05f ? (w - mid) / up : w - mid) : (dn > 0.05f ? (w - mid) / dn : w - mid);
        x = juce::jlimit (-1.0f, 1.0f, x);
        return std::abs (x) < 0.015f ? 0.0f : x;
    };
    auto span = [] (float v, float lo, float hi) { return hi - lo > 0.05f ? juce::jlimit (0.0f, 1.0f, (v - lo) / (hi - lo)) : v; };
    auto bend = [&] (int chan, float w) {
        const int x = juce::jlimit (0, 16383, (int) std::lround (8192 + w * 8191));
        send ({ 0xe0 | (chan - 1), x & 0x7f, x >> 7 });
    };
    if (m.isNoteOn())
    {
        if (wiz)
        {
            capture (kSrcVelocity, m.getFloatVelocity());
            ++capNotes;
        }
        // the velocity curve, over the range the keys really reach
        double v = m.getFloatVelocity();
        const double lo = cal.velLo, hi = cal.velHi;
        if (hi - lo > 0.05) v = juce::jlimit (0.0, 1.0, (v - lo) / (hi - lo));
        const double curve = apvts.getRawParameterValue ("velcurve")->load();
        v = std::max (0.02, std::pow (v, std::exp2 (juce::jlimit (-1.0, 1.0, curve) * 1.25)));
        send ({ 0x90 | c0, m.getNoteNumber(), juce::jlimit (1, 127, (int) std::lround (v * 127)) });
    }
    else if (m.isNoteOff())
        send ({ 0x80 | c0, m.getNoteNumber(), 0 });
    else if (m.isPitchWheel())
    {
        const float w = (float) ((m.getPitchWheelValue() - 8192) / 8191.0);
        if (wiz) capture (kSrcPitch, w);
        bend (ch, member || cal.pitchCc >= 0 ? w : pitch (w));
    }
    else if (m.isChannelPressure() || m.isAftertouch())
    {
        const float v = (m.isChannelPressure() ? m.getChannelPressureValue() : m.getAfterTouchValue()) / 127.0f;
        if (wiz) capture (kSrcPressure, v);
        const int scaled = juce::jlimit (0, 127, (int) std::lround (127 * (cal.atHi > 0.05f ? juce::jmin (1.0f, v / cal.atHi) : v)));
        if (m.isChannelPressure())
            send ({ 0xd0 | c0, scaled });
        else
            send ({ 0xa0 | c0, m.getNoteNumber(), scaled });
    }
    else if (m.isProgramChange())
        pendingProgram = m.getProgramChangeNumber();
    else if (m.isController())
    {
        int cc = m.getControllerNumber();
        float v = m.getControllerValue() / 127.0f;
        if (wiz && cc != 64 && cc < 120) capture (cc, v);
        if (! member)
        {
            if (cc == cal.pitchCc)
            {
                bend (ch, pitch (v * 2.0f - 1.0f));
                return;
            }
            const int pedalCc = kPedalCcs[juce::jlimit (0, 4, (int) apvts.getRawParameterValue ("pedalcc")->load())];
            if (cc == cal.exprCc && pedalCc >= 0) cc = pedalCc;
            else if (cc == cal.modCc) cc = 1;
            if (cc == 1) v = span (v, cal.modLo, cal.modHi);
            else if (cc == pedalCc) v = span (v, cal.exprLo, cal.exprHi);
            if (cc == 1) uiMod = v;
            if (cc == pedalCc) uiPedal = v;
        }
        const bool learnable = cc != 0 && cc != 32 && cc < 120 && ! member && cc != 101 && cc != 100 && cc != 6 && cc != 38;
        if (learnable && learnTarget.load() >= 0)
        {
            const int target = learnTarget.exchange (-1);
            for (auto& c : ccMap)
                if (c.load() == target) c = -1;
            ccMap[(size_t) cc] = target;
        }
        if (learnable)
            if (const int pi = ccMap[(size_t) cc].load(); pi >= 0 && pi < getParameters().size())
            {
                getParameters()[pi]->setValueNotifyingHost (v);
                return;
            }
        send ({ 0xb0 | c0, cc, juce::jlimit (0, 127, (int) std::lround (v * 127)) });
    }
}

void ScrapeCelloProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    if (! prepared_ || n < 1)
    {
        buffer.clear();
        return;
    }
    if (const int v = calVersion_.load(); v != calAudioVersion_)
    {
        const juce::SpinLock::ScopedTryLockType l (calLock_);
        if (l.isLocked())
        {
            calAudio_ = calMsg_;
            calAudioVersion_ = v;
        }
    }
    cello_->setParams (readParams());
    if (auditionRequest.exchange (false))
    {
        cello_->allNotesOff();
        buildPhrase();
    }
    float* out = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0) : nullptr;
    float scratch[512];
    auto render = [&] (int from, int to) {
        if (out != nullptr)
            return cello_->process (out + from, to - from);
        for (int i = from; i < to; i += 512)
            cello_->process (scratch, std::min (512, to - i));
    };
    // the audition phrase's events due in this block, merged with the incoming MIDI in time order
    auto nextPhrase = [&] () -> int {
        if (phraseNext_ >= phrase_.size()) return n;
        const int64_t at = phrase_[phraseNext_].at - phraseClock_;
        return at < n ? (int) juce::jmax ((int64_t) 0, at) : n;
    };
    auto playPhrase = [&] (int upTo) {
        while (phraseNext_ < phrase_.size() && phrase_[phraseNext_].at - phraseClock_ <= upTo)
        {
            const auto& e = phrase_[phraseNext_++];
            cello_->midi (e.m, e.n);
        }
    };
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        for (int p = nextPhrase(); p <= at && p < n; p = nextPhrase())
        {
            if (p > pos) { render (pos, p); pos = p; }
            playPhrase (p);
        }
        if (at > pos) { render (pos, at); pos = at; }
        handle (meta.getMessage());
    }
    for (int p = nextPhrase(); p < n; p = nextPhrase())
    {
        if (p > pos) { render (pos, p); pos = p; }
        playPhrase (p);
    }
    if (pos < n) render (pos, n);
    if (phraseNext_ < phrase_.size()) phraseClock_ += n;
    for (int c = 1; c < buffer.getNumChannels(); ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, n);

    if (const int prog = pendingProgram.exchange (-1); prog >= 0)
        applyPreset (prog);

    for (int i = 0; i < sc::ScrapeCello::kVoices; ++i)
    {
        const auto w = cello_->view (i);
        auto& u = uiVoices[(size_t) i];
        u.on = w.on;
        u.held = w.held;
        u.pitch = (float) w.pitch;
        u.position = (float) w.position;
        u.pressure = (float) w.pressure;
        u.level = (float) w.level;
        u.dir = (float) w.dir;
    }
    const double used = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    cpuSmooth_ += 0.1 * (used / (n / sampleRate_) - cpuSmooth_);
    uiCpu = (float) cpuSmooth_;
}

void ScrapeCelloProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();
    if (prepared_)
        cello_->allNotesOff();
}

juce::AudioProcessorEditor* ScrapeCelloProcessor::createEditor()
{
    return new ScrapeCelloEditor (*this);
}

// --- presets -------------------------------------------------------------------------------------
void ScrapeCelloProcessor::applyPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) scpresets::presets().size()))
        return;
    const auto& pr = scpresets::presets()[(size_t) index];
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (scpresets::isRig (r->paramID))
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

void ScrapeCelloProcessor::setCurrentProgram (int index) { applyPreset (index); }

juce::File ScrapeCelloProcessor::settingsDir()
{
    if (const char* env = std::getenv ("SCRAPECELLO_SETTINGS_DIR"); env != nullptr && *env != 0)
        return juce::File (juce::String (env));
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Scrape Cello");
}
juce::File ScrapeCelloProcessor::userPresetDir() { return settingsDir().getChildFile ("Presets"); }

juce::StringArray ScrapeCelloProcessor::userPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : userPresetDir().findChildFiles (juce::File::findFiles, false, "*.scpreset"))
        names.add (f.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

bool ScrapeCelloProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty() || ! userPresetDir().createDirectory())
        return false;
    juce::XmlElement xml ("ScrapeCelloPreset");
    xml.setAttribute ("name", clean);
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (! scpresets::isRig (r->paramID))
                xml.setAttribute (r->paramID, (double) r->convertFrom0to1 (r->getValue()));
    if (! xml.writeTo (userPresetDir().getChildFile (clean + ".scpreset")))
        return false;
    userPreset = clean;
    currentProgram = -1;
    return true;
}

bool ScrapeCelloProcessor::loadUserPreset (const juce::String& name)
{
    const auto xml = juce::parseXML (userPresetDir().getChildFile (name + ".scpreset"));
    if (xml == nullptr || ! xml->hasTagName ("ScrapeCelloPreset"))
        return false;
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (scpresets::isRig (r->paramID))
                continue;
            const float v = xml->hasAttribute (r->paramID) ? r->convertTo0to1 ((float) xml->getDoubleAttribute (r->paramID)) : r->getDefaultValue();
            r->beginChangeGesture();
            r->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
            r->endChangeGesture();
        }
    userPreset = name;
    currentProgram = -1;
    return true;
}

bool ScrapeCelloProcessor::deleteUserPreset (const juce::String& name)
{
    const bool ok = userPresetDir().getChildFile (name + ".scpreset").deleteFile();
    if (ok && userPreset == name)
        userPreset.clear();
    return ok;
}

juce::String ScrapeCelloProcessor::presetName() const
{
    if (userPreset.isNotEmpty())
        return userPreset;
    const int i = currentProgram.load();
    return juce::isPositiveAndBelow (i, (int) scpresets::presets().size()) ? juce::String (scpresets::presets()[(size_t) i].name) : juce::String ("-");
}

juce::StringArray ScrapeCelloProcessor::favourites() const
{
    juce::StringArray a;
    a.addLines (settingsDir().getChildFile ("Favourites.txt").loadFileAsString());
    a.removeEmptyStrings();
    return a;
}
bool ScrapeCelloProcessor::isFavourite (const juce::String& name) const { return favourites().contains (name); }
void ScrapeCelloProcessor::toggleFavourite (const juce::String& name)
{
    if (name.isEmpty() || name == "-")
        return;
    auto a = favourites();
    if (a.contains (name)) a.removeString (name);
    else a.add (name);
    settingsDir().createDirectory();
    settingsDir().getChildFile ("Favourites.txt").replaceWithText (a.joinIntoString ("\n"));
}

std::unique_ptr<juce::XmlElement> ScrapeCelloProcessor::captureSound() const
{
    auto xml = std::make_unique<juce::XmlElement> ("Sound");
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (! scpresets::isRig (r->paramID))
                xml->setAttribute (r->paramID, (double) r->getValue());
    xml->setAttribute ("_program", currentProgram.load());
    xml->setAttribute ("_user", userPreset);
    return xml;
}
void ScrapeCelloProcessor::applySound (const juce::XmlElement& xml)
{
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (! scpresets::isRig (r->paramID) && xml.hasAttribute (r->paramID))
            {
                r->beginChangeGesture();
                r->setValueNotifyingHost ((float) xml.getDoubleAttribute (r->paramID));
                r->endChangeGesture();
            }
    currentProgram = xml.getIntAttribute ("_program", -1);
    userPreset = xml.getStringAttribute ("_user");
}
void ScrapeCelloProcessor::abSwitch (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == abActive_)
        return;
    abSlot_[abActive_] = captureSound();
    if (abSlot_[slot] != nullptr)
        applySound (*abSlot_[slot]);
    abActive_ = slot;
}
void ScrapeCelloProcessor::abCopyToOther() { abSlot_[1 - abActive_] = captureSound(); }

// A phrase that shows the instrument: a D-A drone pressed from clean into a grind and back, then a short
// melody that leans into the edge.
void ScrapeCelloProcessor::buildPhrase()
{
    phrase_.clear();
    phraseNext_ = 0;
    phraseClock_ = 0;
    const double fs = sampleRate_;
    auto ev = [&] (double t, std::initializer_list<int> b) {
        if (phrase_.size() >= phrase_.capacity()) return;
        PhraseEvent e { (int64_t) (t * fs), {}, 0 };
        for (int x : b) if (e.n < 3) e.m[e.n++] = (uint8_t) x;
        phrase_.push_back (e);
    };
    ev (0.0, { 0x90, 38, 70 });
    ev (0.0, { 0x90, 45, 70 });
    for (double t = 0.2; t < 3.0; t += 0.05) // pressure: up into the grind and back down
        ev (t, { 0xd0, (int) std::lround (127 * std::sin (3.14159 * (t - 0.2) / 2.8)) });
    ev (3.1, { 0x80, 38, 0 });
    ev (3.1, { 0x80, 45, 0 });
    const int mel[] = { 57, 59, 62, 64 };
    for (int i = 0; i < 4; ++i)
    {
        ev (3.3 + 0.55 * i, { 0x90, mel[i], 90 });
        ev (3.3 + 0.55 * i + 0.5, { 0x80, mel[i], 0 });
    }
    for (double t = 3.3; t < 5.5; t += 0.05)
        ev (t, { 0xd0, (int) std::lround (70 + 50 * std::sin (6.0 * (t - 3.3))) });
    ev (5.6, { 0xd0, 0 });
    std::stable_sort (phrase_.begin(), phrase_.end(), [] (const PhraseEvent& a, const PhraseEvent& b) { return a.at < b.at; });
}

// --- controller setup ----------------------------------------------------------------------------
ScrapeCelloProcessor::ControllerCal ScrapeCelloProcessor::calibration() const
{
    const juce::SpinLock::ScopedLockType l (calLock_);
    return calMsg_;
}

void ScrapeCelloProcessor::setCalibration (const ControllerCal& c, bool save)
{
    {
        const juce::SpinLock::ScopedLockType l (calLock_);
        calMsg_ = c;
    }
    ++calVersion_;
    if (! save)
        return;
    juce::XmlElement x ("ScrapeCelloController");
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

void ScrapeCelloProcessor::loadCalibration()
{
    ControllerCal c;
    if (const auto x = juce::parseXML (settingsDir().getChildFile ("Controller.xml")); x != nullptr && x->hasTagName ("ScrapeCelloController"))
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

void ScrapeCelloProcessor::capture (int source, float v)
{
    auto& lo = capLo[(size_t) source];
    auto& hi = capHi[(size_t) source];
    if (v < lo.load()) lo = v;
    if (v > hi.load()) hi = v;
    capLast[(size_t) source] = v;
}

void ScrapeCelloProcessor::startCapture (int step)
{
    for (int i = 0; i < kSources; ++i)
    {
        capLo[(size_t) i] = 1e9f;
        capHi[(size_t) i] = -1e9f;
    }
    capNotes = 0;
    wizardStep = step;
}

int ScrapeCelloProcessor::bestSource (int step, const std::vector<int>& exclude) const
{
    if (step == WizAftertouch)
        return capHi[kSrcPressure].load() > 0.05f ? kSrcPressure : -1;
    if (step == WizVelocity)
        return capNotes.load() >= 3 ? kSrcVelocity : -1;
    int best = -1;
    float bestRange = 0.15f;
    for (int i = 0; i < 120; ++i)
    {
        if (i == 64 || std::find (exclude.begin(), exclude.end(), i) != exclude.end())
            continue;
        const float r = capHi[(size_t) i].load() - capLo[(size_t) i].load();
        if (r > bestRange) { bestRange = r; best = i; }
    }
    if (step == WizPitch)
        if (const float r = 0.5f * (capHi[kSrcPitch].load() - capLo[kSrcPitch].load()); r > 0.15f && r >= bestRange * 0.8f)
            return kSrcPitch;
    return best;
}

void ScrapeCelloProcessor::finishStep (int step, int source)
{
    auto c = calibration();
    const float lo = source >= 0 ? capLo[(size_t) source].load() : 0.0f, hi = source >= 0 ? capHi[(size_t) source].load() : 1.0f;
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
                const int volIdx[] = { 11, 7, 4, 2 };
                c.exprCc = source;
                for (int i = 0; i < 4; ++i)
                    if (volIdx[i] == source)
                    {
                        c.exprCc = -1;
                        if (auto* p = apvts.getParameter ("pedalcc"))
                            p->setValueNotifyingHost (p->convertTo0to1 ((float) i));
                    }
                if (c.exprCc >= 0 && (int) apvts.getRawParameterValue ("pedalcc")->load() == 4)
                    if (auto* p = apvts.getParameter ("pedalcc"))
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

// --- state ---------------------------------------------------------------------------------------
void ScrapeCelloProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
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

void ScrapeCelloProcessor::setStateInformation (const void* data, int size)
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
            userPreset = state.getProperty ("userPreset", "").toString();
            state.removeChild (map, nullptr);
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ScrapeCelloProcessor();
}

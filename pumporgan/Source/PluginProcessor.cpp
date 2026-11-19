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

APVTS::ParameterLayout PumpOrganProcessor::createLayout()
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

    // the instrument and its wind
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "model", 1 }, "Instrument", StringArray { "Chord organ", "Parlour organ" }, 0));
    addPct ("wind", "Wind", 0.8f);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "windmode", 1 }, "Pumping",
                                                   StringArray { "Easy (pedal = wind)", "Reservoir (pump it)", "Expression (strokes = wind)" }, 0));
    // the stops
    for (int s = 0; s < ro::kStops; ++s)
        l.add (std::make_unique<AudioParameterBool> (ParameterID { "stop" + String (s), 1 }, ro::rankVoices()[(size_t) s].name, s == ro::Diapason));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "vox", 1 }, "Vox Humana", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "forte", 1 }, "Forte", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bass", 1 }, "Bass coupler", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "melody", 1 }, "Melody coupler", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "hold", 1 }, "Drone hold", false));
    // the swell lever (the mod slider opens it the rest of the way), the key action, the mic
    addPct ("swell", "Swell", 0.0f);
    addPct ("chiff", "Chiff", 0.3f);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mic", 1 }, "Mic", StringArray { "Close", "Inside the case", "Across the room" }, 0));
    // auto-pump
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pumprate", 1 }, "Auto-pump",
                                                   StringArray { "Off", "1/4", "1/2", "1 bar", "2 bars" }, 0));
    addPct ("pumpdepth", "Pump depth", 0.5f);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "level", 1 }, "Level", Range (-24.0f, 12.0f), 0.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })
                                                      .withValueFromStringFunction (num)));
    // playing: the pedal, velocity, MPE and expression
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pedalcc", 1 }, "Wind pedal",
                                                   StringArray { "CC11 Expression", "CC7 Volume", "CC4 Foot", "CC2 Breath", "Off" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "velcurve", 1 }, "Velocity curve", Range (-1.0f, 1.0f), 0.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) {
                                                      return std::abs (v) < 0.05f ? String ("Linear") : (v < 0 ? "Light " : "Heavy ") + String (roundToInt (std::abs (v) * 100.0f)) + " %"; })
                                                      .withValueFromStringFunction ([] (const String& t) {
                                                          const float x = t.getFloatValue() / 100.0f;
                                                          return t.containsIgnoreCase ("light") ? -x : x; })));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mpe", 1 }, "MPE", StringArray { "Off", "On" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "memberrange", 1 }, "MPE note bend range",
                                                   StringArray { "12 st", "24 st", "48 st", "96 st" }, 2));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "masterrange", 1 }, "Pitch wheel range", StringArray { "2 st", "12 st", "24 st" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pressure", 1 }, "Pressure", StringArray { "Vibrato", "Swell", "Off" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibdepth", 1 }, "Vibrato depth", Range (0.0f, 1.0f), 0.35f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " ct"; })
                                                      .withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibrate", 1 }, "Vibrato rate", Range (3.0f, 8.0f), 5.4f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " Hz"; })
                                                      .withValueFromStringFunction (num)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "slide", 1 }, "MPE slide", StringArray { "Off", "Note's air" }, 1));

    // Advanced: the instrument's character
    auto adv = std::make_unique<AudioProcessorParameterGroup> ("advanced", "Advanced", "|");
    adv->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "age", 1 }, "Tuning age", Range (0.0f, 2.0f), 1.0f,
                                                          Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    adv->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "air", 1 }, "Air noise", Range (0.0f, 2.0f), 1.0f,
                                                          Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    auto amount = [] (float v, int) {
        return v < 0.02f ? String ("None") : String (roundToInt (v * 100.0f)) + " %" + (std::abs (v - 1.0f) < 0.02f ? " (as built)" : "");
    };
    adv->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "cutout", 1 }, "Low-wind cut-out", Range (0.0f, 2.0f), 1.0f,
                                                          Attr().withStringFromValueFunction (amount).withValueFromStringFunction (pctIn)));
    adv->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "warble", 1 }, "Low-wind warble", Range (0.0f, 2.0f), 1.0f,
                                                          Attr().withStringFromValueFunction (amount).withValueFromStringFunction (pctIn)));
    adv->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "neglect", 1 }, "Neglect", Range (0.0f, 1.0f), 0.0f,
                                                          Attr().withStringFromValueFunction ([] (float v, int) {
                                                              return v < 0.02f ? String ("As built") : v > 0.9f ? String ("Damp basement")
                                                                                                                : String (roundToInt (v * 100.0f)) + " %"; })
                                                              .withValueFromStringFunction (pctIn)));
    l.add (std::move (adv));
    return l;
}

PumpOrganProcessor::PumpOrganProcessor()
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
    for (auto& k : uiKeyBits) k = 0;
    for (auto& k : uiKeyLevel) k = 0.0f;
    phrase_.reserve (512);
    loadCalibration();
}

bool PumpOrganProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet().isDisabled() && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

ro::OrganParams PumpOrganProcessor::readParams() const
{
    auto raw = [this] (const juce::String& id) { return (double) apvts.getRawParameterValue (id)->load(); };
    ro::OrganParams p;
    p.model = (int) raw ("model") == 0 ? ro::Model::ChordOrgan : ro::Model::Parlour;
    p.wind = (ro::Wind) juce::jlimit (0, 2, (int) raw ("windmode"));
    for (int s = 0; s < ro::kStops; ++s)
        p.stops[(size_t) s] = raw ("stop" + juce::String (s)) > 0.5;
    p.voxHumana = raw ("vox") > 0.5;
    p.forte = raw ("forte") > 0.5;
    p.bassCoupler = raw ("bass") > 0.5;
    p.melodyCoupler = raw ("melody") > 0.5;
    p.hold = raw ("hold") > 0.5;
    p.swell = raw ("swell");
    p.swellCc = 1; // the mod slider (fitted to the player's own by controller setup)
    p.chiff = raw ("chiff");
    p.mic = (ro::OrganParams::Mic) juce::jlimit (0, 2, (int) raw ("mic"));
    p.pumpRate = (ro::OrganParams::PumpRate) juce::jlimit (0, 4, (int) raw ("pumprate"));
    p.pumpDepth = raw ("pumpdepth");
    p.tuningAge = raw ("age");
    p.airNoise = raw ("air");
    p.lowWindCutout = raw ("cutout");
    p.lowWindWarble = raw ("warble");
    p.neglect = raw ("neglect");
    p.level = std::pow (10.0, raw ("level") / 20.0);
    p.mpe = (int) raw ("mpe") == 1;
    p.memberBendRange = kMemberRanges[juce::jlimit (0, 3, (int) raw ("memberrange"))];
    p.masterBendRange = kMasterRanges[juce::jlimit (0, 2, (int) raw ("masterrange"))];
    p.pressure = (ro::OrganParams::Pressure) juce::jlimit (0, 2, (int) raw ("pressure"));
    p.vibDepth = raw ("vibdepth");
    p.vibRate = raw ("vibrate");
    p.slideAir = (int) raw ("slide") == 1;
    p.pedalCc = -1; // taken here (scaled by Wind), not by the engine
    return p;
}

void PumpOrganProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRate_ = sampleRate;
    organ_->prepare (sampleRate);
    organ_->setParams (readParams());
    updatePedal();
    prepared_ = true;
}

// the wind: the Wind knob, times the pedal once one is moved
void PumpOrganProcessor::updatePedal()
{
    organ_->pedal (apvts.getRawParameterValue ("wind")->load() * pedalCcValue_);
    uiPedal = (float) pedalCcValue_;
}

// MIDI, fitted to the player's controllers, then on to the engine
void PumpOrganProcessor::handle (const juce::MidiMessage& m)
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
        if (m.getNoteNumber() == kHoldKey) // the keyswitch: Drone hold on / off
        {
            if (auto* h = apvts.getParameter ("hold"))
                h->setValueNotifyingHost (h->getValue() > 0.5f ? 0.0f : 1.0f);
            return;
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
    {
        if (m.getNoteNumber() != kHoldKey)
            send ({ 0x80 | c0, m.getNoteNumber(), 0 });
    }
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
        int pedalCc = -1;
        if (! member)
        {
            if (cc == cal.pitchCc)
            {
                bend (ch, pitch (v * 2.0f - 1.0f));
                return;
            }
            pedalCc = pedalCc_;
            if (cc == cal.exprCc && pedalCc >= 0) cc = pedalCc;
            else if (cc == cal.modCc) cc = 1;
            if (cc == 1) v = span (v, cal.modLo, cal.modHi);
            else if (cc == pedalCc) v = span (v, cal.exprLo, cal.exprHi);
            if (cc == 1) uiMod = v;
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
        if (pedalCc >= 0 && cc == pedalCc) // the wind pedal: scaled by the Wind knob, here
        {
            pedalCcValue_ = v;
            updatePedal();
            return;
        }
        send ({ 0xb0 | c0, cc, juce::jlimit (0, 127, (int) std::lround (v * 127)) });
    }
}

void PumpOrganProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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
    const int newCc = kPedalCcs[juce::jlimit (0, 4, (int) apvts.getRawParameterValue ("pedalcc")->load())];
    if (newCc != pedalCc_)
    {
        pedalCc_ = newCc;
        pedalCcValue_ = 1.0;
    }
    organ_->setParams (readParams());
    updatePedal();
    // the host's tempo and position, for auto-pump
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            organ_->setTransport (pos->getBpm().orFallback (100.0), pos->getPpqPosition().orFallback (0.0), pos->getIsPlaying());
    if (auditionRequest.exchange (false))
    {
        organ_->allNotesOff();
        buildPhrase();
    }
    float* out = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0) : nullptr;
    float scratch[512];
    auto render = [&] (int from, int to) {
        if (out != nullptr)
            return organ_->process (out + from, to - from);
        for (int i = from; i < to; i += 512)
            organ_->process (scratch, std::min (512, to - i));
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
            if ((e.m[0] & 0xf0) == 0xb0 && e.m[1] == 11)
            {
                pedalCcValue_ = e.m[2] / 127.0;
                updatePedal();
            }
            else
            {
                if ((e.m[0] & 0xf0) == 0xb0 && e.m[1] == 1)
                    uiMod = e.m[2] / 127.0f;
                organ_->midi (e.m, e.n);
            }
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

    // for the editor
    const auto& o = *organ_;
    for (int k = 0; k < ro::kKeys; ++k)
    {
        const auto st = o.keyState (ro::kLowKey + k);
        uiKeyBits[(size_t) k] = (st.held ? KeyHeld : 0) | (st.coupled ? KeyCoupled : 0) | (st.latched ? KeyLatched : 0) | (st.cipher ? KeyCipher : 0);
        uiKeyLevel[(size_t) k] = (float) st.level;
    }
    uiSuction = (float) o.suction();
    uiChest = (float) o.chestSuction();
    uiReservoir = (float) o.reservoir();
    uiStruggle = (float) o.struggle();
    uiMotor = (float) o.motor();
    uiSwell = (float) o.swellOpen();
    uiPump = (float) o.pumpPhase();
    uiHolding = o.holding();
    const double used = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    cpuSmooth_ += 0.1 * (used / (n / sampleRate_) - cpuSmooth_);
    uiCpu = (float) cpuSmooth_;
}

void PumpOrganProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();
    if (prepared_)
        organ_->allNotesOff();
}

juce::AudioProcessorEditor* PumpOrganProcessor::createEditor()
{
    return new PumpOrganEditor (*this);
}

juce::String PumpOrganProcessor::presetNote() const
{
    if (userPreset.isNotEmpty())
        return "One of yours.";
    const int i = currentProgram.load();
    return juce::isPositiveAndBelow (i, (int) popresets::presets().size()) ? juce::String (popresets::presets()[(size_t) i].note) : juce::String();
}

// A phrase that shows the instrument: a hymn-like progression with the wind swelling in on the pedal, the
// swell lever opening for the climax, a melody over it, and the wind easing away at the end.
void PumpOrganProcessor::buildPhrase()
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
    // C - Am - F - G - C, two seconds each
    const int chords[5][3] = { { 48, 64, 67 }, { 45, 64, 69 }, { 41, 65, 69 }, { 43, 62, 71 }, { 48, 64, 72 } };
    const int tune[5] = { 76, 76, 77, 74, 72 };
    for (int c = 0; c < 5; ++c)
    {
        const double t = 0.2 + 2.0 * c;
        for (int k : chords[c])
        {
            ev (t, { 0x90, k, 72 });
            ev (t + 1.95, { 0x80, k, 0 });
        }
        ev (t + 0.02, { 0x90, tune[c], 96 });
        ev (t + (c == 4 ? 2.4 : 1.95), { 0x80, tune[c], 0 });
    }
    for (double t = 0.0; t < 11.0; t += 0.05)
    {
        // the pedal: swelling in, held, easing away at the end
        const double w = t < 1.5 ? 0.35 + 0.5 * t / 1.5 : t < 8.5 ? 0.85 + 0.1 * std::sin (t) : 0.85 * std::max (0.0, 1.0 - (t - 8.5) / 2.5);
        ev (t, { 0xb0, 11, juce::jlimit (0, 127, (int) std::lround (127 * w)) });
        // the swell lever opening over the third and fourth chords, closing for the last
        const double s = t < 4.2 ? 0.0 : t < 7.0 ? (t - 4.2) / 2.8 : std::max (0.0, 1.0 - (t - 7.0) / 1.5);
        ev (t, { 0xb0, 1, (int) std::lround (127 * s) });
    }
    ev (11.0, { 0xb0, 11, 127 });
    ev (11.0, { 0xb0, 1, 0 });
    std::stable_sort (phrase_.begin(), phrase_.end(), [] (const PhraseEvent& a, const PhraseEvent& b) { return a.at < b.at; });
}

int PumpOrganProcessor::paramIndex (const juce::String& id) const
{
    const auto& ps = getParameters();
    for (int i = 0; i < ps.size(); ++i)
        if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (ps[i]))
            if (w->paramID == id)
                return i;
    return -1;
}

void PumpOrganProcessor::send (std::initializer_list<int> bytes)
{
    uint8_t d[3] = {};
    int n = 0;
    for (int b : bytes)
        if (n < 3) d[n++] = (uint8_t) b;
    organ_->midi (d, n);
}

// --- presets -------------------------------------------------------------------------------------
void PumpOrganProcessor::applyPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) popresets::presets().size()))
        return;
    const auto& pr = popresets::presets()[(size_t) index];
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (popresets::isRig (r->paramID))
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

void PumpOrganProcessor::setCurrentProgram (int index) { applyPreset (index); }

juce::File PumpOrganProcessor::settingsDir()
{
    if (const char* env = std::getenv ("PUMPORGAN_SETTINGS_DIR"); env != nullptr && *env != 0)
        return juce::File (juce::String (env));
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Pump Organ");
}
juce::File PumpOrganProcessor::userPresetDir() { return settingsDir().getChildFile ("Presets"); }

juce::StringArray PumpOrganProcessor::userPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : userPresetDir().findChildFiles (juce::File::findFiles, false, "*.popreset"))
        names.add (f.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

bool PumpOrganProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty() || ! userPresetDir().createDirectory())
        return false;
    juce::XmlElement xml ("PumpOrganPreset");
    xml.setAttribute ("name", clean);
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (! popresets::isRig (r->paramID))
                xml.setAttribute (r->paramID, (double) r->convertFrom0to1 (r->getValue()));
    if (! xml.writeTo (userPresetDir().getChildFile (clean + ".popreset")))
        return false;
    userPreset = clean;
    currentProgram = -1;
    return true;
}

bool PumpOrganProcessor::loadUserPreset (const juce::String& name)
{
    const auto xml = juce::parseXML (userPresetDir().getChildFile (name + ".popreset"));
    if (xml == nullptr || ! xml->hasTagName ("PumpOrganPreset"))
        return false;
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (popresets::isRig (r->paramID))
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

bool PumpOrganProcessor::deleteUserPreset (const juce::String& name)
{
    const bool ok = userPresetDir().getChildFile (name + ".popreset").deleteFile();
    if (ok && userPreset == name)
        userPreset.clear();
    return ok;
}

juce::String PumpOrganProcessor::presetName() const
{
    if (userPreset.isNotEmpty())
        return userPreset;
    const int i = currentProgram.load();
    return juce::isPositiveAndBelow (i, (int) popresets::presets().size()) ? juce::String (popresets::presets()[(size_t) i].name) : juce::String ("-");
}

juce::StringArray PumpOrganProcessor::favourites() const
{
    juce::StringArray a;
    a.addLines (settingsDir().getChildFile ("Favourites.txt").loadFileAsString());
    a.removeEmptyStrings();
    return a;
}
bool PumpOrganProcessor::isFavourite (const juce::String& name) const { return favourites().contains (name); }
void PumpOrganProcessor::toggleFavourite (const juce::String& name)
{
    if (name.isEmpty() || name == "-")
        return;
    auto a = favourites();
    if (a.contains (name)) a.removeString (name);
    else a.add (name);
    settingsDir().createDirectory();
    settingsDir().getChildFile ("Favourites.txt").replaceWithText (a.joinIntoString ("\n"));
}

std::unique_ptr<juce::XmlElement> PumpOrganProcessor::captureSound() const
{
    auto xml = std::make_unique<juce::XmlElement> ("Sound");
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (! popresets::isRig (r->paramID))
                xml->setAttribute (r->paramID, (double) r->getValue());
    xml->setAttribute ("_program", currentProgram.load());
    xml->setAttribute ("_user", userPreset);
    return xml;
}
void PumpOrganProcessor::applySound (const juce::XmlElement& xml)
{
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (! popresets::isRig (r->paramID) && xml.hasAttribute (r->paramID))
            {
                r->beginChangeGesture();
                r->setValueNotifyingHost ((float) xml.getDoubleAttribute (r->paramID));
                r->endChangeGesture();
            }
    currentProgram = xml.getIntAttribute ("_program", -1);
    userPreset = xml.getStringAttribute ("_user");
}
void PumpOrganProcessor::abSwitch (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == abActive_)
        return;
    abSlot_[abActive_] = captureSound();
    if (abSlot_[slot] != nullptr)
        applySound (*abSlot_[slot]);
    abActive_ = slot;
}
void PumpOrganProcessor::abCopyToOther() { abSlot_[1 - abActive_] = captureSound(); }

// --- controller setup ----------------------------------------------------------------------------
PumpOrganProcessor::ControllerCal PumpOrganProcessor::calibration() const
{
    const juce::SpinLock::ScopedLockType l (calLock_);
    return calMsg_;
}

void PumpOrganProcessor::setCalibration (const ControllerCal& c, bool save)
{
    {
        const juce::SpinLock::ScopedLockType l (calLock_);
        calMsg_ = c;
    }
    ++calVersion_;
    if (! save)
        return;
    juce::XmlElement x ("PumpOrganController");
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

void PumpOrganProcessor::loadCalibration()
{
    ControllerCal c;
    if (const auto x = juce::parseXML (settingsDir().getChildFile ("Controller.xml")); x != nullptr && x->hasTagName ("PumpOrganController"))
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

void PumpOrganProcessor::capture (int source, float v)
{
    auto& lo = capLo[(size_t) source];
    auto& hi = capHi[(size_t) source];
    if (v < lo.load()) lo = v;
    if (v > hi.load()) hi = v;
    capLast[(size_t) source] = v;
}

void PumpOrganProcessor::startCapture (int step)
{
    for (int i = 0; i < kSources; ++i)
    {
        capLo[(size_t) i] = 1e9f;
        capHi[(size_t) i] = -1e9f;
    }
    capNotes = 0;
    wizardStep = step;
}

int PumpOrganProcessor::bestSource (int step, const std::vector<int>& exclude) const
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

void PumpOrganProcessor::finishStep (int step, int source)
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
void PumpOrganProcessor::getStateInformation (juce::MemoryBlock& dest)
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

void PumpOrganProcessor::setStateInformation (const void* data, int size)
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
            // tell the host every value, changed or not: a switch restored to the state it was already in
            // sends nothing on its own, and the host can be left showing the in-between value it last sent
            for (auto* prm : getParameters())
                prm->sendValueChangedMessageToListeners (prm->getValue());
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PumpOrganProcessor();
}

#include "PluginProcessor.h"

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

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "model", 1 }, "Instrument", StringArray { "Chord organ", "Parlour organ" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "wind", 1 }, "Wind", Range (0.0f, 1.0f), 0.8f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "windmode", 1 }, "Pumping",
                                                   StringArray { "Easy (pedal = wind)", "Reservoir (pump it)", "Expression (strokes = wind)" }, 0));
    for (int s = 0; s < ro::kStops; ++s)
        l.add (std::make_unique<AudioParameterBool> (ParameterID { "stop" + String (s), 1 }, ro::rankVoices()[(size_t) s].name, s == ro::Diapason));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "vox", 1 }, "Vox Humana", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "forte", 1 }, "Forte", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "level", 1 }, "Level", Range (-24.0f, 12.0f), 0.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })
                                                      .withValueFromStringFunction (num)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pedalcc", 1 }, "Wind pedal",
                                                   StringArray { "CC11 Expression", "CC7 Volume", "CC4 Foot", "CC2 Breath", "Off" }, 0));
    // MPE and expression
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
    l.add (std::move (adv));
    return l;
}

PumpOrganProcessor::PumpOrganProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool PumpOrganProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet().isDisabled() && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

ro::OrganParams PumpOrganProcessor::readParams() const
{
    auto raw = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    ro::OrganParams p;
    p.model = (int) raw ("model") == 0 ? ro::Model::ChordOrgan : ro::Model::Parlour;
    p.wind = (ro::Wind) juce::jlimit (0, 2, (int) raw ("windmode"));
    for (int s = 0; s < ro::kStops; ++s)
        p.stops[(size_t) s] = raw ("stop" + juce::String (s)) > 0.5f;
    p.voxHumana = raw ("vox") > 0.5f;
    p.forte = raw ("forte") > 0.5f;
    p.tuningAge = raw ("age");
    p.airNoise = raw ("air");
    p.lowWindCutout = raw ("cutout");
    p.lowWindWarble = raw ("warble");
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
    organ_->prepare (sampleRate);
    organ_->setParams (readParams());
    updatePedal();
    prepared_ = true;
}

// the wind: the Wind knob, times the pedal once one is moved
void PumpOrganProcessor::updatePedal()
{
    organ_->pedal (apvts.getRawParameterValue ("wind")->load() * pedalCcValue_);
}

void PumpOrganProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (! prepared_ || n < 1)
    {
        buffer.clear();
        return;
    }
    const int newCc = kPedalCcs[juce::jlimit (0, 4, (int) apvts.getRawParameterValue ("pedalcc")->load())];
    if (newCc != pedalCc_)
    {
        pedalCc_ = newCc;
        pedalCcValue_ = 1.0;
    }
    organ_->setParams (readParams());
    updatePedal();
    float* out = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0) : nullptr;
    std::vector<float> scratch;
    if (out == nullptr)
    {
        scratch.assign ((size_t) n, 0.0f);
        out = scratch.data();
    }
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos)
        {
            organ_->process (out + pos, at - pos);
            pos = at;
        }
        const auto m = meta.getMessage();
        if (pedalCc_ >= 0 && m.isController() && m.getControllerNumber() == pedalCc_)
        {
            pedalCcValue_ = m.getControllerValue() / 127.0;
            updatePedal();
            continue;
        }
        organ_->midi (m.getRawData(), m.getRawDataSize());
    }
    if (pos < n)
        organ_->process (out + pos, n - pos);
    for (int c = 1; c < buffer.getNumChannels(); ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, n);
}

void PumpOrganProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();
    if (prepared_)
        organ_->allNotesOff();
}

juce::AudioProcessorEditor* PumpOrganProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void PumpOrganProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void PumpOrganProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PumpOrganProcessor();
}

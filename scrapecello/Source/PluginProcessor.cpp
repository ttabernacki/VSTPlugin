#include "PluginProcessor.h"

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
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "level", 1 }, "Level", Range (-24.0f, 12.0f), 0.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })
                                                      .withValueFromStringFunction (num)));
    return l;
}

ScrapeCelloProcessor::ScrapeCelloProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
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
    cello_->prepare (sampleRate);
    cello_->setParams (readParams());
    prepared_ = true;
}

void ScrapeCelloProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (! prepared_ || n < 1)
    {
        buffer.clear();
        return;
    }
    cello_->setParams (readParams());
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
            cello_->process (out + pos, at - pos);
            pos = at;
        }
        const auto m = meta.getMessage();
        cello_->midi (m.getRawData(), m.getRawDataSize());
    }
    if (pos < n)
        cello_->process (out + pos, n - pos);
    for (int c = 1; c < buffer.getNumChannels(); ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, n);
}

void ScrapeCelloProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();
    if (prepared_)
        cello_->allNotesOff();
}

juce::AudioProcessorEditor* ScrapeCelloProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void ScrapeCelloProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void ScrapeCelloProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ScrapeCelloProcessor();
}

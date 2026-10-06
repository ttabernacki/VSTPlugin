#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout BassLevelerProcessor::createLayout()
{
    using namespace juce;
    using Range = NormalisableRange<float>;
    APVTS::ParameterLayout l;
    using Attr = AudioParameterFloatAttributes;
    auto dbText = [] (float v, int) { return String (v, 1) + " dB"; };
    auto numberFrom = [] (const String& t) { return t.getFloatValue(); };
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 }, "Amount", Range (0.0f, 1.0f), 0.6f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })
                                                      .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue() / 100.0f; })));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", StringArray { "Balance", "Level" }, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "focus", 1 }, "Focus 2nd harmonic", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "boost", 1 }, "Max boost", Range (0.0f, 12.0f), 6.0f,
                                                  Attr().withStringFromValueFunction (dbText).withValueFromStringFunction (numberFrom)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "cut", 1 }, "Max cut", Range (0.0f, 18.0f), 9.0f,
                                                  Attr().withStringFromValueFunction (dbText).withValueFromStringFunction (numberFrom)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "speed", 1 }, "Speed", Range (5.0f, 120.0f, 0.0f, 0.5f), 30.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })
                                                      .withValueFromStringFunction (numberFrom)));
    return l;
}

BassLevelerProcessor::BassLevelerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pAmount = apvts.getRawParameterValue ("amount");
    pMode = apvts.getRawParameterValue ("mode");
    pFocus = apvts.getRawParameterValue ("focus");
    pBoost = apvts.getRawParameterValue ("boost");
    pCut = apvts.getRawParameterValue ("cut");
    pSpeed = apvts.getRawParameterValue ("speed");
    for (int i = 0; i < kRecent; ++i)
        recMidi[i] = recDev[i] = recCorr[i] = 0.0f;
}

bool BassLevelerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk && ! (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

void BassLevelerProcessor::prepareToPlay (double sampleRate, int)
{
    core.prepare (sampleRate);
    setLatencySamples (core.latencySamples());
    latencySeconds = (double) core.latencySamples() / sampleRate;
    recentCount = 0;
    prepared_ = true;
}

void BassLevelerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nCh = std::min (buffer.getNumChannels(), 2);
    if (nCh < 1 || ! prepared_)
        return;
    // a mono input on a stereo bus: make both channels carry it
    if (getTotalNumInputChannels() == 1 && nCh == 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    bnl::Params p;
    p.amount = pAmount->load();
    p.mode = (int) pMode->load();
    p.focus2 = pFocus->load() > 0.5f;
    p.maxBoostDb = pBoost->load();
    p.maxCutDb = pCut->load();
    p.speedMs = pSpeed->load();
    core.setParams (p);

    core.process (buffer.getArrayOfWritePointers(), nCh, n);

    gainDb.store (core.currentGainDb());
    pitchHz.store (core.currentPitchHz());

    bnl::RecentNote r[kRecent];
    const int count = core.copyRecent (r, kRecent);
    for (int i = 0; i < count; ++i)
    {
        recMidi[i].store (r[i].midi);
        recDev[i].store (r[i].deviationDb);
        recCorr[i].store (r[i].correctionDb);
    }
    recentCount.store (count);
}

juce::AudioProcessorEditor* BassLevelerProcessor::createEditor()
{
    return new BassLevelerEditor (*this);
}

void BassLevelerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void BassLevelerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BassLevelerProcessor();
}

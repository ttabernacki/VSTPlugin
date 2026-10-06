#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout LowEndDefinitionProcessor::createLayout()
{
    using namespace juce;
    using Range = NormalisableRange<float>;
    using Attr = AudioParameterFloatAttributes;
    APVTS::ParameterLayout l;
    auto percent = [] (float v, int) {
        const int p = roundToInt (v * 100.0f);
        return String (p > 0 ? "+" : "") + String (p) + " %";
    };
    auto percentIn = [] (const String& t) { return t.getFloatValue() / 100.0f; };
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "contrast", 1 }, "Contrast", Range (-1.0f, 1.0f), 0.5f,
                                                  Attr().withStringFromValueFunction (percent).withValueFromStringFunction (percentIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "punch", 1 }, "Punch", Range (-1.0f, 1.0f), 0.3f,
                                                  Attr().withStringFromValueFunction (percent).withValueFromStringFunction (percentIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "sustain", 1 }, "Sustain", Range (-1.0f, 1.0f), 0.0f,
                                                  Attr().withStringFromValueFunction (percent).withValueFromStringFunction (percentIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "range", 1 }, "Range", Range (60.0f, 300.0f, 0.0f, 0.6f), 200.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })
                                                      .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); })));
    return l;
}

LowEndDefinitionProcessor::LowEndDefinitionProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pContrast = apvts.getRawParameterValue ("contrast");
    pPunch = apvts.getRawParameterValue ("punch");
    pSustain = apvts.getRawParameterValue ("sustain");
    pRange = apvts.getRawParameterValue ("range");
    for (int i = 0; i < kHist; ++i)
    {
        hIn[i] = hOut[i] = -1.0f;
        hTrans[i] = 0.0f;
    }
}

bool LowEndDefinitionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk && ! (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

void LowEndDefinitionProcessor::prepareToPlay (double sampleRate, int)
{
    core.prepare (sampleRate);
    setLatencySamples (core.latencySamples());
    latencySeconds = (double) core.latencySamples() / sampleRate;
    histEvery_ = std::max (1, (int) std::lround (0.030 * sampleRate));
    sinceHist_ = 0;
    prepared_ = true;
}

void LowEndDefinitionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nCh = std::min (buffer.getNumChannels(), 2);
    if (nCh < 1 || ! prepared_)
        return;
    // a mono input on a stereo bus: make both channels carry it
    if (getTotalNumInputChannels() == 1 && nCh == 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    led::Params p;
    p.contrast = pContrast->load();
    p.punch = pPunch->load();
    p.sustain = pSustain->load();
    p.rangeHz = pRange->load();
    core.setParams (p);
    core.process (buffer.getArrayOfWritePointers(), nCh, n);

    pitchHz.store (core.pitchHz());
    bellDb.store (core.contrastGainDb());
    transDb.store (core.transientGainDb());
    defIn.store (core.meterValid() ? core.definitionIn() : -1.0f);
    defOut.store (core.meterValid() ? core.definitionOut() : -1.0f);
    sinceHist_ += n;
    if (sinceHist_ >= histEvery_)
    {
        sinceHist_ = 0;
        const int k = histHead.load();
        hIn[k].store (defIn.load());
        hOut[k].store (defOut.load());
        hTrans[k].store (transDb.load());
        histHead.store ((k + 1) % kHist);
    }
}

juce::AudioProcessorEditor* LowEndDefinitionProcessor::createEditor()
{
    return new LowEndDefinitionEditor (*this);
}

void LowEndDefinitionProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void LowEndDefinitionProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LowEndDefinitionProcessor();
}

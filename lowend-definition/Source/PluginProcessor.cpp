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
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "match", 1 }, "Match loudness", true));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "kick", 1 }, "Kick", Range (0.0f, 1.0f), 0.6f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })
                                                      .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue() / 100.0f; })));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "align", 1 }, "Auto polarity", false));
    return l;
}

LowEndDefinitionProcessor::LowEndDefinitionProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pContrast = apvts.getRawParameterValue ("contrast");
    pPunch = apvts.getRawParameterValue ("punch");
    pSustain = apvts.getRawParameterValue ("sustain");
    pRange = apvts.getRawParameterValue ("range");
    pMatch = apvts.getRawParameterValue ("match");
    pKick = apvts.getRawParameterValue ("kick");
    pAlign = apvts.getRawParameterValue ("align");
    for (int i = 0; i < kHist; ++i)
    {
        hIn[i] = hOut[i] = -1.0f;
        hTrans[i] = hDuck[i] = 0.0f;
    }
}

bool LowEndDefinitionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
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
    const int mainIn = getMainBusNumInputChannels();
    // a mono input on a stereo bus: make both channels carry it
    if (mainIn == 1 && nCh == 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);
    // the kick, if a track is routed to the sidechain input
    const float* scPtr[2] = { nullptr, nullptr };
    int scCh = 0;
    if (getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled())
    {
        auto scBuf = getBusBuffer (buffer, true, 1);
        scCh = std::min (scBuf.getNumChannels(), 2);
        for (int c = 0; c < scCh; ++c)
            scPtr[c] = scBuf.getReadPointer (c);
    }

    led::Params p;
    p.contrast = pContrast->load();
    p.punch = pPunch->load();
    p.sustain = pSustain->load();
    p.rangeHz = pRange->load();
    p.match = pMatch->load() > 0.5f;
    p.kick = pKick->load();
    p.align = pAlign->load() > 0.5f ? 1 : 0;
    core.setParams (p);
    core.process (buffer.getArrayOfWritePointers(), nCh, n, scCh > 0 ? scPtr : nullptr, scCh);

    pitchHz.store (core.pitchHz());
    bellDb.store (core.contrastGainDb());
    transDb.store (core.transientGainDb());
    duckDb.store (core.kickDuckDb());
    alignDb.store (core.alignKnown() ? core.alignDb() : 0.0f);
    alignLagMs.store (core.alignLagMs());
    alignKnown.store (core.alignKnown());
    flipped.store (core.polarityFlipped());
    scConnected.store (scCh > 0);
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
        hDuck[k].store (duckDb.load());
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

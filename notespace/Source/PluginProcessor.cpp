#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout NoteSpaceProcessor::createLayout()
{
    using namespace juce;
    using Range = NormalisableRange<float>;
    using Attr = AudioParameterFloatAttributes;
    APVTS::ParameterLayout l;
    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };
    auto pctSigned = [] (float v, int) {
        const int p = roundToInt (v * 100.0f);
        return String (p > 0 ? "+" : "") + String (p) + " %";
    };
    auto pctIn = [] (const String& t) { return t.getFloatValue() / 100.0f; };
    auto num = [] (const String& t) { return t.getFloatValue(); };
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "contrast", 1 }, "Contrast", Range (-1.0f, 1.0f), 0.4f,
                                                  Attr().withStringFromValueFunction (pctSigned).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tonelock", 1 }, "Tone lock", Range (0.0f, 1.0f), 0.0f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "fundamental", 1 }, "Fundamental", Range (-6.0f, 6.0f), 0.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (v > 0 ? "+" : "") + String (v, 1) + " dB"; })
                                                      .withValueFromStringFunction (num)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "repair", 1 }, "Repair", Range (0.0f, 1.0f), 0.0f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "translate", 1 }, "Translate", Range (0.0f, 1.0f), 0.0f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "range", 1 }, "Range", Range (100.0f, 500.0f, 0.0f, 0.6f), 300.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })
                                                      .withValueFromStringFunction (num)));
    return l;
}

NoteSpaceProcessor::NoteSpaceProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pContrast = apvts.getRawParameterValue ("contrast");
    pTone = apvts.getRawParameterValue ("tonelock");
    pFund = apvts.getRawParameterValue ("fundamental");
    pRepair = apvts.getRawParameterValue ("repair");
    pTranslate = apvts.getRawParameterValue ("translate");
    pRange = apvts.getRawParameterValue ("range");
    for (int h = 0; h < kH; ++h)
        hIn[h] = hOut[h] = -100.0f;
}

bool NoteSpaceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk && ! (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

void NoteSpaceProcessor::prepareToPlay (double sampleRate, int)
{
    core.prepare (sampleRate);
    setLatencySamples (core.latencySamples());
    latencySeconds = (double) core.latencySamples() / sampleRate;
    prepared_ = true;
}

void NoteSpaceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nCh = std::min ({ getMainBusNumOutputChannels(), buffer.getNumChannels(), 2 });
    if (nCh < 1 || ! prepared_ || n < 1)
        return;
    if (getMainBusNumInputChannels() == 1 && nCh == 2) // a mono input on a stereo bus: both channels carry it
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    nsp::Params p;
    p.contrast = pContrast->load();
    p.toneLock = pTone->load();
    p.fundamentalDb = pFund->load();
    p.repair = pRepair->load();
    p.translate = pTranslate->load();
    p.rangeHz = pRange->load();
    core.setParams (p);
    core.process (buffer.getArrayOfWritePointers(), nCh, n);

    for (int h = 0; h < kH; ++h)
    {
        hIn[h].store (core.harmonicInDb (h));
        hOut[h].store (core.harmonicOutDb (h));
    }
    pitchHz.store (core.pitchHz());
    voicing.store (core.voicing());
    noteResIn.store (core.noteToResidualInDb());
    noteResOut.store (core.noteToResidualOutDb());
    residualDb.store (core.residualGainDb());
}

juce::AudioProcessorEditor* NoteSpaceProcessor::createEditor()
{
    return new NoteSpaceEditor (*this);
}

void NoteSpaceProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void NoteSpaceProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NoteSpaceProcessor();
}

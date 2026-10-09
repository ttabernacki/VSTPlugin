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
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "fundamental", 1 }, "Fundamental", Range (-18.0f, 18.0f), 0.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (v > 0 ? "+" : "") + String (v, 1) + " dB"; })
                                                      .withValueFromStringFunction (num)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "repair", 1 }, "Repair", Range (0.0f, 1.0f), 0.0f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "translate", 1 }, "Translate", Range (0.0f, 2.0f), 0.0f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "range", 1 }, "Range", Range (50.0f, 1000.0f, 0.0f, 0.4f), 300.0f,
                                                  Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })
                                                      .withValueFromStringFunction (num)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "punch", 1 }, "Punch", Range (-1.0f, 1.0f), 0.0f,
                                                  Attr().withStringFromValueFunction (pctSigned).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "sustain", 1 }, "Sustain", Range (-1.0f, 1.0f), 0.0f,
                                                  Attr().withStringFromValueFunction (pctSigned).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "kick", 1 }, "Kick", Range (0.0f, 1.0f), 0.0f,
                                                  Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    return l;
}

NoteSpaceProcessor::NoteSpaceProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pContrast = apvts.getRawParameterValue ("contrast");
    pTone = apvts.getRawParameterValue ("tonelock");
    pFund = apvts.getRawParameterValue ("fundamental");
    pRepair = apvts.getRawParameterValue ("repair");
    pTranslate = apvts.getRawParameterValue ("translate");
    pRange = apvts.getRawParameterValue ("range");
    pPunch = apvts.getRawParameterValue ("punch");
    pSustain = apvts.getRawParameterValue ("sustain");
    pKick = apvts.getRawParameterValue ("kick");
    for (int h = 0; h < kH; ++h)
        hIn[h] = hOut[h] = -100.0f;
}

bool NoteSpaceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void NoteSpaceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    scratch.setSize (2, std::max (samplesPerBlock, 512), false, true, false);
    core.prepare (sampleRate);
    setLatencySamples (core.latencySamples());
    latencySeconds = (double) core.latencySamples() / sampleRate;
    prepared_ = true;
}

void NoteSpaceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // a host may hand over more samples than it announced: work in slices that fit the scratch buffer
    const int cap = scratch.getNumSamples();
    if (prepared_ && cap > 0 && buffer.getNumSamples() > cap)
    {
        for (int pos = 0; pos < buffer.getNumSamples(); pos += cap)
        {
            juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), pos,
                                            std::min (cap, buffer.getNumSamples() - pos));
            processBlock (slice, midi);
        }
        return;
    }
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    // the audio channels are those of the main bus: with a sidechain the buffer carries more channels than that
    const int nCh = std::min ({ getMainBusNumOutputChannels(), buffer.getNumChannels(), 2 });
    if (nCh < 1 || ! prepared_ || n < 1)
        return;
    // the kick, if a track is routed to the sidechain input. Copied first: in a mono-in / stereo-out layout the host's
    // buffer shares its second channel between the sidechain input and the main output.
    int scCh = 0;
    if (getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled())
    {
        auto scBuf = getBusBuffer (buffer, true, 1);
        scCh = std::min ({ scBuf.getNumChannels(), 2, scratch.getNumChannels() });
        for (int c = 0; c < scCh; ++c)
            scratch.copyFrom (c, 0, scBuf, c, 0, n);
    }
    const float* scPtr[2] = { scCh > 0 ? scratch.getReadPointer (0) : nullptr, scCh > 1 ? scratch.getReadPointer (1) : nullptr };
    if (getMainBusNumInputChannels() == 1 && nCh == 2) // a mono input on a stereo bus: both channels carry it
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    nsp::Params p;
    p.contrast = pContrast->load();
    p.toneLock = pTone->load();
    p.fundamentalDb = pFund->load();
    p.repair = pRepair->load();
    p.translate = pTranslate->load();
    p.rangeHz = pRange->load();
    p.punch = pPunch->load();
    p.sustain = pSustain->load();
    p.kick = pKick->load();
    core.setParams (p);
    core.process (buffer.getArrayOfWritePointers(), nCh, n, scCh > 0 ? scPtr : nullptr, scCh);
    duckDb.store (core.kickDuckDb());
    dynDb.store (core.dynamicsDb());
    scConnected.store (scCh > 0);

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

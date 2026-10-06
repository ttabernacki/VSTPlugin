#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout BassLevelerProcessor::createLayout()
{
    using namespace juce;
    using Range = NormalisableRange<float>;
    APVTS::ParameterLayout l;
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "learn", 1 }, "Learn", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "strength", 1 }, "Strength", Range (0.0f, 1.0f), 0.7f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", StringArray { "Balance", "Level" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "rider", 1 }, "Rider", Range (0.0f, 1.0f), 0.0f));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "focus", 1 }, "Focus 2nd harmonic", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "boost", 1 }, "Max boost", Range (0.0f, 12.0f), 6.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "cut", 1 }, "Max cut", Range (0.0f, 24.0f), 12.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "speed", 1 }, "Speed", Range (5.0f, 120.0f, 0.0f, 0.5f), 30.0f,
                                                  AudioParameterFloatAttributes().withLabel ("ms")));
    return l;
}

BassLevelerProcessor::BassLevelerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pLearn = apvts.getRawParameterValue ("learn");
    pStrength = apvts.getRawParameterValue ("strength");
    pMode = apvts.getRawParameterValue ("mode");
    pRider = apvts.getRawParameterValue ("rider");
    pFocus = apvts.getRawParameterValue ("focus");
    pBoost = apvts.getRawParameterValue ("boost");
    pCut = apvts.getRawParameterValue ("cut");
    pSpeed = apvts.getRawParameterValue ("speed");
    pending_.clear();
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
    prepared_ = true;
    // a table that arrived with the project state before audio started
    applyPendingRestore();
    lastTotal_ = -1;
    publish (true);
}

bool BassLevelerProcessor::applyPendingRestore()
{
    if (! restorePending.load())
        return false;
    const juce::SpinLock::ScopedTryLockType sl (lock); // never block the audio thread; retry next block
    if (! sl.isLocked())
        return false;
    restorePending = false;
    core.table() = pending_;
    core.table().rebuild();
    return true;
}

void BassLevelerProcessor::publish (bool force)
{
    const auto& t = core.table();
    if (! force && t.totalNotes() == lastTotal_)
        return;
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked())
        return; // the UI / host is reading: try again next time
    lastTotal_ = t.totalNotes();
    const int mode = (int) pMode->load();
    view_.mode = mode;
    view_.target = t.target (mode);
    view_.total = t.totalNotes();
    view_.observedPitches = t.observedPitches();
    for (int p = 0; p < 128; ++p)
    {
        view_.count[p] = t.count (p);
        view_.measured[p] = t.count (p) > 0 ? t.measuredMedian (mode, p) : 0.0f;
    }
    t.serialise (blob_);
}

void BassLevelerProcessor::getView (View& out) const
{
    const juce::SpinLock::ScopedLockType sl (const_cast<juce::SpinLock&> (lock));
    out = view_;
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

    bool tableChanged = false;
    if (clearRequested.exchange (false))
    {
        core.table().clear();
        core.table().rebuild();
        tableChanged = true;
    }
    if (applyPendingRestore())
        tableChanged = true;

    bnl::Params p;
    p.learn = pLearn->load() > 0.5f;
    p.strength = pStrength->load();
    p.mode = (int) pMode->load();
    p.rider = pRider->load();
    p.focus2 = pFocus->load() > 0.5f;
    p.maxBoostDb = pBoost->load();
    p.maxCutDb = pCut->load();
    p.speedMs = pSpeed->load();
    core.setParams (p);

    core.process (buffer.getArrayOfWritePointers(), nCh, n);

    gainDb.store (core.currentGainDb());
    pitchHz.store (core.currentPitchHz());

    // share the learned table with the editor / state saving: at once when something changed, otherwise ~4x a second
    publishCountdown_ -= n;
    const bool learnJustEnded = lastLearn_ && ! p.learn;
    lastLearn_ = p.learn;
    if (tableChanged || learnJustEnded)
        publish (true);
    else if (publishCountdown_ <= 0)
    {
        publishCountdown_ = (int) (getSampleRate() * 0.25);
        publish (false);
    }
}

juce::AudioProcessorEditor* BassLevelerProcessor::createEditor()
{
    return new BassLevelerEditor (*this);
}

void BassLevelerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (! blob_.empty())
            state.setProperty ("table", juce::Base64::toBase64 (blob_.data(), blob_.size()), nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void BassLevelerProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    const auto b64 = state.getProperty ("table").toString();
    state.removeProperty ("table", nullptr);
    apvts.replaceState (state);
    bnl::PitchTable t;
    juce::MemoryOutputStream mo;
    if (b64.isNotEmpty() && juce::Base64::convertFromBase64 (mo, b64)
        && t.deserialise (static_cast<const uint8_t*> (mo.getData()), mo.getDataSize()))
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        pending_ = t;
        blob_.assign (static_cast<const uint8_t*> (mo.getData()), static_cast<const uint8_t*> (mo.getData()) + mo.getDataSize());
        view_ = {};
        view_.total = t.totalNotes();
        view_.observedPitches = t.observedPitches();
        restorePending = true;
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BassLevelerProcessor();
}

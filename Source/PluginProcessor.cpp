#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <BinaryData.h>

namespace
{
using Attr = juce::AudioParameterFloatAttributes;

float wrapDegrees (float a)
{
    a = std::fmod (a + 180.0f, 360.0f);
    if (a < 0.0f)
        a += 360.0f;
    return a - 180.0f;
}

spat::SpatParams lerpParams (const spat::SpatParams& a, const spat::SpatParams& b, float t)
{
    auto l = [t] (float x, float y) { return x + t * (y - x); };
    spat::SpatParams r;
    r.azimuthDeg = a.azimuthDeg + t * wrapDegrees (b.azimuthDeg - a.azimuthDeg); // shortest way round
    r.elevationDeg = l (a.elevationDeg, b.elevationDeg);
    r.distance = l (a.distance, b.distance);
    r.depth = l (a.depth, b.depth);
    r.focus = l (a.focus, b.focus);
    r.rear = l (a.rear, b.rear);
    r.room = l (a.room, b.room);
    r.decay = l (a.decay, b.decay);
    r.orbitHz = b.orbitHz;
    return r;
}
} // namespace

float OrbitPanAudioProcessor::distanceToMetres (float d01)
{
    return 0.3f * std::pow (50.0f, juce::jlimit (0.0f, 1.0f, d01));
}

juce::AudioProcessorValueTreeState::ParameterLayout OrbitPanAudioProcessor::createLayout()
{
    using Range = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // Azimuth spans two full turns (-360..360, 0 = front). The sound only depends on the angle
    // modulo 360, but this lets an automation lane (or a recorded drag) travel on through
    // "behind" without a jump, which a -180..180 range cannot do.
    auto azText = [] (float v, int) {
        const float a = wrapDegrees (v);
        if (std::abs (a) < 0.5f)
            return juce::String ("front");
        if (std::abs (a) > 179.5f)
            return juce::String ("behind");
        return juce::String (juce::roundToInt (std::abs (a))) + juce::String::fromUTF8 ("\xC2\xB0 ") + (a > 0 ? "R" : "L");
    };
    auto azParse = [] (const juce::String& t) {
        if (t.containsIgnoreCase ("behind"))
            return 180.0f;
        float v = t.getFloatValue();
        if (t.containsIgnoreCase ("L"))
            v = -std::abs (v);
        else if (t.containsIgnoreCase ("R"))
            v = std::abs (v);
        return v;
    };
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "azimuth", 1 }, "Azimuth", Range (-360.0f, 360.0f), 0.0f,
        Attr().withStringFromValueFunction (azText).withValueFromStringFunction (azParse)));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "elevation", 1 }, "Elevation", Range (-90.0f, 90.0f), 0.0f,
        Attr().withLabel (juce::String::fromUTF8 ("\xC2\xB0"))));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "distance", 1 }, "Distance", Range (0.0f, 1.0f), 0.25f,
        Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (distanceToMetres (v), 1) + " m"; })));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "depth", 1 }, "Depth", Range (-1.0f, 1.0f), 0.0f,
        Attr().withStringFromValueFunction ([] (float v, int) {
            return std::abs (v) < 0.005f ? juce::String ("neutral") : (v > 0 ? "close " : "far ") + juce::String (juce::roundToInt (std::abs (v) * 100.0f)) + "%";
        })));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "focus", 1 }, "Focus", Range (0.0f, 1.0f), 0.7f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "rear", 1 }, "Rear", Range (0.0f, 1.0f), 0.5f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "room", 1 }, "Room", Range (0.0f, 1.0f), 0.2f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "decay", 1 }, "Decay", Range (0.0f, 1.0f), 0.4f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "orbit", 1 }, "Orbit", Range (-2.0f, 2.0f), 0.0f, Attr().withLabel ("Hz")));

    return { p.begin(), p.end() };
}

OrbitPanAudioProcessor::OrbitPanAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pAzimuth = apvts.getRawParameterValue ("azimuth");
    pElevation = apvts.getRawParameterValue ("elevation");
    pDistance = apvts.getRawParameterValue ("distance");
    pDepth = apvts.getRawParameterValue ("depth");
    pFocus = apvts.getRawParameterValue ("focus");
    pRear = apvts.getRawParameterValue ("rear");
    pRoom = apvts.getRawParameterValue ("room");
    pDecay = apvts.getRawParameterValue ("decay");
    pOrbit = apvts.getRawParameterValue ("orbit");
}

bool OrbitPanAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in = layouts.getMainInputChannelSet();
    return out == juce::AudioChannelSet::stereo()
           && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void OrbitPanAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    tableLoaded = spatializer.prepare (sampleRate, samplesPerBlock, BinaryData::hrtf_d1_5deg_bin,
                                       (size_t) BinaryData::hrtf_d1_5deg_binSize);
    jassert (tableLoaded);
    scratch.setSize (2, std::max (1, samplesPerBlock), false, false, true);
    setLatencySamples (spatializer.latencySamples());
    havePrevParams = false;
    wasPlaying = false;
}

void OrbitPanAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();

    if (! tableLoaded)
    {
        buffer.clear();
        return;
    }

    spat::SpatParams target;
    target.azimuthDeg = pAzimuth->load();
    target.elevationDeg = pElevation->load();
    target.distance = pDistance->load();
    target.depth = pDepth->load();
    target.focus = pFocus->load();
    target.rear = pRear->load();
    target.room = pRoom->load();
    target.decay = pDecay->load();
    target.orbitHz = pOrbit->load();
    if (! havePrevParams)
    {
        prevParams = target;
        havePrevParams = true;
    }

    // Lock Orbit to the host timeline so playback, loops, scrubbing, Freeze and Export all put the
    // source in the same place at the same time (it free-runs only while the transport is stopped).
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            const bool playing = pos->getIsPlaying();
            if (playing)
                if (auto t = pos->getTimeInSamples())
                {
                    if (! wasPlaying || *t != expectedNextSample)
                        spatializer.setOrbitPhase ((float) std::fmod ((double) target.orbitHz * (double) *t / getSampleRate(), 1.0));
                    expectedNextSample = *t + numSamples;
                }
            wasPlaying = playing;
        }
    }

    // Parameters arrive once per host block; ramp from last block's value to this one over the block
    // (in 64-sample steps) so large buffers do not turn automation into audible steps.
    constexpr int kRampStep = 64;
    const int maxChunk = scratch.getNumSamples();
    for (int start = 0; start < numSamples; start += maxChunk)
    {
        const int n = std::min (maxChunk, numSamples - start);
        scratch.copyFrom (0, 0, buffer, 0, start, n);
        if (numIn > 1)
            scratch.copyFrom (1, 0, buffer, 1, start, n);
        for (int off = 0; off < n; off += kRampStep)
        {
            const int m = std::min (kRampStep, n - off);
            const auto ps = lerpParams (prevParams, target, (float) (start + off + m) / (float) numSamples);
            spatializer.process (scratch.getReadPointer (0) + off, numIn > 1 ? scratch.getReadPointer (1) + off : nullptr,
                                 buffer.getWritePointer (0) + start + off, buffer.getWritePointer (1) + start + off, m, ps);
        }
    }
    prevParams = target;

    heardAzimuth.store (spatializer.heardAzimuth());
    heardElevation.store (spatializer.heardElevation());
}

juce::AudioProcessorEditor* OrbitPanAudioProcessor::createEditor()
{
    return new OrbitPanEditor (*this);
}

void OrbitPanAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void OrbitPanAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OrbitPanAudioProcessor();
}

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <BinaryData.h>

namespace
{
using Attr = juce::AudioParameterFloatAttributes;
}

float OrbitPanAudioProcessor::distanceToMetres (float d01)
{
    return 0.3f * std::pow (50.0f, juce::jlimit (0.0f, 1.0f, d01));
}

juce::AudioProcessorValueTreeState::ParameterLayout OrbitPanAudioProcessor::createLayout()
{
    using Range = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "azimuth", 1 }, "Azimuth", Range (-180.0f, 180.0f, 0.1f), 0.0f,
        Attr().withLabel (juce::String::fromUTF8 ("\xC2\xB0"))));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "elevation", 1 }, "Elevation", Range (-90.0f, 90.0f, 0.1f), 0.0f,
        Attr().withLabel (juce::String::fromUTF8 ("\xC2\xB0"))));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "distance", 1 }, "Distance", Range (0.0f, 1.0f, 0.001f), 0.25f,
        Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (distanceToMetres (v), 1) + " m"; })));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "focus", 1 }, "Focus", Range (0.0f, 1.0f, 0.001f), 0.7f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "rear", 1 }, "Rear", Range (0.0f, 1.0f, 0.001f), 0.5f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "room", 1 }, "Room", Range (0.0f, 1.0f, 0.001f), 0.2f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "decay", 1 }, "Decay", Range (0.0f, 1.0f, 0.001f), 0.4f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "orbit", 1 }, "Orbit", Range (-2.0f, 2.0f, 0.01f), 0.0f, Attr().withLabel ("Hz")));

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

    spat::SpatParams p;
    p.azimuthDeg = pAzimuth->load();
    p.elevationDeg = pElevation->load();
    p.distance = pDistance->load();
    p.focus = pFocus->load();
    p.rear = pRear->load();
    p.room = pRoom->load();
    p.decay = pDecay->load();
    p.orbitHz = pOrbit->load();

    const int maxChunk = scratch.getNumSamples();
    for (int start = 0; start < numSamples; start += maxChunk)
    {
        const int n = std::min (maxChunk, numSamples - start);
        scratch.copyFrom (0, 0, buffer, 0, start, n);
        if (numIn > 1)
            scratch.copyFrom (1, 0, buffer, 1, start, n);
        spatializer.process (scratch.getReadPointer (0), numIn > 1 ? scratch.getReadPointer (1) : nullptr,
                             buffer.getWritePointer (0) + start, buffer.getWritePointer (1) + start, n, p);
    }

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

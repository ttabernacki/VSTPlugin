#pragma once

#include "dsp/Spatializer.h"

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

class OrbitPanAudioProcessor : public juce::AudioProcessor
{
public:
    OrbitPanAudioProcessor();
    ~OrbitPanAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "OrbitPan"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static float distanceToMetres (float d01);

    juce::AudioProcessorValueTreeState apvts;

    // Where the source is currently heard (after smoothing and orbit), for the UI.
    std::atomic<float> heardAzimuth { 0.0f }, heardElevation { 0.0f };

private:
    spat::Spatializer spatializer;
    juce::AudioBuffer<float> scratch; // copy of the input so we never process in place
    bool tableLoaded = false;
    float bypassHist[2][8] {}; // the dry signal, delayed by the reported latency while bypassed
    uint32_t bypassPos = 0;

    spat::SpatParams prevParams;
    bool havePrevParams = false, wasPlaying = false;
    juce::int64 expectedNextSample = 0;

    std::atomic<float>* pAzimuth = nullptr;
    std::atomic<float>* pElevation = nullptr;
    std::atomic<float>* pDistance = nullptr;
    std::atomic<float>* pDepth = nullptr;
    std::atomic<float>* pFocus = nullptr;
    std::atomic<float>* pRear = nullptr;
    std::atomic<float>* pRoom = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pOrbit = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrbitPanAudioProcessor)
};

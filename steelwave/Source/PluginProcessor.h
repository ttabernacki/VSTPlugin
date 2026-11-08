#pragma once

#include "../core/SteelEngine.h"

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

class SteelwaveProcessor : public juce::AudioProcessor
{
public:
    SteelwaveProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Steelwave"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; } // strings ring a long time

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::StringArray setNames();
    static juce::StringArray keyNames();

    juce::AudioProcessorValueTreeState apvts;

    // --- for the editor (written on the audio thread; a stale read is harmless) ---
    std::atomic<int> uiSet { 0 }, uiSlot { -1 }, uiBar { 0 }, uiSounding { 0 };
    std::atomic<float> uiWheel { 0.0f }, uiVolume { 1.0f };
    std::array<std::atomic<float>, sw::kStrings> uiSemis {};

private:
    sw::EngineParams readParams() const;
    void handle (const juce::MidiMessage&);

    sw::SteelEngine engine;
    bool prepared_ = false;
    int lastSetParam_ = -1;
    std::atomic<float>*pSet = nullptr, *pKey = nullptr, *pGlide = nullptr, *pStrum = nullptr, *pHard = nullptr, *pBright = nullptr,
        *pSustain = nullptr, *pNoise = nullptr, *pVibRate = nullptr, *pVibDepth = nullptr, *pSym = nullptr, *pPickup = nullptr,
        *pVolCc = nullptr, *pLevel = nullptr, *pBehind = nullptr, *pVolType = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SteelwaveProcessor)
};

#pragma once

#include "../core/SteelEngine.h"
#include "AdvancedParams.h"
#include "Presets.h"

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

    int getNumPrograms() override { return (int) swpresets::presets().size(); }
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override
    {
        return juce::isPositiveAndBelow (index, getNumPrograms()) ? juce::String (swpresets::presets()[(size_t) index].name) : juce::String();
    }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::StringArray setNames();
    static juce::StringArray keyNames();

    juce::AudioProcessorValueTreeState apvts;

    // --- for the editor (written on the audio thread; a stale read is harmless) ---
    std::atomic<int> uiSet { 0 }, uiSlot { -1 }, uiBar { 0 }, uiSounding { 0 };

    // --- MIDI learn: a CC moves a parameter (index into getParameters()); the CC is then not passed on ---
    std::array<std::atomic<int>, 128> ccMap;
    std::atomic<int> learnTarget { -1 }; // parameter waiting for a CC (-1: not learning)
    int paramIndex (const juce::String& id) const;
    void clearMidiMap()
    {
        for (auto& c : ccMap)
            c = -1;
    }
    std::atomic<int> currentProgram { 0 };
    std::atomic<float> uiWheel { 0.0f }, uiVolume { 1.0f };
    std::array<std::atomic<float>, sw::kStrings> uiSemis {};
    std::array<std::atomic<float>, sw::kControls> uiTravel {}; // pedals and levers, 0..1

private:
    sw::EngineParams readParams() const;
    void handle (const juce::MidiMessage&);
    void applyPreset (int index);
    std::atomic<int> pendingProgram { -1 };

    sw::SteelEngine engine;
    bool prepared_ = false;
    int lastSetParam_ = -1;
    std::atomic<float>*pSet = nullptr, *pKey = nullptr, *pGlide = nullptr, *pStrum = nullptr, *pHard = nullptr, *pBright = nullptr,
        *pSustain = nullptr, *pNoise = nullptr, *pVibRate = nullptr, *pVibDepth = nullptr, *pSym = nullptr, *pPickup = nullptr,
        *pVolCc = nullptr, *pLevel = nullptr, *pBehind = nullptr, *pVolType = nullptr, *pPedalKeys = nullptr,
        *pPedalSpeed = nullptr, *pModWheel = nullptr, *pAftertouch = nullptr, *pSwellBelow = nullptr, *pSwellTime = nullptr,
        *pAutoVib = nullptr, *pVibDelay = nullptr;
    std::vector<std::atomic<float>*> pAdv; // in swadv::params() order

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SteelwaveProcessor)
};

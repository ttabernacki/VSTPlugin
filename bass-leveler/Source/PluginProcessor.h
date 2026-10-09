#pragma once

#include "../core/Leveler.h"

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

class BassLevelerProcessor : public juce::AudioProcessor
{
public:
    BassLevelerProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Bass Note Leveler"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return latencySeconds.load(); } // the delayed audio still has to come out

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

    // --- for the editor (thread-safe; a torn read of the display data is harmless) ---
    static constexpr int kRecent = 32;
    int getRecent (bnl::RecentNote* out) const
    {
        const int n = std::min (recentCount.load(), kRecent);
        for (int i = 0; i < n; ++i)
            out[i] = { recMidi[i].load(), recDev[i].load(), recCorr[i].load() };
        return n;
    }
    std::atomic<float> gainDb { 0.0f }, pitchHz { 0.0f };
    std::atomic<double> latencySeconds { 0.13 };

private:
    bnl::Leveler core;
    bool prepared_ = false, bypassNow_ = false;
    std::atomic<float> recMidi[kRecent], recDev[kRecent], recCorr[kRecent];
    std::atomic<int> recentCount { 0 };

    std::atomic<float>*pAmount = nullptr, *pBoost = nullptr, *pCut = nullptr, *pSpeed = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassLevelerProcessor)
};

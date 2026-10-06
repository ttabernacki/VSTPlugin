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
    using juce::AudioProcessor::processBlock;

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

    // --- for the editor (all thread-safe) ---
    struct View
    {
        float measured[128] {};
        int count[128] {};
        float target = 0.0f;
        int total = 0, observedPitches = 0, mode = 0;
    };
    void getView (View& out) const;
    void clearTable() { clearRequested = true; }
    std::atomic<float> gainDb { 0.0f }, pitchHz { 0.0f };
    std::atomic<double> latencySeconds { 0.13 };

private:
    void publish (bool force);
    bool applyPendingRestore();

    bnl::Leveler core;
    juce::SpinLock lock;
    // published copies of the learned table (guarded by `lock`)
    View view_;
    std::vector<uint8_t> blob_;
    bnl::PitchTable pending_;
    std::atomic<bool> restorePending { false }, clearRequested { false };
    bool prepared_ = false, lastLearn_ = false;
    int lastTotal_ = -1, publishCountdown_ = 0;

    std::atomic<float>*pLearn = nullptr, *pStrength = nullptr, *pMode = nullptr, *pRider = nullptr, *pFocus = nullptr,
                       *pBoost = nullptr, *pCut = nullptr, *pSpeed = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassLevelerProcessor)
};

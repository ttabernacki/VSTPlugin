#pragma once

#include "../core/Definition.h"

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

class LowEndDefinitionProcessor : public juce::AudioProcessor
{
public:
    LowEndDefinitionProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Low-End Definition"; }
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
    static constexpr int kHist = 160; // about 5 seconds at one point per 30 ms
    struct Point
    {
        float defIn, defOut, trans;
    };
    int getHistory (Point* out) const // oldest first; defIn < 0 = nothing measured then
    {
        const int head = histHead.load();
        for (int i = 0; i < kHist; ++i)
        {
            const int k = (head + i) % kHist;
            out[i] = { hIn[k].load(), hOut[k].load(), hTrans[k].load() };
        }
        return kHist;
    }
    std::atomic<float> pitchHz { 0.0f }, bellDb { 0.0f }, defIn { -1.0f }, defOut { -1.0f }, transDb { 0.0f };
    std::atomic<double> latencySeconds { 0.07 };

private:
    led::Definition core;
    bool prepared_ = false;
    int sinceHist_ = 0, histEvery_ = 1440;
    std::atomic<float> hIn[kHist], hOut[kHist], hTrans[kHist];
    std::atomic<int> histHead { 0 };

    std::atomic<float>*pContrast = nullptr, *pPunch = nullptr, *pSustain = nullptr, *pRange = nullptr, *pMatch = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LowEndDefinitionProcessor)
};

#pragma once

#include "../core/NoteSpace.h"

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

class NoteSpaceProcessor : public juce::AudioProcessor
{
public:
    NoteSpaceProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Note Space"; }
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

    // --- for the editor (thread-safe; a torn read of display data is harmless) ---
    static constexpr int kH = nsp::NoteSpace::kH;
    std::atomic<float> hIn[kH], hOut[kH];
    std::atomic<float> pitchHz { 0.0f }, voicing { 0.0f }, noteResIn { 0.0f }, noteResOut { 0.0f }, residualDb { 0.0f }, duckDb { 0.0f }, dynDb { 0.0f };
    std::atomic<bool> scConnected { false };
    std::atomic<double> latencySeconds { 0.11 };

private:
    static nsp::Params neutralParams();
    nsp::NoteSpace core;
    bool prepared_ = false, bypassNow_ = false;
    std::atomic<float>*pContrast = nullptr, *pTone = nullptr, *pFund = nullptr, *pRepair = nullptr, *pTranslate = nullptr, *pRange = nullptr, *pPunch = nullptr, *pSustain = nullptr, *pKick = nullptr;
    juce::AudioBuffer<float> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteSpaceProcessor)
};

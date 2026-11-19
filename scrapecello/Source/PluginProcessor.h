#pragma once

#include "../core/ScrapeCello.h"

#include <juce_audio_processors/juce_audio_processors.h>

// Scrape Cello: the bowed-string engine as an instrument. MIDI goes to the engine as it comes (MPE included).
class ScrapeCelloProcessor : public juce::AudioProcessor
{
public:
    ScrapeCelloProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Scrape Cello"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    const sc::ScrapeCello& cello() const { return *cello_; } // tests, display

private:
    sc::CelloParams readParams() const;
    std::unique_ptr<sc::ScrapeCello> cello_ = std::make_unique<sc::ScrapeCello>();
    bool prepared_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScrapeCelloProcessor)
};

#pragma once

#include "../core/ReedOrgan.h"

#include <juce_audio_processors/juce_audio_processors.h>

// Pump Organ: the reed organ engine as an instrument. MIDI goes to the engine as it comes (MPE included);
// the volume pedal's CC is taken here and scaled by the Wind knob, so the organ sounds with no pedal at all.
class PumpOrganProcessor : public juce::AudioProcessor
{
public:
    PumpOrganProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Pump Organ"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    const ro::ReedOrgan& organ() const { return *organ_; } // tests, display

private:
    ro::OrganParams readParams() const;
    void updatePedal();
    std::unique_ptr<ro::ReedOrgan> organ_ = std::make_unique<ro::ReedOrgan>();
    bool prepared_ = false;
    double pedalCcValue_ = 1.0; // the expression pedal, until one is moved: all the way down (full)
    int pedalCc_ = 11;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PumpOrganProcessor)
};

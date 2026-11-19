#pragma once

#include "../core/ScrapeCello.h"
#include "Presets.h"

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

// Scrape Cello: the bowed-string engine as an instrument. MIDI is fitted to the player's controllers
// (controller setup, the velocity curve, MIDI learn) and then goes to the engine (MPE included).
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

    int getNumPrograms() override { return (int) scpresets::presets().size(); }
    int getCurrentProgram() override { return juce::jmax (0, currentProgram.load()); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override
    {
        return juce::isPositiveAndBelow (index, getNumPrograms()) ? juce::String (scpresets::presets()[(size_t) index].name) : juce::String();
    }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    const sc::ScrapeCello& cello() const { return *cello_; } // tests

    // --- for the editor (written on the audio thread; a stale read is harmless) ---
    struct UiVoice { std::atomic<bool> on { false }, held { false }; std::atomic<float> pitch { 0 }, position { 0 }, pressure { 0 }, level { 0 }, dir { 1 }; };
    std::array<UiVoice, sc::ScrapeCello::kVoices> uiVoices;
    std::atomic<float> uiCpu { 0.0f }, uiPedal { 1.0f }, uiMod { 0.0f };

    // --- presets: factory (programs) and the player's own (files) ---
    std::atomic<int> currentProgram { 0 };
    juce::String userPreset;
    static juce::File settingsDir(); // %APPDATA%/Scrape Cello (SCRAPECELLO_SETTINGS_DIR overrides, for tests)
    static juce::File userPresetDir();
    juce::StringArray userPresetNames() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);
    juce::String presetName() const;
    // favourites, A/B, audition
    juce::StringArray favourites() const;
    bool isFavourite (const juce::String& name) const;
    void toggleFavourite (const juce::String& name);
    int abActive() const { return abActive_; }
    void abSwitch (int slot);
    void abCopyToOther();
    void audition() { auditionRequest = true; }
    std::atomic<bool> auditionRequest { false };

    // --- MIDI learn: a CC moves a parameter (index into getParameters()) instead of its usual job ---
    std::array<std::atomic<int>, 128> ccMap;
    std::atomic<int> learnTarget { -1 };
    int paramIndex (const juce::String& id) const;
    void clearMidiMap() { for (auto& c : ccMap) c = -1; }

    // --- controller setup: what the player's hardware really sends (one file, shared by every instance) ---
    struct ControllerCal
    {
        int pitchCc = -1, modCc = 1, exprCc = -1;
        float pitchLo = -1, pitchHi = 1, pitchMid = 0, modLo = 0, modHi = 1, exprLo = 0, exprHi = 1, atHi = 1, velLo = 0, velHi = 1;
        bool asked = false;
    };
    ControllerCal calibration() const;
    void setCalibration (const ControllerCal&, bool save = true);
    enum WizardStep { WizPitch, WizMod, WizExpr, WizAftertouch, WizVelocity, kWizSteps };
    std::atomic<int> wizardStep { -1 };
    static constexpr int kSrcPitch = 128, kSrcPressure = 129, kSrcVelocity = 130, kSources = 131;
    std::array<std::atomic<float>, kSources> capLo, capHi, capLast;
    std::atomic<int> capNotes { 0 };
    void startCapture (int step);
    int bestSource (int step, const std::vector<int>& exclude = {}) const;
    void finishStep (int step, int source);

private:
    sc::CelloParams readParams() const;
    void handle (const juce::MidiMessage&);
    void send (std::initializer_list<int> bytes);
    void applyPreset (int index);
    std::unique_ptr<juce::XmlElement> captureSound() const;
    void applySound (const juce::XmlElement&);
    void loadCalibration();
    void capture (int source, float v);
    void buildPhrase();

    std::unique_ptr<sc::ScrapeCello> cello_ = std::make_unique<sc::ScrapeCello>();
    bool prepared_ = false;
    double sampleRate_ = 48000.0, cpuSmooth_ = 0.0;
    std::atomic<int> pendingProgram { -1 };
    std::unique_ptr<juce::XmlElement> abSlot_[2];
    int abActive_ = 0;
    mutable juce::SpinLock calLock_;
    ControllerCal calMsg_, calAudio_;
    std::atomic<int> calVersion_ { 0 };
    int calAudioVersion_ = -1;
    // audition phrase: raw MIDI events, in samples from its start
    struct PhraseEvent { int64_t at; uint8_t m[3]; int n; };
    std::vector<PhraseEvent> phrase_;
    size_t phraseNext_ = 0;
    int64_t phraseClock_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScrapeCelloProcessor)
};

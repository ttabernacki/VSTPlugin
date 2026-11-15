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
    int getCurrentProgram() override { return juce::jmax (0, currentProgram.load()); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override
    {
        return juce::isPositiveAndBelow (index, getNumPrograms()) ? juce::String (swpresets::presets()[(size_t) index].name) : juce::String();
    }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::StringArray modeNames() { return { "Smart notes", "Chords", "Single notes" }; }
    static juce::StringArray voicingNames();
    static juce::StringArray scaleNames();
    // the engine's set (0-7 chord voicings, 8-11 single-note sets, 12 smart notes) from Mode, Voicing and
    // Scale, and the other way (message thread)
    int engineSet() const;
    void setEngineSet (int set);
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
    std::atomic<int> currentProgram { 0 }; // factory preset index, or -1 for a user preset / none
    std::atomic<int> uiSmartRoot { -1 }, uiSmartQual { 0 };

    // --- presets: factory (programs) and the user's own (files) ---
    static int calibratedProgram();
    static juce::File userPresetDir();
    juce::StringArray userPresetNames() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);
    juce::String presetName() const; // what the browser shows
    juce::String userPreset;         // the user preset loaded, if any (message thread)
    std::atomic<float> uiWheel { 0.0f }, uiVolume { 1.0f };
    std::array<std::atomic<float>, sw::kStrings> uiSemis {};
    std::array<std::atomic<float>, sw::kControls> uiTravel {}; // pedals and levers, 0..1
    std::atomic<int> uiMoveCombo { -1 }, uiMoveFrets { 0 }, uiPatternStep { -1 };
    std::atomic<float> uiBpm { 120.0f }, uiCpu { 0.0f };
    std::atomic<bool> uiPlaying { false };

    // --- controller setup: what the player's hardware really sends (one file, shared by every instance) ---
    struct ControllerCal
    {
        int pitchCc = -1;                          // a CC that is the pitch slider (-1: the pitch wheel itself)
        int modCc = 1;                             // the CC the mod slider sends (treated as CC1)
        int exprCc = -1;                           // the CC the expression pedal sends (treated as the volume pedal's CC)
        float pitchLo = -1, pitchHi = 1, pitchMid = 0; // the slider's real travel, -1..1
        float modLo = 0, modHi = 1, exprLo = 0, exprHi = 1, atHi = 1, velLo = 0, velHi = 1;
        bool asked = false;                        // the setup has been offered (finished or skipped)
    };
    ControllerCal calibration() const;
    void setCalibration (const ControllerCal&, bool save = true);
    static juce::File settingsDir(); // %APPDATA%/Steelwave (STEELWAVE_SETTINGS_DIR overrides, for tests)
    // what the setup is listening for: -1 off; while on, every source's travel is recorded
    enum WizardStep { WizPitch, WizMod, WizExpr, WizAftertouch, WizVelocity, kWizSteps };
    std::atomic<int> wizardStep { -1 };
    static constexpr int kSrcPitch = 128, kSrcPressure = 129, kSrcVelocity = 130, kSources = 131;
    std::array<std::atomic<float>, kSources> capLo, capHi, capLast;
    std::atomic<int> capNotes { 0 };
    void startCapture (int step);
    int bestSource (int step, const std::vector<int>& exclude = {}) const; // the source moved furthest, or -1
    void finishStep (int step, int source); // store what was found

    // --- A/B compare: two sounds to flip between ---
    int abActive() const { return abActive_; }
    void abSwitch (int slot);
    void abCopyToOther();

    // --- favourites (one file, shared) ---
    juce::StringArray favourites() const;
    bool isFavourite (const juce::String& name) const;
    void toggleFavourite (const juce::String& name);

    // --- audition: a short phrase in the current mode, played through the instrument ---
    void audition() { auditionRequest = true; }
    std::atomic<bool> auditionRequest { false };

private:
    sw::EngineParams readParams() const;
    void handle (const juce::MidiMessage&);
    void applyPreset (int index);
    std::atomic<int> pendingProgram { -1 };

    sw::SteelEngine engine;
    bool prepared_ = false;
    double sampleRate_ = 48000.0, bpm_ = 120.0, cpuSmooth_ = 0.0;
    // controller calibration: the message thread writes calMsg_ under the lock; the audio thread copies it
    mutable juce::SpinLock calLock_;
    ControllerCal calMsg_, calAudio_;
    std::atomic<int> calVersion_ { 0 };
    int calAudioVersion_ = -1;
    void loadCalibration();
    void capture (int source, float v);
    // A/B
    std::unique_ptr<juce::XmlElement> abSlot_[2];
    int abActive_ = 0;
    std::unique_ptr<juce::XmlElement> captureSound() const;
    void applySound (const juce::XmlElement&);
    // audition phrase: note events, in samples from its start
    struct PhraseEvent { int64_t at; int note; float vel; };
    std::vector<PhraseEvent> phrase_;
    size_t phraseNext_ = 0;
    int64_t phraseClock_ = 0;
    void buildPhrase();
    int lastSetParam_ = -1;
    std::atomic<float>*pMode = nullptr, *pVoicing = nullptr, *pScale = nullptr, *pKey = nullptr, *pGlide = nullptr, *pStrum = nullptr, *pHard = nullptr, *pBright = nullptr,
        *pSustain = nullptr, *pNoise = nullptr, *pVibRate = nullptr, *pVibDepth = nullptr, *pSym = nullptr, *pPickup = nullptr,
        *pVolCc = nullptr, *pLevel = nullptr, *pBehind = nullptr, *pVolType = nullptr, *pPedalKeys = nullptr,
        *pPedalSpeed = nullptr, *pModWheel = nullptr, *pAftertouch = nullptr, *pSwellBelow = nullptr, *pSwellTime = nullptr,
        *pAutoVib = nullptr, *pVibDelay = nullptr;
    std::vector<std::atomic<float>*> pAdv; // in swadv::params() order

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SteelwaveProcessor)
};

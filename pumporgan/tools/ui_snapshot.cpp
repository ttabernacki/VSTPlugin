// Renders the real Pump Organ editor offscreen to PNGs (no display needed) for visual checks.
// Usage: pumporgan_ui_snapshot out_dir
#include "../Source/PluginEditor.h"

#include <cstdlib>

int main (int argc, char** argv)
{
    if (argc < 2)
        return 2;
    juce::ScopedJuceInitialiser_GUI init;
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pumporgan_ui_settings");
    dir.deleteRecursively();
    dir.createDirectory();
#ifdef _WIN32
    _putenv_s ("PUMPORGAN_SETTINGS_DIR", dir.getFullPathName().toRawUTF8());
#else
    setenv ("PUMPORGAN_SETTINGS_DIR", dir.getFullPathName().toRawUTF8(), 1);
#endif
    dir.getChildFile ("Controller.xml").replaceWithText ("<PumpOrganController asked=\"1\"/>");
    struct Shot { const char* name; int page; bool setup; int preset; };
    const Shot shots[] = { { "organ_front", 0, false, 0 }, { "organ_parlour", 0, false, 4 }, { "organ_back", 1, false, 0 }, { "organ_setup", 0, true, 0 } };
    for (const auto& s : shots)
    {
        PumpOrganProcessor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        proc.setCurrentProgram (s.preset);
        if (s.preset == 0)
        {
            proc.apvts.getParameter ("melody")->setValueNotifyingHost (1.0f);
            proc.apvts.getParameter ("neglect")->setValueNotifyingHost (1.0f);
        }
        juce::AudioBuffer<float> buf (2, 512);
        for (int i = 0; i < 200; ++i)
        {
            juce::MidiBuffer midi;
            if (i == 1)
            {
                for (int n : { 48, 55, 64, 67 }) // C3 G3 E4 G4
                    midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 70), 0); // the swell lever, half open
            }
            if (i == 120)
                for (int n : { 60, 62, 65, 69, 71, 74, 77 }) // tapped and let go: some cipher
                    midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 90), 0);
            if (i == 140)
                for (int n : { 60, 62, 65, 69, 71, 74, 77 })
                    midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            buf.clear();
            proc.processBlock (buf, midi);
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (PumpOrganEditor::kW, PumpOrganEditor::kH);
        if (auto* pe = dynamic_cast<PumpOrganEditor*> (ed.get()))
        {
            pe->showPage (s.page);
            pe->showSetup (s.setup);
        }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (juce::String (argv[1]) + "/" + s.name + ".png");
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::FileOutputStream out (f);
        juce::PNGImageFormat png;
        png.writeImageToStream (img, out);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    }
    return 0;
}

// Renders the real Scrape Cello editor offscreen to PNGs (no display needed) for visual checks.
// Usage: scrapecello_ui_snapshot out_dir
#include "../Source/PluginEditor.h"

#include <cstdlib>

int main (int argc, char** argv)
{
    if (argc < 2)
        return 2;
    juce::ScopedJuceInitialiser_GUI init;
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("scrapecello_ui_settings");
    dir.deleteRecursively();
    dir.createDirectory();
#ifdef _WIN32
    _putenv_s ("SCRAPECELLO_SETTINGS_DIR", dir.getFullPathName().toRawUTF8());
#else
    setenv ("SCRAPECELLO_SETTINGS_DIR", dir.getFullPathName().toRawUTF8(), 1);
#endif
    dir.getChildFile ("Controller.xml").replaceWithText ("<ScrapeCelloController asked=\"1\"/>");
    struct Shot { const char* name; int page; bool setup; };
    const Shot shots[] = { { "cello_play", 0, false }, { "cello_settings", 1, false }, { "cello_setup", 0, true } };
    for (const auto& s : shots)
    {
        ScrapeCelloProcessor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buf (2, 512);
        for (int i = 0; i < 120; ++i)
        {
            juce::MidiBuffer midi;
            if (i == 1)
            {
                midi.addEvent (juce::MidiMessage::noteOn (2, 38, (juce::uint8) 100), 0); // D2 clean
                midi.addEvent (juce::MidiMessage::noteOn (3, 45, (juce::uint8) 100), 0); // A2 on the edge
                midi.addEvent (juce::MidiMessage::noteOn (4, 57, (juce::uint8) 100), 0); // A3 grinding
                midi.addEvent (juce::MidiMessage::channelPressureChange (3, 64), 0);
                midi.addEvent (juce::MidiMessage::channelPressureChange (4, 127), 0);
                midi.addEvent (juce::MidiMessage::controllerEvent (4, 74, 110), 0); // its bow near the bridge
            }
            buf.clear();
            proc.processBlock (buf, midi);
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (ScrapeCelloEditor::kW, ScrapeCelloEditor::kH);
        if (auto* se = dynamic_cast<ScrapeCelloEditor*> (ed.get()))
        {
            se->showPage (s.page);
            se->showSetup (s.setup);
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

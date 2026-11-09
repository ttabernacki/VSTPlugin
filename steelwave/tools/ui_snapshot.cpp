// Renders the real Steelwave editor offscreen to PNGs (no display needed) for visual checks.
// Usage: steelwave_ui_snapshot out_dir
#include "../Source/PluginEditor.h"

int main (int argc, char** argv)
{
    if (argc < 2)
        return 2;
    juce::ScopedJuceInitialiser_GUI init;
    struct Shot { const char* name; int set; int note; int wheel; int page; };
    const Shot shots[] = { { "ui_chord_bent", 0, 60, 12000, 0 }, { "ui_single", 8, 52, 8192, 0 }, { "ui_pad_v", 7, 67, 4000, 0 },
                           { "ui_controls", 0, 60, 8192, 1 }, { "ui_advanced", 0, 60, 8192, 2 } };
    for (const auto& s : shots)
    {
        SteelwaveProcessor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        auto* set = proc.apvts.getParameter ("set");
        set->setValueNotifyingHost (set->convertTo0to1 ((float) s.set));
        juce::AudioBuffer<float> buf (2, 512);
        for (int i = 0; i < 60; ++i)
        {
            juce::MidiBuffer midi;
            if (i == 1) midi.addEvent (juce::MidiMessage::noteOn (1, s.note, (juce::uint8) 100), 0);
            if (i == 2) midi.addEvent (juce::MidiMessage::noteOn (1, sw::kPedalKeyLo + sw::PedB, (juce::uint8) 110), 0); // B pedal key down
            if (i == 20) midi.addEvent (juce::MidiMessage::pitchWheel (1, s.wheel), 0);
            buf.clear();
            proc.processBlock (buf, midi);
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (900, 520);
        if (auto* se = dynamic_cast<SteelwaveEditor*> (ed.get()))
            se->showPage (s.page);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (juce::String (argv[1]) + "/" + s.name + ".png");
        f.deleteFile();
        juce::FileOutputStream out (f);
        juce::PNGImageFormat png;
        png.writeImageToStream (img, out);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    }
    return 0;
}

// Renders the real plugin editor offscreen to PNGs (no display needed) for visual checks.
// Usage: ui_snapshot out_dir
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

struct Shot
{
    const char* name;
    float az, el, dist, depth, orbit;
};

int main (int argc, char** argv)
{
    if (argc < 2)
        return 2;
    juce::ScopedJuceInitialiser_GUI init;

    const Shot shots[] = {
        { "ui_default", 0, 0, 0.25f, 0, 0 },
        { "ui_behind_right_up", 145, 40, 0.55f, 0, 0 },
        { "ui_close_left_down", -70, -30, 0.05f, 0.9f, 0 },
        { "ui_far_front", 10, 5, 0.9f, -0.9f, 0 },
    };
    for (const auto& s : shots)
    {
        OrbitPanAudioProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        auto set = [&] (const char* id, float v) {
            auto* p = proc.apvts.getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        set ("azimuth", s.az);
        set ("elevation", s.el);
        set ("distance", s.dist);
        set ("depth", s.depth);
        set ("orbit", s.orbit);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int i = 0; i < 40; ++i) // let the smoothing settle so the UI shows the heard position
        {
            buf.clear();
            proc.processBlock (buf, midi);
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (780, 520);
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

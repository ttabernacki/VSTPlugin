#include "PluginEditor.h"

namespace
{
using namespace bassui;

juce::String noteName (int midi)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[midi % 12]) + juce::String (midi / 12 - 1);
}
} // namespace

BassLevelerEditor::BassLevelerEditor (BassLevelerProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    amount.setup (*this, proc.apvts, "amount", "AMOUNT");
    boost.setup (*this, proc.apvts, "boost", "MAX BOOST");
    cut.setup (*this, proc.apvts, "cut", "MAX CUT");
    speed.setup (*this, proc.apvts, "speed", "SPEED");
    setResizable (true, true);
    setResizeLimits (560, 380, 1100, 700);
    getConstrainer()->setFixedAspectRatio (680.0 / 440.0);
    setSize (680, 440);
    startTimerHz (15);
}

BassLevelerEditor::~BassLevelerEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void BassLevelerEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    auto head = r.removeFromTop (34);
    head.removeFromLeft (190); // title
    r.removeFromTop (8);
    auto knobs = r.removeFromTop (118);
    const int w = knobs.getWidth() / 4;
    for (auto* k : { &amount, &boost, &cut, &speed })
    {
        auto cell = knobs.removeFromLeft (w);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell);
    }
    r.removeFromTop (6);
    chart = r;
}

void BassLevelerEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("BASS NOTE LEVELER", 14, 14, 170, 26, juce::Justification::centredLeft);

    bnl::RecentNote notes[BassLevelerProcessor::kRecent];
    const int n = proc.getRecent (notes);
    g.setColour (kPanel);
    g.fillRoundedRectangle (chart.toFloat(), 8.0f);
    auto area = chart.reduced (12, 10);
    auto footer = area.removeFromBottom (18);
    auto labels = area.removeFromBottom (14);

    if (n == 0)
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("Just play. Each note is measured while it is still in the look-ahead, compared with the\n"
                          "typical level of the recent notes, and evened out before you hear it.",
                          area, juce::Justification::centred, 3);
    }
    else
    {
        float range = 3.0f;
        for (int i = 0; i < n; ++i)
            range = std::max (range, std::fabs (notes[i].deviationDb) + 1.0f);
        const float midY = (float) area.getCentreY(), halfH = 0.5f * (float) area.getHeight();
        auto Y = [&] (float db) { return midY - db / range * halfH; };
        g.setColour (kLine);
        g.drawHorizontalLine ((int) midY, (float) area.getX(), (float) area.getRight());
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (kDim);
        g.drawText ("louder than typical", area.getX() + 2, area.getY(), 120, 12, juce::Justification::left);
        g.drawText ("quieter", area.getX() + 2, area.getBottom() - 12, 80, 12, juce::Justification::left);
        const int slots = BassLevelerProcessor::kRecent;
        const float bw = (float) area.getWidth() / (float) slots;
        for (int i = 0; i < n; ++i)
        {
            const float x = (float) area.getX() + (float) (slots - n + i) * bw;
            const float dev = notes[i].deviationDb, after = dev + notes[i].correctionDb;
            auto bar = [&] (float db, juce::Colour c, float x0, float w) {
                g.setColour (c);
                const float y = Y (db);
                g.fillRoundedRectangle (x0, std::min (y, midY), w, std::max (1.5f, std::fabs (y - midY)), 1.5f);
            };
            bar (dev, kDim.withAlpha (0.8f), x + 1.0f, std::max (1.0f, bw * 0.5f - 1.0f));
            bar (after, kAccent, x + bw * 0.5f, std::max (1.0f, bw * 0.5f - 1.0f));
            if (bw > 15.0f || i % 2 == 0)
            {
                g.setColour (kDim);
                g.drawText (noteName ((int) std::lround (notes[i].midi)), (int) x - 6, labels.getY(), (int) bw + 12, labels.getHeight(),
                            juce::Justification::centred);
            }
        }
    }

    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    const double latMs = proc.latencySeconds.load() * 1000.0;
    g.drawText (juce::String ("grey = before, teal = after leveling    |    now ")
                    + (proc.pitchHz.load() > 20.0f ? juce::String (proc.pitchHz.load(), 1) + " Hz" : juce::String ("-")) + ", "
                    + juce::String (proc.gainDb.load(), 1) + " dB    |    latency " + juce::String ((int) std::lround (latMs)) + " ms",
                footer, juce::Justification::centredLeft);
}

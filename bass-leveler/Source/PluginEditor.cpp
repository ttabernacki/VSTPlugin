#include "PluginEditor.h"

namespace
{
const juce::Colour kBg (0xff101418), kPanel (0xff171d23), kPanel2 (0xff1d252d), kLine (0xff2c3640), kAccent (0xff4fd1c5),
    kWarm (0xfff6ad55), kText (0xffdde6ee), kDim (0xff7b8a97);

juce::String noteName (int midi)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[midi % 12]) + juce::String (midi / 12 - 1);
}
} // namespace

BnlLookAndFeel::BnlLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, kPanel2);
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::outlineColourId, kLine);
    setColour (juce::ComboBox::arrowColourId, kDim);
    setColour (juce::PopupMenu::backgroundColourId, kPanel2);
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent.darker (0.5f));
    setColour (juce::TextButton::textColourOffId, kText);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

void BnlLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                                       juce::Slider&)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = 0.5f * std::min (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const float arcR = r - 3.0f, angle = startAngle + pos * (endAngle - startAngle);
    juce::Path track, value;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
    g.setColour (kLine);
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (kAccent);
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float ir = arcR - 7.0f;
    g.setColour (kPanel2);
    g.fillEllipse (c.x - ir, c.y - ir, 2 * ir, 2 * ir);
    g.setColour (kText);
    g.drawLine (c.x + ir * 0.35f * std::sin (angle), c.y - ir * 0.35f * std::cos (angle), c.x + ir * 0.9f * std::sin (angle),
                c.y - ir * 0.9f * std::cos (angle), 2.0f);
}

void BnlLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    juce::Colour fill = on ? kAccent.darker (0.4f) : kPanel2;
    if (highlighted)
        fill = fill.brighter (0.1f);
    if (down)
        fill = fill.brighter (0.2f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? fill.brighter (0.3f) : kLine);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void BassLevelerEditor::addKnob (Knob& k, const juce::String& id, const juce::String& text)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    k.slider.setColour (juce::Slider::textBoxTextColourId, kText);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    auto* prm = proc.apvts.getParameter (id);
    k.slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    addAndMakeVisible (k.slider);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, kDim);
    k.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
}

BassLevelerEditor::BassLevelerEditor (BassLevelerProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    focus.setClickingTogglesState (true);
    addAndMakeVisible (focus);
    mode.addItemList ({ "Balance", "Level" }, 1);
    addAndMakeVisible (mode);
    focusAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "focus", focus);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mode", mode);
    addKnob (amount, "amount", "AMOUNT");
    addKnob (boost, "boost", "MAX BOOST");
    addKnob (cut, "cut", "MAX CUT");
    addKnob (speed, "speed", "SPEED");
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
    mode.setBounds (head.removeFromLeft (110));
    head.removeFromLeft (8);
    focus.setBounds (head.removeFromLeft (130));
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

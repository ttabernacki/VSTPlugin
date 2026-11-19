#include "PluginEditor.h"

#include <cstring>

using namespace organui;

namespace
{
const float kPi = juce::MathConstants<float>::pi;

juce::Colour inkDim() { return kInk.withAlpha (0.62f); }

// a raised brass plate with a fine bevel
void brassPlate (juce::Graphics& g, juce::Rectangle<float> r, float corner = 4.0f)
{
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), corner);
    juce::ColourGradient grad (kBrassLight, r.getX(), r.getY(), kBrassDark, r.getX(), r.getBottom(), false);
    grad.addColour (0.45, kBrass);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, corner);
    g.setColour (kBrassLight.withAlpha (0.7f));
    g.drawRoundedRectangle (r.reduced (1.5f), corner, 0.8f);
    g.setColour (kBrassDark.darker (0.4f));
    g.drawRoundedRectangle (r, corner, 1.0f);
    // the screws
    for (auto p : { juce::Point<float> (r.getX() + 7, r.getCentreY()), juce::Point<float> (r.getRight() - 7, r.getCentreY()) })
    {
        g.setColour (kBrassDark);
        g.fillEllipse (p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f);
        g.setColour (kBrassLight);
        g.drawLine (p.x - 1.8f, p.y + 1.0f, p.x + 1.8f, p.y - 1.0f, 0.8f);
    }
}

// aged paper: a cream sheet, foxed at the edges
void paper (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRect (r.translated (3.0f, 4.0f));
    juce::ColourGradient grad (kIvory, r.getCentreX(), r.getCentreY(), kIvoryAged.darker (0.12f), r.getX(), r.getY(), true);
    g.setGradientFill (grad);
    g.fillRect (r);
    juce::Random rnd (7);
    for (int i = 0; i < 60; ++i) // foxing
    {
        const float x = r.getX() + rnd.nextFloat() * r.getWidth(), y = r.getY() + rnd.nextFloat() * r.getHeight();
        const float edge = std::min ({ x - r.getX(), r.getRight() - x, y - r.getY(), r.getBottom() - y });
        if (edge > 90.0f && rnd.nextFloat() > 0.2f)
            continue;
        const float s = 2.0f + rnd.nextFloat() * 9.0f;
        g.setColour (juce::Colour (0xff9a7440).withAlpha (0.05f + 0.08f * rnd.nextFloat()));
        g.fillEllipse (x - s, y - s, 2 * s, 2 * s);
    }
    g.setColour (kInk.withAlpha (0.25f));
    g.drawRect (r, 1.0f);
}

juce::String noteName (int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[n % 12]) + juce::String (n / 12 - 1);
}
} // namespace

// ==== look and feel ===============================================================================
juce::Font organui::serif (float size, bool bold, bool italic)
{
    int style = juce::Font::plain;
    if (bold) style |= juce::Font::bold;
    if (italic) style |= juce::Font::italic;
    // Georgia where there is one (Windows, macOS); otherwise the system's serif
    static const juce::String name = juce::Font::findAllTypefaceNames().contains ("Georgia") ? juce::String ("Georgia") : juce::Font::getDefaultSerifFontName();
    return juce::Font (juce::FontOptions (name, size, style));
}

VintageLookAndFeel::VintageLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, kIvory);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::textColourId, kInk);
    setColour (juce::ComboBox::arrowColourId, kInk);
    setColour (juce::PopupMenu::backgroundColourId, kIvory);
    setColour (juce::PopupMenu::textColourId, kInk);
    setColour (juce::PopupMenu::headerTextColourId, kRedInk);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kBrass);
    setColour (juce::PopupMenu::highlightedTextColourId, kInk);
    setColour (juce::TextButton::textColourOffId, kInk);
    setColour (juce::TextButton::textColourOnId, kInk);
    setColour (juce::Label::textColourId, kIvory);
    setColour (juce::TextEditor::backgroundColourId, kIvory);
    setColour (juce::TextEditor::textColourId, kInk);
    setColour (juce::TextEditor::outlineColourId, kBrassDark);
    setColour (juce::AlertWindow::backgroundColourId, kIvory);
    setColour (juce::AlertWindow::textColourId, kInk);
    setColour (juce::AlertWindow::outlineColourId, kBrassDark);
}

// A Bakelite knob on a nickel skirt: the skirt's ticks, the brown body with its sheen, an ivory line.
void VintageLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider& s)
{
    const auto tickColour = s.findColour (juce::Slider::textBoxTextColourId);
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float r = 0.5f * std::min (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    // ticks around the skirt
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
        const float r0 = r * 0.86f, r1 = r * (i % 5 == 0 ? 1.0f : 0.95f);
        g.setColour (tickColour.withAlpha (i % 5 == 0 ? 0.85f : 0.5f));
        g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), i % 5 == 0 ? 1.6f : 1.0f);
    }
    // the nickel skirt
    const float sr = r * 0.8f;
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - sr + 1.5f, c.y - sr + 3.0f, 2 * sr, 2 * sr);
    juce::ColourGradient skirt (juce::Colour (0xffe9e4da), c.x - sr, c.y - sr, juce::Colour (0xff6f6a62), c.x + sr, c.y + sr, false);
    g.setGradientFill (skirt);
    g.fillEllipse (c.x - sr, c.y - sr, 2 * sr, 2 * sr);
    // the skirt's pointer notch
    g.setColour (kRedInk);
    g.drawLine (c.x + sr * 0.72f * std::sin (angle), c.y - sr * 0.72f * std::cos (angle), c.x + sr * 0.98f * std::sin (angle),
                c.y - sr * 0.98f * std::cos (angle), 2.2f);
    // the Bakelite body: fluted, glossy
    const float kr = r * 0.6f;
    juce::ColourGradient body (juce::Colour (0xff6b3a1f), c.x - kr * 0.5f, c.y - kr * 0.7f, juce::Colour (0xff1a0d06), c.x + kr * 0.6f, c.y + kr, true);
    g.setGradientFill (body);
    juce::Path flutes;
    const int nf = 18;
    for (int i = 0; i <= nf * 2; ++i)
    {
        const float a = angle + kPi * (float) i / (float) nf;
        const float rr = i % 2 == 0 ? kr : kr * 0.93f;
        if (i == 0) flutes.startNewSubPath (c.x + rr * std::sin (a), c.y - rr * std::cos (a));
        else flutes.lineTo (c.x + rr * std::sin (a), c.y - rr * std::cos (a));
    }
    flutes.closeSubPath();
    g.fillPath (flutes);
    // the cap and its sheen
    const float cr = kr * 0.72f;
    juce::ColourGradient cap (juce::Colour (0xff7a4526), c.x - cr * 0.4f, c.y - cr * 0.8f, juce::Colour (0xff241208), c.x + cr * 0.4f, c.y + cr, false);
    g.setGradientFill (cap);
    g.fillEllipse (c.x - cr, c.y - cr, 2 * cr, 2 * cr);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillEllipse (c.x - cr * 0.6f, c.y - cr * 0.85f, cr * 0.9f, cr * 0.45f);
    // the ivory line
    g.setColour (kIvory);
    g.drawLine (c.x + cr * 0.15f * std::sin (angle), c.y - cr * 0.15f * std::cos (angle), c.x + cr * 0.92f * std::sin (angle),
                c.y - cr * 0.92f * std::cos (angle), 2.0f);
}

void VintageLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState() || down;
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (r.translated (0.0f, on ? 0.5f : 1.5f), 4.0f);
    juce::ColourGradient grad (on ? kBrass.darker (0.3f) : kBrassLight, r.getX(), r.getY(), on ? kBrassDark : kBrass.darker (0.15f), r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 4.0f);
    if (over && ! on)
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (r, 4.0f);
    }
    g.setColour (kBrassDark.darker (0.5f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void VintageLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    const bool on = b.getToggleState();
    g.setColour (b.findColour (on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1);
}

// an ivory selector plate in a brass frame
void VintageLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f);
    g.setColour (kBrassDark);
    g.fillRoundedRectangle (r, 3.0f);
    juce::ColourGradient grad (kIvory, 0, 0, kIvoryAged, 0, (float) h, false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r.reduced (2.0f), 2.0f);
    // the arrow: a small engraved chevron
    const float ax = (float) w - 14.0f, ay = h * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (ax - 4.5f, ay - 2.5f, ax + 4.5f, ay - 2.5f, ax, ay + 3.5f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 0.8f : 0.3f));
    g.fillPath (arrow);
}

void VintageLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Label* VintageLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (serif (12.5f, false, true));
    l->setColour (juce::Label::textColourId, s.findColour (juce::Slider::textBoxTextColourId).withMultipliedAlpha (0.9f));
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineWhenEditingColourId, kBrass);
    l->setColour (juce::Label::backgroundWhenEditingColourId, kWalnutDark);
    return l;
}

void VintageLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (kIvory);
    g.setColour (kBrassDark);
    g.drawRect (0, 0, w, h, 1);
    g.setColour (kInk);
    g.setFont (serif (13.5f));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (8, 5), juce::Justification::centredLeft, 8);
}

juce::Rectangle<int> VintageLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const int w = 320;
    juce::AttributedString s;
    s.append (text, serif (13.5f));
    juce::TextLayout tl;
    tl.createLayout (s, (float) (w - 16));
    const int h = (int) tl.getHeight() + 14;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12, pos.y > parent.getCentreY() ? pos.y - (h + 12) : pos.y + 18, w, h)
        .constrainedWithin (parent);
}

// ==== the stop tablets ==============================================================================
StopTab::StopTab (const juce::String& name, const juce::String& foot, bool redInk) : juce::Button (name), footage (foot), red (redInk)
{
    setClickingTogglesState (true);
}

void StopTab::paintButton (juce::Graphics& g, bool over, bool down)
{
    const bool on = getToggleState();
    auto r = getLocalBounds().toFloat().reduced (3.0f, 6.0f);
    r = r.translated (0.0f, on ? -4.0f : 4.0f);
    if (down) r = r.translated (0.0f, 2.0f);
    // the tablet's shadow on the felt
    g.setColour (juce::Colours::black.withAlpha (on ? 0.55f : 0.3f));
    g.fillRoundedRectangle (r.translated (0.0f, on ? 7.0f : 3.0f).expanded (1.0f), 9.0f);
    // the ivory, lit from above when drawn out, dull when pushed back
    juce::ColourGradient grad (on ? kIvory.brighter (0.05f) : kIvoryAged.darker (0.28f), r.getX(), r.getY(), on ? kIvoryAged : kIvoryAged.darker (0.45f),
                               r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 9.0f);
    if (over)
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (r, 9.0f);
    }
    // engraved border
    g.setColour (kInk.withAlpha (on ? 0.35f : 0.25f));
    g.drawRoundedRectangle (r.reduced (5.0f), 6.0f, 0.8f);
    g.setColour (kBrassDark.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 9.0f, 1.0f);
    // the name and footage
    const auto ink = (red ? kRedInk : kInk).withAlpha (on ? 1.0f : 0.55f);
    g.setColour (ink);
    auto t = r.reduced (8.0f, 10.0f);
    const bool hasFoot = footage.isNotEmpty();
    auto nameArea = hasFoot ? t.removeFromTop (t.getHeight() * 0.62f) : t;
    g.setFont (serif (14.5f, true, true));
    g.drawFittedText (getButtonText(), nameArea.toNearestInt(), juce::Justification::centred, 2, 0.9f);
    if (hasFoot)
    {
        g.setFont (serif (19.0f, true));
        g.drawText (footage, t, juce::Justification::centredTop);
    }
}

// ==== controller setup ===========================================================================
SetupWizard::SetupWizard (PumpOrganProcessor& p, std::function<void()> onClose) : proc (p), close (std::move (onClose))
{
    found.fill (-1);
    for (auto* b : { &back, &skip, &next, &done, &reset })
        addAndMakeVisible (*b);
    back.onClick = [this] { go (std::max (0, step - 1)); };
    skip.onClick = [this] {
        proc.finishStep (step, -1);
        found[(size_t) step] = -1;
        go (step + 1);
    };
    next.onClick = [this] {
        if (step >= PumpOrganProcessor::kWizSteps)
            return;
        const int src = proc.bestSource (step, taken());
        proc.finishStep (step, src);
        found[(size_t) step] = src;
        go (step + 1);
    };
    done.onClick = [this] {
        proc.wizardStep = -1;
        auto c = proc.calibration();
        if (! c.asked)
        {
            c.asked = true;
            proc.setCalibration (c);
        }
        if (close)
            close();
    };
    reset.onClick = [this] {
        PumpOrganProcessor::ControllerCal c;
        c.asked = true;
        proc.setCalibration (c);
        found.fill (-1);
        go (PumpOrganProcessor::kWizSteps);
    };
}

void SetupWizard::begin()
{
    found.fill (-1);
    go (0);
}

void SetupWizard::go (int s)
{
    step = juce::jlimit (0, (int) PumpOrganProcessor::kWizSteps, s);
    if (step < PumpOrganProcessor::kWizSteps)
        proc.startCapture (step);
    else
        proc.wizardStep = -1;
    const bool summary = step == PumpOrganProcessor::kWizSteps;
    skip.setVisible (! summary);
    next.setVisible (! summary);
    reset.setVisible (summary);
    back.setEnabled (step > 0);
    next.setButtonText (step == PumpOrganProcessor::kWizSteps - 1 ? "Finish" : "Next");
    done.setButtonText (summary ? "Done" : "Close");
    repaint();
}

std::vector<int> SetupWizard::taken() const
{
    std::vector<int> t;
    for (int i = 0; i < step; ++i)
        if (found[(size_t) i] >= 0 && found[(size_t) i] < 128)
            t.push_back (found[(size_t) i]);
    return t;
}

juce::String SetupWizard::sourceName (int src) const
{
    if (src == PumpOrganProcessor::kSrcPitch)
        return "Pitch wheel";
    if (src == PumpOrganProcessor::kSrcPressure)
        return "Aftertouch";
    if (src == PumpOrganProcessor::kSrcVelocity)
        return "Velocity";
    if (src < 0)
        return "-";
    const juce::String known = src == 1 ? " (mod wheel)" : src == 11 ? " (expression)" : src == 7 ? " (volume)" : src == 4 ? " (foot)" : src == 2 ? " (breath)" : "";
    return "CC " + juce::String (src) + known;
}

void SetupWizard::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (640, 400);
    auto row = panel.reduced (30, 26).removeFromBottom (32);
    done.setBounds (row.removeFromRight (96));
    row.removeFromRight (8);
    next.setBounds (row.removeFromRight (96));
    row.removeFromRight (8);
    skip.setBounds (row.removeFromRight (84));
    back.setBounds (row.removeFromLeft (84));
    row.removeFromLeft (8);
    reset.setBounds (row.removeFromLeft (160));
}

void SetupWizard::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.7f));
    paper (g, panel.toFloat());
    auto a = panel.reduced (30, 24);
    g.setColour (kRedInk);
    g.setFont (serif (13.0f, true));
    const int nSteps = PumpOrganProcessor::kWizSteps;
    g.drawText (step < nSteps ? juce::String::fromUTF8 ("CONTROLLER SETUP  \xc2\xb7  ") + juce::String (step + 1) + " of " + juce::String (nSteps)
                              : juce::String::fromUTF8 ("CONTROLLER SETUP  \xc2\xb7  DONE"),
                a.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (kInk.withAlpha (0.3f));
    g.drawHorizontalLine (a.getY() + 4, (float) a.getX(), (float) a.getRight());
    a.removeFromTop (14);
    static const char* titles[] = { "Pitch slider", "Mod slider", "Expression pedal", "Aftertouch", "Velocity" };
    static const char* what[] = {
        "Move your pitch slider (or wheel) all the way up and all the way down, then let it go. The organ bends to its real ends, "
        "with a little dead zone where it rests.",
        "Move your mod slider (or wheel) from the bottom to the top. It becomes the swell lever: down, the shutters shut; up, wide open.",
        "Rock your expression pedal from heel to toe. It becomes the wind, over the travel it really has: heel, no wind; toe, all of it.",
        "Hold a key and press into it as hard as you comfortably would. That becomes full pressure: the most vibrato (or swell) a note can take.",
        "Play a few notes, from your softest to your hardest. That range becomes the whole touch, from easing a pallet open to snapping it "
        "with a chiff; the Velocity curve (behind the panel) shapes it." };
    if (step < nSteps)
    {
        g.setColour (kInk);
        g.setFont (serif (26.0f, true, true));
        g.drawText (titles[step], a.removeFromTop (34), juce::Justification::centredLeft);
        g.setFont (serif (15.0f));
        g.setColour (inkDim());
        g.drawFittedText (what[step], a.removeFromTop (62), juce::Justification::topLeft, 3);
        a.removeFromTop (12);
        const int src = proc.bestSource (step, taken());
        g.setFont (serif (14.0f, true, true));
        if (src < 0)
        {
            g.setColour (kRedInk);
            g.drawText (step == PumpOrganProcessor::WizVelocity ? "Listening... (" + juce::String (proc.capNotes.load()) + " notes so far)"
                                                                : juce::String ("Listening... move it all the way."),
                        a.removeFromTop (20), juce::Justification::centredLeft);
            return;
        }
        const bool pitchWheel = src == PumpOrganProcessor::kSrcPitch;
        auto norm = [&] (float v) { return pitchWheel ? 0.5f * (v + 1.0f) : v; };
        const float lo = norm (proc.capLo[(size_t) src].load()), hi = norm (proc.capHi[(size_t) src].load()),
                    last = norm (proc.capLast[(size_t) src].load());
        g.setColour (kInk);
        g.drawText ("Found: " + sourceName (src), a.removeFromTop (20), juce::Justification::centredLeft);
        a.removeFromTop (8);
        auto bar = a.removeFromTop (22).toFloat();
        g.setColour (kInk.withAlpha (0.15f));
        g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (kBrass);
        g.fillRoundedRectangle (juce::Rectangle<float> (bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, lo), bar.getY(),
                                                        std::max (3.0f, bar.getWidth() * juce::jlimit (0.0f, 1.0f, hi - lo)), bar.getHeight()),
                                4.0f);
        g.setColour (kRedInk);
        g.fillRect (juce::Rectangle<float> (bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, last) - 1.5f, bar.getY() - 3.0f, 3.0f, bar.getHeight() + 6.0f));
        a.removeFromTop (6);
        g.setColour (inkDim());
        g.setFont (serif (13.0f, false, true));
        g.drawText ("reached " + juce::String (juce::roundToInt (lo * 100.0f)) + " % to " + juce::String (juce::roundToInt (hi * 100.0f))
                        + " %  (the brass part is used as the whole range)",
                    a.removeFromTop (18), juce::Justification::centredLeft);
        return;
    }
    const auto c = proc.calibration();
    g.setFont (serif (15.0f));
    auto line = [&] (const juce::String& k, const juce::String& v) {
        auto r = a.removeFromTop (26);
        g.setColour (inkDim());
        g.drawText (k, r.removeFromLeft (180), juce::Justification::centredLeft);
        g.setColour (kInk);
        g.drawText (v, r, juce::Justification::centredLeft);
    };
    auto pc = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
    line ("Pitch slider", (c.pitchCc >= 0 ? sourceName (c.pitchCc) : juce::String ("Pitch wheel")) + ",  " + juce::String (c.pitchLo, 2) + " to "
                              + juce::String (c.pitchHi, 2));
    line ("Swell lever (mod)", sourceName (c.modCc) + ",  " + pc (c.modLo) + " to " + pc (c.modHi));
    line ("Wind pedal", (c.exprCc >= 0 ? sourceName (c.exprCc) : proc.apvts.getParameter ("pedalcc")->getCurrentValueAsText()) + ",  " + pc (c.exprLo)
                            + " to " + pc (c.exprHi));
    line ("Aftertouch", "full pressure at " + pc (c.atHi));
    line ("Velocity", pc (c.velLo) + " to " + pc (c.velHi) + " is the whole touch");
    a.removeFromTop (8);
    g.setColour (inkDim());
    g.setFont (serif (14.0f, false, true));
    g.drawFittedText ("Saved for every Pump Organ on this computer. Run it again any time from behind the panel.", a.removeFromTop (36),
                      juce::Justification::topLeft, 2);
}

// ==== editor =======================================================================================
void PumpOrganEditor::Knob::setup (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& text,
                                   const juce::String& tip)
{
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    auto* prm = apvts.getParameter (id);
    slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    slider.setTooltip (tip);
    parent.addAndMakeVisible (slider);
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, kBrassLight);
    label.setFont (serif (13.0f, true));
    label.setInterceptsMouseClicks (false, false);
    parent.addAndMakeVisible (label);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, slider);
    paramId = id;
}
void PumpOrganEditor::Knob::place (juce::Rectangle<int> cell)
{
    label.setBounds (cell.removeFromTop (18));
    slider.setBounds (cell);
}

void PumpOrganEditor::Choice::setup (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& text,
                                     const juce::String& tip)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
        box.addItemList (p->choices, 1);
    box.setTooltip (tip);
    parent.addAndMakeVisible (box);
    label.setText (text, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, kBrassLight);
    label.setFont (serif (12.5f, true));
    label.setInterceptsMouseClicks (false, false);
    parent.addAndMakeVisible (label);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, id, box);
    paramId = id;
}
void PumpOrganEditor::Choice::place (juce::Rectangle<int> cell)
{
    label.setBounds (cell.removeFromTop (17));
    box.setBounds (cell.removeFromTop (28));
}

PumpOrganEditor::PumpOrganEditor (PumpOrganProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), wizard (p, [this] { showSetup (false); })
{
    setLookAndFeel (&laf);
    addAndMakeVisible (canvas);
    auto& s = proc.apvts;
    auto& c = canvas;
    makeWood();

    // --- the stop rail ---
    struct T { const char* id; const char* name; const char* foot; bool red; const char* tip; };
    const T tabList[] = {
        { "stop0", "Diapason", "8'", false, "The main voice: bright and full." },
        { "stop1", "Melodia", "8'", false, "Mellow and round, flutey." },
        { "stop2", "Dulciana", "8'", false, "Soft and string-like." },
        { "stop3", "Celeste", "8'", false, "A Dulciana tuned a slow beat sharp: it shimmers against the others. Treble only." },
        { "stop4", "Sub Bass", "16'", false, "An octave below, in the bottom octave and a half." },
        { "stop5", "Flute", "4'", false, "An octave above: a little sparkle on top." },
        { "vox", "Vox Humana", "", true, "A fan in the case sweeping past the reeds: a slow tremolo." },
        { "forte", "Forte", "", true, "Throws the swell shutters wide open." },
        { "bass", "Bass Coupler", "", true, "Your lowest note sounds an octave lower too, on the same stops." },
        { "melody", "Melody Coupler", "", true, "Your highest note sounds an octave higher too: the melody sings out." },
        { "hold", "Drone Hold", "", true, "Latches the notes you're holding: they keep sounding (and sagging) while you play over them. "
                                          "Key B1, just below the keyboard, toggles it; so does a sostenuto pedal (CC66)." },
    };
    for (const auto& t : tabList)
    {
        Tab tab;
        tab.button = std::make_unique<StopTab> (t.name, t.foot, t.red);
        tab.button->setTooltip (t.tip);
        c.addAndMakeVisible (*tab.button);
        tab.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, t.id, *tab.button);
        tab.paramId = t.id;
        tabs.push_back (std::move (tab));
    }
    for (auto& t : tabs)
        registerControl (*t.button, t.paramId);

    // --- the front's knobs and selectors ---
    wind.setup (c, s, "wind", "Wind", "How much wind: the expression pedal scales it (heel: none). With no pedal, the organ plays at this.");
    swell.setup (c, s, "swell", "Swell", "Where the swell shutters rest. The mod slider (the swell lever) opens them the rest of the way: "
                                         "louder and brighter.");
    chiff.setup (c, s, "chiff", "Chiff", "Key action: a hard key snaps the pallet open with a puff of air; a soft one eases in.");
    level.setup (c, s, "level", "Level", "Output level.");
    pumpDepth.setup (c, s, "pumpdepth", "Pump", "Auto-pump depth: how much the wind breathes with each stroke.");
    cutout.setup (c, s, "cutout", "Cut-out", "On little wind: how much notes flutter and cut out (none: they just fade).");
    warble.setup (c, s, "warble", "Warble", "On little wind: how much the pitch warbles.");
    neglect.setup (c, s, "neglect", "Neglect", "From as built to a damp basement: stickier keys, leakier wind, and ciphers (a key's reeds "
                                               "whispering on after you let go).");
    model.setup (c, s, "model", "Instrument", "Chord organ: a little 1950s electric chord organ, found in a basement. Parlour organ: a full-size "
                                               "foot-pumped reed organ in a wooden case.");
    mic.setup (c, s, "mic", "Microphone", "Close: in front of the reed board. Inside the case: boxy and woolly, the keys and fan right there. "
                                          "Across the room: softer, with a little of the room.");
    pumpRate.setup (c, s, "pumprate", "Auto-pump", "The feet working on their own, in time with the host: the wind breathes once per beat, "
                                                   "half note, bar or two bars.");

    // --- behind the panel ---
    age.setup (c, s, "age", "Tuning age", "How far the reeds have drifted from their marks: 0 factory fresh, 100 % as found.");
    air.setup (c, s, "air", "Air noise", "Hiss through the reeds, the pallets and keys, the bellows, the blower.");
    vibDepth.setup (c, s, "vibdepth", "Vibrato", "Per-note vibrato at full pressure (aftertouch / MPE pressure).");
    vibRate.setup (c, s, "vibrate", "Vib. rate", "How fast the per-note vibrato goes.");
    velCurve.setup (c, s, "velcurve", "Touch", "How your keys' velocity becomes the key action: Light, a soft touch snaps; Heavy, it takes a hard hit.");
    windMode.setup (c, s, "windmode", "Pumping", "Easy: the pedal is the wind. Reservoir: each stroke of the pedal pumps; stop and it runs dry. "
                                                 "Expression: the strokes themselves are the wind.");
    pedalCc.setup (c, s, "pedalcc", "Wind pedal", "The pedal that is the wind.");
    mpe.setup (c, s, "mpe", "MPE", "Each note's own bend, pressure and slide (in Ableton: Enable MPE Mode on the plug-in).");
    memberRange.setup (c, s, "memberrange", "Note bend range", "Bend range of each MPE note: match your controller (48 is the MPE default, and Ableton's).");
    masterRange.setup (c, s, "masterrange", "Pitch wheel range", "Range of an ordinary pitch wheel / the MPE master channel.");
    pressure.setup (c, s, "pressure", "Pressure", "What pressing into a key does: its own vibrato, opens its own air (swell), or nothing.");
    slide.setup (c, s, "slide", "MPE slide", "Slide (CC74): that note's air, from choked to a little more.");
    // the labels on the service card are ink, not brass
    for (auto* k : { &age, &air, &vibDepth, &vibRate, &velCurve })
    {
        k->label.setColour (juce::Label::textColourId, kInk);
        k->slider.setColour (juce::Slider::textBoxTextColourId, kInk);
    }
    for (auto* ch : { &windMode, &pedalCc, &mpe, &memberRange, &masterRange, &pressure, &slide })
        ch->label.setColour (juce::Label::textColourId, kInk);
    setupButton.setTooltip ("Find your pitch slider, mod slider (the swell lever), expression pedal (the wind), aftertouch and velocity range.");
    setupButton.onClick = [this] { showSetup (true); };
    c.addChildComponent (setupButton);

    for (auto* k : { &wind, &swell, &chiff, &level, &pumpDepth, &cutout, &warble, &neglect, &age, &air, &vibDepth, &vibRate, &velCurve })
        registerControl (k->slider, k->paramId);
    for (auto* ch : { &model, &mic, &pumpRate, &windMode, &pedalCc, &mpe, &memberRange, &masterRange, &pressure, &slide })
        registerControl (ch->box, ch->paramId);

    // --- the header ---
    c.addAndMakeVisible (presetPlate);
    presetPlate.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    presetPlate.setTooltip ("Presets: favourites, factory sounds by category, and your own. They set the sound, not your rig.");
    presetPlate.addMouseListener (this, false);
    for (auto* b : { &prevPreset, &nextPreset, &favButton, &abA, &abB, &abCopy, &auditionButton, &learnButton, &pageButton })
        c.addAndMakeVisible (*b);
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (+1); };
    favButton.setTooltip ("Favourite this preset: favourites come first in the preset menu.");
    favButton.onClick = [this] { proc.toggleFavourite (proc.presetName()); updateHeader(); };
    abA.setTooltip ("A/B compare: two sounds to flip between.");
    abB.setTooltip (abA.getTooltip());
    abA.onClick = [this] { proc.abSwitch (0); updateHeader(); };
    abB.onClick = [this] { proc.abSwitch (1); updateHeader(); };
    abCopy.setTooltip ("Copy this sound to the other side of the A/B.");
    abCopy.onClick = [this] { proc.abCopyToOther(); };
    auditionButton.setTooltip ("Hear the sound: a hymn-like phrase, the wind swelling in on the pedal and the swell lever opening for the climax.");
    auditionButton.onClick = [this] { proc.audition(); };
    learnButton.setClickingTogglesState (true);
    learnButton.setTooltip ("MIDI learn: click a knob, stop or selector, then move a control on your keyboard. Click MIDI learn again when done.");
    learnButton.onClick = [this] {
        learning = learnButton.getToggleState();
        if (! learning) proc.learnTarget = -1;
        clearLearn.setVisible (learning);
        auditionButton.setVisible (! learning);
        canvas.repaint();
    };
    clearLearn.onClick = [this] { proc.clearMidiMap(); canvas.repaint(); };
    c.addChildComponent (clearLearn);
    pageButton.setTooltip ("Behind the panel: how it's pumped and played, MPE, its character, controller setup.");
    pageButton.onClick = [this] { showPage (page == FrontPage ? BackPage : FrontPage); };

    c.addChildComponent (wizard);
    setResizable (true, true);
    setResizeLimits (kW * 3 / 4, kH * 3 / 4, kW * 3 / 2, kH * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
    canvas.setBounds (0, 0, kW, kH);
    canvas.sendLookAndFeelChange(); // the knobs' value labels were made before they joined the canvas
    layout();
    setSize (kW, kH);
    showPage (FrontPage);
    updateHeader();
    startTimerHz (30);
    if (! proc.calibration().asked)
        showSetup (true);
}

PumpOrganEditor::~PumpOrganEditor()
{
    stopTimer();
    proc.wizardStep = -1;
    setLookAndFeel (nullptr);
}

// walnut: long, wavy grain, darker streaks, a soft vignette (made once)
void PumpOrganEditor::makeWood()
{
    auto grain = [] (juce::Colour base, uint32_t seed) {
        juce::Image img (juce::Image::RGB, kW, kH, true);
        juce::Graphics g (img);
        g.fillAll (base);
        juce::Random rnd ((juce::int64) seed);
        for (int i = 0; i < 260; ++i)
        {
            const float y0 = rnd.nextFloat() * (float) kH, a1 = 2.0f + rnd.nextFloat() * 10.0f, f1 = 0.002f + rnd.nextFloat() * 0.006f;
            const float a2 = rnd.nextFloat() * 3.0f, f2 = 0.01f + rnd.nextFloat() * 0.03f, ph = rnd.nextFloat() * 6.28f;
            juce::Path p;
            for (int x = -10; x <= kW + 10; x += 8)
            {
                const float y = y0 + a1 * std::sin ((float) x * f1 + ph) + a2 * std::sin ((float) x * f2 + 2.0f * ph);
                if (x == -10) p.startNewSubPath ((float) x, y);
                else p.lineTo ((float) x, y);
            }
            const bool dark = rnd.nextFloat() < 0.65f;
            g.setColour ((dark ? juce::Colours::black : juce::Colour (0xffc88a55)).withAlpha (dark ? 0.05f + 0.12f * rnd.nextFloat() : 0.03f + 0.06f * rnd.nextFloat()));
            g.strokePath (p, juce::PathStrokeType (0.6f + rnd.nextFloat() * (dark ? 3.2f : 1.6f)));
        }
        juce::ColourGradient vig (juce::Colours::transparentBlack, kW * 0.5f, kH * 0.45f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
        g.setGradientFill (vig);
        g.fillAll();
        return img;
    };
    wood = grain (kWalnut, 1957);
    backWood = grain (kWalnutDark.brighter (0.15f), 1903);
}

void PumpOrganEditor::showSetup (bool show)
{
    if (show)
    {
        wizard.setBounds (0, 0, kW, kH);
        wizard.begin();
        wizard.setVisible (true);
        wizard.toFront (true);
    }
    else
    {
        proc.wizardStep = -1;
        wizard.setVisible (false);
    }
}

void PumpOrganEditor::timerCallback()
{
    phase += 1.0f / 30.0f;
    fanAngle += 0.55f * proc.uiMotor.load();
    if (fanAngle > 2 * kPi) fanAngle -= 2 * kPi;
    // the gauge's needle is damped, like a real one
    const float target = gaugeTarget();
    if (needle < 0) needle = target;
    needle += 0.22f * (target - needle) + 0.004f * proc.uiStruggle.load() * std::sin (phase * 23.0f);
    if (wizard.isVisible())
        wizard.tick();
    if (page == FrontPage)
    {
        for (auto r : { gaugeArea, blowerArea, tubeArea, shutterArea, keysArea, pumpLamp, holdLamp })
            canvas.repaint (r.expanded (6));
    }
    canvas.repaint (statusArea);
    if (learning)
        canvas.repaint();
    updateHeader();
}

float PumpOrganEditor::gaugeTarget() const
{
    const auto& mv = ro::modelVoice ((int) proc.apvts.getRawParameterValue ("model")->load() == 0 ? ro::Model::ChordOrgan : ro::Model::Parlour);
    return juce::jlimit (0.0f, 1.08f, proc.uiChest.load() / (float) mv.windPower);
}

void PumpOrganEditor::updateHeader()
{
    const auto name = proc.presetName();
    const bool fav = proc.isFavourite (name);
    const juce::String star = juce::String::fromUTF8 (fav ? "\xe2\x98\x85" : "\xe2\x98\x86");
    if (favButton.getButtonText() != star)
    {
        favButton.setButtonText (star);
        favButton.setColour (juce::TextButton::textColourOffId, fav ? kRedInk : kInk);
        canvas.repaint (plateArea);
    }
    if (shownPreset != name)
    {
        shownPreset = name;
        canvas.repaint (plateArea);
    }
    abA.setToggleState (proc.abActive() == 0, juce::dontSendNotification);
    abB.setToggleState (proc.abActive() == 1, juce::dontSendNotification);
    abCopy.setButtonText (proc.abActive() == 0 ? "A>B" : "B>A");
}

juce::StringArray PumpOrganEditor::allPresets() const
{
    juce::StringArray a;
    for (const auto& p : popresets::presets())
        a.add (p.name);
    a.addArray (proc.userPresetNames());
    return a;
}

void PumpOrganEditor::stepPreset (int dir)
{
    const auto all = allPresets();
    const int nFactory = (int) popresets::presets().size(), n = all.size();
    if (n == 0) return;
    int i = proc.userPreset.isNotEmpty() ? all.indexOf (proc.userPreset, false, nFactory) : proc.currentProgram.load();
    i = ((juce::jmax (i, 0) + dir) % n + n) % n;
    if (i < nFactory) proc.setCurrentProgram (i);
    else proc.loadUserPreset (all[i]);
    canvas.repaint (plateArea);
}

void PumpOrganEditor::savePresetDialog()
{
    auto* w = new juce::AlertWindow ("Save preset", "Saves the sound as one of your own presets.", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.userPreset.isNotEmpty() ? proc.userPreset : juce::String ("My organ"), "Name");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PumpOrganEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result) {
        if (result == 1 && safe != nullptr)
        {
            safe->proc.saveUserPreset (w->getTextEditorContents ("name"));
            safe->canvas.repaint();
        }
    }), true);
}

void PumpOrganEditor::presetMenu()
{
    juce::PopupMenu m;
    const auto& fac = popresets::presets();
    const int current = proc.userPreset.isEmpty() ? proc.currentProgram.load() : -1;
    const auto user = proc.userPresetNames();
    const auto favs = proc.favourites();
    juce::PopupMenu favMenu;
    for (size_t i = 0; i < fac.size(); ++i)
        if (favs.contains (fac[i].name)) favMenu.addItem (1 + (int) i, fac[i].name, true, (int) i == current);
    for (int i = 0; i < user.size(); ++i)
        if (favs.contains (user[i])) favMenu.addItem (1000 + i, user[i], true, user[i] == proc.userPreset);
    m.addSubMenu (juce::String::fromUTF8 ("\xe2\x98\x85 Favourites"), favMenu, favMenu.getNumItems() > 0);
    m.addSeparator();
    juce::StringArray cats;
    for (const auto& p : fac) cats.addIfNotAlreadyThere (p.category);
    for (const auto& cat : cats)
    {
        juce::PopupMenu sub;
        for (size_t i = 0; i < fac.size(); ++i)
            if (cat == fac[i].category) sub.addItem (1 + (int) i, fac[i].name, true, (int) i == current);
        m.addSubMenu (cat, sub);
    }
    juce::PopupMenu mine;
    for (int i = 0; i < user.size(); ++i)
        mine.addItem (1000 + i, user[i], true, user[i] == proc.userPreset);
    m.addSubMenu ("My presets", mine, user.size() > 0);
    m.addSeparator();
    m.addItem (5000, "Save as...");
    m.addItem (5001, "Delete \"" + proc.userPreset + "\"", proc.userPreset.isNotEmpty());
    juce::Component::SafePointer<PumpOrganEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetPlate), [safe, user] (int r) {
        if (safe == nullptr || r == 0) return;
        if (r == 5000) safe->savePresetDialog();
        else if (r == 5001) safe->proc.deleteUserPreset (safe->proc.userPreset);
        else if (r >= 1000) safe->proc.loadUserPreset (user[r - 1000]);
        else safe->proc.setCurrentProgram (r - 1);
        safe->updateHeader();
        safe->canvas.repaint();
    });
}

void PumpOrganEditor::showPage (int p)
{
    page = p;
    const bool front = p == FrontPage;
    pageButton.setButtonText (front ? "Behind the panel" : "Back to the front");
    for (auto& t : tabs)
        t.button->setVisible (front);
    for (auto* k : { &wind, &swell, &chiff, &level, &pumpDepth, &cutout, &warble, &neglect })
        k->show (front);
    for (auto* ch : { &model, &mic, &pumpRate })
        ch->show (front);
    for (auto* k : { &age, &air, &vibDepth, &vibRate, &velCurve })
        k->show (! front);
    for (auto* ch : { &windMode, &pedalCc, &mpe, &memberRange, &masterRange, &pressure, &slide })
        ch->show (! front);
    setupButton.setVisible (! front);
    canvas.repaint();
}

void PumpOrganEditor::resized()
{
    canvas.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kW));
}

void PumpOrganEditor::layout()
{
    // --- header ---
    auto h = juce::Rectangle<int> (0, 0, kW, 64).reduced (16, 14);
    auto right = h.removeFromRight (150);
    pageButton.setBounds (right.removeFromTop (30));
    statusArea = juce::Rectangle<int> (right.getX(), 46, right.getWidth(), 16);
    h.removeFromRight (10);
    learnButton.setBounds (h.removeFromRight (96).withHeight (30));
    h.removeFromRight (6);
    auditionButton.setBounds (h.removeFromRight (76).withHeight (30));
    clearLearn.setBounds (auditionButton.getBounds());
    h.removeFromRight (14);
    abCopy.setBounds (h.removeFromRight (44).withHeight (30));
    h.removeFromRight (3);
    abB.setBounds (h.removeFromRight (30).withHeight (30));
    h.removeFromRight (3);
    abA.setBounds (h.removeFromRight (30).withHeight (30));
    h.removeFromLeft (230); // the maker's nameplate
    prevPreset.setBounds (h.removeFromLeft (26).withHeight (30));
    h.removeFromLeft (4);
    favButton.setBounds (h.removeFromRight (32).withHeight (30));
    h.removeFromRight (4);
    nextPreset.setBounds (h.removeFromRight (26).withHeight (30));
    h.removeFromRight (4);
    plateArea = h.withY (6).withHeight (54);
    presetPlate.setBounds (plateArea);

    // --- the stop rail ---
    railArea = { 16, 70, kW - 32, 128 };
    {
        const int n = (int) tabs.size(), tw = 86, gap = 8, total = n * tw + (n - 1) * gap;
        int x = railArea.getCentreX() - total / 2;
        for (auto& t : tabs)
        {
            t.button->setBounds (x, railArea.getY() + 8, tw, railArea.getHeight() - 16);
            x += tw + gap;
        }
    }

    // --- the middle: the gauge and blower, the knobs, the shutters and selectors ---
    const int midY = 210, midH = 268;
    gaugeArea = { 26, midY + 12, 226, 226 };
    tubeArea = { 262, midY + 20, 30, 210 };
    blowerArea = { 300, midY + 14, 64, 64 };
    knobArea = { 382, midY, 416, midH };
    {
        const int kw = knobArea.getWidth() / 4, kh = knobArea.getHeight() / 2;
        int i = 0;
        for (auto* k : { &wind, &swell, &chiff, &level, &pumpDepth, &cutout, &warble, &neglect })
        {
            k->place (juce::Rectangle<int> (knobArea.getX() + kw * (i % 4), knobArea.getY() + kh * (i / 4), kw, kh).reduced (6, 4));
            ++i;
        }
    }
    sideArea = { 812, midY, kW - 26 - 812, midH };
    shutterArea = sideArea.withHeight (112).reduced (4, 0);
    {
        auto a = sideArea.withTrimmedTop (122);
        model.place (a.removeFromTop (48).reduced (4, 0));
        mic.place (a.removeFromTop (48).reduced (4, 0));
        auto pr = a.removeFromTop (48).reduced (4, 0);
        pumpLamp = pr.removeFromRight (22).withTrimmedTop (21).withHeight (22);
        pr.removeFromRight (6);
        pumpRate.place (pr);
    }

    // --- the keyboard ---
    keysArea = { 16, 494, kW - 32, 212 };
    holdLamp = { keysArea.getX() + 12, keysArea.getY() + 60, 34, 34 };

    // --- behind the panel: four sections on the service card ---
    const auto card = juce::Rectangle<int> (40, 84, kW - 80, kH - 84 - 30);
    const int cw = card.getWidth() / 2 - 30, ch = card.getHeight() / 2 - 30;
    backAreas[0] = { card.getX() + 24, card.getY() + 64, cw, ch - 30 };
    backAreas[1] = { card.getCentreX() + 12, card.getY() + 64, cw, ch - 30 };
    backAreas[2] = { card.getX() + 24, card.getCentreY() + 36, cw, ch - 30 };
    backAreas[3] = { card.getCentreX() + 12, card.getCentreY() + 36, cw, ch - 30 };
    {
        auto a = backAreas[0].withTrimmedTop (30);
        windMode.place (a.removeFromTop (50).removeFromLeft (300));
        pedalCc.place (a.removeFromTop (50).removeFromLeft (300));
        a.removeFromTop (6);
        setupButton.setBounds (a.removeFromTop (32).removeFromLeft (200));
    }
    {
        auto a = backAreas[1].withTrimmedTop (30);
        pressure.place (a.removeFromTop (50).removeFromLeft (220));
        auto knobs = a.withHeight (120);
        const int kw = knobs.getWidth() / 3;
        vibDepth.place (knobs.removeFromLeft (kw).reduced (6, 0));
        vibRate.place (knobs.removeFromLeft (kw).reduced (6, 0));
        velCurve.place (knobs.removeFromLeft (kw).reduced (6, 0));
    }
    {
        auto a = backAreas[2].withTrimmedTop (30);
        auto l = a.removeFromLeft (a.getWidth() / 2 - 6), r = a.withTrimmedLeft (12);
        mpe.place (l.removeFromTop (50));
        memberRange.place (l.removeFromTop (50));
        slide.place (r.removeFromTop (50));
        masterRange.place (r.removeFromTop (50));
    }
    {
        auto a = backAreas[3].withTrimmedTop (30).withHeight (120);
        age.place (a.removeFromLeft (120).reduced (6, 0));
        air.place (a.removeFromLeft (120).reduced (6, 0));
    }
}

// ==== painting ======================================================================================
void PumpOrganEditor::paintCanvas (juce::Graphics& g)
{
    g.drawImageAt (page == FrontPage ? wood : backWood, 0, 0);
    // --- the header: the maker's nameplate, the preset's nameplate ---
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (0, 0, kW, 64);
    g.setColour (kBrassDark.withAlpha (0.8f));
    g.drawHorizontalLine (64, 0.0f, (float) kW);
    {
        const auto np = juce::Rectangle<float> (16, 10, 220, 46);
        brassPlate (g, np);
        g.setColour (kInk.withAlpha (0.85f));
        g.setFont (serif (25.0f, true, true));
        g.drawText ("Pump Organ", np.withTrimmedBottom (12).translated (0, 1), juce::Justification::centred);
        g.setFont (serif (9.5f, true));
        g.drawText (juce::String::fromUTF8 ("REED ORGAN WORKS  \xc2\xb7  FREE REEDS"), np.withTrimmedTop (31), juce::Justification::centred);
    }
    {
        // the preset nameplate: an ivory card behind glass in a brass frame
        const auto r = plateArea.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (r.translated (0, 2), 5.0f);
        g.setColour (kBrass);
        g.fillRoundedRectangle (r, 5.0f);
        const auto card = r.reduced (4.0f);
        juce::ColourGradient grad (kIvory, card.getX(), card.getY(), kIvoryAged, card.getX(), card.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (card, 3.0f);
        g.setColour (kInk);
        g.setFont (serif (20.0f, true, true));
        g.drawFittedText (proc.presetName(), card.withTrimmedBottom (18).toNearestInt(), juce::Justification::centred, 1);
        g.setColour (inkDim());
        g.setFont (serif (11.5f, false, true));
        g.drawFittedText (proc.presetNote(), card.withTrimmedTop (card.getHeight() - 20).withTrimmedBottom (3).toNearestInt(), juce::Justification::centred, 1);
        g.setColour (juce::Colours::white.withAlpha (0.18f)); // the glass
        g.fillRoundedRectangle (card.withHeight (card.getHeight() * 0.35f), 3.0f);
    }
    g.setColour (kBrassLight.withAlpha (0.65f));
    g.setFont (serif (10.5f, false, true));
    g.drawText ("cpu " + juce::String (juce::roundToInt (proc.uiCpu.load() * 100.0f)) + " %", statusArea, juce::Justification::centredRight);

    if (page == FrontPage)
        paintFront (g);
    else
        paintBack (g);
}

void PumpOrganEditor::paintFront (juce::Graphics& g)
{
    // --- the stop rail: felt in a moulded walnut frame ---
    {
        const auto r = railArea.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r.expanded (6.0f).translated (0, 3), 6.0f);
        juce::ColourGradient frame (kWalnutLight, r.getX(), r.getY() - 6, kWalnutDark, r.getX(), r.getBottom() + 6, false);
        g.setGradientFill (frame);
        g.fillRoundedRectangle (r.expanded (6.0f), 6.0f);
        juce::ColourGradient felt (kFelt.brighter (0.15f), r.getX(), r.getY(), kFelt.darker (0.5f), r.getX(), r.getBottom(), false);
        g.setGradientFill (felt);
        g.fillRect (r);
        juce::Random rnd (3);
        for (int i = 0; i < 900; ++i) // the nap
        {
            g.setColour ((rnd.nextBool() ? juce::Colours::black : juce::Colours::white).withAlpha (0.04f));
            g.fillRect (r.getX() + rnd.nextFloat() * r.getWidth(), r.getY() + rnd.nextFloat() * r.getHeight(), 1.5f, 1.5f);
        }
        g.setColour (kBrass.withAlpha (0.6f));
        g.drawRect (r, 1.0f);
    }
    // --- the middle panel: a recessed wood panel with brass labels ---
    auto recess = [&] (juce::Rectangle<int> a) {
        const auto r = a.toFloat().expanded (4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (kBrassDark.withAlpha (0.5f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    };
    recess ({ gaugeArea.getX() - 6, gaugeArea.getY() - 8, blowerArea.getRight() - gaugeArea.getX() + 14, 268 - 6 });
    recess (knobArea.reduced (0, 2));
    recess (sideArea.reduced (0, 2));
    drawGauge (g, gaugeArea.toFloat());
    drawTube (g, tubeArea.toFloat());
    drawBlower (g, blowerArea.toFloat());
    drawShutters (g, shutterArea.toFloat());
    // the auto-pump lamp: a beat light, breathing in time
    {
        const bool on = (int) proc.apvts.getRawParameterValue ("pumprate")->load() > 0;
        const float lv = on ? 0.5f + 0.5f * std::cos (2 * kPi * proc.uiPump.load()) : 0.0f;
        const auto r = pumpLamp.toFloat().reduced (2.0f);
        g.setColour (kBrassDark);
        g.fillEllipse (r);
        g.setColour (juce::Colour (0xff3a1a0a).interpolatedWith (kGlow, lv));
        g.fillEllipse (r.reduced (3.0f));
        if (lv > 0.05f)
        {
            g.setColour (kGlow.withAlpha (0.35f * lv));
            g.fillEllipse (r.expanded (5.0f));
        }
    }
    drawKeyboard (g, keysArea.toFloat());
}

// The wind gauge: a cream dial in a brass bezel, reading the chest's suction against the most the wind can
// give. The red arc is where the organ labours (it moves with Low-wind cut-out).
void PumpOrganEditor::drawGauge (juce::Graphics& g, juce::Rectangle<float> a)
{
    const auto c = a.getCentre();
    const float R = 0.5f * std::min (a.getWidth(), a.getHeight());
    // bezel
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (c.x - R + 2, c.y - R + 4, 2 * R, 2 * R);
    juce::ColourGradient bez (kBrassLight, c.x - R, c.y - R, kBrassDark, c.x + R, c.y + R, false);
    g.setGradientFill (bez);
    g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
    const float fr = R * 0.86f;
    g.setColour (kBrassDark.darker (0.4f));
    g.fillEllipse (c.x - fr - 1.5f, c.y - fr - 1.5f, 2 * fr + 3, 2 * fr + 3);
    juce::ColourGradient face (kIvory, c.x, c.y - fr * 0.3f, kIvoryAged.darker (0.15f), c.x, c.y + fr, true);
    g.setGradientFill (face);
    g.fillEllipse (c.x - fr, c.y - fr, 2 * fr, 2 * fr);
    // scale: 0..10 over 270 degrees
    const float a0 = juce::degreesToRadians (-135.0f), a1 = juce::degreesToRadians (135.0f);
    auto at = [&] (float v) { return a0 + (a1 - a0) * juce::jlimit (0.0f, 1.08f, v); };
    // the labouring zone
    {
        const auto& mv = ro::modelVoice ((int) proc.apvts.getRawParameterValue ("model")->load() == 0 ? ro::Model::ChordOrgan : ro::Model::Parlour);
        const float cut = proc.apvts.getRawParameterValue ("cutout")->load();
        const float zone = (float) mv.labourBelow * (0.7f + 0.3f * juce::jlimit (0.0f, 2.0f, cut));
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, fr * 0.78f, fr * 0.78f, 0.0f, a0, at (zone), true);
        g.setColour (kStruggle.withAlpha (0.55f));
        g.strokePath (arc, juce::PathStrokeType (fr * 0.1f));
    }
    for (int i = 0; i <= 50; ++i)
    {
        const float an = at ((float) i / 50.0f);
        const bool major = i % 5 == 0;
        const float r0 = fr * (major ? 0.8f : 0.86f), r1 = fr * 0.93f;
        g.setColour (kInk.withAlpha (major ? 0.9f : 0.5f));
        g.drawLine (c.x + r0 * std::sin (an), c.y - r0 * std::cos (an), c.x + r1 * std::sin (an), c.y - r1 * std::cos (an), major ? 1.6f : 0.8f);
        if (major)
        {
            g.setFont (serif (12.0f, true));
            const float rt = fr * 0.66f;
            g.drawText (juce::String (i / 5), juce::Rectangle<float> (c.x + rt * std::sin (an) - 10, c.y - rt * std::cos (an) - 7, 20, 14),
                        juce::Justification::centred);
        }
    }
    g.setColour (kInk);
    g.setFont (serif (15.0f, true, true));
    g.drawText ("Wind", juce::Rectangle<float> (c.x - 50, c.y - fr * 0.42f, 100, 18), juce::Justification::centred);
    g.setFont (serif (8.5f, true));
    g.setColour (inkDim());
    g.drawText ("INCHES OF SUCTION", juce::Rectangle<float> (c.x - 60, c.y + fr * 0.3f, 120, 12), juce::Justification::centred);
    const float st = proc.uiStruggle.load();
    g.setFont (serif (11.0f, false, true));
    g.setColour (st > 0.15f ? kRedInk : inkDim());
    const juce::String mood = proc.uiChest.load() < 0.002f ? "at rest" : st > 0.6f ? "gasping" : st > 0.3f ? "struggling" : st > 0.05f ? "labouring" : "singing";
    g.drawText (mood, juce::Rectangle<float> (c.x - 60, c.y + fr * 0.46f, 120, 14), juce::Justification::centred);
    // the needle
    const float an = at (needle < 0 ? gaugeTarget() : needle);
    juce::Path nd;
    nd.addTriangle (-2.2f, 0.0f, 2.2f, 0.0f, 0.0f, -fr * 0.86f);
    nd.addTriangle (-3.0f, 0.0f, 3.0f, 0.0f, 0.0f, fr * 0.16f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillPath (nd, juce::AffineTransform::rotation (an).translated (c.x + 2, c.y + 3));
    g.setColour (kInk);
    g.fillPath (nd, juce::AffineTransform::rotation (an).translated (c.x, c.y));
    g.setColour (kBrass);
    g.fillEllipse (c.x - 7, c.y - 7, 14, 14);
    g.setColour (kBrassLight);
    g.fillEllipse (c.x - 3, c.y - 4, 5, 4);
    // the glass
    juce::ColourGradient glass (juce::Colours::white.withAlpha (0.22f), c.x - fr * 0.4f, c.y - fr, juce::Colours::transparentWhite, c.x, c.y, false);
    g.setGradientFill (glass);
    g.fillEllipse (c.x - fr, c.y - fr, 2 * fr, 2 * fr * 0.8f);
}

// The blower (the chord organ's electric fan, spinning with its motor) or the bellows (the parlour organ's,
// breathing with the wind).
void PumpOrganEditor::drawBlower (juce::Graphics& g, juce::Rectangle<float> a)
{
    const bool chord = (int) proc.apvts.getRawParameterValue ("model")->load() == 0;
    const auto c = a.getCentre();
    const float R = 0.5f * std::min (a.getWidth(), a.getHeight()) - 2;
    g.setColour (kBrassLight);
    g.setFont (serif (10.0f, true));
    g.drawText (chord ? "BLOWER" : "BELLOWS", juce::Rectangle<float> (a.getX() - 10, a.getBottom() + 2, a.getWidth() + 20, 14), juce::Justification::centred);
    if (chord)
    {
        juce::ColourGradient housing (juce::Colour (0xff5a5650), c.x - R, c.y - R, juce::Colour (0xff1c1a18), c.x + R, c.y + R, false);
        g.setGradientFill (housing);
        g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
        g.setColour (juce::Colour (0xff0d0c0b));
        g.fillEllipse (c.x - R * 0.82f, c.y - R * 0.82f, 2 * R * 0.82f, 2 * R * 0.82f);
        const float m = proc.uiMotor.load();
        for (int b = 0; b < 5; ++b)
        {
            juce::Path blade;
            blade.addEllipse (-R * 0.17f, -R * 0.78f, R * 0.34f, R * 0.66f);
            g.setColour (juce::Colour (0xff9a958b).withAlpha (0.55f + 0.3f * (1.0f - m)));
            g.fillPath (blade, juce::AffineTransform::rotation (0.25f).rotated (fanAngle + 2 * kPi * b / 5.0f).translated (c.x, c.y));
        }
        if (m > 0.5f) // motion blur
        {
            g.setColour (juce::Colour (0xff9a958b).withAlpha (0.18f * m));
            g.fillEllipse (c.x - R * 0.78f, c.y - R * 0.78f, 2 * R * 0.78f, 2 * R * 0.78f);
        }
        g.setColour (kBrass);
        g.fillEllipse (c.x - 5, c.y - 5, 10, 10);
        // the grille
        g.setColour (juce::Colour (0xffb5afa4).withAlpha (0.5f));
        for (int i = -3; i <= 3; ++i)
        {
            const float y = c.y + i * R * 0.25f, hw = std::sqrt (std::max (0.0f, R * R * 0.8f - (y - c.y) * (y - c.y)));
            g.drawLine (c.x - hw, y, c.x + hw, y, 0.8f);
        }
    }
    else
    {
        const float open = juce::jlimit (0.0f, 1.0f, proc.uiSuction.load());
        const float hinge = a.getX() + 4, top = c.y - (6 + 16 * open), bot = c.y + (6 + 16 * open);
        juce::Path bel;
        bel.startNewSubPath (hinge, c.y);
        bel.lineTo (a.getRight() - 2, top);
        bel.lineTo (a.getRight() - 2, bot);
        bel.closeSubPath();
        g.setColour (juce::Colour (0xff3b2414));
        g.fillPath (bel);
        // the leather folds
        g.setColour (juce::Colour (0xff1d120a));
        for (int i = 1; i < 6; ++i)
        {
            const float x = hinge + (a.getRight() - 2 - hinge) * i / 6.0f, t = (x - hinge) / (a.getRight() - 2 - hinge);
            g.drawLine (x, c.y - (c.y - top) * t, x, c.y + (bot - c.y) * t, 1.2f);
        }
        g.setColour (kWalnutLight);
        g.drawLine (hinge, c.y, a.getRight() - 2, top, 3.0f);
        g.drawLine (hinge, c.y, a.getRight() - 2, bot, 3.0f);
        g.setColour (kBrass);
        g.fillEllipse (hinge - 4, c.y - 4, 8, 8);
    }
}

// A glass tube: in Easy mode the pedal (the wind), with a reservoir the air left in it, in Expression the strokes.
void PumpOrganEditor::drawTube (juce::Graphics& g, juce::Rectangle<float> a)
{
    const int mode = (int) proc.apvts.getRawParameterValue ("windmode")->load();
    const float wind = proc.apvts.getRawParameterValue ("wind")->load();
    const float v = mode == 1 ? proc.uiReservoir.load() : mode == 2 ? juce::jlimit (0.0f, 1.0f, proc.uiSuction.load()) : wind * proc.uiPedal.load();
    auto glass = a.reduced (4.0f, 8.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (glass, 6.0f);
    auto fill = glass.reduced (3.0f);
    fill = fill.withTrimmedTop (fill.getHeight() * (1.0f - juce::jlimit (0.0f, 1.0f, v)));
    juce::ColourGradient amber (kGlow.withAlpha (0.9f), fill.getX(), 0, kGlow.darker (0.6f), fill.getRight(), 0, false);
    g.setGradientFill (amber);
    g.fillRoundedRectangle (fill, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.2f));
    g.fillRoundedRectangle (glass.withWidth (5.0f).translated (3.0f, 0.0f), 2.5f);
    for (auto cap : { a.withHeight (9.0f), a.withTrimmedTop (a.getHeight() - 9.0f) })
        brassPlate (g, cap, 2.0f);
    g.setColour (kBrassLight);
    g.setFont (serif (10.0f, true));
    const char* label = mode == 1 ? "AIR" : mode == 2 ? "STROKE" : "PEDAL";
    g.drawText (label, juce::Rectangle<float> (a.getCentreX() - 30, a.getBottom() + 2, 60, 14), juce::Justification::centred);
}

// The swell shutters: louvres that turn open with the swell lever (the mod slider) and Forte, the reeds' glow
// behind them.
void PumpOrganEditor::drawShutters (juce::Graphics& g, juce::Rectangle<float> a)
{
    const float open = juce::jlimit (0.0f, 1.0f, proc.uiSwell.load());
    g.setColour (kBrassLight);
    g.setFont (serif (12.5f, true));
    auto title = a.removeFromTop (17);
    g.drawText ("Swell", title, juce::Justification::centredLeft);
    g.setFont (serif (10.5f, false, true));
    g.setColour (kBrassLight.withAlpha (0.7f));
    g.drawText ("mod slider", title, juce::Justification::centredRight);
    auto box = a.reduced (2.0f, 2.0f);
    brassPlate (g, box, 4.0f);
    auto inside = box.reduced (7.0f);
    // the glow of the sounding reeds behind
    float lv = 0;
    for (auto& k : proc.uiKeyLevel)
        lv = std::max (lv, k.load());
    juce::ColourGradient glow (kGlow.withAlpha (juce::jlimit (0.0f, 0.85f, 0.15f + 0.6f * lv)), inside.getCentreX(), inside.getCentreY(), juce::Colour (0xff120a05),
                               inside.getX(), inside.getY(), true);
    g.setColour (juce::Colour (0xff120a05));
    g.fillRect (inside);
    g.setGradientFill (glow);
    g.fillRect (inside);
    // the louvres
    const int n = 7;
    const float pitch = inside.getHeight() / n;
    for (int i = 0; i < n; ++i)
    {
        const float cy = inside.getY() + pitch * (i + 0.5f);
        const float hgt = pitch * (1.08f - 0.85f * open);
        auto slat = juce::Rectangle<float> (inside.getX(), cy - 0.5f * hgt, inside.getWidth(), hgt);
        juce::ColourGradient wood (kWalnutLight.brighter (0.1f * (1.0f - open)), 0, slat.getY(), kWalnutDark, 0, slat.getBottom(), false);
        g.setGradientFill (wood);
        g.fillRect (slat);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawHorizontalLine ((int) slat.getBottom(), slat.getX(), slat.getRight());
    }
    g.setColour (kInk.withAlpha (0.5f));
    g.drawRect (inside, 1.0f);
}

// The keyboard, C2 to C7: ivory and ebony, each key glowing with its reeds (amber, reddening and flickering as
// the wind struggles), coupled keys pressed by an unseen hand (a sage glow), latched keys pinned, ciphers
// whispering in lilac.
void PumpOrganEditor::drawKeyboard (juce::Graphics& g, juce::Rectangle<float> a)
{
    // the key bed: cheek blocks and the key slip
    juce::ColourGradient bed (kWalnutLight, a.getX(), a.getY(), kWalnutDark, a.getX(), a.getBottom(), false);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (a.translated (0, 4), 6.0f);
    g.setGradientFill (bed);
    g.fillRoundedRectangle (a, 6.0f);
    auto keys = a.reduced (60.0f, 0.0f).withTrimmedTop (26).withTrimmedBottom (8);
    // the key slip, with its brass legend
    {
        auto slip = juce::Rectangle<float> (keys.getX(), a.getY() + 6, keys.getWidth(), 16);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (slip);
        g.setFont (serif (10.5f, false, true));
        struct L { juce::Colour c; const char* t; };
        const L legend[] = { { kGlow, "sounding" }, { kSage, "coupled" }, { kBrass, "held by the drone" }, { kLilac, "cipher" } };
        float x = slip.getX() + 10;
        for (const auto& l : legend)
        {
            g.setColour (l.c);
            g.fillEllipse (x, slip.getCentreY() - 3.5f, 7, 7);
            g.setColour (kIvory.withAlpha (0.75f));
            g.drawText (l.t, juce::Rectangle<float> (x + 11, slip.getY(), 120, slip.getHeight()), juce::Justification::centredLeft);
            x += 30 + 6.5f * (float) strlen (l.t);
        }
    }
    const float st = proc.uiStruggle.load();
    const int whites = 36;
    const float ww = keys.getWidth() / whites;
    auto isBlack = [] (int n) { const int pc = n % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; };
    auto glowFor = [&] (int k) {
        const float lv = juce::jlimit (0.0f, 1.0f, proc.uiKeyLevel[(size_t) k].load() / 0.8f);
        const float flick = st * (0.5f + 0.5f * std::sin (phase * (9.0f + (float) (k % 7)) + (float) k * 1.7f));
        return std::make_pair (kGlow.interpolatedWith (kStruggle, juce::jlimit (0.0f, 1.0f, st * 1.3f)), lv * (1.0f - 0.55f * flick));
    };
    auto decorate = [&] (juce::Graphics& gg, juce::Rectangle<float> r, int k, bool black) {
        const int bits = proc.uiKeyBits[(size_t) k].load();
        const auto [col, amt] = glowFor (k);
        if ((bits & (PumpOrganProcessor::KeyHeld | PumpOrganProcessor::KeyLatched | PumpOrganProcessor::KeyCoupled)) != 0 && amt > 0.01f)
        {
            juce::ColourGradient glow (col.withAlpha ((black ? 0.85f : 0.8f) * amt), r.getCentreX(), r.getBottom() - r.getHeight() * 0.25f,
                                       col.withAlpha (0.0f), r.getCentreX(), r.getY(), false);
            gg.setGradientFill (glow);
            gg.fillRect (r);
        }
        if ((bits & PumpOrganProcessor::KeyCoupled) != 0)
        {
            gg.setColour (kSage.withAlpha (0.85f));
            gg.drawRect (r.reduced (1.5f), 2.0f);
        }
        if ((bits & PumpOrganProcessor::KeyCipher) != 0)
        {
            const float pulse = 0.35f + 0.25f * std::sin (phase * 3.0f + (float) k);
            gg.setColour (kLilac.withAlpha (pulse));
            gg.fillRect (r.withTrimmedTop (r.getHeight() * 0.3f));
        }
        if ((bits & PumpOrganProcessor::KeyLatched) != 0)
        {
            const float px = r.getCentreX(), py = r.getBottom() - (black ? 12.0f : 16.0f);
            gg.setColour (kBrassDark);
            gg.fillEllipse (px - 5, py - 5, 10, 10);
            gg.setColour (kBrassLight);
            gg.fillEllipse (px - 3, py - 3.5f, 5, 4);
        }
    };
    // the white keys
    int wi = 0;
    for (int n = ro::kLowKey; n <= ro::kHighKey; ++n)
    {
        if (isBlack (n)) continue;
        const int k = n - ro::kLowKey;
        const int bits = proc.uiKeyBits[(size_t) k].load();
        const bool down = (bits & (PumpOrganProcessor::KeyHeld | PumpOrganProcessor::KeyLatched)) != 0, ghost = (bits & PumpOrganProcessor::KeyCoupled) != 0;
        auto r = juce::Rectangle<float> (keys.getX() + ww * wi, keys.getY(), ww, keys.getHeight()).reduced (0.6f, 0.0f);
        if (down || ghost) r = r.withTrimmedBottom (down ? 3.0f : 1.5f);
        juce::ColourGradient iv (down ? kIvoryAged.darker (0.08f) : kIvory, r.getX(), r.getY(), kIvoryAged.darker (down ? 0.2f : 0.05f), r.getX(), r.getBottom(), false);
        g.setGradientFill (iv);
        g.fillRoundedRectangle (r, 2.5f);
        decorate (g, r, k, false);
        g.setColour (kInk.withAlpha (0.35f));
        g.drawRoundedRectangle (r, 2.5f, 0.8f);
        // the key's front lip
        g.setColour (juce::Colours::black.withAlpha (down ? 0.25f : 0.12f));
        g.fillRect (r.withTrimmedTop (r.getHeight() - 5.0f));
        if (n % 12 == 0)
        {
            g.setColour (inkDim());
            g.setFont (serif (9.0f, false, true));
            g.drawText (noteName (n), r.withTrimmedTop (r.getHeight() - 22).withTrimmedBottom (6), juce::Justification::centred);
        }
        ++wi;
    }
    // the black keys
    wi = 0;
    for (int n = ro::kLowKey; n <= ro::kHighKey; ++n)
    {
        if (! isBlack (n))
        {
            ++wi;
            continue;
        }
        const int k = n - ro::kLowKey;
        const int bits = proc.uiKeyBits[(size_t) k].load();
        const bool down = (bits & (PumpOrganProcessor::KeyHeld | PumpOrganProcessor::KeyLatched)) != 0;
        const float bw = ww * 0.6f, bh = keys.getHeight() * 0.6f;
        auto r = juce::Rectangle<float> (keys.getX() + ww * wi - bw * 0.5f, keys.getY(), bw, bh - (down ? 3.0f : 0.0f));
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (r.translated (1.5f, 3.0f), 2.0f);
        juce::ColourGradient eb (kEbony.brighter (down ? 0.05f : 0.25f), r.getX(), r.getY(), kEbony, r.getRight(), r.getBottom(), false);
        g.setGradientFill (eb);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (r.reduced (3.0f, 0.0f).withHeight (r.getHeight() - 8).withWidth (2.5f).translated (2.0f, 2.0f));
        decorate (g, r, k, true);
    }
    // the drone-hold lamp in the left cheek block (B1, just below the keyboard)
    {
        const auto r = holdLamp.toFloat();
        const bool on = proc.uiHolding.load();
        g.setColour (kBrassDark);
        g.fillEllipse (r);
        g.setColour (on ? kGlow : juce::Colour (0xff3a1a0a));
        g.fillEllipse (r.reduced (4.0f));
        if (on)
        {
            g.setColour (kGlow.withAlpha (0.3f));
            g.fillEllipse (r.expanded (6.0f));
        }
        g.setColour (kBrassLight);
        g.setFont (serif (10.0f, true));
        g.drawText ("HOLD", juce::Rectangle<float> (r.getX() - 12, r.getBottom() + 4, r.getWidth() + 24, 13), juce::Justification::centred);
        g.setFont (serif (9.0f, false, true));
        g.drawText ("key B1", juce::Rectangle<float> (r.getX() - 12, r.getBottom() + 17, r.getWidth() + 24, 12), juce::Justification::centred);
    }
}

void PumpOrganEditor::drawPlate (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    g.setColour (kRedInk);
    g.setFont (serif (17.0f, true, true));
    g.drawText (title, r.withHeight (22), juce::Justification::centredLeft);
    g.setColour (kInk.withAlpha (0.3f));
    g.drawHorizontalLine ((int) r.getY() + 24, r.getX(), r.getRight());
}

// Behind the panel: the service card, pinned to the back of the case.
void PumpOrganEditor::paintBack (juce::Graphics& g)
{
    const auto card = juce::Rectangle<float> (40, 84, kW - 80, kH - 84 - 30);
    paper (g, card);
    for (auto p : { card.getTopLeft().translated (14, 14), card.getTopRight().translated (-14, 14), card.getBottomLeft().translated (14, -14),
                    card.getBottomRight().translated (-14, -14) })
    {
        g.setColour (kBrassDark);
        g.fillEllipse (p.x - 5, p.y - 5, 10, 10);
        g.setColour (kBrassLight);
        g.fillEllipse (p.x - 3, p.y - 3.5f, 4, 3);
    }
    g.setColour (kInk);
    g.setFont (serif (24.0f, true, true));
    g.drawText ("Service Card", card.withHeight (44).translated (0, 12), juce::Justification::centred);
    g.setFont (serif (11.0f, false, true));
    g.setColour (inkDim());
    g.drawText (juce::String::fromUTF8 ("to be kept behind the back panel  \xc2\xb7  not to be removed"), card.withHeight (16).translated (0, 50), juce::Justification::centred);
    const char* titles[] = { "I.  The Wind", "II.  Touch", "III.  MPE", "IV.  Character" };
    for (int i = 0; i < 4; ++i)
        drawPlate (g, backAreas[(size_t) i].toFloat(), titles[i]);
    // notes
    g.setFont (serif (12.5f, false, true));
    g.setColour (inkDim());
    auto note = [&] (int area, float yOff, const juce::String& t) {
        const auto r = backAreas[(size_t) area].toFloat();
        g.drawFittedText (t, juce::Rectangle<float> (r.getX() + (area == 0 ? 320.0f : 0.0f), r.getY() + yOff, r.getWidth() - (area == 0 ? 320.0f : 0.0f), 80).toNearestInt(),
                          juce::Justification::topLeft, 5);
    };
    note (0, 34, "The expression pedal is the wind (times the Wind knob); until one is moved, the organ plays at the knob. "
                 "Mod slider: the swell lever. Sustain holds notes; sostenuto, or key B1, is Drone hold.");
    note (1, 214, "Pressure is aftertouch, MPE pressure or poly aftertouch.");
    note (2, 140, "In Ableton Live: right-click the plug-in's title bar, Enable MPE Mode.");
    note (3, 160, "Low-wind cut-out and warble, and Neglect, are on the front.");
}

void PumpOrganEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! learning)
    {
        if (e.eventComponent == &presetPlate)
            presetMenu();
        return;
    }
    for (auto* c = e.eventComponent; c != nullptr && c != &canvas; c = c->getParentComponent())
        if (auto it = controlParam.find (c); it != controlParam.end())
        {
            proc.learnTarget = proc.paramIndex (it->second);
            canvas.repaint();
            return;
        }
}

void PumpOrganEditor::paintOverCanvas (juce::Graphics& g)
{
    if (! learning || wizard.isVisible()) return;
    const int target = proc.learnTarget.load();
    for (const auto& [comp, id] : controlParam)
    {
        if (! comp->isVisible()) continue;
        const int pi = proc.paramIndex (id);
        int cc = -1;
        for (int i = 0; i < 128; ++i)
            if (proc.ccMap[(size_t) i].load() == pi) cc = i;
        const auto b = comp->getBounds().toFloat().expanded (3.0f);
        g.setColour (pi == target ? kGlow : cc >= 0 ? kSage : kBrass.withAlpha (0.6f));
        g.drawRoundedRectangle (b, 5.0f, pi == target ? 2.5f : cc >= 0 ? 2.0f : 1.0f);
        if (pi == target)
        {
            g.setFont (serif (11.0f, true, true));
            g.drawText ("move a control", b.withHeight (14.0f).translated (0.0f, -14.0f), juce::Justification::centred);
        }
        if (cc >= 0)
        {
            const auto tag = juce::Rectangle<float> (b.getRight() - 42.0f, b.getY(), 42.0f, 15.0f);
            g.setColour (kSage);
            g.fillRoundedRectangle (tag, 4.0f);
            g.setColour (kInk);
            g.setFont (serif (10.5f, true));
            g.drawText ("CC " + juce::String (cc), tag, juce::Justification::centred);
        }
    }
}

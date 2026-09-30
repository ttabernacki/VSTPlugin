#include "PluginEditor.h"

namespace
{
const juce::Colour kBg (0xff101418), kPanel (0xff171d23), kLine (0xff2c3640), kAccent (0xff4fd1c5),
    kAccent2 (0xfff6ad55), kText (0xffdde6ee), kDim (0xff7b8a97);

float wrapDeg (float a)
{
    a = std::fmod (a + 180.0f, 360.0f);
    if (a < 0.0f)
        a += 360.0f;
    return a - 180.0f;
}
} // namespace

// ------------------------------------------------------------------------ RadarPad

RadarPad::RadarPad (OrbitPanAudioProcessor& p)
    : proc (p), azParam (p.apvts.getParameter ("azimuth")), distParam (p.apvts.getParameter ("distance"))
{
    startTimerHz (30);
}

void RadarPad::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (4.0f);
    const auto c = b.getCentre();
    const float R = 0.5f * std::min (b.getWidth(), b.getHeight());

    g.setColour (kPanel);
    g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);

    // distance rings: 1 m, 3 m, 10 m
    g.setColour (kLine);
    for (float metres : { 1.0f, 3.0f, 10.0f })
    {
        const float d01 = std::log (metres / 0.3f) / std::log (50.0f);
        const float r = R * (kInner + (1.0f - kInner) * d01);
        g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.0f);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (juce::String ((int) metres) + " m", (int) (c.x + 3), (int) (c.y - r - 12), 36, 11, juce::Justification::left);
        g.setColour (kLine);
    }
    g.drawEllipse (c.x - R, c.y - R, 2 * R, 2 * R, 1.5f);
    g.drawLine (c.x - R, c.y, c.x + R, c.y);
    g.drawLine (c.x, c.y - R, c.x, c.y + R);

    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("FRONT", (int) (c.x - 30), (int) (b.getY() + 2), 60, 12, juce::Justification::centred);
    g.drawText ("BACK", (int) (c.x - 30), (int) (b.getBottom() - 14), 60, 12, juce::Justification::centred);
    g.drawText ("L", (int) b.getX() + 4, (int) (c.y - 6), 14, 12, juce::Justification::left);
    g.drawText ("R", (int) (b.getRight() - 16), (int) (c.y - 6), 14, 12, juce::Justification::right);

    // head: circle, ears, nose
    const float hr = R * 0.085f;
    g.setColour (kText.withAlpha (0.85f));
    g.fillEllipse (c.x - hr, c.y - hr, 2 * hr, 2 * hr);
    g.fillRoundedRectangle (c.x - hr - 3, c.y - 5, 4, 10, 2.0f);
    g.fillRoundedRectangle (c.x + hr - 1, c.y - 5, 4, 10, 2.0f);
    juce::Path nose;
    nose.addTriangle (c.x - 4, c.y - hr + 1, c.x + 4, c.y - hr + 1, c.x, c.y - hr - 7);
    g.fillPath (nose);

    auto toPoint = [&] (float azDeg, float d01) {
        const float a = juce::degreesToRadians (azDeg);
        const float r = R * (kInner + (1.0f - kInner) * d01);
        return juce::Point<float> (c.x + r * std::sin (a), c.y - r * std::cos (a));
    };

    const float d01 = distParam->getValue();
    const auto target = toPoint (azParam->convertFrom0to1 (azParam->getValue()), d01);
    const auto heard = toPoint (proc.heardAzimuth.load(), d01);

    // the set position (hollow) and where it is currently heard (filled; differs under orbit/smoothing)
    g.setColour (kAccent.withAlpha (0.35f));
    g.drawLine (c.x, c.y, heard.x, heard.y, 1.5f);
    g.setColour (kAccent);
    g.drawEllipse (target.x - 9, target.y - 9, 18, 18, 1.5f);
    g.fillEllipse (heard.x - 6, heard.y - 6, 12, 12);
}

void RadarPad::applyMouse (juce::Point<float> pt)
{
    const auto b = getLocalBounds().toFloat().reduced (4.0f);
    const auto c = b.getCentre();
    const float R = 0.5f * std::min (b.getWidth(), b.getHeight());
    const float dx = pt.x - c.x, dy = pt.y - c.y;

    // with orbit running, the dot is heard at (azimuth + offset): compensate so it lands under the mouse
    const float offset = wrapDeg (proc.heardAzimuth.load() - azParam->convertFrom0to1 (azParam->getValue()));
    const float angle = juce::radiansToDegrees (std::atan2 (dx, -dy));
    const float base = std::abs (proc.apvts.getRawParameterValue ("orbit")->load()) > 0.001f ? wrapDeg (angle - offset) : angle;
    azParam->setValueNotifyingHost (azParam->convertTo0to1 (wrapDeg (base)));

    const float r = std::sqrt (dx * dx + dy * dy) / R;
    distParam->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (r - kInner) / (1.0f - kInner)));
}

void RadarPad::mouseDown (const juce::MouseEvent& e)
{
    azParam->beginChangeGesture();
    distParam->beginChangeGesture();
    applyMouse (e.position);
}
void RadarPad::mouseDrag (const juce::MouseEvent& e) { applyMouse (e.position); }
void RadarPad::mouseUp (const juce::MouseEvent&)
{
    azParam->endChangeGesture();
    distParam->endChangeGesture();
}

// ------------------------------------------------------------------------ ElevationPad

ElevationPad::ElevationPad (OrbitPanAudioProcessor& p) : proc (p), elParam (p.apvts.getParameter ("elevation"))
{
    startTimerHz (30);
}

void ElevationPad::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (4.0f);
    g.setColour (kPanel);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (kLine);
    g.drawRoundedRectangle (b, 10.0f, 1.5f);

    const float cx = b.getCentreX();
    const float top = b.getY() + 16, bottom = b.getBottom() - 16;
    const float cy = 0.5f * (top + bottom);
    g.drawLine (b.getX() + 6, cy, b.getRight() - 6, cy);

    // head in profile, facing right
    const float hr = 16.0f;
    g.setColour (kText.withAlpha (0.85f));
    g.fillEllipse (cx - hr - 6, cy - hr, 2 * hr, 2 * hr);
    juce::Path nose;
    nose.addTriangle (cx + hr - 6, cy - 4, cx + hr - 6, cy + 4, cx + hr + 4, cy + 3);
    g.fillPath (nose);

    g.setColour (kDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("UP", b.toNearestInt().withHeight (14).translated (0, 2), juce::Justification::centred);
    g.drawText ("DOWN", b.toNearestInt().withTop ((int) b.getBottom() - 14), juce::Justification::centred);

    const float el = elParam->convertFrom0to1 (elParam->getValue());
    const float heardEl = proc.heardElevation.load();
    auto yOf = [&] (float e) { return cy - (e / 90.0f) * (cy - top); };
    g.setColour (kAccent2);
    g.drawEllipse (cx - 9, yOf (el) - 9, 18, 18, 1.5f);
    g.fillEllipse (cx - 6, yOf (heardEl) - 6, 12, 12);
}

void ElevationPad::applyMouse (juce::Point<float> pt)
{
    const auto b = getLocalBounds().toFloat().reduced (4.0f);
    const float top = b.getY() + 16, bottom = b.getBottom() - 16;
    const float e01 = 1.0f - juce::jlimit (0.0f, 1.0f, (pt.y - top) / (bottom - top));
    elParam->setValueNotifyingHost (e01);
}

void ElevationPad::mouseDown (const juce::MouseEvent& e)
{
    elParam->beginChangeGesture();
    applyMouse (e.position);
}
void ElevationPad::mouseDrag (const juce::MouseEvent& e) { applyMouse (e.position); }
void ElevationPad::mouseUp (const juce::MouseEvent&) { elParam->endChangeGesture(); }

// ------------------------------------------------------------------------ Editor

void OrbitPanEditor::addKnob (Knob& k, const juce::String& id, const juce::String& text)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    k.slider.setColour (juce::Slider::thumbColourId, kText);
    k.slider.setColour (juce::Slider::textBoxTextColourId, kText);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
    addAndMakeVisible (k.slider);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, kDim);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
}

OrbitPanEditor::OrbitPanEditor (OrbitPanAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), radar (p), elevation (p)
{
    setSize (640, 470);
    title.setText ("ORBITPAN", juce::dontSendNotification);
    title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, kAccent);
    addAndMakeVisible (title);
    addAndMakeVisible (radar);
    addAndMakeVisible (elevation);
    addKnob (azimuth, "azimuth", "Azimuth");
    addKnob (elev, "elevation", "Elevation");
    addKnob (distance, "distance", "Distance");
    addKnob (focus, "focus", "Focus");
    addKnob (room, "room", "Room");
    addKnob (decay, "decay", "Decay");
    addKnob (orbit, "orbit", "Orbit");
}

void OrbitPanEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Binaural 3D panner - use headphones", getLocalBounds().removeFromTop (34).withTrimmedRight (16),
                juce::Justification::centredRight);
}

void OrbitPanEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto header = r.removeFromTop (26);
    title.setBounds (header.removeFromLeft (200));

    auto knobs = r.removeFromBottom (110);
    auto top = r;
    radar.setBounds (top.removeFromLeft (top.getHeight()));
    top.removeFromLeft (10);
    elevation.setBounds (top.removeFromLeft (110));

    const int w = knobs.getWidth() / 7;
    for (auto* k : { &azimuth, &elev, &distance, &focus, &room, &decay, &orbit })
    {
        auto cell = knobs.removeFromLeft (w);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell);
    }
}

#include "PluginEditor.h"

namespace
{
const juce::Colour kBg (0xff0e1216), kPanel (0xff161c22), kPanel2 (0xff1b232b), kLine (0xff2a343e),
    kAccent (0xff4fd1c5), kWarm (0xfff6ad55), kText (0xffdde6ee), kDim (0xff7b8a97);

float wrapDeg (float a)
{
    a = std::fmod (a + 180.0f, 360.0f);
    if (a < 0.0f)
        a += 360.0f;
    return a - 180.0f;
}

float paramValue (juce::RangedAudioParameter* p) { return p->convertFrom0to1 (p->getValue()); }

void setWithGesture (juce::RangedAudioParameter* p, float plainValue)
{
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    p->endChangeGesture();
}

void darkTextBox (juce::Slider& s)
{
    s.setColour (juce::Slider::textBoxTextColourId, kText);
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxHighlightColourId, kAccent.withAlpha (0.4f));
}

void drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
{
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (text, area.withHeight (16), juce::Justification::centredLeft);
    g.setColour (kLine);
    g.drawHorizontalLine (area.getY() + 17, (float) area.getX(), (float) area.getRight());
}
} // namespace

// ------------------------------------------------------------------ LookAndFeel

OrbitLookAndFeel::OrbitLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, kText);
}

void OrbitLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle,
                                         float endAngle, juce::Slider& s)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = 0.5f * std::min (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const float arcR = r - 3.0f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;

    juce::Path track, value;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, from, angle, true);
    g.setColour (kLine);
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (s.isEnabled() ? kAccent : kDim);
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float ir = arcR - 7.0f;
    g.setColour (kPanel2);
    g.fillEllipse (c.x - ir, c.y - ir, 2 * ir, 2 * ir);
    g.setColour (kText);
    g.drawLine (c.x + ir * 0.35f * std::sin (angle), c.y - ir * 0.35f * std::cos (angle), c.x + ir * 0.9f * std::sin (angle),
                c.y - ir * 0.9f * std::cos (angle), 2.0f);
}

void OrbitLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float, float,
                                         juce::Slider::SliderStyle, juce::Slider& s)
{
    const float cy = (float) y + 0.5f * (float) h;
    const float left = (float) x + 6.0f, right = (float) (x + w) - 6.0f;
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float centreX = bipolar ? 0.5f * (left + right) : left;

    g.setColour (kLine);
    g.fillRoundedRectangle (left, cy - 3.0f, right - left, 6.0f, 3.0f);
    if (bipolar)
        g.drawVerticalLine ((int) centreX, cy - 9.0f, cy + 9.0f);

    // colour runs from cool (far) through accent to warm (close)
    const float t = (float) s.valueToProportionOfLength (s.getValue());
    const auto col = t < 0.5f ? kDim.interpolatedWith (kAccent, t * 2.0f) : kAccent.interpolatedWith (kWarm, (t - 0.5f) * 2.0f);
    g.setColour (col);
    g.fillRoundedRectangle (std::min (centreX, sliderPos), cy - 3.0f, std::abs (sliderPos - centreX), 6.0f, 3.0f);
    g.setColour (kText);
    g.fillEllipse (sliderPos - 8.0f, cy - 8.0f, 16.0f, 16.0f);
    g.setColour (col);
    g.fillEllipse (sliderPos - 4.0f, cy - 4.0f, 8.0f, 8.0f);
}

// ------------------------------------------------------------------ RadarPad

RadarPad::RadarPad (OrbitPanAudioProcessor& p)
    : proc (p),
      azParam (p.apvts.getParameter ("azimuth")),
      distParam (p.apvts.getParameter ("distance")),
      depthParam (p.apvts.getParameter ("depth"))
{
    startTimerHz (30);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

juce::Point<float> RadarPad::toPoint (juce::Point<float> c, float R, float azDeg, float d01) const
{
    const float a = juce::degreesToRadians (azDeg);
    const float r = R * (kInner + (1.0f - kInner) * d01);
    return { c.x + r * std::sin (a), c.y - r * std::cos (a) };
}

void RadarPad::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (2.0f);
    const auto c = b.getCentre();
    const float R = 0.5f * std::min (b.getWidth(), b.getHeight());

    juce::Path disc;
    disc.addEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
    g.setGradientFill (juce::ColourGradient (kPanel2, c.x, c.y, kPanel, c.x + R, c.y, true));
    g.fillPath (disc);

    // the half of the room behind the listener is shaded: that is where the Rear cues apply
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (disc);
        g.setColour (kBg.withAlpha (0.6f));
        g.fillRect (b.getX(), c.y, b.getWidth(), R + 2.0f);
    }

    // distance rings
    g.setFont (juce::FontOptions (10.0f));
    for (float metres : { 1.0f, 3.0f, 10.0f })
    {
        const float d01 = std::log (metres / 0.3f) / std::log (50.0f);
        const float r = R * (kInner + (1.0f - kInner) * d01);
        g.setColour (kLine);
        g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.0f);
        const float lx = c.x + r * 0.7071f, ly = c.y - r * 0.7071f;
        const auto pill = juce::Rectangle<float> (lx - 2.0f, ly - 7.0f, 34.0f, 14.0f);
        g.setColour (kPanel.withAlpha (0.85f));
        g.fillRoundedRectangle (pill, 4.0f);
        g.setColour (kDim);
        g.drawText (juce::String ((int) metres) + " m", pill.toNearestInt().withTrimmedLeft (4), juce::Justification::centredLeft);
    }
    g.setColour (kLine);
    g.drawEllipse (c.x - R, c.y - R, 2 * R, 2 * R, 1.5f);
    g.drawLine (c.x - R, c.y, c.x + R, c.y, 0.8f);
    g.drawLine (c.x, c.y - R, c.x, c.y + R, 0.8f);
    // 45 degree guides
    const float dd = R * 0.7071f;
    g.drawLine (c.x - dd, c.y - dd, c.x + dd, c.y + dd, 0.5f);
    g.drawLine (c.x - dd, c.y + dd, c.x + dd, c.y - dd, 0.5f);

    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("FRONT", (int) (c.x - 30), (int) (b.getY() + 4), 60, 12, juce::Justification::centred);
    g.drawText ("BEHIND", (int) (c.x - 30), (int) (b.getBottom() - 16), 60, 12, juce::Justification::centred);
    g.drawText ("L", (int) b.getX() + 6, (int) (c.y - 6), 14, 12, juce::Justification::left);
    g.drawText ("R", (int) (b.getRight() - 20), (int) (c.y - 6), 14, 12, juce::Justification::right);

    // the listener: head, ears, nose (nose = front)
    const float hr = R * 0.085f;
    g.setColour (kText.withAlpha (0.9f));
    g.fillEllipse (c.x - hr, c.y - hr, 2 * hr, 2 * hr);
    g.fillRoundedRectangle (c.x - hr - 3, c.y - 5, 4, 10, 2.0f);
    g.fillRoundedRectangle (c.x + hr - 1, c.y - 5, 4, 10, 2.0f);
    juce::Path nose;
    nose.addTriangle (c.x - 4, c.y - hr + 1, c.x + 4, c.y - hr + 1, c.x, c.y - hr - 7);
    g.fillPath (nose);

    // source
    const float d01 = distParam->getValue();
    const float setAz = paramValue (azParam);
    const float depth = paramValue (depthParam);
    const auto target = toPoint (c, R, setAz, d01);
    const auto heard = toPoint (c, R, proc.heardAzimuth.load(), d01);

    // short trail (shows motion from Orbit or host automation)
    if (trail.empty() || trail.back().getDistanceFrom (heard) > 0.5f)
        trail.push_back (heard);
    while (trail.size() > 28)
        trail.pop_front();
    for (size_t i = 1; i < trail.size(); ++i)
    {
        g.setColour (kAccent.withAlpha (0.35f * (float) i / (float) trail.size()));
        g.drawLine (trail[i - 1].x, trail[i - 1].y, trail[i].x, trail[i].y, 2.0f);
    }

    // depth colours and sizes the source: far = small, dim, cool; close = large, bright, warm
    const float near = std::max (0.0f, depth), far = std::max (0.0f, -depth);
    const auto col = kAccent.interpolatedWith (kWarm, near).interpolatedWith (kDim, far * 0.7f);
    const float dotR = 7.0f + 6.0f * near - 3.0f * far;
    const float haloR = dotR * (2.4f + 1.2f * near);
    g.setGradientFill (juce::ColourGradient (col.withAlpha (0.45f * (1.0f - 0.6f * far)), heard.x, heard.y, col.withAlpha (0.0f),
                                             heard.x + haloR, heard.y, true));
    g.fillEllipse (heard.x - haloR, heard.y - haloR, 2 * haloR, 2 * haloR);
    g.setColour (col.withAlpha (0.4f));
    g.drawLine (c.x, c.y, heard.x, heard.y, 1.2f);
    g.setColour (col);
    g.fillEllipse (heard.x - dotR, heard.y - dotR, 2 * dotR, 2 * dotR);
    g.setColour (kText);
    g.drawEllipse (heard.x - dotR, heard.y - dotR, 2 * dotR, 2 * dotR, 1.5f);
    if (target.getDistanceFrom (heard) > 3.0f) // Orbit or smoothing: hollow ring marks where it is set
    {
        g.setColour (col.withAlpha (0.8f));
        g.drawEllipse (target.x - 9, target.y - 9, 18, 18, 1.2f);
    }

    // readout
    const float az = wrapDeg (setAz);
    juce::String where = std::abs (az) < 2.0f ? "front" : (std::abs (az) > 178.0f ? "behind" : (az > 0 ? "right " : "left ") + juce::String (juce::roundToInt (std::abs (az))) + juce::String::fromUTF8 ("\xC2\xB0") + (std::abs (az) > 92.0f ? " (behind)" : ""));
    g.setColour (kText.withAlpha (0.85f));
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    auto readout = getLocalBounds().removeFromBottom (30).withTrimmedLeft (6);
    g.drawText (where, readout.removeFromTop (15), juce::Justification::centredLeft);
    g.setColour (kDim);
    g.drawText (juce::String (OrbitPanAudioProcessor::distanceToMetres (d01), 1) + " m from head", readout, juce::Justification::centredLeft);
}

void RadarPad::applyMouse (juce::Point<float> pt)
{
    const auto b = getLocalBounds().toFloat().reduced (2.0f);
    const auto c = b.getCentre();
    const float R = 0.5f * std::min (b.getWidth(), b.getHeight());
    const float dx = pt.x - c.x, dy = pt.y - c.y;

    // with Orbit running the dot is heard at (azimuth + offset): compensate so it lands under the mouse
    const float offset = wrapDeg (proc.heardAzimuth.load() - paramValue (azParam));
    const float angle = juce::radiansToDegrees (std::atan2 (dx, -dy));
    const bool orbiting = std::abs (proc.apvts.getRawParameterValue ("orbit")->load()) > 0.001f;
    azParam->setValueNotifyingHost (azParam->convertTo0to1 (wrapDeg (orbiting ? angle - offset : angle)));

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

void RadarPad::mouseDoubleClick (const juce::MouseEvent&)
{
    setWithGesture (azParam, 0.0f);
    distParam->beginChangeGesture();
    distParam->setValueNotifyingHost (distParam->getDefaultValue());
    distParam->endChangeGesture();
}

void RadarPad::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    distParam->beginChangeGesture();
    distParam->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, distParam->getValue() - wheel.deltaY * 0.15f));
    distParam->endChangeGesture();
}

// ------------------------------------------------------------------ ElevationPad

ElevationPad::ElevationPad (OrbitPanAudioProcessor& p) : proc (p), elParam (p.apvts.getParameter ("elevation"))
{
    startTimerHz (30);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

float ElevationPad::yOf (float el, juce::Rectangle<float> track) const
{
    return track.getCentreY() - (el / 90.0f) * 0.5f * track.getHeight();
}

void ElevationPad::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (kPanel);
    g.fillRoundedRectangle (b, 12.0f);
    g.setColour (kLine);
    g.drawRoundedRectangle (b, 12.0f, 1.5f);

    const auto track = b.reduced (0.0f, 30.0f);
    const float cx = b.getCentreX();
    const float cy = track.getCentreY();

    // tick marks
    g.setFont (juce::FontOptions (9.0f));
    for (int e : { 90, 45, 0, -45, -90 })
    {
        const float y = yOf ((float) e, track);
        g.setColour (kLine);
        g.drawLine (b.getX() + 10, y, b.getRight() - 10, y, e == 0 ? 1.2f : 0.6f);
        g.setColour (kDim);
        g.drawText (juce::String (e), (int) b.getX() + 4, (int) y - 12, 30, 10, juce::Justification::left);
    }

    // head in profile, facing right, at ear height
    const float hr = 15.0f;
    g.setColour (kText.withAlpha (0.85f));
    g.fillEllipse (cx - hr - 4, cy - hr, 2 * hr, 2 * hr);
    juce::Path nose;
    nose.addTriangle (cx + hr - 4, cy - 4, cx + hr - 4, cy + 4, cx + hr + 5, cy + 3);
    g.fillPath (nose);

    g.setColour (kDim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("UP", b.toNearestInt().withHeight (22).translated (0, 4), juce::Justification::centred);
    g.drawText ("DOWN", b.toNearestInt().withTop ((int) b.getBottom() - 22), juce::Justification::centred);

    const float el = paramValue (elParam);
    const float heardEl = proc.heardElevation.load();
    g.setColour (kWarm.withAlpha (0.35f));
    g.drawLine (cx + 6, cy, cx + 6, yOf (heardEl, track), 1.2f);
    g.setColour (kWarm);
    g.fillEllipse (cx + 6 - 7, yOf (heardEl, track) - 7, 14, 14);
    g.setColour (kText);
    g.drawEllipse (cx + 6 - 7, yOf (heardEl, track) - 7, 14, 14, 1.5f);
    if (std::abs (yOf (el, track) - yOf (heardEl, track)) > 3.0f)
        g.drawEllipse (cx + 6 - 9, yOf (el, track) - 9, 18, 18, 1.0f);

    g.setColour (kText.withAlpha (0.85f));
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    g.drawText ((el > 0 ? "+" : "") + juce::String (juce::roundToInt (el)) + juce::String::fromUTF8 ("\xC2\xB0"),
                b.toNearestInt().removeFromBottom (44).removeFromTop (20), juce::Justification::centred);
}

void ElevationPad::applyMouse (juce::Point<float> pt)
{
    const auto track = getLocalBounds().toFloat().reduced (2.0f).reduced (0.0f, 30.0f);
    const float e01 = 1.0f - juce::jlimit (0.0f, 1.0f, (pt.y - track.getY()) / track.getHeight());
    elParam->setValueNotifyingHost (e01);
}

void ElevationPad::mouseDown (const juce::MouseEvent& e)
{
    elParam->beginChangeGesture();
    applyMouse (e.position);
}
void ElevationPad::mouseDrag (const juce::MouseEvent& e) { applyMouse (e.position); }
void ElevationPad::mouseUp (const juce::MouseEvent&) { elParam->endChangeGesture(); }
void ElevationPad::mouseDoubleClick (const juce::MouseEvent&) { setWithGesture (elParam, 0.0f); }

void ElevationPad::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    elParam->beginChangeGesture();
    elParam->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, elParam->getValue() + wheel.deltaY * 0.1f));
    elParam->endChangeGesture();
}

// ------------------------------------------------------------------ Editor

void OrbitPanEditor::addKnob (Knob& k, const juce::String& id, const juce::String& text)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    darkTextBox (k.slider);
    k.slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
    addAndMakeVisible (k.slider);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, kDim);
    k.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
}

OrbitPanEditor::OrbitPanEditor (OrbitPanAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), radar (p), elevation (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (radar);
    addAndMakeVisible (elevation);

    depthSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 84, 20);
    darkTextBox (depthSlider);
    depthSlider.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (depthSlider);
    depthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "depth", depthSlider);

    addKnob (focus, "focus", "FOCUS");
    addKnob (rear, "rear", "REAR");
    addKnob (room, "room", "ROOM");
    addKnob (decay, "decay", "DECAY");
    addKnob (orbit, "orbit", "ORBIT");
    orbit.slider.setTextValueSuffix (" Hz");

    setResizable (true, true);
    setResizeLimits (640, 440, 1200, 820);
    getConstrainer()->setFixedAspectRatio (780.0 / 520.0);
    setSize (780, 520);
}

OrbitPanEditor::~OrbitPanEditor()
{
    setLookAndFeel (nullptr);
}

void OrbitPanEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("ORBITPAN", 16, 8, 200, 24, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Binaural 3D panner - use headphones. Drag the dot: angle = direction, distance from head = distance.",
                getLocalBounds().removeFromTop (38).withTrimmedLeft (130).withTrimmedRight (16), juce::Justification::centredRight);

    drawSectionTitle (g, padArea, "POSITION  (top-down view)");
    drawSectionTitle (g, elevArea, "HEIGHT");
    drawSectionTitle (g, depthArea, "DEPTH  far  <->  close");
    drawSectionTitle (g, tuningArea, "LOCALISATION");
    drawSectionTitle (g, spaceArea, "ROOM");
    drawSectionTitle (g, motionArea, "MOTION");
}

void OrbitPanEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (26); // title row

    // right-hand column of knobs
    const int colW = juce::jlimit (170, 260, (int) (r.getWidth() * 0.27f));
    auto knobCol = r.removeFromRight (colW);
    r.removeFromRight (12);

    // elevation strip
    const int elevW = juce::jlimit (70, 110, (int) (r.getWidth() * 0.15f));
    auto elevCol = r.removeFromRight (elevW);
    r.removeFromRight (10);

    // pad (square) with the depth strip under it
    const int depthH = 58;
    auto padCol = r;
    auto depthRow = padCol.removeFromBottom (depthH);
    padCol.removeFromBottom (6);
    padArea = padCol.removeFromTop (20);
    const int side = std::min (padCol.getWidth(), padCol.getHeight());
    radar.setBounds (padCol.withSizeKeepingCentre (side, side));
    depthArea = depthRow.withTrimmedRight (0);
    depthSlider.setBounds (depthRow.withTrimmedTop (22));

    elevArea = elevCol.removeFromTop (20);
    elevation.setBounds (elevCol.withHeight (side).withTrimmedBottom (0));

    // knob groups: LOCALISATION (focus, rear) / ROOM (room, decay) / MOTION (orbit)
    const int rows = 3;
    const int rowH = knobCol.getHeight() / rows;
    auto placeRow = [&] (juce::Rectangle<int> row, juce::Rectangle<int>& titleArea, std::initializer_list<Knob*> knobs) {
        titleArea = row.withHeight (20);
        auto cells = row.withTrimmedTop (22);
        const int w = cells.getWidth() / 2;
        for (auto* k : knobs)
        {
            auto cell = cells.removeFromLeft (w);
            k->label.setBounds (cell.removeFromTop (14));
            k->slider.setBounds (cell);
        }
    };
    placeRow (knobCol.removeFromTop (rowH), tuningArea, { &focus, &rear });
    placeRow (knobCol.removeFromTop (rowH), spaceArea, { &room, &decay });
    placeRow (knobCol, motionArea, { &orbit });
}

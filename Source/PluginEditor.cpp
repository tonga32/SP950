#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

namespace
{
    constexpr float kScale = 0.5f;
    constexpr float kMaxTune = SP950AudioProcessor::kMaxTune;

    constexpr float kSliderTop = 602.0f, kSliderLen = 576.0f;
    const Rectangle<float> sliderArea (215.0f, 560.0f, 300.0f, 680.0f);

    const Point<float> knobCentre[3] = { { 665.0f, 665.0f }, { 665.0f, 903.0f }, { 665.0f, 1140.0f } };
    const char* const knobId[3] = { "in", "out", "filter" };

    const Rectangle<float> fineBox  (224.0f, 1283.0f, 86.0f, 86.0f);
    const Rectangle<float> extBox   (428.0f, 1283.0f, 86.0f, 86.0f);
    const Rectangle<float> comboBox (588.0f, 45.0f, 255.0f, 58.0f);
    const Rectangle<float> bypassBox(588.0f, 122.0f, 255.0f, 64.0f);

    float tuneToY (float t) { return kSliderTop + (t / kMaxTune) * kSliderLen; }
}

SP950Editor::SP950Editor (SP950AudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    panel = ImageCache::getFromMemory (BinaryData::panel_png, BinaryData::panel_pngSize);
    setSize (488, 806);
    setResizable (false, false);
    startTimerHz (30);
}

SP950Editor::~SP950Editor()
{
    stopTimer();
}

// ---------------------------------------------------------------- ayudas
float SP950Editor::getNorm (const char* id) const
{
    if (auto* p = proc.apvts.getParameter (id)) return p->getValue();
    return 0.0f;
}

bool SP950Editor::getBool (const char* id) const
{
    return proc.apvts.getRawParameterValue (id)->load() > 0.5f;
}

float SP950Editor::getTune() const
{
    return proc.apvts.getRawParameterValue ("tune")->load();
}

void SP950Editor::beginDrag (const char* id)
{
    activeParam = proc.apvts.getParameter (id);
    if (activeParam != nullptr) activeParam->beginChangeGesture();
}

void SP950Editor::setActiveNorm (float norm)
{
    if (activeParam != nullptr)
        activeParam->setValueNotifyingHost (jlimit (0.0f, 1.0f, norm));
}

void SP950Editor::clickToggle (const char* id)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

void SP950Editor::setChoice (int index)
{
    if (auto* p = proc.apvts.getParameter ("mode"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
        p->endChangeGesture();
    }
}

void SP950Editor::resetParam (const char* id)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
}

void SP950Editor::applySliderPos (float y)
{
    float t = (y - kSliderTop) / kSliderLen * kMaxTune;
    t = jlimit (0.0f, kMaxTune, t);
    if (! getBool ("fine"))
        t = jmin (kMaxTune, std::round (t));
    setActiveNorm (t / kMaxTune);
}

// ---------------------------------------------------------------- ratón
void SP950Editor::mouseDown (const MouseEvent& e)
{
    const Point<float> p = e.position / kScale;
    dragMode = Drag::none;

    if (sliderArea.contains (p))
    {
        beginDrag ("tune");
        dragMode = Drag::slider;
        const float hy = tuneToY (getTune());
        const Rectangle<float> handle (271.0f, hy - 36.0f, 186.0f, 72.0f);
        dragOffset = handle.contains (p) ? (p.y - hy) : 0.0f;
        applySliderPos (p.y - dragOffset);
        return;
    }

    for (int i = 0; i < 3; ++i)
    {
        if (p.getDistanceFrom (knobCentre[i]) < 90.0f)
        {
            beginDrag (knobId[i]);
            dragMode = Drag::knob;
            dragStartY = p.y;
            dragStartNorm = getNorm (knobId[i]);
            return;
        }
    }

    if (fineBox.contains (p))   { clickToggle ("fine");   return; }
    if (extBox.contains (p))    { clickToggle ("ext");    return; }
    if (bypassBox.contains (p)) { clickToggle ("bypass"); return; }

    if (comboBox.contains (p))
    {
        const int current = (int) std::lround (proc.apvts.getRawParameterValue ("mode")->load());

        PopupMenu m;
        m.addItem (1, "Stereo",      true, current == 0);
        m.addItem (2, "Stereo Mid",  true, current == 1);
        m.addItem (3, "Stereo Side", true, current == 2);

        const Rectangle<int> area ((int) (comboBox.getX() * kScale), (int) (comboBox.getY() * kScale),
                                   (int) (comboBox.getWidth() * kScale), (int) (comboBox.getHeight() * kScale));

        Component::SafePointer<SP950Editor> safe (this);
        m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (area)),
                         [safe] (int result)
                         {
                             if (safe != nullptr && result > 0)
                                 safe->setChoice (result - 1);
                         });
    }
}

void SP950Editor::mouseDrag (const MouseEvent& e)
{
    const Point<float> p = e.position / kScale;

    if (dragMode == Drag::slider)
        applySliderPos (p.y - dragOffset);
    else if (dragMode == Drag::knob)
        setActiveNorm (dragStartNorm + (dragStartY - p.y) / 300.0f);
}

void SP950Editor::mouseUp (const MouseEvent&)
{
    if (activeParam != nullptr)
    {
        activeParam->endChangeGesture();
        activeParam = nullptr;
    }
    dragMode = Drag::none;
}

void SP950Editor::mouseDoubleClick (const MouseEvent& e)
{
    const Point<float> p = e.position / kScale;

    if (sliderArea.contains (p)) { resetParam ("tune"); return; }

    for (int i = 0; i < 3; ++i)
        if (p.getDistanceFrom (knobCentre[i]) < 90.0f)
            resetParam (knobId[i]);
}

// ---------------------------------------------------------------- dibujo
void SP950Editor::paint (Graphics& g)
{
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImage (panel, getLocalBounds().toFloat(), RectanglePlacement::stretchToFit);

    g.addTransform (AffineTransform::scale (kScale));

    drawLcd (g);
    drawSlider (g);
    for (int i = 0; i < 3; ++i)
        drawKnob (g, knobCentre[i], getNorm (knobId[i]));

    drawToggle (g, fineBox, getBool ("fine"));
    drawToggle (g, extBox,  getBool ("ext"));

    // menú de modo
    drawButtonBox (g, comboBox, false);
    g.setColour (Colour (0xff111111));
    g.setFont (Font (34.0f));
    g.drawText (proc.apvts.getParameter ("mode")->getCurrentValueAsText(),
                comboBox.withTrimmedRight (50.0f), Justification::centred, false);
    Path arrow;
    arrow.addTriangle (comboBox.getRight() - 52.0f, comboBox.getY() + 20.0f,
                       comboBox.getRight() - 18.0f, comboBox.getY() + 20.0f,
                       comboBox.getRight() - 35.0f, comboBox.getY() + 38.0f);
    g.fillPath (arrow);

    // botón bypass
    const bool byp = getBool ("bypass");
    drawButtonBox (g, bypassBox, byp);
    g.setColour (Colour (0xff111111));
    g.setFont (Font (34.0f));
    g.drawText ("Bypass", bypassBox, Justification::centred, false);
}

void SP950Editor::drawLcd (Graphics& g)
{
    const Rectangle<float> r (165.0f, 288.0f, 647.0f, 184.0f);

    g.setGradientFill (ColourGradient (Colour (0xffff6e1e), r.getX(), r.getY(),
                                       Colour (0xffff4a0f), r.getX(), r.getBottom(), false));
    g.fillRect (r);

    g.setColour (Colour (0x1a000000));
    for (float x = r.getX(); x < r.getRight(); x += 8.0f)
        g.fillRect (x, r.getY(), 1.0f, r.getHeight());
    for (float y = r.getY(); y < r.getBottom(); y += 8.0f)
        g.fillRect (r.getX(), y, r.getWidth(), 1.0f);

    const float tune = getTune();
    const bool fine = getBool ("fine");
    String tuneStr;
    if (fine)
        tuneStr = (tune > 0.049f ? "+" : "") + String (tune, 1);
    else
        tuneStr = (std::lround (tune) > 0 ? "+" : "") + String ((int) std::lround (tune));

    const float filterVal = proc.apvts.getRawParameterValue ("filter")->load();
    const String filterStr (String ((int) std::lround (filterVal)));

    const Colour ink (0xff2b0d03);
    g.setColour (ink);
    g.setFont (Font (Font::getDefaultMonospacedFontName(), 56.0f, Font::bold));

    g.drawText ("Tune:",   Rectangle<float> (208.0f, 300.0f, 200.0f, 70.0f), Justification::centredLeft, false);
    g.drawText (tuneStr,   Rectangle<float> (380.0f, 300.0f, 180.0f, 70.0f), Justification::centredRight, false);
    g.drawText ("Filter:", Rectangle<float> (208.0f, 380.0f, 240.0f, 70.0f), Justification::centredLeft, false);
    g.drawText (filterStr, Rectangle<float> (380.0f, 380.0f, 180.0f, 70.0f), Justification::centredRight, false);

    // barras de bloques
    const float tuneFrac   = jlimit (0.0f, 1.0f, tune / kMaxTune);
    const float filterFrac = jlimit (0.0f, 1.0f, filterVal / 99.0f);
    for (int line = 0; line < 2; ++line)
    {
        const float frac = line == 0 ? tuneFrac : filterFrac;
        const int lit = (int) std::lround (frac * 10.0f);
        const float y = line == 0 ? 318.0f : 398.0f;
        for (int b = 0; b < 10; ++b)
        {
            g.setColour (ink.withAlpha (b < lit ? 0.85f : 0.22f));
            g.fillRect (575.0f + (float) b * 20.0f, y, 17.0f, 40.0f);
        }
    }
}

void SP950Editor::drawSlider (Graphics& g)
{
    const float y = tuneToY (getTune());
    const Rectangle<float> h (271.0f, y - 36.0f, 186.0f, 72.0f);

    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (h.translated (0.0f, 8.0f).expanded (4.0f, 0.0f), 10.0f);

    g.setGradientFill (ColourGradient (Colour (0xff3c3c3c), h.getX(), h.getY(),
                                       Colour (0xff0c0c0c), h.getX(), h.getBottom(), false));
    g.fillRoundedRectangle (h, 9.0f);

    g.setColour (Colour (0xff555555));
    g.drawRoundedRectangle (h.reduced (1.0f), 9.0f, 2.0f);

    g.setColour (Colour (0xfff2efe6));
    g.fillRect (h.getX() + 8.0f, h.getCentreY() - 5.0f, h.getWidth() - 16.0f, 10.0f);
}

void SP950Editor::drawKnob (Graphics& g, Point<float> c, float norm)
{
    const float R = 80.0f;

    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillEllipse (c.x - R - 4.0f, c.y - R + 3.0f, 2.0f * R + 8.0f, 2.0f * R + 8.0f);

    g.setGradientFill (ColourGradient (Colour (0xff4a4a4a), c.x - 35.0f, c.y - 45.0f,
                                       Colour (0xff070707), c.x + R, c.y + R, true));
    g.fillEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R);

    const float r2 = R - 15.0f;
    g.setGradientFill (ColourGradient (Colour (0xff222222), c.x - 25.0f, c.y - 30.0f,
                                       Colour (0xff050505), c.x + r2, c.y + r2, true));
    g.fillEllipse (c.x - r2, c.y - r2, 2.0f * r2, 2.0f * r2);

    const float angle = (norm - 0.5f) * 270.0f * MathConstants<float>::pi / 180.0f; // 0 = arriba
    const Point<float> dir (std::sin (angle), -std::cos (angle));
    Path line;
    line.startNewSubPath (c + dir * 12.0f);
    line.lineTo (c + dir * 56.0f);
    g.setColour (Colour (0xfff2efe6));
    g.strokePath (line, PathStrokeType (9.0f, PathStrokeType::curved, PathStrokeType::rounded));
}

void SP950Editor::drawToggle (Graphics& g, Rectangle<float> r, bool on)
{
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRect (r.expanded (5.0f));

    if (on)
    {
        g.setColour (Colour (0x6639ff2a));
        g.fillRect (r.expanded (12.0f));
        g.setGradientFill (ColourGradient (Colour (0xff9dff7a), r.getCentreX(), r.getCentreY(),
                                           Colour (0xff2fae2a), r.getX(), r.getY(), true));
    }
    else
    {
        g.setGradientFill (ColourGradient (Colour (0xffe6e3da), r.getX(), r.getY(),
                                           Colour (0xffb5b2a8), r.getRight(), r.getBottom(), false));
    }
    g.fillRect (r);
    g.setColour (Colour (0xff1a1a1a));
    g.drawRect (r, 4.0f);
}

void SP950Editor::drawButtonBox (Graphics& g, Rectangle<float> r, bool lit)
{
    if (lit)
        g.setGradientFill (ColourGradient (Colour (0xffffa24a), r.getX(), r.getY(),
                                           Colour (0xffff7a1a), r.getX(), r.getBottom(), false));
    else
        g.setGradientFill (ColourGradient (Colour (0xffe4e1d8), r.getX(), r.getY(),
                                           Colour (0xffc4c1b7), r.getX(), r.getBottom(), false));
    g.fillRect (r);
    g.setColour (Colour (0xff111111));
    g.drawRect (r, 4.0f);
}

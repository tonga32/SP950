#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

namespace
{
    constexpr float kMaxTune = SP950AudioProcessor::kMaxTune;

    constexpr float kSliderTop = 602.0f, kSliderLen = 576.0f;
    const Rectangle<float> sliderArea (215.0f, 560.0f, 300.0f, 680.0f);

    // 0 = in, 1 = out, 2 = filter, 3 = dry/wet
    const Point<float> knobCentre[4] = { { 665.0f, 665.0f }, { 665.0f, 903.0f },
                                         { 665.0f, 1140.0f }, { 897.0f, 153.0f } };
    const char* const knobId[4]  = { "in", "out", "filter", "mix" };
    const float knobHit[4]       = { 90.0f, 90.0f, 90.0f, 48.0f };
    const float knobSens[4]      = { 300.0f, 300.0f, 300.0f, 200.0f };

    const Rectangle<float> fineBox   (224.0f, 1283.0f, 86.0f, 86.0f);
    const Rectangle<float> extBox    (428.0f, 1283.0f, 86.0f, 86.0f);
    const Rectangle<float> linkBox   (822.0f, 742.0f, 82.0f, 82.0f);
    const Rectangle<float> comboBox  (588.0f, 45.0f, 255.0f, 58.0f);
    const Rectangle<float> bypassBox (588.0f, 122.0f, 255.0f, 64.0f);
    const Rectangle<float> menuBox   (855.0f, 38.0f, 80.0f, 66.0f);

    float tuneToY (float t) { return kSliderTop + (t / kMaxTune) * kSliderLen; }
}

SP950Editor::SP950Editor (SP950AudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    panel = ImageCache::getFromMemory (BinaryData::panel_png, BinaryData::panel_pngSize);

    constrainer.setFixedAspectRatio (976.0 / 1612.0);
    constrainer.setSizeLimits (244, 403, 976, 1612);
    setConstrainer (&constrainer);

    const int w = jlimit (244, 976, (int) proc.apvts.state.getProperty ("editorWidth", 488));
    setSize (w, roundToInt ((float) w * 1612.0f / 976.0f));
    setResizable (true, true);

    startTimerHz (30);
}

SP950Editor::~SP950Editor()
{
    stopTimer();
}

void SP950Editor::resized()
{
    proc.apvts.state.setProperty ("editorWidth", getWidth(), nullptr);
}

void SP950Editor::setZoom (int percent)
{
    const int w = roundToInt (488.0f * (float) percent / 100.0f);
    setSize (w, roundToInt ((float) w * 1612.0f / 976.0f));
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
    // con "ext." apagado el slider llega solo hasta -15 (la marca de 78 RPM)
    const float limit = getBool ("ext") ? kMaxTune : SP950AudioProcessor::kNormalMaxTune;
    return jmin (proc.apvts.getRawParameterValue ("tune")->load(), limit);
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
    const float limit = getBool ("ext") ? kMaxTune : SP950AudioProcessor::kNormalMaxTune;
    t = jlimit (0.0f, limit, t);
    if (getBool ("fine"))
        t = std::round (t * 10.0f) / 10.0f;      // fine: pasos de décimas de semitono
    else
        t = jmin (limit, std::round (t));        // normal: semitonos enteros
    setActiveNorm (t / kMaxTune);
}

// ---------------------------------------------------------------- menús
void SP950Editor::showModeMenu()
{
    const int current = (int) std::lround (proc.apvts.getRawParameterValue ("mode")->load());
    const char* names[8] = { "Mono Sum", "Mono L", "Mono R", "Stereo", "Stereo L", "Stereo R",
                             "Stereo Mid", "Stereo Side" };

    PopupMenu m;
    for (int i = 0; i < 8; ++i)
    {
        if (i == 3) m.addSeparator();
        m.addItem (i + 1, names[i], true, current == i);
    }

    const float s = scale();
    const Rectangle<int> area ((int) (comboBox.getX() * s), (int) (comboBox.getY() * s),
                               (int) (comboBox.getWidth() * s), (int) (comboBox.getHeight() * s));

    Component::SafePointer<SP950Editor> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (area)),
                     [safe] (int result)
                     {
                         if (safe != nullptr && result > 0)
                             safe->setChoice (result - 1);
                     });
}

void SP950Editor::showMainMenu()
{
    const int zooms[7] = { 50, 67, 80, 100, 125, 150, 200 };
    const int currentPercent = roundToInt ((float) getWidth() / 488.0f * 100.0f);

    PopupMenu zoom;
    for (int i = 0; i < 7; ++i)
        zoom.addItem (101 + i, String (zooms[i]) + "%", true, std::abs (currentPercent - zooms[i]) <= 2);

    PopupMenu m;
    m.addSubMenu ("GUI Zoom", zoom);
    m.addSeparator();
    m.addItem (1, "Load From Clipboard");
    m.addItem (2, "Save To Clipboard");

    const float s = scale();
    const Rectangle<int> area ((int) (menuBox.getX() * s), (int) (menuBox.getY() * s),
                               (int) (menuBox.getWidth() * s), (int) (menuBox.getHeight() * s));

    Component::SafePointer<SP950Editor> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (area)),
                     [safe, zooms] (int result)
                     {
                         if (safe == nullptr || result <= 0) return;

                         if (result >= 101 && result <= 107)
                         {
                             safe->setZoom (zooms[result - 101]);
                         }
                         else if (result == 2)
                         {
                             if (auto xml = safe->proc.apvts.copyState().createXml())
                                 SystemClipboard::copyTextToClipboard (xml->toString());
                         }
                         else if (result == 1)
                         {
                             if (auto xml = parseXML (SystemClipboard::getTextFromClipboard()))
                                 if (xml->hasTagName (safe->proc.apvts.state.getType()))
                                     safe->proc.apvts.replaceState (ValueTree::fromXml (*xml));
                         }
                     });
}

// ---------------------------------------------------------------- ratón
void SP950Editor::mouseDown (const MouseEvent& e)
{
    const Point<float> p = e.position / scale();
    dragMode = Drag::none;
    activeKnob = -1;
    linkedParam = nullptr;

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

    for (int i = 0; i < 4; ++i)
    {
        if (p.getDistanceFrom (knobCentre[i]) < knobHit[i])
        {
            beginDrag (knobId[i]);
            dragMode = Drag::knob;
            activeKnob = i;
            dragStartY = p.y;
            dragStartNorm = getNorm (knobId[i]);

            // In y Out enlazados: se mueven al revés (si uno sube, el otro baja)
            if (i < 2 && getBool ("link") && activeParam != nullptr)
            {
                linkedParam = proc.apvts.getParameter (knobId[1 - i]);
                if (linkedParam != nullptr)
                {
                    linkedParam->beginChangeGesture();
                    thisStartDb = activeParam->convertFrom0to1 (activeParam->getValue());
                    linkStartDb = linkedParam->convertFrom0to1 (linkedParam->getValue());
                }
            }
            return;
        }
    }

    if (fineBox.contains (p))   { clickToggle ("fine");   return; }
    if (extBox.contains (p))    { clickToggle ("ext");    return; }
    if (linkBox.contains (p))   { clickToggle ("link");   return; }
    if (bypassBox.contains (p)) { clickToggle ("bypass"); return; }
    if (comboBox.contains (p))  { showModeMenu();         return; }
    if (menuBox.contains (p))   { showMainMenu();         return; }
}

void SP950Editor::mouseDrag (const MouseEvent& e)
{
    const Point<float> p = e.position / scale();

    if (dragMode == Drag::slider)
    {
        applySliderPos (p.y - dragOffset);
    }
    else if (dragMode == Drag::knob && activeKnob >= 0)
    {
        const float norm = jlimit (0.0f, 1.0f, dragStartNorm + (dragStartY - p.y) / knobSens[activeKnob]);
        setActiveNorm (norm);

        if (linkedParam != nullptr && activeParam != nullptr)
        {
            const float newDb = activeParam->convertFrom0to1 (norm);
            const float otherDb = jlimit (-24.0f, 24.0f, linkStartDb - (newDb - thisStartDb));
            linkedParam->setValueNotifyingHost (linkedParam->convertTo0to1 (otherDb));
        }
    }
}

void SP950Editor::mouseUp (const MouseEvent&)
{
    if (activeParam != nullptr)
    {
        activeParam->endChangeGesture();
        activeParam = nullptr;
    }
    if (linkedParam != nullptr)
    {
        linkedParam->endChangeGesture();
        linkedParam = nullptr;
    }
    dragMode = Drag::none;
    activeKnob = -1;
}

void SP950Editor::mouseDoubleClick (const MouseEvent& e)
{
    const Point<float> p = e.position / scale();

    if (sliderArea.contains (p)) { resetParam ("tune"); return; }

    for (int i = 0; i < 4; ++i)
        if (p.getDistanceFrom (knobCentre[i]) < knobHit[i])
            resetParam (knobId[i]);
}

// ---------------------------------------------------------------- dibujo
void SP950Editor::paint (Graphics& g)
{
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImage (panel, getLocalBounds().toFloat(), RectanglePlacement::stretchToFit);

    g.addTransform (AffineTransform::scale (scale()));

    drawLcd (g);
    drawSlider (g);
    for (int i = 0; i < 3; ++i)
        drawKnob (g, knobCentre[i], getNorm (knobId[i]));
    drawMixKnob (g, knobCentre[3], getNorm ("mix"));

    drawToggle (g, fineBox, getBool ("fine"));
    drawToggle (g, extBox,  getBool ("ext"));
    drawToggle (g, linkBox, getBool ("link"));

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

    // botón bypass (mientras se mueve la perilla dry/wet muestra el porcentaje)
    if (dragMode == Drag::knob && activeKnob == 3)
    {
        g.setColour (Colour (0xff101010));
        g.fillRect (bypassBox);
        g.setColour (Colours::white);
        g.setFont (Font (34.0f));
        const int wet = (int) std::lround (proc.apvts.getRawParameterValue ("mix")->load());
        g.drawText (String (wet) + "% wet", bypassBox, Justification::centred, false);
    }
    else
    {
        drawButtonBox (g, bypassBox, getBool ("bypass"));
        g.setColour (Colour (0xff111111));
        g.setFont (Font (34.0f));
        g.drawText ("Bypass", bypassBox, Justification::centred, false);
    }
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

    // Tune: se muestra en negativo (el tono baja al bajar el slider)
    const float tune = getTune();
    const bool fine = getBool ("fine");
    String tuneStr;
    if (fine)
        tuneStr = (tune > 0.049f ? "-" : "") + String (tune, 1);
    else
        tuneStr = (std::lround (tune) > 0 ? "-" : "") + String ((int) std::lround (tune));

    const float filterVal = proc.apvts.getRawParameterValue ("filter")->load();
    const String filterStr (String ((int) std::lround (filterVal)));

    const Colour ink (0xff2b0d03);
    g.setColour (ink);
    g.setFont (Font (Font::getDefaultMonospacedFontName(), 56.0f, Font::bold));

    g.drawText ("Tune:",   Rectangle<float> (208.0f, 300.0f, 200.0f, 70.0f), Justification::centredLeft, false);
    g.drawText (tuneStr,   Rectangle<float> (380.0f, 300.0f, 180.0f, 70.0f), Justification::centredRight, false);
    g.drawText ("Filter:", Rectangle<float> (208.0f, 380.0f, 240.0f, 70.0f), Justification::centredLeft, false);
    g.drawText (filterStr, Rectangle<float> (380.0f, 380.0f, 180.0f, 70.0f), Justification::centredRight, false);

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

void SP950Editor::drawMixKnob (Graphics& g, Point<float> c, float norm)
{
    const float R = 42.0f;

    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillEllipse (c.x - R - 3.0f, c.y - R + 3.0f, 2.0f * R + 6.0f, 2.0f * R + 6.0f);

    g.setGradientFill (ColourGradient (Colour (0xffe9e9e9), c.x - 18.0f, c.y - 22.0f,
                                       Colour (0xff6a6a6a), c.x + R, c.y + R, true));
    g.fillEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R);

    g.setColour (Colour (0xff2a2a2a));
    g.drawEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R, 3.0f);

    const float angle = (norm - 0.5f) * 270.0f * MathConstants<float>::pi / 180.0f;
    const Point<float> dir (std::sin (angle), -std::cos (angle));
    Path line;
    line.startNewSubPath (c + dir * 8.0f);
    line.lineTo (c + dir * 32.0f);
    g.setColour (Colour (0xff151515));
    g.strokePath (line, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded));
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

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// Todas las medidas internas están en píxeles de la imagen original (976 x 1612).
// Tamaño normal (100 %): 488 x 806. La ventana se puede agrandar y achicar.
class SP950Editor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit SP950Editor (SP950AudioProcessor&);
    ~SP950Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Drag { none, slider, knob };

    void timerCallback() override { repaint(); }
    float scale() const { return (float) getWidth() / 976.0f; }

    void drawLcd (juce::Graphics&);
    void drawSlider (juce::Graphics&);
    void drawKnob (juce::Graphics&, juce::Point<float> centre, float norm);
    void drawMixKnob (juce::Graphics&, juce::Point<float> centre, float norm);
    void drawToggle (juce::Graphics&, juce::Rectangle<float> r, bool on);
    void drawButtonBox (juce::Graphics&, juce::Rectangle<float> r, bool lit);

    float getNorm (const char* id) const;
    bool  getBool (const char* id) const;
    float getTune() const;
    void  beginDrag (const char* id);
    void  setActiveNorm (float norm);
    void  clickToggle (const char* id);
    void  setChoice (int index);
    void  setZoom (int percent);
    void  applySliderPos (float yPixels);
    void  resetParam (const char* id);
    void  showModeMenu();
    void  showMainMenu();

    SP950AudioProcessor& proc;
    juce::Image panel;
    juce::ComponentBoundsConstrainer constrainer;

    juce::RangedAudioParameter* activeParam = nullptr;
    juce::RangedAudioParameter* linkedParam = nullptr;
    Drag  dragMode = Drag::none;
    int   activeKnob = -1;
    float dragStartY = 0.0f, dragStartNorm = 0.0f, dragOffset = 0.0f;
    float linkStartDb = 0.0f, thisStartDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SP950Editor)
};

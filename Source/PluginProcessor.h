#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// Filtro pasa-bajos de 2 polos
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void setLowpass (double sampleRate, double freq, double q)
    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (float) (((1.0 - cw) * 0.5) / a0);
        b1 = (float) ((1.0 - cw) / a0);
        b2 = b0;
        a1 = (float) ((-2.0 * cw) / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() { z1 = z2 = 0; }
};

// ---------------------------------------------------------------
// Convertidor estilo SP-1200: toma muestras de 12 bits a una frecuencia
// fija de 26,04 kHz. Como la fuente se "acelera" (RPM) y luego se baja
// con el detune, el resultado es: mismo tono y duración, pero con
// menos ancho de banda y aliasing que cambian con el detune.
// ---------------------------------------------------------------
struct SPConverter
{
    double phase = 0.0;
    float held = 0.0f;
    Biquad inLp, outLp;

    void reset()
    {
        phase = 0.0;
        held = 0.0f;
        inLp.reset();
        outLp.reset();
    }

    void setFilters (double hostRate, double effRate)
    {
        const double fc = juce::jmin (0.45 * hostRate, 0.5 * effRate);
        inLp.setLowpass (hostRate, fc, 0.7071);
        outLp.setLowpass (hostRate, fc, 0.7071);
    }

    float process (float x, double step)
    {
        const float xin = inLp.process (x);

        phase += step;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            if (phase >= 1.0) phase = 0.0;

            // 12 bits
            const float c = juce::jlimit (-1.0f, 0.99951f, xin);
            held = std::floor (c * 2048.0f + 0.5f) / 2048.0f;
        }
        return outLp.process (held);
    }
};

class SP950AudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr float kMaxTune = 27.863137f;   // x5 (con "ext." activado)
    static constexpr float kNormalMaxTune = 15.0f;  // con "ext." apagado

    SP950AudioProcessor();
    ~SP950AudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SP950"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    double sr = 44100.0;
    SPConverter conv[2];
    Biquad filt[2][2];          // filtro tipo S950: 4 polos (2 etapas) por canal
    float cutoffCurrent = 20000.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inGain, outGain, dwMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> tuneSmooth;

    std::atomic<float>* pTune = nullptr;
    std::atomic<float>* pFilter = nullptr;
    std::atomic<float>* pIn = nullptr;
    std::atomic<float>* pOut = nullptr;
    std::atomic<float>* pExt = nullptr;
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pMix = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SP950AudioProcessor)
};

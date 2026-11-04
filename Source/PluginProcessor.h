#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>

// ---------------------------------------------------------------
// Cambia el tono moviendo dos "lecturas" por una memoria corta
// y mezclándolas suavemente (método clásico de pitch shifter).
// ---------------------------------------------------------------
class PitchShifter
{
public:
    void prepare (double sampleRate)
    {
        windowSize = juce::jmax (64, (int) (0.05 * sampleRate)); // 50 ms
        bufSize    = windowSize + 16;
        buffer.assign ((size_t) bufSize, 0.0f);
        writePos = 0;
        phase = 0.0;
        lastDry = 0.0f;
    }

    int getLatency() const { return windowSize / 2 + 2; }

    // Devuelve la señal con el tono cambiado. lastDry queda con la señal
    // original retrasada lo mismo (para que todo quede alineado).
    float tick (float in, double ratio)
    {
        buffer[(size_t) writePos] = in;
        writePos = (writePos + 1) % bufSize;

        phase += (1.0 - ratio) / (double) windowSize;
        phase -= std::floor (phase);

        double p1 = phase + 0.5;
        p1 -= std::floor (p1);

        const double d0 = 2.0 + phase * (double) windowSize;
        const double d1 = 2.0 + p1    * (double) windowSize;

        const double s = std::sin (juce::MathConstants<double>::pi * phase);
        const float w0 = (float) (s * s);
        const float w1 = 1.0f - w0;

        lastDry = readAt (2.0 + 0.5 * (double) windowSize);
        return w0 * readAt (d0) + w1 * readAt (d1);
    }

    float lastDry = 0.0f;

private:
    float readAt (double delay) const
    {
        double pos = (double) writePos - delay;
        while (pos < 0.0) pos += (double) bufSize;
        const int i0 = (int) pos;
        const float fr = (float) (pos - (double) i0);
        const int i1 = (i0 + 1) % bufSize;
        return buffer[(size_t) i0] * (1.0f - fr) + buffer[(size_t) i1] * fr;
    }

    std::vector<float> buffer;
    int bufSize = 1024, windowSize = 512, writePos = 0;
    double phase = 0.0;
};

// Filtro pasa-bajos simple (12 dB por octava)
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

class SP950AudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr float kMaxTune = 27.863137f; // semitonos hacia abajo (x5)

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
    PitchShifter shifter[2];
    PitchShifter dryDelay[2];
    Biquad filt[2];
    float cutoffCurrent = 20000.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inGain, outGain, mixAmt, dwMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> ratioSmooth;

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

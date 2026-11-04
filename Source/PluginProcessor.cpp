#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout SP950AudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    // "tune" = semitonos HACIA ABAJO (0 .. 27,86). En pantalla se muestra con signo menos.
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tune", 1 }, "Tune",
                 NormalisableRange<float> (0.0f, kMaxTune, 0.001f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "filter", 1 }, "Filter",
                 NormalisableRange<float> (0.0f, 99.0f, 0.01f), 99.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "in", 1 }, "In",
                 NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Out",
                 NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Dry/Wet",
                 NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));
    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { "fine", 1 }, "Fine", false));
    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { "ext", 1 }, "Ext", true));
    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { "link", 1 }, "Link In/Out", false));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                 StringArray { "Mono Sum", "Mono L", "Mono R", "Stereo", "Stereo L", "Stereo R",
                               "Stereo Mid", "Stereo Side" }, 3));
    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { "bypass", 1 }, "Bypass", false));

    return { p.begin(), p.end() };
}

SP950AudioProcessor::SP950AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SP950", createLayout())
{
    pTune   = apvts.getRawParameterValue ("tune");
    pFilter = apvts.getRawParameterValue ("filter");
    pIn     = apvts.getRawParameterValue ("in");
    pOut    = apvts.getRawParameterValue ("out");
    pExt    = apvts.getRawParameterValue ("ext");
    pMode   = apvts.getRawParameterValue ("mode");
    pBypass = apvts.getRawParameterValue ("bypass");
    pMix    = apvts.getRawParameterValue ("mix");
}

bool SP950AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void SP950AudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& s : shifter) s.prepare (sr);
    for (auto& s : dryDelay) s.prepare (sr);
    for (auto& f : filt) f.reset();

    setLatencySamples (shifter[0].getLatency());

    inGain.reset (sr, 0.02);   inGain.setCurrentAndTargetValue (Decibels::decibelsToGain ((float) *pIn));
    outGain.reset (sr, 0.02);  outGain.setCurrentAndTargetValue (Decibels::decibelsToGain ((float) *pOut));
    mixAmt.reset (sr, 0.03);   mixAmt.setCurrentAndTargetValue (0.0f);
    dwMix.reset (sr, 0.02);    dwMix.setCurrentAndTargetValue (pMix->load() / 100.0f);
    ratioSmooth.reset (sr, 0.03);
    ratioSmooth.setCurrentAndTargetValue (std::pow (2.0f, -(float) *pTune / 12.0f));
    cutoffCurrent = 20000.0f;
}

void SP950AudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const int numCh = jmin (2, buffer.getNumChannels());
    const int n = buffer.getNumSamples();
    if (numCh < 1) return;

    const bool  bypass = pBypass->load() > 0.5f;
    const bool  ext    = pExt->load() > 0.5f;
    const int   mode   = jlimit (0, 7, (int) std::lround (pMode->load()));
    const float tune   = pTune->load();   // semitonos hacia abajo

    // por debajo de ~0,25 semitonos se mezcla con la señal original
    mixAmt.setTargetValue (ext ? jlimit (0.0f, 1.0f, tune / 0.25f) : 0.0f);
    ratioSmooth.setTargetValue (std::pow (2.0f, -tune / 12.0f));

    inGain.setTargetValue  (Decibels::decibelsToGain (pIn->load()));
    outGain.setTargetValue (Decibels::decibelsToGain (pOut->load()));
    dwMix.setTargetValue   (jlimit (0.0f, 1.0f, pMix->load() / 100.0f));

    // filtro: 0..99 -> 20 Hz .. 20 kHz
    const float fv = pFilter->load();
    float cutoffTarget = 20.0f * std::pow (1000.0f, fv / 99.0f);
    cutoffTarget = jmin (cutoffTarget, (float) (sr * 0.45));
    cutoffCurrent += (cutoffTarget - cutoffCurrent) * 0.35f;
    for (auto& f : filt) f.setLowpass (sr, (double) cutoffCurrent, 0.7071);

    auto* c0 = buffer.getWritePointer (0);
    auto* c1 = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        const float inG    = inGain.getNextValue();
        const float outG   = outGain.getNextValue();
        const float pMixV  = mixAmt.getNextValue();
        const float dw     = dwMix.getNextValue();
        const double ratio = (double) ratioSmooth.getNextValue();

        const float l = c0[i];
        const float r = c1 != nullptr ? c1[i] : l;

        // señal original, retrasada lo mismo que la procesada (para que no se desfasen)
        dryDelay[0].tick (l, 1.0);
        dryDelay[1].tick (r, 1.0);
        const float dryL = dryDelay[0].lastDry;
        const float dryR = dryDelay[1].lastDry;

        if (bypass)
        {
            c0[i] = dryL;
            if (c1 != nullptr) c1[i] = dryR;
            continue;
        }

        const float gl = l * inG, gr = r * inG;
        float a = 0.0f, b = 0.0f;
        switch (mode)
        {
            case 0:  a = (gl + gr) * 0.5f; break;                               // Mono Sum
            case 1:  a = gl; break;                                             // Mono L
            case 2:  a = gr; break;                                             // Mono R
            case 3: case 4: case 5:  a = gl; b = gr; break;                     // Stereo, L, R
            default: a = (gl + gr) * 0.5f; b = (gl - gr) * 0.5f; break;         // Mid / Side
        }

        const float wetA = shifter[0].tick (a, ratio);
        const float wetB = shifter[1].tick (b, ratio);
        const float dryA = shifter[0].lastDry;
        const float dryB = shifter[1].lastDry;

        const float procA = dryA + pMixV * (wetA - dryA);
        const float procB = dryB + pMixV * (wetB - dryB);

        float wl, wr;
        switch (mode)
        {
            case 0: case 1: case 2:  wl = wr = procA; break;
            case 3:  wl = procA; wr = procB; break;
            case 4:  wl = procA; wr = dryB;  break;
            case 5:  wl = dryA;  wr = procB; break;
            case 6:  wl = procA + dryB; wr = procA - dryB; break;   // solo el centro
            default: wl = dryA + procB; wr = dryA - procB; break;   // solo los costados
        }

        wl = filt[0].process (wl);
        wr = filt[1].process (wr);

        const float oL = dryL + dw * (wl - dryL);
        const float oR = dryR + dw * (wr - dryR);

        c0[i] = oL * outG;
        if (c1 != nullptr) c1[i] = oR * outG;
    }
}

AudioProcessorEditor* SP950AudioProcessor::createEditor()
{
    return new SP950Editor (*this);
}

void SP950AudioProcessor::getStateInformation (MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SP950AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SP950AudioProcessor();
}

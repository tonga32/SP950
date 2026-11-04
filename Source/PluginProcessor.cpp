#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout SP950AudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tune", 1 }, "Tune",
                 NormalisableRange<float> (0.0f, kMaxTune, 0.001f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "filter", 1 }, "Filter",
                 NormalisableRange<float> (0.0f, 99.0f, 0.01f), 99.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "in", 1 }, "In",
                 NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Out",
                 NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { "fine", 1 }, "Fine", false));
    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { "ext", 1 }, "Ext", true));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                 StringArray { "Stereo", "Stereo Mid", "Stereo Side" }, 0));
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
    for (auto& f : filt) f.reset();

    setLatencySamples (shifter[0].getLatency());

    inGain.reset (sr, 0.02);   inGain.setCurrentAndTargetValue (Decibels::decibelsToGain ((float) *pIn));
    outGain.reset (sr, 0.02);  outGain.setCurrentAndTargetValue (Decibels::decibelsToGain ((float) *pOut));
    mixAmt.reset (sr, 0.03);   mixAmt.setCurrentAndTargetValue (0.0f);
    ratioSmooth.reset (sr, 0.03);
    ratioSmooth.setCurrentAndTargetValue (std::pow (2.0f, (float) *pTune / 12.0f));
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
    const int   mode   = (int) std::lround (pMode->load());
    const float tune   = pTune->load();

    // pitch: por debajo de ~0,25 semitonos se mezcla con la señal original
    const float pitchAmount = ext ? jlimit (0.0f, 1.0f, tune / 0.25f) : 0.0f;
    mixAmt.setTargetValue (pitchAmount);
    ratioSmooth.setTargetValue (std::pow (2.0f, tune / 12.0f));

    inGain.setTargetValue  (Decibels::decibelsToGain (pIn->load()));
    outGain.setTargetValue (Decibels::decibelsToGain (pOut->load()));

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
        const float inG  = inGain.getNextValue();
        const float outG = outGain.getNextValue();
        const float mix  = mixAmt.getNextValue();
        const double ratio = (double) ratioSmooth.getNextValue();

        float l = c0[i];
        float r = c1 != nullptr ? c1[i] : l;

        if (! bypass) { l *= inG; r *= inG; }

        float a, b;
        if (mode == 0) { a = l; b = r; }
        else           { a = (l + r) * 0.5f; b = (l - r) * 0.5f; }

        const float wetA = shifter[0].tick (a, ratio);
        const float wetB = shifter[1].tick (b, ratio);
        const float dryA = shifter[0].lastDry;
        const float dryB = shifter[1].lastDry;

        const float procA = dryA + mix * (wetA - dryA);
        const float procB = dryB + mix * (wetB - dryB);

        float outA, outB;
        if (mode == 0)      { outA = procA; outB = procB; }
        else if (mode == 1) { outA = procA; outB = dryB; }   // solo el centro (Mid)
        else                { outA = dryA;  outB = procB; }  // solo los lados (Side)

        float oL, oR;
        if (mode == 0) { oL = outA; oR = outB; }
        else           { oL = outA + outB; oR = outA - outB; }

        if (bypass)
        {
            // el bypass también va retrasado lo mismo, así no se desfasa
            if (mode == 0) { oL = dryA; oR = dryB; }
            else           { oL = dryA + dryB; oR = dryA - dryB; }
            // (en bypass no se aplica filtro ni volumen)
            c0[i] = oL;
            if (c1 != nullptr) c1[i] = oR;
            continue;
        }

        oL = filt[0].process (oL) * outG;
        oR = filt[1].process (oR) * outG;

        c0[i] = oL;
        if (c1 != nullptr) c1[i] = oR;
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

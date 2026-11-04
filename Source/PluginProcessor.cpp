#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout MixDoctorProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };
    auto lab = [] (const char* s) { return AudioParameterFloatAttributes().withLabel (s); };

    l.add (std::make_unique<AudioParameterBool> (id ("cleanLow"), "Clean low-end", true));
    l.add (std::make_unique<AudioParameterBool> (id ("monoLow"),  "Mono low-end",  true));
    l.add (std::make_unique<AudioParameterBool> (id ("autoTone"), "Auto tone",     true));
    l.add (std::make_unique<AudioParameterBool> (id ("harsh"),    "Harshness tamer", true));
    l.add (std::make_unique<AudioParameterBool> (id ("limiter"),  "Limiter",       true));

    l.add (std::make_unique<AudioParameterFloat> (id ("monoFreq"), "Mono below",  NormalisableRange<float> (60.f, 200.f, 1.f), 120.f, lab ("Hz")));
    l.add (std::make_unique<AudioParameterFloat> (id ("monoAmt"),  "Mono amount", NormalisableRange<float> (0.f, 100.f, 1.f), 100.f, lab ("%")));
    l.add (std::make_unique<AudioParameterFloat> (id ("toneAmt"),  "Tone amount", NormalisableRange<float> (0.f, 100.f, 1.f), 50.f,  lab ("%")));
    l.add (std::make_unique<AudioParameterFloat> (id ("toneMax"),  "Tone limit",  NormalisableRange<float> (0.5f, 6.f, 0.1f), 3.f,  lab ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (id ("harshMax"), "Harsh limit", NormalisableRange<float> (0.f, 8.f, 0.1f), 4.f,   lab ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (id ("ceiling"),  "Ceiling",     NormalisableRange<float> (-3.f, 0.f, 0.1f), -0.3f, lab ("dBTP")));
    l.add (std::make_unique<AudioParameterChoice> (id ("style"),   "Style", StringArray { "Neurofunk", "Liquid", "Dancefloor", "Captured" }, 0));
    return l;
}

MixDoctorProcessor::MixDoctorProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pClean = apvts.getRawParameterValue ("cleanLow");   pMono = apvts.getRawParameterValue ("monoLow");
    pTone  = apvts.getRawParameterValue ("autoTone");   pHarsh = apvts.getRawParameterValue ("harsh");
    pLimiter = apvts.getRawParameterValue ("limiter");  pMonoFreq = apvts.getRawParameterValue ("monoFreq");
    pMonoAmt = apvts.getRawParameterValue ("monoAmt");  pToneAmt = apvts.getRawParameterValue ("toneAmt");
    pToneMax = apvts.getRawParameterValue ("toneMax");  pHarshMax = apvts.getRawParameterValue ("harshMax");
    pCeiling = apvts.getRawParameterValue ("ceiling");  pStyle = apvts.getRawParameterValue ("style");
    for (auto& c : captured) c.store (0.f);
}

bool MixDoctorProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == AudioChannelSet::stereo() && l.getMainOutputChannelSet() == AudioChannelSet::stereo();
}

void MixDoctorProcessor::prepareToPlay (double sampleRate, int)
{
    doctor.prepare (sampleRate);
    setLatencySamples (doctor.latencySamples());
}

void MixDoctorProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2) return;

    mixdoctor::Params p;
    p.cleanLow = pClean->load() > 0.5f;   p.monoLow = pMono->load() > 0.5f;   p.autoTone = pTone->load() > 0.5f;
    p.harsh = pHarsh->load() > 0.5f;      p.limiter = pLimiter->load() > 0.5f;
    p.monoFreq = pMonoFreq->load();       p.monoAmount = pMonoAmt->load() * 0.01f;
    p.toneAmount = pToneAmt->load() * 0.01f; p.toneMaxDb = pToneMax->load();
    p.harshMaxDb = pHarshMax->load();     p.ceilingDb = pCeiling->load();

    const int style = (int) std::lround (pStyle->load());
    if (style >= 3 && hasCaptured.load()) { for (int i = 0; i < mixdoctor::kNumBands; ++i) p.target[(size_t) i] = captured[i].load(); }
    else                                  { mixdoctor::getStyleTarget (style >= 3 ? 0 : style, p.target.data()); }

    doctor.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples(), p);
}

void MixDoctorProcessor::captureTarget()
{
    if (doctor.meters.ready.load() < 0.5f) return;                      // need ~1 s of analysed audio first
    String csv;
    for (int i = 0; i < mixdoctor::kNumBands; ++i)
    {
        const float v = doctor.meters.bandRel[i].load();
        captured[i].store (v);
        csv << (i ? "," : "") << String (v, 3);
    }
    hasCaptured.store (true);
    apvts.state.setProperty ("captured", csv, nullptr);
    if (auto* style = apvts.getParameter ("style")) style->setValueNotifyingHost (style->convertTo0to1 (3.f));
}

void MixDoctorProcessor::loadCapturedFromState()
{
    const auto parts = StringArray::fromTokens (apvts.state.getProperty ("captured").toString(), ",", "");
    if (parts.size() != mixdoctor::kNumBands) return;
    for (int i = 0; i < mixdoctor::kNumBands; ++i) captured[i].store (parts[i].getFloatValue());
    hasCaptured.store (true);
}

void MixDoctorProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void MixDoctorProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            loadCapturedFromState();
        }
}

AudioProcessorEditor* MixDoctorProcessor::createEditor() { return new MixDoctorEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MixDoctorProcessor(); }

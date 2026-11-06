#include "PluginProcessor.h"
#include "PluginEditor.h"

DnBMixDoctorProcessor::DnBMixDoctorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", params::createLayout())
{
    auto bind = [this] (P& p, const juce::String& id) { p.v = apvts.getRawParameterValue (id); jassert (p.v != nullptr); };
    bind (pInput, params::input);       bind (pSubMono, params::subMono);   bind (pSubTight, params::subTight);
    bind (pKickBass, params::kickBass); bind (pPunch, params::punch);       bind (pSnap, params::snap);
    bind (pMud, params::mud);           bind (pReese, params::reese);       bind (pHarsh, params::harsh);
    bind (pAir, params::air);           bind (pGlue, params::glue);         bind (pAttack, params::attack);
    bind (pRelease, params::release);   bind (pWidth, params::width);       bind (pSaturate, params::saturate);
    bind (pClipper, params::clipper);
    bind (pAutoGainMix, params::autoGainMix);
    bind (pLoudness, params::loudness); bind (pCeiling, params::ceiling);   bind (pFix, params::fix);
    bind (pMix, params::mix);           bind (pOutput, params::output);     bind (pReleaseAuto, params::releaseAuto);
    bind (pBypass, params::bypass);     bind (pOversample, params::oversample);

    presets.setCurrentName ("Default");
}

bool DnBMixDoctorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void DnBMixDoctorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate);
    maxChunk = juce::jlimit (64, 4096, samplesPerBlock);
    const int ch = juce::jmax (1, getTotalNumOutputChannels());

    engine.prepare (sampleRate, maxChunk, juce::jmin (2, ch));
    setLatencySamples (engine.getLatency());

    outLoud.prepare (sampleRate);
    outTp.reset();
    columns.prepare (sampleRate);
    autoFix.prepare (sampleRate);
    load.reset (sampleRate, samplesPerBlock);
    tpHold = 0;
}

mcdsp::EngineParams DnBMixDoctorProcessor::readParams() const noexcept
{
    mcdsp::EngineParams p;
    p.inputDb = pInput.get();            p.subMonoHz = pSubMono.get();
    p.subTight = pSubTight.get() * 0.01f; p.kickBass = pKickBass.get() * 0.01f;
    p.punch = pPunch.get() * 0.01f;      p.snap = pSnap.get() * 0.01f;
    p.mud = pMud.get() * 0.01f;          p.reese = pReese.get() * 0.01f;
    p.harsh = pHarsh.get() * 0.01f;      p.air = pAir.get() * 0.01f;
    p.glue = pGlue.get() * 0.01f;        p.attackMs = pAttack.get();
    p.releaseMs = pRelease.get();        p.releaseAuto = pReleaseAuto.get() > 0.5f;
    p.width = pWidth.get() * 0.01f;      p.saturate = pSaturate.get() * 0.01f;
    p.clipper = pClipper.get() * 0.01f;
    p.autoGainMix = pAutoGainMix.get() * 0.01f;
    p.loudnessLufs = pLoudness.get();    p.ceilingDb = pCeiling.get();
    p.fix = pFix.get() * 0.01f;          p.mix = pMix.get() * 0.01f;
    p.outputDb = pOutput.get();
    p.bypass = pBypass.get() > 0.5f;     p.oversample = pOversample.get() > 0.5f;
    return p;
}

void DnBMixDoctorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (load, buffer.getNumSamples());

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // ---- transport: tempo for AUTO-FIX, restart integrated loudness when playback starts
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) hostBpm.store (*bpm);
            const bool playing = pos->getIsPlaying();
            if (playing && ! wasPlaying) { engine.resetLoudness(); outLoud.resetIntegrated(); tpHold = 0; }
            wasPlaying = playing;
        }
    if (meters.resetRequested.exchange (false)) { outLoud.resetIntegrated(); engine.resetLoudness(); engine.relearnLoudness(); tpHold = 0; }

    autoFix.process (buffer.getArrayOfReadPointers(), juce::jmin (2, buffer.getNumChannels()), buffer.getNumSamples());

    runEngine (buffer, false);
    meters.cpuPercent.store ((float) load.getLoadAsPercentage());
}

void DnBMixDoctorProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    runEngine (buffer, true);   // keeps the latency-aligned dry path running so bypass stays in sync
}

void DnBMixDoctorProcessor::runEngine (juce::AudioBuffer<float>& buffer, bool forceBypass)
{
    auto p = readParams();
    if (forceBypass) p.bypass = true;

    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int total = buffer.getNumSamples();
    const bool visuals = editorOpen.load (std::memory_order_relaxed);

    for (int start = 0; start < total; start += maxChunk)
    {
        const int n = juce::jmin (maxChunk, total - start);
        float* io[2] = { buffer.getWritePointer (0, start), numCh > 1 ? buffer.getWritePointer (1, start) : nullptr };

        engine.process (io, numCh, n, p);

        // ---- output metering (post everything)
        outLoud.process (io, numCh, n);
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch) tpHold = std::max (tpHold, outTp.process (ch, io[ch][i]));
            outTp.advance();
        }

        // ---- visual data (only while the editor is open)
        if (visuals)
        {
            const auto* dry = engine.getAlignedDry();
            const float gr = engine.glueGrDb + engine.limiterGrDb + engine.clipDb + engine.dynamicEqDb * 0.5f;
            columns.process (dry, io, numCh, n, gr, columnFifo);

            constexpr int tmpSize = 256;
            float monoIn[tmpSize], monoOut[tmpSize];
            mcdsp::StereoFrame frames[tmpSize];
            for (int off = 0; off < n; off += tmpSize)
            {
                const int m = juce::jmin (tmpSize, n - off);
                for (int i = 0; i < m; ++i)
                {
                    const float l = io[0][off + i], r = numCh > 1 ? io[1][off + i] : l;
                    const float dl = dry[0][off + i], dr = numCh > 1 ? dry[1][off + i] : dl;
                    monoIn[i] = 0.5f * (dl + dr);
                    monoOut[i] = 0.5f * (l + r);
                    frames[i] = { l, r };
                }
                spectrumInFifo.push (monoIn, m);
                spectrumOutFifo.push (monoOut, m);
                stereoFifo.push (frames, m);
            }
        }
    }

    meters.lufsIntegrated.store (outLoud.getIntegrated());
    meters.lufsShortTerm.store (outLoud.getShortTerm());
    meters.lufsMomentary.store (outLoud.getMomentary());
    meters.truePeakDb.store (mcdsp::gainToDb (tpHold));
    meters.glueGrDb.store (engine.glueGrDb);
    meters.limiterGrDb.store (engine.limiterGrDb);
    meters.clipDb.store (engine.clipDb);
    meters.autoGainDb.store (engine.autoGainDb);
}

//==============================================================================
int DnBMixDoctorProcessor::getNumPrograms()                   { return juce::jmax (1, (int) presets.getAll().size()); }
int DnBMixDoctorProcessor::getCurrentProgram()                { return juce::jmax (0, presets.indexOf (presets.getCurrentName())); }
void DnBMixDoctorProcessor::setCurrentProgram (int index)     { presets.load (index); }
const juce::String DnBMixDoctorProcessor::getProgramName (int index)
{
    const auto& all = presets.getAll();
    return juce::isPositiveAndBelow (index, (int) all.size()) ? all[(size_t) index].name : juce::String();
}

void DnBMixDoctorProcessor::toggleAB()
{
    auto current = apvts.copyState();
    if (! abOther.isValid())
        abOther = current.createCopy();

    // A/B swaps the sound only; the power switch and oversampling mode stay where they are
    auto keep = [&current] (juce::ValueTree& target, const juce::String& id)
    {
        auto src = current.getChildWithProperty ("id", id);
        auto dst = target.getChildWithProperty ("id", id);
        if (src.isValid() && dst.isValid()) dst.setProperty ("value", src.getProperty ("value"), nullptr);
    };
    auto next = abOther.createCopy();
    keep (next, params::bypass);
    keep (next, params::oversample);

    abOther = current;
    apvts.replaceState (next);
    abOnB = ! abOnB;
}

void DnBMixDoctorProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("editorW", editorWidth, nullptr);
    state.setProperty ("editorH", editorHeight, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void DnBMixDoctorProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            editorWidth = tree.getProperty ("editorW", 1300);
            editorHeight = tree.getProperty ("editorH", 700);
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* DnBMixDoctorProcessor::createEditor() { return new DnBMixDoctorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DnBMixDoctorProcessor(); }

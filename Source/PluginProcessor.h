#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "Params.h"
#include "PresetManager.h"
#include "dsp/MixEngine.h"
#include "analysis/VisualFifo.h"
#include "analysis/AutoFix.h"

class DnBMixDoctorProcessor : public juce::AudioProcessor
{
public:
    DnBMixDoctorProcessor();
    ~DnBMixDoctorProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (params::bypass); }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { apvts };

    // ---- lock-free audio -> UI data
    mcdsp::SpscFifo<mcdsp::VisColumn, 8192> columnFifo;
    mcdsp::SpscFifo<float, 32768> spectrumInFifo, spectrumOutFifo;
    mcdsp::SpscFifo<mcdsp::StereoFrame, 16384> stereoFifo;
    mcdsp::AutoFixAnalyser autoFix;

    struct Meters
    {
        std::atomic<float> lufsIntegrated { -100 }, lufsShortTerm { -100 }, lufsMomentary { -100 };
        std::atomic<float> truePeakDb { -100 }, glueGrDb { 0 }, limiterGrDb { 0 }, autoGainDb { 0 }, clipDb { 0 }, cpuPercent { 0 };
        std::atomic<bool> resetRequested { false };
    } meters;

    std::atomic<bool> editorOpen { false };
    std::atomic<double> hostBpm { 174.0 };
    std::atomic<double> currentSampleRate { 48000.0 };

    // ---- A/B compare (message thread)
    void toggleAB();
    bool isOnB() const noexcept { return abOnB; }

    int editorWidth = 1300, editorHeight = 700;
    bool advancedOpen = false;   // ADVANCED (EQ) panel showing (kept while the editor is closed and reopened)

private:
    void runEngine (juce::AudioBuffer<float>&, bool forceBypass);
    mcdsp::EngineParams readParams() const noexcept;

    mcdsp::MixEngine engine;
    juce::AudioBuffer<float> chunkScratch;
    int maxChunk = 512;

    mcdsp::LoudnessMeter outLoud;
    mcdsp::TruePeakDetector outTp;
    mcdsp::ColumnBuilder columns;
    juce::AudioProcessLoadMeasurer load;
    float tpHold = 0;
    bool wasPlaying = false;

    struct P
    {
        std::atomic<float>* v = nullptr;
        float get() const noexcept { return v->load (std::memory_order_relaxed); }
    };
    P pInput, pSubMono, pSubTight, pKickBass, pPunch, pSnap, pMud, pReese, pHarsh, pAir, pGlue, pAttack, pRelease,
      pWidth, pSaturate, pClipper, pAutoGainMix, pLoudness, pCeiling, pFix, pMix, pOutput, pReleaseAuto, pBypass, pOversample,
      pEqMix, pEqOn;
    std::array<P, params::numEqBands> pEq;

    juce::ValueTree abOther;
    bool abOnB = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DnBMixDoctorProcessor)
};

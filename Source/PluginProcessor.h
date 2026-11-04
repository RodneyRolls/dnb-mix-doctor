#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "MixDoctorDSP.h"

class MixDoctorProcessor : public juce::AudioProcessor
{
public:
    MixDoctorProcessor();
    ~MixDoctorProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DnB Mix Doctor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** UI thread: store the currently measured tonal balance as the "Captured" target. */
    void captureTarget();

    juce::AudioProcessorValueTreeState apvts;
    mixdoctor::MixDoctor doctor;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void loadCapturedFromState();

    std::atomic<float>* pClean {}, *pMono {}, *pTone {}, *pHarsh {}, *pLimiter {}, *pMonoFreq {}, *pMonoAmt {},
                       *pToneAmt {}, *pToneMax {}, *pHarshMax {}, *pCeiling {}, *pStyle {};
    std::atomic<float> captured[mixdoctor::kNumBands];
    std::atomic<bool>  hasCaptured { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixDoctorProcessor)
};

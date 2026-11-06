#pragma once
#include <juce_dsp/juce_dsp.h>
#include "DspUtils.h"
#include "TruePeakLimiter.h"
#include "LoudnessMeter.h"

namespace mcdsp
{
    /** Plain parameter snapshot handed to the engine once per block. Amounts are 0..1. */
    struct EngineParams
    {
        float inputDb = 0, subMonoHz = 100, subTight = 0.2f, kickBass = 0.2f, punch = 0.2f, snap = 0.15f,
              mud = 0.25f, reese = 0.2f, harsh = 0.25f, air = 0.2f, glue = 0.25f, attackMs = 10, releaseMs = 150,
              width = 1.0f, saturate = 0.15f, loudnessLufs = -7, ceilingDb = -1, fix = 1.0f, mix = 1.0f, outputDb = 0;
        bool releaseAuto = true, bypass = false, oversample = true;
    };

    /** The complete DnB Mix Doctor signal chain:

        input gain -> [120 Hz LR4 split: SUB TIGHT + KICK/BASS on the low band] -> [SUB MONO LR4 split:
        mono lows, WIDTH on everything above] -> PUNCH -> SNAP -> MUD / REESE TAME / HARSH dynamic EQ ->
        AIR -> GLUE -> (optional 4x oversampling: SATURATE -> loudness auto-gain -> true-peak limiter)
        -> latency-aligned MIX -> OUTPUT -> click-free bypass.

        Every Linkwitz-Riley split is recombined by summing (all-pass, phase coherent). Latency is constant
        regardless of the oversampling switch so host delay compensation never changes. */
    class MixEngine
    {
    public:
        void prepare (double sampleRate, int maxBlockSize, int numChannels);
        void reset();

        /** Total plugin latency in samples at the host rate (constant for a given sample rate). */
        int getLatency() const noexcept { return totalLatency; }

        /** Processes up to maxBlockSize samples in place. */
        void process (float* const* io, int numCh, int n, const EngineParams& p) noexcept;

        /** The latency-aligned dry input of the last processed block (for metering / visuals). */
        const float* const* getAlignedDry() const noexcept { return dryPtrs; }

        void resetLoudness() noexcept { preLoud.resetIntegrated(); postLoud.resetIntegrated(); loudCorrection = 0; }

        // meter values from the last block (audio thread writes, processor publishes)
        float glueGrDb = 0, limiterGrDb = 0, autoGainDb = 0, dynamicEqDb = 0, clipDb = 0;

    private:
        static constexpr int sub = 32;   // control-rate sub-block

        void processSubBlock (float* const* io, int numCh, int n, const EngineParams& p) noexcept;
        void outputStage (float* const* io, int numCh, int n, const EngineParams& p) noexcept;
        void saturate (float* const* d, int numCh, int n, float amount) noexcept;
        void updateAutoGain (const EngineParams& p, int n) noexcept;
        static float clipThresholdDb (const EngineParams& p) noexcept;
        static float softClip (float* const* d, int numCh, int n, float threshold) noexcept;
        void configureOutputModes();

        double sr = 48000.0;
        int maxBlock = 512, channels = 2;

        // ---- latency bookkeeping
        int lookahead = 72, totalLatency = 0, padBase = 0, padOs = 0;
        DelayLine dryDelay, padDelayBase, padDelayOs;
        juce::AudioBuffer<float> dryBuffer;
        float* dryPtrs[2] {};

        // ---- smoothed block-rate gains
        juce::SmoothedValue<float> inGain, outGain, mixAmt, bypassAmt;
        float modeFade = 1.0f;
        bool activeOs = false, firstBlock = true;

        // ---- control-rate parameter smoothing
        struct Smooth { float v = 0, t = 0; void step (float c) noexcept { v += (t - v) * c; } };
        Smooth sTight, sKick, sPunch, sSnap, sMud, sReese, sHarsh, sAir, sGlue, sWidth, sMono, sMonoHz;
        float paramCoef = 0.1f;

        // ---- low end (SUB TIGHT + KICK/BASS)
        juce::dsp::LinkwitzRileyFilter<float> lowSplit, monoSplit;
        Envelope tightFast, tightSlow, lowRms, kickFast, kickSlow;
        int samplesSinceKick = 1 << 30, kickRefractory = 4000;
        float lowGainPrev = 1.0f;
        Svf bodyBell;   // 220 Hz bass-body duck on kick hits
        float bodyDbCur = 0;

        // ---- transient shaping
        Envelope punchFast, punchSlow, snapFast, snapSlow;
        Svf snapDetect, snapBell;
        float punchGainPrev = 1.0f, snapDbCur = 0;

        // ---- dynamic EQ (MUD / REESE / HARSH)
        struct DynBell
        {
            Svf detect, eq;
            Envelope env;
            double freq = 1000, q = 1;
            float curDb = 0, levelDb = -100;
        };
        Svf refHp;
        Envelope refEnv;
        DynBell mudBell;
        std::array<DynBell, 5> reeseBells;
        std::array<DynBell, 2> harshBells;
        float dynAttCoef = 0.5f, dynRelCoef = 0.05f;

        // ---- AIR
        Svf airShelf, exciteHp1, exciteHp2;

        // ---- GLUE
        float glueEnvDb = 0, glueAvgGr = 0;

        // ---- output stage
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
        TruePeakLimiter limiterBase, limiterOs, limiterOsCatch;
        DcBlocker dcBase, dcOs;
        LoudnessMeter preLoud, postLoud;
        float autoGainCur = 0, loudCorrection = 0;
    };
}

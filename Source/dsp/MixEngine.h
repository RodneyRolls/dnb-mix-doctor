#pragma once
#include <juce_dsp/juce_dsp.h>
#include "DspUtils.h"
#include "TruePeakLimiter.h"
#include "LoudnessMeter.h"

namespace mcdsp
{
    /** Centre frequencies of the ADVANCED panel's 16-band graphic EQ (roughly 2/3-octave spacing). */
    inline constexpr std::array<float, 16> kEqFreqs {
        25.0f, 40.0f, 63.0f, 100.0f, 160.0f, 250.0f, 400.0f, 630.0f,
        1000.0f, 1600.0f, 2500.0f, 4000.0f, 6300.0f, 10000.0f, 14000.0f, 18000.0f };

    /** Plain parameter snapshot handed to the engine once per block. Amounts are 0..1. */
    struct EngineParams
    {
        float inputDb = 0, subMonoHz = 100, subTight = 0.2f, kickBass = 0.2f, punch = 0.2f, snap = 0.15f,
              mud = 0.25f, reese = 0.2f, harsh = 0.25f, air = 0.2f, glue = 0.25f, attackMs = 10, releaseMs = 150,
              width = 1.0f, saturate = 0.15f, clipper = 0.3f, autoGainMix = 1.0f, loudnessLufs = -7, ceilingDb = -1, fix = 1.0f, mix = 1.0f, outputDb = 0;
        bool releaseAuto = true, bypass = false, oversample = true;
        // ADVANCED: 16-band graphic EQ (dB per band), EQ MIX scales every band (0..1), EQ ON
        static constexpr int numEqBands = 16;
        std::array<float, numEqBands> eqDb {};
        float eqMix = 1.0f;
        bool eqOn = true;
        // dynamic EQ: a DYNAMIC band only applies its gain while that band is louder than its THRESHOLD
        std::array<bool, numEqBands> eqDyn {};
        std::array<float, numEqBands> eqThrDb { -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30, -30 };
        float eqDynAttackMs = 10.0f, eqDynReleaseMs = 150.0f;

        /** Per-function ADVANCED settings (double-click a knob). The defaults are the values the engine
            always used internally, so an untouched advanced page sounds exactly like before. */
        struct Advanced
        {
            float monoAmount = 1.0f;                                                   // SUB MONO: how much low side is removed
            float tightThrDb = -18.0f, tightMaxDb = 12.0f, tightReleaseMs = 300.0f;    // SUB TIGHT
            float kbSplitHz = 120.0f, kbDuckDb = 9.0f, kbDecayMs = 60.0f, kbBodyHz = 220.0f;   // KICK/BASS
            float punchWindowMs = 15.0f, punchReleaseMs = 40.0f, punchMaxDb = 10.0f;   // PUNCH
            float snapHz = 900.0f, snapQ = 0.5f, snapMaxDb = 9.0f;                     // SNAP
            float mudHz = 320.0f, mudQ = 0.9f, mudMaxDb = 9.0f;                        // MUD
            float reeseLowHz = 350.0f, reeseHighHz = 2800.0f, reeseQ = 2.0f, reeseMaxDb = 8.0f;   // REESE TAME
            float harshHz1 = 3200.0f, harshHz2 = 6000.0f, harshMaxDb = 9.0f;           // HARSH
            float airHz = 10000.0f, airMaxDb = 6.0f, airExcite = 0.5f;                 // AIR
            float widthGuard = 0.0f;                                                   // WIDTH: mono-safety guard 0..1
            float glueThrOffsetDb = 0.0f, glueRatioMul = 1.0f, glueKneeDb = 6.0f;      // GLUE
            float satWarmth = 0.5f;                                                    // SATURATE: even-harmonic share 0..1
            float clipSoftness = 0.6f;                                                 // CLIPPER: knee start as share of threshold
        } adv;
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

        /** Restarts the integrated meters only; the learned auto-gain is kept, so pressing play never changes the level. */
        void resetLoudness() noexcept { preLoud.resetIntegrated(); postLoud.resetIntegrated(); }
        /** Forgets the learned loudness (new song): auto-gain re-learns from the next loud section. */
        void relearnLoudness() noexcept { refLufs = -100.0f; lossDb = quietTime = 0.0f; }

        // meter values from the last block (audio thread writes, processor publishes)
        float glueGrDb = 0, limiterGrDb = 0, autoGainDb = 0, dynamicEqDb = 0, clipDb = 0;

    private:
        static constexpr int sub = 32;   // control-rate sub-block

        void graphicEq (float* const* io, int numCh, int n, const EngineParams& p) noexcept;
        void processSubBlock (float* const* io, int numCh, int n, const EngineParams& p) noexcept;
        void outputStage (float* const* io, int numCh, int n, const EngineParams& p) noexcept;
        void saturate (float* const* d, int numCh, int n, float amount, float warmth) noexcept;
        void updateAdvanced (const EngineParams::Advanced& a) noexcept;
        void updateAutoGain (const EngineParams& p, int n) noexcept;
        static float clipThresholdDb (const EngineParams& p) noexcept;
        static float softClip (float* const* d, int numCh, int n, float threshold, float softness = 0.6f) noexcept;
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

        // ---- ADVANCED settings currently applied (smoothed frequencies, cached times)
        EngineParams::Advanced advCur;            // values the filters/envelopes are configured for
        Smooth sKbSplit, sKbBody, sSnapHz, sMudHz, sReeseLo, sReeseHi, sHarsh1, sHarsh2, sAirHz;
        float widthCorr = 1.0f, widthGuardCur = 1.0f;   // WIDTH guard: running correlation of the highs
        float lowSplitHz = 120.0f, snapHzSet = 900.0f, snapQSet = 0.5f;
        bool advInit = false;
        void retuneBell (DynBell& b, float freq, float q) noexcept;

        // ---- ADVANCED 16-band graphic EQ
        std::array<Svf, EngineParams::numEqBands> eqBands;
        std::array<float, EngineParams::numEqBands> eqCurDb {};
        bool eqActive = false;
        std::array<Svf, EngineParams::numEqBands> eqDet;      // band level detectors for DYNAMIC bands
        std::array<float, EngineParams::numEqBands> eqEnv {}; // band power envelopes
        std::array<float, EngineParams::numEqBands> eqPrevDb {};
        std::array<bool, EngineParams::numEqBands> eqIdle {};
        float eqAttCoef = 0.99f, eqRelCoef = 0.999f, eqAttMsSet = -1.0f, eqRelMsSet = -1.0f, eqFastCoef = 0.5f;
    public:
        /** Live gain of each EQ band in dB (dynamic bands move with the music), for the UI. */
        std::array<float, EngineParams::numEqBands> eqLiveDb {};
    private:

        // ---- output stage
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
        TruePeakLimiter limiterBase, limiterOs, limiterOsCatch;
        DcBlocker dcBase, dcOs;
        LoudnessMeter preLoud, postLoud;
        float autoGainCur = 0, refLufs = -100.0f, lossDb = 0, quietTime = 0, prevAgMix = 1.0f;
    };
}

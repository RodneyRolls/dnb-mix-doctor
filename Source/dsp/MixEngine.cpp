#include "MixEngine.h"

namespace mcdsp
{
    namespace
    {
        constexpr float kTiny = 1.0e-9f;
        constexpr std::array<double, 5> kReeseFreqs { 350.0, 600.0, 1000.0, 1700.0, 2800.0 };
        constexpr std::array<double, 2> kHarshFreqs { 3200.0, 6000.0 };

        /** Ramp a gain linearly across a sub-block. */
        inline void applyGainRamp (float* const* d, int numCh, int n, float from, float to) noexcept
        {
            const float step = (to - from) / (float) n;
            for (int ch = 0; ch < numCh; ++ch)
            {
                float g = from;
                for (int i = 0; i < n; ++i) { g += step; d[ch][i] *= g; }
            }
        }

        /** Generic "how much should this dynamic band cut" curve. */
        inline float dynCut (float excessDb, float amount, float maxCutDb) noexcept
        {
            if (amount <= 0.0005f || excessDb <= 0.0f) return 0.0f;
            return -std::min (maxCutDb * amount, excessDb * (0.35f + 0.45f * amount));
        }
    }

    //==============================================================================
    void MixEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        sr = sampleRate;
        maxBlock = std::max (32, maxBlockSize);
        channels = juce::jlimit (1, 2, numChannels);

        juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, (juce::uint32) channels };
        lowSplit.prepare (spec);  lowSplit.setCutoffFrequency (120.0f);
        monoSplit.prepare (spec); monoSplit.setCutoffFrequency (100.0f);

        paramCoef = 1.0f - onePoleCoef (0.03, sr / sub);
        dynAttCoef = 1.0f - onePoleCoef (0.004, sr / sub);
        dynRelCoef = 1.0f - onePoleCoef (0.090, sr / sub);
        eqFastCoef = 1.0f - onePoleCoef (0.002, sr / sub);
        for (size_t b = 0; b < eqDet.size(); ++b) eqDet[b].setup (Svf::Mode::bandpass, sr, (double) kEqFreqs[b], 1.8);
        eqAttMsSet = eqRelMsSet = -1.0f;

        tightFast.setup (0.001, 0.025, sr);
        tightSlow.setup (0.001, 0.300, sr);
        lowRms.setup (0.010, 0.120, sr);
        kickFast.setup (0.0005, 0.030, sr);
        kickSlow.setup (0.030, 0.200, sr);
        kickRefractory = (int) (0.09 * sr);

        punchFast.setup (0.0001, 0.040, sr);
        punchSlow.setup (0.015, 0.040, sr);
        snapFast.setup (0.0002, 0.040, sr);
        snapSlow.setup (0.012, 0.040, sr);
        snapDetect.setup (Svf::Mode::bandpass, sr, 900.0, 0.5);
        snapBell.setup (Svf::Mode::bell, sr, 900.0, 0.5, 0.0f);
        bodyBell.setup (Svf::Mode::bell, sr, 220.0, 0.9, 0.0f);

        refHp.setup (Svf::Mode::highpass, sr, 150.0, 0.707);
        refEnv.setup (0.003, 0.060, sr);

        auto initBell = [this] (DynBell& b, double f, double q, double att, double rel)
        {
            b.freq = f; b.q = q;
            b.detect.setup (Svf::Mode::bandpass, sr, f, q);
            b.eq.setup (Svf::Mode::bell, sr, f, q, 0.0f);
            b.env.setup (att, rel, sr);
        };
        initBell (mudBell, 320.0, 0.9, 0.005, 0.080);
        for (size_t i = 0; i < reeseBells.size(); ++i) initBell (reeseBells[i], kReeseFreqs[i], 2.0, 0.004, 0.070);
        for (size_t i = 0; i < harshBells.size(); ++i) initBell (harshBells[i], kHarshFreqs[i], 1.4, 0.001, 0.050);

        airShelf.setup (Svf::Mode::highShelf, sr, 10000.0, 0.7, 0.0f);
        exciteHp1.setup (Svf::Mode::highpass, sr, 5000.0, 0.707);
        exciteHp2.setup (Svf::Mode::highpass, sr, 9000.0, 0.707);

        // ---- output stage: base-rate and 4x paths, constant total latency
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channels, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        oversampler->initProcessing ((size_t) maxBlock);

        lookahead = std::max (8, (int) std::lround (0.0015 * sr));
        // maximiser: fast program-dependent release (25 ms for peaks, 250 ms when limiting is sustained)
        limiterBase.prepare (sr, lookahead, true, 0.025, 0.25);
        limiterOs.prepare (sr * 4.0, lookahead * 4 + 1, false, 0.025, 0.25);   // (4L+1)-1 = 4L samples at 4x == L at 1x
        dcBase.setup (sr);
        dcOs.setup (sr * 4.0);

        // after decimation, a short true-peak catch limiter removes the small overs the downsampling
        // filter re-introduces, so the 4x path honours the dBTP ceiling as strictly as the 1x path
        limiterOsCatch.prepare (sr, std::max (8, lookahead / 2), true, 0.03);

        const int osLatency = (int) std::lround (oversampler->getLatencyInSamples());
        const int baseTotal = limiterBase.getLatency();
        const int osTotal = osLatency + lookahead + limiterOsCatch.getLatency();
        totalLatency = std::max (baseTotal, osTotal);
        padBase = totalLatency - baseTotal;
        padOs = totalLatency - osTotal;

        padDelayBase.prepare (padBase + 1); padDelayBase.setDelay (padBase);
        padDelayOs.prepare (padOs + 1);     padDelayOs.setDelay (padOs);
        dryDelay.prepare (totalLatency + 1); dryDelay.setDelay (totalLatency);

        dryBuffer.setSize (2, maxBlock, false, true, false);
        dryPtrs[0] = dryBuffer.getWritePointer (0);
        dryPtrs[1] = dryBuffer.getWritePointer (1);

        preLoud.prepare (sr);
        postLoud.prepare (sr);

        for (auto* s : { &inGain, &outGain, &mixAmt })
            s->reset (sr, 0.05);
        bypassAmt.reset (sr, 0.03);

        reset();
    }

    void MixEngine::reset()
    {
        lowSplit.reset(); monoSplit.reset();
        for (auto* e : { &tightFast, &tightSlow, &lowRms, &kickFast, &kickSlow, &punchFast, &punchSlow,
                         &snapFast, &snapSlow, &refEnv })
            e->reset();
        for (auto* f : { &snapDetect, &snapBell, &bodyBell, &refHp, &airShelf, &exciteHp1, &exciteHp2 })
            f->reset();
        auto resetBell = [] (DynBell& b) { b.detect.reset(); b.eq.reset(); b.env.reset(); b.curDb = 0; };
        resetBell (mudBell);
        for (auto& b : reeseBells) resetBell (b);
        for (auto& b : harshBells) resetBell (b);

        for (auto& b : eqBands) b.reset();
        eqCurDb.fill (0.0f);
        for (auto& d : eqDet) d.reset();
        eqEnv.fill (0.0f); eqPrevDb.fill (0.0f); eqIdle.fill (false); eqLiveDb.fill (0.0f);
        eqActive = false;
        samplesSinceKick = 1 << 30;
        lowGainPrev = punchGainPrev = 1.0f;
        bodyDbCur = snapDbCur = 0;
        glueEnvDb = glueAvgGr = 0;
        advInit = false;
        widthCorr = widthGuardCur = 1.0f;

        if (oversampler) oversampler->reset();
        limiterBase.reset(); limiterOs.reset(); limiterOsCatch.reset();
        dcBase.reset(); dcOs.reset();
        firstBlock = true;
        dryDelay.reset(); padDelayBase.reset(); padDelayOs.reset();
        preLoud.resetAll(); postLoud.resetAll();
        autoGainCur = lossDb = quietTime = 0;
        refLufs = -100.0f;
        modeFade = 1.0f;
    }

    //==============================================================================
    void MixEngine::process (float* const* io, int numCh, int n, const EngineParams& p) noexcept
    {
        numCh = std::min (numCh, channels);
        jassert (n <= maxBlock);

        if (firstBlock)   // start every smoother at its target: no fade-in or parameter sweep on load
        {
            firstBlock = false;
            inGain.setCurrentAndTargetValue (dbToGain (p.inputDb));
            outGain.setCurrentAndTargetValue (dbToGain (p.outputDb));
            mixAmt.setCurrentAndTargetValue (p.mix);
            bypassAmt.setCurrentAndTargetValue (p.bypass ? 1.0f : 0.0f);
            activeOs = p.oversample;
            prevAgMix = std::clamp (p.autoGainMix, 0.0f, 1.0f);
            modeFade = 1.0f;
            const float f = p.fix;
            sTight.v = p.subTight * f; sKick.v = p.kickBass * f; sPunch.v = p.punch * f; sSnap.v = p.snap * f;
            sMud.v = p.mud * f; sReese.v = p.reese * f; sHarsh.v = p.harsh * f; sAir.v = p.air * f; sGlue.v = p.glue * f;
            sWidth.v = 1.0f + (p.width - 1.0f) * f; sMono.v = f; sMonoHz.v = p.subMonoHz;
            for (size_t b = 0; b < eqCurDb.size(); ++b)
                eqCurDb[b] = p.eqOn && ! p.eqDyn[b] ? std::clamp (p.eqDb[b], -12.0f, 12.0f) * std::clamp (p.eqMix, 0.0f, 1.0f) : 0.0f;
        }

        // ---- latency-aligned dry copy (pre input gain, so MIX 0 % == untouched input)
        for (int ch = 0; ch < numCh; ++ch)
            std::copy (io[ch], io[ch] + n, dryPtrs[ch]);
        dryDelay.processBlock (dryPtrs, numCh, n);

        // ---- input gain
        inGain.setTargetValue (dbToGain (p.inputDb));
        if (inGain.isSmoothing())
            for (int i = 0; i < n; ++i) { const float g = inGain.getNextValue(); for (int ch = 0; ch < numCh; ++ch) io[ch][i] *= g; }
        else
            for (int ch = 0; ch < numCh; ++ch) juce::FloatVectorOperations::multiply (io[ch], inGain.getTargetValue(), n);

        // ---- corrective chain at control rate
        const float f = p.fix;
        sTight.t = p.subTight * f;   sKick.t = p.kickBass * f;  sPunch.t = p.punch * f;  sSnap.t = p.snap * f;
        sMud.t = p.mud * f;          sReese.t = p.reese * f;    sHarsh.t = p.harsh * f;  sAir.t = p.air * f;
        sGlue.t = p.glue * f;        sWidth.t = 1.0f + (p.width - 1.0f) * f;            sMono.t = f;
        sMonoHz.t = p.subMonoHz;
        updateAdvanced (p.adv);

        glueGrDb = 0; dynamicEqDb = 0;
        for (int start = 0; start < n; start += sub)
        {
            float* chunk[2] = { io[0] + start, numCh > 1 ? io[1] + start : nullptr };
            processSubBlock (chunk, numCh, std::min (sub, n - start), p);
        }

        outputStage (io, numCh, n, p);
    }

    //==============================================================================
    void MixEngine::processSubBlock (float* const* io, int numCh, int n, const EngineParams& p) noexcept
    {
        for (auto* s : { &sTight, &sKick, &sPunch, &sSnap, &sMud, &sReese, &sHarsh, &sAir, &sGlue, &sWidth, &sMono, &sMonoHz,
                         &sKbSplit, &sKbBody, &sSnapHz, &sMudHz, &sReeseLo, &sReeseHi, &sHarsh1, &sHarsh2, &sAirHz })
            s->step (paramCoef);
        const auto& A = advCur;

        const bool stereo = numCh > 1;
        auto mono = [&] (int i) { return stereo ? 0.5f * (io[0][i] + io[1][i]) : io[0][i]; };

        // ====== 1. LOW END: SUB TIGHT + KICK/BASS on the < 120 Hz band ===========================
        {
            if (std::abs (sKbSplit.v - lowSplitHz) > 0.05f)
            {
                lowSplitHz = sKbSplit.v;
                lowSplit.setCutoffFrequency (lowSplitHz);
            }
            float lo[2][sub], hi[2][sub];
            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < numCh; ++ch)
                    lowSplit.processSample (ch, io[ch][i], lo[ch][i], hi[ch][i]);

                const float m = std::abs (stereo ? 0.5f * (lo[0][i] + lo[1][i]) : lo[0][i]);
                tightFast.process (m);
                tightSlow.process (m);
                lowRms.process (m * m);
                const float kf = kickFast.process (m), ks = kickSlow.process (m);

                if (samplesSinceKick > kickRefractory && kf > 0.02f && kf > ks * 1.8f)
                    samplesSinceKick = 0;
                else if (samplesSinceKick < (1 << 30))
                    ++samplesSinceKick;
            }

            // SUB TIGHT: pull the sustain of the sub down (fast vs slow release) + gentle compression
            const float aT = sTight.v;
            const float sustainDb = gainToDb ((tightFast.value + kTiny) / (tightSlow.value + kTiny));
            const float tightDb = std::max (-A.tightMaxDb * aT, aT * 1.2f * std::min (0.0f, sustainDb));
            const float compDb = -std::max (0.0f, powerToDb (lowRms.value) - A.tightThrDb) * 0.5f * aT;

            // KICK/BASS: after the kick's attack (first ~8 ms passes untouched) duck the low band so the
            // overlapping bass sustain doesn't smear the kick; also dip the 220 Hz bass body during the hit.
            const float t = (float) samplesSinceKick / (float) sr;
            float shape = 0.0f;
            if (t < 0.008f)       shape = 0.0f;
            else if (t < 0.014f)  shape = (t - 0.008f) / 0.006f;
            else if (t < 0.045f)  shape = 1.0f;
            else if (t < 0.6f)    shape = std::exp (-(t - 0.045f) / (A.kbDecayMs * 0.001f));
            const float aK = sKick.v;
            const float duckDb = -A.kbDuckDb * aK * shape;
            const float bodyShape = t < 0.002f ? t / 0.002f : (t < 0.6f ? std::exp (-(t - 0.002f) / 0.08f) : 0.0f);
            const float bodyTarget = -6.0f * aK * bodyShape;

            const float lowGain = dbToGain (std::max (-18.0f, tightDb + compDb + duckDb));
            float g = lowGainPrev;
            const float step = (lowGain - lowGainPrev) / (float) n;
            for (int i = 0; i < n; ++i)
            {
                g += step;
                for (int ch = 0; ch < numCh; ++ch)
                    io[ch][i] = lo[ch][i] * g + hi[ch][i];
            }
            lowGainPrev = lowGain;

            bodyDbCur += (bodyTarget - bodyDbCur) * 0.5f;
            if (aK > 0.0005f || std::abs (bodyDbCur) > 0.01f)
            {
                bodyBell.setup (Svf::Mode::bell, sr, sKbBody.v, 0.9, bodyDbCur);
                for (int ch = 0; ch < numCh; ++ch)
                    for (int i = 0; i < n; ++i) io[ch][i] = bodyBell.process (io[ch][i], ch);
            }
        }

        // ====== 2. SUB MONO + WIDTH (Linkwitz-Riley split at the SUB MONO frequency) ============
        if (stereo)
        {
            monoSplit.setCutoffFrequency (sMonoHz.v);
            const float sideLo = 1.0f - sMono.v * std::clamp (A.monoAmount, 0.0f, 1.0f);
            // WIDTH guard: when the highs go out of phase (correlation below +0.3), pull any extra width back
            // towards 100 % so the mix stays mono-compatible. guard 0 = off.
            {
                const float target = A.widthGuard <= 0.0f || sWidth.v <= 1.0f ? 1.0f
                                   : 1.0f - std::clamp (A.widthGuard, 0.0f, 1.0f) * clamp01 ((0.3f - widthCorr) / 0.6f);
                widthGuardCur += (target - widthGuardCur) * paramCoef;
            }
            const float sideHi = 1.0f + (sWidth.v - 1.0f) * (sWidth.v > 1.0f ? widthGuardCur : 1.0f);
            double sLR = 0, sLL = 0, sRR = 0;
            for (int i = 0; i < n; ++i)
            {
                float lL, hL, lR, hR;
                monoSplit.processSample (0, io[0][i], lL, hL);
                monoSplit.processSample (1, io[1][i], lR, hR);
                sLR += (double) hL * hR; sLL += (double) hL * hL; sRR += (double) hR * hR;
                const float m = 0.5f * (lL + lR + hL + hR);
                const float s = 0.5f * ((lL - lR) * sideLo + (hL - hR) * sideHi);
                io[0][i] = m + s;
                io[1][i] = m - s;
            }
            if (sLL + sRR > 1.0e-10)   // ~300 ms running correlation of the highs, ignoring silence
            {
                const float c = (float) (sLR / std::sqrt (sLL * sRR + 1.0e-20));
                widthCorr += (std::clamp (c, -1.0f, 1.0f) - widthCorr) * (float) (n / (0.3 * sr));
            }
        }

        // ====== 3. PUNCH (broadband transient shaper) =============================================
        {
            for (int i = 0; i < n; ++i)
            {
                const float m = std::abs (mono (i));
                punchFast.process (m);
                punchSlow.process (m);
            }
            const float a = sPunch.v;
            const float trDb = std::clamp (gainToDb ((punchFast.value + kTiny) / (punchSlow.value + kTiny)), 0.0f, 15.0f);
            const float gain = dbToGain (std::min (A.punchMaxDb * a, a * 1.0f * trDb));
            applyGainRamp (io, numCh, n, punchGainPrev, gain);
            punchGainPrev = gain;
        }

        // ====== 4. SNAP (transient bell boost in the snare range 150 Hz - 2 kHz) =================
        {
            if (std::abs (sSnapHz.v - snapHzSet) > 0.05f || A.snapQ != snapQSet)
            {
                snapHzSet = sSnapHz.v; snapQSet = A.snapQ;
                snapDetect.setup (Svf::Mode::bandpass, sr, snapHzSet, snapQSet);
            }
            for (int i = 0; i < n; ++i)
            {
                const float bp = std::abs (snapDetect.process (mono (i)));
                snapFast.process (bp);
                snapSlow.process (bp);
            }
            const float a = sSnap.v;
            const float trDb = std::clamp (gainToDb ((snapFast.value + kTiny) / (snapSlow.value + kTiny)), 0.0f, 15.0f);
            const float target = std::min (A.snapMaxDb * a, a * 1.2f * trDb);
            snapDbCur += (target - snapDbCur) * (target > snapDbCur ? 0.7f : 0.25f);
            if (a > 0.0005f || snapDbCur > 0.01f)
            {
                snapBell.setup (Svf::Mode::bell, sr, snapHzSet, snapQSet, snapDbCur);
                for (int ch = 0; ch < numCh; ++ch)
                    for (int i = 0; i < n; ++i) io[ch][i] = snapBell.process (io[ch][i], ch);
            }
        }

        // ====== 5. MUD / REESE TAME / HARSH : dynamic EQ =========================================
        {
            // ADVANCED: retune the detectors and bells when their frequencies / widths move
            retuneBell (mudBell, sMudHz.v, A.mudQ);
            for (size_t k = 0; k < reeseBells.size(); ++k)
            {
                const float lo = std::min (sReeseLo.v, sReeseHi.v), hi = std::max (sReeseLo.v, sReeseHi.v);
                // keep the original spacing (350 / 600 / 1000 / 1700 / 2800 Hz at the default range)
                const float pos = (float) (std::log (kReeseFreqs[k] / kReeseFreqs[0]) / std::log (kReeseFreqs.back() / kReeseFreqs[0]));
                const float f = lo * std::pow (hi / lo, pos);
                retuneBell (reeseBells[k], f, A.reeseQ);
            }
            retuneBell (harshBells[0], sHarsh1.v, 1.4f);
            retuneBell (harshBells[1], sHarsh2.v, 1.4f);

            auto detectBell = [] (DynBell& b, float x) { const float bp = b.detect.process (x); b.env.process (bp * bp); };
            for (int i = 0; i < n; ++i)
            {
                const float m = mono (i);
                const float r = refHp.process (m);
                refEnv.process (r * r);
                detectBell (mudBell, m);
                for (auto& b : reeseBells) detectBell (b, m);
                for (auto& b : harshBells) detectBell (b, m);
            }
            const float refDb = powerToDb (refEnv.value);
            const bool signal = refDb > -70.0f;

            auto smoothAndSet = [this] (DynBell& b, float targetDb)
            {
                b.curDb += (targetDb - b.curDb) * (targetDb < b.curDb ? dynAttCoef : dynRelCoef);
                b.eq.setup (Svf::Mode::bell, sr, b.freq, b.q, b.curDb);
            };
            auto applyBell = [&] (DynBell& b)
            {
                if (std::abs (b.curDb) < 0.005f) return;
                for (int ch = 0; ch < numCh; ++ch)
                    for (int i = 0; i < n; ++i) io[ch][i] = b.eq.process (io[ch][i], ch);
            };

            // MUD: 200-500 Hz build-up relative to the broadband (above 150 Hz) level
            {
                const float a = sMud.v;
                mudBell.levelDb = powerToDb (mudBell.env.value);
                const float excess = mudBell.levelDb - (refDb - 3.0f - 5.0f * a);
                smoothAndSet (mudBell, signal ? dynCut (excess, a, A.mudMaxDb) : 0.0f);
                applyBell (mudBell);
            }
            // REESE TAME: resonances poking out above their spectral neighbours (300 Hz - 3 kHz)
            {
                const float a = sReese.v;
                for (auto& b : reeseBells) b.levelDb = powerToDb (b.env.value);
                for (size_t k = 0; k < reeseBells.size(); ++k)
                {
                    float neighbour;
                    if (k == 0)                         neighbour = reeseBells[1].levelDb;
                    else if (k == reeseBells.size() - 1) neighbour = reeseBells[k - 1].levelDb;
                    else                                neighbour = 0.5f * (reeseBells[k - 1].levelDb + reeseBells[k + 1].levelDb);
                    const float excess = reeseBells[k].levelDb - neighbour - (4.0f - 3.0f * a);
                    smoothAndSet (reeseBells[k], signal && reeseBells[k].levelDb > -60.0f ? dynCut (excess, a, A.reeseMaxDb) : 0.0f);
                    applyBell (reeseBells[k]);
                }
            }
            // HARSH: 2-8 kHz bursts relative to the broadband level
            {
                const float a = sHarsh.v;
                for (auto& b : harshBells)
                {
                    b.levelDb = powerToDb (b.env.value);
                    const float excess = b.levelDb - (refDb - 10.0f - 6.0f * a);
                    smoothAndSet (b, signal ? dynCut (excess, a, A.harshMaxDb) : 0.0f);
                    applyBell (b);
                }
            }

            float deepest = mudBell.curDb;
            for (auto& b : reeseBells) deepest = std::min (deepest, b.curDb);
            for (auto& b : harshBells) deepest = std::min (deepest, b.curDb);
            dynamicEqDb = std::min (dynamicEqDb, deepest);
        }

        // ====== 6. AIR: 10 kHz shelf + gentle harmonic exciter ===================================
        {
            const float a = sAir.v;
            if (a > 0.0005f)
            {
                airShelf.setup (Svf::Mode::highShelf, sr, sAirHz.v, 0.7, A.airMaxDb * a);
                const float exc = A.airExcite * a;
                for (int ch = 0; ch < numCh; ++ch)
                    for (int i = 0; i < n; ++i)
                    {
                        const float x = io[ch][i];
                        const float h = exciteHp2.process (std::tanh (3.0f * exciteHp1.process (x, ch)) * 0.333f, ch);
                        io[ch][i] = airShelf.process (x, ch) + h * exc;
                    }
            }
        }

        // ====== 7. GLUE bus compressor ==========================================================
        {
            const float a = sGlue.v;
            if (a > 0.0005f || glueEnvDb < -0.01f)
            {
                const float thr = -6.0f - 18.0f * a + A.glueThrOffsetDb;
                const float ratio = std::max (1.05f, (1.5f + 2.5f * a) * A.glueRatioMul);
                const float knee = std::max (0.1f, A.glueKneeDb);
                const float slope = 1.0f / ratio - 1.0f;
                const float makeup = -thr * (1.0f - 1.0f / ratio) * 0.25f * a;
                const float att = onePoleCoef (p.attackMs * 0.001, sr);
                const float relS = p.releaseAuto ? 0.06f + 0.84f * clamp01 (-glueAvgGr / 6.0f) : p.releaseMs * 0.001f;
                const float rel = onePoleCoef (relS, sr);
                float deepest = 0;

                for (int i = 0; i < n; ++i)
                {
                    float pk = std::abs (io[0][i]);
                    if (stereo) pk = std::max (pk, std::abs (io[1][i]));
                    const float over = gainToDb (pk) - thr;
                    float gc = 0.0f;
                    if (2.0f * over > knee)            gc = slope * over;
                    else if (2.0f * over > -knee)      gc = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
                    gc *= std::min (1.0f, a * 20.0f);   // fade the compressor in from 0 %
                    glueEnvDb = gc < glueEnvDb ? att * glueEnvDb + (1.0f - att) * gc
                                               : rel * glueEnvDb + (1.0f - rel) * gc;
                    deepest = std::min (deepest, glueEnvDb);
                    const float g = dbToGain (glueEnvDb + makeup);
                    for (int ch = 0; ch < numCh; ++ch) io[ch][i] *= g;
                }
                glueAvgGr += (deepest - glueAvgGr) * (float) (n / sr);   // ~1 s average for AUTO release
                glueGrDb = std::min (glueGrDb, deepest);
            }
        }

        // ====== 8. ADVANCED 16-band graphic EQ ====================================================
        graphicEq (io, numCh, n, p);
    }

    void MixEngine::updateAdvanced (const EngineParams::Advanced& a) noexcept
    {
        // Called once per host block. Frequencies glide (smoothed per sub-block); envelope times are only
        // recomputed when they actually change. Everything is clamped so automation can never break a filter.
        const float fMax = (float) (sr * 0.45);
        auto hz = [fMax] (float f, float lo) { return std::clamp (f, lo, fMax); };
        auto ms = [] (float v, float lo, float hi) { return std::clamp (v, lo, hi); };

        EngineParams::Advanced c = a;
        c.monoAmount = clamp01 (a.monoAmount);
        c.tightThrDb = std::clamp (a.tightThrDb, -40.0f, 0.0f);
        c.tightMaxDb = std::clamp (a.tightMaxDb, 0.0f, 24.0f);
        c.tightReleaseMs = ms (a.tightReleaseMs, 50.0f, 1000.0f);
        c.kbSplitHz = hz (a.kbSplitHz, 40.0f);
        c.kbDuckDb = std::clamp (a.kbDuckDb, 0.0f, 18.0f);
        c.kbDecayMs = ms (a.kbDecayMs, 10.0f, 300.0f);
        c.kbBodyHz = hz (a.kbBodyHz, 60.0f);
        c.punchWindowMs = ms (a.punchWindowMs, 2.0f, 60.0f);
        c.punchReleaseMs = ms (a.punchReleaseMs, 10.0f, 200.0f);
        c.punchMaxDb = std::clamp (a.punchMaxDb, 0.0f, 18.0f);
        c.snapHz = hz (a.snapHz, 100.0f);
        c.snapQ = std::clamp (a.snapQ, 0.2f, 4.0f);
        c.snapMaxDb = std::clamp (a.snapMaxDb, 0.0f, 18.0f);
        c.mudHz = hz (a.mudHz, 80.0f);
        c.mudQ = std::clamp (a.mudQ, 0.3f, 6.0f);
        c.mudMaxDb = std::clamp (a.mudMaxDb, 0.0f, 18.0f);
        c.reeseLowHz = hz (a.reeseLowHz, 100.0f);
        c.reeseHighHz = hz (a.reeseHighHz, 100.0f);
        c.reeseQ = std::clamp (a.reeseQ, 0.5f, 8.0f);
        c.reeseMaxDb = std::clamp (a.reeseMaxDb, 0.0f, 18.0f);
        c.harshHz1 = hz (a.harshHz1, 1000.0f);
        c.harshHz2 = hz (a.harshHz2, 1000.0f);
        c.harshMaxDb = std::clamp (a.harshMaxDb, 0.0f, 18.0f);
        c.airHz = hz (a.airHz, 2000.0f);
        c.airMaxDb = std::clamp (a.airMaxDb, 0.0f, 12.0f);
        c.airExcite = clamp01 (a.airExcite);
        c.widthGuard = clamp01 (a.widthGuard);
        c.glueThrOffsetDb = std::clamp (a.glueThrOffsetDb, -24.0f, 24.0f);
        c.glueRatioMul = std::clamp (a.glueRatioMul, 0.25f, 4.0f);
        c.glueKneeDb = std::clamp (a.glueKneeDb, 0.0f, 18.0f);

        if (! advInit || c.tightReleaseMs != advCur.tightReleaseMs)
            tightSlow.setup (0.001, c.tightReleaseMs * 0.001, sr);
        if (! advInit || c.punchWindowMs != advCur.punchWindowMs || c.punchReleaseMs != advCur.punchReleaseMs)
        {
            punchFast.setup (0.0001, c.punchReleaseMs * 0.001, sr);
            punchSlow.setup (c.punchWindowMs * 0.001, c.punchReleaseMs * 0.001, sr);
        }

        sKbSplit.t = c.kbSplitHz; sKbBody.t = c.kbBodyHz; sSnapHz.t = c.snapHz; sMudHz.t = c.mudHz;
        sReeseLo.t = c.reeseLowHz; sReeseHi.t = c.reeseHighHz; sHarsh1.t = c.harshHz1; sHarsh2.t = c.harshHz2;
        sAirHz.t = c.airHz;
        if (! advInit)
        {
            for (auto* s : { &sKbSplit, &sKbBody, &sSnapHz, &sMudHz, &sReeseLo, &sReeseHi, &sHarsh1, &sHarsh2, &sAirHz })
                s->v = s->t;
            advInit = true;
        }
        advCur = c;
    }

    void MixEngine::retuneBell (DynBell& b, float freq, float q) noexcept
    {
        if (std::abs ((double) freq - b.freq) < 0.05 && std::abs ((double) q - b.q) < 1.0e-4) return;
        b.freq = freq; b.q = q;
        b.detect.setup (Svf::Mode::bandpass, sr, b.freq, b.q);
    }

    void MixEngine::graphicEq (float* const* io, int numCh, int n, const EngineParams& p) noexcept
    {
        // EQ MIX scales every band's gain (0 % = flat, 100 % = as drawn), so it blends the curve in without the
        // phase smearing a parallel dry/wet blend of minimum-phase bells would cause. EQ ON fades the curve out.
        // A DYNAMIC band applies its gain only while the band is louder than its THRESHOLD: none of it at the
        // threshold, all of it 6 dB above (attack/release from the EQ's ATTACK and RELEASE).
        const float depth = p.eqOn ? std::clamp (p.eqMix, 0.0f, 1.0f) : 0.0f;
        const int nb = (int) eqBands.size();

        bool anyDyn = false;
        for (int b = 0; b < nb; ++b)
            if (p.eqDyn[(size_t) b] && std::abs (p.eqDb[(size_t) b]) > 0.001f && depth > 0.0f) anyDyn = true;
            else eqEnv[(size_t) b] = 0.0f;
        if (anyDyn)
        {
            const float attMs = std::clamp (p.eqDynAttackMs, 0.5f, 200.0f), relMs = std::clamp (p.eqDynReleaseMs, 5.0f, 2000.0f);
            if (attMs != eqAttMsSet) { eqAttCoef = onePoleCoef (attMs * 0.001, sr); eqAttMsSet = attMs; }
            if (relMs != eqRelMsSet) { eqRelCoef = onePoleCoef (relMs * 0.001, sr); eqRelMsSet = relMs; }
            const bool stereo = numCh > 1;
            for (int i = 0; i < n; ++i)
            {
                const float m = stereo ? 0.5f * (io[0][i] + io[1][i]) : io[0][i];
                for (int b = 0; b < nb; ++b)
                {
                    if (! p.eqDyn[(size_t) b] || std::abs (p.eqDb[(size_t) b]) <= 0.001f) continue;
                    const float y = eqDet[(size_t) b].process (m);
                    const float pw = y * y;
                    auto& e = eqEnv[(size_t) b];
                    e = pw + (pw > e ? eqAttCoef : eqRelCoef) * (e - pw);
                }
            }
        }

        bool any = false;
        for (int b = 0; b < nb; ++b)
        {
            float target = std::clamp (p.eqDb[(size_t) b], -12.0f, 12.0f) * depth;
            auto& cur = eqCurDb[(size_t) b];
            if (p.eqDyn[(size_t) b])
            {
                // band power of a bandpass sits ~3 dB under a sine's peak level; compare in RMS-like dB
                const float levelDb = powerToDb (eqEnv[(size_t) b]) + 3.0f;
                target *= clamp01 ((levelDb - p.eqThrDb[(size_t) b]) / 6.0f);
                cur += (target - cur) * eqFastCoef;   // the envelope already sets the timing
            }
            else
                cur += (target - cur) * paramCoef;
            if (std::abs (target - cur) < 0.001f) cur = target;
            eqLiveDb[(size_t) b] = cur;
            any = any || std::abs (cur) > 0.001f;
        }
        if (! any)
        {
            if (eqActive) { for (auto& f : eqBands) f.reset(); eqActive = false; }
            return;
        }
        eqActive = true;
        for (size_t b = 0; b < eqBands.size(); ++b)
        {
            if (std::abs (eqCurDb[b]) < 0.001f && std::abs (eqPrevDb[b]) < 0.001f)   // a 0 dB bell is exactly transparent: skip it
            {
                if (eqPrevDb[b] != 0.0f || ! eqIdle[b]) { eqBands[b].reset(); eqIdle[b] = true; }
                eqPrevDb[b] = 0.0f;
                continue;
            }
            eqIdle[b] = false;
            eqPrevDb[b] = eqCurDb[b];
            auto& f = eqBands[b];
            f.setup (Svf::Mode::bell, sr, (double) kEqFreqs[b], 1.8, eqCurDb[b]);
            for (int ch = 0; ch < numCh; ++ch)
                for (int i = 0; i < n; ++i) io[ch][i] = f.process (io[ch][i], ch);
        }
    }

    //==============================================================================
    void MixEngine::saturate (float* const* d, int numCh, int n, float s, float warmth) noexcept
    {
        if (s <= 0.0005f) return;
        const float drive = 1.0f + 3.0f * std::pow (s, 1.3f);
        const float bias = 0.2f * std::clamp (warmth, 0.0f, 1.0f) * s;   // asymmetry -> 2nd harmonic ("valve"); WARMTH 50 % = original
        const float tb = std::tanh (drive * bias);
        const float norm = 1.0f / (drive * (1.0f - tb * tb));   // unity small-signal gain
        const float blend = std::min (1.0f, 3.0f * s);
        for (int ch = 0; ch < numCh; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float x = d[ch][i];
                const float y = (std::tanh (drive * (x + bias)) - tb) * norm;
                d[ch][i] = x + (y - x) * blend;
            }
    }

    float MixEngine::clipThresholdDb (const EngineParams& p) noexcept
    {
        // CLIPPER knob: 0 % = off (threshold far above anything), 100 % = clipper sits 0.1 dB over the ceiling
        // and takes almost every peak off before the limiter (loudest, most aggressive). Independent of LOUDNESS.
        if (p.clipper <= 0.001f) return 60.0f;
        const float head = 6.0f - 5.9f * std::pow (std::clamp (p.clipper, 0.0f, 1.0f), 0.7f);
        return p.ceilingDb + head;
    }

    float MixEngine::softClip (float* const* d, int numCh, int n, float threshold, float softness) noexcept
    {
        // linear below 60 % of the threshold, then a tanh knee that approaches (never exceeds) the threshold
        const float knee = std::clamp (softness, 0.05f, 0.95f) * threshold, range = threshold - knee;
        float deepest = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float x = d[ch][i], a = std::abs (x);
                if (a <= knee) continue;
                const float y = knee + range * std::tanh ((a - knee) / range);
                deepest = std::max (deepest, a / y);
                d[ch][i] = std::copysign (y, x);
            }
        return -gainToDb (std::max (1.0f, deepest));
    }

    void MixEngine::updateAutoGain (const EngineParams& p, int n) noexcept
    {
        // Stable auto-gain. The gain is set from the loudness of the LOUD sections of the track (the drops),
        // not from a running integrated average, so it does not creep down as more loud material plays and
        // does not jump up in breakdowns.
        //  - refLufs follows the input's short-term (3 s) loudness upward within ~1.5 s, then holds. It only
        //    sinks (0.05 dB/s) after 30 s without loud material, so a new, quieter song is still picked up.
        //  - lossDb makes up the loudness the clipper/limiter shave off. It only adapts while a loud section
        //    plays, compares short-term (not integrated) output loudness, is rate-limited and bounded to
        //    0..+8 dB: no wind-up, no slow downward drift.
        const float dt = (float) (n / sr);
        const float st = preLoud.getShortTerm();
        if (st > -45.0f)
        {
            if (refLufs < -90.0f)            refLufs = st;
            else if (st > refLufs)           refLufs += (st - refLufs) * (1.0f - std::exp (-dt / 1.5f));

            if (st > refLufs - 1.5f)         quietTime = 0.0f;
            else if ((quietTime += dt) > 30.0f) refLufs -= 0.05f * dt;

            // make-up only adapts at full AUTO GAIN, so turning the knob down never winds it up
            if (st > refLufs - 1.0f && p.autoGainMix > 0.999f)
            {
                const float outSt = postLoud.getShortTerm();
                if (outSt > -45.0f)
                    lossDb = std::clamp (lossDb + std::clamp (p.loudnessLufs - outSt, -3.0f, 3.0f) * 0.5f * dt, 0.0f, 8.0f);
            }
            const float target = std::clamp (p.loudnessLufs - refLufs + lossDb, -12.0f, 30.0f);
            autoGainCur += (target - autoGainCur) * (1.0f - std::exp (-dt / 0.5f));
        }
        autoGainDb = autoGainCur;
    }

    void MixEngine::outputStage (float* const* io, int numCh, int n, const EngineParams& p) noexcept
    {
        // ---- oversampling switch: fade out, swap path, fade in (constant latency, so no PDC change)
        const float fadeStep = (float) n / (float) (0.006 * sr);
        float fadeFrom = modeFade;
        if (p.oversample != activeOs)
        {
            modeFade -= fadeStep;
            if (modeFade <= 0.0f)
            {
                modeFade = fadeFrom = 0.0f;
                activeOs = p.oversample;
                oversampler->reset(); limiterBase.reset(); limiterOs.reset(); limiterOsCatch.reset(); dcBase.reset(); dcOs.reset();
                padDelayBase.reset(); padDelayOs.reset();
            }
        }
        else if (modeFade < 1.0f)
            modeFade = std::min (1.0f, modeFade + fadeStep);

        preLoud.process (io, numCh, n);
        // AUTO GAIN knob: how much of the learned gain is applied (0 % = none, the maximiser works on the raw level)
        const float agMix = std::clamp (p.autoGainMix, 0.0f, 1.0f);
        const float gainStart = dbToGain (autoGainDb * prevAgMix);
        updateAutoGain (p, n);
        const float gainEnd = dbToGain (autoGainDb * agMix);
        prevAgMix = agMix;
        const float ceiling = dbToGain (p.ceilingDb);
        const float clipThreshold = dbToGain (clipThresholdDb (p));
        const float sat = p.saturate * p.fix;

        if (activeOs)
        {
            juce::dsp::AudioBlock<float> block (io, (size_t) numCh, (size_t) n);
            auto up = oversampler->processSamplesUp (block);
            const int un = (int) up.getNumSamples();
            float* upPtrs[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
            saturate (upPtrs, numCh, un, sat, p.adv.satWarmth);
            for (int ch = 0; ch < numCh; ++ch)
                for (int i = 0; i < un; ++i) upPtrs[ch][i] = dcOs.process (ch, upPtrs[ch][i]);
            applyGainRamp (upPtrs, numCh, un, gainStart, gainEnd);
            clipDb = softClip (upPtrs, numCh, un, clipThreshold, p.adv.clipSoftness);   // alias-free at 4x
            limiterGrDb = limiterOs.process (upPtrs, numCh, un, ceiling * 0.989f);   // ~0.1 dB margin for the decimator
            oversampler->processSamplesDown (block);
            limiterGrDb = std::min (limiterGrDb, limiterOsCatch.process (io, numCh, n, ceiling));
            padDelayOs.processBlock (io, numCh, n);
        }
        else
        {
            saturate (io, numCh, n, sat, p.adv.satWarmth);
            for (int ch = 0; ch < numCh; ++ch)
                for (int i = 0; i < n; ++i) io[ch][i] = dcBase.process (ch, io[ch][i]);
            applyGainRamp (io, numCh, n, gainStart, gainEnd);
            clipDb = softClip (io, numCh, n, clipThreshold, p.adv.clipSoftness);
            limiterGrDb = limiterBase.process (io, numCh, n, ceiling);
            padDelayBase.processBlock (io, numCh, n);
        }

        postLoud.process (io, numCh, n);

        // ---- MIX (latency aligned dry), OUTPUT gain, click-free bypass
        mixAmt.setTargetValue (p.mix);
        outGain.setTargetValue (dbToGain (p.outputDb));
        bypassAmt.setTargetValue (p.bypass ? 1.0f : 0.0f);
        float fade = std::max (0.0f, fadeFrom);
        const float fadeInc = (std::max (0.0f, modeFade) - fade) / (float) n;

        for (int i = 0; i < n; ++i)
        {
            const float m = mixAmt.getNextValue();
            const float og = outGain.getNextValue();
            const float b = bypassAmt.getNextValue();
            fade += fadeInc;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float dry = dryPtrs[ch][i];
                const float wet = (dry * (1.0f - m) + io[ch][i] * fade * m) * og;
                io[ch][i] = wet + (dry - wet) * b;
            }
        }
    }
}

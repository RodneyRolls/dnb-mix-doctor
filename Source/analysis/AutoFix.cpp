#include "AutoFix.h"
#include "../Params.h"

namespace mcdsp
{
    void AutoFixAnalyser::prepare (double sampleRate)
    {
        sr = sampleRate;
        for (size_t b = 0; b < bands.size(); ++b)
            bands[b].setup (Svf::Mode::bandpass, sr, std::min (bandFreqs[b], sr * 0.45), 1.41);
        lowL.setup (Svf::Mode::lowpass, sr, 120.0, 0.707);  lowR = lowL;
        hpL.setup (Svf::Mode::highpass, sr, 300.0, 0.707);  hpR = hpL;
        kFast.setup (0.0005, 0.030, sr); kSlow.setup (0.030, 0.200, sr);
        oFast.setup (0.0002, 0.030, sr); oSlow.setup (0.020, 0.030, sr);
        loud.prepare (sr);
        state.store (idle);
    }

    void AutoFixAnalyser::resetMeasurements() noexcept
    {
        for (auto& b : bands) b.reset();
        for (auto* f : { &lowL, &lowR, &hpL, &hpR }) f->reset();
        for (auto* e : { &kFast, &kSlow, &oFast, &oSlow }) e->reset();
        bandE.fill (0);
        lowLR = lowLL = lowRR = midE = sideE = 0;
        sinceKick = sinceOnset = 1 << 30; kicks = onsets = 0;
        kickE = tailE = 0; kickN = tailN = 0;
        sumSq = 0; peak = 0;
        activeSamples = 0;
        loud.resetAll();
    }

    void AutoFixAnalyser::process (const float* const* d, int numCh, int n) noexcept
    {
        if (cancelRequested.exchange (false) && state.load() == listening)
            state.store (idle);

        if (startRequested.exchange (false))
        {
            resetMeasurements();
            targetSamples = std::max<long long> (1, (long long) (requestedSeconds.load() * sr));
            progress.store (0.0f);
            state.store (listening);
        }
        if (state.load (std::memory_order_relaxed) != listening)
            return;

        // only count blocks that actually carry signal, so silence before playback doesn't use up the window
        double blockE = 0;
        for (int ch = 0; ch < numCh; ++ch)
            for (int i = 0; i < n; ++i) blockE += (double) d[ch][i] * d[ch][i];
        if (blockE / (n * numCh) < 1.0e-8)   // below -80 dBFS
            return;

        loud.process (d, numCh, n);
        const int kickWin = (int) (0.030 * sr), tailStart = (int) (0.040 * sr), tailEnd = (int) (0.150 * sr);

        for (int i = 0; i < n; ++i)
        {
            const float L = d[0][i], R = numCh > 1 ? d[1][i] : L;
            const float m = 0.5f * (L + R);

            for (size_t b = 0; b < bands.size(); ++b)
            {
                const float y = bands[b].process (m);
                bandE[b] += (double) y * y;
            }

            const float lL = lowL.process (L), lR = lowR.process (R);
            lowLR += (double) lL * lR; lowLL += (double) lL * lL; lowRR += (double) lR * lR;

            const float hL = hpL.process (L), hR = hpR.process (R);
            const float hm = 0.5f * (hL + hR), hs = 0.5f * (hL - hR);
            midE += (double) hm * hm; sideE += (double) hs * hs;

            // kick onsets + how much low end is still ringing 40-150 ms later
            const float lm = 0.5f * (lL + lR), alm = std::abs (lm);
            const float kf = kFast.process (alm), ks = kSlow.process (alm);
            if (sinceKick > (int) (0.09 * sr) && kf > 0.02f && kf > ks * 1.8f) { sinceKick = 0; ++kicks; }
            else if (sinceKick < (1 << 30)) ++sinceKick;
            if (sinceKick < kickWin)                              { kickE += (double) lm * lm; ++kickN; }
            else if (sinceKick >= tailStart && sinceKick < tailEnd) { tailE += (double) lm * lm; ++tailN; }

            // broadband transient onsets
            const float am = std::abs (m);
            const float of = oFast.process (am), os = oSlow.process (am);
            if (sinceOnset > (int) (0.05 * sr) && of > 0.01f && of > os * 2.0f) { sinceOnset = 0; ++onsets; }
            else if (sinceOnset < (1 << 30)) ++sinceOnset;

            sumSq += (double) m * m;
            peak = std::max (peak, std::max (std::abs (L), std::abs (R)));
        }

        activeSamples += n;
        progress.store ((float) std::min (1.0, (double) activeSamples / (double) targetSamples));
        if (activeSamples >= targetSamples)
            finish();
    }

    void AutoFixAnalyser::finish() noexcept
    {
        Result r;
        double total = 0;
        for (auto e : bandE) total += e;
        for (size_t b = 0; b < bandE.size(); ++b)
            r.bandDb[b] = total > 0 ? powerToDb ((float) (bandE[b] / total)) : -30.0f;

        r.lowCorrelation = (lowLL > 1e-9 && lowRR > 1e-9) ? (float) (lowLR / std::sqrt (lowLL * lowRR)) : 1.0f;
        r.sideToMidDb = midE > 1e-12 ? powerToDb ((float) (sideE / midE)) : -40.0f;
        const double kAvg = kickN > 0 ? kickE / (double) kickN : 0.0, tAvg = tailN > 0 ? tailE / (double) tailN : 0.0;
        r.kickOverlap = kAvg > 1e-12 ? (float) (tAvg / kAvg) : 0.0f;
        const double seconds = (double) activeSamples / sr;
        r.kicksPerSecond = (float) (kicks / seconds);
        r.onsetsPerSecond = (float) (onsets / seconds);
        const double rms = std::sqrt (sumSq / std::max<long long> (1, activeSamples));
        r.crestDb = rms > 1e-9 ? gainToDb (peak) - gainToDb ((float) rms) : 12.0f;
        r.lufs = loud.getIntegrated();

        result = r;
        progress.store (1.0f);
        state.store (done, std::memory_order_release);
    }

    //==============================================================================
    std::array<float, AutoFixAnalyser::numBands> dnbReferenceCurve()
    {
        // Engineering estimate of a well-balanced modern DnB master (energy share per octave band).
        // Heavy sub/low end, steady downward tilt through the mids, controlled top.
        std::array<float, AutoFixAnalyser::numBands> ref { -9.5f, -6.0f, -8.0f, -11.5f, -14.0f, -16.0f, -18.0f, -20.0f, -23.0f, -29.0f };
        double total = 0;
        for (auto v : ref) total += std::pow (10.0, v / 10.0);
        const float norm = (float) (10.0 * std::log10 (total));
        for (auto& v : ref) v -= norm;
        return ref;
    }

    std::vector<std::pair<juce::String, float>> suggestSettings (const AutoFixAnalyser::Result& r, double bpm)
    {
        const auto ref = dnbReferenceCurve();
        std::array<float, AutoFixAnalyser::numBands> dev {};
        for (size_t b = 0; b < dev.size(); ++b) dev[b] = std::clamp (r.bandDb[b] - ref[b], -12.0f, 12.0f);

        auto lim = [] (float v, float lo, float hi) { return std::clamp (v, lo, hi); };
        const float crest = r.crestDb;

        const float subExcess  = 0.5f * (dev[0] + dev[1]);
        const float mudExcess  = 0.5f * (dev[3] + dev[4]);
        const float reeseExc   = std::max (dev[5], dev[6]);
        const float harshExc   = 0.5f * (dev[7] + dev[8]);
        const float airDeficit = -dev[9];

        const float punch = lim (20.0f + (12.0f - crest) * 6.0f, 5.0f, 65.0f);
        const float beatMs = (float) (60000.0 / juce::jlimit (60.0, 240.0, bpm));

        std::vector<std::pair<juce::String, float>> s;
        s.push_back ({ params::subMono,  r.lowCorrelation < 0.95f ? lim (120.0f + (0.95f - r.lowCorrelation) * 200.0f, 100.0f, 180.0f) : 100.0f });
        s.push_back ({ params::subTight, lim (25.0f + subExcess * 8.0f, 10.0f, 70.0f) });
        s.push_back ({ params::kickBass, lim (20.0f + (r.kickOverlap - 0.25f) * 120.0f, 5.0f, 70.0f) });
        s.push_back ({ params::punch,    punch });
        s.push_back ({ params::snap,     lim (20.0f - dev[5] * 5.0f + (12.0f - crest) * 3.0f, 5.0f, 60.0f) });
        s.push_back ({ params::mud,      lim (20.0f + mudExcess * 10.0f, 5.0f, 75.0f) });
        s.push_back ({ params::reese,    lim (15.0f + reeseExc * 8.0f, 5.0f, 70.0f) });
        s.push_back ({ params::harsh,    lim (20.0f + harshExc * 9.0f, 5.0f, 75.0f) });
        s.push_back ({ params::air,      lim (20.0f + airDeficit * 7.0f, 0.0f, 70.0f) });
        s.push_back ({ params::glue,     lim (20.0f + (crest - 10.0f) * 4.0f, 10.0f, 60.0f) });
        s.push_back ({ params::attack,   punch > 35.0f ? 15.0f : 8.0f });
        s.push_back ({ params::release,  lim (beatMs * 0.5f, 50.0f, 400.0f) });   // 1/8 note
        s.push_back ({ params::width,    r.sideToMidDb < -16.0f ? 120.0f : (r.sideToMidDb > -6.0f ? 90.0f : 105.0f) });
        s.push_back ({ params::saturate, lim (15.0f + std::max (0.0f, airDeficit) * 3.0f, 5.0f, 45.0f) });
        s.push_back ({ params::loudness, r.lufs > -7.0f ? -5.0f : -6.5f });
        s.push_back ({ params::ceiling,  -0.5f });
        s.push_back ({ params::fix,      100.0f });
        s.push_back ({ params::mix,      100.0f });
        return s;
    }
}

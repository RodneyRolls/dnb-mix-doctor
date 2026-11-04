#pragma once
// ============================================================================
//  MixDoctorDSP.h  -  JUCE-free analysis + correction core for "DnB Mix Doctor"
//
//  Signal flow (stereo, in place):
//    in -> [DC block + 25 Hz 4th-order HP] -> [LR4 split: low band -> mono, recombine]
//       -> [8-band auto tonal EQ] -> [dynamic harshness bell @ ~4.2 kHz]
//       -> [look-ahead limiter with 32-tap sinc true-peak detection] -> out
//
//  Analysis is taken from the *input* (open loop), so the correctors cannot
//  chase their own output. Everything is allocation-free after prepare().
// ============================================================================
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

namespace mixdoctor
{
constexpr int    kNumBands = 8;
constexpr int    kChunk    = 32;     // control-rate block (samples)
constexpr int    kTp       = 16;     // true-peak interpolator: taps each side
constexpr int    kHist     = 2 * kTp + 1;
constexpr double kPi       = 3.14159265358979323846;

inline double dbToGain (double db) { return std::pow (10.0, db * 0.05); }
inline double powToDb  (double p)  { return 10.0 * std::log10 (std::max (p, 1.0e-20)); }

// ---------------------------------------------------------------------------
//  Band layout used for tonal analysis (all measured on the mid signal)
// ---------------------------------------------------------------------------
constexpr double kBandLo[kNumBands] = { 30, 60, 120, 250, 500, 2000, 5000, 10000 };
constexpr double kBandHi[kNumBands] = { 60, 120, 250, 500, 2000, 5000, 10000, 18000 };
inline const char* bandName (int i)
{
    static const char* n[kNumBands] = { "Sub", "Bass", "Low-mid", "Mid", "Upper-mid", "Presence", "Highs", "Air" };
    return n[i];
}

// Starting-point DnB targets: average energy *per octave*, dB, relative to Sub.
// These are engineering estimates - use "Capture target" with a reference you
// love to replace them with real measurements.
inline void getStyleTarget (int style, float* out)
{
    static const float base[kNumBands] = { 0.f, -1.5f, -6.f, -11.f, -16.f, -21.f, -26.f, -33.f };
    static const float off[3][kNumBands] = {
        {  1.0f,  1.0f,  0.5f,  1.5f,  2.0f,  1.0f, -1.0f, -2.0f },   // 0 Neurofunk: heavy, growly mids, darker top
        { -1.5f, -1.0f,  0.0f,  0.0f,  0.5f,  0.5f,  1.0f,  2.0f },   // 1 Liquid: lighter sub, silkier top
        {  0.0f,  0.5f, -0.5f,  0.0f,  0.5f,  1.5f,  1.5f,  1.0f }    // 2 Dancefloor / jump-up: bright, punchy
    };
    const int s = std::clamp (style, 0, 2);
    for (int i = 0; i < kNumBands; ++i) out[i] = base[i] + off[s][i];
}

// ---------------------------------------------------------------------------
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }

    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) noexcept
    {
        const double inv = 1.0 / a0;
        b0 = nb0 * inv; b1 = nb1 * inv; b2 = nb2 * inv; a1 = na1 * inv; a2 = na2 * inv;
    }
    static double cf (double fs, double f) { return std::clamp (f, 1.0, fs * 0.45); }

    void makeLowpass (double fs, double f, double q)
    {
        f = cf (fs, f); const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) * 0.5, 1 - c, (1 - c) * 0.5, 1 + al, -2 * c, 1 - al);
    }
    void makeHighpass (double fs, double f, double q)
    {
        f = cf (fs, f); const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) * 0.5, -(1 + c), (1 + c) * 0.5, 1 + al, -2 * c, 1 - al);
    }
    void makeBandpass (double fs, double f, double q)   // 0 dB peak gain
    {
        f = cf (fs, f); const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void makePeak (double fs, double f, double q, double gainDb)
    {
        f = cf (fs, f); const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    double magnitude (double fs, double f) const
    {
        const double w = 2 * kPi * f / fs;
        const std::complex<double> e1 (std::cos (-w), std::sin (-w)), e2 (std::cos (-2 * w), std::sin (-2 * w));
        return std::abs (b0 + b1 * e1 + b2 * e2) / std::abs (1.0 + a1 * e1 + a2 * e2);
    }
};

struct DcBlock
{
    double x1 = 0, y1 = 0, R = 0.999;
    void setup (double fs, double fc) { R = std::exp (-2 * kPi * fc / fs); }
    inline double process (double x) noexcept { const double y = x - x1 + R * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0; }
};

// ---------------------------------------------------------------------------
struct Params
{
    bool  cleanLow = true, monoLow = true, autoTone = true, harsh = true, limiter = true;
    float monoFreq = 120.f;     // Hz
    float monoAmount = 1.f;     // 0..1
    float toneAmount = 0.5f;    // 0..1
    float toneMaxDb = 3.f;      // max +/- correction per band
    float harshMaxDb = 4.f;     // max gain reduction of the harshness bell
    float ceilingDb = -0.3f;    // limiter ceiling (true-peak estimate)
    std::array<float, kNumBands> target {};   // per-octave target levels (dB, any offset - mean is removed)
};

struct Meters   // written by the audio thread, read by the UI (relaxed atomics)
{
    std::atomic<float> bandRel[kNumBands], bandCorr[kNumBands], targetRel[kNumBands];
    std::atomic<float> dcOffset { 0.f }, rumbleRel { -90.f }, lowSideDb { -90.f }, harshGR { 0.f },
                       limGR { 0.f }, peakOverDb { -90.f }, correlation { 1.f }, rmsDb { -90.f },
                       peakDb { -90.f }, ready { 0.f }, active { 0.f };
    Meters() { for (int i = 0; i < kNumBands; ++i) { bandRel[i] = 0.f; bandCorr[i] = 0.f; targetRel[i] = 0.f; } }
};

// ---------------------------------------------------------------------------
class MixDoctor
{
public:
    Meters meters;

    void prepare (double sampleRate)
    {
        fs = sampleRate;

        for (int b = 0; b < kNumBands; ++b)
        {
            const double fc = std::sqrt (kBandLo[b] * kBandHi[b]);
            const double q  = fc / (kBandHi[b] - kBandLo[b]);
            bp1[b].makeBandpass (fs, fc, q);
            bp2[b] = bp1[b];

            // effective bandwidth (octaves) of the cascaded band-pass, so that
            // pink noise reads flat across bands
            double acc = 0; const int steps = 4000; const double lo = std::log2 (10.0), hi = std::log2 (std::min (20000.0, fs * 0.45));
            for (int s = 0; s < steps; ++s)
            {
                const double f = std::pow (2.0, lo + (hi - lo) * (s + 0.5) / steps);
                const double m = bp1[b].magnitude (fs, f);
                acc += m * m * m * m;                         // two cascaded stages, power
            }
            beff[b] = acc * (hi - lo) / steps;
        }

        hpA.makeHighpass (fs, 25.0, 0.5412);  hpB.makeHighpass (fs, 25.0, 1.3066);
        rumLp1.makeLowpass (fs, 22.0, 0.70710678); rumLp2 = rumLp1;
        dcb.setup (fs, 5.0);
        harshBp.makeBandpass (fs, std::sqrt (2500.0 * 7000.0), std::sqrt (2500.0 * 7000.0) / 4500.0);

        lookahead = std::max (8, (int) std::lround (0.0015 * fs));
        delayLen  = lookahead - 1 + kTp;   // detector sees kTp samples beyond the sample it protects
        for (int q = 0; q < 3; ++q)        // sinc weights for fractions 1/4, 1/2, 3/4
        {
            const double f = 0.25 * (q + 1); double sum = 0;
            for (int j = 0; j < 2 * kTp; ++j)
            {
                const double x = (j - (kTp - 1)) - f;
                const double sinc = std::abs (x) < 1e-12 ? 1.0 : std::sin (kPi * x) / (kPi * x);
                const double win  = 0.5 + 0.5 * std::cos (kPi * x / kTp);
                tpW[q][j] = sinc * win; sum += tpW[q][j];
            }
            for (int j = 0; j < 2 * kTp; ++j) tpW[q][j] /= sum;
        }
        tRing.assign ((size_t) lookahead, 1.0); mRing.assign ((size_t) lookahead, 1.0);
        dlyL.assign ((size_t) delayLen + 1, 0.0); dlyR.assign ((size_t) delayLen + 1, 0.0);

        kSlow  = k (2.5, kChunk);   kSlowSample = k (1.0, 1);
        kCorrSm = k (0.4, kChunk);  kMix = k (0.01, 1);
        kHAtt = k (0.010, kChunk);  kHRel = k (0.120, kChunk);
        kHFast = k (0.025, 1);      kHSlow = k (5.0, kChunk);
        kLimRel = k (0.080, 1);
        reset();
    }

    void reset()
    {
        for (auto& f : bp1) f.reset();
        for (auto& f : bp2) f.reset();
        for (int c = 0; c < 2; ++c)
        {
            dcC[c].reset(); hp1[c].reset(); hp2[c].reset(); lp1[c].reset(); lp2[c].reset();
            for (auto& e : eq[c]) e.reset();
            harshEq[c].reset();
            for (auto& h : hist[c]) h = 0.0;
            hq1[c].reset(); hq2[c].reset();
        }
        hpA.reset(); hpB.reset(); rumLp1.reset(); rumLp2.reset(); dcb.reset(); harshBp.reset();
        for (int c = 0; c < 2; ++c) { hp1[c] = hpA; hp2[c] = hpB; }
        std::fill (tRing.begin(), tRing.end(), 1.0); std::fill (mRing.begin(), mRing.end(), 1.0);
        std::fill (dlyL.begin(), dlyL.end(), 0.0);   std::fill (dlyR.begin(), dlyR.end(), 0.0);
        tw = mw = dw = 0; gain = 1.0;
        for (int b = 0; b < kNumBands; ++b) { ms[b] = 0; corrSm[b] = 0; corrApplied[b] = 1e9; }
        rumbleMs = lowSideMs = lowMidMs = harshSlow = 0; harshFast = 0; harshGr = 0; harshApplied = 1e9;
        gatedChunks = 0; cleanMix = 0; monoEn = 0; monoKeep = 0; dcMean[0] = dcMean[1] = 0; pLL = pRR = pLR = 0;
        peakHold = 0; limHold = overHold = 0;
    }

    int latencySamples() const { return delayLen; }

    void process (float* left, float* right, int numSamples, const Params& p)
    {
        const double ceilLin = dbToGain (p.ceilingDb);
        const double lpF = std::clamp ((double) p.monoFreq, 40.0, 300.0);
        if (std::abs (lpF - lpApplied) > 0.01)
        {
            Biquad t, u; t.makeLowpass (fs, lpF, 0.70710678); u.makeHighpass (fs, lpF, 0.70710678);   // 2 x Butterworth = Linkwitz-Riley 4
            for (int c = 0; c < 2; ++c) { copyCoeffs (lp1[c], t); copyCoeffs (lp2[c], t); copyCoeffs (hq1[c], u); copyCoeffs (hq2[c], u); }
            lpApplied = lpF;
        }

        int off = 0;
        while (off < numSamples)
        {
            const int n = std::min (kChunk, numSamples - off);

            // ---- gate: is there signal in this chunk? -------------------------------
            double chunkMid = 0;
            for (int i = 0; i < n; ++i) { const double m = 0.5 * ((double) left[off + i] + right[off + i]); chunkMid += m * m; }
            chunkMid /= n;
            const bool gateOpen = chunkMid > 1.0e-7;                       // ~ -70 dBFS
            meters.active.store (gateOpen ? 1.f : 0.f, std::memory_order_relaxed);

            // ---- control: tonal EQ ----------------------------------------------------
            const bool ready = gatedChunks * kChunk > (int) fs;            // ~1 s of signal analysed
            double lvl[kNumBands], tgt[kNumBands]; double lm = 0, tm = 0;
            for (int b = 0; b < kNumBands; ++b) { lvl[b] = powToDb (ms[b] / beff[b]); tgt[b] = p.target[(size_t) b]; lm += lvl[b]; tm += tgt[b]; }
            lm /= kNumBands; tm /= kNumBands;
            for (int b = 0; b < kNumBands; ++b)
            {
                const double rel  = lvl[b] - lm, trel = tgt[b] - tm;
                double want = 0;
                if (p.autoTone && ready)
                    want = std::clamp ((trel - rel) * 0.5 * (double) p.toneAmount, -(double) p.toneMaxDb, (double) p.toneMaxDb);
                corrSm[b] += kCorrSm * (want - corrSm[b]);
                if (std::abs (corrSm[b] - corrApplied[b]) > 0.005)
                {
                    const double fc = std::sqrt (kBandLo[b] * kBandHi[b]);
                    eq[0][b].makePeak (fs, fc, b == 4 ? 0.6 : 1.0, corrSm[b]);
                    copyCoeffs (eq[1][b], eq[0][b]);
                    corrApplied[b] = corrSm[b];
                }
                meters.bandRel[b].store ((float) rel, std::memory_order_relaxed);
                meters.targetRel[b].store ((float) trel, std::memory_order_relaxed);
                meters.bandCorr[b].store ((float) corrSm[b], std::memory_order_relaxed);
            }

            // ---- control: harshness bell ---------------------------------------------
            {
                double want = 0;
                if (p.harsh && gatedChunks * kChunk > 2 * (int) fs && harshSlow > 1.0e-14)
                {
                    const double excess = powToDb (harshFast / harshSlow);
                    want = std::clamp ((excess - 3.0) * 0.8, 0.0, (double) p.harshMaxDb);
                }
                harshGr += (want > harshGr ? kHAtt : kHRel) * (want - harshGr);
                if (std::abs (harshGr - harshApplied) > 0.01)
                {
                    harshEq[0].makePeak (fs, std::sqrt (2500.0 * 7000.0), 1.0, -harshGr);
                    copyCoeffs (harshEq[1], harshEq[0]);
                    harshApplied = harshGr;
                }
                meters.harshGR.store ((float) harshGr, std::memory_order_relaxed);
            }

            const double cleanTarget = p.cleanLow ? 1.0 : 0.0;
            const double monoEnTarget = p.monoLow ? 1.0 : 0.0, monoKeepTarget = 1.0 - std::clamp ((double) p.monoAmount, 0.0, 1.0);

            // ---- per-sample --------------------------------------------------------------
            double acc[kNumBands] = {}; double accRum = 0, accHarsh = 0, accSide = 0, accLowMid = 0;
            double chunkPeak = 0, chunkGr = 0, chunkOver = -90.0;

            for (int i = 0; i < n; ++i)
            {
                const double inL = left[off + i], inR = right[off + i];
                const double mid = 0.5 * (inL + inR);

                // analysis (from input)
                for (int b = 0; b < kNumBands; ++b) { const double y = bp2[b].process (bp1[b].process (mid)); acc[b] += y * y; }
                { const double y = rumLp2.process (rumLp1.process (dcb.process (mid))); accRum += y * y; }
                { const double y = harshBp.process (mid); const double y2 = y * y; harshFast += kHFast * (y2 - harshFast); accHarsh += y2; }
                dcMean[0] += kSlowSample * (inL - dcMean[0]); dcMean[1] += kSlowSample * (inR - dcMean[1]);
                pLL += kSlowSample * (inL * inL - pLL); pRR += kSlowSample * (inR * inR - pRR); pLR += kSlowSample * (inL * inR - pLR);

                // 1) DC + subsonic
                double in[2] = { inL, inR }, x[2];
                cleanMix += kMix * (cleanTarget - cleanMix);
                for (int c = 0; c < 2; ++c)
                {
                    const double cl = hp2[c].process (hp1[c].process (dcC[c].process (in[c])));
                    x[c] = in[c] + cleanMix * (cl - in[c]);
                }
                // 2) mono below X Hz: Linkwitz-Riley split, collapse the low band's side, recombine.
                //    (LP + HP of an LR4 pair sum to an all-pass, so nothing is comb-filtered.)
                const double lowL = lp2[0].process (lp1[0].process (x[0])), highL = hq2[0].process (hq1[0].process (x[0]));
                const double lowR = lp2[1].process (lp1[1].process (x[1])), highR = hq2[1].process (hq1[1].process (x[1]));
                const double side = 0.5 * (lowL - lowR), lowMid = 0.5 * (lowL + lowR);
                accSide += side * side; accLowMid += lowMid * lowMid;
                monoEn   += kMix * (monoEnTarget - monoEn);
                monoKeep += kMix * (monoKeepTarget - monoKeep);
                x[0] += monoEn * ((highL + lowMid + monoKeep * side) - x[0]);
                x[1] += monoEn * ((highR + lowMid - monoKeep * side) - x[1]);
                // 3) auto tonal EQ + 4) harshness bell
                for (int b = 0; b < kNumBands; ++b) { x[0] = eq[0][b].process (x[0]); x[1] = eq[1][b].process (x[1]); }
                x[0] = harshEq[0].process (x[0]); x[1] = harshEq[1].process (x[1]);

                // 5) limiter -------------------------------------------------------------
                for (int c = 0; c < 2; ++c) { std::memmove (hist[c], hist[c] + 1, (kHist - 1) * sizeof (double)); hist[c][kHist - 1] = x[c]; }
                dlyL[(size_t) dw] = x[0]; dlyR[(size_t) dw] = x[1];
                const size_t rd = (size_t) ((dw + (int) dlyL.size() - delayLen) % (int) dlyL.size());
                dw = (dw + 1) % (int) dlyL.size();

                double pk = 0;                                              // true-peak estimate around centre sample hist[kTp]
                for (int c = 0; c < 2; ++c)
                {
                    const double* h = hist[c];
                    pk = std::max (pk, std::abs (h[kTp]));
                    bool near = false; for (int j = 0; j < kHist; ++j) near = near || std::abs (h[j]) > 0.4 * ceilLin;
                    if (near)
                        for (int q = 0; q < 3; ++q)
                        {
                            double a = 0, b = 0;                           // intervals (centre, centre+1) and (centre-1, centre)
                            for (int j = 0; j < 2 * kTp; ++j) { a += tpW[q][j] * h[kTp - (kTp - 1) + j]; b += tpW[q][j] * h[kTp - 1 - (kTp - 1) + j]; }
                            pk = std::max (pk, std::max (std::abs (a), std::abs (b)));
                        }
                }
                chunkPeak = std::max (chunkPeak, pk);
                if (pk > ceilLin) chunkOver = std::max (chunkOver, 20.0 * std::log10 (pk / ceilLin));

                const double need = (p.limiter && pk > ceilLin) ? ceilLin / pk : 1.0;
                tRing[(size_t) tw] = need; tw = (tw + 1) % lookahead;
                double mn = 1.0; for (double v : tRing) mn = std::min (mn, v);
                mRing[(size_t) mw] = mn; mw = (mw + 1) % lookahead;
                double s = 0; for (double v : mRing) s += v; s /= lookahead;
                if (s < gain) gain = s; else gain += (s - gain) * kLimRel;
                chunkGr = std::max (chunkGr, -20.0 * std::log10 (std::max (gain, 1.0e-6)));

                const double oL = dlyL[rd] * gain, oR = dlyR[rd] * gain;
                // the clamp is only a last-resort safety net; the look-ahead gain already guarantees <= ceiling
                left[off + i]  = (float) (p.limiter ? std::clamp (oL, -ceilLin, ceilLin) : oL);
                right[off + i] = (float) (p.limiter ? std::clamp (oR, -ceilLin, ceilLin) : oR);
            }

            // ---- end of chunk: update slow statistics (gated) ------------------------------------
            if (gateOpen)
            {
                const double kk = std::max (kSlow, 1.0 / (gatedChunks + 1));
                for (int b = 0; b < kNumBands; ++b) ms[b] += kk * (acc[b] / n - ms[b]);
                rumbleMs  += kk * (accRum / n - rumbleMs);
                lowSideMs += kk * (accSide / n - lowSideMs);
                lowMidMs  += kk * (accLowMid / n - lowMidMs);
                harshSlow += std::max (kHSlow, 1.0 / (gatedChunks + 1)) * (accHarsh / n - harshSlow);
                if (gatedChunks < 1000000) ++gatedChunks;

                meters.rumbleRel.store ((float) (powToDb (rumbleMs) - powToDb (ms[0])), std::memory_order_relaxed);
                meters.lowSideDb.store ((float) (powToDb (lowSideMs) - powToDb (lowMidMs)), std::memory_order_relaxed);
                const double den = std::sqrt (std::max (pLL * pRR, 1.0e-20));
                meters.correlation.store ((float) std::clamp (pLR / den, -1.0, 1.0), std::memory_order_relaxed);
                meters.rmsDb.store ((float) powToDb (0.5 * (pLL + pRR)), std::memory_order_relaxed);
            }
            meters.dcOffset.store ((float) std::max (std::abs (dcMean[0]), std::abs (dcMean[1])), std::memory_order_relaxed);
            meters.ready.store (ready ? 1.f : 0.f, std::memory_order_relaxed);

            const double dec = 12.0 * n / fs;                               // 12 dB/s meter fall
            limHold  = std::max (chunkGr, limHold - dec);
            overHold = std::max (chunkOver, overHold - dec);
            peakHold = std::max (chunkPeak, peakHold * std::exp (-(double) n / (3.0 * fs)));
            meters.limGR.store ((float) limHold, std::memory_order_relaxed);
            meters.peakOverDb.store ((float) overHold, std::memory_order_relaxed);
            meters.peakDb.store ((float) (20.0 * std::log10 (std::max (peakHold, 1.0e-6))), std::memory_order_relaxed);

            off += n;
        }
    }

private:
    static void copyCoeffs (Biquad& d, const Biquad& s) { d.b0 = s.b0; d.b1 = s.b1; d.b2 = s.b2; d.a1 = s.a1; d.a2 = s.a2; }
    static double catmull (double p0, double p1, double p2, double p3, double t)
    {
        return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
    }
    double k (double tau, int step) const { return 1.0 - std::exp (-(double) step / (tau * fs)); }

    double fs = 48000.0;
    Biquad bp1[kNumBands], bp2[kNumBands];
    double beff[kNumBands] = {}, ms[kNumBands] = {}, corrSm[kNumBands] = {}, corrApplied[kNumBands] = {};
    Biquad hpA, hpB, hp1[2], hp2[2], lp1[2], lp2[2], hq1[2], hq2[2], rumLp1, rumLp2, harshBp, eq[2][kNumBands], harshEq[2];
    DcBlock dcC[2], dcb;
    double lpApplied = -1;

    double rumbleMs = 0, lowSideMs = 0, lowMidMs = 0, harshSlow = 0, harshFast = 0, harshGr = 0, harshApplied = 1e9;
    double dcMean[2] = {}, pLL = 0, pRR = 0, pLR = 0, cleanMix = 0, monoEn = 0, monoKeep = 0;
    int gatedChunks = 0;
    double kSlow = 0, kSlowSample = 0, kCorrSm = 0, kMix = 0, kHAtt = 0, kHRel = 0, kHFast = 0, kHSlow = 0, kLimRel = 0;

    int lookahead = 72, delayLen = 73, tw = 0, mw = 0, dw = 0;
    std::vector<double> tRing, mRing, dlyL, dlyR;
    double hist[2][kHist] = {};
    double tpW[3][2 * kTp] = {};
    double gain = 1.0, peakHold = 0, limHold = 0, overHold = 0;
};

} // namespace mixdoctor

// Native test-bench for the JUCE-free DSP core.   g++ -O2 -std=c++17 -I Source tests/test_dsp.cpp -o test_dsp
#include "MixDoctorDSP.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <random>

using namespace mixdoctor;
static const double FS = 48000.0;
static int failures = 0;
#define CHECK(cond, ...) do { const bool ok_ = (cond); std::printf("  [%s] ", ok_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); if (! ok_) ++failures; } while (0)

using Sig = std::vector<float>;
static std::mt19937 rng (12345);
static double white() { static std::normal_distribution<double> d (0.0, 1.0); return d (rng); }

static Sig pink (int n, double rms)
{   // Paul Kellet's economy pink filter
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0; Sig s ((size_t) n); double e = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = white();
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        const double y = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362; b6 = w * 0.115926;
        s[(size_t) i] = (float) y; e += y * y;
    }
    const double g = rms / std::sqrt (e / n); for (auto& v : s) v *= (float) g; return s;
}
static Sig tone (int n, double f, double amp, double phase = 0)
{ Sig s ((size_t) n); for (int i = 0; i < n; ++i) s[(size_t) i] = (float) (amp * std::sin (2 * kPi * f * i / FS + phase)); return s; }
static Sig add (Sig a, const Sig& b) { for (size_t i = 0; i < a.size(); ++i) a[i] += b[i]; return a; }
static Sig scaled (Sig a, double g) { for (auto& v : a) v *= (float) g; return a; }
static Sig neg (Sig a) { return scaled (std::move (a), -1.0); }
static Sig hpNoise (int n, double f, double amp)
{ Biquad a, b; a.makeHighpass (FS, f, 0.5412); b.makeHighpass (FS, f, 1.3066); Sig s ((size_t) n); for (auto& v : s) v = (float) (amp * b.process (a.process (white()))); return s; }
static Sig bandNoise (int n, double lo, double hi, double amp)
{ Biquad a, b; const double fc = std::sqrt (lo * hi); a.makeBandpass (FS, fc, fc / (hi - lo)); b = a; Sig s ((size_t) n); for (auto& v : s) v = (float) (amp * b.process (a.process (white()))); return s; }

static double rmsOf (const Sig& s, int from, int to)
{ double e = 0; for (int i = from; i < to; ++i) e += (double) s[(size_t) i] * s[(size_t) i]; return std::sqrt (e / std::max (1, to - from)); }
static double db (double x) { return 20 * std::log10 (std::max (x, 1e-12)); }

static Params allOff()
{ Params p; p.cleanLow = p.monoLow = p.autoTone = p.harsh = p.limiter = false; getStyleTarget (0, p.target.data()); return p; }

// run through the processor in odd-sized blocks (to exercise chunk handling)
static void run (MixDoctor& d, const Params& p, Sig& L, Sig& R, std::function<void (int)> perBlock = {})
{
    const int N = (int) L.size(), B = 477;
    for (int o = 0; o < N; o += B) { const int n = std::min (B, N - o); d.process (L.data() + o, R.data() + o, n, p); if (perBlock) perBlock (o + n); }
}
static double truePeak (const Sig& s)   // 8x windowed-sinc oversampling, independent of the limiter's estimator
{
    double pk = 0; const int T = 32, OS = 8;
    for (int i = T; i < (int) s.size() - T; ++i)
        for (int k = 0; k < OS; ++k)
        {
            double acc = 0; const double fr = (double) k / OS;
            for (int t = -T + 1; t <= T; ++t)
            {
                const double x = t - fr; const double w = 0.5 + 0.5 * std::cos (kPi * x / (T + 1));
                const double sinc = std::abs (x) < 1e-9 ? 1.0 : std::sin (kPi * x) / (kPi * x);
                acc += s[(size_t) (i + t)] * sinc * w;
            }
            pk = std::max (pk, std::abs (acc));
        }
    return pk;
}

int main()
{
    const int N = (int) (FS * 8);
    std::printf("\n== 1. transparent when everything is off (latency-compensated bit match)\n");
    {
        MixDoctor d; d.prepare (FS); Sig L = pink (N, 0.1), R = pink (N, 0.1), L0 = L, R0 = R;
        run (d, allOff(), L, R); const int lat = d.latencySamples(); double err = 0;
        for (int i = lat; i < N; ++i) err = std::max (err, (double) std::abs (L[(size_t) i] - L0[(size_t) (i - lat)]));
        CHECK (err < 1e-5, "max error vs delayed input = %.2e  (latency %d samples = %.2f ms)", err, lat, 1000.0 * lat / FS);
    }

    std::printf("\n== 2. DC / subsonic clean-up\n");
    {
        Params p = allOff(); p.cleanLow = true; const int n = (int) (FS * 3);
        auto test = [&] (Sig in, int ch) { MixDoctor d; d.prepare (FS); Sig L = in, R = in; run (d, p, L, R); return rmsOf (ch ? R : L, n / 2, n) / rmsOf (in, n / 2, n); };
        const double r10 = test (tone (n, 10, 0.5), 0), r60 = test (tone (n, 60, 0.5), 0), r35 = test (tone (n, 35, 0.5), 0);
        CHECK (db (r10) < -20.0, "10 Hz rumble attenuated by %.1f dB", -db (r10));
        CHECK (std::abs (db (r60)) < 0.5, "60 Hz sub passes (%.2f dB)", db (r60));
        CHECK (db (r35) > -1.5, "35 Hz sub passes (%.2f dB)", db (r35));
        MixDoctor d; d.prepare (FS); Sig L = add (tone (n, 100, 0.3), Sig ((size_t) n, 0.05f)), R = L; run (d, p, L, R);
        double mean = 0; for (int i = n / 2; i < n; ++i) mean += L[(size_t) i]; mean /= (n - n / 2);
        CHECK (std::abs (mean) < 0.002, "0.05 DC offset removed -> residual mean %.5f", mean);
    }

    std::printf("\n== 3. mono-below-X-Hz\n");
    {
        Params p = allOff(); p.monoLow = true; p.monoFreq = 120; const int n = (int) (FS * 2);
        auto sideRatio = [&] (double f) {
            MixDoctor d; d.prepare (FS); Sig a = tone (n, f, 0.4), L = a, R = neg (a); run (d, p, L, R);
            Sig S ((size_t) n); for (int i = 0; i < n; ++i) S[(size_t) i] = 0.5f * (L[(size_t) i] - R[(size_t) i]);
            return db (rmsOf (S, n / 2, n) / rmsOf (a, n / 2, n)); };
        const double s45 = sideRatio (45), s60 = sideRatio (60), s5k = sideRatio (5000), s400 = sideRatio (400);
        CHECK (s45 < -30.0, "45 Hz out-of-phase side content reduced by %.1f dB", -s45);
        CHECK (s60 < -20.0, "60 Hz out-of-phase side content reduced by %.1f dB", -s60);
        CHECK (std::abs (s5k) < 0.2, "5 kHz stereo untouched (%.3f dB)", s5k);
        CHECK (std::abs (s400) < 1.0, "400 Hz stereo essentially untouched (%.2f dB)", s400);
    }

    std::printf("\n== 4. limiter: sample peak AND inter-sample (true) peak at the ceiling\n");
    {
        Params p = allOff(); p.limiter = true; p.ceilingDb = -0.3f; const double ceil = dbToGain (-0.3); const int n = (int) (FS * 3);
        MixDoctor d; d.prepare (FS);
        // worst case for sample-peak limiters: fs/4 tone at 45 deg -> samples are 0.707 of true peak
        Sig L = tone (n, FS / 4, 1.5, kPi / 4), R = L;
        for (int i = 0; i < n; i += 24000) for (int k = 0; k < 2000; ++k) { L[(size_t) std::min (n - 1, i + k)] += (float) (0.8 * std::sin (2 * kPi * 50 * k / FS)); }
        const Sig R0 = R; (void) R0; run (d, p, L, R);
        const Sig body (L.begin() + 4000, L.end());
        double sp = 0; for (float v : body) sp = std::max (sp, (double) std::abs (v));
        CHECK (sp <= ceil + 1e-6, "sample peak %.4f <= ceiling %.4f", sp, ceil);
        CHECK (db (truePeak (body)) <= -0.3 + 0.15, "true peak %.2f dBTP (ceiling -0.30)", db (truePeak (body)));
        MixDoctor d2; d2.prepare (FS); Sig q = tone (n, 440, 0.3), q2 = q; run (d2, p, q, q2);
        CHECK (std::abs (db (rmsOf (q, n / 2, n) / rmsOf (tone (n, 440, 0.3), n / 2, n))) < 0.01, "quiet material is not touched by the limiter");
    }

    std::printf("\n== 5. latency reporting\n");
    {
        MixDoctor d; d.prepare (FS); Sig L ((size_t) 4096, 0.f), R = L; L[100] = 0.5f; run (d, allOff(), L, R);
        int at = -1; for (int i = 0; i < 4096; ++i) if (std::abs (L[(size_t) i]) > 0.25f) { at = i; break; }
        CHECK (at - 100 == d.latencySamples(), "impulse delayed by %d samples == reported %d", at - 100, d.latencySamples());
    }

    std::printf("\n== 6. analyser calibration + tonal correction direction\n");
    {
        MixDoctor d; d.prepare (FS); Params p = allOff(); p.autoTone = false;
        Sig L = pink (N, 0.1), R = L; run (d, p, L, R);
        double worst = 0; std::printf ("     pink noise per-octave levels (should be flat):");
        for (int b = 0; b < kNumBands; ++b) { const double v = d.meters.bandRel[b].load(); std::printf (" %+.1f", v); worst = std::max (worst, std::abs (v)); }
        std::printf ("\n"); CHECK (worst < 2.0, "all bands within +/-2 dB of flat (worst %.2f dB)", worst);

        // bright mix: pink + lots of >5 kHz energy -> highs/air should be pulled DOWN, within the limit
        MixDoctor d2; d2.prepare (FS); Params q = allOff(); q.autoTone = true; q.toneAmount = 1.f; q.toneMaxDb = 3.f; getStyleTarget (0, q.target.data());
        Sig bl = add (pink (N, 0.1), hpNoise (N, 5000, 0.08)), br = bl; run (d2, q, bl, br);
        std::printf ("     bright-mix corrections (dB):"); for (int b = 0; b < kNumBands; ++b) std::printf (" %+.2f", d2.meters.bandCorr[b].load()); std::printf ("\n");
        CHECK (d2.meters.bandCorr[7].load() < -0.5 && d2.meters.bandCorr[6].load() < -0.5, "air (%.2f) and highs (%.2f) are reduced", d2.meters.bandCorr[7].load(), d2.meters.bandCorr[6].load());
        double mx = 0; for (int b = 0; b < kNumBands; ++b) mx = std::max (mx, (double) std::abs (d2.meters.bandCorr[b].load()));
        CHECK (mx <= 3.01, "no band exceeds the +/-3 dB limit (max %.2f)", mx);
    }

    std::printf("\n== 7. harshness tamer reacts to bursts, not to steady content\n");
    {
        MixDoctor d; d.prepare (FS); Params p = allOff(); p.harsh = true; p.harshMaxDb = 4.f;
        double peakGr = 0, steadyGr = 0; const int n = (int) (FS * 12);
        Sig L = pink (n, 0.1), R;
        Sig burst = bandNoise (n, 2500, 7000, 0.35);
        for (int i = 0; i < n; ++i) if (i > (int) (FS * 9) && i < (int) (FS * 10)) L[(size_t) i] += burst[(size_t) i];
        R = L;
        run (d, p, L, R, [&] (int done) { const double g = d.meters.harshGR.load(); if (done < (int) (FS * 8.9)) steadyGr = std::max (steadyGr, g); else peakGr = std::max (peakGr, g); });
        CHECK (steadyGr < 0.5, "steady pink noise: GR stays %.2f dB", steadyGr);
        CHECK (peakGr > 1.5 && peakGr <= 4.05, "3-7 kHz burst: GR reaches %.2f dB (limit 4.0)", peakGr);
    }

    std::printf("\n== 8. detectors\n");
    {
        const int n = (int) (FS * 4); Params p = allOff();
        { MixDoctor d; d.prepare (FS); Sig L ((size_t) n, 0.05f), R = L; run (d, p, L, R); CHECK (std::abs (d.meters.dcOffset.load() - 0.05f) < 0.003, "DC meter reads %.4f for 0.05 offset", d.meters.dcOffset.load()); }
        { MixDoctor d; d.prepare (FS); Sig L = add (tone (n, 12, 0.3), tone (n, 50, 0.3)), R = L; run (d, p, L, R); CHECK (d.meters.rumbleRel.load() > -6.0, "12 Hz rumble flagged (%.1f dB re sub)", d.meters.rumbleRel.load()); }
        { MixDoctor d; d.prepare (FS); Sig L = tone (n, 50, 0.3), R = L; run (d, p, L, R); CHECK (d.meters.rumbleRel.load() < -25.0, "clean 50 Hz sub not flagged (%.1f dB)", d.meters.rumbleRel.load()); }
        { MixDoctor d; d.prepare (FS); Sig a = tone (n, 60, 0.3, 0), L = add (a, tone (n, 3000, 0.1)), R = add (neg (a), tone (n, 3000, 0.1)); run (d, p, L, R);
          CHECK (d.meters.lowSideDb.load() > 0.0, "wide sub detected: side %.1f dB re mid", d.meters.lowSideDb.load());
          CHECK (d.meters.correlation.load() < 0.0, "anti-phase low end drives correlation negative (%.2f)", d.meters.correlation.load()); }
        { MixDoctor d; d.prepare (FS); Sig L = tone (n, 60, 0.3), R = L; run (d, p, L, R); CHECK (d.meters.lowSideDb.load() < -40.0, "mono sub reads %.1f dB side", d.meters.lowSideDb.load()); }
    }

    std::printf("\n== 9. CPU\n");
    {
        MixDoctor d; d.prepare (FS); Params p; getStyleTarget (0, p.target.data()); Sig L = pink (N, 0.2), R = L;
        const auto t0 = std::chrono::steady_clock::now(); run (d, p, L, R); const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  8 s of stereo @48k, everything on: %.3f s  ->  %.1f%% of one core\n", s, 100.0 * s / 8.0);
        CHECK (s / 8.0 < 0.15, "comfortably real-time");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

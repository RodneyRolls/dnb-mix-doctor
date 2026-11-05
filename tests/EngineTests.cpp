// Offline checks for the DnB Mix Doctor engine. Build: cmake --build build --target DnBMixDoctorTests
#include <juce_dsp/juce_dsp.h>
#include "dsp/MixEngine.h"
#include <cstdio>
#include <random>

using namespace mcdsp;
static int failures = 0;
#define CHECK(cond, ...) do { const bool ok_ = (cond); std::printf ("  [%s] ", ok_ ? "PASS" : "FAIL"); std::printf (__VA_ARGS__); std::printf ("\n"); if (! ok_) ++failures; } while (0)

namespace
{
    std::mt19937 rng (7);

    /** Rough DnB-like test mix: 55 Hz sub with kicks, snare noise bursts at 2/4, hats, a detuned saw reese. */
    juce::AudioBuffer<float> makeMix (double sr, double seconds, float gain, bool stereoSub = false)
    {
        const int n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (2, n);
        std::normal_distribution<float> noise (0.0f, 1.0f);
        const double beat = 60.0 / 174.0;
        double ph1 = 0, ph2 = 0, phSub = 0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double inBeat = std::fmod (t, beat);
            const int beatIdx = (int) (t / beat);
            const float kick = (beatIdx % 2 == 0) ? (float) (std::sin (2 * kPi * (50 + 120 * std::exp (-inBeat * 40)) * inBeat) * std::exp (-inBeat * 9)) : 0.0f;
            const float snare = (beatIdx % 2 == 1) ? noise (rng) * (float) std::exp (-inBeat * 25) * 0.5f : 0.0f;
            const float hat = noise (rng) * (float) std::exp (-std::fmod (t, beat / 2) * 120) * 0.12f;
            phSub += 55.0 / sr; ph1 += 110.0 / sr; ph2 += 110.7 / sr;
            const float sub = (float) std::sin (2 * kPi * phSub) * 0.45f;
            const float reese = (float) ((std::fmod (ph1, 1.0) - 0.5) + (std::fmod (ph2, 1.0) - 0.5)) * 0.18f;
            const float m = (kick * 0.8f + snare + hat + sub + reese) * gain;
            const float side = stereoSub ? (float) std::sin (2 * kPi * 45.0 * t) * 0.3f * gain : 0.0f;
            b.setSample (0, i, m + side + hat * 0.05f);
            b.setSample (1, i, m - side - hat * 0.05f);
        }
        return b;
    }

    void run (MixEngine& e, juce::AudioBuffer<float>& b, const EngineParams& p, int block = 512)
    {
        for (int s = 0; s < b.getNumSamples(); s += block)
        {
            const int n = std::min (block, b.getNumSamples() - s);
            float* io[2] = { b.getWritePointer (0, s), b.getNumChannels() > 1 ? b.getWritePointer (1, s) : nullptr };
            e.process (io, b.getNumChannels(), n, p);
        }
    }

    float truePeakDb (const juce::AudioBuffer<float>& b, int skip)
    {
        TruePeakDetector d;
        float pk = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
            {
                const float v = d.process (ch, b.getSample (ch, i));
                if (i > skip) pk = std::max (pk, v);
            }
            d.advance();
        }
        return gainToDb (pk);
    }

    bool finite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i))) return false;
        return true;
    }

    EngineParams neutral()
    {
        EngineParams p;
        p.fix = 0; p.saturate = 0; p.glue = 0; p.loudnessLufs = -14; p.ceilingDb = 0;
        return p;
    }
}

int main()
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        std::printf ("\n=== %.0f Hz ===\n", sr);

        // 1. MIX 0 % == input delayed by the reported latency
        {
            MixEngine e; e.prepare (sr, 512, 2);
            auto in = makeMix (sr, 1.0, 0.3f), out = in;
            auto p = neutral(); p.mix = 0;
            run (e, out, p);
            const int L = e.getLatency();
            float err = 0;
            for (int i = L; i < in.getNumSamples(); ++i) err = std::max (err, std::abs (out.getSample (0, i) - in.getSample (0, i - L)));
            CHECK (err < 1e-5f, "dry path aligned to latency %d samples (%.2f ms), max err %.2e", L, 1000.0 * L / sr, err);
        }

        // 2. wet path is aligned with the dry path (impulse lands at the same sample)
        for (bool os : { false, true })
        {
            MixEngine e; e.prepare (sr, 512, 2);
            juce::AudioBuffer<float> b (2, (int) sr / 2); b.clear();
            const int at = 1000; b.setSample (0, at, 0.01f); b.setSample (1, at, 0.01f);
            auto p = neutral(); p.oversample = os; p.mix = 1;
            run (e, b, p);
            int best = 0; float bestV = 0;
            for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > bestV) { bestV = std::abs (b.getSample (0, i)); best = i; }
            CHECK (std::abs (best - (at + e.getLatency())) <= 1, "%s wet impulse at %d, expected %d", os ? "4x" : "1x", best, at + e.getLatency());
        }

        // 3. true-peak ceiling holds on a hot mix, both paths
        for (bool os : { false, true })
        {
            MixEngine e; e.prepare (sr, 512, 2);
            auto b = makeMix (sr, 6.0, 2.0f);   // ~+6 dB too hot
            EngineParams p; p.oversample = os; p.ceilingDb = -1.0f; p.loudnessLufs = -5.0f;
            p.subTight = p.kickBass = p.punch = p.snap = p.mud = p.reese = p.harsh = p.air = p.glue = p.saturate = 0.6f;
            run (e, b, p);
            const float tp = truePeakDb (b, e.getLatency() + (int) sr / 10);
            CHECK (finite (b) && tp <= -1.0f + 0.05f, "%s true peak %.2f dBTP with ceiling -1.0", os ? "4x" : "1x", tp);
        }

        // 4. loudness lands near the target
        {
            MixEngine e; e.prepare (sr, 512, 2);
            auto b = makeMix (sr, 30.0, 0.12f);
            EngineParams p; p.loudnessLufs = -9.0f; p.ceilingDb = -1.0f;
            run (e, b, p);
            LoudnessMeter m; m.prepare (sr);
            const int start = (int) (sr * 20.0);
            const float* rd[2] = { b.getReadPointer (0, start), b.getReadPointer (1, start) };
            m.process (rd, 2, b.getNumSamples() - start);
            CHECK (std::abs (m.getIntegrated() - (-9.0f)) < 1.5f, "output %.2f LUFS for a -9 LUFS target (last 10 s)", m.getIntegrated());
        }

        // 5. SUB MONO removes low side content, keeps highs wide
        {
            MixEngine e; e.prepare (sr, 512, 2);
            auto b = makeMix (sr, 3.0, 0.2f, true);
            auto in = b;
            auto p = neutral(); p.fix = 1; p.subMonoHz = 120; p.width = 1;
            run (e, b, p);
            auto sideDb = [] (const juce::AudioBuffer<float>& x, int from, int to)
            { double s = 0; for (int i = from; i < to; ++i) { const double d = 0.5 * (x.getSample (0, i) - x.getSample (1, i)); s += d * d; } return 10.0 * std::log10 (s / (to - from) + 1e-20); };
            const int L = e.getLatency(), from = (int) sr;
            const double before = sideDb (in, from, in.getNumSamples() - L), after = sideDb (b, from + L, b.getNumSamples());
            CHECK (after < before - 15.0, "low side energy %.1f dB -> %.1f dB", before, after);
        }

        // 6. mono layout runs and stays finite
        {
            MixEngine e; e.prepare (sr, 512, 1);
            auto st = makeMix (sr, 2.0, 0.5f);
            juce::AudioBuffer<float> b (1, st.getNumSamples()); b.copyFrom (0, 0, st, 0, 0, st.getNumSamples());
            EngineParams p; p.oversample = true;
            run (e, b, p, 333);
            CHECK (finite (b), "mono + 4x oversampling, odd block size: output finite");
        }
    }

    // 7. CPU: 60 s of stereo audio at 48 kHz, everything on
    {
        MixEngine e; e.prepare (48000.0, 256, 2);
        auto b = makeMix (48000.0, 60.0, 0.4f);
        EngineParams p; p.oversample = true;
        p.subTight = p.kickBass = p.punch = p.snap = p.mud = p.reese = p.harsh = p.air = p.glue = p.saturate = 0.5f;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        run (e, b, p, 256);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("\n  [INFO] 60 s stereo @48k with 4x OS processed in %.0f ms -> %.2f %% of one core\n", ms, ms / 600.0);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

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

        // 4a. volume stability over a song: intro -> drop -> breakdown -> drop. Both drops must come out at the
        //     same level (no downward drift as more loud material plays) and the level must not sag within a drop.
        {
            MixEngine e; e.prepare (sr, 512, 2);
            const double secs[] = { 8.0, 20.0, 10.0, 20.0 };
            const float gains[] = { 0.03f, 0.12f, 0.03f, 0.12f };
            juce::AudioBuffer<float> b (2, 0);
            std::vector<int> starts;
            for (int s = 0; s < 4; ++s)
            {
                auto part = makeMix (sr, secs[s], gains[s]);
                starts.push_back (b.getNumSamples());
                b.setSize (2, b.getNumSamples() + part.getNumSamples(), true);
                for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, starts.back(), part, ch, 0, part.getNumSamples());
            }
            EngineParams p; p.loudnessLufs = -7.0f; p.ceilingDb = -0.5f;
            run (e, b, p);
            auto lufs = [&] (double fromS, double toS)
            {
                LoudnessMeter m; m.prepare (sr);
                const int a = (int) (fromS * sr), n = (int) ((toS - fromS) * sr);
                const float* rd[2] = { b.getReadPointer (0, a), b.getReadPointer (1, a) };
                m.process (rd, 2, n);
                return m.getIntegrated();
            };
            const double d1 = starts[1] / sr, d2 = starts[3] / sr;
            const float drop1Late = lufs (d1 + 12.0, d1 + 20.0), drop2Early = lufs (d2 + 3.0, d2 + 10.0), drop2Late = lufs (d2 + 12.0, d2 + 20.0);
            CHECK (std::abs (drop1Late - drop2Late) < 0.6f && std::abs (drop2Early - drop2Late) < 0.6f && std::abs (drop2Late - (-7.0f)) < 1.5f,
                   "stable level: drop 1 %.2f, drop 2 early %.2f / late %.2f LUFS (target -7)", drop1Late, drop2Early, drop2Late);
        }

        // 4c. AUTO GAIN knob scales the applied gain: 0 % leaves the level alone, 50 % lands in between
        {
            float out[3] {};
            const float mixes[3] = { 0.0f, 0.5f, 1.0f };
            for (int k = 0; k < 3; ++k)
            {
                MixEngine e; e.prepare (sr, 512, 2);
                auto b = makeMix (sr, 20.0, 0.12f);
                EngineParams p; p.loudnessLufs = -7.0f; p.autoGainMix = mixes[k];
                run (e, b, p);
                LoudnessMeter m; m.prepare (sr);
                const int start = (int) (sr * 12.0);
                const float* rd[2] = { b.getReadPointer (0, start), b.getReadPointer (1, start) };
                m.process (rd, 2, b.getNumSamples() - start);
                out[k] = m.getIntegrated();
            }
            CHECK (out[0] < -14.0f && out[1] > out[0] + 3.0f && out[2] > out[1] + 3.0f && std::abs (out[2] - (-7.0f)) < 1.5f,
                   "AUTO GAIN 0/50/100 %%: %.2f / %.2f / %.2f LUFS", out[0], out[1], out[2]);
        }

        // 4b. maximiser: loud targets are reached and the true-peak ceiling still holds
        for (float target : { -5.0f, -4.0f })
        {
            MixEngine e; e.prepare (sr, 512, 2);
            auto b = makeMix (sr, 30.0, 0.12f);
            EngineParams p; p.loudnessLufs = target; p.ceilingDb = -0.3f;
            run (e, b, p);
            LoudnessMeter m; m.prepare (sr);
            const int start = (int) (sr * 20.0);
            const float* rd[2] = { b.getReadPointer (0, start), b.getReadPointer (1, start) };
            m.process (rd, 2, b.getNumSamples() - start);
            const float tp = truePeakDb (b, start);
            CHECK (std::abs (m.getIntegrated() - target) < 1.5f && tp <= -0.3f + 0.05f,
                   "loud target %.1f LUFS -> %.2f LUFS, true peak %.2f dBTP (ceiling -0.3)", target, m.getIntegrated(), tp);
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

        // 5b. ADVANCED 16-band EQ: a band moves a sine at its frequency by its gain, scaled by EQ MIX; EQ ON off = flat
        {
            auto sineDb = [&] (float freq, float bandDb, int band, float eqMix, bool eqOn)
            {
                MixEngine e; e.prepare (sr, 512, 2);
                const int n = (int) (sr * 1.0);
                juce::AudioBuffer<float> b (2, n);
                for (int i = 0; i < n; ++i)
                {
                    const float v = 0.05f * (float) std::sin (2 * kPi * freq * i / sr);
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
                auto p = neutral(); p.autoGainMix = 0; p.clipper = 0; p.oversample = false;
                p.eqDb[(size_t) band] = bandDb; p.eqMix = eqMix; p.eqOn = eqOn;
                run (e, b, p);
                double s = 0; for (int i = n / 2; i < n; ++i) s += (double) b.getSample (0, i) * b.getSample (0, i);
                return 10.0 * std::log10 (s / (n - n / 2)) - 10.0 * std::log10 (0.05 * 0.05 * 0.5);
            };
            const double full = sineDb (1000.0f, 6.0f, 8, 1.0f, true), half = sineDb (1000.0f, 6.0f, 8, 0.5f, true);
            const double cut = sineDb (100.0f, -9.0f, 3, 1.0f, true), off = sineDb (1000.0f, 6.0f, 8, 1.0f, false);
            const double top = sineDb (10000.0f, 6.0f, 13, 1.0f, true);
            CHECK (std::abs (full - 6.0) < 0.6 && std::abs (half - 3.0) < 0.5 && std::abs (cut + 9.0) < 0.7
                   && std::abs (off) < 0.2 && std::abs (top - 6.0) < 0.8,
                   "EQ 1k +6 dB -> %+.2f, at EQ MIX 50 %% %+.2f, 100 Hz -9 dB -> %+.2f, 10k +6 -> %+.2f, EQ off %+.2f",
                   full, half, cut, top, off);
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
        for (size_t k = 0; k < p.eqDb.size(); ++k) p.eqDb[k] = (k % 2 == 0) ? 3.0f : -3.0f;   // all 16 EQ bands active
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        run (e, b, p, 256);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("\n  [INFO] 60 s stereo @48k with 4x OS processed in %.0f ms -> %.2f %% of one core\n", ms, ms / 600.0);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

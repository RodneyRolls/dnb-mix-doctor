#pragma once
#include "DspUtils.h"
#include <cstdint>

namespace mcdsp
{
    /** MULTIBAND maximizer stage (our own design, runs in front of the broadband true-peak limiter).

        A plain broadband limiter turns the whole mix down whenever anything peaks: a big kick ducks the
        hats and vocals with it (pumping), and fast release on the bass distorts it. This stage instead
        works out WHICH part of the spectrum is causing each peak and turns down mostly that part:

        1. Phase-coherent 4-band split (Linkwitz-Riley 4th order at 120 Hz / 1 kHz / 6 kHz, with all-pass
           compensation, so the bands sum back to a flat, all-pass copy of the input).
        2. At every sample the overshoot above the ceiling is shared between the bands in proportion to
           how much each band pushes the waveform towards that peak (squared contribution), so the band
           that made the peak takes most of the reduction. Each band is capped at -12 dB; anything the cap
           leaves is spread evenly, and the broadband limiter after this stage catches the rest.
        3. Each band has its own look-ahead gain (sliding minimum + linear-ramp attack, like the main
           limiter) and its own smart release: a fast/slow envelope pair spots transients in that band.
           Transients release fast (loud, punchy), sustained material releases slowly (no pumping or
           distortion). Lows release slower than highs so bass waveforms are never chopped.

        MODE fades smoothly between this stage and a straight (delayed) pass-through, and once it is fully
        off the filters are skipped, so CLASSIC mode is bit-identical to the original maximiser.
        Latency is lookahead - 1 samples in both modes. */
    class MultibandMaximizer
    {
    public:
        static constexpr int numBands = 4;

        void prepare (double sampleRate, int lookaheadSamples)
        {
            sr = sampleRate;
            L = std::max (2, lookaheadSamples);
            for (int b = 0; b < numBands; ++b)
            {
                gains[(size_t) b].prepare (L);
                bandDelay[(size_t) b].prepare (L + 1);
                bandDelay[(size_t) b].setDelay (L - 1);
                // smart release range per band: sustained -> slow, transient -> fast
                relSlow[(size_t) b] = onePoleCoef (kReleaseS[(size_t) b] * 2.5, sr);
                relFast[(size_t) b] = onePoleCoef (kReleaseS[(size_t) b] * 0.4, sr);
                envFast[(size_t) b].setup (0.0005, 0.02, sr);
                envSlow[(size_t) b].setup (0.02, 0.25, sr);
            }
            dryDelay.prepare (L + 1);
            dryDelay.setDelay (L - 1);

            split1k.setup (sr, 1000.0);
            split120.setup (sr, 120.0);
            split6k.setup (sr, 6000.0);
            ap120.setup (Svf::Mode::allpass, sr, 120.0, kButterQ);
            ap6k.setup (Svf::Mode::allpass, sr, 6000.0, kButterQ);
            fadeStep = 1.0f / (float) (0.05 * sr);   // 50 ms mode crossfade
            reset();
        }

        int getLatency() const noexcept { return L - 1; }

        void reset() noexcept
        {
            for (int b = 0; b < numBands; ++b)
            {
                gains[(size_t) b].reset();
                bandDelay[(size_t) b].reset();
                envFast[(size_t) b].reset();
                envSlow[(size_t) b].reset();
            }
            dryDelay.reset();
            resetFilters();
            amount = target;
        }

        /** Sets the mode: true = multiband, false = classic (pass-through). Fades over 50 ms. */
        void setEnabled (bool on) noexcept { target = on ? 1.0f : 0.0f; }

        /** In place. Returns the deepest band gain reduction in this block in dB (<= 0). */
        float process (float* const* d, int numCh, int n, float ceilingGain) noexcept
        {
            if (amount <= 0.0f && target <= 0.0f)
            {
                // CLASSIC: delay only, keep the band machinery idle and clean
                dryDelay.processBlock (d, numCh, n);
                return 0.0f;
            }

            float minGain = 1.0f;
            for (int i = 0; i < n; ++i)
            {
                // ---- split every channel into 4 coherent bands
                std::array<std::array<float, numBands>, 2> xb {};
                int peakCh = 0;
                float peakAbs = 0.0f, peakSum = 0.0f;
                for (int ch = 0; ch < numCh; ++ch)
                {
                    float lo, hi, b0, b1, b2, b3;
                    split1k.process (d[ch][i], ch, lo, hi);
                    split120.process (ap6k.process (lo, ch), ch, b0, b1);
                    split6k.process (ap120.process (hi, ch), ch, b2, b3);
                    xb[(size_t) ch] = { b0, b1, b2, b3 };
                    const float s = b0 + b1 + b2 + b3;
                    if (std::abs (s) > peakAbs) { peakAbs = std::abs (s); peakSum = s; peakCh = ch; }
                }

                // ---- share the overshoot between the bands that cause it
                std::array<float, numBands> r { 1.0f, 1.0f, 1.0f, 1.0f };
                if (peakAbs > ceilingGain)
                {
                    const float need = peakAbs - ceilingGain;   // amplitude to remove at this sample
                    const float sgn = peakSum >= 0.0f ? 1.0f : -1.0f;
                    std::array<float, numBands> c {};
                    float sumC = 0.0f, sumC2 = 0.0f;
                    for (int b = 0; b < numBands; ++b)
                    {
                        c[(size_t) b] = std::max (0.0f, xb[(size_t) peakCh][(size_t) b] * sgn);
                        sumC += c[(size_t) b];
                        sumC2 += c[(size_t) b] * c[(size_t) b];
                    }
                    if (sumC2 > 1.0e-12f)
                    {
                        std::array<float, numBands> f {};
                        float removed = 0.0f;
                        for (int b = 0; b < numBands; ++b)
                        {
                            f[(size_t) b] = std::min (kMaxCut, need * c[(size_t) b] / sumC2);
                            removed += f[(size_t) b] * c[(size_t) b];
                        }
                        const float residual = need - removed;
                        if (residual > 0.0f)   // spread what the per-band cap left over evenly
                            for (int b = 0; b < numBands; ++b)
                                f[(size_t) b] = std::min (1.0f, f[(size_t) b] + residual / sumC);
                        for (int b = 0; b < numBands; ++b)
                            r[(size_t) b] = 1.0f - f[(size_t) b];
                    }
                    else
                    {
                        const float g = ceilingGain / peakAbs;
                        r = { g, g, g, g };
                    }
                }

                // ---- per band: transient-aware release, look-ahead gain, apply to the delayed band
                amount = target > amount ? std::min (target, amount + fadeStep) : std::max (target, amount - fadeStep);
                std::array<float, 2> out {};
                for (int b = 0; b < numBands; ++b)
                {
                    float m = 0.0f;
                    for (int ch = 0; ch < numCh; ++ch) m = std::max (m, std::abs (xb[(size_t) ch][(size_t) b]));
                    const float fast = envFast[(size_t) b].process (m), slow = envSlow[(size_t) b].process (m);
                    const float trans = clamp01 ((fast / (slow + 1.0e-6f) - 1.0f) * 1.5f);
                    const float rel = relSlow[(size_t) b] + (relFast[(size_t) b] - relSlow[(size_t) b]) * trans;

                    const float g = gains[(size_t) b].process (r[(size_t) b], rel);
                    const float ga = 1.0f - amount * (1.0f - g);
                    minGain = std::min (minGain, g);

                    auto& dl = bandDelay[(size_t) b];
                    for (int ch = 0; ch < numCh; ++ch)
                        out[(size_t) ch] += dl.process (ch, xb[(size_t) ch][(size_t) b]) * ga;
                    dl.advance();
                }

                for (int ch = 0; ch < numCh; ++ch)
                {
                    const float dry = dryDelay.process (ch, d[ch][i]);
                    d[ch][i] = dry + (out[(size_t) ch] - dry) * amount;
                }
                dryDelay.advance();
            }

            if (amount <= 0.0f && target <= 0.0f)
            {
                // fully faded to CLASSIC: park the band state so the next switch-on starts clean
                for (int b = 0; b < numBands; ++b) { gains[(size_t) b].reset(); bandDelay[(size_t) b].reset(); }
                resetFilters();
            }
            return gainToDb (minGain);
        }

    private:
        static constexpr double kButterQ = 0.70710678;
        static constexpr float kMaxCut = 0.75f;   // a band is turned down by at most -12 dB here
        static constexpr std::array<double, numBands> kReleaseS { 0.15, 0.08, 0.04, 0.025 };

        /** Linkwitz-Riley 4th-order crossover: two cascaded Butterworth sections per side. */
        struct Lr4
        {
            void setup (double rate, double fc) noexcept
            {
                lp1.setup (Svf::Mode::lowpass, rate, fc, kButterQ);  lp2 = lp1;
                hp1.setup (Svf::Mode::highpass, rate, fc, kButterQ); hp2 = hp1;
                reset();
            }
            void reset() noexcept { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
            inline void process (float x, int ch, float& lo, float& hi) noexcept
            {
                lo = lp2.process (lp1.process (x, ch), ch);
                hi = hp2.process (hp1.process (x, ch), ch);
            }
            Svf lp1, lp2, hp1, hp2;
        };

        /** Look-ahead gain smoother: sliding-window minimum over L samples -> release (coefficient given
            per sample) -> L-tap moving average. The output never exceeds the requested gain of the sample
            it lands on once the audio is delayed by L - 1. */
        struct LookaheadGain
        {
            void prepare (int len)
            {
                L = len;
                dqVal.assign ((size_t) L + 2, 1.0f);
                dqIdx.assign ((size_t) L + 2, 0);
                box.assign ((size_t) L, 1.0f);
                reset();
            }
            void reset() noexcept
            {
                std::fill (box.begin(), box.end(), 1.0f);
                boxSum = (double) L;
                boxPos = head = tail = 0;
                count = 0;
                hr = 1.0f;
            }
            inline float process (float r, float relCoef) noexcept
            {
                const int cap = (int) dqVal.size();
                while (head != tail && dqVal[(size_t) ((tail - 1 + cap) % cap)] >= r)
                    tail = (tail - 1 + cap) % cap;
                dqVal[(size_t) tail] = r;
                dqIdx[(size_t) tail] = count;
                tail = (tail + 1) % cap;
                while (dqIdx[(size_t) head] <= count - L)
                    head = (head + 1) % cap;
                const float h = dqVal[(size_t) head];
                ++count;

                hr = std::min (h, 1.0f - (1.0f - hr) * relCoef);
                boxSum += (double) hr - (double) box[(size_t) boxPos];
                box[(size_t) boxPos] = hr;
                if (++boxPos == L) boxPos = 0;
                if ((count & 0xffff) == 0) { boxSum = 0; for (auto v : box) boxSum += v; }   // kill drift
                return std::min (1.0f, (float) (boxSum / L));
            }

            int L = 2;
            std::vector<float> dqVal, box;
            std::vector<std::int64_t> dqIdx;
            double boxSum = 0;
            int boxPos = 0, head = 0, tail = 0;
            std::int64_t count = 0;
            float hr = 1.0f;
        };

        void resetFilters() noexcept
        {
            split1k.reset(); split120.reset(); split6k.reset(); ap120.reset(); ap6k.reset();
        }

        double sr = 48000.0;
        int L = 2;
        float amount = 0.0f, target = 0.0f, fadeStep = 0.001f;
        Lr4 split1k, split120, split6k;
        Svf ap120, ap6k;
        std::array<LookaheadGain, numBands> gains;
        std::array<DelayLine, numBands> bandDelay;
        DelayLine dryDelay;
        std::array<Envelope, numBands> envFast, envSlow;
        std::array<float, numBands> relSlow {}, relFast {};
    };
}

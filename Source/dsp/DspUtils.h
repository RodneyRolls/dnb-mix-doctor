#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

/** Small, allocation-free DSP building blocks used by the engine (all real-time safe after prepare()). */
namespace mcdsp
{
    constexpr double kPi = 3.14159265358979323846;

    inline float dbToGain (float db) noexcept       { return std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g) noexcept        { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
    inline float powerToDb (float p) noexcept       { return 10.0f * std::log10 (std::max (p, 1.0e-12f)); }
    inline float clamp01 (float v) noexcept         { return std::clamp (v, 0.0f, 1.0f); }

    /** One-pole smoothing coefficient for a time constant in seconds at a given rate. */
    inline float onePoleCoef (double seconds, double rate) noexcept
    {
        return seconds <= 0.0 ? 0.0f : (float) std::exp (-1.0 / (seconds * rate));
    }

    //==============================================================================
    /** Cytomic / Simper trapezoidal state-variable filter. Modulation-safe, so it can be
        retuned every sub-block (used for all dynamic EQ bands). Up to 2 channels. */
    struct Svf
    {
        enum class Mode { lowpass, highpass, bandpass, bell, highShelf, allpass };

        void reset() noexcept { ic1 = {}; ic2 = {}; }

        void setup (Mode m, double sr, double freq, double q, float gainDb = 0.0f) noexcept
        {
            freq = std::clamp (freq, 10.0, sr * 0.49);
            const double A = std::pow (10.0, gainDb / 40.0);
            double g = std::tan (kPi * freq / sr);
            double k = 1.0 / q;
            double M0 = 0, M1 = 0, M2 = 0;

            switch (m)
            {
                case Mode::lowpass:   M2 = 1; break;
                case Mode::highpass:  M0 = 1; M1 = -k; M2 = -1; break;
                case Mode::bandpass:  M1 = k; break;                        // unity peak gain
                case Mode::bell:      k = 1.0 / (q * A); M0 = 1; M1 = k * (A * A - 1.0); break;
                case Mode::highShelf: g *= std::sqrt (A); M0 = A * A; M1 = k * (1.0 - A) * A; M2 = 1.0 - A * A; break;
                case Mode::allpass:   M0 = 1; M1 = -2.0 * k; break;
            }

            a1 = (float) (1.0 / (1.0 + g * (g + k)));
            a2 = (float) (g * a1);
            a3 = (float) (g * a2);
            m0 = (float) M0; m1 = (float) M1; m2 = (float) M2;
        }

        inline float process (float v0, int ch = 0) noexcept
        {
            auto& s1 = ic1[(size_t) ch];
            auto& s2 = ic2[(size_t) ch];
            const float v3 = v0 - s2;
            const float v1 = a1 * s1 + a2 * v3;
            const float v2 = s2 + a2 * s1 + a3 * v3;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;
            return m0 * v0 + m1 * v1 + m2 * v2;
        }

        float a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;
        std::array<float, 2> ic1 {}, ic2 {};
    };

    //==============================================================================
    /** Attack/release envelope follower. Feed it |x| (peak) or x^2 (power). */
    struct Envelope
    {
        void setup (double attackS, double releaseS, double rate) noexcept
        {
            att = onePoleCoef (attackS, rate);
            rel = onePoleCoef (releaseS, rate);
        }
        inline float process (float in) noexcept
        {
            const float c = in > value ? att : rel;
            value = in + c * (value - in);
            return value;
        }
        void reset (float v = 0.0f) noexcept { value = v; }

        float att = 0, rel = 0, value = 0;
    };

    //==============================================================================
    /** Simple fixed-delay ring buffer, up to 2 channels. Allocation happens only in prepare(). */
    struct DelayLine
    {
        void prepare (int maxDelay)
        {
            size = 1;
            while (size < maxDelay + 2) size <<= 1;
            mask = size - 1;
            for (auto& b : buf) b.assign ((size_t) size, 0.0f);
            pos = 0;
        }
        void setDelay (int d) noexcept { delay = std::clamp (d, 0, size - 2); }
        void reset() noexcept { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); }

        /** Push one sample per channel and read the delayed one back (call once per sample frame). */
        inline float process (int ch, float x) noexcept
        {
            auto& b = buf[(size_t) ch];
            b[(size_t) pos] = x;
            return b[(size_t) ((pos - delay) & mask)];
        }
        inline void advance() noexcept { pos = (pos + 1) & mask; }

        /** Delays a whole block in-place. */
        void processBlock (float* const* data, int numCh, int n) noexcept
        {
            if (delay == 0) return;
            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < numCh; ++ch)
                    data[ch][i] = process (ch, data[ch][i]);
                advance();
            }
        }

        std::array<std::vector<float>, 2> buf;
        int size = 0, mask = 0, pos = 0, delay = 0;
    };

    //==============================================================================
    /** 1st-order DC blocker. */
    struct DcBlocker
    {
        void setup (double sr) noexcept { r = (float) (1.0 - (2.0 * kPi * 10.0 / sr)); }
        inline float process (int ch, float x) noexcept
        {
            const float y = x - x1[(size_t) ch] + r * y1[(size_t) ch];
            x1[(size_t) ch] = x; y1[(size_t) ch] = y;
            return y;
        }
        void reset() noexcept { x1 = {}; y1 = {}; }
        float r = 0.999f;
        std::array<float, 2> x1 {}, y1 {};
    };

    //==============================================================================
    /** 4x polyphase windowed-sinc interpolator used to estimate inter-sample (true) peaks.
        latency() samples of delay: the estimate for input sample n-latency() is returned. */
    struct TruePeakDetector
    {
        static constexpr int taps = 16, phases = 4;

        TruePeakDetector()
        {
            // phase p interpolates at fractional position p/4 between centre samples
            for (int p = 1; p < phases; ++p)
            {
                double sum = 0;
                for (int t = 0; t < taps; ++t)
                {
                    const double x = (double) (t - taps / 2 + 1) - (double) p / phases;   // distance to tap
                    const double sinc = std::abs (x) < 1e-9 ? 1.0 : std::sin (kPi * x) / (kPi * x);
                    const double w = 0.5 + 0.5 * std::cos (kPi * x / (taps / 2.0));       // Hann window
                    coefs[(size_t) p][(size_t) t] = (float) (sinc * w);
                    sum += sinc * w;
                }
                for (auto& c : coefs[(size_t) p]) c = (float) (c / sum);
            }
        }

        static constexpr int latency() noexcept { return taps / 2; }
        void reset() noexcept { for (auto& h : hist) h.fill (0.0f); idx = 0; }

        /** Returns the true-peak magnitude around the sample delayed by latency(). */
        inline float process (int ch, float x) noexcept
        {
            auto& h = hist[(size_t) ch];
            h[(size_t) idx] = x;
            h[(size_t) (idx + taps)] = x;                  // mirrored for contiguous reads
            const float* w = h.data() + idx + 1;            // oldest .. newest
            float peak = std::abs (w[taps / 2 - 1]);
            for (int p = 1; p < phases; ++p)
            {
                const auto& c = coefs[(size_t) p];
                float acc = 0;
                for (int t = 0; t < taps; ++t) acc += c[(size_t) t] * w[t];
                peak = std::max (peak, std::abs (acc));
            }
            return peak;
        }
        inline void advance() noexcept { idx = (idx + 1) % taps; }

        std::array<std::array<float, taps>, phases> coefs {};
        std::array<std::array<float, taps * 2>, 2> hist {};
        int idx = 0;
    };
}

#pragma once
#include "DspUtils.h"
#include <cstdint>

namespace mcdsp
{
    /** Look-ahead brick-wall limiter.

        Gain computer: required gain r[n] = min(1, ceiling / peak[n]) -> sliding-window minimum over the
        look-ahead length L -> exponential release -> L-tap moving average. Because every value inside the
        averaging window is <= the required gain of the sample being output, the output can never exceed
        the ceiling, while the moving average gives a smooth (linear-ramp) attack with no distortion.

        With useTruePeak the detector looks at a 4x interpolated signal so inter-sample peaks are caught
        (ITU-R BS.1770 style). When the limiter itself runs inside the 4x oversampled block, plain sample
        peaks are used instead. */
    class TruePeakLimiter
    {
    public:
        /** With slowReleaseSeconds > 0 the release is program dependent: short peaks recover with the fast
            release (loud, punchy), while sustained limiting recovers quickly only back to the sustained
            gain level and then slowly to unity (no pumping/distortion on dense material). */
        void prepare (double sampleRate, int lookaheadSamples, bool useTruePeak, double releaseSeconds = 0.06,
                      double slowReleaseSeconds = 0.0)
        {
            L = std::max (1, lookaheadSamples);
            tp = useTruePeak;
            relCoef = onePoleCoef (releaseSeconds, sampleRate);
            adaptive = slowReleaseSeconds > 0.0;
            relSlowCoef = onePoleCoef (std::max (releaseSeconds, slowReleaseSeconds), sampleRate);
            susCoef = onePoleCoef (std::max (releaseSeconds, slowReleaseSeconds) * 1.5, sampleRate);
            audioDelay.prepare (getLatency() + 1);
            audioDelay.setDelay (getLatency());
            dqVal.assign ((size_t) L + 2, 1.0f);
            dqIdx.assign ((size_t) L + 2, 0);
            box.assign ((size_t) L, 1.0f);
            reset();
        }

        int getLatency() const noexcept { return L - 1 + (tp ? TruePeakDetector::latency() : 0); }

        void reset() noexcept
        {
            audioDelay.reset();
            det.reset();
            std::fill (box.begin(), box.end(), 1.0f);
            boxSum = (double) L;
            boxPos = 0;
            head = tail = 0;
            count = 0;
            hr = sus = 1.0f;
        }

        /** In-place. Returns the deepest gain reduction in this block in dB (<= 0). */
        float process (float* const* d, int numCh, int n, float ceilingGain) noexcept
        {
            const int cap = (int) dqVal.size();
            float minGain = 1.0f;

            for (int i = 0; i < n; ++i)
            {
                float peak = 0.0f;
                for (int ch = 0; ch < numCh; ++ch)
                    peak = std::max (peak, tp ? det.process (ch, d[ch][i]) : std::abs (d[ch][i]));
                if (tp) det.advance();

                const float r = peak > ceilingGain ? ceilingGain / peak : 1.0f;

                // sliding-window minimum (monotonic deque, fixed storage)
                while (head != tail && dqVal[(size_t) ((tail - 1 + cap) % cap)] >= r)
                    tail = (tail - 1 + cap) % cap;
                dqVal[(size_t) tail] = r;
                dqIdx[(size_t) tail] = count;
                tail = (tail + 1) % cap;
                while (dqIdx[(size_t) head] <= count - L)
                    head = (head + 1) % cap;
                const float h = dqVal[(size_t) head];
                ++count;

                if (adaptive)
                {
                    sus = hr + (sus - hr) * susCoef;   // sustained gain-reduction level
                    const float released = hr < sus - 1.0e-4f ? sus - (sus - hr) * relCoef        // fast back to the sustained level
                                                              : 1.0f - (1.0f - hr) * relSlowCoef; // then slowly to unity
                    hr = std::min (h, released);
                }
                else
                    hr = std::min (h, 1.0f - (1.0f - hr) * relCoef);

                boxSum += (double) hr - (double) box[(size_t) boxPos];
                box[(size_t) boxPos] = hr;
                if (++boxPos == L) boxPos = 0;
                const float g = std::min (1.0f, (float) (boxSum / L));
                minGain = std::min (minGain, g);

                for (int ch = 0; ch < numCh; ++ch)
                {
                    const float y = audioDelay.process (ch, d[ch][i]) * g;
                    d[ch][i] = std::clamp (y, -ceilingGain, ceilingGain);   // numerical safety only
                }
                audioDelay.advance();
            }

            if ((count & 0xffff) < n)   // periodically re-sum to kill floating point drift
            {
                boxSum = 0;
                for (auto v : box) boxSum += v;
            }
            return gainToDb (minGain);
        }

    private:
        int L = 1;
        bool tp = true;
        float relCoef = 0.999f, relSlowCoef = 0.9999f, susCoef = 0.9999f, hr = 1.0f, sus = 1.0f;
        bool adaptive = false;
        TruePeakDetector det;
        DelayLine audioDelay;
        std::vector<float> dqVal;
        std::vector<std::int64_t> dqIdx;
        std::vector<float> box;
        double boxSum = 0;
        int boxPos = 0, head = 0, tail = 0;
        std::int64_t count = 0;
    };
}

#pragma once
#include "DspUtils.h"

namespace mcdsp
{
    /** ITU-R BS.1770-4 / EBU R128 loudness meter: K-weighting, 100 ms sub-blocks,
        momentary (400 ms), short-term (3 s) and gated integrated loudness.
        Integrated loudness uses a fixed histogram so no allocation happens while running. */
    class LoudnessMeter
    {
    public:
        void prepare (double sampleRate)
        {
            sr = sampleRate;
            // K-weighting stage 1 (high shelf) and stage 2 (high pass), coefficients for any rate
            {
                const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
                const double K = std::tan (kPi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
                const double a0 = 1.0 + K / Q + K * K;
                s1 = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                       2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
            }
            {
                const double f0 = 38.13547087602444, Q = 0.5003270373238773;
                const double K = std::tan (kPi * f0 / sr), a0 = 1.0 + K / Q + K * K;
                s2 = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
            }
            blockLen = std::max (1, (int) std::lround (sr * 0.1));
            resetAll();
        }

        void resetAll() noexcept
        {
            for (auto& z : z1) z = {};
            for (auto& z : z2) z = {};
            blockAcc = 0; blockPos = 0; blocksSeen = 0; ringPos = 0;
            ring.fill (0.0);
            resetIntegrated();
            momentary = shortTerm = -100.0f;
        }

        void resetIntegrated() noexcept
        {
            histCount.fill (0);
            histEnergy.fill (0.0);
            integrated = -100.0f;
        }

        /** Feed a block (1 or 2 channels). Updates the published values every 100 ms. */
        void process (const float* const* d, int numCh, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                double sum = 0;
                for (int ch = 0; ch < numCh; ++ch)
                {
                    const double y = biquad (s2, z2[(size_t) ch], biquad (s1, z1[(size_t) ch], (double) d[ch][i]));
                    sum += y * y;
                }
                if (numCh == 1) sum *= 2.0;   // mono is reproduced on both speakers (BS.1770 dual-mono)
                blockAcc += sum;

                if (++blockPos >= blockLen)
                    finishBlock();
            }
        }

        float getMomentary() const noexcept  { return momentary; }
        float getShortTerm() const noexcept  { return shortTerm; }
        float getIntegrated() const noexcept { return integrated; }

    private:
        struct Coefs { double b0, b1, b2, a1, a2; };
        struct State { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };

        static double biquad (const Coefs& c, State& s, double x) noexcept
        {
            const double y = c.b0 * x + c.b1 * s.x1 + c.b2 * s.x2 - c.a1 * s.y1 - c.a2 * s.y2;
            s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y;
            return y;
        }

        static float toLufs (double meanSquare) noexcept
        {
            return meanSquare <= 1.0e-12 ? -100.0f : (float) (-0.691 + 10.0 * std::log10 (meanSquare));
        }

        void finishBlock() noexcept
        {
            ring[(size_t) ringPos] = blockAcc / blockLen;
            ringPos = (ringPos + 1) % (int) ring.size();
            blockAcc = 0; blockPos = 0;
            ++blocksSeen;

            auto meanOfLast = [this] (int count)
            {
                count = std::min (count, blocksSeen);
                double s = 0;
                for (int k = 1; k <= count; ++k)
                    s += ring[(size_t) ((ringPos - k + (int) ring.size()) % (int) ring.size())];
                return count > 0 ? s / count : 0.0;
            };

            const double mEnergy = meanOfLast (4);
            momentary = toLufs (mEnergy);
            shortTerm = toLufs (meanOfLast (30));

            // gated integrated loudness (400 ms blocks, 75 % overlap, histogram of 0.1 LU bins)
            if (blocksSeen >= 4 && momentary > -70.0f)
            {
                const int bin = std::clamp ((int) ((momentary + 70.0f) * 10.0f), 0, numBins - 1);
                ++histCount[(size_t) bin];
                histEnergy[(size_t) bin] += mEnergy;

                double e = 0; long long c = 0;
                for (int b = 0; b < numBins; ++b) { e += histEnergy[(size_t) b]; c += histCount[(size_t) b]; }
                if (c > 0)
                {
                    const float relGate = toLufs (e / (double) c) - 10.0f;
                    const int firstBin = std::clamp ((int) std::ceil ((relGate + 70.0f) * 10.0f), 0, numBins);
                    double e2 = 0; long long c2 = 0;
                    for (int b = firstBin; b < numBins; ++b) { e2 += histEnergy[(size_t) b]; c2 += histCount[(size_t) b]; }
                    integrated = c2 > 0 ? toLufs (e2 / (double) c2) : -100.0f;
                }
            }
        }

        static constexpr int numBins = 800;   // -70 .. +10 LUFS
        double sr = 48000.0;
        Coefs s1 {}, s2 {};
        std::array<State, 2> z1 {}, z2 {};
        int blockLen = 4800, blockPos = 0, blocksSeen = 0, ringPos = 0;
        double blockAcc = 0;
        std::array<double, 30> ring {};
        std::array<long long, numBins> histCount {};
        std::array<double, numBins> histEnergy {};
        float momentary = -100.0f, shortTerm = -100.0f, integrated = -100.0f;
    };
}

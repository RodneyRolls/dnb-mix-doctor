#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include "../dsp/DspUtils.h"

namespace mcdsp
{
    /** Single-producer / single-consumer lock-free FIFO with fixed storage (no allocation, no locks).
        The audio thread pushes, the UI thread pops; if the UI falls behind, new data is dropped. */
    template <typename T, int Capacity>
    class SpscFifo
    {
    public:
        int push (const T* src, int n) noexcept
        {
            const auto scope = fifo.write (std::min (n, fifo.getFreeSpace()));
            for (int i = 0; i < scope.blockSize1; ++i) buf[(size_t) (scope.startIndex1 + i)] = src[i];
            for (int i = 0; i < scope.blockSize2; ++i) buf[(size_t) (scope.startIndex2 + i)] = src[scope.blockSize1 + i];
            return scope.blockSize1 + scope.blockSize2;
        }
        bool push (const T& v) noexcept { return push (&v, 1) == 1; }

        int pop (T* dst, int maxItems) noexcept
        {
            const auto scope = fifo.read (std::min (maxItems, fifo.getNumReady()));
            for (int i = 0; i < scope.blockSize1; ++i) dst[i] = buf[(size_t) (scope.startIndex1 + i)];
            for (int i = 0; i < scope.blockSize2; ++i) dst[scope.blockSize1 + i] = buf[(size_t) (scope.startIndex2 + i)];
            return scope.blockSize1 + scope.blockSize2;
        }
        int getNumReady() const noexcept { return fifo.getNumReady(); }

    private:
        juce::AbstractFifo fifo { Capacity };
        std::array<T, Capacity> buf {};
    };

    /** One horizontal pixel-column of the scrolling waveform (~3.9 ms of audio). */
    struct VisColumn
    {
        float outMin = 0, outMax = 0, inMin = 0, inMax = 0;
        float eLow = 0, eMid = 0, eHigh = 0;   // band energies of the output in this column
        float rmsL = 0, rmsR = 0;
        float grDb = 0;                        // glue + limiter + dynamic EQ reduction
    };

    struct StereoFrame { float l = 0, r = 0; };

    /** Builds VisColumns on the audio thread. 256 columns per second -> 1024 columns == 4 seconds. */
    class ColumnBuilder
    {
    public:
        static constexpr int columnsPerSecond = 256;

        void prepare (double sr)
        {
            samplesPerColumn = std::max (1, (int) std::lround (sr / columnsPerSecond));
            lp.setup (Svf::Mode::lowpass, sr, 150.0, 0.707);
            hp.setup (Svf::Mode::highpass, sr, 3500.0, 0.707);
            lp.reset(); hp.reset();
            startColumn();
        }

        template <typename Fifo>
        void process (const float* const* in, const float* const* out, int numCh, int n, float grDb, Fifo& fifo) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                const float iL = in[0][i], iR = numCh > 1 ? in[1][i] : iL;
                const float oL = out[0][i], oR = numCh > 1 ? out[1][i] : oL;
                const float om = 0.5f * (oL + oR);
                // waveform extremes use the louder channel so the shape shows true peaks
                const float oHi = std::max (oL, oR), oLo = std::min (oL, oR);
                const float iHi = std::max (iL, iR), iLo = std::min (iL, iR);
                col.outMax = std::max (col.outMax, oHi); col.outMin = std::min (col.outMin, oLo);
                col.inMax = std::max (col.inMax, iHi);   col.inMin = std::min (col.inMin, iLo);
                const float lo = lp.process (om), hi = hp.process (om), mid = om - lo - hi;
                col.eLow += lo * lo; col.eMid += mid * mid; col.eHigh += hi * hi;
                col.rmsL += oL * oL; col.rmsR += oR * oR;

                if (++count >= samplesPerColumn)
                {
                    const float inv = 1.0f / (float) count;
                    col.eLow *= inv; col.eMid *= inv; col.eHigh *= inv;
                    col.rmsL = std::sqrt (col.rmsL * inv); col.rmsR = std::sqrt (col.rmsR * inv);
                    col.grDb = grDb;
                    fifo.push (col);
                    startColumn();
                }
            }
        }

    private:
        void startColumn() noexcept { col = {}; col.outMin = col.inMin = 0; count = 0; }

        Svf lp, hp;
        VisColumn col;
        int samplesPerColumn = 188, count = 0;
    };
}

#pragma once
#include "../dsp/DspUtils.h"
#include <atomic>

namespace mcdsp
{
    /** What LEARN measured about the incoming track (filled on the audio thread, read on the message thread
        once the analyser reports done). */
    struct LearnStats
    {
        static constexpr int numBands = 28;   // 1/3-octave, 31.5 Hz .. 16 kHz
        static constexpr std::array<float, numBands> freqs {
            31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f, 160.0f, 200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f,
            800.0f, 1000.0f, 1250.0f, 1600.0f, 2000.0f, 2500.0f, 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f,
            10000.0f, 12500.0f, 16000.0f };
        static constexpr int numSideBands = 12;   // side energy only matters in the low end (<= 400 Hz)

        std::array<float, numBands> bandDb {};       // average power per band (mono), dB
        std::array<float, numBands> transDb {};      // transient energy per band (fast-over-slow envelope), dB
        std::array<float, numSideBands> sideDb {};   // side power in the low bands, dB
        float rmsDb = -100, peakDb = -100, crestDb = 0;   // broadband; peak = mean of 50 ms window peaks
        float lowRmsDb = -100;                          // < 120 Hz
        float correlation = 1, sideToMidDb = -100;      // above 200 Hz
        float kickDecayMs = 0; int kickHits = 0;
        float seconds = 0;
    };

    /** LEARN: listens to the plugin input for a few seconds of real audio and measures the track.
        Start/cancel from the message thread; process() on the audio thread; results read after isDone(). */
    class LearnAnalyzer
    {
    public:
        static constexpr float learnSeconds = 8.0f;

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            for (int b = 0; b < LearnStats::numBands; ++b)
            {
                const double f = std::min ((double) LearnStats::freqs[(size_t) b], sr * 0.45);
                bands[(size_t) b].setup (Svf::Mode::bandpass, sr, f, 4.3);
                if (b < LearnStats::numSideBands) sideBands[(size_t) b].setup (Svf::Mode::bandpass, sr, f, 4.3);
                envFast[(size_t) b].setup (0.001, 0.03, sr);
                envSlow[(size_t) b].setup (0.03, 0.3, sr);
            }
            lowLp1.setup (Svf::Mode::lowpass, sr, 120.0, 0.7071); lowLp2 = lowLp1;
            hpL.setup (Svf::Mode::highpass, sr, 200.0, 0.7071); hpR = hpL;
            kickFast.setup (0.0005, 0.03, sr);
            kickSlow.setup (0.03, 0.2, sr);
            state.store (idle);
        }

        // ---- message thread
        void start (int requester) { who.store (requester); progress.store (0.0f); state.store (starting); }
        void cancel()              { state.store (idle); progress.store (0.0f); }
        bool isListening() const   { const int s = state.load(); return s == starting || s == listening; }
        bool isDone() const        { return state.load() == done; }
        int  requester() const     { return who.load(); }
        float getProgress() const  { return progress.load(); }
        bool hasSignal() const     { return signalNow.load(); }
        /** Valid only after isDone(); call acknowledge() once the result has been used. */
        const LearnStats& getStats() const { return result; }
        void acknowledge()         { state.store (idle); }

        // ---- audio thread
        void process (const float* const* in, int numCh, int n) noexcept
        {
            int s = state.load();
            if (s == starting) { resetAccumulators(); state.store (listening); s = listening; }
            if (s != listening || n <= 0 || numCh <= 0) return;

            const bool stereo = numCh > 1;
            double blockPow = 0;
            for (int i = 0; i < n; ++i)
            {
                const float l = in[0][i], r = stereo ? in[1][i] : l;
                const float m = 0.5f * (l + r), sd = 0.5f * (l - r);
                blockPow += (double) m * m;
                winPeak = std::max (winPeak, std::max (std::abs (l), std::abs (r)));
                if (++winCount >= (int) (0.05 * sr)) { if (winPeak > 1.0e-4f) { peakSum += gainToDb (winPeak); ++peakN; } winPeak = 0; winCount = 0; }

                if (! active) continue;   // decided per block below; first block of silence is skipped anyway

                for (int b = 0; b < LearnStats::numBands; ++b)
                {
                    const float y = bands[(size_t) b].process (m);
                    pow[(size_t) b] += (double) y * y;
                    const float a = std::abs (y);
                    const float fast = envFast[(size_t) b].process (a), slow = envSlow[(size_t) b].process (a);
                    const float t = std::max (0.0f, fast - slow);
                    trans[(size_t) b] += (double) t * t;
                }
                for (int b = 0; b < LearnStats::numSideBands; ++b)
                {
                    const float y = sideBands[(size_t) b].process (sd);
                    sidePow[(size_t) b] += (double) y * y;
                }
                const float lo = lowLp2.process (lowLp1.process (m));
                lowPow += (double) lo * lo;
                const float hl = hpL.process (l), hr = hpR.process (r);
                sLR += (double) hl * hr; sLL += (double) hl * hl; sRR += (double) hr * hr;
                sMid += 0.25 * (double) (hl + hr) * (hl + hr); sSide += 0.25 * (double) (hl - hr) * (hl - hr);
                totPow += (double) m * m;
                ++count;

                // kick hits and their decay (time for the low envelope to fall 12 dB below the hit's peak)
                const float k = std::abs (lo), kf = kickFast.process (k), ks = kickSlow.process (k);
                if (sinceHit > (int) (0.09 * sr) && kf > 0.02f && kf > ks * 1.8f) { sinceHit = 0; hitPeak = kf; inHit = true; }
                else ++sinceHit;
                if (inHit)
                {
                    hitPeak = std::max (hitPeak, kf);
                    if (kf < hitPeak * 0.251f || sinceHit > (int) (0.4 * sr))
                    {
                        decaySum += 1000.0 * sinceHit / sr; ++hits; inHit = false;
                    }
                }
            }

            // only count blocks with real audio (above -50 dBFS) towards the learn time
            const float blockDb = powerToDb ((float) (blockPow / n));
            const bool sig = blockDb > -50.0f;
            signalNow.store (sig);
            active = sig;
            if (sig) activeSamples += n;
            progress.store (std::min (1.0f, (float) (activeSamples / (learnSeconds * sr))));
            if (activeSamples >= learnSeconds * sr && count > 0)
            {
                finish();
                state.store (done);
            }
        }

    private:
        enum { idle = 0, starting, listening, done };

        void resetAccumulators() noexcept
        {
            for (auto& b : bands) b.reset();
            for (auto& b : sideBands) b.reset();
            for (auto& e : envFast) e.reset();
            for (auto& e : envSlow) e.reset();
            lowLp1.reset(); lowLp2.reset(); hpL.reset(); hpR.reset(); kickFast.reset(); kickSlow.reset();
            pow.fill (0); trans.fill (0); sidePow.fill (0);
            lowPow = totPow = sLR = sLL = sRR = sMid = sSide = 0;
            peakSum = 0; peakN = 0; winPeak = 0; winCount = 0;
            decaySum = 0; hits = 0; sinceHit = 1 << 30; inHit = false; hitPeak = 0;
            count = 0; activeSamples = 0; active = false;
        }

        void finish() noexcept
        {
            const double c = (double) count;
            for (int b = 0; b < LearnStats::numBands; ++b)
            {
                result.bandDb[(size_t) b] = powerToDb ((float) (pow[(size_t) b] / c));
                result.transDb[(size_t) b] = powerToDb ((float) (trans[(size_t) b] / c));
            }
            for (int b = 0; b < LearnStats::numSideBands; ++b)
                result.sideDb[(size_t) b] = powerToDb ((float) (sidePow[(size_t) b] / c));
            result.rmsDb = powerToDb ((float) (totPow / c));
            result.peakDb = peakN > 0 ? (float) (peakSum / peakN) : -100.0f;
            result.crestDb = result.peakDb - result.rmsDb;
            result.lowRmsDb = powerToDb ((float) (lowPow / c));
            result.correlation = sLL + sRR > 1.0e-12 ? (float) (sLR / std::sqrt (sLL * sRR + 1.0e-24)) : 1.0f;
            result.sideToMidDb = powerToDb ((float) (sSide / c)) - powerToDb ((float) (sMid / c));
            result.kickHits = hits;
            result.kickDecayMs = hits > 0 ? (float) (decaySum / hits) : 0.0f;
            result.seconds = (float) (activeSamples / sr);
        }

        double sr = 48000.0;
        std::atomic<int> state { idle }, who { -1 };
        std::atomic<float> progress { 0.0f };
        std::atomic<bool> signalNow { false };

        std::array<Svf, LearnStats::numBands> bands;
        std::array<Svf, LearnStats::numSideBands> sideBands;
        std::array<Envelope, LearnStats::numBands> envFast, envSlow;
        Svf lowLp1, lowLp2, hpL, hpR;
        Envelope kickFast, kickSlow;

        std::array<double, LearnStats::numBands> pow {}, trans {};
        std::array<double, LearnStats::numSideBands> sidePow {};
        double lowPow = 0, totPow = 0, sLR = 0, sLL = 0, sRR = 0, sMid = 0, sSide = 0;
        double peakSum = 0; int peakN = 0; float winPeak = 0; int winCount = 0;
        double decaySum = 0; int hits = 0, sinceHit = 1 << 30; bool inHit = false; float hitPeak = 0;
        std::int64_t count = 0; double activeSamples = 0; bool active = false;

        LearnStats result;
    };
}

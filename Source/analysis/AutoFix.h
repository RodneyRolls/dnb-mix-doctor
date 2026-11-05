#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>
#include "../dsp/DspUtils.h"
#include "../dsp/LoudnessMeter.h"

namespace mcdsp
{
    /** Listens to the incoming mix for N seconds on the audio thread (allocation-free) and measures:
        octave-band spectral balance, low-end stereo correlation, side/mid ratio, kick/bass overlap,
        transient density, crest factor and integrated loudness. */
    class AutoFixAnalyser
    {
    public:
        static constexpr int numBands = 10;
        static constexpr std::array<double, numBands> bandFreqs { 31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

        enum State { idle = 0, listening = 1, done = 2 };

        struct Result
        {
            std::array<float, numBands> bandDb {};   // energy share per octave band, dB (sums to 0 dB)
            float lowCorrelation = 1;                // L/R correlation below 120 Hz
            float sideToMidDb = -12;                 // S/M energy above 300 Hz
            float kickOverlap = 0;                   // low-band tail power after kicks / power during kicks
            float kicksPerSecond = 0, onsetsPerSecond = 0;
            float crestDb = 12;
            float lufs = -20;
        };

        void prepare (double sampleRate);

        // ---- UI thread
        void start (double seconds) noexcept { requestedSeconds.store (seconds); startRequested.store (true); }
        void cancel() noexcept               { cancelRequested.store (true); }
        int getState() const noexcept        { return state.load (std::memory_order_acquire); }
        float getProgress() const noexcept   { return progress.load(); }
        Result getResult() const noexcept    { return result; }    // valid once getState() == done
        void acknowledge() noexcept          { state.store (idle); }

        // ---- audio thread
        void process (const float* const* d, int numCh, int n) noexcept;

    private:
        void resetMeasurements() noexcept;
        void finish() noexcept;

        double sr = 48000.0;
        std::atomic<bool> startRequested { false }, cancelRequested { false };
        std::atomic<double> requestedSeconds { 11.0 };
        std::atomic<int> state { idle };
        std::atomic<float> progress { 0.0f };
        Result result;

        std::array<Svf, numBands> bands;
        std::array<double, numBands> bandE {};
        Svf lowL, lowR, hpL, hpR;
        double lowLR = 0, lowLL = 0, lowRR = 0, midE = 0, sideE = 0;
        Envelope kFast, kSlow, oFast, oSlow;
        int sinceKick = 1 << 30, sinceOnset = 1 << 30, kicks = 0, onsets = 0;
        double kickE = 0, tailE = 0; long long kickN = 0, tailN = 0;
        double sumSq = 0; float peak = 0;
        long long activeSamples = 0, targetSamples = 1;
        LoudnessMeter loud;
    };

    /** Turns a measurement into suggested knob values (real parameter units). UI thread. */
    std::vector<std::pair<juce::String, float>> suggestSettings (const AutoFixAnalyser::Result&, double bpm);

    /** Built-in DnB reference balance (octave bands, dB share of total energy). */
    std::array<float, AutoFixAnalyser::numBands> dnbReferenceCurve();
}

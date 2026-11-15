#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LearnAnalyzer.h"
#include "../AdvancedParams.h"

/** Turns what LEARN measured into settings. Runs on the message thread. */
namespace learnmap
{
    using mcdsp::LearnStats;

    struct Result
    {
        std::vector<std::pair<juce::String, float>> values;   // parameter id -> real (unnormalised) value
        juce::String summary;
    };

    inline int bandIndex (float hz)
    {
        int best = 0;
        for (int b = 1; b < LearnStats::numBands; ++b)
            if (std::abs (std::log (LearnStats::freqs[(size_t) b] / hz)) < std::abs (std::log (LearnStats::freqs[(size_t) best] / hz))) best = b;
        return best;
    }

    /** How far each band sticks out above its neighbours (two either side), dB. */
    inline std::array<float, LearnStats::numBands> excess (const LearnStats& s)
    {
        std::array<float, LearnStats::numBands> e {};
        for (int b = 0; b < LearnStats::numBands; ++b)
        {
            float sum = 0; int n = 0;
            for (int k = b - 2; k <= b + 2; ++k)
                if (k != b && k >= 0 && k < LearnStats::numBands) { sum += s.bandDb[(size_t) k]; ++n; }
            e[(size_t) b] = n > 0 ? s.bandDb[(size_t) b] - sum / (float) n : 0.0f;
        }
        return e;
    }

    /** Index of the largest value of v in [loHz, hiHz]. */
    template <typename Arr>
    inline int peakIn (const Arr& v, float loHz, float hiHz, int skip = -1)
    {
        int best = -1;
        for (int b = 0; b < LearnStats::numBands; ++b)
        {
            const float f = LearnStats::freqs[(size_t) b];
            if (f < loHz || f > hiHz || b == skip) continue;
            if (best < 0 || v[(size_t) b] > v[(size_t) best]) best = b;
        }
        return best;
    }

    inline juce::String hzText (float f) { return advparams::format (advparams::Unit::hz, f); }
    inline float lerpClamp (float x, float x0, float x1, float y0, float y1)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - x0) / (x1 - x0));
        return y0 + (y1 - y0) * t;
    }

    /** Settings for one ADVANCED page. */
    inline Result forPage (const juce::String& knobId, const LearnStats& s, float inputDb, float glueAmount)
    {
        Result r;
        auto set = [&r] (const char* id, float v) { r.values.push_back ({ id, v }); };
        const auto ex = excess (s);
        const float crest = s.crestDb;

        if (knobId == "subMono")
        {
            // highest low band whose side (stereo) energy is within 12 dB of its mono energy
            float top = 0.0f;
            for (int b = 0; b < LearnStats::numSideBands; ++b)
            {
                const float f = LearnStats::freqs[(size_t) b];
                if (f < 40.0f || f > 250.0f) continue;
                if (s.sideDb[(size_t) b] - s.bandDb[(size_t) b] > -12.0f && s.bandDb[(size_t) b] > s.rmsDb - 40.0f) top = f;
            }
            const float fc = top > 0.0f ? juce::jlimit (60.0f, 220.0f, top * 1.25f) : 80.0f;
            set ("subMono", fc);
            set ("adv_monoAmount", 100.0f);
            r.summary = top > 0.0f ? "Stereo bass found up to " + hzText (top) + " - crossover set to " + hzText (fc)
                                   : "Your bass is already mono - crossover set low to " + hzText (fc) + " to keep the width";
        }
        else if (knobId == "subTight")
        {
            const float thr = juce::jlimit (-40.0f, 0.0f, s.lowRmsDb + inputDb + 4.0f);
            const float rel = s.kickHits > 2 ? juce::jlimit (100.0f, 700.0f, s.kickDecayMs * 3.0f) : 300.0f;
            set ("adv_tightThr", thr);
            set ("adv_tightRel", rel);
            set ("adv_tightMax", 12.0f);
            r.summary = "Low end averages " + juce::String (s.lowRmsDb + inputDb, 1) + " dB - threshold " + juce::String (thr, 1)
                      + " dB, release " + juce::String (juce::roundToInt (rel)) + " ms";
        }
        else if (knobId == "kickBass")
        {
            const int fund = peakIn (s.transDb, 40.0f, 125.0f);
            const float kickHz = fund >= 0 ? LearnStats::freqs[(size_t) fund] : 60.0f;
            const float split = juce::jlimit (80.0f, 200.0f, kickHz * 2.0f);
            const int body = peakIn (ex, 150.0f, 400.0f);
            const float bodyHz = body >= 0 ? LearnStats::freqs[(size_t) body] : 220.0f;
            const float decay = s.kickHits > 2 ? juce::jlimit (20.0f, 200.0f, s.kickDecayMs * 0.6f) : 60.0f;
            set ("adv_kbSplit", split); set ("adv_kbBody", bodyHz); set ("adv_kbDecay", decay); set ("adv_kbDuck", 9.0f);
            r.summary = "Kick around " + hzText (kickHz) + (s.kickHits > 2 ? ", " + juce::String (s.kickHits) + " hits" : juce::String())
                      + " - split " + hzText (split) + ", bass body " + hzText (bodyHz) + ", duck decay " + juce::String (juce::roundToInt (decay)) + " ms";
        }
        else if (knobId == "punch")
        {
            // squashed tracks (low crest) need a longer window and more boost; dynamic ones need less
            const float window = lerpClamp (crest, 8.0f, 16.0f, 22.0f, 8.0f);
            const float maxDb = lerpClamp (crest, 8.0f, 16.0f, 12.0f, 6.0f);
            set ("adv_punchWindow", window); set ("adv_punchMax", maxDb); set ("adv_punchRel", 40.0f);
            r.summary = "Crest factor " + juce::String (crest, 1) + " dB (" + (crest < 10.0f ? "already squashed" : crest > 14.0f ? "very dynamic" : "healthy")
                      + ") - window " + juce::String (window, 1) + " ms, max boost " + juce::String (maxDb, 1) + " dB";
        }
        else if (knobId == "snap")
        {
            const int b = peakIn (s.transDb, 150.0f, 2500.0f);
            const float f = b >= 0 ? LearnStats::freqs[(size_t) b] : 900.0f;
            set ("adv_snapHz", f); set ("adv_snapQ", 0.7f); set ("adv_snapMax", 9.0f);
            r.summary = "Snare transient strongest around " + hzText (f) + " - SNAP tuned there";
        }
        else if (knobId == "mud")
        {
            const int b = peakIn (ex, 125.0f, 630.0f);
            const float e = b >= 0 ? ex[(size_t) b] : 0.0f;
            const float f = b >= 0 && e > 0.8f ? LearnStats::freqs[(size_t) b] : 320.0f;
            const bool wide = b > 0 && b < LearnStats::numBands - 1 && ex[(size_t) (b - 1)] > 0.5f * e && ex[(size_t) (b + 1)] > 0.5f * e;
            const float q = wide ? 0.7f : 1.2f;
            const float cut = juce::jlimit (3.0f, 12.0f, e * 1.5f + 3.0f);
            set ("adv_mudHz", f); set ("adv_mudQ", q); set ("adv_mudMax", cut);
            r.summary = e > 0.8f ? "Low-mid build-up at " + hzText (f) + " (+" + juce::String (e, 1) + " dB) - cut tuned there, up to " + juce::String (cut, 1) + " dB"
                                 : "No clear mud build-up found - MUD left at " + hzText (f);
        }
        else if (knobId == "reese")
        {
            int a = peakIn (ex, 250.0f, 4000.0f), b = peakIn (ex, 250.0f, 4000.0f, a);
            int c = -1;
            for (int k = 0; k < LearnStats::numBands; ++k)
            {
                const float f = LearnStats::freqs[(size_t) k];
                if (f < 250.0f || f > 4000.0f || k == a || k == b) continue;
                if (c < 0 || ex[(size_t) k] > ex[(size_t) c]) c = k;
            }
            float lo = 1.0e9f, hi = 0.0f, top = 0.0f;
            for (int k : { a, b, c })
                if (k >= 0 && ex[(size_t) k] > 0.5f)
                { lo = std::min (lo, LearnStats::freqs[(size_t) k]); hi = std::max (hi, LearnStats::freqs[(size_t) k]); top = std::max (top, ex[(size_t) k]); }
            if (hi <= 0.0f) { lo = 350.0f; hi = 2800.0f; }
            lo = juce::jlimit (150.0f, 1500.0f, lo * 0.85f);
            hi = juce::jlimit (800.0f, 6000.0f, std::max (hi * 1.15f, lo * 2.5f));
            const float cut = juce::jlimit (4.0f, 12.0f, top * 1.5f + 3.0f);
            set ("adv_reeseLow", lo); set ("adv_reeseHigh", hi); set ("adv_reeseQ", 2.0f); set ("adv_reeseMax", cut);
            r.summary = top > 0.5f ? "Resonances found between " + hzText (lo) + " and " + hzText (hi) + " - notches spread across them"
                                   : "No strong resonances found - notches kept on the default range";
        }
        else if (knobId == "harsh")
        {
            const int a = peakIn (ex, 2000.0f, 10000.0f), b = peakIn (ex, 2000.0f, 10000.0f, a);
            float f1 = a >= 0 ? LearnStats::freqs[(size_t) a] : 3200.0f, f2 = b >= 0 ? LearnStats::freqs[(size_t) b] : 6000.0f;
            if (f1 > f2) std::swap (f1, f2);
            if (f2 < f1 * 1.3f) f2 = f1 * 1.8f;
            f1 = juce::jlimit (1500.0f, 8000.0f, f1); f2 = juce::jlimit (3000.0f, 14000.0f, f2);
            const float e = a >= 0 ? ex[(size_t) a] : 0.0f;
            const float cut = juce::jlimit (4.0f, 12.0f, e * 1.5f + 4.0f);
            set ("adv_harsh1", f1); set ("adv_harsh2", f2); set ("adv_harshMax", cut);
            r.summary = "Harshest frequencies around " + hzText (f1) + " and " + hzText (f2) + " - cuts tuned there";
        }
        else if (knobId == "air")
        {
            float ref = 0; int n = 0;
            for (int b = 0; b < LearnStats::numBands; ++b)
                if (LearnStats::freqs[(size_t) b] >= 1000.0f && LearnStats::freqs[(size_t) b] <= 4000.0f) { ref += s.bandDb[(size_t) b]; ++n; }
            ref /= (float) std::max (1, n);
            float roll = 0.0f, top = 0; int tn = 0;
            for (int b = 0; b < LearnStats::numBands; ++b)
            {
                const float f = LearnStats::freqs[(size_t) b];
                if (f >= 5000.0f && roll <= 0.0f && s.bandDb[(size_t) b] < ref - 12.0f) roll = f;
                if (f >= 10000.0f) { top += s.bandDb[(size_t) b]; ++tn; }
            }
            const float dull = ref - top / (float) std::max (1, tn);
            const float hz = roll > 0.0f ? juce::jlimit (6000.0f, 14000.0f, roll * 0.8f) : 12000.0f;
            const float boost = juce::jlimit (2.0f, 9.0f, (dull - 8.0f) / 2.0f + 3.0f);
            set ("adv_airHz", hz); set ("adv_airMax", boost); set ("adv_airExcite", dull > 20.0f ? 70.0f : 40.0f);
            r.summary = "Top end sits " + juce::String (dull, 1) + " dB under the mids" + (roll > 0.0f ? ", rolling off from " + hzText (roll) : juce::String())
                      + " - shelf at " + hzText (hz) + ", up to +" + juce::String (boost, 1) + " dB";
        }
        else if (knobId == "width")
        {
            // aim for side 9 dB under mid above 200 Hz: wide, but still solid in mono
            const float sm = s.sideToMidDb;
            const float w = sm > -60.0f ? juce::jlimit (60.0f, 160.0f, 100.0f * std::pow (10.0f, (-9.0f - sm) / 20.0f)) : 100.0f;
            const float guard = s.correlation < 0.3f ? 80.0f : 40.0f;
            set ("width", w); set ("adv_widthGuard", guard);
            r.summary = "Side is " + juce::String (-sm, 1) + " dB under mid, correlation " + juce::String (s.correlation, 2)
                      + " - WIDTH " + juce::String (juce::roundToInt (w)) + " %, mono guard " + juce::String (juce::roundToInt (guard)) + " %";
        }
        else if (knobId == "glue")
        {
            const float thr0 = -6.0f - 18.0f * juce::jlimit (0.0f, 1.0f, glueAmount);
            const float target = s.peakDb + inputDb - 6.0f;   // sit ~6 dB under the typical peaks
            const float off = juce::jlimit (-24.0f, 24.0f, target - thr0);
            const float ratioMul = lerpClamp (crest, 8.0f, 16.0f, 0.8f, 1.3f);
            const float attack = lerpClamp (crest, 8.0f, 16.0f, 8.0f, 25.0f);
            set ("adv_glueThr", off); set ("adv_glueRatio", ratioMul); set ("adv_glueKnee", 6.0f); set ("attack", attack);
            r.summary = "Peaks average " + juce::String (s.peakDb + inputDb, 1) + " dBFS, crest " + juce::String (crest, 1) + " dB - threshold "
                      + juce::String (thr0 + off, 1) + " dB, ratio x" + juce::String (ratioMul, 2) + ", attack " + juce::String (juce::roundToInt (attack)) + " ms";
        }
        else if (knobId == "saturate")
        {
            float lowMid = 0, high = 0; int a = 0, b = 0;
            for (int k = 0; k < LearnStats::numBands; ++k)
            {
                const float f = LearnStats::freqs[(size_t) k];
                if (f >= 200.0f && f <= 1000.0f) { lowMid += s.bandDb[(size_t) k]; ++a; }
                if (f >= 4000.0f && f <= 12500.0f) { high += s.bandDb[(size_t) k]; ++b; }
            }
            const float tilt = high / (float) std::max (1, b) - lowMid / (float) std::max (1, a);   // bright -> less negative
            const float drive = lerpClamp (crest, 8.0f, 16.0f, 8.0f, 30.0f);
            const float warmth = lerpClamp (tilt, -24.0f, -8.0f, 35.0f, 70.0f);
            set ("saturate", drive); set ("adv_satWarmth", warmth);
            r.summary = "Crest " + juce::String (crest, 1) + " dB, " + (tilt > -14.0f ? "bright" : "warm") + " balance - drive "
                      + juce::String (juce::roundToInt (drive)) + " %, warmth " + juce::String (juce::roundToInt (warmth)) + " %";
        }
        else if (knobId == "clipper")
        {
            const float amount = lerpClamp (crest, 8.0f, 16.0f, 15.0f, 65.0f);
            const float soft = lerpClamp (crest, 8.0f, 16.0f, 70.0f, 50.0f);
            set ("clipper", amount); set ("adv_clipSoft", soft);
            r.summary = "Crest " + juce::String (crest, 1) + " dB (" + (crest > 13.0f ? "peaky - room for clipping" : "already controlled") + ") - CLIPPER "
                      + juce::String (juce::roundToInt (amount)) + " %, softness " + juce::String (juce::roundToInt (soft)) + " %";
        }
        return r;
    }

    //==============================================================================
    /** EQ LEARN: genre target curves (rough tonal balance, dB relative to 1 kHz, at the 16 EQ band frequencies). */
    struct Genre { const char* name; std::array<float, 16> curve; };
    inline const std::vector<Genre>& genres()
    {
        //                         25    40    63   100   160   250   400   630    1k  1.6k  2.5k    4k  6.3k   10k   14k   18k
        static const std::vector<Genre> g {
            { "Drum & Bass",  {  8,   10,    9,    6,    3,    1,   -1,   -1,    0,   -1,   -2,   -3,   -5,   -8,  -11,  -16 } },
            { "House",        {  5,    9,    9,    6,    3,    1,    0,    0,    0,   -1,   -2,   -3,   -5,   -7,  -10,  -15 } },
            { "Techno",       {  4,    9,    9,    6,    3,    1,    0,    0,    0,   -1,   -2,   -4,   -6,   -8,  -11,  -16 } },
            { "Dubstep",      {  9,   11,    9,    5,    2,    0,   -1,   -1,    0,   -1,   -2,   -3,   -4,   -7,  -10,  -15 } },
            { "Hip-Hop",      {  7,   10,    9,    6,    3,    1,    0,    0,    0,   -1,   -3,   -4,   -6,   -9,  -12,  -17 } },
            { "Pop",          {  2,    6,    7,    5,    3,    1,    0,    0,    0,    0,   -1,   -2,   -4,   -6,   -9,  -14 } },
        };
        return g;
    }

    /** Measured 1/3-octave spectrum interpolated (in log frequency) to a frequency, dB. */
    inline float spectrumAt (const LearnStats& s, float hz)
    {
        const auto& f = LearnStats::freqs;
        if (hz <= f.front()) return s.bandDb.front();
        if (hz >= f.back())  return s.bandDb.back();
        for (int b = 1; b < LearnStats::numBands; ++b)
            if (hz <= f[(size_t) b])
            {
                const float t = std::log (hz / f[(size_t) (b - 1)]) / std::log (f[(size_t) b] / f[(size_t) (b - 1)]);
                return s.bandDb[(size_t) (b - 1)] + t * (s.bandDb[(size_t) b] - s.bandDb[(size_t) (b - 1)]);
            }
        return s.bandDb.back();
    }

    /** 16 EQ band gains that move the track part of the way towards the genre's tonal balance. Level-neutral
        through the mids, gentle (60 % of the difference, +/-6 dB, smoothed across neighbours), and the very top
        and bottom bands never boost more than +3 dB. */
    inline std::array<float, 16> eqForGenre (const LearnStats& s, int genreIndex, const std::array<float, 16>& eqFreqs)
    {
        const auto& target = genres()[(size_t) juce::jlimit (0, (int) genres().size() - 1, genreIndex)].curve;
        const float ref = spectrumAt (s, 1000.0f);
        std::array<float, 16> diff {};
        float mean = 0; int n = 0;
        for (size_t i = 0; i < 16; ++i)
        {
            diff[i] = target[i] - (spectrumAt (s, eqFreqs[i]) - ref);
            if (eqFreqs[i] >= 100.0f && eqFreqs[i] <= 6300.0f) { mean += diff[i]; ++n; }
        }
        mean /= (float) std::max (1, n);
        std::array<float, 16> g {};
        for (size_t i = 0; i < 16; ++i)
        {
            const float l = diff[i > 0 ? i - 1 : i], c = diff[i], r = diff[i < 15 ? i + 1 : i];
            float v = 0.6f * ((0.25f * l + 0.5f * c + 0.25f * r) - mean);
            v = juce::jlimit (-6.0f, 6.0f, v);
            if (i == 0 || i == 15) v = std::min (v, 3.0f);
            g[i] = std::round (v * 10.0f) / 10.0f;
        }
        return g;
    }
}

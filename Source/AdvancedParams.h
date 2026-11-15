#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/MixEngine.h"

/** The per-function ADVANCED pages (double-click a knob).

    One table drives everything: the host parameters, the engine mapping, the page layout and LEARN.
    Every default equals the value the engine used before these pages existed, so untouched pages sound
    exactly like the plain knobs. */
namespace advparams
{
    enum class Unit { hz, ms, db, pct, ratio, q };

    struct Def
    {
        const char* id;           // parameter id
        const char* name;         // host name
        const char* label;        // short label on the page
        float lo, hi, def, skewCentre;   // skewCentre <= 0 -> linear
        Unit unit;
        float mcdsp::EngineParams::Advanced::* field;
        float scale;              // parameter value * scale -> engine value (pct -> 0..1)
    };

    using A = mcdsp::EngineParams::Advanced;
    inline const std::vector<Def>& defs()
    {
        static const std::vector<Def> d {
            { "adv_monoAmount",  "Sub Mono Amount",     "AMOUNT",     0.0f,   100.0f,  100.0f,   0.0f, Unit::pct,   &A::monoAmount,      0.01f },
            { "adv_tightThr",    "Sub Tight Threshold", "THRESHOLD", -40.0f,    0.0f,  -18.0f,   0.0f, Unit::db,    &A::tightThrDb,      1.0f },
            { "adv_tightMax",    "Sub Tight Range",     "RANGE",      0.0f,    24.0f,   12.0f,   0.0f, Unit::db,    &A::tightMaxDb,      1.0f },
            { "adv_tightRel",    "Sub Tight Release",   "RELEASE",   50.0f,  1000.0f,  300.0f, 300.0f, Unit::ms,    &A::tightReleaseMs,  1.0f },
            { "adv_kbSplit",     "Kick/Bass Split",     "SPLIT",     60.0f,   250.0f,  120.0f, 120.0f, Unit::hz,    &A::kbSplitHz,       1.0f },
            { "adv_kbDuck",      "Kick/Bass Duck",      "DUCK",       0.0f,    18.0f,    9.0f,   0.0f, Unit::db,    &A::kbDuckDb,        1.0f },
            { "adv_kbDecay",     "Kick/Bass Decay",     "DECAY",     10.0f,   300.0f,   60.0f,  60.0f, Unit::ms,    &A::kbDecayMs,       1.0f },
            { "adv_kbBody",      "Kick/Bass Body",      "BODY",     100.0f,   500.0f,  220.0f, 220.0f, Unit::hz,    &A::kbBodyHz,        1.0f },
            { "adv_punchWindow", "Punch Window",        "WINDOW",     2.0f,    60.0f,   15.0f,  15.0f, Unit::ms,    &A::punchWindowMs,   1.0f },
            { "adv_punchRel",    "Punch Release",       "RELEASE",   10.0f,   200.0f,   40.0f,  40.0f, Unit::ms,    &A::punchReleaseMs,  1.0f },
            { "adv_punchMax",    "Punch Max Boost",     "MAX BOOST",  0.0f,    18.0f,   10.0f,   0.0f, Unit::db,    &A::punchMaxDb,      1.0f },
            { "adv_snapHz",      "Snap Frequency",      "FREQ",     150.0f,  5000.0f,  900.0f, 900.0f, Unit::hz,    &A::snapHz,          1.0f },
            { "adv_snapQ",       "Snap Width",          "Q",          0.2f,     4.0f,    0.5f,   1.0f, Unit::q,     &A::snapQ,           1.0f },
            { "adv_snapMax",     "Snap Max Boost",      "MAX BOOST",  0.0f,    18.0f,    9.0f,   0.0f, Unit::db,    &A::snapMaxDb,       1.0f },
            { "adv_mudHz",       "Mud Frequency",       "FREQ",     100.0f,   800.0f,  320.0f, 300.0f, Unit::hz,    &A::mudHz,           1.0f },
            { "adv_mudQ",        "Mud Width",           "Q",          0.3f,     6.0f,    0.9f,   1.5f, Unit::q,     &A::mudQ,            1.0f },
            { "adv_mudMax",      "Mud Max Cut",         "MAX CUT",    0.0f,    18.0f,    9.0f,   0.0f, Unit::db,    &A::mudMaxDb,        1.0f },
            { "adv_reeseLow",    "Reese Low",           "LOW",      150.0f,  1500.0f,  350.0f, 400.0f, Unit::hz,    &A::reeseLowHz,      1.0f },
            { "adv_reeseHigh",   "Reese High",          "HIGH",     800.0f,  6000.0f, 2800.0f, 2500.0f, Unit::hz,   &A::reeseHighHz,     1.0f },
            { "adv_reeseQ",      "Reese Width",         "Q",          0.5f,     8.0f,    2.0f,   2.0f, Unit::q,     &A::reeseQ,          1.0f },
            { "adv_reeseMax",    "Reese Max Cut",       "MAX CUT",    0.0f,    18.0f,    8.0f,   0.0f, Unit::db,    &A::reeseMaxDb,      1.0f },
            { "adv_harsh1",      "Harsh Freq 1",        "FREQ 1",  1500.0f,  8000.0f, 3200.0f, 3500.0f, Unit::hz,   &A::harshHz1,        1.0f },
            { "adv_harsh2",      "Harsh Freq 2",        "FREQ 2",  3000.0f, 14000.0f, 6000.0f, 6500.0f, Unit::hz,   &A::harshHz2,        1.0f },
            { "adv_harshMax",    "Harsh Max Cut",       "MAX CUT",    0.0f,    18.0f,    9.0f,   0.0f, Unit::db,    &A::harshMaxDb,      1.0f },
            { "adv_airHz",       "Air Frequency",       "FREQ",    4000.0f, 18000.0f, 10000.0f, 9000.0f, Unit::hz,  &A::airHz,           1.0f },
            { "adv_airMax",      "Air Max Boost",       "MAX BOOST",  0.0f,    12.0f,    6.0f,   0.0f, Unit::db,    &A::airMaxDb,        1.0f },
            { "adv_airExcite",   "Air Exciter",         "EXCITER",    0.0f,   100.0f,   50.0f,   0.0f, Unit::pct,   &A::airExcite,       0.01f },
            { "adv_widthGuard",  "Width Mono Guard",    "MONO GUARD", 0.0f,   100.0f,    0.0f,   0.0f, Unit::pct,   &A::widthGuard,      0.01f },
            { "adv_glueThr",     "Glue Threshold",      "THRESHOLD",-24.0f,    24.0f,    0.0f,   0.0f, Unit::db,    &A::glueThrOffsetDb, 1.0f },
            { "adv_glueRatio",   "Glue Ratio",          "RATIO",      0.25f,    4.0f,    1.0f,   1.0f, Unit::ratio, &A::glueRatioMul,    1.0f },
            { "adv_glueKnee",    "Glue Knee",           "KNEE",       0.0f,    18.0f,    6.0f,   0.0f, Unit::db,    &A::glueKneeDb,      1.0f },
            { "adv_satWarmth",   "Saturate Warmth",     "WARMTH",     0.0f,   100.0f,   50.0f,   0.0f, Unit::pct,   &A::satWarmth,       0.01f },
            { "adv_clipSoft",    "Clipper Softness",    "SOFTNESS",   5.0f,    95.0f,   60.0f,   0.0f, Unit::pct,   &A::clipSoftness,    0.01f },
        };
        return d;
    }

    inline const Def* find (const juce::String& id)
    {
        for (auto& d : defs()) if (id == d.id) return &d;
        return nullptr;
    }

    inline juce::String format (Unit u, float v)
    {
        using juce::String;
        switch (u)
        {
            case Unit::hz:    return v >= 1000.0f ? String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz" : String (juce::roundToInt (v)) + " Hz";
            case Unit::ms:    return v < 10.0f ? String (v, 1) + " ms" : String (juce::roundToInt (v)) + " ms";
            case Unit::db:    return (v > 0.0f ? "+" : "") + String (v, 1) + " dB";
            case Unit::pct:   return String (juce::roundToInt (v)) + " %";
            case Unit::ratio: return "x" + String (v, 2);
            case Unit::q:     return String (v, 2);
        }
        return String (v);
    }

    inline const char* unitLabel (Unit u)
    {
        switch (u)
        {
            case Unit::hz: return "Hz"; case Unit::ms: return "ms"; case Unit::db: return "dB";
            case Unit::pct: return "%"; case Unit::ratio: return "x"; case Unit::q: return "Q";
        }
        return "";
    }

    /** One ADVANCED page: the knob it opens from, the main knobs shown on it and its advanced settings. */
    struct Page
    {
        const char* knobId;
        const char* title;
        const char* blurb;
        std::vector<const char*> mainIds;   // existing knob parameters shown on the page (first = the knob itself)
        std::vector<const char*> advIds;
        const char* learnHint;
    };

    inline const std::vector<Page>& pages()
    {
        static const std::vector<Page> p {
            { "subMono",  "SUB MONO",   "Makes the bass mono below the crossover so it hits hard on club systems and phones.",
              { "subMono" }, { "adv_monoAmount" },
              "finds how high the stereo bass goes and sets the crossover just above it" },
            { "subTight", "SUB TIGHT",  "Shortens a long sub tail and evens out the low end with gentle compression.",
              { "subTight" }, { "adv_tightThr", "adv_tightMax", "adv_tightRel" },
              "measures the level and tail of your low end and sets threshold and release to match" },
            { "kickBass", "KICK/BASS",  "Ducks the bass for a moment on every kick so the two stop masking each other.",
              { "kickBass" }, { "adv_kbSplit", "adv_kbDuck", "adv_kbDecay", "adv_kbBody" },
              "finds your kick's pitch, its decay time and the bass body frequency" },
            { "punch",    "PUNCH",      "Broadband transient shaper: brings the front of every hit forward.",
              { "punch" }, { "adv_punchWindow", "adv_punchRel", "adv_punchMax" },
              "measures how squashed or dynamic the track is and sets the window and boost to suit" },
            { "snap",     "SNAP",       "Boosts the crack of the snare only while it hits.",
              { "snap" }, { "adv_snapHz", "adv_snapQ", "adv_snapMax" },
              "finds the frequency where your snare's transient is strongest" },
            { "mud",      "MUD",        "Dynamic cut that only digs in when the low-mids build up.",
              { "mud" }, { "adv_mudHz", "adv_mudQ", "adv_mudMax" },
              "finds the low-mid build-up in your track and tunes the cut onto it" },
            { "reese",    "REESE TAME", "Five dynamic notches that catch resonances poking out of reese basses.",
              { "reese" }, { "adv_reeseLow", "adv_reeseHigh", "adv_reeseQ", "adv_reeseMax" },
              "finds the resonant peaks in your mids and spreads the notches across them" },
            { "harsh",    "HARSH",      "Two dynamic cuts that tame piercing upper-mids only when they spike.",
              { "harsh" }, { "adv_harsh1", "adv_harsh2", "adv_harshMax" },
              "finds the two harshest frequencies in your top end" },
            { "air",      "AIR",        "High shelf plus a gentle exciter for sparkle on top.",
              { "air" }, { "adv_airHz", "adv_airMax", "adv_airExcite" },
              "measures where your top end rolls off and how dull it is" },
            { "width",    "WIDTH",      "Stereo width above the SUB MONO crossover, with an optional mono-safety guard.",
              { "width" }, { "adv_widthGuard" },
              "measures your stereo image and sets a wide but mono-safe width" },
            { "glue",     "GLUE",       "Bus compressor that holds the mix together.",
              { "glue", "attack", "release" }, { "adv_glueThr", "adv_glueRatio", "adv_glueKnee" },
              "measures your peak level and dynamics and sets threshold, ratio and attack to suit" },
            { "saturate", "SATURATE",   "Valve-style saturation for weight and loudness.",
              { "saturate" }, { "adv_satWarmth" },
              "sets the drive and warmth to suit how dynamic and bright the track is" },
            { "clipper",  "CLIPPER",    "Soft clipper that shaves peaks before the limiter for extra loudness.",
              { "clipper" }, { "adv_clipSoft" },
              "measures how peaky the track is and sets how hard and how soft the clipper works" },
        };
        return p;
    }

    inline int pageIndexForKnob (const juce::String& knobId)
    {
        for (size_t i = 0; i < pages().size(); ++i) if (knobId == pages()[i].knobId) return (int) i;
        return -1;
    }

    /** Adds every advanced parameter to the layout. */
    inline void addToLayout (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& p, int version)
    {
        using namespace juce;
        for (auto& d : defs())
        {
            NormalisableRange<float> r { d.lo, d.hi, 0.0f };
            if (d.skewCentre > d.lo && d.skewCentre < d.hi) r.setSkewForCentre (d.skewCentre);
            const auto u = d.unit;
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { d.id, version }, d.name, r, d.def,
                AudioParameterFloatAttributes().withLabel (unitLabel (u))
                    .withStringFromValueFunction ([u] (float v, int) { return format (u, v); })));
        }
    }
}

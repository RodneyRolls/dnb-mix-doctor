#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

/** Parameter IDs, ranges and the layout used by the AudioProcessorValueTreeState. */
namespace params
{
    inline constexpr int version = 1;

    // ---- Row 1 : low end & drums
    inline const juce::String input     { "input" };
    inline const juce::String subMono   { "subMono" };
    inline const juce::String subTight  { "subTight" };
    inline const juce::String kickBass  { "kickBass" };
    inline const juce::String punch     { "punch" };
    inline const juce::String snap      { "snap" };
    inline const juce::String mud       { "mud" };
    inline const juce::String reese     { "reese" };
    inline const juce::String harsh     { "harsh" };
    inline const juce::String air       { "air" };
    // ---- Row 2 : glue, width & output
    inline const juce::String glue      { "glue" };
    inline const juce::String attack    { "attack" };
    inline const juce::String release   { "release" };
    inline const juce::String width     { "width" };
    inline const juce::String saturate  { "saturate" };
    inline const juce::String clipper   { "clipper" };
    inline const juce::String loudness  { "loudness" };
    inline const juce::String ceiling   { "ceiling" };
    inline const juce::String fix       { "fix" };
    inline const juce::String mix       { "mix" };
    inline const juce::String output    { "output" };
    // ---- switches
    inline const juce::String releaseAuto { "releaseAuto" };
    inline const juce::String bypass      { "bypass" };
    inline const juce::String oversample  { "oversample" };

    /** The 21 knobs in on-screen order (row 1 then row 2). */
    inline const juce::StringArray knobOrder {
        input, subMono, subTight, kickBass, punch, snap, mud, reese, harsh, air,
        glue, attack, release, width, saturate, clipper, loudness, ceiling, fix, mix, output };

    inline const juce::StringArray knobLabels {
        "INPUT", "SUB MONO", "SUB TIGHT", "KICK/BASS", "PUNCH", "SNAP", "MUD", "REESE TAME", "HARSH", "AIR",
        "GLUE", "ATTACK", "RELEASE", "WIDTH", "SATURATE", "CLIPPER", "LOUDNESS", "CEILING", "FIX AMOUNT", "MIX", "OUTPUT" };

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> p;

        auto dB = [] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; };
        auto pct = [] (float v, int) { return String (roundToInt (v)) + " %"; };
        auto hz = [] (float v, int) { return String (roundToInt (v)) + " Hz"; };
        auto ms = [] (float v, int) { return v < 10.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; };

        auto addFloat = [&] (const String& id, const String& name, NormalisableRange<float> r, float def,
                             const String& label, std::function<String (float, int)> toText)
        {
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, version }, name, r, def,
                AudioParameterFloatAttributes().withLabel (label).withStringFromValueFunction (std::move (toText))));
        };
        auto amount = [&] (const String& id, const String& name, float def)
        { addFloat (id, name, { 0.0f, 100.0f, 0.1f }, def, "%", pct); };

        auto skewed = [] (float lo, float hi, float centre)
        { NormalisableRange<float> r { lo, hi, 0.01f }; r.setSkewForCentre (centre); return r; };

        addFloat (input,   "Input",   { -24.0f, 24.0f, 0.01f }, 0.0f, "dB", dB);
        addFloat (subMono, "Sub Mono", skewed (40.0f, 250.0f, 110.0f), 100.0f, "Hz", hz);
        amount   (subTight, "Sub Tight", 20.0f);
        amount   (kickBass, "Kick/Bass", 20.0f);
        amount   (punch,    "Punch",     20.0f);
        amount   (snap,     "Snap",      15.0f);
        amount   (mud,      "Mud",       25.0f);
        amount   (reese,    "Reese Tame", 20.0f);
        amount   (harsh,    "Harsh",     25.0f);
        amount   (air,      "Air",       20.0f);

        amount   (glue,     "Glue",      25.0f);
        addFloat (attack,  "Attack",  skewed (0.1f, 100.0f, 10.0f), 10.0f, "ms", ms);
        addFloat (release, "Release", skewed (10.0f, 1000.0f, 150.0f), 150.0f, "ms", ms);
        addFloat (width,   "Width",   { 0.0f, 200.0f, 0.1f }, 100.0f, "%", pct);
        amount   (saturate, "Saturate", 15.0f);
        amount   (clipper,  "Clipper",   30.0f);
        addFloat (loudness, "Loudness", { -14.0f, -3.0f, 0.1f }, -7.0f, "LUFS",
                  [] (float v, int) { return String (v, 1) + " LUFS"; });
        addFloat (ceiling,  "Ceiling",  { -3.0f, 0.0f, 0.01f }, -1.0f, "dBTP",
                  [] (float v, int) { return String (v, 2) + " dBTP"; });
        amount   (fix,      "Fix Amount", 100.0f);
        amount   (mix,      "Mix",       100.0f);
        addFloat (output,  "Output",  { -24.0f, 24.0f, 0.01f }, 0.0f, "dB", dB);

        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { releaseAuto, version }, "Release Auto", true));
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { bypass, version }, "Bypass", false));
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { oversample, version }, "4x Oversampling", true,
                         AudioParameterBoolAttributes().withAutomatable (false)));

        return { p.begin(), p.end() };
    }
}

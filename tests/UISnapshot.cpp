// Renders the editor to PNGs with synthetic DnB audio flowing through the processor.
// Usage: DnBMixDoctorSnapshot <output-folder> [scale]
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <random>

using namespace juce;

static bool brightStyle = false;   // false: sub-heavy neuro test mix, true: bright liquid-style mix

static void fillMix (AudioBuffer<float>& b, double sr, int64& pos)
{
    static std::mt19937 rng (3);
    std::normal_distribution<float> noise (0.0f, 1.0f);
    const double beat = 60.0 / 174.0;
    for (int i = 0; i < b.getNumSamples(); ++i, ++pos)
    {
        const double t = pos / sr, inBeat = std::fmod (t, beat);
        const int beatIdx = (int) (t / beat);
        const float kick = (beatIdx % 2 == 0) ? (float) (std::sin (2 * MathConstants<double>::pi * (50 + 120 * std::exp (-inBeat * 40)) * inBeat) * std::exp (-inBeat * 9)) : 0.0f;
        const float snare = (beatIdx % 2 == 1) ? noise (rng) * (float) std::exp (-inBeat * 25) * 0.5f : 0.0f;
        const float hat = noise (rng) * (float) std::exp (-std::fmod (t, beat / 2) * 120) * 0.15f;
        const float sub = (float) std::sin (2 * MathConstants<double>::pi * 55.0 * t) * 0.4f;
        const float reese = (float) ((std::fmod (t * 110.0, 1.0) - 0.5) + (std::fmod (t * 110.8, 1.0) - 0.5)) * 0.2f;
        const double tp = 2 * MathConstants<double>::pi * t;
        const float pad = (float) (std::sin (tp * 1320.0) + std::sin (tp * 1660.0) + std::sin (tp * 1980.0) + std::sin (tp * 2640.0)) * 0.06f;
        const float m = brightStyle ? (kick * 0.45f + snare * 1.2f + hat * 3.0f + sub * 0.3f + pad) * 0.35f
                                    : (kick * 0.8f + snare + hat + sub + reese) * 0.35f;
        const float side = (float) std::sin (2 * MathConstants<double>::pi * 3000.0 * t) * 0.02f + hat * 0.1f;
        b.setSample (0, i, m + side);
        b.setSample (1, i, m - side);
    }
}

// ---- App icon: a square panel VU meter (black bezel, four screws, cream face with the pivot housing cut
//      into the bottom, black scale with a red +1..+3 block, "VU", -/+ marks, needle at rest on the left).
static Image renderIcon (int S)
{
    Image img (Image::ARGB, S, S, true);
    Graphics g (img);
    g.addTransform (AffineTransform::scale ((float) S / 1024.0f));
    auto font = [] (float h, bool bold = false) { return mcui::retroFont (h, bold); };

    // bezel
    const Rectangle<float> body (28.0f, 28.0f, 968.0f, 968.0f);
    ColourGradient bez (Colour (0xff3a3a3c), 0, body.getY(), Colour (0xff060606), 0, body.getBottom(), false);
    bez.addColour (0.15, Colour (0xff1c1c1e));
    g.setGradientFill (bez);
    g.fillRoundedRectangle (body, 70.0f);
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawRoundedRectangle (body.reduced (4.0f), 66.0f, 6.0f);

    // screws
    for (auto p : { Point<float> (112, 112), Point<float> (912, 112), Point<float> (112, 912), Point<float> (912, 912) })
    {
        g.setColour (Colour (0xff000000));
        g.fillEllipse (Rectangle<float> (58, 58).withCentre (p));
        g.setColour (Colours::white.withAlpha (0.22f));
        g.drawEllipse (Rectangle<float> (58, 58).withCentre (p), 4.0f);
    }

    // face with the pivot housing biting into the bottom edge
    const Rectangle<float> face (150.0f, 120.0f, 724.0f, 720.0f);
    Path facePath;
    facePath.addRoundedRectangle (face, 40.0f);
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (facePath);
        ColourGradient cream (Colour (0xfffff7cf), 512, 420, Colour (0xffe6d78f), 150, 120, true);
        g.setGradientFill (cream);
        g.fillRect (face);

        const Point<float> pivot (512.0f, 900.0f);
        const float R = 540.0f;
        auto posFor = [] (float vu) { const float lo = 0.1f, hi = std::pow (10.0f, 3.0f / 20.0f); return (std::pow (10.0f, vu / 20.0f) - lo) / (hi - lo); };
        auto angFor = [&] (float vu) { return jmap (posFor (vu), -0.54f, 0.54f); };
        auto at = [&] (float a, float r) { return Point<float> (pivot.x + std::sin (a) * r, pivot.y - std::cos (a) * r); };

        const Colour ink (0xff151515), red (0xffd0281c);
        Path arc;
        arc.addCentredArc (pivot.x, pivot.y, R, R, 0, angFor (-20), angFor (0), true);
        g.setColour (ink);
        g.strokePath (arc, PathStrokeType (7.0f));
        Path redBand;
        redBand.addCentredArc (pivot.x, pivot.y, R + 14.0f, R + 14.0f, 0, angFor (0), angFor (3), true);
        g.setColour (red);
        g.strokePath (redBand, PathStrokeType (26.0f));

        g.setFont (font (64.0f));
        for (int v : { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 })
        {
            const float a = angFor ((float) v);
            g.setColour (v > 0 ? red : ink);
            g.drawLine (Line<float> (at (a, R), at (a, R + (v == 0 || v == -20 || v == 3 ? 48.0f : 32.0f))), 8.0f);
            if (v == -20 || v == -10 || v == -5 || v == -3 || v == 0 || v == 3)
                g.drawText (String (std::abs (v)), Rectangle<float> (110, 70).withCentre (at (a, R + 88.0f)), Justification::centred);
        }
        // lower percentage scale: dots
        g.setColour (ink);
        for (int k = 0; k <= 10; ++k)
            g.fillEllipse (Rectangle<float> (14, 14).withCentre (at (jmap ((float) k / 10.0f, angFor (-20), angFor (0)), R - 50.0f)));

        g.setColour (ink);
        g.setFont (font (150.0f, true));
        g.drawText ("VU", Rectangle<float> (400, 160).withCentre ({ 512, 585 }), Justification::centred);
        g.setFont (font (120.0f, true));
        g.drawText ("-", Rectangle<float> (120, 120).withCentre ({ 250, 660 }), Justification::centred);
        g.setColour (red);
        g.drawText ("+", Rectangle<float> (120, 120).withCentre ({ 775, 660 }), Justification::centred);

        // needle resting on the left, plus its shadow
        const auto tip = at (angFor (-23.0f), R + 40.0f);
        g.setColour (Colours::black.withAlpha (0.25f));
        g.drawLine (Line<float> (pivot.translated (14, 10), tip.translated (14, 10)), 10.0f);
        g.setColour (ink);
        g.drawLine (Line<float> (pivot, tip), 9.0f);

        // pivot housing biting into the bottom of the face
        g.setColour (Colour (0xff0a0a0a));
        g.fillEllipse (Rectangle<float> (360.0f, 290.0f).withCentre ({ 512.0f, 870.0f }));
        g.setColour (Colours::white.withAlpha (0.10f));
        g.drawEllipse (Rectangle<float> (360.0f, 290.0f).withCentre ({ 512.0f, 870.0f }), 4.0f);

        // glass sheen
        ColourGradient glare (Colours::white.withAlpha (0.35f), 150, 120, Colours::transparentWhite, 150, 470, false);
        g.setGradientFill (glare);
        g.fillRect (face.withHeight (360.0f));
    }
    g.setColour (Colours::black.withAlpha (0.6f));
    g.strokePath (facePath, PathStrokeType (6.0f));

    // screw on the pivot housing
    g.setColour (Colour (0xff050505));
    g.fillEllipse (Rectangle<float> (64, 64).withCentre ({ 512, 900 }));
    g.setColour (Colours::white.withAlpha (0.2f));
    g.drawEllipse (Rectangle<float> (64, 64).withCentre ({ 512, 900 }), 4.0f);
    return img;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    if (argc > 2 && String (argv[1]) == "--icon")
    {
        const File f { String (argv[2]) };
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        FileOutputStream os (f);
        PNGImageFormat().writeImageToStream (renderIcon (1024), os);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        return 0;
    }
    const File outDir (argc > 1 ? String (argv[1]) : File::getCurrentWorkingDirectory().getFullPathName());
    const float scale = argc > 2 ? (float) String (argv[2]).getDoubleValue() : 1.0f;
    outDir.createDirectory();

    const double sr = 48000.0;
    const int block = 512;
    DnBMixDoctorProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);
    proc.presets.load ("Neuro Smash");

    std::unique_ptr<AudioProcessorEditor> editor (proc.createEditorIfNeeded());
    editor->setSize ((int) (1300 * scale), (int) (700 * scale));

    mcui::Visualizer* vis = nullptr;
    std::function<void (Component&)> find = [&] (Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* v = dynamic_cast<mcui::Visualizer*> (child)) vis = v;
            find (*child);
        }
    };
    find (*editor);
    jassert (vis != nullptr);

    AudioBuffer<float> buf (2, block);
    MidiBuffer midi;
    int64 pos = 0;
    double t = 0;
    const int totalBlocks = (int) (6.0 * sr / block);
    for (int i = 0; i < totalBlocks; ++i)
    {
        fillMix (buf, sr, pos);
        proc.processBlock (buf, midi);
        if (i % 2 == 0) { t += 1.0 / 46.875; vis->advanceFrame (t); }
    }

    auto shoot = [&] (mcui::Visualizer::View v, const String& name)
    {
        vis->setView (v);
        for (int i = 0; i < 12; ++i)
        {
            fillMix (buf, sr, pos);
            proc.processBlock (buf, midi);
            t += 1.0 / 93.75; vis->advanceFrame (t);
        }
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        auto f = outDir.getChildFile (name);
        f.deleteFile();
        FileOutputStream os (f);
        PNGImageFormat().writeImageToStream (img, os);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    };
    shoot (mcui::Visualizer::View::wave, "ui_wave.png");
    shoot (mcui::Visualizer::View::spectrum, "ui_spectrum.png");
    shoot (mcui::Visualizer::View::stereo, "ui_stereo.png");

    // same display after 8 s of a bright, hat-and-pad heavy mix: the waveform palette moves with the music
    brightStyle = true;
    vis->setView (mcui::Visualizer::View::wave);
    for (int i = 0; i < (int) (8.0 * sr / block); ++i)
    {
        fillMix (buf, sr, pos);
        proc.processBlock (buf, midi);
        if (i % 2 == 0) { t += 1.0 / 46.875; vis->advanceFrame (t); }
    }
    shoot (mcui::Visualizer::View::wave, "ui_wave_bright.png");

    editor = nullptr;
    return 0;
}

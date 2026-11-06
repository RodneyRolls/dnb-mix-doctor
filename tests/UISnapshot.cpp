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

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
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

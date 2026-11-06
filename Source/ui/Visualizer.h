#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Theme.h"
#include "../analysis/VisualFifo.h"

class DnBMixDoctorProcessor;

namespace mcui
{
    /** The main display: WAVE (scrolling waveform whose colours follow the music), SPECTRUM (input vs
        output FFT) and STEREO (goniometer + correlation). Repaints on the display's vblank (~60 fps);
        all audio data arrives through the processor's lock-free FIFOs.

        Music-reactive colour: every waveform column is analysed for its spectral balance (sub / mids /
        highs) and its level. A slowly moving "mood" hue follows the track's overall brightness and
        punch: sub-heavy rollers glow deep violet, reese-driven neuro shifts to magenta/pink, bright
        liquid sits in cyan/teal. Each column is then tinted around that hue by its own balance and lit
        by its level, so kicks, snares and drops are visible as colour changes. */
    class Visualizer : public juce::Component
    {
    public:
        enum class View { wave = 0, spectrum, stereo };

        explicit Visualizer (DnBMixDoctorProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

        float getRmsL() const noexcept        { return rmsL; }
        float getRmsR() const noexcept        { return rmsR; }
        float getCorrelation() const noexcept { return correlation; }
        /** Current music-reactive accent colour (changes slowly with the track). */
        juce::Colour getMoodColour() const noexcept { return moodColour (0.0f, 1.0f); }

        /** Called once per display frame with the elapsed time (drives the level gauges). */
        std::function<void (double)> onFrame;

        void setView (View v)                 { view = v; repaint(); }
        /** Drives one display frame manually (offline snapshot rendering / tests). */
        void advanceFrame (double timestampSeconds) { frame (timestampSeconds); }

    private:
        void frame (double timestampSeconds);
        void pullColumns();
        void pullSpectrum();
        void pullStereo();
        void computeSpectrum();

        juce::Colour moodColour (float hueOffset, float brightness, float saturation = 0.78f) const noexcept;
        static float columnBrightness (const mcdsp::VisColumn&) noexcept;
        /** Waveform colour by level: yellow (quiet, -24 dBFS) round the spectrum to red (loudest, 0 dBFS). */
        static juce::Colour loudnessColour (float peakLinear) noexcept;

        void drawWave (juce::Graphics&);
        void drawSpectrum (juce::Graphics&);
        void drawStereo (juce::Graphics&);
        void drawTabs (juce::Graphics&);

        DnBMixDoctorProcessor& proc;

        static constexpr int numCols = 1024;
        std::array<mcdsp::VisColumn, numCols> cols {};
        std::array<mcdsp::VisColumn, numCols> popBuf {};
        std::array<float, numCols> colTone {};   // per-column brightness 0..1 (sub -> highs)
        int colWrite = 0;
        juce::int64 colCount = 0;
        double lastColumnTime = 0, lastFrameTime = 0, clipFlashUntil = 0;

        // ---- music-reactive colour state
        float moodTone = 0.4f, moodPunch = 0.0f, moodHue = 0.66f, slowPeak = 0.1f, energy = 0.0f;

        static constexpr int fftOrder = 12, fftSize = 1 << fftOrder, numPoints = 360;
        juce::dsp::FFT fft { fftOrder };
        juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
        std::array<float, fftSize> ringIn {}, ringOut {}, popTmpIn {}, popTmpOut {};
        int ringPos = 0;
        std::array<float, fftSize * 2> fftBuf {};
        std::array<float, numPoints> specIn {}, specOut {};

        std::array<mcdsp::StereoFrame, 8192> stereoTmp {};
        int stereoCount = 0;
        juce::Image phosphor;
        float correlation = 1.0f, rmsL = 0.0f, rmsR = 0.0f;

        View view = View::wave;
        juce::Rectangle<float> screen, plot;
        std::array<juce::Rectangle<float>, 3> tabs;

        juce::VBlankAttachment vblank { this, [this] (double t) { frame (t); } };
    };
}

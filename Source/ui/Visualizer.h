#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "HiFiLookAndFeel.h"
#include "../analysis/VisualFifo.h"

class DnBMixDoctorProcessor;

namespace hifi
{
    /** The large tuner-window visualiser: WAVE (scrolling band-coloured waveform, input ghost, peak hold,
        clip line, gain-reduction trace), SPECTRUM (input vs output FFT) and STEREO (goniometer +
        correlation). Repaints on the display's vblank (~60 fps); all audio data arrives through the
        processor's lock-free FIFOs. */
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

        /** Called once per display frame with the elapsed time (used to animate the VU needles). */
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

        void renderBackground (float scale);
        void drawWave (juce::Graphics&);
        void drawSpectrum (juce::Graphics&);
        void drawStereo (juce::Graphics&);
        void drawTabs (juce::Graphics&);

        DnBMixDoctorProcessor& proc;

        static constexpr int numCols = 1024;
        std::array<mcdsp::VisColumn, numCols> cols {};
        std::array<mcdsp::VisColumn, numCols> popBuf {};
        int colWrite = 0;
        juce::int64 colCount = 0;
        double lastColumnTime = 0, lastFrameTime = 0, clipFlashUntil = 0;

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
        juce::Rectangle<float> glass, plot;
        std::array<juce::Rectangle<float>, 3> tabs;
        juce::Image background;
        float bgScale = 0.0f;

        juce::VBlankAttachment vblank { this, [this] (double t) { frame (t); } };
    };
}

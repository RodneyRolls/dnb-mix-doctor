#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Params.h"
#include "Theme.h"
#include "Widgets.h"

namespace mcui
{
    //==============================================================================
    /** One vertical EQ fader: drag up/down (shift = fine), wheel, double-click = 0 dB.
        Drawn by itself (thin track, accent fill from 0 dB, value on top, frequency underneath). */
    class EqFader : public juce::Slider
    {
    public:
        static constexpr float trackTop = 30.0f, trackBottomPad = 26.0f;

        EqFader() : juce::Slider (RotaryVerticalDrag, NoTextBox)   // drag mode: a click never makes the band jump
        {
            setScrollWheelEnabled (true);
        }

        void setLabel (const juce::String& s) { label = s; }

        void mouseDown (const juce::MouseEvent& e) override { lastY = e.position.y; juce::Slider::mouseDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! isEnabled() || e.mods.isPopupMenu()) return;
            const float dy = lastY - e.position.y;
            lastY = e.position.y;
            const double perPixel = (e.mods.isShiftDown() ? 0.15 : 1.0) / juce::jmax (20.0f, trackHeight());
            const double prop = juce::jlimit (0.0, 1.0, valueToProportionOfLength (getValue()) + dy * perPixel);
            setValue (proportionOfLengthToValue (prop), juce::sendNotificationSync);
        }
        void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); repaint(); }
        void mouseExit (const juce::MouseEvent& e) override  { juce::Slider::mouseExit (e); repaint(); }

        float trackHeight() const { return (float) getHeight() - trackTop - trackBottomPad; }
        float yForDb (float db) const
        {
            const float t = (db + params::eqRangeDb) / (2.0f * params::eqRangeDb);
            return trackTop + (1.0f - juce::jlimit (0.0f, 1.0f, t)) * trackHeight();
        }

        void paint (juce::Graphics& g) override
        {
            using namespace juce;
            const float cx = (float) getWidth() * 0.5f;
            const float db = (float) getValue();
            const bool hot = isMouseOverOrDragging();
            const float y0 = yForDb (0.0f), yv = yForDb (db);
            const bool lit = isEnabled();

            // track
            g.setColour (Colours::white.withAlpha (0.07f));
            g.fillRoundedRectangle (Rectangle<float> (cx - 2.0f, trackTop, 4.0f, trackHeight()), 2.0f);

            // fill from 0 dB to the value: cyan for boosts, pink for cuts
            if (std::abs (db) > 0.05f)
            {
                const auto c = db > 0 ? colours::cyan : colours::pink;
                const auto fill = Rectangle<float> (cx - 2.0f, jmin (y0, yv), 4.0f, std::abs (yv - y0));
                g.setColour (c.withAlpha (lit ? 0.85f : 0.3f));
                g.fillRoundedRectangle (fill, 2.0f);
                if (lit) drawGlow (g, { cx, yv }, 18.0f, c.withAlpha (0.35f));
            }

            // thumb
            auto thumb = Rectangle<float> (jmin (34.0f, (float) getWidth() - 10.0f), 13.0f).withCentre ({ cx, yv });
            g.setColour (hot ? colours::controlHi.brighter (0.15f) : colours::controlHi);
            g.fillRoundedRectangle (thumb, 4.0f);
            g.setColour (Colours::white.withAlpha (hot ? 0.35f : 0.18f));
            g.drawRoundedRectangle (thumb.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (lit ? Colours::white.withAlpha (0.9f) : colours::textFaint);
            g.fillRect (thumb.withSizeKeepingCentre (thumb.getWidth() - 14.0f, 1.5f));

            // value on top, frequency underneath
            g.setFont (font (10.5f, true));
            g.setColour (std::abs (db) < 0.05f ? colours::textDim : (db > 0 ? colours::cyan : colours::pink).withAlpha (lit ? 1.0f : 0.5f));
            const String v = std::abs (db) < 0.05f ? String ("0.0") : (db > 0 ? "+" : "") + String (db, 1);
            g.drawText (v, Rectangle<float> (0.0f, 6.0f, (float) getWidth(), 14.0f), Justification::centred, false);
            g.setFont (font (10.5f, true));
            g.setColour (hot ? colours::text : colours::textDim);
            g.drawText (label, Rectangle<float> (0.0f, (float) getHeight() - 18.0f, (float) getWidth(), 14.0f), Justification::centred, false);
        }

    private:
        float lastY = 0;
        juce::String label;
    };

    //==============================================================================
    /** The ADVANCED panel: 16-band graphic EQ with a live response curve, EQ MIX, ON and FLAT. */
    class EqPanel : public juce::Component, private juce::Timer
    {
    public:
        EqPanel (juce::AudioProcessorValueTreeState& s, std::function<double()> sampleRate)
            : apvts (s), getSampleRate (std::move (sampleRate))
        {
            setOpaque (false);
            for (int i = 0; i < params::numEqBands; ++i)
            {
                auto& f = faders[(size_t) i];
                f.setLabel (params::eqFreqLabel (i));
                f.setDoubleClickReturnValue (true, 0.0);
                f.setTitle ("EQ " + params::eqFreqLabel (i));
                addAndMakeVisible (f);
                attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, params::eqBand (i), f));
            }
            mixKnob.setTitle ("EQ MIX");
            mixKnob.setDoubleClickReturnValue (true, 100.0);
            addAndMakeVisible (mixKnob);
            attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, params::eqMix, mixKnob));

            onButton.setText ("EQ ON");
            onButton.setClickingTogglesState (true);
            onAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, params::eqOn, onButton);
            flatButton.setText ("FLAT");
            flatButton.onClick = [this] { flatten(); };
            closeButton.setText ("CLOSE");
            closeButton.onClick = [this] { if (onClose) onClose(); };
            for (auto* b : { &onButton, &flatButton, &closeButton }) addAndMakeVisible (b);

            startTimerHz (30);
        }

        std::function<void()> onClose;

        void resized() override
        {
            using namespace juce;
            auto r = getLocalBounds();
            faderArea = r.withTrimmedLeft (52).withTrimmedRight (250).withTrimmedTop (34).withTrimmedBottom (10);
            const float colW = (float) faderArea.getWidth() / (float) params::numEqBands;
            for (int i = 0; i < params::numEqBands; ++i)
                faders[(size_t) i].setBounds (Rectangle<float> ((float) faderArea.getX() + colW * (float) i, (float) faderArea.getY(),
                                                                colW, (float) faderArea.getHeight()).toNearestInt());

            auto side = r.removeFromRight (232).reduced (16, 12);
            closeButton.setBounds (side.removeFromTop (22).removeFromRight (70));
            side.removeFromTop (14);
            mixKnob.setBounds (side.removeFromTop (118).withSizeKeepingCentre (104, 104).translated (0, 8));
            side.removeFromTop (16);
            auto row = side.removeFromTop (34);
            onButton.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (4, 0));
            flatButton.setBounds (row.reduced (4, 0));
        }

        void paint (juce::Graphics& g) override
        {
            using namespace juce;
            const auto bounds = getLocalBounds().toFloat();
            drawCard (g, bounds, 16.0f);

            drawCaption (g, "Advanced  -  16-band EQ", { 22.0f, 10.0f, 300.0f, 16.0f }, Justification::centredLeft, colours::text, 11.0f);
            drawCaption (g, "Drag a band up to boost, down to cut. Double-click resets it.",
                         { 260.0f, 10.0f, 600.0f, 16.0f }, Justification::centredLeft, colours::textFaint, 9.5f);

            // screen behind the faders
            const auto scr = faderArea.toFloat().expanded (6.0f, 2.0f).withTrimmedTop (EqFader::trackTop - 12.0f)
                                                .withTrimmedBottom (EqFader::trackBottomPad - 12.0f);
            g.setColour (colours::screen);
            g.fillRoundedRectangle (scr, 10.0f);

            // dB grid aligned to the fader travel
            const auto& f0 = faders[0];
            auto yFor = [&] (float db) { return (float) faderArea.getY() + f0.yForDb (db); };
            g.setFont (font (9.0f));
            for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f })
            {
                g.setColour (Colours::white.withAlpha (db == 0.0f ? 0.10f : 0.04f));
                g.drawHorizontalLine (roundToInt (yFor (db)), scr.getX() + 4.0f, scr.getRight() - 4.0f);
                g.setColour (colours::textFaint);
                g.drawText ((db > 0 ? "+" : "") + String ((int) db), Rectangle<float> (scr.getX() - 34.0f, yFor (db) - 6.0f, 28.0f, 12.0f),
                            Justification::centredRight, false);
            }

            // response curve of what the EQ is doing right now (EQ MIX and ON applied)
            const bool on = apvts.getRawParameterValue (params::eqOn)->load() > 0.5f;
            const float depth = on ? apvts.getRawParameterValue (params::eqMix)->load() * 0.01f : 0.0f;
            const double sr = jmax (22050.0, getSampleRate ? getSampleRate() : 48000.0);
            std::array<float, params::numEqBands> gains {};
            for (int b = 0; b < params::numEqBands; ++b)
                gains[(size_t) b] = apvts.getRawParameterValue (params::eqBand (b))->load() * depth;

            Path curve, fill;
            const float x0 = (float) faderArea.getX(), x1 = (float) faderArea.getRight();
            const int steps = (int) (x1 - x0);
            for (int s = 0; s <= steps; ++s)
            {
                const float x = x0 + (float) s;
                const float db = responseDb (freqForX (x), gains, sr);
                const float y = jlimit (scr.getY() + 2.0f, scr.getBottom() - 2.0f, yFor (jlimit (-15.0f, 15.0f, db)));
                if (s == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, yFor (0.0f)); }
                else          curve.lineTo (x, y);
                fill.lineTo (x, y);
            }
            fill.lineTo (x1, yFor (0.0f));
            fill.closeSubPath();
            g.setGradientFill (accentGradient ({ x0, 0.0f }, { x1, 0.0f }, 0.16f));
            g.fillPath (fill);
            g.setGradientFill (accentGradient ({ x0, 0.0f }, { x1, 0.0f }, on ? 0.95f : 0.35f));
            g.strokePath (curve, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

            // right-hand controls
            auto side = bounds.withTrimmedLeft (bounds.getWidth() - 232.0f).reduced (16.0f, 12.0f);
            g.setColour (Colours::white.withAlpha (0.06f));
            g.fillRect (side.getX() - 14.0f, 18.0f, 1.0f, bounds.getHeight() - 36.0f);
            drawCaption (g, "EQ Mix", mixKnob.getBounds().toFloat().withY ((float) mixKnob.getY() - 20.0f).withHeight (14.0f),
                         Justification::centred, colours::textDim, 10.0f);
            drawCaption (g, "0 % = flat   100 % = full curve",
                         Rectangle<float> (side.getX(), (float) onButton.getBottom() + 10.0f, side.getWidth(), 12.0f),
                         Justification::centred, colours::textFaint, 9.0f);
        }

    private:
        void timerCallback() override
        {
            float sig = apvts.getRawParameterValue (params::eqMix)->load() + 7.0f * apvts.getRawParameterValue (params::eqOn)->load()
                      + (float) (getSampleRate ? getSampleRate() : 0.0) * 1.0e-3f;
            for (int b = 0; b < params::numEqBands; ++b)
                sig += apvts.getRawParameterValue (params::eqBand (b))->load() * (float) (b + 3) * 0.37f;
            if (sig != lastSignature) { lastSignature = sig; repaint(); }
        }

        void flatten()
        {
            for (int b = 0; b < params::numEqBands; ++b)
                if (auto* p = apvts.getParameter (params::eqBand (b)))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
                    p->endChangeGesture();
                }
        }

        /** x on the fader area -> frequency, so each fader sits exactly on its band's centre frequency. */
        float freqForX (float x) const
        {
            using namespace juce;
            const float colW = (float) faderArea.getWidth() / (float) params::numEqBands;
            const float pos = (x - (float) faderArea.getX()) / colW - 0.5f;   // 0 = first band centre
            const int n = params::numEqBands;
            const int i = jlimit (0, n - 2, (int) std::floor (pos));
            const float t = pos - (float) i;
            const float l0 = std::log (params::eqFreqs[(size_t) i]), l1 = std::log (params::eqFreqs[(size_t) i + 1]);
            return std::exp (l0 + (l1 - l0) * t);
        }

        /** Exact magnitude of the engine's bells (TPT SVF bell, Q 1.8, bilinear-prewarped analogue response). */
        static float responseDb (float f, const std::array<float, params::numEqBands>& gains, double sr)
        {
            const double w = std::tan (juce::MathConstants<double>::pi * juce::jlimit (1.0, sr * 0.4999, (double) f) / sr);
            double total = 0.0;
            for (size_t b = 0; b < gains.size(); ++b)
            {
                if (std::abs (gains[b]) < 0.001f) continue;
                const double A = std::pow (10.0, gains[b] / 40.0), q = 1.8;
                const double w0 = std::tan (juce::MathConstants<double>::pi * juce::jmin ((double) params::eqFreqs[b], sr * 0.49) / sr);
                const double W = w / w0, re = 1.0 - W * W;
                const double num = re * re + std::pow (W * A / q, 2.0), den = re * re + std::pow (W / (q * A), 2.0);
                total += 10.0 * std::log10 (num / den);
            }
            return (float) total;
        }

        juce::AudioProcessorValueTreeState& apvts;
        std::function<double()> getSampleRate;
        std::array<EqFader, params::numEqBands> faders;
        Knob mixKnob;
        IconButton onButton { "EQ on", IconButton::Icon::none, true },
                   flatButton { "Flat", IconButton::Icon::none, false },
                   closeButton { "Close", IconButton::Icon::none, false };
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;
        juce::Rectangle<int> faderArea;
        float lastSignature = -1.0e9f;
    };
}

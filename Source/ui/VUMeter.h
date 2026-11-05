#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "HiFiLookAndFeel.h"

namespace hifi
{
    /** Backlit analogue VU meter with a mechanically damped needle (~300 ms rise, slight overshoot).
        0 VU is calibrated to referenceDbfs RMS. Call setLevel() + tick() from the UI refresh. */
    class VUMeter : public juce::Component
    {
    public:
        explicit VUMeter (juce::String channelName) : name (std::move (channelName)) {}

        void setLevel (float rmsLinear) noexcept { target = rmsLinear; }

        void tick (double dt)
        {
            const float vu = juce::jlimit (-24.0f, 6.0f, juce::Decibels::gainToDecibels (target, -60.0f) - referenceDbfs);
            const float targetPos = positionFor (vu);
            // 2nd-order needle: w ~ 2pi*2.1Hz, damping 0.78 -> ~300 ms to 99 % with ~1.5 % overshoot
            const float w = 13.2f, zeta = 0.78f;
            const int steps = juce::jmax (1, (int) std::ceil (dt / 0.004));
            const float h = (float) dt / (float) steps;
            for (int i = 0; i < steps; ++i)
            {
                const float acc = w * w * (targetPos - pos) - 2.0f * zeta * w * vel;
                vel += acc * h;
                pos += vel * h;
            }
            pos = juce::jlimit (-0.05f, 1.06f, pos);
            peakLamp = vu > 0.0f ? 1.0f : juce::jmax (0.0f, peakLamp - (float) dt * 2.0f);
            repaint();
        }

        void resized() override { face = {}; }

        void paint (juce::Graphics& g) override
        {
            using namespace juce;
            const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
            if (face.isNull() || faceScale != scale)
                renderFace (scale);
            g.drawImage (face, getLocalBounds().toFloat());

            const auto geo = geometry();
            const float a = angleFor (pos);
            const Point<float> tip (geo.pivot.x + std::sin (a) * geo.radius, geo.pivot.y - std::cos (a) * geo.radius);
            const Point<float> base (geo.pivot.x + std::sin (a) * geo.radius * 0.18f, geo.pivot.y - std::cos (a) * geo.radius * 0.18f);

            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (geo.window.toNearestInt());
            g.setColour (Colours::black.withAlpha (0.25f));
            g.drawLine ({ base.translated (2.5f, 3.0f), tip.translated (2.5f, 3.0f) }, 1.6f);
            g.setColour (Colour (0xff1a1410));
            g.drawLine ({ base, tip }, 1.4f);

            // peak LED
            auto led = Rectangle<float> (7, 7).withCentre ({ geo.window.getRight() - 12.0f, geo.window.getY() + 12.0f });
            g.setColour (Colour (0xff3a0d08).interpolatedWith (colours::clipRed, peakLamp));
            g.fillEllipse (led);
            if (peakLamp > 0.0f)
            {
                g.setColour (colours::clipRed.withAlpha (0.35f * peakLamp));
                g.fillEllipse (led.expanded (4.0f));
            }

            // glass reflection
            ColourGradient glare (Colours::white.withAlpha (0.16f), geo.window.getX(), geo.window.getY(),
                                  Colours::transparentWhite, geo.window.getX(), geo.window.getCentreY(), false);
            g.setGradientFill (glare);
            g.fillRoundedRectangle (geo.window.withHeight (geo.window.getHeight() * 0.45f), 4.0f);
        }

        float referenceDbfs = -14.0f;

    private:
        struct Geo { juce::Rectangle<float> window; juce::Point<float> pivot; float radius; };

        Geo geometry() const
        {
            auto w = getLocalBounds().toFloat().reduced (5.0f);
            const float rScale = (w.getWidth() * 0.5f - 20.0f) / std::sin (0.82f);   // scale arc fits the width
            return { w, { w.getCentreX(), w.getBottom() + w.getHeight() * 0.18f }, rScale / 0.80f };
        }

        static float positionFor (float vu)
        {
            // real VU scales are linear in voltage
            const float lo = std::pow (10.0f, -20.0f / 20.0f), hi = std::pow (10.0f, 3.0f / 20.0f);
            return (std::pow (10.0f, vu / 20.0f) - lo) / (hi - lo);
        }
        static float angleFor (float p) { return juce::jmap (p, -0.82f, 0.82f); }

        void renderFace (float scale)
        {
            using namespace juce;
            faceScale = scale;
            face = Image (Image::ARGB, jmax (1, roundToInt (getWidth() * scale)), jmax (1, roundToInt (getHeight() * scale)), true);
            Graphics g (face);
            g.addTransform (AffineTransform::scale (scale));
            const auto geo = geometry();
            const auto outer = getLocalBounds().toFloat();

            // chrome bezel and backlit face
            ColourGradient bezel (Colour (0xfff2f0ea), 0, outer.getY(), Colour (0xff5f5c57), 0, outer.getBottom(), false);
            g.setGradientFill (bezel);
            g.fillRoundedRectangle (outer, 7.0f);
            ColourGradient bg (Colour (0xfffff0c4), geo.window.getCentreX(), geo.window.getBottom(),
                               Colour (0xffd99a3e), geo.window.getX(), geo.window.getY(), true);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (geo.window, 4.0f);
            g.setColour (Colours::black.withAlpha (0.6f));
            g.drawRoundedRectangle (geo.window, 4.0f, 1.2f);

            // scale
            const float rScale = geo.radius * 0.80f;
            Path arcBlack, arcRed;
            arcBlack.addCentredArc (geo.pivot.x, geo.pivot.y, rScale, rScale, 0, angleFor (positionFor (-20)), angleFor (positionFor (0)), true);
            arcRed.addCentredArc (geo.pivot.x, geo.pivot.y, rScale, rScale, 0, angleFor (positionFor (0)), angleFor (positionFor (3)), true);
            g.setColour (Colour (0xff201812));
            g.strokePath (arcBlack, PathStrokeType (1.2f));
            g.setColour (Colour (0xffc0261b));
            g.strokePath (arcRed, PathStrokeType (3.5f));

            g.setFont (labelFont (jmax (7.0f, geo.window.getHeight() * 0.075f)));
            for (int v : { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 })
            {
                const float a = angleFor (positionFor ((float) v));
                const float sx = std::sin (a), cy = -std::cos (a);
                const float len = (v == 0 || v == -20 || v == -10 || v == 3) ? 7.0f : 4.5f;
                g.setColour (v > 0 ? Colour (0xffc0261b) : Colour (0xff201812));
                g.drawLine (geo.pivot.x + sx * rScale, geo.pivot.y + cy * rScale,
                            geo.pivot.x + sx * (rScale + len), geo.pivot.y + cy * (rScale + len), 1.1f);
                if (v == -20 || v == -10 || v == -5 || v == -3 || v == 0 || v == 3)
                {
                    const Point<float> tp (geo.pivot.x + sx * (rScale + 14.0f), geo.pivot.y + cy * (rScale + 14.0f));
                    g.drawText (v > 0 ? "+" + String (v) : String (std::abs (v)), Rectangle<float> (26, 12).withCentre (tp),
                                Justification::centred);
                }
            }
            // needle pivot housing at the bottom
            const float housing = geo.window.getHeight() * 0.10f;
            g.setColour (Colour (0xff1b1511));
            g.fillRect (geo.window.withTop (geo.window.getBottom() - housing));

            g.setColour (Colour (0xff201812));
            g.setFont (labelFont (jmax (9.0f, geo.window.getHeight() * 0.14f)));
            g.drawText ("VU", geo.window.withTrimmedTop (geo.window.getHeight() * 0.56f).withHeight (geo.window.getHeight() * 0.2f),
                        Justification::centred);
            g.setFont (labelFont (jmax (7.0f, geo.window.getHeight() * 0.065f)));
            const auto labels = geo.window.withTrimmedBottom (housing + 2.0f).reduced (7.0f, 0.0f);
            g.drawText (name, labels, Justification::bottomLeft);
            g.drawText ("MOD CON DSP", labels, Justification::bottomRight);
        }

        juce::String name;
        juce::Image face;
        float faceScale = 0.0f;
        float target = 0.0f, pos = 0.0f, vel = 0.0f, peakLamp = 0.0f;
    };
}

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace mcui
{
    /** Arc level gauge for one channel. Shows RMS relative to the LOUDNESS target (0 dB = on target),
        with VU-like ballistics (~300 ms) and a 1.5 s peak-hold tick. The fill colour follows the
        visualiser's music-reactive colour. Call setLevel() + tick() once per display frame. */
    class LevelGauge : public juce::Component
    {
    public:
        explicit LevelGauge (juce::String channelName) : name (std::move (channelName)) {}

        void setLevel (float rmsLinear) noexcept { target = rmsLinear; }
        void setAccent (juce::Colour c) noexcept { accent = c; }

        void tick (double dt)
        {
            const float rel = juce::jlimit (minDb - 6.0f, maxDb + 3.0f, juce::Decibels::gainToDecibels (target, -80.0f) - referenceDbfs);
            const float w = 13.2f, zeta = 0.85f;   // critically-ish damped, ~300 ms rise
            const int steps = juce::jmax (1, (int) std::ceil (dt / 0.004));
            const float h = (float) dt / (float) steps;
            for (int i = 0; i < steps; ++i)
            {
                const float acc = w * w * (rel - level) - 2.0f * zeta * w * vel;
                vel += acc * h;
                level += vel * h;
            }
            if (level > hold) { hold = level; holdAge = 0.0; }
            else if ((holdAge += dt) > 1.5) hold = juce::jmax (level, hold - (float) dt * 12.0f);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            using namespace juce;
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            drawCard (g, r, 12.0f);

            auto inner = r.reduced (12.0f, 8.0f);
            auto top = inner.removeFromTop (12.0f);
            drawCaption (g, name, top, Justification::centredLeft, colours::textDim, 9.5f);
            drawCaption (g, "vs target", top, Justification::centredRight, colours::textFaint, 8.5f);

            const float radius = jmin (inner.getWidth() * 0.5f, inner.getHeight() * 0.62f) - 6.0f;
            const Point<float> c (inner.getCentreX(), inner.getY() + radius + 8.0f);
            const float t = 7.0f;

            Path track;
            track.addCentredArc (c.x, c.y, radius, radius, 0.0f, a0, a1, true);
            g.setColour (Colours::white.withAlpha (0.06f));
            g.strokePath (track, PathStrokeType (t, PathStrokeType::curved, PathStrokeType::rounded));

            // target zone marker (-1 .. +1 dB)
            Path zone;
            zone.addCentredArc (c.x, c.y, radius + t * 0.5f + 3.0f, radius + t * 0.5f + 3.0f, 0.0f, angleFor (-1.0f), angleFor (1.0f), true);
            g.setColour (colours::good.withAlpha (0.7f));
            g.strokePath (zone, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

            const float a = angleFor (level);
            if (a > a0 + 0.01f)
            {
                Path fill;
                fill.addCentredArc (c.x, c.y, radius, radius, 0.0f, a0, a, true);
                ColourGradient grad (accent.withAlpha (0.55f), c.x - radius, c.y + radius * 0.4f,
                                     level > 3.0f ? colours::bad : (level > 0.5f ? colours::warn : accent), c.x + radius, c.y, false);
                auto glow = grad;
                glow.multiplyOpacity (0.25f);
                g.setGradientFill (glow);
                g.strokePath (fill, PathStrokeType (t * 2.4f, PathStrokeType::curved, PathStrokeType::rounded));
                g.setGradientFill (grad);
                g.strokePath (fill, PathStrokeType (t, PathStrokeType::curved, PathStrokeType::rounded));
            }

            // peak hold tick
            {
                const float ha = angleFor (hold);
                const Point<float> p0 (c.x + std::sin (ha) * (radius - t * 0.8f), c.y - std::cos (ha) * (radius - t * 0.8f));
                const Point<float> p1 (c.x + std::sin (ha) * (radius + t * 0.8f), c.y - std::cos (ha) * (radius + t * 0.8f));
                g.setColour (Colours::white.withAlpha (hold > minDb ? 0.85f : 0.0f));
                g.drawLine ({ p0, p1 }, 2.0f);
            }

            // scale labels
            g.setFont (font (9.0f));
            g.setColour (colours::textFaint);
            for (float v : { minDb, -6.0f, 0.0f, maxDb })
            {
                const float la = angleFor (v);
                const float lr = radius - t - 9.0f;
                const Point<float> p (c.x + std::sin (la) * lr, c.y - std::cos (la) * lr);
                g.drawText (v > 0 ? "+" + String ((int) v) : String ((int) v), Rectangle<float> (26, 10).withCentre (p), Justification::centred);
            }

            // numeric readout in the middle
            const String txt = level <= minDb ? String ("--") : (level > 0 ? "+" : "") + String (level, 1);
            g.setColour (level > 3.0f ? colours::bad : colours::text);
            g.setFont (font (22.0f, true));
            g.drawText (txt, Rectangle<float> (radius * 1.4f, 24.0f).withCentre (c.translated (0.0f, -2.0f)), Justification::centred);
            g.setColour (colours::textDim);
            g.setFont (font (9.5f));
            g.drawText ("dB", Rectangle<float> (radius * 1.4f, 12.0f).withCentre (c.translated (0.0f, 16.0f)), Justification::centred);
        }

        float referenceDbfs = -7.0f;

    private:
        static constexpr float minDb = -18.0f, maxDb = 6.0f;
        static constexpr float a0 = -2.2f, a1 = 2.2f;
        static float angleFor (float db) { return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, a0, a1); }

        juce::String name;
        juce::Colour accent { colours::cyan };
        float target = 0.0f, level = -40.0f, vel = 0.0f, hold = -40.0f;
        double holdAge = 0.0;
    };
}

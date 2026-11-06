#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace mcui
{
    //==============================================================================
    /** Rotary knob: vertical drag, shift-drag for fine control, wheel and double-click reset.
        The current value is drawn inside the knob by the LookAndFeel. */
    class Knob : public juce::Slider
    {
    public:
        Knob() : juce::Slider (RotaryVerticalDrag, NoTextBox)
        {
            setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            setScrollWheelEnabled (true);
        }

        void setBipolar (bool b) { getProperties().set ("bipolar", b); repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            lastY = e.position.y;
            juce::Slider::mouseDown (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! isEnabled() || e.mods.isPopupMenu()) return;
            const float dy = lastY - e.position.y;
            lastY = e.position.y;
            const double perPixel = e.mods.isShiftDown() ? 0.0006 : 0.0055;
            const double prop = juce::jlimit (0.0, 1.0, valueToProportionOfLength (getValue()) + dy * perPixel);
            setValue (proportionOfLengthToValue (prop), juce::sendNotificationSync);
        }

        void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); repaint(); }
        void mouseExit (const juce::MouseEvent& e) override  { juce::Slider::mouseExit (e); repaint(); }

    private:
        float lastY = 0;
    };

    //==============================================================================
    /** Rounded flat button with a vector icon or text, an optional status LED and a progress bar. */
    class IconButton : public juce::Button
    {
    public:
        enum class Icon { none, power, bolt, left, right, star, menu, folder };

        IconButton (const juce::String& name, Icon i, bool hasLed)
            : juce::Button (name), icon (i), led (hasLed) {}

        void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
        void setProgress (float p)           { if (p != progress) { progress = p; repaint(); } }
        void setBlink (bool b)               { if (b != blink) { blink = b; repaint(); } }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            using namespace juce;
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            const bool small = getHeight() < 26;
            const float radius = small ? 6.0f : 9.0f;
            const bool latched = getClickingTogglesState() && getToggleState();
            const bool lit = getToggleState() && (! blink || (Time::getMillisecondCounter() / 300) % 2 == 0);

            // body
            g.setColour (down ? colours::controlHi.brighter (0.08f) : (over ? colours::controlHi : colours::control));
            g.fillRoundedRectangle (r, radius);
            if (latched)
            {
                g.setGradientFill (accentGradient (r.getTopLeft(), r.getBottomRight(), 0.22f));
                g.fillRoundedRectangle (r, radius);
                g.setGradientFill (accentGradient (r.getTopLeft(), r.getBottomRight(), 0.75f));
                g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            }
            else
            {
                g.setColour (Colours::white.withAlpha (over ? 0.12f : 0.07f));
                g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            }

            // status LED
            auto content = r;
            if (led)
            {
                const float d = small ? 5.0f : 7.0f;
                auto ledArea = content.removeFromLeft (small ? 12.0f : 18.0f).withTrimmedLeft (small ? 3.0f : 6.0f)
                                      .withSizeKeepingCentre (d, d);
                if (lit)
                {
                    drawGlow (g, ledArea.getCentre(), d * 2.2f, colours::cyan.withAlpha (0.6f));
                    g.setColour (colours::cyan.brighter (0.4f));
                }
                else
                    g.setColour (Colours::white.withAlpha (0.12f));
                g.fillEllipse (ledArea);
            }

            const auto ink = (lit && ! led) || latched ? Colours::white
                           : colours::text.withAlpha (isEnabled() ? (over ? 1.0f : 0.82f) : 0.35f);
            g.setColour (ink);
            drawIcon (g, small ? content : content.reduced (3.0f), ink);

            if (progress > 0.0f && progress < 1.0f)
            {
                auto bar = r.reduced (radius * 0.6f, 0.0f).removeFromBottom (3.0f).translated (0.0f, -2.0f);
                g.setColour (Colours::white.withAlpha (0.08f));
                g.fillRoundedRectangle (bar, 1.5f);
                g.setGradientFill (accentGradient (bar.getTopLeft(), bar.getTopRight()));
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * progress), 1.5f);
            }
        }

    private:
        void drawIcon (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour ink)
        {
            using namespace juce;
            const float s = jmin (a.getWidth(), a.getHeight()) * 0.78f;
            const auto c = a.getCentre();
            const PathStrokeType stroke (jmax (1.4f, s * 0.085f), PathStrokeType::curved, PathStrokeType::rounded);
            Path p;
            switch (icon)
            {
                case Icon::power:
                    p.addCentredArc (c.x, c.y, s * 0.34f, s * 0.34f, 0, 0.75f, MathConstants<float>::twoPi - 0.75f, true);
                    p.startNewSubPath (c.x, c.y - s * 0.46f);
                    p.lineTo (c.x, c.y - s * 0.06f);
                    g.strokePath (p, stroke);
                    return;
                case Icon::bolt:
                    p.startNewSubPath (c.x + s * 0.10f, c.y - s * 0.48f);
                    p.lineTo (c.x - s * 0.24f, c.y + s * 0.06f);
                    p.lineTo (c.x - s * 0.01f, c.y + s * 0.06f);
                    p.lineTo (c.x - s * 0.10f, c.y + s * 0.48f);
                    p.lineTo (c.x + s * 0.24f, c.y - s * 0.08f);
                    p.lineTo (c.x + s * 0.02f, c.y - s * 0.08f);
                    p.closeSubPath();
                    if (getToggleState()) g.setGradientFill (accentGradient ({ c.x, c.y - s * 0.5f }, { c.x, c.y + s * 0.5f }));
                    g.fillPath (p);
                    return;
                case Icon::left:
                case Icon::right:
                {
                    const float d = icon == Icon::left ? -1.0f : 1.0f;
                    p.startNewSubPath (c.x - d * s * 0.10f, c.y - s * 0.24f);
                    p.lineTo (c.x + d * s * 0.12f, c.y);
                    p.lineTo (c.x - d * s * 0.10f, c.y + s * 0.24f);
                    g.strokePath (p, stroke);
                    return;
                }
                case Icon::star:
                    p.addStar (c, 5, s * 0.19f, s * 0.42f, 0.0f);
                    if (getToggleState())
                    {
                        g.setColour (colours::warn);
                        g.fillPath (p);
                        return;
                    }
                    g.strokePath (p, PathStrokeType (jmax (1.2f, s * 0.06f), PathStrokeType::curved, PathStrokeType::rounded));
                    return;
                case Icon::menu:
                    for (int i = -1; i <= 1; ++i)
                    {
                        p.startNewSubPath (c.x - s * 0.28f, c.y + (float) i * s * 0.2f);
                        p.lineTo (c.x + s * 0.28f, c.y + (float) i * s * 0.2f);
                    }
                    g.strokePath (p, stroke);
                    return;
                case Icon::folder:
                    p.startNewSubPath (c.x - s * 0.38f, c.y - s * 0.26f);
                    p.lineTo (c.x - s * 0.10f, c.y - s * 0.26f);
                    p.lineTo (c.x, c.y - s * 0.15f);
                    p.lineTo (c.x + s * 0.38f, c.y - s * 0.15f);
                    p.lineTo (c.x + s * 0.38f, c.y + s * 0.28f);
                    p.lineTo (c.x - s * 0.38f, c.y + s * 0.28f);
                    p.closeSubPath();
                    g.strokePath (p, PathStrokeType (jmax (1.3f, s * 0.07f), PathStrokeType::curved, PathStrokeType::rounded));
                    return;
                case Icon::none:
                    g.setFont (font (jmin (14.0f, jmax (9.0f, a.getHeight() * 0.5f)), true));
                    g.drawText (text, a, Justification::centred, false);
                    return;
            }
            ignoreUnused (ink);
        }

        Icon icon;
        bool led;
        bool blink = false;
        juce::String text;
        float progress = 0.0f;
    };

    //==============================================================================
    /** Numeric readout card: small caption, large value, optional unit. */
    class Readout : public juce::Component
    {
    public:
        Readout (juce::String captionText, juce::String unitText = {})
            : caption (std::move (captionText)), unit (std::move (unitText)) {}

        void setValueText (const juce::String& t)  { if (t != value) { value = t; repaint(); } }
        void setValueColour (juce::Colour c)       { if (c != valueColour) { valueColour = c; repaint(); } }
        std::function<void()> onClick;

        void paint (juce::Graphics& g) override
        {
            using namespace juce;
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            if (card) drawCard (g, r, 10.0f);
            auto inner = r.reduced (card ? 12.0f : 4.0f, card ? 8.0f : 2.0f);
            if (caption.isNotEmpty())
                drawCaption (g, caption, inner.removeFromTop (12.0f), Justification::centredLeft, colours::textDim, 9.5f);

            const float h = jmin (26.0f, inner.getHeight() * 0.92f);
            const auto f = font (h, true);
            const float uw = unit.isNotEmpty() ? textWidth (font (h * 0.48f), unit) + 6.0f : 0.0f;
            g.setColour (valueColour);
            g.setFont (f);
            g.drawText (value, inner.withTrimmedRight (uw), Justification::centredRight, false);
            if (unit.isNotEmpty())
            {
                g.setColour (colours::textDim);
                g.setFont (font (h * 0.48f));
                g.drawText (unit, inner.removeFromRight (uw - 2.0f).withTrimmedTop (h * 0.32f), Justification::centredRight, false);
            }
        }

        void setCardStyle (bool b) { card = b; repaint(); }
        void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }

    private:
        juce::String caption, unit, value { "--" };
        juce::Colour valueColour { colours::text };
        bool card = true;
    };
}

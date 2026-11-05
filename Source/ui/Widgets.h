#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "HiFiLookAndFeel.h"

namespace hifi
{
    //==============================================================================
    /** Rotary knob: vertical drag, shift-drag for fine control, wheel, double-click reset,
        value bubble while dragging (bubble appearance comes from the LookAndFeel). */
    class Knob : public juce::Slider
    {
    public:
        Knob() : juce::Slider (RotaryVerticalDrag, NoTextBox)
        {
            setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            setScrollWheelEnabled (true);
        }

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

    private:
        float lastY = 0;
    };

    //==============================================================================
    /** Chunky chrome-edged rectangular push button with an optional amber pilot lamp. */
    class ChromeButton : public juce::Button
    {
    public:
        enum class Icon { none, power, bolt, left, right, star, menu, folder };

        ChromeButton (const juce::String& name, Icon i, bool hasLamp)
            : juce::Button (name), icon (i), lamp (hasLamp) {}

        void setText (const juce::String& t) { text = t; repaint(); }
        void setProgress (float p)           { if (p != progress) { progress = p; repaint(); } }
        void setBlink (bool b)               { blink = b; repaint(); }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            using namespace juce;
            auto r = getLocalBounds().toFloat().reduced (1.5f);

            // drop shadow + chrome bezel
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (r.translated (0, 1.5f), 4.0f);
            ColourGradient chrome (Colour (0xfff7f7f4), r.getX(), r.getY(), Colour (0xff6f6c66), r.getX(), r.getBottom(), false);
            chrome.addColour (0.48, Colour (0xffc9c7c1));
            chrome.addColour (0.52, Colour (0xff8f8c86));
            g.setGradientFill (chrome);
            g.fillRoundedRectangle (r, 4.0f);

            // brushed face, pressed in when down/on (thinner bezel on the small AUTO-style buttons)
            const bool small = getHeight() < 26;
            auto face = r.reduced (small ? 1.5f : 3.0f);
            const bool pressed = down || (getClickingTogglesState() && getToggleState());
            ColourGradient fg (pressed ? Colour (0xffb9b19c) : Colour (0xffe6dec9), face.getX(), face.getY(),
                               pressed ? Colour (0xffd8cfb8) : Colour (0xffb3a88d), face.getX(), face.getBottom(), false);
            g.setGradientFill (fg);
            g.fillRoundedRectangle (face, 2.5f);
            Random rng (7);
            for (float y = face.getY() + 1; y < face.getBottom() - 1; y += 1.0f)
            {
                g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.03f + rng.nextFloat() * 0.04f));
                g.drawHorizontalLine ((int) y, face.getX() + 1, face.getRight() - 1);
            }
            g.setColour (Colours::black.withAlpha (pressed ? 0.5f : 0.25f));
            g.drawRoundedRectangle (face, 2.5f, 1.0f);
            if (over && ! pressed)
            {
                g.setColour (Colours::white.withAlpha (0.12f));
                g.fillRoundedRectangle (face, 2.5f);
            }

            // pilot lamp
            auto content = face;
            if (lamp)
            {
                const float lampD = small ? 5.0f : 7.0f;
                auto lampArea = content.removeFromLeft (small ? 11.0f : 16.0f).withSizeKeepingCentre (lampD, lampD);
                const bool on = getToggleState() && (! blink || (Time::getMillisecondCounter() / 250) % 2 == 0);
                if (on)
                {
                    ColourGradient glow (colours::amber.withAlpha (0.65f), lampArea.getCentreX(), lampArea.getCentreY(),
                                         Colours::transparentBlack, lampArea.getCentreX() + 9.0f, lampArea.getCentreY(), true);
                    g.setGradientFill (glow);
                    g.fillEllipse (lampArea.expanded (8.0f));
                }
                ColourGradient bulb (on ? colours::amberHot : colours::amberDim.brighter (0.2f), lampArea.getCentreX() - 1, lampArea.getY(),
                                     on ? colours::amber : colours::amberDim, lampArea.getCentreX(), lampArea.getBottom(), false);
                g.setGradientFill (bulb);
                g.fillEllipse (lampArea);
                g.setColour (Colours::black.withAlpha (0.6f));
                g.drawEllipse (lampArea, 0.8f);
            }

            auto ink = colours::engraving.withAlpha (isEnabled() ? 1.0f : 0.4f);
            g.setColour (ink);
            drawIcon (g, small ? content : content.reduced (2.0f), ink);

            if (progress > 0.0f && progress < 1.0f)
            {
                auto bar = r.removeFromBottom (3.0f).reduced (4.0f, 0.0f);
                g.setColour (Colours::black.withAlpha (0.4f));
                g.fillRect (bar);
                g.setColour (colours::amber);
                g.fillRect (bar.withWidth (bar.getWidth() * progress));
            }
        }

    private:
        void drawIcon (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour ink)
        {
            using namespace juce;
            const float s = jmin (a.getWidth(), a.getHeight()) * 0.8f;
            const auto c = a.getCentre();
            Path p;
            switch (icon)
            {
                case Icon::power:
                {
                    p.addCentredArc (c.x, c.y, s * 0.36f, s * 0.36f, 0, 0.7f, MathConstants<float>::twoPi - 0.7f, true);
                    g.strokePath (p, PathStrokeType (s * 0.1f, PathStrokeType::curved, PathStrokeType::rounded));
                    g.drawLine (c.x, c.y - s * 0.48f, c.x, c.y - s * 0.05f, s * 0.1f);
                    return;
                }
                case Icon::bolt:
                    p.startNewSubPath (c.x + s * 0.10f, c.y - s * 0.50f);
                    p.lineTo (c.x - s * 0.26f, c.y + s * 0.06f);
                    p.lineTo (c.x - s * 0.01f, c.y + s * 0.06f);
                    p.lineTo (c.x - s * 0.12f, c.y + s * 0.50f);
                    p.lineTo (c.x + s * 0.26f, c.y - s * 0.08f);
                    p.lineTo (c.x + s * 0.02f, c.y - s * 0.08f);
                    p.closeSubPath();
                    g.fillPath (p);
                    return;
                case Icon::left:
                case Icon::right:
                {
                    const float d = icon == Icon::left ? -1.0f : 1.0f;
                    p.addTriangle (c.x + d * s * 0.22f, c.y, c.x - d * s * 0.18f, c.y - s * 0.26f, c.x - d * s * 0.18f, c.y + s * 0.26f);
                    g.fillPath (p);
                    return;
                }
                case Icon::star:
                {
                    p.addStar (c, 5, s * 0.2f, s * 0.45f, 0.0f);
                    if (getToggleState()) { g.setColour (colours::amber.darker (0.2f)); g.fillPath (p); g.setColour (ink); }
                    g.strokePath (p, PathStrokeType (1.2f));
                    return;
                }
                case Icon::menu:
                    for (int i = -1; i <= 1; ++i)
                        g.fillRoundedRectangle (c.x - s * 0.32f, c.y + i * s * 0.22f - s * 0.05f, s * 0.64f, s * 0.1f, s * 0.05f);
                    return;
                case Icon::folder:
                    p.startNewSubPath (c.x - s * 0.42f, c.y - s * 0.28f);
                    p.lineTo (c.x - s * 0.12f, c.y - s * 0.28f);
                    p.lineTo (c.x - s * 0.02f, c.y - s * 0.16f);
                    p.lineTo (c.x + s * 0.42f, c.y - s * 0.16f);
                    p.lineTo (c.x + s * 0.42f, c.y + s * 0.30f);
                    p.lineTo (c.x - s * 0.42f, c.y + s * 0.30f);
                    p.closeSubPath();
                    g.strokePath (p, PathStrokeType (s * 0.08f, PathStrokeType::mitered));
                    return;
                case Icon::none:
                    g.setFont (labelFont (jmin (13.0f, jmax (8.5f, a.getHeight() * 0.62f))));
                    g.drawText (text, a, Justification::centred, false);
                    return;
            }
        }

        Icon icon;
        bool lamp;
        bool blink = false;
        juce::String text;
        float progress = 0.0f;
    };

    //==============================================================================
    /** Vacuum-fluorescent numeric readout with an engraved caption above or beside it. */
    class VfdDisplay : public juce::Component
    {
    public:
        explicit VfdDisplay (juce::String captionText, juce::Colour c = colours::vfd)
            : caption (std::move (captionText)), colour (c) {}

        void setValueText (const juce::String& t) { if (t != value) { value = t; repaint(); } }
        std::function<void()> onClick;

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            if (caption.isNotEmpty())
                drawEngravedText (g, caption, r.removeFromTop (13.0f), labelFont (9.5f), juce::Justification::centred);
            drawInsetWindow (g, r.reduced (0.5f, 1.0f), 3.0f);
            auto digits = r.reduced (8.0f, r.getHeight() * 0.24f);
            drawVfdText (g, value, digits, colour, true);
        }

        void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }

    private:
        juce::String caption, value { "---" };
        juce::Colour colour;
    };
}

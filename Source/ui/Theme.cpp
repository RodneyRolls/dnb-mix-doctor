#include "Theme.h"

namespace mcui
{
    using namespace juce;

    //==============================================================================
    static const String& faceName()
    {
        static const String name = []
        {
            const auto all = Font::findAllTypefaceNames();
            for (auto* candidate : { "Inter", "SF Pro Display", "Segoe UI", "Helvetica Neue", "Roboto", "Arial" })
                if (all.contains (candidate))
                    return String (candidate);
            return Font::getDefaultSansSerifFontName();
        }();
        return name;
    }

    Font font (float height, bool bold)
    {
        return Font (FontOptions (faceName(), height, bold ? Font::bold : Font::plain));
    }

    float textWidth (const Font& f, const String& s)
    {
        return GlyphArrangement::getStringWidth (f, s);
    }

    void drawCard (Graphics& g, Rectangle<float> r, float radius, Colour top, Colour bottom)
    {
        // soft drop shadow
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (Colours::black.withAlpha (0.10f));
            g.fillRoundedRectangle (r.translated (0.0f, (float) i * 1.5f).expanded ((float) i * 0.5f), radius + (float) i);
        }
        ColourGradient grad (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, radius);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        // top edge highlight
        ColourGradient hi (Colours::white.withAlpha (0.07f), 0.0f, r.getY(), Colours::transparentWhite, 0.0f, r.getY() + 14.0f, false);
        g.setGradientFill (hi);
        g.drawRoundedRectangle (r.reduced (1.0f), radius - 1.0f, 1.0f);
    }

    void drawGlow (Graphics& g, Point<float> c, float radius, Colour colour)
    {
        ColourGradient glow (colour, c.x, c.y, colour.withAlpha (0.0f), c.x + radius, c.y, true);
        glow.addColour (0.45, colour.withMultipliedAlpha (0.35f));
        g.setGradientFill (glow);
        g.fillEllipse (Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c));
    }

    void drawCaption (Graphics& g, const String& s, Rectangle<float> area, Justification j, Colour c, float height)
    {
        g.setFont (font (height, true).withExtraKerningFactor (0.12f));
        g.setColour (c);
        g.drawText (s.toUpperCase(), area, j, false);
    }

    ColourGradient accentGradient (Point<float> from, Point<float> to, float alpha)
    {
        return ColourGradient (colours::cyan.withAlpha (alpha), from, colours::violet.withAlpha (alpha), to, false);
    }

    //==============================================================================
    LookAndFeel::LookAndFeel()
    {
        setColour (PopupMenu::backgroundColourId, Colour (0xf2141a22));
        setColour (PopupMenu::textColourId, colours::text);
        setColour (PopupMenu::highlightedBackgroundColourId, colours::cyan.withAlpha (0.16f));
        setColour (PopupMenu::highlightedTextColourId, Colours::white);
        setColour (ComboBox::textColourId, colours::text);
        setColour (ComboBox::outlineColourId, Colours::transparentBlack);
        setColour (AlertWindow::backgroundColourId, colours::card);
        setColour (AlertWindow::textColourId, colours::text);
        setColour (AlertWindow::outlineColourId, Colours::white.withAlpha (0.08f));
        setColour (TextEditor::backgroundColourId, colours::screen);
        setColour (TextEditor::textColourId, colours::text);
        setColour (TextEditor::outlineColourId, Colours::white.withAlpha (0.10f));
        setColour (TextEditor::focusedOutlineColourId, colours::cyan);
        setColour (TextEditor::highlightColourId, colours::cyan.withAlpha (0.3f));
        setColour (CaretComponent::caretColourId, colours::cyan);
        setColour (TextButton::buttonColourId, colours::control);
        setColour (TextButton::textColourOffId, colours::text);
    }

    void LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, Slider& slider)
    {
        const auto bounds = Rectangle<int> (x, y, w, h).toFloat();
        const float side = jmin (bounds.getWidth(), bounds.getHeight());
        const auto c = bounds.getCentre();
        const float t = jmax (3.0f, side * 0.045f);          // arc thickness
        const float Ra = side * 0.5f - t * 0.5f - 3.0f;       // arc radius
        const float Rb = Ra - t * 1.9f;                       // knob body radius
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool enabled = slider.isEnabled();
        const bool bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);
        const bool hot = slider.isMouseOverOrDragging();

        // ---- track + value arc
        Path track;
        track.addCentredArc (c.x, c.y, Ra, Ra, 0.0f, startAngle, endAngle, true);
        g.setColour (Colours::white.withAlpha (0.07f));
        g.strokePath (track, PathStrokeType (t, PathStrokeType::curved, PathStrokeType::rounded));

        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        if (std::abs (angle - from) > 0.01f)
        {
            Path value;
            value.addCentredArc (c.x, c.y, Ra, Ra, 0.0f, jmin (from, angle), jmax (from, angle), true);
            const auto grad = accentGradient ({ c.x - Ra, c.y + Ra }, { c.x + Ra, c.y - Ra * 0.2f }, enabled ? 1.0f : 0.4f);
            auto glow = grad;
            glow.multiplyOpacity (hot ? 0.30f : 0.18f);
            g.setGradientFill (glow);
            g.strokePath (value, PathStrokeType (t * 2.6f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setGradientFill (grad);
            g.strokePath (value, PathStrokeType (t, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // ---- body: drop shadow, soft dome, hairline rim
        const auto body = Rectangle<float> (Rb * 2.0f, Rb * 2.0f).withCentre (c);
        {
            ColourGradient shadow (Colours::black.withAlpha (0.55f), c.x, c.y + Rb * 0.25f,
                                   Colours::transparentBlack, c.x + Rb * 1.25f, c.y + Rb * 0.25f, true);
            g.setGradientFill (shadow);
            g.fillEllipse (body.expanded (Rb * 0.25f).translated (0.0f, Rb * 0.15f));

            ColourGradient dome (Colour (0xff2a3240), c.x - Rb * 0.4f, c.y - Rb * 0.7f,
                                 Colour (0xff131820), c.x + Rb * 0.3f, c.y + Rb, true);
            g.setGradientFill (dome);
            g.fillEllipse (body);
            ColourGradient rim (Colours::white.withAlpha (hot ? 0.20f : 0.13f), c.x, body.getY(),
                                Colours::white.withAlpha (0.02f), c.x, body.getBottom(), false);
            g.setGradientFill (rim);
            g.drawEllipse (body.reduced (0.5f), 1.0f);
        }

        // ---- pointer dot near the rim
        {
            const float pr = Rb * 0.80f, d = jmax (3.0f, Rb * 0.13f);
            const Point<float> p (c.x + std::sin (angle) * pr, c.y - std::cos (angle) * pr);
            drawGlow (g, p, d * 2.2f, colours::cyan.withAlpha (enabled ? 0.45f : 0.15f));
            g.setColour (enabled ? Colours::white : colours::textDim);
            g.fillEllipse (Rectangle<float> (d, d).withCentre (p));
        }

        // ---- value inside the knob: number large, unit small underneath
        {
            auto text = slider.getTextFromValue (slider.getValue());
            String unit;
            const int sp = text.lastIndexOfChar (' ');
            if (sp > 0) { unit = text.substring (sp + 1); text = text.substring (0, sp); }
            const float numH = jlimit (9.0f, 17.0f, Rb * 0.40f);
            auto area = body.reduced (Rb * 0.18f);
            g.setColour (enabled ? colours::text : colours::textDim);
            g.setFont (font (numH, true));
            g.drawText (text, area.withY (c.y - numH * 0.62f).withHeight (numH), Justification::centred, false);
            if (unit.isNotEmpty())
            {
                g.setColour (colours::textDim);
                g.setFont (font (numH * 0.62f));
                g.drawText (unit, area.withY (c.y + numH * 0.42f).withHeight (numH * 0.7f), Justification::centred, false);
            }
        }
    }

    //==============================================================================
    void LookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
    {
        const auto r = Rectangle<float> ((float) w, (float) h).reduced (0.5f);
        g.setColour (box.isMouseOver (true) ? colours::controlHi : colours::control);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

        Path chevron;
        const float ax = r.getRight() - 16.0f, ay = r.getCentreY();
        chevron.startNewSubPath (ax - 4.0f, ay - 2.0f);
        chevron.lineTo (ax, ay + 2.0f);
        chevron.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (colours::textDim);
        g.strokePath (chevron, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    Font LookAndFeel::getComboBoxFont (ComboBox&) { return font (14.0f, true); }

    void LookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
    {
        label.setBounds (24, 1, box.getWidth() - 48, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (Justification::centred);
    }

    void LookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
    {
        const auto r = Rectangle<float> ((float) w, (float) h);
        g.fillAll (findColour (PopupMenu::backgroundColourId));
        g.setColour (Colours::white.withAlpha (0.08f));
        g.drawRect (r, 1.0f);
    }

    void LookAndFeel::drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive,
                                         bool isHighlighted, bool isTicked, bool hasSubMenu, const String& text,
                                         const String& shortcut, const Drawable* icon, const Colour* textColour)
    {
        if (isSeparator)
        {
            g.setColour (Colours::white.withAlpha (0.08f));
            g.fillRect (area.reduced (10, 0).withHeight (1).withY (area.getCentreY()));
            return;
        }
        auto r = area.reduced (4, 1);
        if (isHighlighted && isActive)
        {
            g.setColour (findColour (PopupMenu::highlightedBackgroundColourId));
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
        }
        auto col = textColour != nullptr ? *textColour
                 : (isHighlighted ? findColour (PopupMenu::highlightedTextColourId) : findColour (PopupMenu::textColourId));
        if (! isActive) col = col.withAlpha (0.35f);

        auto tickArea = r.removeFromLeft (r.getHeight());
        if (isTicked)
        {
            g.setColour (colours::cyan);
            g.fillEllipse (tickArea.toFloat().withSizeKeepingCentre (7, 7));
            drawGlow (g, tickArea.toFloat().getCentre(), 8.0f, colours::cyan.withAlpha (0.35f));
        }
        else if (icon != nullptr)
            icon->drawWithin (g, tickArea.toFloat().reduced (4), RectanglePlacement::centred, 1.0f);

        g.setColour (col);
        g.setFont (getPopupMenuFont());
        g.drawFittedText (text, r.reduced (4, 0), Justification::centredLeft, 1);
        if (hasSubMenu)
        {
            Path p;
            const float ax = (float) r.getRight() - 12.0f, ay = (float) r.getCentreY();
            p.startNewSubPath (ax - 2.0f, ay - 4.0f);
            p.lineTo (ax + 2.0f, ay);
            p.lineTo (ax - 2.0f, ay + 4.0f);
            g.strokePath (p, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        if (shortcut.isNotEmpty())
            g.drawText (shortcut, r.reduced (8, 0), Justification::centredRight);
    }

    void LookAndFeel::drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& name)
    {
        drawCaption (g, name, area.reduced (12, 0).toFloat(), Justification::bottomLeft, colours::cyan.withAlpha (0.85f), 10.0f);
    }

    Font LookAndFeel::getPopupMenuFont() { return font (14.0f); }

    void LookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (down ? colours::controlHi.brighter (0.1f) : (over ? colours::controlHi : colours::control));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    }

    void LookAndFeel::drawCornerResizer (Graphics& g, int w, int h, bool over, bool dragging)
    {
        g.setColour ((over || dragging) ? colours::cyan : colours::textFaint);
        const float d = 2.2f;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col <= row; ++col)
                g.fillEllipse ((float) w - 4.0f - (float) col * 5.0f - d, (float) h - 4.0f - (float) (row - col) * 5.0f - d, d, d);
    }
}

#include "HiFiLookAndFeel.h"

#if MCDSP_HAS_EMBEDDED_FONT
 #include "BinaryData.h"
#endif

namespace hifi
{
    using namespace juce;

    //==============================================================================
    static Typeface::Ptr embeddedTypeface()
    {
       #if MCDSP_HAS_EMBEDDED_FONT
        static Typeface::Ptr tf = Typeface::createSystemTypefaceFor (BinaryData::MichromaRegular_ttf,
                                                                      (size_t) BinaryData::MichromaRegular_ttfSize);
        return tf;
       #else
        return nullptr;
       #endif
    }

    static const String& fallbackFaceName()
    {
        static const String name = []
        {
            const auto all = Font::findAllTypefaceNames();
            for (auto* candidate : { "Eurostile", "Eurostile LT Std", "Microgramma", "Michroma", "Bahnschrift",
                                     "Avenir Next Condensed", "DIN Alternate", "Futura", "Arial" })
                if (all.contains (candidate))
                    return String (candidate);
            return Font::getDefaultSansSerifFontName();
        }();
        return name;
    }

    Font labelFont (float height, bool bold)
    {
        if (auto tf = embeddedTypeface())
            return Font (FontOptions (tf).withHeight (height * 0.92f).withKerningFactor (0.06f));
        return Font (FontOptions (fallbackFaceName(), height, bold ? Font::bold : Font::plain).withKerningFactor (0.10f));
    }

    Font monoFont (float height)
    {
        return Font (FontOptions (Font::getDefaultMonospacedFontName(), height, Font::bold));
    }

    //==============================================================================
    static int segmentMask (juce_wchar c)
    {
        switch (c)
        {
            case '0': return 0x3f; case '1': return 0x06; case '2': return 0x5b; case '3': return 0x4f; case '4': return 0x66;
            case '5': return 0x6d; case '6': return 0x7d; case '7': return 0x07; case '8': return 0x7f; case '9': return 0x6f;
            case '-': return 0x40; case 'A': return 0x77; case 'b': case 'B': return 0x7c; case 'C': return 0x39;
            case 'd': case 'D': return 0x5e; case 'E': return 0x79; case 'F': return 0x71; case 'H': return 0x76;
            case 'I': return 0x06; case 'L': return 0x38; case 'n': case 'N': return 0x54; case 'o': case 'O': return 0x5c;
            case 'P': return 0x73; case 'r': case 'R': return 0x50; case 't': case 'T': return 0x78; case 'U': return 0x3e;
            case 'S': return 0x6d; case 'u': return 0x1c; case '_': return 0x08;
            default: return 0;
        }
    }

    void drawVfdText (Graphics& g, const String& text, Rectangle<float> area, Colour colour, bool rightAlign)
    {
        const float h = area.getHeight();
        const float cw = h * 0.52f, gap = h * 0.16f, t = h * 0.12f;

        int cells = 0;
        for (auto c : text) if (c != '.') ++cells;
        const float totalW = (float) cells * cw + (float) juce::jmax (0, cells - 1) * gap;
        float x = rightAlign ? area.getRight() - totalW : area.getCentreX() - totalW * 0.5f;
        const float y = area.getY();

        auto segRects = [&] (float cx) -> std::array<Rectangle<float>, 7>
        {
            const float inset = t * 0.55f, halfH = h * 0.5f;
            return {{ { cx + inset, y, cw - 2 * inset, t },                                  // a
                      { cx + cw - t, y + inset, t, halfH - inset - t * 0.4f },              // b
                      { cx + cw - t, y + halfH + t * 0.4f, t, halfH - inset - t * 0.4f },   // c
                      { cx + inset, y + h - t, cw - 2 * inset, t },                         // d
                      { cx, y + halfH + t * 0.4f, t, halfH - inset - t * 0.4f },            // e
                      { cx, y + inset, t, halfH - inset - t * 0.4f },                       // f
                      { cx + inset, y + halfH - t * 0.5f, cw - 2 * inset, t } }};           // g
        };

        Graphics::ScopedSaveState ss (g);
        g.addTransform (AffineTransform::shear (-0.08f, 0.0f).translated (0.08f * (y + h), 0.0f));

        Path lit, ghost;
        for (int i = 0; i < text.length(); ++i)
        {
            const auto c = text[i];
            if (c == '.')
            {
                lit.addEllipse (x - gap * 0.5f - t * 0.55f, y + h - t * 1.1f, t * 1.1f, t * 1.1f);
                continue;
            }
            const int mask = segmentMask (c);
            const auto rects = segRects (x);
            for (int s = 0; s < 7; ++s)
                ((mask >> s) & 1 ? lit : ghost).addRoundedRectangle (rects[(size_t) s], t * 0.45f);
            x += cw + gap;
        }

        g.setColour (colour.withAlpha (0.07f));
        g.fillPath (ghost);
        g.setColour (colour.withAlpha (0.22f));
        g.strokePath (lit, PathStrokeType (t * 0.9f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (colour);
        g.fillPath (lit);
    }

    void drawInsetWindow (Graphics& g, Rectangle<float> r, float corner)
    {
        g.setColour (Colours::white.withAlpha (0.35f));                         // lower lip catches the light
        g.drawRoundedRectangle (r.translated (0, 1.0f), corner, 1.0f);
        ColourGradient bg (Colour (0xff050302), r.getX(), r.getY(), Colour (0xff17100a), r.getX(), r.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (r, corner);
        g.setColour (Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (r, corner, 1.2f);
        g.setColour (Colours::black.withAlpha (0.45f));                         // inner top shadow
        g.fillRoundedRectangle (r.withHeight (3.0f).reduced (1.0f, 0.0f).translated (0, 1.0f), 1.5f);
    }

    void drawEngravedText (Graphics& g, const String& s, Rectangle<float> area, const Font& f, Justification j)
    {
        g.setFont (f);
        g.setColour (Colours::white.withAlpha (0.45f));
        g.drawText (s, area.translated (0.0f, 1.0f), j, false);
        g.setColour (colours::engraving);
        g.drawText (s, area, j, false);
    }

    //==============================================================================
    LookAndFeel::LookAndFeel()
    {
        setColour (PopupMenu::backgroundColourId, Colour (0xff17100b));
        setColour (PopupMenu::textColourId, colours::panelLight);
        setColour (PopupMenu::highlightedBackgroundColourId, colours::amber.withAlpha (0.25f));
        setColour (PopupMenu::highlightedTextColourId, colours::amberHot);
        setColour (ComboBox::textColourId, colours::vfd);
        setColour (ComboBox::outlineColourId, Colours::transparentBlack);
        setColour (AlertWindow::backgroundColourId, Colour (0xff1b130d));
        setColour (AlertWindow::textColourId, colours::panelLight);
        setColour (AlertWindow::outlineColourId, colours::amber.withAlpha (0.5f));
        setColour (TextEditor::backgroundColourId, Colour (0xff0b0806));
        setColour (TextEditor::textColourId, colours::vfd);
        setColour (TextEditor::outlineColourId, colours::amber.withAlpha (0.4f));
        setColour (TextEditor::focusedOutlineColourId, colours::amber);
        setColour (TextButton::buttonColourId, Colour (0xff3a2d22));
        setColour (TextButton::textColourOffId, colours::panelLight);
        setColour (BubbleComponent::backgroundColourId, Colour (0xff0b0806));
        setColour (BubbleComponent::outlineColourId, colours::amber.withAlpha (0.6f));
    }

    const Image& LookAndFeel::getCapImage (int d, bool faceLayer)
    {
        d = jlimit (8, 1024, d);
        const int key = d * 2 + (faceLayer ? 1 : 0);
        if (auto it = capCache.find (key); it != capCache.end())
            return it->second;

        Image img (Image::ARGB, d, d, true);
        Graphics g (img);
        const float R = (float) d * 0.5f, c = R;
        Random rng (1234 + d);

        if (! faceLayer)
        {
            // ---- knurled rim ridges (this layer rotates with the knob)
            const int ridges = jmax (40, (int) (R * 2.4f));
            const float r0 = R * 0.84f, r1 = R - 0.4f;
            for (int i = 0; i < ridges; ++i)
            {
                const float a = MathConstants<float>::twoPi * (float) i / (float) ridges;
                const float sx = std::sin (a), cy = -std::cos (a);
                g.setColour ((i & 1) ? Colours::black.withAlpha (0.38f) : Colours::white.withAlpha (0.30f));
                g.drawLine (c + sx * r0, c + cy * r0, c + sx * r1, c + cy * r1, jmax (0.8f, R * 0.045f));
            }
        }
        else
        {
            // ---- machined face: top-down gradient + concentric brushing + light "cross" sheen
            const float fr = R * 0.82f;
            Rectangle<float> face (c - fr, c - fr, fr * 2, fr * 2);
            ColourGradient faceGrad (Colour (0xffe3dfd6), c, c - fr, Colour (0xff9a968d), c, c + fr, false);
            g.setGradientFill (faceGrad);
            g.fillEllipse (face);

            for (float r = 1.0f; r < fr; r += 0.75f)
            {
                const bool light = rng.nextBool();
                g.setColour ((light ? Colours::white : Colours::black).withAlpha (0.02f + rng.nextFloat() * 0.05f));
                g.drawEllipse (c - r, c - r, r * 2, r * 2, 0.6f);
            }
            const int wedges = 180;
            for (int i = 0; i < wedges; ++i)
            {
                const float a0 = MathConstants<float>::twoPi * (float) i / wedges;
                const float a1 = a0 + MathConstants<float>::twoPi / wedges + 0.01f;
                const float k = std::cos (2.0f * (a0 - 0.6f));
                Path wedge;
                wedge.addPieSegment (face, a0, a1, 0.0f);
                g.setColour (k > 0 ? Colours::white.withAlpha (0.16f * k * k) : Colours::black.withAlpha (0.10f * k * k));
                g.fillPath (wedge);
            }
            // bevel between rim and face
            g.setColour (Colours::black.withAlpha (0.45f));
            g.drawEllipse (face.expanded (0.5f), 1.0f);
            g.setColour (Colours::white.withAlpha (0.35f));
            g.drawEllipse (face.reduced (0.8f), 0.8f);
        }
        return capCache[key] = img;
    }

    void LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, Slider& slider)
    {
        const auto bounds = Rectangle<int> (x, y, w, h).toFloat();
        const float side = jmin (bounds.getWidth(), bounds.getHeight());
        const auto centre = bounds.getCentre();
        const float Rw = side * 0.5f - 4.0f;      // recessed well
        const float Ra = Rw * 0.865f;              // value arc
        const float Rc = Rw * 0.66f;               // cap
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float alpha = slider.isEnabled() ? 1.0f : 0.5f;

        // 1. soft dark glow around the well, then the well itself
        {
            ColourGradient halo (Colours::black.withAlpha (0.55f), centre.x, centre.y,
                                 Colours::transparentBlack, centre.x + Rw + 6.0f, centre.y, true);
            halo.addColour (Rw / (Rw + 6.0f), Colours::black.withAlpha (0.45f));
            g.setGradientFill (halo);
            g.fillEllipse (Rectangle<float> (Rw * 2 + 12, Rw * 2 + 12).withCentre (centre.translated (0, 1.5f)));

            const auto well = Rectangle<float> (Rw * 2, Rw * 2).withCentre (centre);
            ColourGradient wg (colours::wellCentre, centre.x, centre.y - Rw * 0.3f, colours::wellEdge, centre.x, centre.y + Rw, true);
            g.setGradientFill (wg);
            g.fillEllipse (well);
            ColourGradient lip (Colours::black.withAlpha (0.9f), centre.x, well.getY(),
                                Colours::white.withAlpha (0.35f), centre.x, well.getBottom(), false);
            g.setGradientFill (lip);
            g.drawEllipse (well, 1.2f);
        }

        // 2. amber value arc, 7:30 -> 4:30
        {
            Path track, value;
            track.addCentredArc (centre.x, centre.y, Ra, Ra, 0.0f, startAngle, endAngle, true);
            value.addCentredArc (centre.x, centre.y, Ra, Ra, 0.0f, startAngle, angle, true);
            const float t = jmax (1.6f, side * 0.022f);
            g.setColour (colours::amber.withAlpha (0.07f));
            g.strokePath (track, PathStrokeType (t, PathStrokeType::curved, PathStrokeType::rounded));
            if (pos > 0.002f)
            {
                g.setColour (colours::amber.withAlpha (0.12f * alpha));
                g.strokePath (value, PathStrokeType (t * 3.6f, PathStrokeType::curved, PathStrokeType::rounded));
                g.setColour (colours::amber.withAlpha (0.25f * alpha));
                g.strokePath (value, PathStrokeType (t * 2.0f, PathStrokeType::curved, PathStrokeType::rounded));
                g.setColour (colours::amberHot.withAlpha (alpha));
                g.strokePath (value, PathStrokeType (t, PathStrokeType::curved, PathStrokeType::rounded));
            }
        }

        // 3. machined aluminium cap with drop shadow
        {
            const auto capArea = Rectangle<float> (Rc * 2, Rc * 2).withCentre (centre);
            ColourGradient shadow (Colours::black.withAlpha (0.7f), centre.x, centre.y + Rc * 0.18f,
                                   Colours::transparentBlack, centre.x + Rc * 1.18f, centre.y + Rc * 0.18f, true);
            g.setGradientFill (shadow);
            g.fillEllipse (capArea.expanded (Rc * 0.18f).translated (0, Rc * 0.12f));

            // rim base (static lighting), knurl ridges (turn with the knob), machined face (static lighting)
            ColourGradient rimGrad (Colour (0xfff4f1ea).withMultipliedAlpha (alpha), centre.x, capArea.getY(),
                                    Colour (0xff86827b).withMultipliedAlpha (alpha), centre.x, capArea.getBottom(), false);
            g.setGradientFill (rimGrad);
            g.fillEllipse (capArea);

            const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
            const int px = roundToInt (Rc * 2 * scale);
            const auto& ridges = getCapImage (px, false);
            const auto& face = getCapImage (px, true);
            const float s = Rc * 2 / (float) ridges.getWidth();
            const auto toCentre = AffineTransform::translation (-ridges.getWidth() * 0.5f, -ridges.getHeight() * 0.5f).scaled (s);
            g.setOpacity (alpha);
            g.drawImageTransformed (ridges, toCentre.rotated (angle).translated (centre.x, centre.y));
            g.drawImageTransformed (face, toCentre.translated (centre.x, centre.y));
            g.setOpacity (1.0f);
            g.setColour (Colours::black.withAlpha (0.55f));
            g.drawEllipse (capArea.reduced (0.5f), 1.0f);

            ColourGradient spec (Colours::white.withAlpha (0.40f), centre.x - Rc * 0.35f, centre.y - Rc * 0.55f,
                                 Colours::transparentWhite, centre.x, centre.y + Rc * 0.1f, true);
            g.setGradientFill (spec);
            g.fillEllipse (capArea.reduced (Rc * 0.18f));
        }

        // 4. engraved pointer line
        {
            const float sx = std::sin (angle), cy = -std::cos (angle);
            const Point<float> a (centre.x + sx * Rc * 0.78f, centre.y + cy * Rc * 0.78f);
            const Point<float> b (centre.x + sx * Rc * 0.28f, centre.y + cy * Rc * 0.28f);
            const float t = jmax (1.4f, side * 0.022f);
            g.setColour (Colours::white.withAlpha (0.45f));
            g.drawLine ({ a.translated (0.6f, 0.9f), b.translated (0.6f, 0.9f) }, t * 0.8f);
            g.setColour (Colour (0xff0e0c0a).withAlpha (alpha));
            g.drawLine ({ a, b }, t);
        }
    }

    //==============================================================================
    void LookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
    {
        const auto r = Rectangle<float> ((float) w, (float) h).reduced (0.5f, 1.0f);
        drawInsetWindow (g, r, 3.0f);
        ColourGradient glow (colours::amber.withAlpha (box.isMouseOver() ? 0.10f : 0.06f), r.getCentreX(), r.getCentreY(),
                             Colours::transparentBlack, r.getX(), r.getCentreY(), true);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (r, 3.0f);
        Path arrow;
        const float ax = r.getRight() - 14.0f, ay = r.getCentreY();
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (colours::vfd.withAlpha (0.85f));
        g.fillPath (arrow);
    }

    Font LookAndFeel::getComboBoxFont (ComboBox&) { return labelFont (13.0f); }

    void LookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
    {
        label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (Justification::centred);
    }

    void LookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
    {
        g.fillAll (findColour (PopupMenu::backgroundColourId));
        g.setColour (colours::amber.withAlpha (0.35f));
        g.drawRect (0, 0, w, h, 1);
    }

    void LookAndFeel::drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive,
                                         bool isHighlighted, bool isTicked, bool hasSubMenu, const String& text,
                                         const String& shortcut, const Drawable* icon, const Colour* textColour)
    {
        if (isSeparator)
        {
            g.setColour (colours::amber.withAlpha (0.2f));
            g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
            return;
        }
        auto r = area.reduced (2, 0);
        if (isHighlighted && isActive)
        {
            g.setColour (findColour (PopupMenu::highlightedBackgroundColourId));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
        }
        auto col = textColour != nullptr ? *textColour
                 : (isHighlighted ? findColour (PopupMenu::highlightedTextColourId) : findColour (PopupMenu::textColourId));
        if (! isActive) col = col.withAlpha (0.4f);

        auto tickArea = r.removeFromLeft (r.getHeight());
        if (isTicked)
        {
            g.setColour (colours::amber);
            g.fillEllipse (tickArea.toFloat().withSizeKeepingCentre (7, 7));
            g.setColour (colours::amber.withAlpha (0.3f));
            g.fillEllipse (tickArea.toFloat().withSizeKeepingCentre (13, 13));
        }
        else if (icon != nullptr)
            icon->drawWithin (g, tickArea.toFloat().reduced (4), RectanglePlacement::centred, 1.0f);

        g.setColour (col);
        g.setFont (getPopupMenuFont());
        g.drawFittedText (text, r.reduced (4, 0), Justification::centredLeft, 1);
        if (hasSubMenu)
        {
            Path p;
            const float ax = (float) r.getRight() - 10.0f, ay = (float) r.getCentreY();
            p.addTriangle (ax, ay - 4.0f, ax, ay + 4.0f, ax + 5.0f, ay);
            g.fillPath (p);
        }
        if (shortcut.isNotEmpty())
            g.drawText (shortcut, r.reduced (8, 0), Justification::centredRight);
    }

    void LookAndFeel::drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& name)
    {
        g.setFont (labelFont (11.0f));
        g.setColour (colours::amber.withAlpha (0.75f));
        g.drawText (name.toUpperCase(), area.reduced (10, 0), Justification::bottomLeft);
    }

    Font LookAndFeel::getPopupMenuFont() { return labelFont (13.0f); }

    void LookAndFeel::drawBubble (Graphics& g, BubbleComponent&, const Point<float>&, const Rectangle<float>& body)
    {
        drawInsetWindow (g, body, 3.0f);
        g.setColour (colours::amber.withAlpha (0.5f));
        g.drawRoundedRectangle (body.reduced (0.5f), 3.0f, 1.0f);
    }

    Font LookAndFeel::getSliderPopupFont (Slider&) { return labelFont (13.0f); }
    int LookAndFeel::getSliderPopupPlacement (Slider&) { return BubbleComponent::above; }

    void LookAndFeel::drawCornerResizer (Graphics& g, int w, int h, bool over, bool dragging)
    {
        const float s = (float) jmin (w, h);
        for (float k = 0.25f; k < 1.0f; k += 0.25f)
        {
            const float o = s * k;
            // sits on the walnut cheek, so the grooves are light rather than dark
            g.setColour (Colours::black.withAlpha (0.5f));
            g.drawLine ((float) w - o + 1.0f, (float) h, (float) w, (float) h - o + 1.0f, 1.0f);
            g.setColour ((over || dragging) ? colours::amber : colours::panel.withAlpha (0.75f));
            g.drawLine ((float) w - o, (float) h, (float) w, (float) h - o, 1.2f);
        }
    }
}

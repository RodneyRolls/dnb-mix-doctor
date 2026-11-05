#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

/** 1970s hi-fi receiver styling: champagne brushed aluminium, walnut, machined knobs, amber dial lamps. */
namespace hifi
{
    namespace colours
    {
        const juce::Colour panelLight  { 0xffe9dfc6 };
        const juce::Colour panel       { 0xffd3c6a6 };
        const juce::Colour panelDark   { 0xffa99b7b };
        const juce::Colour walnut      { 0xff4a2a16 };
        const juce::Colour walnutLight { 0xff7a4a2a };
        const juce::Colour walnutDark  { 0xff26140a };
        const juce::Colour engraving   { 0xff2a2521 };
        const juce::Colour wellCentre  { 0xff231812 };
        const juce::Colour wellEdge    { 0xff080504 };
        const juce::Colour amber       { 0xffffa833 };
        const juce::Colour amberHot    { 0xffffd27a };
        const juce::Colour amberDim    { 0xff4a2f12 };
        const juce::Colour vfd         { 0xffffb53a };
        const juce::Colour vfdGreen    { 0xff8dffc6 };
        const juce::Colour glass       { 0xff0d0a07 };
        const juce::Colour subRed      { 0xffff5a2a };
        const juce::Colour midAmber    { 0xffffb03a };
        const juce::Colour highCyan    { 0xffbdf3ff };
        const juce::Colour clipRed     { 0xffff2e2e };
    }

    /** Eurostile/Microgramma-style font: embedded Michroma (OFL) when present at build time, otherwise
        the best wide-geometric system face that exists (Eurostile, Bahnschrift, Avenir Next ...). */
    juce::Font labelFont (float height, bool bold = false);
    /** Monospaced face used for small numeric text. */
    juce::Font monoFont (float height);

    /** Draws text as a 7-segment vacuum-fluorescent display, centred in area. */
    void drawVfdText (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, juce::Colour colour,
                      bool rightAlign = false);

    /** Dark inset window used behind VFD readouts and the preset display. */
    void drawInsetWindow (juce::Graphics&, juce::Rectangle<float> area, float corner = 4.0f);

    /** Text with an engraved/silkscreened look on the aluminium panel. */
    void drawEngravedText (juce::Graphics&, const juce::String&, juce::Rectangle<float> area, const juce::Font&,
                           juce::Justification = juce::Justification::centred);

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;

        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

        void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
        void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
        void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
        juce::Font getPopupMenuFont() override;

        void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip, const juce::Rectangle<float>& body) override;
        juce::Font getSliderPopupFont (juce::Slider&) override;
        int getSliderPopupPlacement (juce::Slider&) override;

        void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

        juce::Font getAlertWindowTitleFont() override   { return labelFont (16.0f, true); }
        juce::Font getAlertWindowMessageFont() override { return labelFont (13.0f); }
        juce::Font getTextButtonFont (juce::TextButton&, int h) override { return labelFont (juce::jmin (14.0f, h * 0.5f)); }

    private:
        /** Cached machined-aluminium cap (knurled rim + concentric brushing), keyed by pixel diameter. */
        const juce::Image& getCapImage (int pixelDiameter, bool face);
        std::map<int, juce::Image> capCache;
    };
}

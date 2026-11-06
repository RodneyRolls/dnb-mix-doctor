#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** DnB Mix Doctor V2 styling: dark graphite surfaces, soft rounded cards, cyan -> violet accent gradient. */
namespace mcui
{
    namespace colours
    {
        const juce::Colour bg0       { 0xff0a0d12 };
        const juce::Colour bg1       { 0xff10141b };
        const juce::Colour card      { 0xff151a22 };
        const juce::Colour cardHi    { 0xff1b212b };
        const juce::Colour control   { 0xff1d2430 };
        const juce::Colour controlHi { 0xff262f3d };
        const juce::Colour screen    { 0xff0b0f15 };
        const juce::Colour text      { 0xffe9eef5 };
        const juce::Colour textDim   { 0xff8d99ab };
        const juce::Colour textFaint { 0xff4f5a6b };
        const juce::Colour cyan      { 0xff22d3ee };
        const juce::Colour violet    { 0xff8b5cf6 };
        const juce::Colour pink      { 0xfff472b6 };
        const juce::Colour good      { 0xff34d399 };
        const juce::Colour warn      { 0xfffbbf24 };
        const juce::Colour bad       { 0xfff43f5e };
    }

    /** Clean geometric sans: Inter / SF Pro / Segoe UI / Helvetica Neue, whichever exists. */
    juce::Font font (float height, bool bold = false);

    /** Width of a string in the given font. */
    float textWidth (const juce::Font&, const juce::String&);

    /** Rounded surface with a soft vertical gradient, hairline border and top highlight. */
    void drawCard (juce::Graphics&, juce::Rectangle<float> area, float radius = 12.0f,
                   juce::Colour top = colours::cardHi, juce::Colour bottom = colours::card);

    /** Soft radial glow (used behind lit elements and as background light blobs). */
    void drawGlow (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour colour);

    /** Small upper-case caption with letter spacing. */
    void drawCaption (juce::Graphics&, const juce::String&, juce::Rectangle<float> area,
                      juce::Justification = juce::Justification::centredLeft, juce::Colour = colours::textDim,
                      float height = 10.0f);

    /** The brand gradient between two points. */
    juce::ColourGradient accentGradient (juce::Point<float> from, juce::Point<float> to, float alpha = 1.0f);

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
        int getPopupMenuBorderSize() override { return 6; }

        void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

        juce::Font getAlertWindowTitleFont() override   { return font (16.0f, true); }
        juce::Font getAlertWindowMessageFont() override { return font (13.0f); }
        juce::Font getTextButtonFont (juce::TextButton&, int h) override { return font (juce::jmin (14.0f, h * 0.5f), true); }
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    };
}

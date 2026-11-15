#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Params.h"
#include "../analysis/LearnAnalyzer.h"
#include "../analysis/LearnMapper.h"
#include "Theme.h"
#include "Widgets.h"

namespace mcui
{
    //==============================================================================
    /** One vertical EQ fader: drag up/down (shift = fine), wheel, double-click or Alt + click = 0 dB, click selects
        the band. Drawn by itself (thin track, accent fill from 0 dB, value on top, frequency underneath). A DYNAMIC
        band shows a "DYN" tag and a bright marker at the gain it is applying right now. */
    class EqFader : public juce::Slider
    {
    public:
        static constexpr float trackTop = 30.0f, trackBottomPad = 26.0f;

        EqFader() : juce::Slider (RotaryVerticalDrag, NoTextBox)   // drag mode: a click never makes the band jump
        {
            setScrollWheelEnabled (true);
        }

        void setLabel (const juce::String& s) { label = s; }
        void setState (bool dynamic, bool selected, float liveDb)
        {
            if (dynamic != dyn || selected != sel || std::abs (liveDb - live) > 0.05f)
            { dyn = dynamic; sel = selected; live = liveDb; repaint(); }
        }
        std::function<void()> onSelect;

        void mouseDown (const juce::MouseEvent& e) override
        {
            lastY = e.position.y;
            if (onSelect) onSelect();
            juce::Slider::mouseDown (e);
        }
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

            if (sel)
            {
                g.setColour (Colours::white.withAlpha (0.045f));
                g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f, 2.0f), 6.0f);
            }

            // track
            g.setColour (Colours::white.withAlpha (0.07f));
            g.fillRoundedRectangle (Rectangle<float> (cx - 2.0f, trackTop, 4.0f, trackHeight()), 2.0f);

            // fill from 0 dB to the value: cyan for boosts, pink for cuts (dimmer on a dynamic band: it's the maximum)
            if (std::abs (db) > 0.05f)
            {
                const auto c = db > 0 ? colours::cyan : colours::pink;
                const auto fill = Rectangle<float> (cx - 2.0f, jmin (y0, yv), 4.0f, std::abs (yv - y0));
                g.setColour (c.withAlpha (lit ? (dyn ? 0.35f : 0.85f) : 0.3f));
                g.fillRoundedRectangle (fill, 2.0f);
                if (lit && ! dyn) drawGlow (g, { cx, yv }, 18.0f, c.withAlpha (0.35f));
            }

            // dynamic band: what it is applying right now
            if (dyn && std::abs (live) > 0.05f)
            {
                const auto c = live > 0 ? colours::cyan : colours::pink;
                const float yl = yForDb (live);
                g.setColour (c);
                g.fillRoundedRectangle (Rectangle<float> (cx - 2.0f, jmin (y0, yl), 4.0f, std::abs (yl - y0)), 2.0f);
                drawGlow (g, { cx, yl }, 16.0f, c.withAlpha (0.45f));
            }

            // thumb
            auto thumb = Rectangle<float> (jmin (34.0f, (float) getWidth() - 10.0f), 13.0f).withCentre ({ cx, yv });
            g.setColour (hot ? colours::controlHi.brighter (0.15f) : colours::controlHi);
            g.fillRoundedRectangle (thumb, 4.0f);
            g.setColour (sel ? colours::cyan.withAlpha (0.8f) : Colours::white.withAlpha (hot ? 0.35f : 0.18f));
            g.drawRoundedRectangle (thumb.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (lit ? Colours::white.withAlpha (0.9f) : colours::textFaint);
            g.fillRect (thumb.withSizeKeepingCentre (thumb.getWidth() - 14.0f, 1.5f));

            // value on top, frequency (and DYN tag) underneath
            g.setFont (font (10.5f, true));
            g.setColour (std::abs (db) < 0.05f ? colours::textDim : (db > 0 ? colours::cyan : colours::pink).withAlpha (lit ? 1.0f : 0.5f));
            const String v = std::abs (db) < 0.05f ? String ("0.0") : (db > 0 ? "+" : "") + String (db, 1);
            g.drawText (v, Rectangle<float> (0.0f, 6.0f, (float) getWidth(), 14.0f), Justification::centred, false);
            g.setColour (sel ? colours::text : (hot ? colours::text : colours::textDim));
            g.drawText (label, Rectangle<float> (0.0f, (float) getHeight() - 18.0f, (float) getWidth(), 14.0f), Justification::centred, false);
            if (dyn)
            {
                g.setColour (colours::warn);
                g.setFont (font (8.5f, true));
                g.drawText ("DYN", Rectangle<float> (0.0f, 19.0f, (float) getWidth(), 10.0f), Justification::centred, false);
            }
        }

    private:
        float lastY = 0, live = 0;
        bool dyn = false, sel = false;
        juce::String label;
    };

    //==============================================================================
    /** The ADVANCED panel: 16-band EQ where every band can be NORMAL (static) or DYNAMIC (only boosts/cuts while
        that band is louder than its threshold), with a live response curve, EQ MIX / ON / FLAT, and LEARN, which
        listens to the track and sets the bands to move it towards a chosen genre's tonal balance. */
    class EqPanel : public juce::Component, private juce::Timer
    {
    public:
        static constexpr int learnRequester = 100;
        using SetParam = std::function<void (const juce::String&, float)>;

        EqPanel (juce::AudioProcessorValueTreeState& s, std::function<double()> sampleRate, mcdsp::LearnAnalyzer& l,
                 SetParam setter, int& genreRef, std::function<float (int)> liveGain)
            : apvts (s), getSampleRate (std::move (sampleRate)), learn (l), setParam (std::move (setter)),
              genre (genreRef), getLive (std::move (liveGain))
        {
            setOpaque (false);
            for (int i = 0; i < params::numEqBands; ++i)
            {
                auto& f = faders[(size_t) i];
                f.setLabel (params::eqFreqLabel (i));
                f.setDoubleClickReturnValue (true, 0.0, juce::ModifierKeys::altModifier);
                f.setTitle ("EQ " + params::eqFreqLabel (i));
                f.onSelect = [this, i] { selectBand (i); };
                addAndMakeVisible (f);
                attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, params::eqBand (i), f));
            }
            auto setupKnob = [this] (Knob& k, const juce::String& title, const juce::String& id)
            {
                k.setTitle (title);
                if (auto* p = apvts.getParameter (id))
                    k.setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()), juce::ModifierKeys::altModifier);
                addAndMakeVisible (k);
                attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, k));
            };
            setupKnob (mixKnob, "EQ MIX", params::eqMix);
            setupKnob (attackKnob, "ATTACK", params::eqDynAttack);
            setupKnob (releaseKnob, "RELEASE", params::eqDynRelease);
            thrKnob.setTitle ("THRESHOLD");
            thrKnob.setDoubleClickReturnValue (true, (double) params::eqThrDefault, juce::ModifierKeys::altModifier);
            addAndMakeVisible (thrKnob);

            onButton.setText ("EQ ON");
            onButton.setClickingTogglesState (true);
            onAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, params::eqOn, onButton);
            flatButton.setText ("FLAT");
            flatButton.onClick = [this] { flatten(); };
            closeButton.setText ("CLOSE");
            closeButton.onClick = [this] { if (onClose) onClose(); };
            normalButton.setText ("NORMAL");
            normalButton.onClick = [this] { setParam (params::eqDyn (selected), 0.0f); };
            dynButton.setText ("DYNAMIC");
            dynButton.onClick = [this] { setParam (params::eqDyn (selected), 1.0f); };
            learnButton.setText ("LEARN");
            learnButton.onClick = [this] { toggleLearn(); };
            for (auto* b : { &onButton, &flatButton, &closeButton, &normalButton, &dynButton, &learnButton }) addAndMakeVisible (b);

            for (size_t i = 0; i < learnmap::genres().size(); ++i)
                genreBox.addItem (learnmap::genres()[i].name, (int) i + 1);
            genreBox.setSelectedId (juce::jlimit (0, (int) learnmap::genres().size() - 1, genre) + 1, juce::dontSendNotification);
            genreBox.onChange = [this] { genre = genreBox.getSelectedId() - 1; };
            genreBox.setTooltip ("Genre whose tonal balance EQ LEARN aims for");
            addAndMakeVisible (genreBox);

            selectBand (8);
            startTimerHz (30);
        }

        std::function<void()> onClose;
        /** Runs one UI update now (the timer does this 30 times a second; used by the offline snapshot tool). */
        void pollNow() { timerCallback(); }

        void resized() override
        {
            using namespace juce;
            auto r = getLocalBounds();

            // header: title on the left; genre, LEARN and CLOSE on the right
            auto head = r.removeFromTop (40).reduced (16, 8);
            closeButton.setBounds (head.removeFromRight (70));
            head.removeFromRight (10);
            learnButton.setBounds (head.removeFromRight (104));
            head.removeFromRight (8);
            genreBox.setBounds (head.removeFromRight (150));
            headerText = head.withTrimmedLeft (200);

            auto side = r.removeFromRight (392).reduced (14, 6);
            faderArea = r.withTrimmedLeft (48).withTrimmedRight (6).withTrimmedBottom (10);
            const float colW = (float) faderArea.getWidth() / (float) params::numEqBands;
            for (int i = 0; i < params::numEqBands; ++i)
                faders[(size_t) i].setBounds (Rectangle<float> ((float) faderArea.getX() + colW * (float) i, (float) faderArea.getY(),
                                                                colW, (float) faderArea.getHeight()).toNearestInt());

            // side, column A: EQ MIX, EQ ON / FLAT
            auto colA = side.removeFromLeft (150);
            side.removeFromLeft (14);
            mixArea = colA.removeFromTop (128);
            mixKnob.setBounds (mixArea.withTrimmedTop (18).withSizeKeepingCentre (92, 92));
            colA.removeFromTop (8);
            onButton.setBounds (colA.removeFromTop (30).reduced (6, 0));
            colA.removeFromTop (8);
            flatButton.setBounds (colA.removeFromTop (30).reduced (6, 0));

            // side, column B: the selected band (NORMAL / DYNAMIC, THRESHOLD) and the dynamic ATTACK / RELEASE
            bandArea = side;
            auto b = side.withTrimmedTop (20);
            auto modeRow = b.removeFromTop (28);
            normalButton.setBounds (modeRow.removeFromLeft (modeRow.getWidth() / 2).reduced (3, 0));
            dynButton.setBounds (modeRow.reduced (3, 0));
            b.removeFromTop (4);
            auto knobs = b.removeFromTop (110);
            const int kw = knobs.getWidth() / 3;
            thrKnob.setBounds (knobs.removeFromLeft (kw).withTrimmedTop (18).withSizeKeepingCentre (70, 70));
            attackKnob.setBounds (knobs.removeFromLeft (kw).withTrimmedTop (18).withSizeKeepingCentre (70, 70));
            releaseKnob.setBounds (knobs.withTrimmedTop (18).withSizeKeepingCentre (70, 70));
            statusArea = b.reduced (2, 2);
        }

        void paint (juce::Graphics& g) override
        {
            using namespace juce;
            const auto bounds = getLocalBounds().toFloat();
            drawCard (g, bounds, 16.0f);

            drawCaption (g, "Advanced  -  16-band EQ", { 22.0f, 12.0f, 200.0f, 16.0f }, Justification::centredLeft, colours::text, 11.0f);
            drawCaption (g, "Click a band to select it. Alt + click or double-click resets it.",
                         headerText.toFloat(), Justification::centredLeft, colours::textFaint, 9.5f);

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

            // response curve of what the EQ is doing right now (EQ MIX, ON and dynamic bands applied)
            const bool on = apvts.getRawParameterValue (params::eqOn)->load() > 0.5f;
            const float depth = on ? apvts.getRawParameterValue (params::eqMix)->load() * 0.01f : 0.0f;
            const double sr = jmax (22050.0, getSampleRate ? getSampleRate() : 48000.0);
            std::array<float, params::numEqBands> gains {}, maxGains {};
            bool anyDyn = false;
            for (int b = 0; b < params::numEqBands; ++b)
            {
                maxGains[(size_t) b] = apvts.getRawParameterValue (params::eqBand (b))->load() * depth;
                const bool d = apvts.getRawParameterValue (params::eqDyn (b))->load() > 0.5f;
                anyDyn = anyDyn || d;
                gains[(size_t) b] = d ? (getLive ? getLive (b) : 0.0f) : maxGains[(size_t) b];
            }
            const float x0 = (float) faderArea.getX(), x1 = (float) faderArea.getRight();
            auto curvePath = [&] (const std::array<float, params::numEqBands>& gv, Path& curve, Path* fill)
            {
                const int steps = (int) (x1 - x0);
                for (int s = 0; s <= steps; ++s)
                {
                    const float x = x0 + (float) s;
                    const float db = responseDb (freqForX (x), gv, sr);
                    const float y = jlimit (scr.getY() + 2.0f, scr.getBottom() - 2.0f, yFor (jlimit (-15.0f, 15.0f, db)));
                    if (s == 0) { curve.startNewSubPath (x, y); if (fill) fill->startNewSubPath (x, yFor (0.0f)); }
                    else          curve.lineTo (x, y);
                    if (fill) fill->lineTo (x, y);
                }
                if (fill) { fill->lineTo (x1, yFor (0.0f)); fill->closeSubPath(); }
            };
            if (anyDyn)   // dashed: the most the dynamic bands can do
            {
                Path maxCurve;
                curvePath (maxGains, maxCurve, nullptr);
                Path dashed;
                const float dashes[] = { 4.0f, 4.0f };
                PathStrokeType (1.2f).createDashedStroke (dashed, maxCurve, dashes, 2);
                g.setColour (Colours::white.withAlpha (0.22f));
                g.fillPath (dashed);
            }
            Path curve, fill;
            curvePath (gains, curve, &fill);
            g.setGradientFill (accentGradient ({ x0, 0.0f }, { x1, 0.0f }, 0.16f));
            g.fillPath (fill);
            g.setGradientFill (accentGradient ({ x0, 0.0f }, { x1, 0.0f }, on ? 0.95f : 0.35f));
            g.strokePath (curve, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

            // right-hand controls
            g.setColour (Colours::white.withAlpha (0.06f));
            g.fillRect ((float) mixArea.getX() - 12.0f, 46.0f, 1.0f, bounds.getHeight() - 60.0f);
            g.fillRect ((float) bandArea.getX() - 8.0f, 46.0f, 1.0f, bounds.getHeight() - 60.0f);
            drawCaption (g, "EQ Mix", mixArea.toFloat().withHeight (14.0f), Justification::centred, colours::textDim, 10.0f);
            drawCaption (g, "0 % = flat   100 % = full curve",
                         Rectangle<float> ((float) mixArea.getX(), (float) flatButton.getBottom() + 10.0f, (float) mixArea.getWidth(), 12.0f),
                         Justification::centred, colours::textFaint, 9.0f);

            drawCaption (g, "Band  " + params::eqFreqLabel (selected) + (params::eqFreqs[(size_t) selected] >= 1000.0f ? "Hz" : " Hz"),
                         bandArea.toFloat().withHeight (14.0f), Justification::centredLeft, colours::text, 10.0f);
            const bool selDyn = apvts.getRawParameterValue (params::eqDyn (selected))->load() > 0.5f;
            for (auto* k : { &thrKnob, &attackKnob, &releaseKnob })
            {
                const String cap = k == &thrKnob ? "THRESHOLD" : (k == &attackKnob ? "ATTACK" : "RELEASE");
                drawCaption (g, cap, k->getBounds().toFloat().withY ((float) k->getY() - 17.0f).withHeight (13.0f),
                             Justification::centred, selDyn ? colours::textDim : colours::textFaint, 9.0f);
            }

            // status: learn progress / result, or what DYNAMIC means
            String line;
            Colour c = colours::textFaint;
            if (learn.isListening() && learn.requester() == learnRequester)
            {
                line = learn.hasSignal() ? "Listening...  " + String (roundToInt (learn.getProgress() * 100.0f)) + " %"
                                         : "Waiting for audio - press play in your DAW";
                c = colours::cyan;
            }
            else if (learnSummary.isNotEmpty()) { line = learnSummary; c = colours::text; }
            else line = selDyn ? "DYNAMIC: this band only boosts / cuts while it is louder than THRESHOLD (all of it 6 dB above)."
                               : "NORMAL: this band is a fixed EQ. Switch to DYNAMIC to make it react to the music.";
            g.setColour (c);
            g.setFont (font (10.0f));
            g.drawFittedText (line, statusArea, Justification::topLeft, 3);
        }

    private:
        void selectBand (int i)
        {
            selected = juce::jlimit (0, params::numEqBands - 1, i);
            thrAttachment.reset();
            thrAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, params::eqThr (selected), thrKnob);
            refreshBandState();
            repaint();
        }

        void refreshBandState()
        {
            const bool d = apvts.getRawParameterValue (params::eqDyn (selected))->load() > 0.5f;
            normalButton.setToggleState (! d, juce::dontSendNotification);
            dynButton.setToggleState (d, juce::dontSendNotification);
            thrKnob.setEnabled (d);
            attackKnob.setEnabled (d);
            releaseKnob.setEnabled (d);
        }

        void timerCallback() override
        {
            // learn
            const bool mine = learn.requester() == learnRequester;
            const bool listening = mine && learn.isListening();
            learnButton.setToggleState (listening, juce::dontSendNotification);
            learnButton.setBlink (listening && ! learn.hasSignal());
            learnButton.setProgress (listening ? juce::jmax (0.001f, learn.getProgress()) : 0.0f);
            learnButton.setText (listening ? "STOP" : "LEARN");
            if (mine && learn.isDone())
            {
                const auto stats = learn.getStats();
                learn.acknowledge();
                const auto g = learnmap::eqForGenre (stats, genre, params::eqFreqs);
                int big = 0;
                for (int b = 0; b < params::numEqBands; ++b)
                {
                    setParam (params::eqBand (b), g[(size_t) b]);
                    if (std::abs (g[(size_t) b]) > std::abs (g[(size_t) big])) big = b;
                }
                setParam (params::eqOn, 1.0f);
                learnSummary = "Learned: matched towards " + juce::String (learnmap::genres()[(size_t) juce::jlimit (0, (int) learnmap::genres().size() - 1, genre)].name)
                             + ". Biggest move " + (g[(size_t) big] > 0 ? "+" : "") + juce::String (g[(size_t) big], 1) + " dB at "
                             + params::eqFreqLabel (big) + (params::eqFreqs[(size_t) big] >= 1000.0f ? "Hz." : " Hz.");
            }

            // faders: dynamic flag, selection, live gain
            bool anyDyn = false;
            for (int b = 0; b < params::numEqBands; ++b)
            {
                const bool d = apvts.getRawParameterValue (params::eqDyn (b))->load() > 0.5f;
                anyDyn = anyDyn || d;
                faders[(size_t) b].setState (d, b == selected, d && getLive ? getLive (b) : 0.0f);
            }
            refreshBandState();

            float sig = apvts.getRawParameterValue (params::eqMix)->load() + 7.0f * apvts.getRawParameterValue (params::eqOn)->load()
                      + (float) (getSampleRate ? getSampleRate() : 0.0) * 1.0e-3f + (listening ? learn.getProgress() * 13.0f : 0.0f)
                      + (float) learnSummary.length();
            for (int b = 0; b < params::numEqBands; ++b)
                sig += apvts.getRawParameterValue (params::eqBand (b))->load() * (float) (b + 3) * 0.37f
                     + apvts.getRawParameterValue (params::eqDyn (b))->load() * (float) (b + 5) * 1.13f;
            if (anyDyn || sig != lastSignature) { lastSignature = sig; repaint(); }
        }

        void toggleLearn()
        {
            if (learn.isListening() && learn.requester() == learnRequester) { learn.cancel(); return; }
            learnSummary.clear();
            learn.start (learnRequester);
        }

        void flatten()
        {
            for (int b = 0; b < params::numEqBands; ++b) setParam (params::eqBand (b), 0.0f);
            learnSummary.clear();
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
        mcdsp::LearnAnalyzer& learn;
        SetParam setParam;
        int& genre;
        std::function<float (int)> getLive;

        std::array<EqFader, params::numEqBands> faders;
        Knob mixKnob, thrKnob, attackKnob, releaseKnob;
        IconButton onButton { "EQ on", IconButton::Icon::none, true },
                   flatButton { "Flat", IconButton::Icon::none, false },
                   closeButton { "Close", IconButton::Icon::none, false },
                   normalButton { "Normal", IconButton::Icon::none, true },
                   dynButton { "Dynamic", IconButton::Icon::none, true },
                   learnButton { "Learn", IconButton::Icon::none, true };
        juce::ComboBox genreBox;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thrAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;
        juce::Rectangle<int> faderArea, mixArea, bandArea, statusArea, headerText;
        int selected = 8;
        juce::String learnSummary;
        float lastSignature = -1.0e9f;
    };
}

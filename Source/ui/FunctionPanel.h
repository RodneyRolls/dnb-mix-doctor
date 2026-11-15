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
    /** A function's ADVANCED page (double-click its knob): its main knob(s), its extra settings, and LEARN,
        which listens to the playing track for a few seconds and sets everything on the page to suit it.
        The right-hand screen shows the learned spectrum with this page's frequencies marked on it. */
    class FunctionPanel : public juce::Component, private juce::Timer
    {
    public:
        using SetParam = std::function<void (const juce::String&, float)>;

        FunctionPanel (juce::AudioProcessorValueTreeState& s, mcdsp::LearnAnalyzer& l, SetParam setter)
            : apvts (s), learn (l), setParam (std::move (setter))
        {
            setOpaque (false);
            learnButton.setText ("LEARN");
            learnButton.onClick = [this] { toggleLearn(); };
            defaultsButton.setText ("DEFAULTS");
            defaultsButton.onClick = [this] { restoreDefaults(); };
            closeButton.setText ("CLOSE");
            closeButton.onClick = [this] { if (onClose) onClose(); };
            for (auto* b : { &learnButton, &defaultsButton, &closeButton }) addAndMakeVisible (b);
            startTimerHz (30);
        }

        std::function<void()> onClose;
        int getPage() const { return page; }
        /** Runs one UI update now (the timer does this 30 times a second; used by the offline snapshot tool). */
        void pollNow() { timerCallback(); }

        /** Shows the page for advparams::pages()[index]. */
        void setPage (int index)
        {
            if (index == page) return;
            if (learn.isListening() && learn.requester() == page) learn.cancel();
            page = index;
            attachments.clear();
            knobs.clear();
            cells.clear();
            summary.clear();
            hasStats = false;
            if (page < 0) return;

            const auto& pg = advparams::pages()[(size_t) page];
            auto addKnob = [this] (const juce::String& id, const juce::String& label, bool isMain)
            {
                auto k = std::make_unique<Knob>();
                k->setTitle (label);
                if (auto* p = apvts.getParameter (id))
                    k->setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()), juce::ModifierKeys::altModifier);
                k->setBipolar (id == params::width || id == "adv_glueThr");
                addAndMakeVisible (*k);
                attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, *k));
                cells.push_back ({ label, isMain });
                knobs.push_back (std::move (k));
            };
            for (auto* id : pg.mainIds)
                addKnob (id, params::knobLabels[params::knobOrder.indexOf (id)], true);
            for (auto* id : pg.advIds)
                if (auto* d = advparams::find (id)) addKnob (id, d->label, false);
            resized();
            repaint();
        }

        void resized() override
        {
            using namespace juce;
            auto r = getLocalBounds().reduced (16, 10);
            auto top = r.removeFromTop (26);
            closeButton.setBounds (top.removeFromRight (74));
            top.removeFromRight (8);
            defaultsButton.setBounds (top.removeFromRight (96));
            top.removeFromRight (8);
            learnButton.setBounds (top.removeFromRight (150));

            r.removeFromTop (8);
            screenArea = r.removeFromRight (420).reduced (0, 4);
            r.removeFromRight (16);
            knobArea = r;
            const int n = (int) knobs.size();
            if (n == 0) return;
            const int cellW = jmin (124, knobArea.getWidth() / n);
            const int x0 = knobArea.getX() + (knobArea.getWidth() - cellW * n) / 2;
            for (int i = 0; i < n; ++i)
            {
                const Rectangle<int> cell (x0 + i * cellW, knobArea.getY() + 34, cellW, knobArea.getHeight() - 34);
                cells[(size_t) i].bounds = cell;
                knobs[(size_t) i]->setBounds (cell.withTrimmedTop (22).withHeight (104).withSizeKeepingCentre (104, 104));
            }
        }

        void paint (juce::Graphics& g) override
        {
            using namespace juce;
            const auto bounds = getLocalBounds().toFloat();
            drawCard (g, bounds, 16.0f);
            if (page < 0) return;
            const auto& pg = advparams::pages()[(size_t) page];

            drawCaption (g, String ("Advanced  -  ") + pg.title, { 22.0f, 12.0f, 360.0f, 16.0f }, Justification::centredLeft, colours::text, 11.0f);
            g.setColour (colours::textDim);
            g.setFont (font (12.0f));
            g.drawText (pg.blurb, Rectangle<float> ((float) knobArea.getX(), (float) knobArea.getY() + 2.0f, (float) knobArea.getWidth(), 16.0f),
                        Justification::centredLeft, true);

            // knob captions; the page's own knob(s) are marked with an accent underline
            for (auto& c : cells)
            {
                auto cap = c.bounds.toFloat().withHeight (16.0f);
                drawCaption (g, c.label, cap, Justification::centred, c.main ? colours::text : colours::textDim, 10.0f);
                if (c.main)
                {
                    g.setGradientFill (accentGradient ({ cap.getCentreX() - 16.0f, 0.0f }, { cap.getCentreX() + 16.0f, 0.0f }, 0.9f));
                    g.fillRoundedRectangle (cap.getCentreX() - 16.0f, cap.getBottom() + 1.0f, 32.0f, 2.0f, 1.0f);
                }
            }
            g.setColour (Colours::white.withAlpha (0.06f));
            g.fillRect ((float) screenArea.getX() - 9.0f, (float) screenArea.getY(), 1.0f, (float) screenArea.getHeight());

            drawScreen (g, pg);
        }

    private:
        struct Cell { juce::String label; bool main; juce::Rectangle<int> bounds {}; };

        void drawScreen (juce::Graphics& g, const advparams::Page& pg)
        {
            using namespace juce;
            auto scr = screenArea.toFloat();
            g.setColour (colours::screen);
            g.fillRoundedRectangle (scr, 10.0f);
            auto inner = scr.reduced (12.0f, 10.0f);

            auto status = inner.removeFromBottom (40.0f);
            auto plot = inner.withTrimmedBottom (14.0f);

            // frequency axis (log, 30 Hz .. 18 kHz)
            auto xFor = [&plot] (float hz) { return plot.getX() + plot.getWidth() * std::log (jlimit (30.0f, 18000.0f, hz) / 30.0f) / std::log (600.0f); };
            g.setFont (font (9.0f));
            for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
            {
                const float x = xFor (f);
                g.setColour (Colours::white.withAlpha (0.04f));
                g.drawVerticalLine (roundToInt (x), plot.getY(), plot.getBottom());
                g.setColour (colours::textFaint);
                g.drawText (f >= 1000.0f ? String ((int) (f / 1000.0f)) + "k" : String ((int) f),
                            Rectangle<float> (x - 16.0f, plot.getBottom() + 1.0f, 32.0f, 11.0f), Justification::centred, false);
            }

            // learned spectrum
            if (hasStats)
            {
                float top = -200.0f;
                for (auto v : stats.bandDb) top = std::max (top, v);
                const float range = 48.0f;
                for (int b = 0; b < mcdsp::LearnStats::numBands; ++b)
                {
                    const float f = mcdsp::LearnStats::freqs[(size_t) b];
                    const float x = xFor (f), w = jmax (3.0f, plot.getWidth() / 34.0f);
                    const float h = plot.getHeight() * jlimit (0.0f, 1.0f, (stats.bandDb[(size_t) b] - (top - range)) / range);
                    g.setGradientFill (accentGradient ({ x, plot.getBottom() }, { x, plot.getBottom() - h }, 0.55f));
                    g.fillRoundedRectangle (x - w * 0.5f, plot.getBottom() - h, w, h, 1.5f);
                }
            }
            else
            {
                g.setColour (colours::textFaint);
                g.setFont (font (11.0f));
                g.drawText ("LEARN " + String (pg.learnHint), plot.reduced (10.0f, 0.0f), Justification::centred, true);
            }

            // this page's frequencies as markers
            auto marker = [&] (const juce::String& id, const juce::String& name)
            {
                if (auto* v = apvts.getRawParameterValue (id))
                {
                    const float f = v->load(), x = xFor (f);
                    g.setColour (colours::warn.withAlpha (0.85f));
                    g.fillRect (x - 0.75f, plot.getY(), 1.5f, plot.getHeight());
                    g.setFont (font (9.5f, true));
                    g.drawText (name + " " + advparams::format (advparams::Unit::hz, f),
                                Rectangle<float> (jlimit (plot.getX(), plot.getRight() - 90.0f, x + 3.0f), plot.getY(), 90.0f, 12.0f),
                                Justification::centredLeft, false);
                }
            };
            for (auto* id : pg.advIds)
                if (auto* d = advparams::find (id); d != nullptr && d->unit == advparams::Unit::hz) marker (id, d->label);
            for (auto* id : pg.mainIds)
                if (String (id) == params::subMono) marker (id, "XOVER");

            // status line
            String line;
            Colour c = colours::textDim;
            if (learn.isListening() && learn.requester() == page)
            {
                line = learn.hasSignal() ? "Listening...  " + String (roundToInt (learn.getProgress() * 100.0f)) + " %"
                                         : "Waiting for audio - press play in your DAW";
                c = colours::cyan;
            }
            else if (summary.isNotEmpty()) { line = summary; c = colours::text; }
            else line = "Press LEARN while your track plays (about 8 seconds of audio).";
            g.setColour (c);
            g.setFont (font (11.0f));
            g.drawFittedText (line, status.toNearestInt(), Justification::centredLeft, 2);
        }

        void toggleLearn()
        {
            if (page < 0) return;
            if (learn.isListening() && learn.requester() == page) { learn.cancel(); return; }
            summary.clear();
            learn.start (page);
        }

        void restoreDefaults()
        {
            if (page < 0) return;
            for (auto* id : advparams::pages()[(size_t) page].advIds)
                if (auto* d = advparams::find (id)) setParam (id, d->def);
            summary = "Advanced settings back to their defaults.";
            repaint();
        }

        void timerCallback() override
        {
            const bool mine = page >= 0 && learn.requester() == page;
            const bool listening = mine && learn.isListening();
            learnButton.setToggleState (listening, juce::dontSendNotification);
            learnButton.setBlink (listening && ! learn.hasSignal());
            learnButton.setProgress (listening ? juce::jmax (0.001f, learn.getProgress()) : 0.0f);
            learnButton.setText (listening ? "STOP" : "LEARN");

            if (mine && learn.isDone())
            {
                stats = learn.getStats();
                learn.acknowledge();
                hasStats = true;
                const auto& pg = advparams::pages()[(size_t) page];
                const float inputDb = apvts.getRawParameterValue (params::input)->load();
                const float glue = apvts.getRawParameterValue (params::glue)->load() * 0.01f;
                const auto res = learnmap::forPage (pg.knobId, stats, inputDb, glue);
                for (auto& [id, v] : res.values) setParam (id, v);
                summary = "Learned: " + res.summary;
            }
            repaint (screenArea.expanded (2));
        }

        juce::AudioProcessorValueTreeState& apvts;
        mcdsp::LearnAnalyzer& learn;
        SetParam setParam;
        int page = -1;

        std::vector<std::unique_ptr<Knob>> knobs;
        std::vector<Cell> cells;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
        IconButton learnButton { "Learn", IconButton::Icon::none, true },
                   defaultsButton { "Defaults", IconButton::Icon::none, false },
                   closeButton { "Close", IconButton::Icon::none, false };
        juce::Rectangle<int> knobArea, screenArea;

        mcdsp::LearnStats stats;
        bool hasStats = false;
        juce::String summary;
    };
}

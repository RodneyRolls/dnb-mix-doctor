#include "PluginEditor.h"

using namespace juce;
namespace
{
const Colour kBg    { 0xff0e1116 }, kPanel { 0xff161b22 }, kText { 0xffe8ecf1 }, kDim { 0xff8b95a5 },
             kAccent { 0xff19e3b1 }, kOk { 0xff3ddc84 }, kWarn { 0xffffb020 }, kBad { 0xffff4d5e };
}

MixDoctorEditor::MixDoctorEditor (MixDoctorProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setSize (900, 600);

    styleToggle (cleanB, "Clean low-end  (DC + sub-25 Hz)");
    styleToggle (monoB,  "Mono low-end  below");
    styleToggle (toneB,  "Auto tone  -");
    styleToggle (harshB, "Harshness tamer  -");
    styleToggle (limB,   "True-peak limiter");

    styleSlider (monoFreqS, " Hz");  styleSlider (monoAmtS, " %");  styleSlider (toneAmtS, " %");
    styleSlider (toneMaxS, " dB");   styleSlider (harshMaxS, " dB"); styleSlider (ceilS, " dBTP");

    auto cap = [this] (Label& l, const String& t) { l.setText (t, dontSendNotification); l.setColour (Label::textColourId, kDim); l.setFont (FontOptions (12.f)); addAndMakeVisible (l); };
    cap (monoFreqL, "freq"); cap (monoAmtL, "amount"); cap (toneAmtL, "amount"); cap (toneMaxL, "max"); cap (harshMaxL, "max cut"); cap (ceilL, "ceiling");

    styleBox.addItemList (StringArray { "Neurofunk", "Liquid", "Dancefloor", "Captured" }, 1);
    addAndMakeVisible (styleBox);
    captureBtn.setTooltip ("Play a reference track you love for a few seconds, then click: its tonal balance becomes the target.");
    captureBtn.onClick = [this] { proc.captureTarget(); };
    addAndMakeVisible (captureBtn);

    auto& s = proc.apvts;
    cleanA = std::make_unique<BA> (s, "cleanLow", cleanB);  monoA = std::make_unique<BA> (s, "monoLow", monoB);
    toneA  = std::make_unique<BA> (s, "autoTone", toneB);   harshA = std::make_unique<BA> (s, "harsh", harshB);
    limA   = std::make_unique<BA> (s, "limiter", limB);
    monoFreqA = std::make_unique<SA> (s, "monoFreq", monoFreqS); monoAmtA = std::make_unique<SA> (s, "monoAmt", monoAmtS);
    toneAmtA  = std::make_unique<SA> (s, "toneAmt", toneAmtS);   toneMaxA = std::make_unique<SA> (s, "toneMax", toneMaxS);
    harshMaxA = std::make_unique<SA> (s, "harshMax", harshMaxS); ceilA = std::make_unique<SA> (s, "ceiling", ceilS);
    styleA = std::make_unique<CA> (s, "style", styleBox);

    startTimerHz (30);
}

MixDoctorEditor::~MixDoctorEditor() { stopTimer(); }

void MixDoctorEditor::styleSlider (Slider& s, const String& suffix)
{
    s.setSliderStyle (Slider::LinearHorizontal);
    s.setTextBoxStyle (Slider::TextBoxRight, false, 78, 22);
    s.setTextValueSuffix (suffix);
    s.setColour (Slider::trackColourId, kAccent);
    s.setColour (Slider::thumbColourId, kText);
    s.setColour (Slider::backgroundColourId, Colour (0xff2a313c));
    s.setColour (Slider::textBoxTextColourId, kText);
    s.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    addAndMakeVisible (s);
}

void MixDoctorEditor::styleToggle (ToggleButton& b, const String& text)
{
    b.setButtonText (text);
    b.setColour (ToggleButton::textColourId, kText);
    b.setColour (ToggleButton::tickColourId, kAccent);
    b.setColour (ToggleButton::tickDisabledColourId, kDim);
    addAndMakeVisible (b);
}

void MixDoctorEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (52);                                            // header
    auto top = r.removeFromTop (330);
    issuesArea = top.removeFromLeft (520);
    top.removeFromLeft (12);
    bandsArea = top;
    r.removeFromTop (12);
    controlsArea = r;

    auto c = controlsArea.reduced (12, 8);
    const int rowH = 38;
    auto row = [&] { return c.removeFromTop (rowH); };
    auto place = [] (Rectangle<int>& rw, Component& comp, int w) { comp.setBounds (rw.removeFromLeft (w)); };

    { auto rw = row(); place (rw, cleanB, 300); }
    { auto rw = row(); place (rw, monoB, 170); place (rw, monoFreqL, 34); place (rw, monoFreqS, 230); rw.removeFromLeft (14);
      place (rw, monoAmtL, 50); place (rw, monoAmtS, 230); }
    { auto rw = row(); place (rw, toneB, 110); styleBox.setBounds (rw.removeFromLeft (110).reduced (0, 6)); rw.removeFromLeft (10);
      place (rw, toneAmtL, 50); place (rw, toneAmtS, 190); rw.removeFromLeft (10); place (rw, toneMaxL, 30); place (rw, toneMaxS, 160);
      rw.removeFromLeft (8); captureBtn.setBounds (rw.removeFromLeft (110).reduced (0, 6)); }
    { auto rw = row(); place (rw, harshB, 170); place (rw, harshMaxL, 58); place (rw, harshMaxS, 230); }
    { auto rw = row(); place (rw, limB, 170); place (rw, ceilL, 58); place (rw, ceilS, 230); }
}

std::vector<MixDoctorEditor::Issue> MixDoctorEditor::evaluate() const
{
    using namespace mixdoctor;
    const auto& m = proc.doctor.meters;
    const bool ready = m.ready.load() > 0.5f;
    const bool cleanOn = proc.apvts.getRawParameterValue ("cleanLow")->load() > 0.5f,
               monoOn  = proc.apvts.getRawParameterValue ("monoLow")->load()  > 0.5f,
               toneOn  = proc.apvts.getRawParameterValue ("autoTone")->load() > 0.5f,
               harshOn = proc.apvts.getRawParameterValue ("harsh")->load()    > 0.5f,
               limOn   = proc.apvts.getRawParameterValue ("limiter")->load()  > 0.5f;

    std::vector<Issue> v;
    auto fixOrIssue = [] (bool fixed) { return fixed ? std::pair<String, Colour> { "FIXED", kAccent } : std::pair<String, Colour> { "ISSUE", kBad }; };

    {   // 1. peaks
        const float over = m.peakOverDb.load(), gr = m.limGR.load();
        if (over > 0.05f) { auto t = fixOrIssue (limOn); v.push_back ({ "Clipping / peaks", "Peaks reach " + String (over, 1) + " dB over the ceiling" + (limOn ? "  (limiter -" + String (gr, 1) + " dB)" : ""), t.first, t.second }); }
        else               v.push_back ({ "Clipping / peaks", "Peaks stay under the ceiling", "OK", kOk });
    }
    {   // 2. DC
        const float dc = m.dcOffset.load();
        if (dc > 0.002f) { auto t = fixOrIssue (cleanOn); v.push_back ({ "DC offset", "Offset of " + String (dc, 4) + " eats headroom", t.first, t.second }); }
        else              v.push_back ({ "DC offset", "None detected", "OK", kOk });
    }
    {   // 3. rumble
        const float r = m.rumbleRel.load();
        if (ready && r > -14.f) { auto t = fixOrIssue (cleanOn); v.push_back ({ "Subsonic rumble", "Energy below 22 Hz is only " + String (-r, 0) + " dB under the sub", t.first, t.second }); }
        else                      v.push_back ({ "Subsonic rumble", ready ? "Clean below 25 Hz" : "Analysing...", ready ? "OK" : "...", ready ? kOk : kDim });
    }
    {   // 4. low-end width
        const float s = m.lowSideDb.load();
        if (ready && s > -18.f) { auto t = fixOrIssue (monoOn); v.push_back ({ "Low-end width", "Bass side signal is " + String (s, 1) + " dB re mid - not mono-safe", t.first, t.second }); }
        else                      v.push_back ({ "Low-end width", ready ? "Low end is essentially mono" : "Analysing...", ready ? "OK" : "...", ready ? kOk : kDim });
    }
    {   // 5. phase (advisory only - flipping polarity on a master bus is not safe to automate)
        const float c = m.correlation.load();
        if (ready && c < 0.f)      v.push_back ({ "Phase / mono check", "Correlation " + String (c, 2) + ": will partly cancel in mono. Check polarity of stereo FX", "CHECK", kWarn });
        else if (ready && c < 0.3f) v.push_back ({ "Phase / mono check", "Correlation " + String (c, 2) + ": very wide; check on a mono speaker", "CHECK", kWarn });
        else                         v.push_back ({ "Phase / mono check", ready ? "Correlation " + String (c, 2) : "Analysing...", ready ? "OK" : "...", ready ? kOk : kDim });
    }
    {   // 6. tonal balance
        float worst = 0.f; int wi = 0;
        for (int i = 0; i < kNumBands; ++i) { const float d = m.bandRel[i].load() - m.targetRel[i].load(); if (std::abs (d) > std::abs (worst)) { worst = d; wi = i; } }
        if (ready && std::abs (worst) > 3.f)
        {
            auto t = fixOrIssue (toneOn);
            v.push_back ({ "Tonal balance", String (bandName (wi)) + " is " + String (std::abs (worst), 1) + " dB " + (worst > 0 ? "above" : "below") + " target", t.first, t.second });
        }
        else v.push_back ({ "Tonal balance", ready ? "Within 3 dB of target" : "Analysing...", ready ? "OK" : "...", ready ? kOk : kDim });
    }
    {   // 7. harshness
        const float g = m.harshGR.load();
        if (g > 0.8f) { auto t = fixOrIssue (harshOn); v.push_back ({ "Harshness 2.5-7 kHz", "Bursts jump out of the mix" + String (harshOn ? " (cutting " + String (g, 1) + " dB)" : ""), t.first, t.second }); }
        else           v.push_back ({ "Harshness 2.5-7 kHz", ready ? "No harsh bursts" : "Analysing...", ready ? "OK" : "...", ready ? kOk : kDim });
    }
    {   // 8. density (advisory)
        const float crest = m.peakDb.load() - m.rmsDb.load();
        if (ready && crest < 6.f)  v.push_back ({ "Density", "Crest factor " + String (crest, 1) + " dB: very squashed. Ease off upstream compression/clipping", "CHECK", kWarn });
        else                        v.push_back ({ "Density", ready ? "Crest factor " + String (crest, 1) + " dB" : "Analysing...", ready ? "OK" : "...", ready ? kOk : kDim });
    }
    return v;
}

void MixDoctorEditor::paint (Graphics& g)
{
    using namespace mixdoctor;
    g.fillAll (kBg);

    // header
    g.setColour (kText);  g.setFont (FontOptions (26.f, Font::bold));
    g.drawText ("DnB MIX DOCTOR", 20, 12, 400, 34, Justification::centredLeft);
    g.setColour (kAccent); g.fillRoundedRectangle (20.f, 46.f, 56.f, 3.f, 1.5f);
    g.setColour (kDim);   g.setFont (FontOptions (12.f));
    g.drawText ("detects and corrects problems on your drum & bass master  -  latency "
                    + String (1000.0 * proc.doctor.latencySamples() / std::max (1.0, proc.getSampleRate()), 1) + " ms",
                380, 16, getWidth() - 400, 28, Justification::centredRight);

    // issues panel
    g.setColour (kPanel); g.fillRoundedRectangle (issuesArea.toFloat(), 10.f);
    g.setColour (kDim);   g.setFont (FontOptions (11.f, Font::bold)); g.drawText ("MIX CHECK", issuesArea.getX() + 16, issuesArea.getY() + 8, 200, 18, Justification::centredLeft);
    const auto issues = evaluate();
    auto ir = issuesArea.reduced (16, 0).withTrimmedTop (30);
    for (auto& is : issues)
    {
        auto row = ir.removeFromTop (36);
        g.setColour (is.colour); g.fillEllipse ((float) row.getX(), (float) row.getCentreY() - 5.f, 10.f, 10.f);
        g.setColour (kText); g.setFont (FontOptions (13.f, Font::bold));
        g.drawText (is.title, row.getX() + 20, row.getY(), 150, row.getHeight(), Justification::centredLeft);
        g.setColour (kDim);  g.setFont (FontOptions (12.f));
        g.drawFittedText (is.message, row.getX() + 172, row.getY(), row.getWidth() - 172 - 62, row.getHeight(), Justification::centredLeft, 2);
        g.setColour (is.colour.withAlpha (0.18f)); g.fillRoundedRectangle ((float) row.getRight() - 56.f, (float) row.getCentreY() - 10.f, 56.f, 20.f, 5.f);
        g.setColour (is.colour); g.setFont (FontOptions (11.f, Font::bold));
        g.drawText (is.tag, row.getRight() - 56, row.getCentreY() - 10, 56, 20, Justification::centred);
    }

    // tonal balance panel
    g.setColour (kPanel); g.fillRoundedRectangle (bandsArea.toFloat(), 10.f);
    g.setColour (kDim);   g.setFont (FontOptions (11.f, Font::bold)); g.drawText ("TONAL BALANCE  (dB per octave, vs target)", bandsArea.getX() + 16, bandsArea.getY() + 8, 300, 18, Justification::centredLeft);
    auto br = bandsArea.reduced (14, 0).withTrimmedTop (34).withTrimmedBottom (46);
    const float zeroY = (float) br.getCentreY(), scale = (float) br.getHeight() * 0.5f / 12.f;
    g.setColour (Colour (0x22ffffff)); g.drawHorizontalLine ((int) zeroY, (float) br.getX(), (float) br.getRight());
    const float colW = (float) br.getWidth() / kNumBands;
    const auto& m = proc.doctor.meters;
    for (int i = 0; i < kNumBands; ++i)
    {
        const float x = (float) br.getX() + colW * (float) i, bw = colW * 0.5f, bx = x + colW * 0.25f;
        const float rel = jlimit (-12.f, 12.f, m.bandRel[i].load()), tgt = jlimit (-12.f, 12.f, m.targetRel[i].load());
        const float dev = std::abs (rel - tgt);
        g.setColour ((dev < 3.f ? kAccent : dev < 6.f ? kWarn : kBad).withAlpha (0.85f));
        g.fillRect (bx, jmin (zeroY, zeroY - rel * scale), bw, std::abs (rel * scale));
        g.setColour (kText); g.fillRect (bx - 4.f, zeroY - tgt * scale - 1.5f, bw + 8.f, 3.f);
        const float corr = m.bandCorr[i].load();
        g.setColour (std::abs (corr) > 0.15f ? kAccent : kDim); g.setFont (FontOptions (11.f));
        g.drawText ((corr >= 0 ? "+" : "") + String (corr, 1), (int) x, bandsArea.getBottom() - 42, (int) colW, 16, Justification::centred);
        g.setColour (kDim); g.setFont (FontOptions (10.f));
        g.drawText (bandName (i), (int) x - 4, bandsArea.getBottom() - 26, (int) colW + 8, 16, Justification::centred);
    }
    g.setColour (kDim); g.setFont (FontOptions (10.f));
    g.drawText ("bars = your mix   |   white tick = target   |   numbers = EQ applied (dB)", bandsArea.getX() + 14, bandsArea.getY() + 24, bandsArea.getWidth() - 28, 14, Justification::centredLeft);

    // controls panel
    g.setColour (kPanel); g.fillRoundedRectangle (controlsArea.toFloat(), 10.f);
    g.setColour (kDim);   g.setFont (FontOptions (10.f));
    g.drawText ("Targets are starting-point estimates for each style - use Capture target on a reference you love for ground truth.",
                controlsArea.getX() + 14, controlsArea.getBottom() - 20, controlsArea.getWidth() - 28, 16, Justification::centredLeft);
}

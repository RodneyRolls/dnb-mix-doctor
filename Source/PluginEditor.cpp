#include "PluginEditor.h"

using namespace juce;
namespace col = hifi::colours;

namespace
{
    // ---- design-space layout (1300 x 700)
    constexpr int cheek = 26;
    const Rectangle<int> plateArea { cheek, 0, DnBMixDoctorEditor::designW - 2 * cheek, DnBMixDoctorEditor::designH };
    const Rectangle<int> visArea { 236, 100, 828, 272 };
    constexpr int knobsX = 40, knobsW = 1220, rowY[2] = { 382, 530 }, rowH = 146;
    constexpr int favouriteIdOffset = 1000;

    float easeInOut (float t) { return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow (-2.0f * t + 2.0f, 3.0f) * 0.5f; }

    String fmtMeter (float v, int decimals = 1) { return v <= -99.0f ? String ("---") : String (v, decimals); }
}

//==============================================================================
DnBMixDoctorEditor::DnBMixDoctorEditor (DnBMixDoctorProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (panel);

    for (auto* b : { &powerButton, &autoFixButton, &prevButton, &nextButton, &starButton, &menuButton,
                     &folderButton, &abButton, &releaseAutoButton })
        panel.addAndMakeVisible (b);
    for (auto* c : std::initializer_list<Component*> { &presetBox, &cpuDisplay, &visualizer, &vuLeft, &vuRight,
                                                        &lufsIntDisplay, &lufsShortDisplay, &truePeakDisplay, &grDisplay })
        panel.addAndMakeVisible (c);

    abButton.setText ("A");
    releaseAutoButton.setText ("AUTO");
    releaseAutoButton.setClickingTogglesState (true);

    // ---- knobs
    for (int i = 0; i < 20; ++i)
    {
        auto& k = knobs[(size_t) i];
        const auto& id = params::knobOrder[i];
        panel.addAndMakeVisible (k);
        sliderAttachments.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k));
        if (auto* param = proc.apvts.getParameter (id))
            k.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
        k.setPopupDisplayEnabled (true, false, &panel, 900);
        k.setTitle (params::knobLabels[i]);
    }
    releaseAutoAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, params::releaseAuto, releaseAutoButton);

    // ---- top bar
    powerButton.onClick = [this]
    {
        if (auto* b = proc.apvts.getParameter (params::bypass))
        {
            b->beginChangeGesture();
            b->setValueNotifyingHost (b->getValue() > 0.5f ? 0.0f : 1.0f);
            b->endChangeGesture();
        }
    };
    autoFixButton.onClick = [this] { toggleAutoFix(); };
    prevButton.onClick = [this] { proc.presets.loadNext (-1); refreshPresetBox(); };
    nextButton.onClick = [this] { proc.presets.loadNext (+1); refreshPresetBox(); };
    starButton.onClick = [this] { proc.presets.toggleFavourite (proc.presets.getCurrentName()); refreshPresetBox(); };
    menuButton.onClick = [this] { showMainMenu(); };
    folderButton.onClick = [this] { showPresetBrowser(); };
    abButton.onClick = [this] { proc.toggleAB(); };

    presetBox.setJustificationType (Justification::centred);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id <= 0) return;
        proc.presets.load (id >= favouriteIdOffset ? id - favouriteIdOffset : id - 1);
        refreshPresetBox();
    };

    cpuDisplay.setValueText ("0.0");
    lufsIntDisplay.onClick = [this] { proc.meters.resetRequested = true; };
    truePeakDisplay.onClick = [this] { proc.meters.resetRequested = true; };

    visualizer.onFrame = [this] (double dt)
    {
        vuLeft.setLevel (visualizer.getRmsL());
        vuRight.setLevel (visualizer.getRmsR());
        vuLeft.tick (dt);
        vuRight.tick (dt);
    };

    layout();
    refreshPresetBox();
    vuLeft.referenceDbfs = vuRight.referenceDbfs = proc.apvts.getRawParameterValue (params::loudness)->load();

    // resizing: aspect ratio locked, whole panel scaled
    setResizable (true, true);
    setResizeLimits (designW * 6 / 10, designH * 6 / 10, designW * 2, designH * 2);
    getConstrainer()->setFixedAspectRatio ((double) designW / designH);
    setSize (jlimit (designW * 6 / 10, designW * 2, proc.editorWidth),
             roundToInt (jlimit (designW * 6 / 10, designW * 2, proc.editorWidth) * (double) designH / designW));

    proc.editorOpen = true;
    startTimerHz (60);
}

DnBMixDoctorEditor::~DnBMixDoctorEditor()
{
    proc.editorOpen = false;
    for (auto& s : sweeps) s.param->endChangeGesture();
    setLookAndFeel (nullptr);
}

void DnBMixDoctorEditor::resized()
{
    const float scale = (float) getWidth() / (float) designW;
    panel.setBounds (0, 0, designW, designH);
    panel.setTransform (AffineTransform::scale (scale));
    proc.editorWidth = getWidth();
    proc.editorHeight = getHeight();
}

void DnBMixDoctorEditor::layout()
{
    // top bar
    powerButton.setBounds (44, 10, 58, 36);
    autoFixButton.setBounds (108, 10, 58, 36);
    prevButton.setBounds (452, 10, 40, 36);
    presetBox.setBounds (498, 12, 304, 32);
    nextButton.setBounds (808, 10, 40, 36);
    starButton.setBounds (854, 10, 40, 36);
    cpuDisplay.setBounds (1112, 12, 90, 32);
    menuButton.setBounds (1210, 10, 50, 36);

    // second row
    folderButton.setBounds (44, 54, 58, 36);
    abButton.setBounds (1196, 54, 64, 36);

    // visualiser + meters
    visualizer.setBounds (visArea);
    vuLeft.setBounds (40, 100, 188, 142);
    vuRight.setBounds (1072, 100, 188, 142);
    lufsIntDisplay.setBounds (40, 246, 188, 60);
    lufsShortDisplay.setBounds (40, 310, 188, 60);
    truePeakDisplay.setBounds (1072, 246, 188, 60);
    grDisplay.setBounds (1072, 310, 188, 60);

    // 2 x 10 knobs, label above each knob
    const int cellW = knobsW / 10;
    for (int i = 0; i < 20; ++i)
    {
        const int row = i / 10, c = i % 10;
        const Rectangle<int> cell (knobsX + c * cellW, rowY[row], cellW, rowH);
        knobCells[(size_t) i] = cell;
        knobs[(size_t) i].setBounds (cell.withTrimmedTop (18).withHeight (112).withSizeKeepingCentre (112, 112));
    }
    const auto relCell = knobCells[12];
    releaseAutoButton.setBounds (relCell.getCentreX() - 32, relCell.getBottom() - 19, 64, 20);
}

//==============================================================================
void DnBMixDoctorEditor::Panel::renderBackground (float scale)
{
    bgScale = scale;
    const int W = DnBMixDoctorEditor::designW, H = DnBMixDoctorEditor::designH;
    background = Image (Image::RGB, roundToInt (W * scale), roundToInt (H * scale), true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (scale));
    Random rng (1975);

    // ---- walnut side cheeks
    for (int side = 0; side < 2; ++side)
    {
        const auto r = Rectangle<float> (side == 0 ? 0.0f : (float) (W - cheek), 0.0f, (float) cheek, (float) H);
        ColourGradient wood (col::walnutDark, r.getX(), 0, col::walnutDark, r.getRight(), 0, false);
        wood.addColour (0.25, col::walnutLight);
        wood.addColour (0.6, col::walnut);
        g.setGradientFill (wood);
        g.fillRect (r);
        for (int i = 0; i < 70; ++i)
        {
            Path grain;
            float x = r.getX() + rng.nextFloat() * r.getWidth();
            const float freq = 0.004f + rng.nextFloat() * 0.01f, amp = 0.4f + rng.nextFloat() * 1.6f, ph = rng.nextFloat() * 6.0f;
            grain.startNewSubPath (x, 0);
            for (float y = 0; y <= (float) H; y += 8.0f)
                grain.lineTo (jlimit (r.getX(), r.getRight(), x + std::sin (y * freq + ph) * amp), y);
            g.setColour ((rng.nextBool() ? Colour (0xff1a0d05) : Colour (0xff9a6440)).withAlpha (0.10f + rng.nextFloat() * 0.18f));
            g.strokePath (grain, PathStrokeType (0.5f + rng.nextFloat() * 1.2f));
        }
        ColourGradient varnish (Colours::white.withAlpha (0.10f), r.getX() + r.getWidth() * 0.3f, 0,
                                Colours::transparentWhite, r.getRight(), 0, false);
        g.setGradientFill (varnish);
        g.fillRect (r);
    }

    // ---- brushed champagne aluminium faceplate
    const auto plate = plateArea.toFloat();
    ColourGradient base (col::panelLight, 0, 0, col::panelDark, 0, (float) H, false);
    base.addColour (0.45, col::panel);
    g.setGradientFill (base);
    g.fillRect (plate);
    for (float y = 0; y < (float) H; y += 0.5f)
        for (int k = 0; k < 3; ++k)
        {
            const float x0 = plate.getX() + rng.nextFloat() * plate.getWidth();
            const float len = 40.0f + rng.nextFloat() * 520.0f;
            g.setColour ((rng.nextBool() ? Colours::white : Colour (0xff5a4e36)).withAlpha (0.025f + rng.nextFloat() * 0.05f));
            g.fillRect (x0, y, jmin (len, plate.getRight() - x0), 0.5f);
        }
    for (float cx : { 0.28f, 0.72f })
    {
        ColourGradient sheen (Colours::white.withAlpha (0.09f), plate.getX() + plate.getWidth() * cx, 0,
                              Colours::transparentWhite, plate.getX() + plate.getWidth() * (cx + 0.18f), 0, false);
        g.setGradientFill (sheen);
        g.fillRect (plate);
    }
    g.setColour (Colours::black.withAlpha (0.45f));
    g.drawVerticalLine (cheek, 0, (float) H);
    g.drawVerticalLine (W - cheek - 1, 0, (float) H);
    g.setColour (Colours::white.withAlpha (0.5f));
    g.drawHorizontalLine (0, plate.getX(), plate.getRight());

    // ---- engraved section rules
    auto rule = [&] (float y, float x0, float x1)
    {
        g.setColour (Colours::black.withAlpha (0.35f)); g.drawHorizontalLine ((int) y, x0, x1);
        g.setColour (Colours::white.withAlpha (0.5f));  g.drawHorizontalLine ((int) y + 1, x0, x1);
    };
    rule (50.0f, 40.0f, 1260.0f);
    rule (377.0f, 40.0f, 1260.0f);
    rule (528.0f, 40.0f, 1260.0f);
    rule (677.0f, 40.0f, 1260.0f);

    // ---- title
    hifi::drawEngravedText (g, "DNB MIX DOCTOR", { 0.0f, 52.0f, (float) W, 32.0f },
                            hifi::labelFont (27.0f, true).withExtraKerningFactor (0.18f));
    hifi::drawEngravedText (g, "DRUM & BASS MIX REPAIR SYSTEM  -  MODEL MD-1", { 0.0f, 83.0f, (float) W, 12.0f },
                            hifi::labelFont (8.5f).withExtraKerningFactor (0.25f));

    // ---- captions
    hifi::drawEngravedText (g, "CPU %", { 1050.0f, 12.0f, 58.0f, 32.0f }, hifi::labelFont (10.0f), Justification::centredRight);
    hifi::drawEngravedText (g, "PRESET", { 380.0f, 12.0f, 66.0f, 32.0f }, hifi::labelFont (10.0f), Justification::centredRight);
    hifi::drawEngravedText (g, "PRESETS", { 106.0f, 54.0f, 80.0f, 36.0f }, hifi::labelFont (9.0f), Justification::centredLeft);
    hifi::drawEngravedText (g, "COMPARE", { 1110.0f, 54.0f, 80.0f, 36.0f }, hifi::labelFont (9.0f), Justification::centredRight);

    // ---- knob labels (engraved, centred above each knob)
    for (int i = 0; i < 20; ++i)
        hifi::drawEngravedText (g, params::knobLabels[i], owner.knobCells[(size_t) i].withHeight (16).toFloat(),
                                hifi::labelFont (11.0f), Justification::centred);

    // ---- developer badge (bottom-left) and version (bottom-right)
    auto badge = Rectangle<float> (40.0f, 681.0f, 118.0f, 15.0f);
    ColourGradient bg (Colour (0xff2a2622), 0, badge.getY(), Colour (0xff0d0b09), 0, badge.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (badge, 3.0f);
    g.setColour (Colour (0xffd9d4c8));
    g.drawRoundedRectangle (badge.reduced (0.5f), 3.0f, 1.0f);
    g.setFont (hifi::labelFont (8.5f).withExtraKerningFactor (0.2f));
    g.drawText ("MOD CON DSP", badge, Justification::centred);
    hifi::drawEngravedText (g, "1.0", { 1200.0f, 681.0f, 60.0f, 15.0f }, hifi::labelFont (9.0f), Justification::centredRight);
}

void DnBMixDoctorEditor::Panel::paint (Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (background.isNull() || std::abs (bgScale - scale) > 0.01f)
        renderBackground (scale);
    g.drawImage (background, getLocalBounds().toFloat());
}

//==============================================================================
void DnBMixDoctorEditor::timerCallback()
{
    updateSweep();

    if (auto* b = proc.apvts.getParameter (params::bypass))
        powerButton.setToggleState (b->getValue() < 0.5f, dontSendNotification);

    // ---- auto-fix listening / results
    switch (proc.autoFix.getState())
    {
        case mcdsp::AutoFixAnalyser::listening:
            autoFixButton.setToggleState (true, dontSendNotification);
            autoFixButton.setBlink (true);
            autoFixButton.setProgress (jmax (0.01f, proc.autoFix.getProgress()));
            break;
        case mcdsp::AutoFixAnalyser::done:
        {
            const auto result = proc.autoFix.getResult();
            proc.autoFix.acknowledge();
            autoFixButton.setBlink (false);
            autoFixButton.setProgress (0.0f);
            startSweep (mcdsp::suggestSettings (result, proc.hostBpm.load()));
            proc.presets.setCurrentName ("Auto-Fix");
            break;
        }
        default:
            autoFixButton.setBlink (false);
            autoFixButton.setProgress (0.0f);
            autoFixButton.setToggleState (! sweeps.empty(), dontSendNotification);
            break;
    }

    // ---- slower UI (text readouts, preset sync) at ~12 Hz
    const auto now = Time::getMillisecondCounter();
    if (now - lastSlowUpdate < 80) return;
    lastSlowUpdate = now;

    const auto& m = proc.meters;
    cpuDisplay.setValueText (String (m.cpuPercent.load(), 1));
    lufsIntDisplay.setValueText (fmtMeter (m.lufsIntegrated.load()));
    lufsShortDisplay.setValueText (fmtMeter (m.lufsShortTerm.load()));
    truePeakDisplay.setValueText (fmtMeter (m.truePeakDb.load()));
    grDisplay.setValueText (String (jmin (0.0f, m.glueGrDb.load() + m.limiterGrDb.load()), 1));

    // 0 VU == the LOUDNESS target, so a needle sitting on 0 means "on target"
    vuLeft.referenceDbfs = vuRight.referenceDbfs = proc.apvts.getRawParameterValue (params::loudness)->load();

    abButton.setText (proc.isOnB() ? "B" : "A");
    abButton.setToggleState (proc.isOnB(), dontSendNotification);
    starButton.setToggleState (proc.presets.isFavourite (proc.presets.getCurrentName()), dontSendNotification);

    if (proc.presets.getCurrentName() != shownPreset || (int) proc.presets.getAll().size() != shownPresetCount)
        refreshPresetBox();
}

void DnBMixDoctorEditor::refreshPresetBox()
{
    const auto& all = proc.presets.getAll();
    presetBox.clear (dontSendNotification);

    bool anyFav = false;
    for (size_t i = 0; i < all.size(); ++i)
        if (proc.presets.isFavourite (all[i].name))
        {
            if (! anyFav) { presetBox.addSectionHeading ("Favourites"); anyFav = true; }
            presetBox.addItem (all[i].name, favouriteIdOffset + (int) i);
        }
    presetBox.addSectionHeading ("Factory");
    bool userHeading = false;
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (! all[i].factory && ! userHeading) { presetBox.addSectionHeading ("User"); userHeading = true; }
        presetBox.addItem (all[i].name, (int) i + 1);
    }

    shownPreset = proc.presets.getCurrentName();
    shownPresetCount = (int) all.size();
    const int idx = proc.presets.indexOf (shownPreset);
    if (idx >= 0) presetBox.setSelectedId (idx + 1, dontSendNotification);
    else          presetBox.setText (shownPreset, dontSendNotification);
    starButton.setToggleState (proc.presets.isFavourite (shownPreset), dontSendNotification);
}

//==============================================================================
void DnBMixDoctorEditor::showPresetBrowser()
{
    auto& pm = proc.presets;
    pm.refresh();
    const auto& all = pm.getAll();
    const auto current = pm.getCurrentName();

    PopupMenu menu, favs, factory, user;
    for (size_t i = 0; i < all.size(); ++i)
    {
        const int id = (int) i + 1;
        auto& target = all[i].factory ? factory : user;
        target.addItem (id, all[i].name, true, all[i].name == current);
        if (pm.isFavourite (all[i].name))
            favs.addItem (favouriteIdOffset + id, all[i].name, true, all[i].name == current);
    }
    menu.addSectionHeader ("Preset browser");
    menu.addSubMenu ("Favourites", favs, favs.getNumItems() > 0);
    menu.addSubMenu ("Factory", factory);
    menu.addSubMenu ("User", user, user.getNumItems() > 0);
    menu.addSeparator();
    menu.addItem (5001, "Save preset as...");
    const int curIdx = pm.indexOf (current);
    menu.addItem (5002, "Delete \"" + current + "\"", curIdx >= 0 && ! all[(size_t) curIdx].factory);
    menu.addItem (5003, "Open preset folder");

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (folderButton).withParentComponent (&panel),
                        [this] (int r)
                        {
                            auto& p = proc.presets;
                            if (r == 5001) savePresetDialog();
                            else if (r == 5002) { p.deleteUser (p.getCurrentName()); p.setCurrentName ("Default"); }
                            else if (r == 5003) PresetManager::getUserFolder().startAsProcess();
                            else if (r > favouriteIdOffset) p.load (r - favouriteIdOffset - 1);
                            else if (r > 0) p.load (r - 1);
                            refreshPresetBox();
                        });
}

void DnBMixDoctorEditor::savePresetDialog()
{
    auto* w = new AlertWindow ("Save preset", "Name for the user preset:", MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    const auto current = proc.presets.getCurrentName();
    w->addTextEditor ("name", PresetManager::getFactoryNames().contains (current) ? current + " (mine)" : current);
    w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result == 1)
            proc.presets.saveUser (w->getTextEditorContents ("name"));
        refreshPresetBox();
    }), true);
}

void DnBMixDoctorEditor::showMainMenu()
{
    PopupMenu menu;
    const bool os = proc.apvts.getParameter (params::oversample)->getValue() > 0.5f;
    menu.addSectionHeader ("DnB Mix Doctor 1.0 - Mod Con DSP");
    menu.addItem (1, "4x oversampling (saturation + limiter)", true, os);
    menu.addItem (2, "Reset loudness / true-peak meters");
    menu.addItem (3, String ("Switch to ") + (proc.isOnB() ? "A" : "B") + " (compare)");
    menu.addSeparator();
    menu.addItem (4, "Run auto-fix (listens for 8 bars)");
    menu.addItem (5, "Reset all knobs to Default preset");

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (menuButton).withParentComponent (&panel),
                        [this] (int r)
                        {
                            if (r == 1)
                            {
                                auto* p = proc.apvts.getParameter (params::oversample);
                                p->beginChangeGesture();
                                p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
                                p->endChangeGesture();
                            }
                            else if (r == 2) proc.meters.resetRequested = true;
                            else if (r == 3) proc.toggleAB();
                            else if (r == 4) toggleAutoFix();
                            else if (r == 5) { proc.presets.load ("Default"); refreshPresetBox(); }
                        });
}

//==============================================================================
void DnBMixDoctorEditor::toggleAutoFix()
{
    if (proc.autoFix.getState() == mcdsp::AutoFixAnalyser::listening)
    {
        proc.autoFix.cancel();
        return;
    }
    const double bpm = jlimit (60.0, 240.0, proc.hostBpm.load());
    const double eightBars = 8.0 * 4.0 * 60.0 / bpm;
    proc.autoFix.start (eightBars);
    autoFixButton.setToggleState (true, dontSendNotification);
}

void DnBMixDoctorEditor::startSweep (const std::vector<std::pair<String, float>>& targets)
{
    for (auto& s : sweeps) s.param->endChangeGesture();
    sweeps.clear();

    auto add = [this] (const String& id, float value, double delay)
    {
        if (auto* p = proc.apvts.getParameter (id))
        {
            p->beginChangeGesture();
            sweeps.push_back ({ p, p->getValue(), p->convertTo0to1 (value), delay });
        }
    };
    for (size_t i = 0; i < targets.size(); ++i)
    {
        // stagger by knob position so the sweep rolls across the panel
        const int pos = params::knobOrder.indexOf (targets[i].first);
        add (targets[i].first, targets[i].second, 0.045 * jmax (0, pos));
    }
    add (params::releaseAuto, 1.0f, 0.0);
    sweepStart = Time::getMillisecondCounterHiRes() * 0.001;
}

void DnBMixDoctorEditor::updateSweep()
{
    if (sweeps.empty()) return;
    const double t = Time::getMillisecondCounterHiRes() * 0.001 - sweepStart;
    bool finished = true;
    for (auto& s : sweeps)
    {
        const float k = (float) jlimit (0.0, 1.0, (t - s.delay) / 1.1);
        s.param->setValueNotifyingHost (s.from + (s.to - s.from) * easeInOut (k));
        finished = finished && k >= 1.0f;
    }
    if (finished)
    {
        for (auto& s : sweeps) s.param->endChangeGesture();
        sweeps.clear();
    }
}

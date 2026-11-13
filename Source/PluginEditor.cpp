#include "PluginEditor.h"

using namespace juce;
namespace col = mcui::colours;

namespace
{
    // ---- design-space layout (1300 x 700)
    const Rectangle<int> headerArea { 24, 6, 1252, 46 };
    const Rectangle<int> visArea { 236, 100, 828, 272 };
    const Rectangle<int> sectionArea[2] = { { 24, 378, 1252, 148 }, { 24, 530, 1252, 148 } };
    constexpr int numKnobs = 22, knobsX = 40, knobsW = 1220, rowY[2] = { 384, 536 }, rowH = 140, knobSize = 104;
    constexpr int favouriteIdOffset = 1000;

    float easeInOut (float t) { return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow (-2.0f * t + 2.0f, 3.0f) * 0.5f; }

    String fmtMeter (float v, int decimals = 1) { return v <= -99.0f ? String ("---") : String (v, decimals); }
}

//==============================================================================
DnBMixDoctorEditor::DnBMixDoctorEditor (DnBMixDoctorProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    // read the saved window width first: setResizable() below calls resized() at the default size,
    // which would overwrite it and make the window reopen at its minimum size
    const int savedWidth = jlimit (designW * 6 / 10, designW * 2, proc.editorWidth);
    setLookAndFeel (&lnf);
    addAndMakeVisible (panel);

    for (auto* b : { &powerButton, &autoFixButton, &prevButton, &nextButton, &starButton, &menuButton,
                     &folderButton, &abButton, &releaseAutoButton, &multibandButton, &advancedButton })
        panel.addAndMakeVisible (b);
    for (auto* c : std::initializer_list<Component*> { &presetBox, &cpuDisplay, &visualizer, &vuLeft, &vuRight,
                                                        &lufsIntDisplay, &lufsShortDisplay, &truePeakDisplay, &grDisplay })
        panel.addAndMakeVisible (c);

    abButton.setText ("A");
    releaseAutoButton.setText ("AUTO");
    releaseAutoButton.setClickingTogglesState (true);
    multibandButton.setText ("MULTIBAND");
    multibandButton.setClickingTogglesState (true);
    multibandButton.setTooltip ("Maximizer mode: off = CLASSIC broadband limiter, on = MULTIBAND smart-release limiter");
    advancedButton.setText ("ADVANCED");

    // ---- knobs
    for (int i = 0; i < numKnobs; ++i)
    {
        auto& k = knobs[(size_t) i];
        const auto& id = params::knobOrder[i];
        panel.addAndMakeVisible (k);
        sliderAttachments.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k));
        if (auto* param = proc.apvts.getParameter (id))
            k.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
        k.setTitle (params::knobLabels[i]);
        k.setBipolar (id == params::input || id == params::output || id == params::width);
    }
    releaseAutoAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, params::releaseAuto, releaseAutoButton);
    multibandAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, params::multiband, multibandButton);

    // ---- ADVANCED: the 16-band EQ panel slides over the knob section
    panel.addChildComponent (eqPanel);
    advancedButton.onClick = [this] { setAdvancedOpen (! proc.advancedOpen); };
    eqPanel.onClose = [this] { setAdvancedOpen (false); };

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
    cpuDisplay.setCardStyle (false);
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
    setAdvancedOpen (proc.advancedOpen);
    vuLeft.referenceDbfs = vuRight.referenceDbfs = proc.apvts.getRawParameterValue (params::loudness)->load();

    // resizing: aspect ratio locked, whole panel scaled
    setResizable (true, true);
    setResizeLimits (designW * 6 / 10, designH * 6 / 10, designW * 2, designH * 2);
    getConstrainer()->setFixedAspectRatio ((double) designW / designH);
    setSize (savedWidth, roundToInt (savedWidth * (double) designH / designW));

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
    // header bar
    powerButton.setBounds (34, 11, 48, 36);
    autoFixButton.setBounds (88, 11, 48, 36);
    prevButton.setBounds (450, 11, 36, 36);
    presetBox.setBounds (492, 11, 316, 36);
    nextButton.setBounds (814, 11, 36, 36);
    starButton.setBounds (856, 11, 36, 36);
    cpuDisplay.setBounds (1100, 13, 104, 32);
    menuButton.setBounds (1218, 11, 48, 36);
    advancedButton.setBounds (914, 11, 150, 36);

    // title row
    folderButton.setBounds (40, 60, 44, 32);
    abButton.setBounds (1212, 60, 48, 32);

    // display + meters
    visualizer.setBounds (visArea);
    vuLeft.setBounds (40, 100, 188, 142);
    vuRight.setBounds (1072, 100, 188, 142);
    lufsIntDisplay.setBounds (40, 248, 188, 60);
    lufsShortDisplay.setBounds (40, 312, 188, 60);
    truePeakDisplay.setBounds (1072, 248, 188, 60);
    grDisplay.setBounds (1072, 312, 188, 60);

    // 22 knobs: two rows of 11
    const int cellW = knobsW / 11, x0 = knobsX + (knobsW - cellW * 11) / 2;
    for (int i = 0; i < numKnobs; ++i)
    {
        const int row = i < 11 ? 0 : 1, c = row == 0 ? i : i - 11;
        const int x = x0 + c * cellW;
        const Rectangle<int> cell (x, rowY[row], cellW, rowH);
        knobCells[(size_t) i] = cell;
        knobs[(size_t) i].setBounds (cell.withTrimmedTop (20).withHeight (knobSize).withSizeKeepingCentre (knobSize, knobSize));
    }
    const auto relCell = knobCells[(size_t) params::knobOrder.indexOf (params::release)];
    releaseAutoButton.setBounds (relCell.getCentreX() - 26, relCell.getBottom() - 21, 52, 19);
    const auto loudCell = knobCells[(size_t) params::knobOrder.indexOf (params::loudness)];
    multibandButton.setBounds (loudCell.getCentreX() - 44, loudCell.getBottom() - 21, 88, 19);
    eqPanel.setBounds (sectionArea[0].getUnion (sectionArea[1]));
}

void DnBMixDoctorEditor::setAdvancedOpen (bool open)
{
    proc.advancedOpen = open;
    eqPanel.setVisible (open);
    if (open) eqPanel.toFront (false);
    for (auto& k : knobs) k.setVisible (! open);
    releaseAutoButton.setVisible (! open);
    multibandButton.setVisible (! open);
    advancedButton.setToggleState (open, dontSendNotification);
}

//==============================================================================
void DnBMixDoctorEditor::Panel::renderBackground (float scale)
{
    bgScale = scale;
    const int W = DnBMixDoctorEditor::designW, H = DnBMixDoctorEditor::designH;
    background = Image (Image::RGB, roundToInt (W * scale), roundToInt (H * scale), true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (scale));

    // ---- deep graphite backdrop with soft coloured light
    ColourGradient base (col::bg1, 0, 0, col::bg0, 0, (float) H, false);
    g.setGradientFill (base);
    g.fillAll();
    mcui::drawGlow (g, { 140.0f, 40.0f }, 520.0f, col::cyan.withAlpha (0.07f));
    mcui::drawGlow (g, { 1180.0f, 660.0f }, 600.0f, col::violet.withAlpha (0.08f));
    mcui::drawGlow (g, { 650.0f, 240.0f }, 520.0f, col::violet.withAlpha (0.04f));

    // ---- surfaces
    mcui::drawCard (g, headerArea.toFloat(), 14.0f);
    for (auto& s : sectionArea)
        mcui::drawCard (g, s.toFloat(), 16.0f);

    // ---- header captions
    mcui::drawCaption (g, "Auto-fix", { 142.0f, 11.0f, 80.0f, 36.0f });
    mcui::drawCaption (g, "Preset", { 380.0f, 11.0f, 62.0f, 36.0f }, Justification::centredRight);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.fillRect (1088.0f, 16.0f, 1.0f, 26.0f);

    // ---- title: the original wordmark face
    g.setFont (mcui::retroFont (27.0f, true).withExtraKerningFactor (0.18f));
    g.setColour (col::text);
    g.drawText ("FIX DR", Rectangle<float> (0.0f, 54.0f, (float) W, 32.0f), Justification::centred, false);
    mcui::drawCaption (g, "Drum & bass mix repair  +  loudness maximizer", { 0.0f, 84.0f, (float) W, 12.0f },
                       Justification::centred, col::textFaint, 9.0f);
    mcui::drawCaption (g, "Presets", { 92.0f, 60.0f, 80.0f, 32.0f });
    mcui::drawCaption (g, "Compare", { 1120.0f, 60.0f, 84.0f, 32.0f }, Justification::centredRight);

    // ---- knob labels, centred above each knob
    for (int i = 0; i < numKnobs; ++i)
        mcui::drawCaption (g, params::knobLabels[i], owner.knobCells[(size_t) i].withHeight (16).toFloat(),
                           Justification::centred, col::textDim, 10.0f);

    // ---- developer badge (bottom-left) and version (bottom-right)
    {
        const auto badgeFont = mcui::font (9.5f, true).withExtraKerningFactor (0.16f);
        const String maker ("MOD CON DSP");
        auto badge = Rectangle<float> (40.0f, 683.0f, mcui::textWidth (badgeFont, maker) + 26.0f, 14.0f);
        g.setColour (Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (badge, 7.0f);
        g.setColour (Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (badge.reduced (0.5f), 7.0f, 1.0f);
        g.setGradientFill (mcui::accentGradient (badge.getTopLeft(), badge.getTopRight()));
        g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre ({ badge.getX() + 9.0f, badge.getCentreY() }));
        g.setColour (col::textDim);
        g.setFont (badgeFont);
        g.drawText (maker, badge.withTrimmedLeft (16.0f), Justification::centredLeft, false);
    }
    mcui::drawCaption (g, "v1.0", { 1180.0f, 683.0f, 64.0f, 14.0f }, Justification::centredRight, col::textFaint, 9.5f);
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
    grDisplay.setValueText (String (jmin (0.0f, m.glueGrDb.load() + m.limiterGrDb.load() + m.clipDb.load()), 1));
    const float ceilingDb = proc.apvts.getRawParameterValue (params::ceiling)->load();
    truePeakDisplay.setValueColour (m.truePeakDb.load() > ceilingDb + 0.05f ? col::bad : col::text);

    // gauges read 0 dB when the level sits on the LOUDNESS target
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
    menu.addSectionHeader ("Fix Dr  1.0  -  Mod Con DSP");
    menu.addItem (1, "4x oversampling (saturation + limiter)", true, os);
    menu.addItem (2, "Reset meters + re-learn loudness (new song)");
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

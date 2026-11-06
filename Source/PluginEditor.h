#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/VUMeter.h"
#include "ui/Visualizer.h"

/** 1300 x 700 design-size editor. Everything lives in `panel`, which is scaled with an affine transform
    so the window can be resized with a locked aspect ratio while staying pixel-sharp. */
class DnBMixDoctorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int designW = 1300, designH = 700;

    explicit DnBMixDoctorEditor (DnBMixDoctorProcessor&);
    ~DnBMixDoctorEditor() override;

    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (mcui::colours::bg0); }

private:
    //==============================================================================
    class Panel : public juce::Component
    {
    public:
        explicit Panel (DnBMixDoctorEditor& e) : owner (e) { setOpaque (true); }
        void paint (juce::Graphics&) override;
    private:
        void renderBackground (float scale);
        DnBMixDoctorEditor& owner;
        juce::Image background;
        float bgScale = 0.0f;
    };

    void layout();
    void timerCallback() override;
    void refreshPresetBox();
    void showPresetBrowser();
    void showMainMenu();
    void savePresetDialog();
    void toggleAutoFix();
    void startSweep (const std::vector<std::pair<juce::String, float>>& targets);
    void updateSweep();

    DnBMixDoctorProcessor& proc;
    mcui::LookAndFeel lnf;
    Panel panel { *this };

    using Icon = mcui::IconButton::Icon;
    mcui::IconButton powerButton { "Power", Icon::power, true },
                     autoFixButton { "Auto-Fix", Icon::bolt, true },
                     prevButton { "Previous preset", Icon::left, false },
                     nextButton { "Next preset", Icon::right, false },
                     starButton { "Favourite", Icon::star, false },
                     menuButton { "Menu", Icon::menu, false },
                     folderButton { "Preset browser", Icon::folder, false },
                     abButton { "A/B", Icon::none, true },
                     releaseAutoButton { "Auto release", Icon::none, true };
    juce::ComboBox presetBox;
    mcui::Readout cpuDisplay { {}, "% CPU" };

    mcui::Visualizer visualizer { proc };
    mcui::VUMeter vuLeft { "LEFT" }, vuRight { "RIGHT" };
    mcui::Readout lufsIntDisplay { "Integrated", "LUFS" }, lufsShortDisplay { "Short-term", "LUFS" },
                  truePeakDisplay { "True peak", "dBTP" }, grDisplay { "Gain reduction", "dB" };

    std::array<mcui::Knob, 21> knobs;
    std::array<juce::Rectangle<int>, 21> knobCells;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> releaseAutoAttachment;

    struct Sweep { juce::RangedAudioParameter* param; float from, to; double delay; };
    std::vector<Sweep> sweeps;
    double sweepStart = 0;

    juce::String shownPreset;
    int shownPresetCount = -1;
    juce::uint32 lastSlowUpdate = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DnBMixDoctorEditor)
};

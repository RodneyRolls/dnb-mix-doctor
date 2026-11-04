#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class MixDoctorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MixDoctorEditor (MixDoctorProcessor&);
    ~MixDoctorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Issue { juce::String title, message, tag; juce::Colour colour; };
    std::vector<Issue> evaluate() const;
    void timerCallback() override { repaint(); }

    void styleSlider (juce::Slider&, const juce::String& suffix);
    void styleToggle (juce::ToggleButton&, const juce::String& text);

    MixDoctorProcessor& proc;

    juce::ToggleButton cleanB, monoB, toneB, harshB, limB;
    juce::Slider monoFreqS, monoAmtS, toneAmtS, toneMaxS, harshMaxS, ceilS;
    juce::Label  monoFreqL, monoAmtL, toneAmtL, toneMaxL, harshMaxL, ceilL;
    juce::ComboBox styleBox;
    juce::TextButton captureBtn { "Capture target" };

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<BA> cleanA, monoA, toneA, harshA, limA;
    std::unique_ptr<SA> monoFreqA, monoAmtA, toneAmtA, toneMaxA, harshMaxA, ceilA;
    std::unique_ptr<CA> styleA;

    juce::Rectangle<int> issuesArea, bandsArea, controlsArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixDoctorEditor)
};

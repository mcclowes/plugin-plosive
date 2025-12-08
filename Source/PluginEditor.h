#pragma once

#include "PluginProcessor.h"

class PlosiveRemoverEditor : public juce::AudioProcessorEditor,
                              public juce::Timer
{
public:
    explicit PlosiveRemoverEditor(PlosiveRemoverProcessor&);
    ~PlosiveRemoverEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    PlosiveRemoverProcessor& processorRef;

    juce::Slider thresholdSlider;
    juce::Slider reductionSlider;
    juce::Slider frequencySlider;

    juce::Label thresholdLabel;
    juce::Label reductionLabel;
    juce::Label frequencyLabel;

    // Meter values (smoothed for display)
    float displayInputLevel = 0.0f;
    float displayDetectionLevel = 0.0f;
    float displayGainReduction = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlosiveRemoverEditor)
};

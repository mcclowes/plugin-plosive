#include "PluginProcessor.h"
#include "PluginEditor.h"

PlosiveRemoverEditor::PlosiveRemoverEditor(PlosiveRemoverProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    // Threshold slider
    thresholdSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    thresholdSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    thresholdSlider.setRange(0.0, 24.0, 0.1);
    thresholdSlider.setValue(processorRef.threshold->get());
    thresholdSlider.onValueChange = [this] {
        *processorRef.threshold = (float)thresholdSlider.getValue();
    };
    addAndMakeVisible(thresholdSlider);

    thresholdLabel.setText("Sensitivity", juce::dontSendNotification);
    thresholdLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(thresholdLabel);

    // Reduction slider
    reductionSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    reductionSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    reductionSlider.setRange(0.0, 100.0, 1.0);
    reductionSlider.setValue(processorRef.reduction->get());
    reductionSlider.onValueChange = [this] {
        *processorRef.reduction = (float)reductionSlider.getValue();
    };
    addAndMakeVisible(reductionSlider);

    reductionLabel.setText("Reduction", juce::dontSendNotification);
    reductionLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(reductionLabel);

    // Frequency slider
    frequencySlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    frequencySlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    frequencySlider.setRange(100.0, 400.0, 1.0);
    frequencySlider.setValue(processorRef.frequency->get());
    frequencySlider.onValueChange = [this] {
        *processorRef.frequency = (float)frequencySlider.getValue();
    };
    addAndMakeVisible(frequencySlider);

    frequencyLabel.setText("Frequency", juce::dontSendNotification);
    frequencyLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(frequencyLabel);

    setSize(400, 280);

    // Start timer for meter updates (30fps)
    startTimerHz(30);
}

PlosiveRemoverEditor::~PlosiveRemoverEditor()
{
    stopTimer();
}

void PlosiveRemoverEditor::timerCallback()
{
    // Smooth meter values for display
    float targetInput = processorRef.getInputLevelMeter();
    float targetDetection = processorRef.getDetectionLevelMeter();
    float targetReduction = processorRef.getGainReductionMeter();

    // Fast attack, slow release for meters
    auto updateMeter = [](float& current, float target) {
        if (target > current)
            current += 0.5f * (target - current);
        else
            current += 0.1f * (target - current);
    };

    updateMeter(displayInputLevel, targetInput);
    updateMeter(displayDetectionLevel, targetDetection);
    updateMeter(displayGainReduction, targetReduction);

    repaint();
}

void PlosiveRemoverEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));

    g.setColour(juce::Colour(0xff16213e));
    g.fillRoundedRectangle(getLocalBounds().reduced(10).toFloat(), 10.0f);

    g.setColour(juce::Colours::white);
    g.setFont(18.0f);
    g.drawText("Plosive Remover", getLocalBounds().removeFromTop(40),
               juce::Justification::centred, true);

    // Draw meters at the bottom
    auto meterArea = getLocalBounds().reduced(20).removeFromBottom(60);

    // Input level meter
    auto inputMeterArea = meterArea.removeFromLeft(meterArea.getWidth() / 3).reduced(5);
    g.setColour(juce::Colours::grey);
    g.fillRoundedRectangle(inputMeterArea.toFloat(), 4.0f);

    float inputMeterWidth = displayInputLevel * inputMeterArea.getWidth();
    g.setColour(juce::Colours::green);
    g.fillRoundedRectangle(inputMeterArea.removeFromLeft((int)inputMeterWidth).toFloat(), 4.0f);

    g.setColour(juce::Colours::white);
    g.setFont(12.0f);
    g.drawText("Input", inputMeterArea.getX(), inputMeterArea.getBottom() + 2,
               inputMeterArea.getWidth(), 15, juce::Justification::centred);

    // Detection level meter (shows low-freq envelope)
    auto detectionMeterArea = meterArea.removeFromLeft(meterArea.getWidth() / 2).reduced(5);
    g.setColour(juce::Colours::grey);
    g.fillRoundedRectangle(detectionMeterArea.toFloat(), 4.0f);

    // Scale detection for visibility (it's usually small values)
    float scaledDetection = std::min(1.0f, displayDetectionLevel * 10.0f);
    float detectionMeterWidth = scaledDetection * detectionMeterArea.getWidth();
    g.setColour(juce::Colours::yellow);
    g.fillRoundedRectangle(detectionMeterArea.removeFromLeft((int)detectionMeterWidth).toFloat(), 4.0f);

    g.setColour(juce::Colours::white);
    g.drawText("Detection", detectionMeterArea.getX(), detectionMeterArea.getBottom() + 2,
               detectionMeterArea.getWidth(), 15, juce::Justification::centred);

    // Gain reduction meter
    auto reductionMeterArea = meterArea.reduced(5);
    g.setColour(juce::Colours::grey);
    g.fillRoundedRectangle(reductionMeterArea.toFloat(), 4.0f);

    float reductionMeterWidth = displayGainReduction * reductionMeterArea.getWidth();
    g.setColour(juce::Colours::red);
    g.fillRoundedRectangle(reductionMeterArea.removeFromLeft((int)reductionMeterWidth).toFloat(), 4.0f);

    g.setColour(juce::Colours::white);
    g.drawText("Reduction", reductionMeterArea.getX(), reductionMeterArea.getBottom() + 2,
               reductionMeterArea.getWidth(), 15, juce::Justification::centred);

    // Show sensitivity value
    g.setFont(10.0f);
    g.setColour(juce::Colours::lightgrey);
    float sensitivity = processorRef.threshold->get();
    float detectionDb = (displayDetectionLevel > 0.00001f)
        ? 20.0f * std::log10(displayDetectionLevel)
        : -100.0f;
    g.drawText("Sens: " + juce::String(sensitivity, 1) + "dB  Det: " + juce::String(detectionDb, 1) + "dB",
               getLocalBounds().removeFromBottom(20), juce::Justification::centred);
}

void PlosiveRemoverEditor::resized()
{
    auto area = getLocalBounds().reduced(20);
    area.removeFromTop(30);    // Space for title
    area.removeFromBottom(80); // Space for meters

    auto sliderWidth = area.getWidth() / 3;

    auto thresholdArea = area.removeFromLeft(sliderWidth);
    thresholdLabel.setBounds(thresholdArea.removeFromBottom(25));
    thresholdSlider.setBounds(thresholdArea);

    auto reductionArea = area.removeFromLeft(sliderWidth);
    reductionLabel.setBounds(reductionArea.removeFromBottom(25));
    reductionSlider.setBounds(reductionArea);

    frequencyLabel.setBounds(area.removeFromBottom(25));
    frequencySlider.setBounds(area);
}

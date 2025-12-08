#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <atomic>

class PlosiveRemoverProcessor : public juce::AudioProcessor
{
public:
    PlosiveRemoverProcessor();
    ~PlosiveRemoverProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Parameters exposed for the UI
    juce::AudioParameterFloat* threshold;
    juce::AudioParameterFloat* reduction;
    juce::AudioParameterFloat* frequency;

    // For metering in UI (atomic for thread safety)
    std::atomic<float> meterGainReduction{0.0f};
    std::atomic<float> meterInputLevel{0.0f};
    std::atomic<float> meterDetectionLevel{0.0f};

    float getGainReductionMeter() const { return meterGainReduction.load(); }
    float getInputLevelMeter() const { return meterInputLevel.load(); }
    float getDetectionLevelMeter() const { return meterDetectionLevel.load(); }

private:
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    // Look-ahead delay buffer
    static constexpr int MAX_LOOKAHEAD_SAMPLES = 512; // ~11ms at 44.1kHz
    std::vector<std::vector<float>> delayBuffer;
    int delayWritePos = 0;
    int lookaheadSamples = 0;

    // Plosive detection filter (isolates low frequencies)
    struct BiquadFilter {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;

        float process(float input) {
            float output = b0 * input + b1 * z1 + b2 * z2 - a1 * z1 - a2 * z2;
            z2 = z1;
            z1 = output;
            return output;
        }

        void reset() { z1 = z2 = 0; }
    };

    BiquadFilter detectionLPF;  // Low-pass for detecting plosives
    BiquadFilter duckingHPF;    // High-pass for ducking low frequencies

    // Per-channel ducking filters (2 cascaded for 24dB/octave slope)
    std::vector<BiquadFilter> channelHPF1;
    std::vector<BiquadFilter> channelHPF2;

    // Envelope followers
    float fastEnvelope = 0.0f;   // Fast attack for transient detection
    float slowEnvelope = 0.0f;   // Slow for sustained content
    float fastAttack = 0.0f;
    float fastRelease = 0.0f;
    float slowAttack = 0.0f;
    float slowRelease = 0.0f;

    // Auto-threshold: track average signal level
    float avgLevel = 0.0f;
    float avgLevelCoeff = 0.0f;

    // Gain reduction (smoothed)
    float currentGainReduction = 1.0f;
    float targetGainReduction = 1.0f;
    float gainSmoothCoeff = 0.0f;

    // Helper functions
    void updateDetectionFilter(float cutoffHz);
    void updateDuckingFilter(float cutoffHz, float gainReduction);
    void calculateBiquadLPF(BiquadFilter& filter, float cutoffHz, float q = 0.707f);
    void calculateBiquadHPF(BiquadFilter& filter, float cutoffHz, float q = 0.707f);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlosiveRemoverProcessor)
};

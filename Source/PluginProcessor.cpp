#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

PlosiveRemoverProcessor::PlosiveRemoverProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    addParameter(threshold = new juce::AudioParameterFloat(
        juce::ParameterID("threshold", 1), "Sensitivity",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 6.0f));

    addParameter(reduction = new juce::AudioParameterFloat(
        juce::ParameterID("reduction", 1), "Reduction",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 70.0f));

    addParameter(frequency = new juce::AudioParameterFloat(
        juce::ParameterID("frequency", 1), "Frequency",
        juce::NormalisableRange<float>(100.0f, 400.0f, 1.0f), 200.0f));
}

PlosiveRemoverProcessor::~PlosiveRemoverProcessor()
{
}

const juce::String PlosiveRemoverProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PlosiveRemoverProcessor::acceptsMidi() const { return false; }
bool PlosiveRemoverProcessor::producesMidi() const { return false; }
bool PlosiveRemoverProcessor::isMidiEffect() const { return false; }

double PlosiveRemoverProcessor::getTailLengthSeconds() const
{
    // Report latency in seconds
    return static_cast<double>(lookaheadSamples) / currentSampleRate;
}

int PlosiveRemoverProcessor::getNumPrograms() { return 1; }
int PlosiveRemoverProcessor::getCurrentProgram() { return 0; }
void PlosiveRemoverProcessor::setCurrentProgram(int) {}
const juce::String PlosiveRemoverProcessor::getProgramName(int) { return {}; }
void PlosiveRemoverProcessor::changeProgramName(int, const juce::String&) {}

void PlosiveRemoverProcessor::calculateBiquadLPF(BiquadFilter& filter, float cutoffHz, float q)
{
    float w0 = 2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(currentSampleRate);
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);
    float alpha = sinw0 / (2.0f * q);

    float a0 = 1.0f + alpha;
    filter.b0 = ((1.0f - cosw0) / 2.0f) / a0;
    filter.b1 = (1.0f - cosw0) / a0;
    filter.b2 = filter.b0;
    filter.a1 = (-2.0f * cosw0) / a0;
    filter.a2 = (1.0f - alpha) / a0;
}

void PlosiveRemoverProcessor::calculateBiquadHPF(BiquadFilter& filter, float cutoffHz, float q)
{
    float w0 = 2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(currentSampleRate);
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);
    float alpha = sinw0 / (2.0f * q);

    float a0 = 1.0f + alpha;
    filter.b0 = ((1.0f + cosw0) / 2.0f) / a0;
    filter.b1 = (-(1.0f + cosw0)) / a0;
    filter.b2 = filter.b0;
    filter.a1 = (-2.0f * cosw0) / a0;
    filter.a2 = (1.0f - alpha) / a0;
}

void PlosiveRemoverProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    // Look-ahead: ~5ms gives us time to react before the plosive hits
    lookaheadSamples = static_cast<int>(sampleRate * 0.005);
    lookaheadSamples = std::min(lookaheadSamples, MAX_LOOKAHEAD_SAMPLES);

    // Report latency to host
    setLatencySamples(lookaheadSamples);

    // Initialize delay buffer for look-ahead
    int numChannels = getTotalNumInputChannels();
    delayBuffer.resize(static_cast<size_t>(numChannels));
    for (auto& channel : delayBuffer)
    {
        channel.resize(static_cast<size_t>(MAX_LOOKAHEAD_SAMPLES), 0.0f);
    }
    delayWritePos = 0;

    // Initialize per-channel HPFs for ducking (2 cascaded = 24dB/octave)
    channelHPF1.resize(static_cast<size_t>(numChannels));
    channelHPF2.resize(static_cast<size_t>(numChannels));
    for (auto& hpf : channelHPF1) hpf.reset();
    for (auto& hpf : channelHPF2) hpf.reset();

    // Reset detection filter
    detectionLPF.reset();
    calculateBiquadLPF(detectionLPF, frequency->get(), 0.707f);

    // Reset envelopes
    fastEnvelope = 0.0f;
    slowEnvelope = 0.0f;

    // Envelope time constants
    // Fast: 1ms attack, 30ms release - catches transients
    fastAttack = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * 0.001f));
    fastRelease = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * 0.030f));

    // Slow: 20ms attack, 100ms release - tracks sustained content
    slowAttack = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * 0.020f));
    slowRelease = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * 0.100f));

    // Gain smoothing: ~3ms
    gainSmoothCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * 0.003f));

    // Average level tracking: ~500ms time constant for stable threshold
    avgLevelCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * 0.5f));
    avgLevel = 0.0f;

    currentGainReduction = 1.0f;
    targetGainReduction = 1.0f;
}

void PlosiveRemoverProcessor::releaseResources()
{
    delayBuffer.clear();
    channelHPF1.clear();
    channelHPF2.clear();
}

bool PlosiveRemoverProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void PlosiveRemoverProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Get current parameter values
    float thresholdDb = threshold->get();
    float reductionAmount = reduction->get() / 100.0f;
    float cutoffFreq = frequency->get();

    // Update detection filter - use wider bandwidth for detection (1.5x the cutoff)
    // This catches more of the plosive energy
    calculateBiquadLPF(detectionLPF, cutoffFreq * 1.5f, 0.5f);

    // Update ducking HPF coefficients (cascaded for steeper slope)
    for (size_t i = 0; i < channelHPF1.size(); ++i)
    {
        calculateBiquadHPF(channelHPF1[i], cutoffFreq, 0.707f);
        calculateBiquadHPF(channelHPF2[i], cutoffFreq, 0.707f);
    }

    auto numSamples = buffer.getNumSamples();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Sum input for detection (mono sum)
        float inputSum = 0.0f;
        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {
            inputSum += buffer.getReadPointer(channel)[sample];
        }
        inputSum /= static_cast<float>(totalNumInputChannels);

        // Low-pass filter to isolate plosive frequencies
        float lowFreqContent = detectionLPF.process(inputSum);
        float lowFreqAbs = std::abs(lowFreqContent);

        // Fast envelope (catches transients) - 0.5ms attack
        if (lowFreqAbs > fastEnvelope)
            fastEnvelope += fastAttack * (lowFreqAbs - fastEnvelope);
        else
            fastEnvelope += fastRelease * (lowFreqAbs - fastEnvelope);

        // Slow envelope (tracks sustained content) - 50ms attack
        if (lowFreqAbs > slowEnvelope)
            slowEnvelope += slowAttack * (lowFreqAbs - slowEnvelope);
        else
            slowEnvelope += slowRelease * (lowFreqAbs - slowEnvelope);

        // Track average low-frequency level (slow follower for stable reference)
        avgLevel += avgLevelCoeff * (lowFreqAbs - avgLevel);

        // Convert to dB
        float envelopeDb = (fastEnvelope > 0.00001f)
            ? 20.0f * std::log10(fastEnvelope)
            : -100.0f;

        float avgLevelDb = (avgLevel > 0.00001f)
            ? 20.0f * std::log10(avgLevel)
            : -100.0f;

        // Sensitivity control: how many dB above average to trigger
        // Higher sensitivity value = triggers on smaller spikes (more sensitive)
        // sensitivity=24 means trigger when 0dB above average (very sensitive)
        // sensitivity=0 means trigger when 24dB above average (not sensitive)
        float triggerThresholdDb = avgLevelDb + (24.0f - thresholdDb);

        // Calculate target gain reduction
        targetGainReduction = 1.0f;

        // Trigger when fast envelope exceeds threshold
        // Must also exceed absolute minimum to avoid triggering on silence
        bool hasEnergy = (envelopeDb > triggerThresholdDb) && (envelopeDb > -40.0f);

        if (hasEnergy)
        {
            // Proportional reduction based on how much we exceed threshold
            float excessDb = envelopeDb - triggerThresholdDb;
            float ratio = std::min(1.0f, excessDb / 6.0f);  // Full reduction at 6dB over threshold
            targetGainReduction = 1.0f - (reductionAmount * ratio);
            targetGainReduction = std::max(0.05f, targetGainReduction);
        }

        // Smooth gain changes with separate attack/release
        // Faster attack to catch plosive, slower release to avoid clicks on recovery
        if (targetGainReduction < currentGainReduction) {
            // Attack: reduce gain quickly (but not instant)
            currentGainReduction += gainSmoothCoeff * 2.0f * (targetGainReduction - currentGainReduction);
        } else {
            // Release: restore gain slowly to avoid click
            currentGainReduction += gainSmoothCoeff * 0.3f * (targetGainReduction - currentGainReduction);
        }

        // Process each channel with look-ahead
        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {
            auto* channelData = buffer.getWritePointer(channel);
            auto& delay = delayBuffer[static_cast<size_t>(channel)];

            // Read from delay buffer (look-ahead)
            int readPos = (delayWritePos - lookaheadSamples + MAX_LOOKAHEAD_SAMPLES) % MAX_LOOKAHEAD_SAMPLES;
            float delayedSample = delay[static_cast<size_t>(readPos)];

            // Write current sample to delay buffer
            delay[static_cast<size_t>(delayWritePos)] = channelData[sample];

            // Apply gain reduction with soft knee
            // Use squared gain for smoother perceived volume change
            float smoothGain = currentGainReduction * currentGainReduction;
            smoothGain = smoothGain * 0.5f + currentGainReduction * 0.5f; // Blend linear and squared

            float outputSample = delayedSample * smoothGain;

            channelData[sample] = outputSample;
        }

        // Advance delay write position
        delayWritePos = (delayWritePos + 1) % MAX_LOOKAHEAD_SAMPLES;
    }

    // Update meters (once per block for efficiency)
    meterInputLevel.store(std::abs(buffer.getSample(0, numSamples / 2)));
    meterDetectionLevel.store(fastEnvelope);
    meterGainReduction.store(1.0f - currentGainReduction);
}

bool PlosiveRemoverProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PlosiveRemoverProcessor::createEditor()
{
    return new PlosiveRemoverEditor(*this);
}

void PlosiveRemoverProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = juce::ValueTree("PlosiveRemoverState");
    state.setProperty("threshold", threshold->get(), nullptr);
    state.setProperty("reduction", reduction->get(), nullptr);
    state.setProperty("frequency", frequency->get(), nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PlosiveRemoverProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName("PlosiveRemoverState"))
    {
        auto state = juce::ValueTree::fromXml(*xmlState);
        *threshold = static_cast<float>(state.getProperty("threshold", -30.0f));
        *reduction = static_cast<float>(state.getProperty("reduction", 70.0f));
        *frequency = static_cast<float>(state.getProperty("frequency", 150.0f));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PlosiveRemoverProcessor();
}

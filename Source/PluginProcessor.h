#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Telephone.h"

class N3310Processor : public juce::AudioProcessor
{
public:
    N3310Processor();
    ~N3310Processor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "3310"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static constexpr int kNumChoices = 10; // 0 = bypass, 1..9 = modes
    static juce::StringArray modeChoices();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> lastActiveMode { 1 }; // last non-bypass mode, used by the 0 key

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    n3310::Engine engine_;
    std::atomic<float>* modeParam_ = nullptr;
    double sampleRate_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (N3310Processor)
};

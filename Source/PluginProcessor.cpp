#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::StringArray N3310Processor::modeChoices()
{
    return { "0 BYPASS", "1 CALL", "2 STUTTER", "3 AM RADIO", "4 LOW BATT",
             "5 WALKIE", "6 SCRAMBLE", "7 BAD LINE", "8 CORRUPT", "9 ROBOT" };
}

juce::AudioProcessorValueTreeState::ParameterLayout N3310Processor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Mode", modeChoices(), 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix",
                                                              juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f,
                                                              juce::AudioParameterFloatAttributes().withLabel ("%")
                                                                  .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));
    return layout;
}

N3310Processor::N3310Processor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    modeParam_ = apvts.getRawParameterValue ("mode");
    mixParam_ = apvts.getRawParameterValue ("mix");
}

bool N3310Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void N3310Processor::prepareToPlay (double sampleRate, int)
{
    sampleRate_ = sampleRate;
    engine_.prepare (sampleRate);
    engine_.setModeIndex ((int) modeParam_->load());
    engine_.setMix (mixParam_->load());
    setLatencySamples (engine_.latencySamples());
}

void N3310Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels());
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
    if (numCh <= 0)
        return;

    const int mode = juce::jlimit (0, kNumChoices - 1, (int) modeParam_->load());
    if (mode > 0)
        lastActiveMode.store (mode);
    engine_.setModeIndex (mode);
    engine_.setMix (mixParam_->load());

    float* left = buffer.getWritePointer (0);
    float* right = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    engine_.process (left, right, buffer.getNumSamples());
}

juce::AudioProcessorEditor* N3310Processor::createEditor() { return new N3310Editor (*this); }

void N3310Processor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void N3310Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new N3310Processor(); }

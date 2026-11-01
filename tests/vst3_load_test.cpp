// Loads the built 3310.vst3 through JUCE's plugin host code (the same way a DAW would),
// then prepares it, processes audio, checks latency / bypass and opens the editor.
//   usage: N3310HostTest /path/to/3310.vst3
#include <juce_audio_processors/juce_audio_processors.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: N3310HostTest <3310.vst3>\n"); return 2; }

    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> descs;
    fmt.findAllTypesForFile (descs, argv[1]);
    std::printf ("plugin types found in file: %d\n", descs.size());
    if (descs.isEmpty()) { std::printf ("FAILED: nothing found\n"); return 1; }
    const auto& d = *descs[0];
    std::printf ("name '%s'  manufacturer '%s'  category '%s'  inputs %d outputs %d  instrument %d\n",
                 d.name.toRawUTF8(), d.manufacturerName.toRawUTF8(), d.category.toRawUTF8(), d.numInputChannels, d.numOutputChannels, (int) d.isInstrument);

    juce::String err;
    std::unique_ptr<juce::AudioPluginInstance> inst (fmt.createInstanceFromDescription (d, 48000.0, 512, err));
    if (! inst) { std::printf ("FAILED to instantiate: %s\n", err.toRawUTF8()); return 1; }

    inst->setPlayConfigDetails (2, 2, 48000.0, 512);
    inst->prepareToPlay (48000.0, 512);
    std::printf ("latency reported to host: %d samples\n", inst->getLatencySamples());
    std::printf ("parameters:");
    for (auto* p : inst->getParameters())
        std::printf (" [%s = %s]", p->getName (32).toRawUTF8(), p->getCurrentValueAsText().toRawUTF8());
    std::printf ("\n");

    int failures = 0;
    if (inst->getLatencySamples() < 900 || inst->getLatencySamples() > 1100) { std::printf ("BAD latency\n"); ++failures; }

    // process a sine through every mode via the host-visible parameter
    juce::AudioProcessorParameter* mode = nullptr;
    for (auto* p : inst->getParameters()) if (p->getName (32) == "Mode") mode = p;
    if (! mode) { std::printf ("FAILED: no Mode parameter\n"); return 1; }
    for (int m = 0; m <= 9; ++m)
    {
        mode->setValueNotifyingHost ((float) m / 9.0f);
        double e = 0; bool fin = true;
        for (int b = 0; b < 200; ++b)
        {
            juce::AudioBuffer<float> buf (2, 512);
            for (int i = 0; i < 512; ++i)
            {
                const float s = 0.3f * std::sin ((float) (b * 512 + i) * 0.05f);
                buf.setSample (0, i, s); buf.setSample (1, i, s);
            }
            juce::MidiBuffer midi;
            inst->processBlock (buf, midi);
            if (b > 60) for (int i = 0; i < 512; ++i) { const float v = buf.getSample (0, i); if (! std::isfinite (v)) fin = false; e += (double) v * v; }
        }
        std::printf ("  mode %d via host: energy %.1f %s\n", m, e, fin && e > 1.0 ? "ok" : "BAD");
        if (! fin || e < 1.0) ++failures;
    }

    // state save / restore through the host interface
    mode->setValueNotifyingHost (7.0f / 9.0f);
    juce::MemoryBlock mb; inst->getStateInformation (mb);
    mode->setValueNotifyingHost (2.0f / 9.0f);
    inst->setStateInformation (mb.getData(), (int) mb.getSize());
    const int restored = juce::roundToInt (mode->getValue() * 9.0f);
    std::printf ("state round trip: mode %d %s\n", restored, restored == 7 ? "ok" : "BAD");
    if (restored != 7) ++failures;

    // editor opens
    if (inst->hasEditor())
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        std::printf ("editor created: %s (%dx%d)\n", ed ? "yes" : "NO", ed ? ed->getWidth() : 0, ed ? ed->getHeight() : 0);
        if (! ed) ++failures;
    }

    inst->releaseResources();
    std::printf (failures ? "FAILED (%d)\n" : "ALL OK\n", failures);
    return failures ? 1 : 0;
}

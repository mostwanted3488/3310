// Headless test of the real plugin classes: audio path through the processor
// (parameter -> engine, latency, bypass) and UI snapshots of every mode.
//   usage: N3310Test <output_dir>

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginEditor.h"
#include "PluginProcessor.h"

static void setMode (N3310Processor& p, int mode)
{
    auto* prm = p.apvts.getParameter ("mode");
    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) mode));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (argc > 1 ? argv[1] : ".");
    out.createDirectory();
    int failures = 0;

    N3310Processor proc;
    const double fs = 48000.0;
    proc.setPlayConfigDetails (2, 2, fs, 512);
    proc.prepareToPlay (fs, 512);
    std::printf ("reported latency: %d samples (%.2f ms)\n", proc.getLatencySamples(), 1000.0 * proc.getLatencySamples() / fs);

    // ---- audio: every mode must produce finite, non-silent output
    juce::Random rnd (7);
    for (int mode = 0; mode <= 9; ++mode)
    {
        setMode (proc, mode);
        double energy = 0.0;
        bool finite = true;
        for (int b = 0; b < 400; ++b)
        {
            juce::AudioBuffer<float> buf (2, 512);
            for (int i = 0; i < 512; ++i)
            {
                const float s = 0.3f * std::sin ((float) (b * 512 + i) * 0.05f) * (0.5f + 0.5f * std::sin ((float) (b * 512 + i) * 0.0004f))
                                + 0.02f * (rnd.nextFloat() - 0.5f);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            if (b > 100)
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                    {
                        const float v = buf.getSample (ch, i);
                        if (! std::isfinite (v) || std::fabs (v) > 1.5f) finite = false;
                        energy += (double) v * v;
                    }
        }
        std::printf ("mode %d: energy %.2f %s\n", mode, energy, finite ? "ok" : "BAD");
        if (! finite || energy < 1.0) ++failures;
    }

    // ---- bypass: output must equal the input delayed by the reported latency
    {
        N3310Processor p2;
        p2.setPlayConfigDetails (2, 2, fs, 512);
        p2.prepareToPlay (fs, 512);
        setMode (p2, 0);
        const int lat = p2.getLatencySamples();
        std::vector<float> in, res;
        for (int b = 0; b < 200; ++b)
        {
            juce::AudioBuffer<float> buf (2, 512);
            for (int i = 0; i < 512; ++i)
            {
                const float s = 0.4f * std::sin ((float) (b * 512 + i) * 0.031f);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
                in.push_back (s);
            }
            juce::MidiBuffer midi;
            p2.processBlock (buf, midi);
            for (int i = 0; i < 512; ++i) res.push_back (buf.getSample (0, i));
        }
        double err = 0.0;
        for (size_t i = 20000; i < res.size(); ++i)
            err = std::max (err, (double) std::fabs (res[i] - in[i - (size_t) lat]));
        std::printf ("bypass max error vs delayed input: %.6f %s\n", err, err < 1.0e-4 ? "ok" : "BAD");
        if (err > 1.0e-4) ++failures;
    }

    // ---- mix: 0% = dry (delayed by the latency), 50% = in between
    {
        N3310Processor p4;
        p4.setPlayConfigDetails (2, 2, fs, 512);
        p4.prepareToPlay (fs, 512);
        setMode (p4, 1);
        auto* mp = p4.apvts.getParameter ("mix");
        mp->setValueNotifyingHost (mp->convertTo0to1 (0.0f));
        const int lat = p4.getLatencySamples();
        std::vector<float> in, res;
        for (int b = 0; b < 200; ++b)
        {
            juce::AudioBuffer<float> buf (2, 512);
            for (int i = 0; i < 512; ++i)
            {
                const float s = 0.4f * std::sin ((float) (b * 512 + i) * 0.031f);
                buf.setSample (0, i, s); buf.setSample (1, i, s); in.push_back (s);
            }
            juce::MidiBuffer midi;
            p4.processBlock (buf, midi);
            for (int i = 0; i < 512; ++i) res.push_back (buf.getSample (0, i));
        }
        double err = 0.0;
        for (size_t i = 20000; i < res.size(); ++i)
            err = std::max (err, (double) std::fabs (res[i] - in[i - (size_t) lat]));
        std::printf ("mix 0%% max error vs delayed input: %.6f %s\n", err, err < 1.0e-4 ? "ok" : "BAD");
        if (err > 1.0e-4) ++failures;
    }

    // ---- state round trip
    {
        setMode (proc, 6);
        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        N3310Processor p3;
        p3.setStateInformation (mb.getData(), (int) mb.getSize());
        const int m = (int) std::lround (p3.apvts.getRawParameterValue ("mode")->load());
        std::printf ("state restore: mode %d %s\n", m, m == 6 ? "ok" : "BAD");
        if (m != 6) ++failures;
    }

    // ---- UI snapshots
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (490, 1060);
        for (int mode : { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 })
        {
            setMode (proc, mode);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (600);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png;
            juce::FileOutputStream fos (out.getChildFile ("ui_mode" + juce::String (mode) + ".png"));
            fos.setPosition (0);
            fos.truncate();
            png.writeImageToStream (img, fos);
        }
        // soft keys: right = mix up, left = mix down
        {
            setMode (proc, 3);
            auto* mp = proc.apvts.getParameter ("mix");
            mp->setValueNotifyingHost (mp->convertTo0to1 (0.5f));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
            auto clickAt = [&] (float dx, float dy)
            {
                const juce::Point<float> pos (dx, dy);
                ed->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                 ed.get(), ed.get(), juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false));
                ed->mouseUp (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                               ed.get(), ed.get(), juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false));
                juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            };
            clickAt (330.0f, 622.0f); // right soft key
            float mx = proc.apvts.getRawParameterValue ("mix")->load();
            std::printf ("right soft key -> mix %.2f %s\n", mx, std::fabs (mx - 0.6f) < 0.01f ? "ok" : "BAD");
            if (std::fabs (mx - 0.6f) > 0.01f) ++failures;
            {
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                juce::PNGImageFormat png;
                juce::FileOutputStream fos (out.getChildFile ("ui_mix.png"));
                fos.setPosition (0); fos.truncate();
                png.writeImageToStream (img, fos);
            }
            clickAt (140.0f, 610.0f); // left soft key
            clickAt (140.0f, 610.0f);
            mx = proc.apvts.getRawParameterValue ("mix")->load();
            std::printf ("left soft key x2 -> mix %.2f %s\n", mx, std::fabs (mx - 0.4f) < 0.01f ? "ok" : "BAD");
            if (std::fabs (mx - 0.4f) > 0.01f) ++failures;
            mp->setValueNotifyingHost (mp->convertTo0to1 (1.0f));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        }

        // click on key "5" and on key "0" through the real mouse path
        setMode (proc, 1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        auto click = [&] (float dx, float dy)
        {
            const juce::Point<float> pos (dx, dy);
            ed->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                             ed.get(), ed.get(), juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
        };
        click (213.7f + 30.0f, 763.6f + 30.0f); // key 5
        int m = (int) std::lround (proc.apvts.getRawParameterValue ("mode")->load());
        std::printf ("click key 5 -> mode %d %s\n", m, m == 5 ? "ok" : "BAD");
        if (m != 5) ++failures;
        click (213.5f + 30.0f, 906.2f + 30.0f); // key 0
        m = (int) std::lround (proc.apvts.getRawParameterValue ("mode")->load());
        std::printf ("click key 0 -> mode %d %s\n", m, m == 0 ? "ok" : "BAD");
        if (m != 0) ++failures;
        click (213.5f + 30.0f, 906.2f + 30.0f); // key 0 again: back to last mode
        m = (int) std::lround (proc.apvts.getRawParameterValue ("mode")->load());
        std::printf ("click key 0 again -> mode %d %s\n", m, m == 5 ? "ok" : "BAD");
        if (m != 5) ++failures;
    }

    std::printf (failures ? "FAILED (%d)\n" : "ALL OK\n", failures);
    return failures ? 1 : 0;
}

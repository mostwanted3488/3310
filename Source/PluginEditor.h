#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PixelArt.h"

// The whole plugin UI is the Nokia: pixel LCD + the 12 keys.
// Keys 1-9 pick a mode, 0 toggles bypass (the phone switches off).
// The right-hand scroll key (up / down arrows) raises / lowers the dry-wet mix.
class N3310Editor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit N3310Editor (N3310Processor&);
    ~N3310Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Key
    {
        char label;
        float cx, cy, w, h, rotDeg; // design space: padded photo, 490 x 1060
    };

    void timerCallback() override;
    int keyAt (juce::Point<float> designPos) const;
    juce::Path keyPath (const Key&, float grow) const;
    void pressKey (int index);
    void setMode (int mode);
    void setMix (float mix);
    void pressScrollKey (bool up); // right-hand key: up arrow = mix up, down arrow = mix down
    void drawLcd (juce::Graphics&) const;
    void drawKeys (juce::Graphics&) const;

    juce::Point<float> toDesign (juce::Point<float> p) const;
    juce::AffineTransform designToScreen() const;

    N3310Processor& processor_;
    juce::Image phone_;
    juce::ParameterAttachment modeAttachment_;
    juce::ParameterAttachment mixAttachment_;

    juce::Rectangle<float> phoneBounds_;
    float scale_ = 1.0f;

    int mode_ = 1;          // 0 = bypass
    float mix_ = 1.0f;      // 0..1
    int mixShowMs_ = 0;     // the LCD shows the mix bar while > 0
    bool ready_ = false;
    float lcdOn_ = 1.0f;    // 0..1, animated
    int frame_ = 0;
    int frameMs_ = 0;
    int pressedKey_ = -1;
    int pressedMs_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (N3310Editor)
};

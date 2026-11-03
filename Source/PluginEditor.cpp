#include "PluginEditor.h"
#include "BinaryData.h"
#include <string>

namespace
{
constexpr float kDesignW = 490.0f, kDesignH = 1060.0f, kPad = 30.0f;

// LCD rectangle inside the padded photo (phone coordinates + padding)
const juce::Rectangle<float> kLcd { 64.0f + kPad, 260.0f + kPad, 302.0f, 212.0f };
constexpr int kGridW = 84, kGridH = 59; // logical LCD pixels (square on screen)

// Key ellipses measured on the photo. cx/cy are in phone coordinates (add kPad).
const struct { char c; float x, y, w, h, r; } kKeyData[12] = {
    { '1', 86.7f, 676.5f, 80.7f, 40.9f, 19.6f },  { '2', 213.7f, 692.3f, 83.5f, 42.4f, 1.2f },  { '3', 340.4f, 677.1f, 80.7f, 42.3f, -17.1f },
    { '4', 91.5f, 747.6f, 80.6f, 41.1f, 19.2f },  { '5', 213.7f, 763.6f, 83.5f, 42.2f, 0.9f },  { '6', 335.7f, 748.0f, 80.6f, 42.5f, -17.6f },
    { '7', 96.1f, 818.8f, 80.5f, 41.0f, 19.9f },  { '8', 213.4f, 835.0f, 83.4f, 41.7f, 0.9f },  { '9', 330.9f, 819.5f, 80.7f, 42.1f, -17.5f },
    { '*', 100.3f, 889.5f, 80.0f, 40.5f, 19.5f }, { '0', 213.5f, 906.2f, 83.1f, 41.4f, 1.2f },  { '#', 326.5f, 890.4f, 80.6f, 41.7f, -17.6f }
};

// The right-hand key of the phone is a two-way scroll key: its upper end (up arrow) raises the
// mix, its lower end (down arrow) lowers it. Polygon in padded-photo coordinates.
const std::vector<juce::Point<float>> kScrollKey { { 362, 570 }, { 380, 573 }, { 391, 586 }, { 386, 605 }, { 345, 648 },
                                                   { 295, 674 }, { 280, 672 }, { 270, 640 }, { 269, 617 }, { 300, 603 } };
const juce::Point<float> kScrollCentre { 331.0f, 620.0f };
const juce::Point<float> kScrollAxis { -0.77f, 0.63f }; // points towards the lower-left (down arrow) end

float scrollSide (juce::Point<float> d) // < 0: up arrow half, > 0: down arrow half
{
    return (d.x - kScrollCentre.x) * kScrollAxis.x + (d.y - kScrollCentre.y) * kScrollAxis.y;
}

juce::Path scrollKeyPath()
{
    juce::Path p;
    p.startNewSubPath (kScrollKey[0]);
    for (size_t i = 1; i < kScrollKey.size(); ++i)
        p.lineTo (kScrollKey[i]);
    p.closeSubPath();
    return p.createPathWithRoundedCorners (6.0f);
}

const char* const kLcdNames[10] = { "", "1 CALL", "2 STUTTER", "3 AM RADIO", "4 LOW BATT",
                                    "5 WALKIE", "6 SCRAMBLE", "7 BAD LINE", "8 CORRUPT", "9 ROBOT" };

const juce::Colour kInk { 0xff1b2a1d };
const juce::Colour kLcdTop { 0xff86b98d }, kLcdMain { 0xff9bc9a0 }, kLcdOff { 0xff6d8571 };
} // namespace

N3310Editor::N3310Editor (N3310Processor& p)
    : AudioProcessorEditor (&p),
      processor_ (p),
      modeAttachment_ (*p.apvts.getParameter ("mode"),
                       [this] (float v)
                       {
                           mode_ = (int) std::lround (v);
                           if (mode_ > 0)
                               processor_.lastActiveMode.store (mode_);
                           frame_ = 0;
                           repaint();
                       }),
      mixAttachment_ (*p.apvts.getParameter ("mix"),
                      [this] (float v)
                      {
                          mix_ = v;
                          if (ready_)
                              mixShowMs_ = 1800;
                          repaint();
                      })
{
    phone_ = juce::ImageCache::getFromMemory (BinaryData::phone_png, BinaryData::phone_pngSize);

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);
    getConstrainer()->setSizeLimits (245, 530, 980, 2120);
    setSize (368, (int) std::lround (368.0 * kDesignH / kDesignW));

    modeAttachment_.sendInitialUpdate();
    mixAttachment_.sendInitialUpdate();
    ready_ = true;
    lcdOn_ = mode_ == 0 ? 0.0f : 1.0f;
    startTimer (33);
}

N3310Editor::~N3310Editor() { stopTimer(); }

// ----------------------------------------------------------------- geometry
void N3310Editor::resized()
{
    const auto b = getLocalBounds().toFloat();
    scale_ = juce::jmin (b.getWidth() / kDesignW, b.getHeight() / kDesignH);
    const float w = kDesignW * scale_, h = kDesignH * scale_;
    phoneBounds_ = { b.getCentreX() - w * 0.5f, b.getCentreY() - h * 0.5f, w, h };
}

juce::AffineTransform N3310Editor::designToScreen() const
{
    return juce::AffineTransform::scale (scale_).translated (phoneBounds_.getX(), phoneBounds_.getY());
}

juce::Point<float> N3310Editor::toDesign (juce::Point<float> p) const
{
    return { (p.x - phoneBounds_.getX()) / scale_, (p.y - phoneBounds_.getY()) / scale_ };
}

juce::Path N3310Editor::keyPath (const Key& k, float grow) const
{
    juce::Path p;
    const float w = k.w * grow, h = k.h * grow;
    p.addEllipse (-w * 0.5f, -h * 0.5f, w, h);
    p.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (k.rotDeg)).translated (k.cx, k.cy));
    return p;
}

int N3310Editor::keyAt (juce::Point<float> d) const
{
    for (int i = 0; i < 12; ++i)
    {
        const auto& kd = kKeyData[i];
        const float a = juce::degreesToRadians (-kd.r);
        const float dx = d.x - (kd.x + kPad), dy = d.y - (kd.y + kPad);
        const float rx = dx * std::cos (a) - dy * std::sin (a);
        const float ry = dx * std::sin (a) + dy * std::cos (a);
        const float ex = rx / (kd.w * 0.5f * 1.04f), ey = ry / (kd.h * 0.5f * 1.04f);
        if (ex * ex + ey * ey <= 1.0f)
            return i;
    }
    return -1;
}

// -------------------------------------------------------------------- input
void N3310Editor::mouseDown (const juce::MouseEvent& e)
{
    const auto d = toDesign (e.position);
    if (scrollKeyPath().contains (d))
    {
        pressScrollKey (scrollSide (d) < 0.0f);
        return;
    }
    const int k = keyAt (d);
    if (k >= 0)
        pressKey (k);
}

void N3310Editor::mouseUp (const juce::MouseEvent&)
{
    pressedKey_ = -1;
    repaint();
}

void N3310Editor::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (w.deltaY != 0.0f)
        setMix (juce::jlimit (0.0f, 1.0f, std::round ((mix_ + (w.deltaY > 0.0f ? 0.05f : -0.05f)) * 20.0f) / 20.0f));
}

void N3310Editor::setMix (float mix)
{
    mixShowMs_ = 1800;
    mixAttachment_.beginGesture();
    mixAttachment_.setValueAsPartOfGesture (mix);
    mixAttachment_.endGesture();
    repaint();
}

void N3310Editor::pressScrollKey (bool up)
{
    pressedKey_ = up ? 12 : 13;
    pressedMs_ = 0;
    setMix (juce::jlimit (0.0f, 1.0f, std::round ((mix_ + (up ? 0.1f : -0.1f)) * 10.0f) / 10.0f));
}

void N3310Editor::setMode (int mode)
{
    modeAttachment_.beginGesture();
    modeAttachment_.setValueAsPartOfGesture ((float) mode);
    modeAttachment_.endGesture();
}

void N3310Editor::pressKey (int index)
{
    pressedKey_ = index;
    pressedMs_ = 0;
    const char c = kKeyData[index].c;
    if (c >= '1' && c <= '9')
    {
        setMode (c - '0');
    }
    else if (c == '0') // bypass toggle
    {
        if (mode_ == 0)
            setMode (juce::jlimit (1, 9, processor_.lastActiveMode.load()));
        else
            setMode (0);
    }
    repaint();
}

void N3310Editor::timerCallback()
{
    bool dirty = false;

    frameMs_ += 33;
    if (frameMs_ >= 260)
    {
        frameMs_ = 0;
        ++frame_;
        dirty = true;
    }

    const float target = mode_ == 0 ? 0.0f : 1.0f;
    if (std::abs (lcdOn_ - target) > 1.0e-4f)
    {
        const float step = 33.0f / 220.0f;
        lcdOn_ = target > lcdOn_ ? juce::jmin (target, lcdOn_ + step) : juce::jmax (target, lcdOn_ - step);
        dirty = true;
    }

    if (mixShowMs_ > 0)
    {
        mixShowMs_ = juce::jmax (0, mixShowMs_ - 33);
        dirty = true;
    }

    if (pressedKey_ >= 0)
    {
        pressedMs_ += 33;
        if (pressedMs_ > 120 && ! isMouseButtonDown())
        {
            pressedKey_ = -1;
            dirty = true;
        }
    }
    if (dirty)
        repaint();
}

// ------------------------------------------------------------------ drawing
void N3310Editor::paint (juce::Graphics& g)
{
    // stage
    g.fillAll (juce::Colour (0xff050607));
    juce::ColourGradient glow (juce::Colour (0xff16222a), getWidth() * 0.5f, getHeight() * 0.4f,
                               juce::Colour (0xff08090b), getWidth() * 0.5f, getHeight() * 1.0f, true);
    g.setGradientFill (glow);
    g.fillAll();

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (phone_, phoneBounds_, juce::RectanglePlacement::stretchToFit);

    drawLcd (g);
    drawKeys (g);
}

void N3310Editor::drawLcd (juce::Graphics& g) const
{
    const auto lcd = kLcd.transformedBy (designToScreen());
    juce::Graphics::ScopedSaveState state (g);

    juce::Path clip;
    clip.addRoundedRectangle (lcd, lcd.getWidth() * 0.022f);
    g.reduceClipRegion (clip);

    // background: lit green gradient -> flat dull green when the phone is off
    juce::ColourGradient bg (kLcdTop.interpolatedWith (kLcdOff, 1.0f - lcdOn_), lcd.getX(), lcd.getY(),
                             kLcdMain.interpolatedWith (kLcdOff, 1.0f - lcdOn_), lcd.getX(), lcd.getBottom(), false);
    bg.addColour (0.38, kLcdMain.interpolatedWith (kLcdOff, 1.0f - lcdOn_));
    g.setGradientFill (bg);
    g.fillRect (lcd);

    const float px = lcd.getWidth() / (float) kGridW, py = lcd.getHeight() / (float) kGridH;
    auto cell = [&] (int gx, int gy) -> juce::Rectangle<int>
    {
        const int x0 = (int) std::lround (lcd.getX() + gx * px), x1 = (int) std::lround (lcd.getX() + (gx + 1) * px);
        const int y0 = (int) std::lround (lcd.getY() + gy * py), y1 = (int) std::lround (lcd.getY() + (gy + 1) * py);
        return { x0, y0, juce::jmax (1, x1 - x0), juce::jmax (1, y1 - y0) };
    };

    if (lcdOn_ > 0.01f)
    {
        const float ghostOff = juce::jmax (1.0f, px * 0.3f);
        // ghost pass first so the real pixels always sit on top
        auto drawAll = [&] (bool ghostPass)
        {
            auto put = [&] (int gx, int gy)
            {
                auto r = cell (gx, gy);
                if (ghostPass)
                {
                    g.setColour (kInk.withAlpha (0.16f * lcdOn_));
                    g.fillRect (r.translated ((int) std::lround (ghostOff), (int) std::lround (ghostOff)));
                }
                else
                {
                    g.setColour (kInk.withAlpha (lcdOn_));
                    g.fillRect (r);
                }
            };
            auto text = [&] (const char* s, int x0, int y0)
            {
                for (int i = 0; s[i]; ++i)
                {
                    const auto* gl = n3310art::glyph (s[i]);
                    for (int row = 0; row < 7; ++row)
                        for (int col = 0; col < 5; ++col)
                            if (gl[row] & (1 << (4 - col)))
                                put (x0 + i * 6 + col, y0 + row);
                }
            };

            // title
            text ("3310", (kGridW - n3310art::textWidth ("3310")) / 2, 3);

            // signal (left) and battery (right) bars, lit from the bottom
            const int idx = juce::jlimit (1, 9, mode_ <= 0 ? 1 : mode_) - 1;
            const int sig = juce::jmin (4, 1 + idx / 3);
            for (int b = 0; b < 4; ++b)
            {
                const bool lit = (3 - b) < sig;
                const bool litBat = true;
                for (int r = 0; r < 2; ++r)
                    for (int c = 0; c < 3; ++c)
                    {
                        if (lit) put (3 + c, 6 + b * 4 + r);
                        if (litBat) put (78 + c, 6 + b * 4 + r);
                    }
            }

            // animated emoji
            static thread_local n3310art::Canvas canvas;
            n3310art::drawEmoji (canvas, juce::jlimit (1, 9, mode_ <= 0 ? 1 : mode_), frame_);
            const int ox = (kGridW - n3310art::kW) / 2, oy = 13;
            for (int y = 0; y < n3310art::kH; ++y)
                for (int x = 0; x < n3310art::kW; ++x)
                    if (canvas.px[y][x])
                        put (ox + x, oy + y);

            // mode name, or the mix readout for a moment after the mix changes
            if (mixShowMs_ > 0)
            {
                const std::string label = "MIX " + std::to_string (juce::roundToInt (mix_ * 100.0f)) + "%";
                text (label.c_str(), (kGridW - n3310art::textWidth (label.c_str())) / 2, 49);
                const int lit = juce::roundToInt (mix_ * 10.0f);
                for (int seg = 0; seg < 10; ++seg)
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 5; ++c)
                        {
                            const int gx = 8 + seg * 7 + c, gy = 44 + r;
                            if (seg < lit)
                                put (gx, gy);
                            else if (! ghostPass)
                            {
                                g.setColour (kInk.withAlpha (0.16f * lcdOn_));
                                g.fillRect (cell (gx, gy));
                            }
                        }
            }
            else
            {
                const char* name = kLcdNames[juce::jlimit (1, 9, mode_ <= 0 ? 1 : mode_)];
                text (name, (kGridW - n3310art::textWidth (name)) / 2, 49);
            }
        };
        drawAll (true);
        drawAll (false);

        // dim unlit signal bars
        for (int b = 0; b < 4; ++b)
        {
            const int idx = juce::jlimit (1, 9, mode_ <= 0 ? 1 : mode_) - 1;
            const int sig = juce::jmin (4, 1 + idx / 3);
            if ((3 - b) >= sig)
                for (int r = 0; r < 2; ++r)
                    for (int c = 0; c < 3; ++c)
                    {
                        g.setColour (kInk.withAlpha (0.16f * lcdOn_));
                        g.fillRect (cell (3 + c, 6 + b * 4 + r));
                    }
        }
    }

    // LCD pixel grid + inner shadow + glass highlight
    g.setColour (juce::Colour (0xff142819).withAlpha (0.07f));
    for (int x = 1; x < kGridW; ++x)
        g.fillRect (juce::Rectangle<float> (lcd.getX() + x * px - 0.5f, lcd.getY(), 1.0f, lcd.getHeight()));
    for (int y = 1; y < kGridH; ++y)
        g.fillRect (juce::Rectangle<float> (lcd.getX(), lcd.getY() + y * py - 0.5f, lcd.getWidth(), 1.0f));

    juce::ColourGradient shade (juce::Colour (0xff0a1e0f).withAlpha (0.42f), lcd.getX(), lcd.getY(),
                                juce::Colour (0xff0a1e0f).withAlpha (0.0f), lcd.getX(), lcd.getY() + lcd.getHeight() * 0.1f, false);
    g.setGradientFill (shade);
    g.fillRect (lcd);

    juce::ColourGradient glass (juce::Colours::white.withAlpha (0.14f), lcd.getX(), lcd.getY(),
                                juce::Colours::white.withAlpha (0.0f), lcd.getX() + lcd.getWidth() * 0.45f, lcd.getY() + lcd.getHeight() * 0.7f, false);
    g.setGradientFill (glass);
    g.fillRect (lcd);

    g.setColour (juce::Colour (0xff142819).withAlpha (0.35f));
    g.drawRoundedRectangle (lcd.reduced (0.5f), lcd.getWidth() * 0.022f, 1.0f);
}

void N3310Editor::drawKeys (juce::Graphics& g) const
{
    const auto t = designToScreen();
    if (pressedKey_ >= 12)
    {
        // shade only the half of the scroll key that was pressed
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (scrollKeyPath(), t);
        const float sgn = pressedKey_ == 13 ? 1.0f : -1.0f;
        const juce::Point<float> a = kScrollAxis, perp { 0.63f, 0.77f };
        juce::Path half;
        half.startNewSubPath (kScrollCentre + perp * 1000.0f);
        half.lineTo (kScrollCentre - perp * 1000.0f);
        half.lineTo (kScrollCentre - perp * 1000.0f + a * (1000.0f * sgn));
        half.lineTo (kScrollCentre + perp * 1000.0f + a * (1000.0f * sgn));
        half.closeSubPath();
        half.applyTransform (t);
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillPath (half);
    }
    for (int i = 0; i < 12; ++i)
    {
        const auto& kd = kKeyData[i];
        const Key k { kd.c, kd.x + kPad, kd.y + kPad, kd.w, kd.h, kd.r };

        // pressed shade
        if (i == pressedKey_)
        {
            auto p = keyPath (k, 1.0f);
            p.applyTransform (t);
            const auto b = p.getBounds();
            juce::ColourGradient sh (juce::Colours::black.withAlpha (0.05f), b.getCentreX(), b.getY() + b.getHeight() * 0.35f,
                                     juce::Colours::black.withAlpha (0.55f), b.getCentreX(), b.getBottom(), true);
            g.setGradientFill (sh);
            g.fillPath (p);
        }

        // active mode ring (key 0 stays lit while bypassed)
        const bool active = (kd.c >= '1' && kd.c <= '9' && mode_ == kd.c - '0') || (kd.c == '0' && mode_ == 0);
        if (active)
        {
            auto ring = keyPath (k, 1.12f);
            ring.applyTransform (t);
            for (int s = 3; s >= 1; --s)
            {
                g.setColour (juce::Colour (0xff82eba0).withAlpha (0.10f));
                g.strokePath (ring, juce::PathStrokeType (2.0f + (float) s * 3.0f * scale_ * 2.0f));
            }
            g.setColour (juce::Colour (0xff9bebaa).withAlpha (0.92f));
            g.strokePath (ring, juce::PathStrokeType (juce::jmax (1.5f, 2.0f * scale_ * 1.4f)));
        }
    }
}

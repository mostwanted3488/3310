#pragma once
// The "3310" engine: everything between the host and the 8 kHz telephone world.
//
//   host audio -> mono -> anti-alias -> 8 kHz -> TelCore (9 modes) -> reconstruct -> host
//
// Mode 1 is the base effect: a 90s GSM call on a Nokia 3310. Modes 2-9 stay in
// the same digital / radio universe (stutter, AM radio, dying battery, walkie
// talkie, voice scrambler, bad line, bit corruption, vocoder robot).
//
// All modes report exactly the same latency (one 20 ms GSM frame + resampler),
// so switching modes or bypassing never shifts timing against the DAW.

#include "Codec.h"
#include "Primitives.h"

namespace n3310
{
// ---------------------------------------------------------------------------
class TelCore
{
public:
    static constexpr int kNumModes = 9;

    void prepare (uint32_t seed)
    {
        seed_ = seed;
        delay_.allocate (kFrame);
        stutterBuf_.assign (8000, 0.0f);
        wobbleBuf_.assign (128, 0.0f);
        setMode (1);
    }

    void setMode (int m)
    {
        mode_ = m < 1 ? 1 : (m > kNumModes ? kNumModes : m);
        resetState();
        configure();
    }

    int getMode() const { return mode_; }

    float process (float x)
    {
        float v = lp_.process (hp_.process (x));
        float y = 0.0f;

        switch (mode_)
        {
            case 1: y = callMode (v); break;
            case 2: y = stutterMode (v); break;
            case 3: y = delay_.process (amMode (v)); break;
            case 4: y = delay_.process (lowBattMode (v)); break;
            case 5: y = delay_.process (walkieMode (v)); break;
            case 6: y = delay_.process (scrambleMode (v)); break;
            case 7: y = badLineMode (v); break;
            case 8: y = delay_.process (corruptMode (v)); break;
            case 9: y = robotMode (v); break;
            default: break;
        }
        return softClip (y * trim_);
    }

private:
    // ------------------------------------------------------------------ setup
    void resetState()
    {
        rng_.seed (seed_ + (uint32_t) mode_ * 7919u);
        hp_.reset(); lp_.reset(); post_.reset(); hiss_.reset(); noiseBp_.reset();
        agc_.reset(); delay_.reset(); gsm_.reset(); voc_.reset();
        std::fill (frameIn_, frameIn_ + kFrame, 0.0f);
        std::fill (frameOut_, frameOut_ + kFrame, 0.0f);
        fi_ = 0;
        lastGain_ = 1.0f;
        bad_ = false; consecutive_ = 0; burst_ = 0.0f;
        lastRx_ = GsmParams();
        std::fill (stutterBuf_.begin(), stutterBuf_.end(), 0.0f);
        stW_ = 0; stActive_ = false; stMix_ = 0.0f; stCount_ = 4000 + (int) (rng_.next() % 4000);
        std::fill (wobbleBuf_.begin(), wobbleBuf_.end(), 0.0f);
        wobW_ = 0; wobPh_ = 0.0f; sagPh_ = 0.0f; holdPh_ = 0.0f; held_ = 0.0f;
        fadePh_ = 0.0f; whPh_ = 0.0f; crack_ = 0.0f;
        sqEnv_ = 0.0f; gEnv_ = 0.0f; gOpen_ = false; gHold_ = 0; gGain_ = 0.0f;
        up16_.reset(); dn16_.reset(); carPh_ = 0.0f; lfoPh_ = 0.0f;
        glHold_ = 0; glVal_ = 0.0f; sh_ = 0; shVal_ = 0.0f; tick_ = 0.0f;
    }

    void configure()
    {
        const float fs = (float) kTelRate;
        float hpF = 300.0f, lpF = 3400.0f;
        hissDb_ = -200.0f;
        trim_ = 1.0f;
        agcOn_ = false;

        switch (mode_)
        {
            case 1:
                hissDb_ = -66.0f; trim_ = dbToGain (5.5f);
                agc_.set (fs, -30.0f, 2.2f, 4.0f, 140.0f, 3.0f); agcOn_ = true;
                break;
            case 2:
                hissDb_ = -62.0f; trim_ = dbToGain (-1.0f);
                break;
            case 3:
                hpF = 150.0f; lpF = 3500.0f; trim_ = dbToGain (1.0f);
                post_.lowpass (fs, 3500.0, 0.7071);
                break;
            case 4:
                hpF = 250.0f; lpF = 3000.0f; trim_ = dbToGain (0.8f);
                post_.lowpass (fs, 2300.0, 0.7071);
                hissDb_ = -50.0f;
                break;
            case 5:
                hpF = 500.0f; lpF = 2600.0f; trim_ = dbToGain (2.3f);
                agc_.set (fs, -36.0f, 10.0f, 1.5f, 80.0f, 16.0f);
                noiseBp_.bandpass (fs, 2000.0, 0.8);
                hissDb_ = -50.0f;
                break;
            case 6:
                hpF = 300.0f; lpF = 3000.0f; trim_ = dbToGain (0.6f);
                up16_.lowpass (2.0 * fs, 3800.0);
                dn16_.lowpass (2.0 * fs, 3100.0);
                post_.lowpass (fs, 3100.0, 0.7071);
                hissDb_ = -62.0f;
                break;
            case 7:
                hpF = 350.0f; lpF = 3000.0f; trim_ = dbToGain (7.0f);
                agc_.set (fs, -34.0f, 3.0f, 2.0f, 60.0f, 5.0f); agcOn_ = true;
                hissDb_ = -50.0f;
                break;
            case 8:
                hpF = 250.0f; lpF = 3400.0f; trim_ = dbToGain (-0.8f);
                post_.lowpass (fs, 3000.0, 0.7071);
                hissDb_ = -56.0f;
                break;
            case 9:
                hpF = 200.0f; lpF = 3600.0f; trim_ = dbToGain (-3.5f);
                hissDb_ = -64.0f;
                break;
            default: break;
        }
        hp_.highpass (fs, hpF);
        lp_.lowpass (fs, lpF);
        hiss_.lowpass (fs, 3200.0, 0.7071);
        hissGain_ = hissDb_ > -150.0f ? dbToGain (hissDb_) : 0.0f;
    }

    float addHiss (float y) { return y + hiss_.process (rng_.gauss() * hissGain_); }

    // ------------------------------------------------------- frame machinery
    float framePush (float v)
    {
        frameIn_[fi_] = v;
        const float y = frameOut_[fi_];
        if (++fi_ >= kFrame)
        {
            fi_ = 0;
            runFrame();
        }
        return y;
    }

    void matchLevel (bool skip)
    {
        float ri = 1.0e-12f, ro = 1.0e-12f;
        for (int n = 0; n < kFrame; ++n)
        {
            ri += frameIn_[n] * frameIn_[n];
            ro += frameOut_[n] * frameOut_[n];
        }
        float g = lastGain_;
        if (! skip)
            g = (ri > 1.0e-6f * kFrame) ? clampf (std::sqrt (ri / ro), 0.4f, 4.0f) : 1.0f;
        for (int n = 0; n < kFrame; ++n)
        {
            const float t = (float) (n + 1) / (float) kFrame;
            frameOut_[n] *= lastGain_ + (g - lastGain_) * t;
        }
        lastGain_ = g;
    }

    void runFrame()
    {
        GsmLite::Config cfg;
        switch (mode_)
        {
            case 1:
            case 2:
                gsm_.encode (frameIn_, cfg, params_);
                gsm_.decode (params_, frameOut_);
                matchLevel (false);
                break;

            case 7:
            {
                cfg.larCoarse = 2.2f;
                cfg.gainStepsPerOct = 1.5f;
                gsm_.encode (frameIn_, cfg, params_);

                // Gilbert-Elliott channel: bursts of lost frames
                bad_ = bad_ ? (rng_.uni() < 0.62f) : (rng_.uni() < 0.13f);
                GsmParams rx = params_;
                if (bad_)
                {
                    ++consecutive_;
                    rx = lastRx_; // frame substitution: repeat, then fade out
                    const float sc = consecutive_ == 1 ? 0.7f : (consecutive_ == 2 ? 0.45f : 0.0f);
                    for (int s = 0; s < 4; ++s)
                    {
                        rx.bq[s] *= 0.8f;
                        for (int i = 0; i < 13; ++i)
                            rx.pulse[s][i] *= sc;
                    }
                    burst_ = 0.03f;
                }
                else
                {
                    consecutive_ = 0;
                    lastRx_ = params_;
                }
                gsm_.decode (rx, frameOut_);
                matchLevel (bad_);
                break;
            }

            case 9:
                voc_.process (frameIn_, frameOut_, 1.3f, 105.0f, rng_);
                matchLevel (false);
                break;

            default: break;
        }
    }

    // ------------------------------------------------------------------ modes
    // 1 - the base 3310 call: GSM codec, band limited, light AGC and line hiss
    float callMode (float v)
    {
        if (agcOn_) v = agc_.process (v);
        return addHiss (framePush (v));
    }

    // 2 - digital buffer stutter in front of the codec
    float stutterMode (float v)
    {
        const int N = (int) stutterBuf_.size();
        stutterBuf_[(size_t) stW_] = v;

        float loop = 0.0f;
        if (stActive_)
        {
            loop = stutterBuf_[(size_t) ((stStart_ + stPos_) % N)];
            if (++stPos_ >= stGrain_)
            {
                stPos_ = 0;
                if (--stRepeats_ <= 0)
                {
                    stActive_ = false;
                    stCount_ = 2500 + (int) (rng_.next() % 5500);
                }
                else if (stShrink_)
                {
                    stGrain_ = std::max (60, (int) (stGrain_ * 0.82f));
                    stStart_ = ((stW_ - stGrain_ + 1) % N + N) % N;
                }
            }
        }
        else if (--stCount_ <= 0)
        {
            static const int grains[] = { 60, 100, 160, 240, 320, 480, 640 };
            stGrain_ = grains[rng_.next() % 7];
            stStart_ = ((stW_ - stGrain_ + 1) % N + N) % N;
            stPos_ = 0;
            const int target = 640 + (int) (rng_.next() % 1500);
            stRepeats_ = (int) clampf ((float) (target / stGrain_), 3.0f, 24.0f);
            stShrink_ = (rng_.next() % 3) == 0;
            stActive_ = true;
        }
        stW_ = (stW_ + 1) % N;

        stMix_ += ((stActive_ ? 1.0f : 0.0f) - stMix_) * 0.12f;
        const float mixed = v * (1.0f - stMix_) + loop * stMix_;
        return addHiss (framePush (mixed));
    }

    // 3 - AM radio: diode detector, slow fading, static, crackle, heterodyne whistle
    float amMode (float v)
    {
        fadePh_ += 2.0f * kPi * 0.17f / (float) kTelRate;
        if (fadePh_ > 2.0f * kPi) fadePh_ -= 2.0f * kPi;
        const float fade = 1.0f - 0.38f * (0.5f + 0.5f * std::sin (fadePh_));

        const float env = std::max (0.0f, 1.0f + 0.85f * v * fade);
        float det = (env - 1.0f) / 0.85f;
        det += 0.18f * det * std::fabs (det);

        if (rng_.uni() < 0.0005f)
            crack_ = rng_.bi() * 0.3f;
        crack_ *= 0.55f;

        whPh_ += 2.0f * kPi * (1750.0f + 160.0f * std::sin (fadePh_ * 1.7f)) / (float) kTelRate;
        if (whPh_ > 2.0f * kPi) whPh_ -= 2.0f * kPi;

        const float staticLvl = dbToGain (-48.0f) * (1.5f - fade);
        float y = det * fade + rng_.gauss() * staticLvl + crack_ + 0.0035f * std::sin (whPh_);
        return post_.process (y);
    }

    // 4 - dying battery: sample-rate sag, 6-bit companding, wavering pitch, power dips
    float lowBattMode (float v)
    {
        holdPh_ += 3000.0f / (float) kTelRate;
        if (holdPh_ >= 1.0f)
        {
            holdPh_ -= 1.0f;
            held_ = v;
        }
        float q = muLawQuant (held_, 6);

        const int M = (int) wobbleBuf_.size();
        wobbleBuf_[(size_t) wobW_] = q;
        wobPh_ += 2.0f * kPi * 0.35f / (float) kTelRate;
        sagPh_ += 2.0f * kPi * 0.11f / (float) kTelRate;
        if (wobPh_ > 2.0f * kPi) wobPh_ -= 2.0f * kPi;
        if (sagPh_ > 2.0f * kPi) sagPh_ -= 2.0f * kPi;
        const float d = 40.0f + 26.0f * std::sin (wobPh_) + 6.0f * std::sin (wobPh_ * 5.3f);
        const float rp = (float) wobW_ - d;
        int i0 = (int) std::floor (rp);
        const float fr = rp - (float) i0;
        i0 = ((i0 % M) + M) % M;
        const int i1 = (i0 + 1) % M;
        const float w = wobbleBuf_[(size_t) i0] * (1.0f - fr) + wobbleBuf_[(size_t) i1] * fr;
        wobW_ = (wobW_ + 1) % M;

        const float s = std::max (0.0f, std::sin (sagPh_));
        const float sag = 1.0f - 0.65f * std::pow (s, 6.0f);
        return addHiss (post_.process (w * sag));
    }

    // 5 - walkie-talkie: narrow band, brutal compression, squelch gate with noise tail
    float walkieMode (float v)
    {
        float c = agc_.process (v);
        c = std::tanh (c * 2.0f) * 0.62f;

        const float a = std::fabs (v);
        const float ca = a > gEnv_ ? 0.94f : 0.9985f;
        gEnv_ = ca * gEnv_ + (1.0f - ca) * a;

        const bool wasOpen = gOpen_;
        if (gEnv_ > 0.006f)
        {
            gOpen_ = true;
            gHold_ = (int) (0.12f * kTelRate);
        }
        else if (gHold_ > 0)
        {
            --gHold_;
        }
        else
        {
            gOpen_ = false;
        }
        if (wasOpen && ! gOpen_)
            sqEnv_ = 0.22f; // squelch tail burst

        gGain_ += ((gOpen_ ? 1.0f : 0.0f) - gGain_) * 0.04f;
        const float sq = noiseBp_.process (rng_.gauss()) * sqEnv_;
        sqEnv_ *= 0.9987f;

        return c * gGain_ + sq + (gOpen_ ? hiss_.process (rng_.gauss() * hissGain_) : 0.0f);
    }

    // 6 - voice scrambler: spectral inversion by ring modulation (oversampled x2)
    float scrambleMode (float v)
    {
        lfoPh_ += 2.0f * kPi * 0.9f / (float) kTelRate;
        if (lfoPh_ > 2.0f * kPi) lfoPh_ -= 2.0f * kPi;
        const float fc = 3300.0f * (1.0f + 0.0015f * std::sin (lfoPh_));
        const float inc = 2.0f * kPi * fc / (2.0f * (float) kTelRate);

        float out = 0.0f;
        for (int s = 0; s < 2; ++s)
        {
            const float u = up16_.process (s == 0 ? 2.0f * v : 0.0f);
            const float m = u * std::cos (carPh_);
            carPh_ += inc;
            if (carPh_ > 2.0f * kPi) carPh_ -= 2.0f * kPi;
            const float f = dn16_.process (m * 2.0f);
            if (s == 1) out = f;
        }
        out = muLawQuant (out, 7);
        return addHiss (post_.process (out));
    }

    // 7 - bad line: coarse codec, burst packet loss, digital noise bursts
    float badLineMode (float v)
    {
        if (agcOn_) v = agc_.process (v);
        float y = framePush (v);
        burst_ *= 0.996f;
        if (burst_ > 0.0005f)
            y += noiseBp_.process (rng_.gauss()) * burst_;
        if (rng_.uni() < 0.0006f)
            crack_ = rng_.bi() * 0.25f;
        crack_ *= 0.5f;
        return addHiss (y + crack_);
    }

    // 8 - corruption: decimation, 5-bit companding, flipped bits, buffer freezes
    float corruptMode (float v)
    {
        if (glHold_ > 0)
        {
            --glHold_;
            v = glVal_;
        }
        else if (rng_.uni() < 0.0007f)
        {
            glHold_ = 20 + (int) (rng_.next() % 180);
            glVal_ = v;
        }

        if (sh_ == 0)
            shVal_ = v;
        sh_ = (sh_ + 1) % 3;

        uint8_t code = muLawEncode (shVal_);
        code = (uint8_t) ((code & 0x80) | (((code & 0x7F) >> 2) << 2));
        if (rng_.uni() < 0.006f)
        {
            int bit = (int) (rng_.next() % 8);
            if (bit >= 6 && rng_.uni() < 0.75f)
                bit -= 4;
            code = (uint8_t) (code ^ (1 << bit));
        }
        float y = muLawDecode (code);
        if (rng_.uni() < 0.002f)
            tick_ = rng_.bi() * 0.12f;
        y += tick_;
        tick_ = 0.0f;
        return addHiss (post_.process (y));
    }

    // 9 - vocoder robot
    float robotMode (float v) { return addHiss (framePush (v)); }

    // ------------------------------------------------------------------ state
    int mode_ = 1;
    uint32_t seed_ = 1;
    Rng rng_;
    Filter4 hp_, lp_;
    Biquad post_, hiss_, noiseBp_;
    Compressor agc_;
    bool agcOn_ = false;
    float hissDb_ = -200.0f, hissGain_ = 0.0f, trim_ = 1.0f;

    Delay delay_;
    GsmLite gsm_;
    Vocoder voc_;
    GsmParams params_, lastRx_;
    float frameIn_[kFrame] = { 0 }, frameOut_[kFrame] = { 0 };
    int fi_ = 0;
    float lastGain_ = 1.0f;
    bool bad_ = false;
    int consecutive_ = 0;
    float burst_ = 0.0f;

    // stutter
    std::vector<float> stutterBuf_;
    int stW_ = 0, stStart_ = 0, stPos_ = 0, stGrain_ = 160, stRepeats_ = 0, stCount_ = 4000;
    bool stActive_ = false, stShrink_ = false;
    float stMix_ = 0.0f;

    // low battery
    std::vector<float> wobbleBuf_;
    int wobW_ = 0;
    float wobPh_ = 0.0f, sagPh_ = 0.0f, holdPh_ = 0.0f, held_ = 0.0f;

    // AM
    float fadePh_ = 0.0f, whPh_ = 0.0f, crack_ = 0.0f;

    // walkie
    float sqEnv_ = 0.0f, gEnv_ = 0.0f, gGain_ = 0.0f;
    bool gOpen_ = false;
    int gHold_ = 0;

    // scrambler
    Filter4 up16_, dn16_;
    float carPh_ = 0.0f, lfoPh_ = 0.0f;

    // corrupt
    int glHold_ = 0, sh_ = 0;
    float glVal_ = 0.0f, shVal_ = 0.0f, tick_ = 0.0f;
};

// ---------------------------------------------------------------------------
// host rate <-> 8 kHz around a TelCore
class WetChannel
{
public:
    // extra latency of the resampler pair, in 8 kHz samples (measured in tests)
    static constexpr double kExtra8k = 5.8;

    static int latencySamples (double fs)
    {
        return (int) std::lround ((kFrame + kExtra8k) * fs / (double) kTelRate);
    }

    void prepare (double fs, uint32_t seed)
    {
        step_ = (double) kTelRate / fs;
        aa_.lowpass (fs, 3600.0);
        recon_.lowpass (fs, 3600.0);
        core_.prepare (seed);
        reset();
    }

    void reset()
    {
        aa_.reset(); recon_.reset();
        accD_ = 0.0; prevD_ = 0.0f; mu_ = 0.0;
        std::fill (y_, y_ + 4, 0.0f);
        fr_ = fw_ = fc_ = 0;
        pushFifo (0.0f);
        pushFifo (0.0f);
    }

    void setMode (int m) { core_.setMode (m); }
    int getMode() const { return core_.getMode(); }

    float process (float x)
    {
        const float f = aa_.process (x);
        accD_ += step_;
        if (accD_ >= 1.0)
        {
            const float w = (float) (1.0 - (accD_ - 1.0) / step_);
            accD_ -= 1.0;
            pushFifo (core_.process (prevD_ + (f - prevD_) * w));
        }
        prevD_ = f;

        mu_ += step_;
        while (mu_ >= 1.0)
        {
            mu_ -= 1.0;
            y_[0] = y_[1]; y_[1] = y_[2]; y_[2] = y_[3];
            y_[3] = popFifo (y_[3]);
        }
        const float t = (float) mu_;
        const float c1 = 0.5f * (y_[2] - y_[0]);
        const float c2 = y_[0] - 2.5f * y_[1] + 2.0f * y_[2] - 0.5f * y_[3];
        const float c3 = 0.5f * (y_[3] - y_[0]) + 1.5f * (y_[1] - y_[2]);
        const float o = ((c3 * t + c2) * t + c1) * t + y_[1];
        return recon_.process (o);
    }

private:
    void pushFifo (float v)
    {
        if (fc_ >= kFifo) return;
        fifo_[fw_] = v;
        fw_ = (fw_ + 1) % kFifo;
        ++fc_;
    }
    float popFifo (float fallback)
    {
        if (fc_ <= 0) return fallback;
        const float v = fifo_[fr_];
        fr_ = (fr_ + 1) % kFifo;
        --fc_;
        return v;
    }

    static constexpr int kFifo = 32;
    TelCore core_;
    Filter4 aa_, recon_;
    double step_ = 0.18, accD_ = 0.0, mu_ = 0.0;
    float prevD_ = 0.0f, y_[4] = { 0 };
    float fifo_[kFifo] = { 0 };
    int fr_ = 0, fw_ = 0, fc_ = 0;
};

// ---------------------------------------------------------------------------
// Stereo front end: mono wet path, latency-matched dry path, click-free
// bypass and mode switching.
class Engine
{
public:
    void prepare (double fs, uint32_t seed = 3310u)
    {
        fs_ = fs;
        wet_.prepare (fs, seed);
        latency_ = WetChannel::latencySamples (fs);
        dryL_.allocate (latency_);
        dryR_.allocate (latency_);
        rampSamples_ = std::max (1, (int) (0.004 * fs));
        wetCoef_ = 1.0f - std::exp (-1.0f / (float) (0.020 * fs));
        wetAmt_ = bypass_ ? 0.0f : 1.0f;
        modeGain_ = 1.0f;
        activeMode_ = requestedMode_;
        wet_.setMode (activeMode_);
    }

    int latencySamples() const { return latency_; }

    // 0 = bypass, 1..9 = modes
    void setModeIndex (int idx)
    {
        if (idx <= 0)
        {
            bypass_ = true;
        }
        else
        {
            bypass_ = false;
            requestedMode_ = std::min (idx, TelCore::kNumModes);
        }
    }

    void process (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = L[i];
            const float r = R ? R[i] : l;
            const float wet = wet_.process (R ? 0.5f * (l + r) : l);

            if (requestedMode_ != activeMode_)
            {
                modeGain_ -= 1.0f / (float) rampSamples_;
                if (modeGain_ <= 0.0f)
                {
                    modeGain_ = 0.0f;
                    activeMode_ = requestedMode_;
                    wet_.setMode (activeMode_);
                }
            }
            else if (modeGain_ < 1.0f)
            {
                modeGain_ = std::min (1.0f, modeGain_ + 1.0f / (float) rampSamples_);
            }

            wetAmt_ += ((bypass_ ? 0.0f : 1.0f) - wetAmt_) * wetCoef_;
            const float dl = dryL_.process (l);
            const float dr = dryR_.process (r);
            const float w = softClip (wet * modeGain_) * wetAmt_;
            L[i] = dl * (1.0f - wetAmt_) + w;
            if (R)
                R[i] = dr * (1.0f - wetAmt_) + w;
        }
    }

private:
    double fs_ = 44100.0;
    WetChannel wet_;
    Delay dryL_, dryR_;
    int latency_ = 0, rampSamples_ = 176;
    float wetCoef_ = 0.002f, wetAmt_ = 1.0f, modeGain_ = 1.0f;
    bool bypass_ = false;
    int activeMode_ = 1, requestedMode_ = 1;
};

} // namespace n3310

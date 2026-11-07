#pragma once
// Small DSP building blocks used by the 3310 engine. No JUCE dependency so the
// whole engine can be unit-tested offline (see tests/dsp_test.cpp).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace n3310
{
constexpr float kPi = 3.14159265358979323846f;
constexpr int kTelRate = 8000; // telephone-band sample rate
constexpr int kFrame = 160;    // 20 ms @ 8 kHz  (GSM frame)
constexpr int kSub = 40;       // 5 ms subframe

inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float flushDenormal (float v) { return std::fabs (v) < 1.0e-20f ? 0.0f : v; }
inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Linear clean up to 0.8, then a gentle tanh knee towards 1.0
inline float softClip (float y)
{
    const float a = std::fabs (y);
    if (a < 0.8f)
        return y;
    const float s = y < 0.0f ? -1.0f : 1.0f;
    return s * (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f));
}

// ---------------------------------------------------------------------------
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    void seed (uint32_t v) { s = v ? v : 1u; }
    uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); } // [0,1)
    float bi() { return uni() * 2.0f - 1.0f; }
    float gauss() // approx. unit variance
    {
        const float a = uni() + uni() + uni() + uni();
        return (a - 2.0f) * 1.7320508f;
    }
};

// ---------------------------------------------------------------------------
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0.0f; }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = flushDenormal (b1 * x - a1 * y + z2);
        z2 = flushDenormal (b2 * x - a2 * y);
        return y;
    }

    void lowpass (double fs, double fc, double q)
    {
        fc = std::min (fc, fs * 0.45);
        const double w = 2.0 * 3.14159265358979323846 * fc / fs, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
        const double a0 = 1.0 + al;
        b0 = (float) (((1.0 - c) * 0.5) / a0);
        b1 = (float) ((1.0 - c) / a0);
        b2 = b0;
        a1 = (float) ((-2.0 * c) / a0);
        a2 = (float) ((1.0 - al) / a0);
    }

    void highpass (double fs, double fc, double q)
    {
        fc = std::min (fc, fs * 0.45);
        const double w = 2.0 * 3.14159265358979323846 * fc / fs, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
        const double a0 = 1.0 + al;
        b0 = (float) (((1.0 + c) * 0.5) / a0);
        b1 = (float) (-(1.0 + c) / a0);
        b2 = b0;
        a1 = (float) ((-2.0 * c) / a0);
        a2 = (float) ((1.0 - al) / a0);
    }

    void highshelf (double fs, double fc, double gainDb, double q)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * 3.14159265358979323846 * fc / fs, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
        const double sq = 2.0 * std::sqrt (A) * al;
        const double a0 = (A + 1.0) - (A - 1.0) * c + sq;
        b0 = (float) ((A * ((A + 1.0) + (A - 1.0) * c + sq)) / a0);
        b1 = (float) ((-2.0 * A * ((A - 1.0) + (A + 1.0) * c)) / a0);
        b2 = (float) ((A * ((A + 1.0) + (A - 1.0) * c - sq)) / a0);
        a1 = (float) ((2.0 * ((A - 1.0) - (A + 1.0) * c)) / a0);
        a2 = (float) (((A + 1.0) - (A - 1.0) * c - sq) / a0);
    }

    void bandpass (double fs, double fc, double q) // constant 0 dB peak gain
    {
        fc = std::min (fc, fs * 0.45);
        const double w = 2.0 * 3.14159265358979323846 * fc / fs, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
        const double a0 = 1.0 + al;
        b0 = (float) (al / a0);
        b1 = 0.0f;
        b2 = (float) (-al / a0);
        a1 = (float) ((-2.0 * c) / a0);
        a2 = (float) ((1.0 - al) / a0);
    }
};

// 4th-order Butterworth (two cascaded biquads)
struct Filter4
{
    Biquad a, b;
    void reset() { a.reset(); b.reset(); }
    void lowpass (double fs, double fc)
    {
        a.lowpass (fs, fc, 0.5411961);
        b.lowpass (fs, fc, 1.3065630);
    }
    void highpass (double fs, double fc)
    {
        a.highpass (fs, fc, 0.5411961);
        b.highpass (fs, fc, 1.3065630);
    }
    float process (float x) { return b.process (a.process (x)); }
};

// ---------------------------------------------------------------------------
// Feed-forward compressor / AGC working on a smoothed |x| envelope
struct Compressor
{
    float env = 0.0f, thr = 0.05f, ratio = 2.0f, atk = 0.0f, rel = 0.0f, makeup = 1.0f;

    void set (float fs, float thrDb, float ratio_, float atkMs, float relMs, float makeupDb)
    {
        thr = dbToGain (thrDb);
        ratio = ratio_;
        atk = std::exp (-1.0f / (atkMs * 0.001f * fs));
        rel = std::exp (-1.0f / (relMs * 0.001f * fs));
        makeup = dbToGain (makeupDb);
    }
    void reset() { env = 0.0f; }

    float process (float x)
    {
        const float a = std::fabs (x);
        const float c = a > env ? atk : rel;
        env = c * env + (1.0f - c) * a;
        float g = 1.0f;
        if (env > thr)
            g = std::pow (env / thr, 1.0f / ratio - 1.0f);
        return x * g * makeup;
    }
};

// ---------------------------------------------------------------------------
// G.711-style mu-law companding
inline uint8_t muLawEncode (float x)
{
    constexpr float mu = 255.0f;
    const uint8_t sign = x < 0.0f ? 0x80 : 0x00;
    const float a = std::min (std::fabs (x), 1.0f);
    const float c = std::log1p (mu * a) / std::log1p (mu);
    const int idx = (int) std::lround (c * 127.0f);
    return (uint8_t) (sign | (uint8_t) clampf ((float) idx, 0.0f, 127.0f));
}

inline float muLawDecode (uint8_t code)
{
    constexpr float mu = 255.0f;
    const float c = (float) (code & 0x7F) / 127.0f;
    const float e = (std::exp (c * std::log1p (mu)) - 1.0f) / mu;
    return (code & 0x80) ? -e : e;
}

// quantise to 'bits' bits of mu-law resolution (8 = telephone grade)
inline float muLawQuant (float x, int bits)
{
    constexpr float mu = 255.0f;
    const float a = std::min (std::fabs (x), 1.0f);
    float c = std::log1p (mu * a) / std::log1p (mu);
    const float lv = (float) ((1 << (bits - 1)) - 1);
    c = std::round (c * lv) / lv;
    const float e = (std::exp (c * std::log1p (mu)) - 1.0f) / mu;
    return x < 0.0f ? -e : e;
}

// ---------------------------------------------------------------------------
// Fixed delay line (integer)
struct Delay
{
    std::vector<float> buf;
    int pos = 0;
    void allocate (int n)
    {
        buf.assign ((size_t) std::max (1, n), 0.0f);
        pos = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
    // pushes x, returns the sample pushed N samples ago (N = size)
    float process (float x)
    {
        const float y = buf[(size_t) pos];
        buf[(size_t) pos] = x;
        if (++pos >= (int) buf.size())
            pos = 0;
        return y;
    }
};

} // namespace n3310

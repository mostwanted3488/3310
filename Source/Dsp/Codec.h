#pragma once
// A small GSM-06.10-style speech codec (LPC-8 + long term predictor + RPE grid
// excitation) and an LPC vocoder. This is not bit-exact GSM, it is built to
// reproduce the *character* of a 90s mobile call: 20 ms frames, quantised
// log-area ratios, 13-pulse RPE excitation, frame substitution on packet loss.

#include "Primitives.h"

namespace n3310
{
// ---------------------------------------------------------------------------
// LPC helpers (order 8)
inline void lpcFromAutocorr (const float* r, float* kOut)
{
    float a[9] = { 0 }, tmp[9] = { 0 };
    float e = r[0];
    for (int i = 1; i <= 8; ++i)
    {
        float acc = r[i];
        for (int j = 1; j < i; ++j)
            acc -= a[j] * r[i - j];
        float k = e > 1.0e-12f ? acc / e : 0.0f;
        k = clampf (k, -0.995f, 0.995f);
        kOut[i - 1] = k;
        for (int j = 1; j < i; ++j)
            tmp[j] = a[j] - k * a[i - j];
        for (int j = 1; j < i; ++j)
            a[j] = tmp[j];
        a[i] = k;
        e *= (1.0f - k * k);
    }
}

inline void kToA (const float* k, float* a /*8*/)
{
    float cur[9] = { 0 }, t[9] = { 0 };
    for (int m = 1; m <= 8; ++m)
    {
        const float km = k[m - 1];
        for (int j = 1; j < m; ++j)
            t[j] = cur[j] - km * cur[m - j];
        for (int j = 1; j < m; ++j)
            cur[j] = t[j];
        cur[m] = km;
    }
    for (int i = 0; i < 8; ++i)
        a[i] = cur[i + 1];
}

inline float kToLar (float k) { return std::log ((1.0f + k) / (1.0f - k)); }
inline float larToK (float l) { return std::tanh (l * 0.5f); }

// quantised LARs of one 160-sample frame. coarse > 1 = fewer bits.
inline void analyseLpc (const float* in160, float coarse, float* larQ /*8*/)
{
    static const float kStep[8] = { 0.12f, 0.12f, 0.25f, 0.25f, 0.40f, 0.40f, 0.60f, 0.60f };
    float w[kFrame];
    for (int n = 0; n < kFrame; ++n)
        w[n] = in160[n] * (0.54f - 0.46f * std::cos (2.0f * kPi * (float) n / (float) (kFrame - 1)));

    float r[9];
    for (int lag = 0; lag <= 8; ++lag)
    {
        float acc = 0.0f;
        for (int n = lag; n < kFrame; ++n)
            acc += w[n] * w[n - lag];
        // lag window (60 Hz bandwidth expansion) keeps the filter well conditioned
        const float x = 2.0f * kPi * 60.0f / (float) kTelRate * (float) lag;
        r[lag] = acc * std::exp (-0.5f * x * x);
    }
    r[0] = r[0] * 1.0001f + 1.0e-9f;

    float k[8];
    if (r[0] < 1.0e-7f)
        std::fill (k, k + 8, 0.0f);
    else
        lpcFromAutocorr (r, k);

    for (int i = 0; i < 8; ++i)
    {
        const float lar = clampf (kToLar (k[i]), -4.0f, 4.0f);
        const float step = kStep[i] * coarse;
        larQ[i] = std::round (lar / step) * step;
    }
}

// ---------------------------------------------------------------------------
struct GsmParams
{
    float larQ[8] = { 0 };
    int lag[4] = { 40, 40, 40, 40 };
    float bq[4] = { 0 };
    int grid[4] = { 0 };
    float pulse[4][13] = { { 0 } }; // dequantised RPE pulses (already scaled by xmax)
};

struct SynthState
{
    float dp[120] = { 0 }; // reconstructed short-term residual history
    float y[8] = { 0 };    // synthesis filter memory
    void reset()
    {
        std::fill (dp, dp + 120, 0.0f);
        std::fill (y, y + 8, 0.0f);
    }
};

inline void interpA (const float* prevLar, const float* curLar, int sub, float* a)
{
    static const float wPrev[4] = { 0.75f, 0.5f, 0.25f, 0.0f };
    float k[8];
    for (int i = 0; i < 8; ++i)
        k[i] = larToK (wPrev[sub] * prevLar[i] + (1.0f - wPrev[sub]) * curLar[i]);
    kToA (k, a);
}

// Rebuilds one 40-sample subframe (shared by encoder-side reconstruction & decoder)
inline void synthSub (SynthState& st, const float* a, int lag, float bq, int grid, const float* pulses, float* out)
{
    float dq[kSub];
    std::fill (dq, dq + kSub, 0.0f);
    for (int i = 0; i < 13; ++i)
        dq[grid + 3 * i] = pulses[i];
    for (int k = 0; k < kSub; ++k)
        dq[k] += bq * st.dp[120 + k - lag];

    std::memmove (st.dp, st.dp + kSub, 80 * sizeof (float));
    std::memcpy (st.dp + 80, dq, kSub * sizeof (float));

    for (int k = 0; k < kSub; ++k)
    {
        float acc = dq[k];
        for (int i = 0; i < 8; ++i)
            acc += a[i] * st.y[i];
        acc = clampf (acc, -4.0f, 4.0f);
        for (int i = 7; i > 0; --i)
            st.y[i] = st.y[i - 1];
        st.y[0] = flushDenormal (acc);
        out[k] = acc;
    }
}

class GsmLite
{
public:
    struct Config
    {
        float larCoarse = 1.0f;       // LAR quantiser step multiplier
        float gainStepsPerOct = 3.0f; // block-amplitude resolution
        bool ltp = true;
    };

    void reset()
    {
        std::fill (xHist_, xHist_ + 8, 0.0f);
        std::fill (prevLarEnc_, prevLarEnc_ + 8, 0.0f);
        std::fill (prevLarDec_, prevLarDec_ + 8, 0.0f);
        enc_.reset();
        dec_.reset();
    }

    void encode (const float* in, const Config& cfg, GsmParams& p)
    {
        analyseLpc (in, cfg.larCoarse, p.larQ);

        float ext[8 + kFrame];
        std::memcpy (ext, xHist_, 8 * sizeof (float));
        std::memcpy (ext + 8, in, kFrame * sizeof (float));

        for (int s = 0; s < 4; ++s)
        {
            float a[8];
            interpA (prevLarEnc_, p.larQ, s, a);

            // short-term residual
            float d[kSub];
            for (int k = 0; k < kSub; ++k)
            {
                const int n = 8 + s * kSub + k;
                float pred = 0.0f;
                for (int i = 0; i < 8; ++i)
                    pred += a[i] * ext[n - 1 - i];
                d[k] = ext[n] - pred;
            }

            // long-term predictor
            int bestLag = 40;
            float bq = 0.0f;
            if (cfg.ltp)
            {
                float bestCross = 0.0f;
                for (int lag = 40; lag <= 120; ++lag)
                {
                    float cross = 0.0f;
                    for (int k = 0; k < kSub; ++k)
                        cross += d[k] * enc_.dp[120 + k - lag];
                    if (cross > bestCross)
                    {
                        bestCross = cross;
                        bestLag = lag;
                    }
                }
                float en = 1.0e-9f;
                for (int k = 0; k < kSub; ++k)
                    en += enc_.dp[120 + k - bestLag] * enc_.dp[120 + k - bestLag];
                const float b = bestCross / en;
                bq = b < 0.2f ? 0.1f : (b < 0.5f ? 0.35f : (b < 0.8f ? 0.65f : 1.0f));
                if (bestCross <= 0.0f)
                    bq = 0.0f;
            }

            float e[kSub];
            for (int k = 0; k < kSub; ++k)
                e[k] = d[k] - bq * enc_.dp[120 + k - bestLag];

            // RPE grid selection
            int grid = 0;
            float bestE = -1.0f;
            for (int m = 0; m < 4; ++m)
            {
                float en = 0.0f;
                for (int i = 0; i < 13; ++i)
                    en += e[m + 3 * i] * e[m + 3 * i];
                if (en > bestE)
                {
                    bestE = en;
                    grid = m;
                }
            }
            float xmax = 0.0f;
            for (int i = 0; i < 13; ++i)
                xmax = std::max (xmax, std::fabs (e[grid + 3 * i]));

            p.lag[s] = bestLag;
            p.bq[s] = bq;
            p.grid[s] = grid;

            if (xmax < 1.0e-5f)
            {
                std::fill (p.pulse[s], p.pulse[s] + 13, 0.0f);
            }
            else
            {
                const float spo = cfg.gainStepsPerOct;
                const float xq = std::pow (2.0f, std::round (std::log2 (xmax) * spo) / spo);
                for (int i = 0; i < 13; ++i)
                {
                    const float v = e[grid + 3 * i] / xq; // about -1..1
                    int idx = (int) std::floor ((v * 0.5f + 0.5f) * 8.0f);
                    idx = std::max (0, std::min (7, idx));
                    p.pulse[s][i] = (((float) idx + 0.5f) / 8.0f * 2.0f - 1.0f) * xq;
                }
            }

            // keep the encoder-side reconstruction in sync (needed by LTP of next subframe)
            float tmp[kSub];
            synthSub (enc_, a, bestLag, bq, grid, p.pulse[s], tmp);
        }

        std::memcpy (xHist_, in + kFrame - 8, 8 * sizeof (float));
        std::memcpy (prevLarEnc_, p.larQ, 8 * sizeof (float));
    }

    void decode (const GsmParams& p, float* out)
    {
        for (int s = 0; s < 4; ++s)
        {
            float a[8];
            interpA (prevLarDec_, p.larQ, s, a);
            synthSub (dec_, a, p.lag[s], p.bq[s], p.grid[s], p.pulse[s], out + s * kSub);
        }
        std::memcpy (prevLarDec_, p.larQ, 8 * sizeof (float));
    }

private:
    float xHist_[8] = { 0 };
    float prevLarEnc_[8] = { 0 };
    float prevLarDec_[8] = { 0 };
    SynthState enc_, dec_;
};

// ---------------------------------------------------------------------------
// LPC vocoder: LPC-8 filter excited by a synthetic pulse train / noise.
class Vocoder
{
public:
    void reset()
    {
        std::fill (xHist_, xHist_ + 8, 0.0f);
        std::fill (prevLar_, prevLar_ + 8, 0.0f);
        std::fill (y_, y_ + 8, 0.0f);
        countdown_ = 0;
        exLp_ = 0.0f;
        exLp2_ = 0.0f;
    }

    void process (const float* in, float* out, float larCoarse, float pitchHz, Rng& rng)
    {
        float lar[8];
        analyseLpc (in, larCoarse, lar);

        // voicing decision: normalised autocorrelation peak (lags 32..100 = 250..80 Hz)
        float r0 = 1.0e-9f;
        for (int n = 0; n < kFrame; ++n)
            r0 += in[n] * in[n];
        float best = 0.0f;
        for (int lag = 32; lag <= 100; ++lag)
        {
            float c = 0.0f;
            for (int n = lag; n < kFrame; ++n)
                c += in[n] * in[n - lag];
            best = std::max (best, c / r0);
        }
        const bool voiced = best > 0.25f && r0 > 1.0e-5f * (float) kFrame;
        const int period = std::max (20, (int) std::lround ((float) kTelRate / pitchHz));

        float ext[8 + kFrame];
        std::memcpy (ext, xHist_, 8 * sizeof (float));
        std::memcpy (ext + 8, in, kFrame * sizeof (float));

        for (int s = 0; s < 4; ++s)
        {
            float a[8];
            interpA (prevLar_, lar, s, a);

            float e2 = 0.0f;
            for (int k = 0; k < kSub; ++k)
            {
                const int n = 8 + s * kSub + k;
                float pred = 0.0f;
                for (int i = 0; i < 8; ++i)
                    pred += a[i] * ext[n - 1 - i];
                const float d = ext[n] - pred;
                e2 += d * d;
            }
            float g = std::sqrt (e2 / (float) kSub);
            if (g > 1.0e-6f)
                g = std::pow (2.0f, std::round (std::log2 (g) * 2.0f) / 2.0f); // 3 dB steps

            for (int k = 0; k < kSub; ++k)
            {
                float ex;
                if (voiced)
                {
                    ex = 0.0f;
                    if (--countdown_ <= 0)
                    {
                        ex = g * std::sqrt ((float) period);
                        countdown_ = period;
                    }
                    ex += 0.06f * g * rng.gauss();
                }
                else
                {
                    ex = g * rng.gauss();
                }
                exLp_ += 0.40f * (ex - exLp_); // two cascaded poles: wider, less spiky pulses
                exLp2_ += 0.40f * (exLp_ - exLp2_);
                float acc = exLp2_ * 1.6f;
                for (int i = 0; i < 8; ++i)
                    acc += a[i] * y_[i];
                acc = clampf (acc, -4.0f, 4.0f);
                for (int i = 7; i > 0; --i)
                    y_[i] = y_[i - 1];
                y_[0] = flushDenormal (acc);
                out[s * kSub + k] = acc;
            }
        }
        std::memcpy (xHist_, in + kFrame - 8, 8 * sizeof (float));
        std::memcpy (prevLar_, lar, 8 * sizeof (float));
    }

private:
    float xHist_[8] = { 0 };
    float prevLar_[8] = { 0 };
    float y_[8] = { 0 };
    int countdown_ = 0;
    float exLp_ = 0.0f, exLp2_ = 0.0f;
};

} // namespace n3310

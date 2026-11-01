// Offline test harness for the 3310 DSP engine (no JUCE needed).
//   g++ -O2 -std=c++17 -I../Source/Dsp dsp_test.cpp -o dsp_test && ./dsp_test out_dir
// Renders every mode on synthetic speech-like material, writes WAVs and checks
// basic invariants (finite output, no runaway levels, constant latency).

#include "Telephone.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace n3310;

static void writeWav (const std::string& path, const std::vector<float>& x, int fs)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) x.size(), dataBytes = n * 2, riff = 36 + dataBytes, rate = (uint32_t) fs, br = rate * 2;
    const uint16_t fmt = 1, ch = 1, ba = 2, bits = 16;
    const uint32_t sixteen = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&sixteen, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f);
    std::fwrite (&rate, 4, 1, f); std::fwrite (&br, 4, 1, f); std::fwrite (&ba, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    for (float v : x)
    {
        const int16_t s = (int16_t) std::lround (clampf (v, -1.0f, 1.0f) * 32767.0f);
        std::fwrite (&s, 2, 1, f);
    }
    std::fclose (f);
}

// crude but speech-like: glottal pulses -> 3 formants, syllables, fricatives
static std::vector<float> makeSpeech (int fs, float seconds)
{
    const int n = (int) (seconds * (float) fs);
    std::vector<float> x ((size_t) n, 0.0f);
    Rng rng; rng.seed (42);
    static const float vowels[5][3] = { { 730, 1090, 2440 }, { 270, 2290, 3010 }, { 300, 870, 2240 }, { 530, 1840, 2480 }, { 570, 840, 2410 } };
    Biquad f1, f2, f3, fr;
    fr.highpass (fs, 3500.0, 0.7);
    float phase = 0.0f;
    int seg = -1;
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) fs;
        const int s = (int) (t / 0.26f);
        if (s != seg)
        {
            seg = s;
            const float* v = vowels[(s * 3 + 1) % 5];
            f1.bandpass (fs, v[0], 6.0); f2.bandpass (fs, v[1], 8.0); f3.bandpass (fs, v[2], 10.0);
        }
        const float syl = std::max (0.0f, std::sin (2.0f * kPi * 1.9f * t));
        const float f0 = 105.0f + 45.0f * (0.5f + 0.5f * std::sin (2.0f * kPi * 0.4f * t)) + 3.0f * std::sin (2.0f * kPi * 5.5f * t);
        phase += f0 / (float) fs;
        float pulse = 0.0f;
        if (phase >= 1.0f) { phase -= 1.0f; pulse = 1.0f; }
        const float voiced = (f1.process (pulse) * 1.0f + f2.process (pulse) * 0.7f + f3.process (pulse) * 0.4f) * 6.0f;
        const bool fric = ((s % 4) == 3);
        const float fricative = fric ? fr.process (rng.gauss()) * 0.25f : 0.0f;
        x[(size_t) i] = (fric ? fricative : voiced) * syl;
    }
    float rms = 1.0e-9f;
    for (float v : x) rms += v * v;
    rms = std::sqrt (rms / (float) n);
    const float g = 0.1f / rms; // about -20 dBFS RMS
    for (float& v : x) v *= g;
    return x;
}

static double rmsDb (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 1.0e-12;
    for (size_t i = a; i < b && i < x.size(); ++i) s += (double) x[i] * x[i];
    return 10.0 * std::log10 (s / (double) (b - a));
}

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";
    int failures = 0;

    for (int fs : { 44100, 48000, 96000 })
    {
        const auto in = makeSpeech (fs, 6.0f);
        if (fs == 44100) writeWav (outDir + "/in.wav", in, fs);

        for (int mode = 1; mode <= 9; ++mode)
        {
            Engine eng;
            eng.prepare ((double) fs);
            eng.setModeIndex (mode);
            // let the mode-switch ramp settle on the first block
            std::vector<float> out = in, outR = in;
            const int block = 512;
            for (size_t i = 0; i < out.size(); i += (size_t) block)
            {
                const int n = (int) std::min<size_t> ((size_t) block, out.size() - i);
                eng.process (out.data() + i, outR.data() + i, n);
            }
            bool finite = true;
            float peak = 0.0f;
            for (float v : out) { if (! std::isfinite (v)) finite = false; peak = std::max (peak, std::fabs (v)); }
            const double rin = rmsDb (in, (size_t) fs, in.size()), rout = rmsDb (out, (size_t) fs, out.size());
            std::printf ("fs %6d mode %d  in %6.1f dB  out %6.1f dB  peak %.3f  %s\n", fs, mode, rin, rout, peak, finite ? "ok" : "NON-FINITE");
            if (! finite || peak > 1.0001f) ++failures;
            if (fs == 44100) writeWav (outDir + "/mode" + std::to_string (mode) + ".wav", out, fs);
        }

        // latency: band-limited click through the analog-ish AM path
        WetChannel ch;
        ch.prepare ((double) fs, 1u);
        ch.setMode (3);
        std::vector<float> sig ((size_t) fs / 2, 0.0f);
        const int clickAt = fs / 10, len = (int) (0.002 * fs);
        for (int i = 0; i < len; ++i)
            sig[(size_t) (clickAt + i)] = 0.5f * std::sin (2.0f * kPi * 1000.0f * (float) i / (float) fs) * std::sin (kPi * (float) i / (float) len);
        std::vector<float> res (sig.size());
        for (size_t i = 0; i < sig.size(); ++i) res[i] = ch.process (sig[i]);
        // cross-correlation lag
        double best = -1e30; int bestLag = 0;
        const int maxLag = (int) (0.05 * fs);
        for (int lag = 0; lag < maxLag; ++lag)
        {
            double c = 0.0;
            for (int i = 0; i < len + 200; ++i)
                c += (double) sig[(size_t) (clickAt + i)] * (double) res[(size_t) (clickAt + i + lag)];
            if (c > best) { best = c; bestLag = lag; }
        }
        const double expected = WetChannel::latencySamples ((double) fs);
        std::printf ("fs %6d latency measured %d samples (%.2f ms), declared %d samples\n", fs, bestLag, 1000.0 * bestLag / fs, (int) expected);
        if (std::abs (bestLag - expected) > 0.0006 * fs) { std::printf ("  LATENCY MISMATCH\n"); ++failures; }
    }

    std::printf (failures ? "FAILED (%d)\n" : "ALL OK\n", failures);
    return failures ? 1 : 0;
}

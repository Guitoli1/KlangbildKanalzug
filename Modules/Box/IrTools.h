#pragma once

// Aufbereitung der Impulsantworten: Umrechnen auf die Projekt-Samplerate,
// Kürzen, Lautstärke angleichen und zwei IRs zeitlich/phasenrichtig ausrichten.

#include <juce_audio_basics/juce_audio_basics.h>

namespace irtools
{
constexpr double maxSeconds = 0.5;   // Boxen-IRs sind kurz; längere werden gekürzt

// Mono-IR auf die Ziel-Samplerate bringen und auf maxSeconds kürzen (mit kurzer Ausblendung)
inline juce::AudioBuffer<float> prepare (const juce::AudioBuffer<float>& raw, double rawRate, double targetRate)
{
    const int inLen = raw.getNumSamples();
    juce::AudioBuffer<float> mono (1, inLen);
    mono.clear();
    for (int ch = 0; ch < raw.getNumChannels(); ++ch)
        mono.addFrom (0, 0, raw, ch, 0, inLen, 1.0f / (float) raw.getNumChannels());

    juce::AudioBuffer<float> out;
    if (std::abs (rawRate - targetRate) < 0.5)
    {
        out = mono;
    }
    else
    {
        const double ratio = rawRate / targetRate;
        const int outLen = (int) std::ceil (inLen / ratio);
        out.setSize (1, outLen);
        out.clear();
        juce::LagrangeInterpolator interp;
        // Polster am Ende, damit der Interpolator nicht über das Ende hinaus liest
        juce::AudioBuffer<float> padded (1, inLen + 8);
        padded.clear();
        padded.copyFrom (0, 0, mono, 0, 0, inLen);
        interp.process (ratio, padded.getReadPointer (0), out.getWritePointer (0), outLen);
    }

    const int maxLen = (int) (maxSeconds * targetRate);
    if (out.getNumSamples() > maxLen)
    {
        juce::AudioBuffer<float> cut (1, maxLen);
        cut.copyFrom (0, 0, out, 0, 0, maxLen);
        out = cut;
        const int fade = maxLen / 10;
        out.applyGainRamp (0, maxLen - fade, fade, 1.0f, 0.0f);
    }
    return out;
}

// Auf gleiche Energie bringen, damit A und B ähnlich laut sind
inline void normalise (juce::AudioBuffer<float>& ir)
{
    double e = 0;
    const float* d = ir.getReadPointer (0);
    for (int i = 0; i < ir.getNumSamples(); ++i)
        e += (double) d[i] * d[i];
    if (e > 1.0e-12)
        ir.applyGain ((float) (0.5 / std::sqrt (e)));
}

struct Alignment { int lag = 0; bool inverted = false; float correlation = 0; };

// Sucht die Verschiebung von b gegenüber a (in Samples), bei der beide am besten übereinstimmen.
// lag > 0: b kommt später als a.
inline Alignment findAlignment (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int maxLag)
{
    const int win = juce::jmin (2048, juce::jmin (a.getNumSamples(), b.getNumSamples()));
    const float* x = a.getReadPointer (0);
    const float* y = b.getReadPointer (0);
    double ex = 0, ey = 0;
    for (int i = 0; i < win; ++i) { ex += (double) x[i] * x[i]; ey += (double) y[i] * y[i]; }
    Alignment best;
    if (ex <= 0 || ey <= 0)
        return best;

    double bestAbs = -1;
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double c = 0;
        for (int i = 0; i < win; ++i)
        {
            const int j = i + lag;
            if (j >= 0 && j < b.getNumSamples())
                c += (double) x[i] * y[j];
        }
        if (std::abs (c) > bestAbs)
        {
            bestAbs = std::abs (c);
            best.lag = lag;
            best.inverted = c < 0;
            best.correlation = (float) (c / std::sqrt (ex * ey));
        }
    }
    return best;
}

// Entfernt n Samples am Anfang (zieht die IR zeitlich nach vorn)
inline void advance (juce::AudioBuffer<float>& ir, int n)
{
    if (n <= 0 || n >= ir.getNumSamples())
        return;
    juce::AudioBuffer<float> out (1, ir.getNumSamples() - n);
    out.copyFrom (0, 0, ir, 0, n, out.getNumSamples());
    ir = out;
}
} // namespace irtools

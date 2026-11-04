#pragma once

// DSP-Bausteine für Klangbild Raum (ohne JUCE-Abhängigkeit):
// Doubler, Bandecho und Federhall.

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace raum
{
constexpr double twoPi = 6.283185307179586;

//==============================================================================
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int n = 1;
        while (n < maxDelaySamples + 8) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
        maxDelay = (float) (n - 8);
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }

    // Liest das Signal von vor d Samples (d >= 3), Hermite-interpoliert
    float read (float d) const noexcept
    {
        d = std::clamp (d, 3.0f, maxDelay);
        const float rp = (float) w - d;
        const float fl = std::floor (rp);
        const int i = (int) fl;
        const float f = rp - fl;
        const float xm1 = buf[(size_t) ((i - 1) & mask)], x0 = buf[(size_t) (i & mask)];
        const float x1 = buf[(size_t) ((i + 1) & mask)], x2 = buf[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

private:
    std::vector<float> buf;
    int mask = 0, w = 0;
    float maxDelay = 0;
};

struct OnePoleLP
{
    float a = 0, z = 0;
    void setCutoff (double fc, double sr) { a = (float) std::exp (-twoPi * fc / sr); }
    float process (float x) noexcept { z = x + a * (z - x); return z; }
    void reset() { z = 0; }
};

struct OnePoleHP
{
    OnePoleLP lp;
    void setCutoff (double fc, double sr) { lp.setCutoff (fc, sr); }
    float process (float x) noexcept { return x - lp.process (x); }
    void reset() { lp.reset(); }
};

struct Lfo
{
    double phase = 0, inc = 0;
    void set (double hz, double sr, double startPhase = 0) { inc = hz / sr; phase = startPhase; }
    float next() noexcept
    {
        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
        return (float) std::sin (twoPi * phase);
    }
};

//==============================================================================
// Doubler: zwei leicht verzögerte, sanft modulierte Kopien, links/rechts verteilt
class Doubler
{
public:
    void prepare (double sr)
    {
        fs = sr;
        line.prepare ((int) (0.05 * sr));
        l1.set (0.31, sr, 0.0); l2.set (0.53, sr, 0.3);
        r1.set (0.37, sr, 0.6); r2.set (0.71, sr, 0.1);
        toneL.setCutoff (9000.0, sr);
        toneR.setCutoff (6000.0, sr);
        reset();
    }
    void reset() { line.reset(); toneL.reset(); toneR.reset(); }

    // mix, width, depth, enable: 0..1
    void process (float in, float& outL, float& outR, float mix, float width, float depth, float enable) noexcept
    {
        const float modL = 0.6f * l1.next() + 0.4f * l2.next();
        const float modR = 0.6f * r1.next() + 0.4f * r2.next();
        const float modAmt = (float) fs * (0.0003f + 0.0017f * depth);
        const float dL = (float) fs * (0.010f + 0.004f * depth) + modAmt * modL;
        const float dR = (float) fs * (0.015f + 0.006f * depth) + modAmt * modR;

        const float vL = toneL.process (line.read (dL));
        const float vR = toneR.process (line.read (dR));
        line.push (in);

        const float wet = mix * enable * 0.85f;
        const float gMain = 0.5f + 0.5f * width, gCross = 1.0f - gMain;
        const float comp = 1.0f / (1.0f + 0.45f * wet);
        outL = (in + wet * (vL * gMain + vR * gCross)) * comp;
        outR = (in + wet * (vR * gMain + vL * gCross)) * comp;
    }

private:
    double fs = 48000;
    DelayLine line;
    Lfo l1, l2, r1, r2;
    OnePoleLP toneL, toneR;
};

//==============================================================================
// Bandecho: Delay mit Gleichlaufschwankungen, dunkler werdenden Wiederholungen
// und Bandsättigung im Feedback. Zeitänderungen gleiten wie bei echtem Band.
class TapeEcho
{
public:
    static constexpr double maxSeconds = 2.2;

    void prepare (double sr)
    {
        fs = sr;
        lineA.prepare ((int) (maxSeconds * sr) + 64);
        lineB.prepare ((int) (maxSeconds * sr) + 64);
        wow1.set (0.55, sr); wow2.set (0.83, sr, 0.4); flutter.set (6.3, sr, 0.2);
        glide = (float) (1.0 - std::exp (-1.0 / (0.18 * sr)));
        for (auto* h : { &hpA, &hpB }) h->setCutoff (90.0, sr);
        reset();
        setTone (0.5f);
    }

    void reset()
    {
        lineA.reset(); lineB.reset();
        for (auto* f : { &lpA1, &lpA2, &lpB1, &lpB2 }) f->reset();
        hpA.reset(); hpB.reset();
        curDelay = targetDelay;
    }

    void setTimeMs (float ms, bool jump = false)
    {
        targetDelay = std::clamp ((float) (ms * 0.001 * fs), 8.0f, (float) ((maxSeconds - 0.05) * fs));
        if (jump) curDelay = targetDelay;
    }

    // tone 0..1: dunkel..hell
    void setTone (float tone)
    {
        const double fc = 1200.0 * std::pow (8.0, (double) tone);   // 1.2 – 9.6 kHz
        for (auto* f : { &lpA1, &lpA2, &lpB1, &lpB2 }) f->setCutoff (std::min (fc, fs * 0.45), fs);
    }

    void process (float inL, float inR, float& outL, float& outR,
                  float feedback, float wow, float mix, bool pingPong, float inputEnable) noexcept
    {
        curDelay += (targetDelay - curDelay) * glide;
        const float mod = wow * ((float) fs * 0.0022f * (0.7f * wow1.next() + 0.3f * wow2.next())
                                 + (float) fs * 0.00022f * flutter.next());
        const float d = curDelay + mod;
        const float in = 0.5f * (inL + inR) * inputEnable;

        const float yA = shapeA (lineA.read (d));
        if (! pingPong)
        {
            lineA.push (in + feedback * sat (yA));
            lineB.push (0.0f);
            outL = inL + mix * yA;
            outR = inR + mix * yA;
        }
        else
        {
            const float yB = shapeB (lineB.read (d));
            lineA.push (in + feedback * sat (yB));
            lineB.push (feedback * sat (yA));
            outL = inL + mix * yA;
            outR = inR + mix * yB;
        }
    }

private:
    static float sat (float x) noexcept { return std::tanh (x * 1.3f) * (1.0f / 1.3f); }
    float shapeA (float x) noexcept { return lpA2.process (lpA1.process (hpA.process (x))); }
    float shapeB (float x) noexcept { return lpB2.process (lpB1.process (hpB.process (x))); }

    double fs = 48000;
    DelayLine lineA, lineB;
    OnePoleLP lpA1, lpA2, lpB1, lpB2;
    OnePoleHP hpA, hpB;
    Lfo wow1, wow2, flutter;
    float targetDelay = 4800, curDelay = 4800, glide = 0.0001f;
};

//==============================================================================
// Ein "Federtank": Kaskade gestreckter Allpässe (Dispersion -> typisches Zwitschern)
// in einer Rückkopplungsschleife (Laufzeit der Feder), nach Välimäki/Parker/Abel.
class SpringTank
{
public:
    static constexpr int stages = 50;

    void prepare (double sr, double loopSeconds, float allpassCoeff, double modHz)
    {
        fs = sr;
        K = std::max (1, (int) std::lround (sr / (2.0 * 4300.0)));
        a = allpassCoeff;
        apX.assign ((size_t) (stages * K), 0.0f);
        apY.assign ((size_t) (stages * K), 0.0f);
        loopSamples = (float) (loopSeconds * sr);
        loop.prepare ((int) (loopSamples + 0.002 * sr) + 16);
        mod.set (modHz, sr);
        dc.setCutoff (30.0, sr);
        reset();
    }

    void reset()
    {
        std::fill (apX.begin(), apX.end(), 0.0f);
        std::fill (apY.begin(), apY.end(), 0.0f);
        loop.reset(); lp1.reset(); lp2.reset(); dc.reset();
        fb = 0; p = 0;
    }

    double transitionFreq() const { return fs / (2.0 * K); }
    double loopDelaySeconds() const
    {
        // Schleifenverzögerung + mittlere Gruppenlaufzeit der Allpässe
        const double gd = stages * K * 0.5 * ((1 - a) / (1 + a) + (1 + a) / (1 - a));
        return (loopSamples + gd) / fs;
    }

    void setTone (double fc)
    {
        fc = std::min (fc, transitionFreq() * 0.9);
        lp1.setCutoff (fc, fs);
        lp2.setCutoff (fc, fs);
    }

    float process (float x, float g) noexcept
    {
        float v = x + g * fb;
        const size_t base = (size_t) p;
        for (int m = 0; m < stages; ++m)
        {
            const size_t idx = (size_t) (m * K) + base;
            const float y = a * v + apX[idx] - a * apY[idx];
            apX[idx] = v;
            apY[idx] = y;
            v = y;
        }
        if (++p >= K) p = 0;

        v = dc.process (lp2.process (lp1.process (v)));
        loop.push (v);
        fb = loop.read (loopSamples + (float) fs * 0.00025f * mod.next());
        return v;
    }

private:
    double fs = 48000;
    int K = 6, p = 0;
    float a = 0.6f;
    std::vector<float> apX, apY;
    DelayLine loop;
    float loopSamples = 1000, fb = 0;
    OnePoleLP lp1, lp2;
    OnePoleHP dc;
    Lfo mod;
};

class SpringReverb
{
public:
    void prepare (double sr)
    {
        fs = sr;
        tankA.prepare (sr, 0.0365, 0.60f, 0.41);
        tankB.prepare (sr, 0.0412, 0.64f, 0.29);
        tankC.prepare (sr, 0.0297, 0.57f, 0.53);
        inHP.setCutoff (160.0, sr);
        inLP.setCutoff (5000.0, sr);
        outHPL.setCutoff (220.0, sr);
        outHPR.setCutoff (220.0, sr);
        setParams (2.0f, 0.5f);
        reset();
    }
    void reset() { tankA.reset(); tankB.reset(); tankC.reset(); inHP.reset(); inLP.reset(); outHPL.reset(); outHPR.reset(); }

    // decaySeconds: Nachhallzeit (T60), tone 0..1
    void setParams (float decaySeconds, float tone)
    {
        // Faktor gleicht die Verluste der Tiefpässe in der Schleife aus (gemessen)
        const double t = decaySeconds * decayCompensation;
        gA = (float) std::pow (10.0, -3.0 * tankA.loopDelaySeconds() / t);
        gB = (float) std::pow (10.0, -3.0 * tankB.loopDelaySeconds() / t);
        gC = (float) std::pow (10.0, -3.0 * tankC.loopDelaySeconds() / t);
        const double fc = 2000.0 * std::pow (2.5, (double) tone);    // 2 – 5 kHz
        tankA.setTone (fc); tankB.setTone (fc); tankC.setTone (fc);
    }

    // dwell 0..1: wie stark die Feder angetrieben wird
    void process (float inL, float inR, float& outL, float& outR,
                  float dwell, float mix, float inputEnable) noexcept
    {
        const float drive = 1.0f + 5.0f * dwell;
        float x = 0.5f * (inL + inR) * inputEnable;
        x = inLP.process (inHP.process (x));
        x = std::tanh (x * drive) / std::pow (drive, 0.8f);

        const float a = tankA.process (x, gA), b = tankB.process (x, gB), c = tankC.process (x, gC);
        const float wl = outHPL.process (a + 0.6f * c);
        const float wr = outHPR.process (b - 0.6f * c);
        const float wet = mix * wetGain;
        outL = inL + wet * wl;
        outR = inR + wet * wr;
    }

    static constexpr double decayCompensation = 1.7;
    static constexpr float wetGain = 1.3f;

private:
    double fs = 48000;
    SpringTank tankA, tankB, tankC;
    OnePoleHP inHP, outHPL, outHPR;
    OnePoleLP inLP;
    float gA = 0.9f, gB = 0.9f, gC = 0.9f;
};
} // namespace raum

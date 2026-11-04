#pragma once

// Opto-Kompressor nach dem Vorbild klassischer Röhren-Leveler mit Optozelle:
//  - Schwelle über "Peak Reduction", weiches Knie
//  - Compress 3:1, Limit 10:1
//  - Attack ca. 10 ms, Release zweistufig: schnell (~60 ms) bis etwa zur Hälfte,
//    danach langsam (bis ~2,5 s) - je länger komprimiert wurde, desto langsamer ("Gedächtnis")
//  - Emphasis: Höhenanhebung im Regelweg (Höhen lösen stärker aus)

#include <cmath>
#include <algorithm>
#include "../EQ/EQDsp.h"

namespace opto
{
class Compressor
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        aAtt   = coef (0.010);
        aRelF  = coef (0.060);
        aCharge = coef (1.0);
        aRelS  = coef (2.5);
        aDet   = coef (0.005);
        setEmphasis (0.0f);
        reset();
    }

    void reset() { det = rf = rs = 0.0; scState.reset(); }

    // peakReduction 0..100, emphasis 0..1
    void setParams (float peakReduction, bool limitMode)
    {
        threshold = -peakReduction * 0.4;          // 0 .. -40 dBFS
        ratio = limitMode ? 10.0 : 3.0;
        knee  = limitMode ? 4.0 : 12.0;
    }

    void setEmphasis (float amount)
    {
        sc = eq::makeHighShelf (fs, 3000.0, 0.707, amount * 12.0);
    }

    // Erwartet das Regelsignal (Mono-Summe) und liefert die Pegelreduktion in dB (>= 0)
    double process (float sidechain) noexcept
    {
        const double s = std::abs ((double) scState.process (sc, sidechain));
        det = std::max (s, det * aDet);
        const double lvl = 20.0 * std::log10 (det + 1.0e-9);
        const double tgt = staticGr (lvl);

        rf = tgt + (rf - tgt) * (tgt > rf ? aAtt : aRelF);
        rs = rf + (rs - rf) * (rf > rs ? aCharge : aRelS);
        return std::max (rf, 0.5 * rs);
    }

    double staticGr (double lvl) const noexcept
    {
        const double over = lvl - threshold;
        if (over <= -knee * 0.5) return 0.0;
        if (over >= knee * 0.5)  return over * (1.0 - 1.0 / ratio);
        const double k = over + knee * 0.5;
        return (1.0 - 1.0 / ratio) * k * k / (2.0 * knee);
    }

private:
    double coef (double seconds) const { return std::exp (-1.0 / (seconds * fs)); }

    double fs = 48000.0;
    double aAtt = 0, aRelF = 0, aCharge = 0, aRelS = 0, aDet = 0;
    double det = 0, rf = 0, rs = 0;
    double threshold = -20.0, ratio = 3.0, knee = 12.0;
    eq::Coeffs sc;
    eq::BiquadState scState;
};

// Leichte Röhrenfärbung: unsymmetrische Sättigung (gerade Obertöne), Gleichanteil wird entfernt
class Tube
{
public:
    void prepare (double fs) { dcA = (float) std::exp (-2.0 * 3.14159265358979 * 10.0 / fs); x1 = y1 = 0; }
    float process (float x) noexcept
    {
        constexpr float drive = 1.6f, bias = 0.12f;
        const float shaped = (std::tanh (drive * (x + bias)) - std::tanh (drive * bias)) / drive;
        // DC-Blocker
        const float y = shaped - x1 + dcA * y1;
        x1 = shaped;
        y1 = y;
        return y;
    }
private:
    float dcA = 0.999f, x1 = 0, y1 = 0;
};
} // namespace opto

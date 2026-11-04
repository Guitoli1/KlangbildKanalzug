#pragma once

// Reine DSP-Bausteine ohne JUCE-Abhängigkeit:
// Biquad-Koeffizienten (RBJ Audio-EQ-Cookbook), Butterworth-Kaskaden
// für die Cut-Filter und die Berechnung des Frequenzgangs für die Grafik.

#include <array>
#include <algorithm>
#include <cmath>
#include <complex>

namespace eq
{
constexpr int numBands  = 8;
constexpr int maxStages = 4;   // 4 Biquads = 48 dB/Okt.
constexpr double pi = 3.14159265358979323846;

enum class BandType { LowCut, LowShelf, Peak, HighShelf, HighCut };

inline BandType typeForBand (int band)
{
    if (band == 0)            return BandType::LowCut;
    if (band == 1)            return BandType::LowShelf;
    if (band == numBands - 2) return BandType::HighShelf;
    if (band == numBands - 1) return BandType::HighCut;
    return BandType::Peak;
}

inline bool isCut (BandType t) { return t == BandType::LowCut || t == BandType::HighCut; }

struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

struct BandCoeffs
{
    std::array<Coeffs, maxStages> stage {};
    int numStages = 1;
};

struct BiquadState
{
    double s1 = 0, s2 = 0;
    void reset() { s1 = s2 = 0; }

    // Transposed Direct Form II
    inline float process (const Coeffs& c, float xIn) noexcept
    {
        const double x = xIn;
        const double y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }
};

inline Coeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
{
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

inline Coeffs makePeak (double sr, double f, double q, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * pi * f / sr, cw = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q);
    return normalise (1 + alpha * A, -2 * cw, 1 - alpha * A,
                      1 + alpha / A, -2 * cw, 1 - alpha / A);
}

inline Coeffs makeLowShelf (double sr, double f, double q, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * pi * f / sr, cw = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q), sa = 2.0 * std::sqrt (A) * alpha;
    return normalise (A * ((A + 1) - (A - 1) * cw + sa),
                      2 * A * ((A - 1) - (A + 1) * cw),
                      A * ((A + 1) - (A - 1) * cw - sa),
                      (A + 1) + (A - 1) * cw + sa,
                      -2 * ((A - 1) + (A + 1) * cw),
                      (A + 1) + (A - 1) * cw - sa);
}

inline Coeffs makeHighShelf (double sr, double f, double q, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * pi * f / sr, cw = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q), sa = 2.0 * std::sqrt (A) * alpha;
    return normalise (A * ((A + 1) + (A - 1) * cw + sa),
                      -2 * A * ((A - 1) + (A + 1) * cw),
                      A * ((A + 1) + (A - 1) * cw - sa),
                      (A + 1) - (A - 1) * cw + sa,
                      2 * ((A - 1) - (A + 1) * cw),
                      (A + 1) - (A - 1) * cw - sa);
}

inline Coeffs makeHighPass (double sr, double f, double q)
{
    const double w0 = 2.0 * pi * f / sr, cw = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q);
    return normalise ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2,
                      1 + alpha, -2 * cw, 1 - alpha);
}

inline Coeffs makeLowPass (double sr, double f, double q)
{
    const double w0 = 2.0 * pi * f / sr, cw = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q);
    return normalise ((1 - cw) / 2, 1 - cw, (1 - cw) / 2,
                      1 + alpha, -2 * cw, 1 - alpha);
}

// slopeIndex: 0 = 12, 1 = 24, 2 = 36, 3 = 48 dB/Okt.
inline BandCoeffs makeBand (BandType type, double sr, double freq, double gainDb, double q, int slopeIndex)
{
    freq = std::clamp (freq, 10.0, sr * 0.49);
    q    = std::max (q, 0.025);

    BandCoeffs bc;
    switch (type)
    {
        case BandType::Peak:      bc.stage[0] = makePeak      (sr, freq, q, gainDb); bc.numStages = 1; break;
        case BandType::LowShelf:  bc.stage[0] = makeLowShelf  (sr, freq, q, gainDb); bc.numStages = 1; break;
        case BandType::HighShelf: bc.stage[0] = makeHighShelf (sr, freq, q, gainDb); bc.numStages = 1; break;
        case BandType::LowCut:
        case BandType::HighCut:
        {
            // Butterworth-Kaskade; die Güte der resonantesten Stufe
            // wird mit dem Q-Regler skaliert (Q 0.71 = neutral).
            const int n = std::clamp (slopeIndex, 0, maxStages - 1) + 1;
            bc.numStages = n;
            for (int k = 0; k < n; ++k)
            {
                double stageQ = 1.0 / (2.0 * std::cos (pi * (2.0 * k + 1.0) / (4.0 * n)));
                if (k == n - 1)
                    stageQ *= q / 0.70710678;
                bc.stage[(size_t) k] = type == BandType::LowCut ? makeHighPass (sr, freq, stageQ)
                                                                : makeLowPass  (sr, freq, stageQ);
            }
            break;
        }
    }
    return bc;
}

inline double magnitude (const BandCoeffs& bc, double freq, double sr)
{
    const double w = 2.0 * pi * freq / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    double mag = 1.0;
    for (int i = 0; i < bc.numStages; ++i)
    {
        const auto& c = bc.stage[(size_t) i];
        mag *= std::abs (c.b0 + c.b1 * z1 + c.b2 * z2) / std::abs (1.0 + c.a1 * z1 + c.a2 * z2);
    }
    return mag;
}

inline double magnitudeDb (const BandCoeffs& bc, double freq, double sr)
{
    return 20.0 * std::log10 (std::max (magnitude (bc, freq, sr), 1.0e-9));
}
} // namespace eq

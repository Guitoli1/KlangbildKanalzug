#pragma once

// Overdrive vor dem Amp. Zwei Charaktere:
//  GRÜN: klassischer Mitten-Overdrive – Bässe laufen sauber vorbei, nur der Mittenanteil
//        (Hochpass ~720 Hz) wird verstärkt und weich über Dioden begrenzt.
//  KLAR: transparenter Overdrive – sauberes Signal wird mit einem breitbandig verzerrten
//        Anteil gemischt (Germanium-artige, frühere Begrenzung), Höhen per Kuhschwanz.
// Gerechnet wird in "Volt": 0 dBFS ≈ +12 dBu ≈ 4,36 V Spitze (typischer Interface-Eingang).
// Die Verzerrung läuft 4-fach überabgetastet (siehe Processor).

#include <cmath>
#include <algorithm>
#include "../EQ/EQDsp.h"

namespace drive
{
constexpr double voltsPerFullScale = 4.36;

struct OnePole
{
    float a = 0, z = 0;
    void lowpass (double fc, double fs) { a = (float) std::exp (-2.0 * 3.14159265358979 * fc / fs); }
    float lp (float x) noexcept { z = x + a * (z - x); return z; }
    float hp (float x) noexcept { return x - lp (x); }
    void reset() { z = 0; }
};

class Channel
{
public:
    void prepare (double oversampledRate)
    {
        fs = oversampledRate;
        hpGreen.lowpass (720.0, fs);
        hpClear.lowpass (120.0, fs);
        postLp.lowpass (7500.0, fs);
        antiFizz.lowpass (9000.0, fs);
        reset();
    }

    void reset() { hpGreen.reset(); hpClear.reset(); toneLp.reset(); postLp.reset(); antiFizz.reset(); shelfState.reset(); }

    // drive, tone: 0..1
    void setParams (float driveAmount, float tone, bool clearMode)
    {
        clear = clearMode;
        d = driveAmount;
        if (! clear)
        {
            gain = 1.0f + 110.0f * d * d + 10.0f * d;      // ca. 1 … 121 (wie 0 … 41 dB)
            toneLp.lowpass (700.0 * std::pow (7.0, (double) tone), fs);   // 700 Hz … 4,9 kHz
        }
        else
        {
            gain = 1.0f + 60.0f * d * d + 6.0f * d;
            shelf = eq::makeHighShelf (fs, 1500.0, 0.707, (tone - 0.5) * 16.0);   // −8 … +8 dB
        }
    }

    float process (float xIn) noexcept
    {
        const float x = (float) (xIn * voltsPerFullScale);
        float y;
        if (! clear)
        {
            const float v = gain * hpGreen.hp (x);
            y = x + 0.6f * std::tanh (v / 0.6f);            // Dioden im Rückkopplungszweig
            y = toneLp.lp (postLp.lp (y));
        }
        else
        {
            const float v = gain * hpClear.hp (x);
            const float clipped = 0.35f * std::tanh (v / 0.35f);
            y = (1.0f - 0.6f * d) * x + (0.6f + 1.2f * d) * clipped;
            y = shelfState.process (shelf, antiFizz.lp (y));
        }
        return (float) (y / voltsPerFullScale);
    }

private:
    double fs = 192000.0;
    bool clear = false;
    float d = 0.5f, gain = 10.0f;
    OnePole hpGreen, hpClear, toneLp, postLp, antiFizz;
    eq::Coeffs shelf;
    eq::BiquadState shelfState;
};
} // namespace drive

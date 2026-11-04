#pragma once

// Ein "Kanal" mit einem geladenen NAM-Modell.
// Läuft das Modell mit einer anderen Samplerate als Logic, wird automatisch
// hin- und zurückgerechnet (Lanczos-Resampler aus AudioDSPTools).

#include <juce_core/juce_core.h>
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
// Der Resampler stammt aus dem iPlug2-Umfeld und erwartet diese beiden Symbole
namespace iplug { constexpr double PI = 3.14159265358979323846; }
#ifndef DEFAULT_BLOCK_SIZE
 #define DEFAULT_BLOCK_SIZE 512
#endif
#include "ResamplingContainer/ResamplingContainer.h"

class NamSlot
{
public:
    // Lädt eine .nam-Datei. Gibt im Fehlerfall nullptr zurück und füllt error.
    static std::unique_ptr<NamSlot> load (const juce::File& file, double hostRate, int maxBlock, juce::String& error)
    {
        auto slot = std::make_unique<NamSlot>();
        try
        {
            slot->model = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()));
        }
        catch (const std::exception& e)
        {
            error = juce::String (e.what());
            return nullptr;
        }
        catch (...)
        {
            error = "Unbekannter Fehler beim Laden";
            return nullptr;
        }
        if (slot->model == nullptr)
        {
            error = "Datei konnte nicht gelesen werden";
            return nullptr;
        }

        slot->file = file;
        slot->name = file.getFileNameWithoutExtension();
        slot->hasLoudness = slot->model->HasLoudness();
        slot->loudnessDb = slot->hasLoudness ? slot->model->GetLoudness() : 0.0;
        const double expected = slot->model->GetExpectedSampleRate();
        slot->modelRate = expected > 0 ? expected : 48000.0;
        slot->prepare (hostRate, maxBlock);
        return slot;
    }

    void prepare (double hostRate, int maxBlock)
    {
        const bool arbitrary = model->SupportsArbitrarySampleRate();
        const double renderRate = arbitrary ? hostRate : modelRate;
        resample = std::abs (renderRate - hostRate) > 0.5;

        const int maxInner = resample ? (int) std::ceil (maxBlock * renderRate / hostRate) + 64 : maxBlock;
        model->Reset (renderRate, maxInner);

        if (resample)
        {
            resampler = std::make_unique<dsp::ResamplingContainer<double, 1, 12>> (renderRate);
            resampler->Reset (hostRate, maxBlock);
            latency = resampler->GetLatency();
        }
        else
        {
            resampler.reset();
            latency = 0;
        }
    }

    // in/out: Mono, n <= maxBlock
    void process (double* in, double* out, int n)
    {
        if (resample)
        {
            double* ins[1] = { in };
            double* outs[1] = { out };
            resampler->ProcessBlock (ins, outs, n, processFunc);
        }
        else
        {
            double* ins[1] = { in };
            double* outs[1] = { out };
            model->process (ins, outs, n);
        }
    }

    int getLatency() const { return latency; }
    bool isResampling() const { return resample; }

    juce::File file;
    juce::String name;
    bool hasLoudness = false;
    double loudnessDb = 0.0, modelRate = 48000.0;

private:
    std::unique_ptr<nam::DSP> model;
    std::unique_ptr<dsp::ResamplingContainer<double, 1, 12>> resampler;
    std::function<void (double**, double**, int)> processFunc = [this] (double** i, double** o, int n) { model->process (i, o, n); };
    bool resample = false;
    int latency = 0;
};

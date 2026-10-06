// Renders a dry / processed A-B WAV of Low-End Definition on a synthetic bass riff with a muddy low band, so the
// result can be auditioned. Usage: lowend_demo out.wav [contrast] [punch] [sustain]
#include "../core/Definition.h"
#include "../../bass-leveler/tests/synth.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <random>

static void writeWav16 (const char* path, const std::vector<float>& x, double fs)
{
    float peak = 1e-9f;
    for (float v : x)
        peak = std::max (peak, std::fabs (v));
    const float g = 0.89f / peak;
    const uint32_t n = (uint32_t) x.size(), bytes = n * 2, rate = (uint32_t) fs;
    std::ofstream o (path, std::ios::binary);
    auto w32 = [&] (uint32_t v) { o.write ((const char*) &v, 4); };
    auto w16 = [&] (uint16_t v) { o.write ((const char*) &v, 2); };
    o.write ("RIFF", 4); w32 (36 + bytes); o.write ("WAVEfmt ", 8);
    w32 (16); w16 (1); w16 (1); w32 (rate); w32 (rate * 2); w16 (2); w16 (16);
    o.write ("data", 4); w32 (bytes);
    for (float v : x)
        w16 ((uint16_t) (int16_t) std::lround (std::max (-1.0f, std::min (1.0f, v * g)) * 32767.0f));
}

int main (int argc, char** argv)
{
    if (argc < 2)
        return 2;
    const double fs = 48000.0;
    led::Params p;
    p.contrast = argc > 2 ? (float) std::atof (argv[2]) : 0.7f;
    p.punch = argc > 3 ? (float) std::atof (argv[3]) : 0.5f;
    p.sustain = argc > 4 ? (float) std::atof (argv[4]) : 0.3f;

    std::mt19937 g (5);
    std::uniform_real_distribution<double> u (-1.0, 1.0);
    const int riff[] = { 28, 28, 35, 31, 33, 33, 40, 36, 38, 38, 31, 43, 41, 40, 36, 33 };
    std::vector<synth::Ev> ev;
    double t = 0.3;
    for (int rep = 0; rep < 3; ++rep)
        for (int m : riff)
        {
            ev.push_back ({ t, 0.22, m, u (g) * 2.0 });
            t += 0.25;
        }
    const double total = t + 1.0;
    std::array<double, 128> flat {};
    auto dry = synth::render (fs, ev, total, flat);
    // a room-mode style "boom" and a hum-like muddy sine under everything
    for (size_t i = 0; i < dry.size(); ++i)
    {
        const double s = (double) i / fs;
        dry[i] += (float) (0.035 * std::sin (2 * 3.14159265358979 * 63.0 * s) + 0.02 * std::sin (2 * 3.14159265358979 * 97.0 * s + 0.5));
    }

    led::Definition d;
    d.prepare (fs);
    d.setParams (p);
    std::vector<float> wet = dry;
    for (size_t i = 0; i < wet.size(); i += 256)
    {
        float* c[1] = { wet.data() + i };
        d.process (c, 1, (int) std::min<size_t> (256, wet.size() - i));
    }
    const size_t lat = (size_t) d.latencySamples();
    std::vector<float> out (dry.begin(), dry.end()); // dry first (the same delay as the processed, so the levels line up)
    std::vector<float> delayedDry (dry.size(), 0.0f);
    for (size_t i = lat; i < dry.size(); ++i)
        delayedDry[i] = dry[i - lat];
    out = delayedDry;
    out.insert (out.end(), (size_t) (0.6 * fs), 0.0f);
    out.insert (out.end(), wet.begin(), wet.end());
    writeWav16 (argv[1], out, fs);
    std::printf ("wrote %s: %.1f s dry, 0.6 s gap, %.1f s processed (contrast %.2f, punch %.2f, sustain %.2f)\n", argv[1], total, total,
                 p.contrast, p.punch, p.sustain);
    return 0;
}

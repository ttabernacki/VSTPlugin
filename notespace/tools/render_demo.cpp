// Renders a listening demo of Note Space on a synthetic bass line with room boom, mud and rumble:
//   dry | processed | the partials alone (the note) | the residual alone (everything else)
// Usage: notespace_demo out.wav
#include "../core/NoteSpace.h"
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
    std::mt19937 g (5);
    std::uniform_real_distribution<double> u (-1.0, 1.0);
    const int riff[] = { 28, 28, 35, 31, 33, 33, 40, 36, 38, 38, 31, 43, 41, 40, 36, 33 };
    std::vector<synth::Ev> ev;
    double t = 0.3;
    for (int rep = 0; rep < 2; ++rep)
        for (int m : riff)
        {
            ev.push_back ({ t, 0.36, m, u (g) * 2.0 });
            t += 0.4;
        }
    const double total = t + 0.8;
    std::array<double, 128> res {};
    std::mt19937 gr (11);
    for (auto& r : res)
        r = std::uniform_real_distribution<double> (-1.0, 1.0) (gr) * 6.0;
    auto dry = synth::render (fs, ev, total, res);
    std::normal_distribution<double> nd;
    double lp = 0;
    for (size_t i = 0; i < dry.size(); ++i)
    {
        const double s = (double) i / fs;
        lp += 0.004 * (nd (g) - lp);
        dry[i] += (float) (0.04 * std::sin (2 * nsp::kPi * 63.0 * s) + 0.025 * std::sin (2 * nsp::kPi * 97.0 * s + 0.5) + 0.25 * lp);
    }

    nsp::NoteSpace d;
    d.prepare (fs);
    nsp::Params p;
    p.contrast = 0.8f;
    p.toneLock = 0.5f;
    p.repair = 0.3f;
    p.translate = 0.4f;
    d.setParams (p);
    const size_t lat = (size_t) d.latencySamples();
    std::vector<float> wet = dry, parts (dry.size(), 0.0f), resid (dry.size(), 0.0f);
    for (size_t i = 0; i < wet.size(); ++i)
    {
        float* c[1] = { wet.data() + i };
        d.process (c, 1, 1);
        if (i >= lat)
        {
            parts[i - lat] = (float) d.lastPartials();
            resid[i - lat] = (float) d.lastResidual();
        }
    }
    std::vector<float> wetAligned (dry.size(), 0.0f);
    for (size_t i = lat; i < wet.size(); ++i)
        wetAligned[i - lat] = wet[i];
    std::vector<float> out;
    const size_t gap = (size_t) (0.6 * fs);
    for (const auto* part : { &dry, &wetAligned, &parts, &resid })
    {
        out.insert (out.end(), part->begin(), part->end());
        out.insert (out.end(), gap, 0.0f);
    }
    writeWav16 (argv[1], out, fs);
    std::printf ("wrote %s: dry, processed, the partials alone, the residual alone (%.1f s each)\n", argv[1], total);
    return 0;
}

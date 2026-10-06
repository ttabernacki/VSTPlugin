// Renders a before/after WAV of the leveler on a synthetic bass line (resonances injected) so the
// result can be auditioned for artifacts. Usage: bass_demo out.wav [amount]
#include "../core/Leveler.h"
#include "../tests/synth.h"

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
    const float amount = argc > 2 ? (float) std::atof (argv[2]) : 0.8f;
    std::mt19937 g (5);
    std::uniform_real_distribution<double> u (-1.0, 1.0);
    std::array<double, 128> res {};
    std::mt19937 gr (11);
    for (auto& r : res)
        r = std::uniform_real_distribution<double> (-1.0, 1.0) (gr) * 8.0;
    // a repeating riff with a couple of boomy / weak notes
    const int riff[] = { 28, 28, 35, 31, 33, 33, 40, 36, 38, 38, 31, 43, 41, 40, 36, 33 };
    std::vector<synth::Ev> ev;
    double t = 0.3;
    for (int rep = 0; rep < 3; ++rep)
        for (int m : riff)
        {
            ev.push_back ({ t, 0.20, m, u (g) * 2.0 });
            t += 0.25;
        }
    const double total = t + 1.0;
    const auto dry = synth::render (fs, ev, total, res);

    bnl::Leveler l;
    l.prepare (fs);
    bnl::Params p;
    p.amount = amount;
    l.setParams (p);
    std::vector<float> wet = dry;
    for (size_t i = 0; i < wet.size(); i += 256)
    {
        float* ch[1] = { wet.data() + i };
        l.process (ch, 1, (int) std::min<size_t> (256, wet.size() - i));
    }
    // aligned: dry (delayed by the latency) then leveled, with a gap in between
    std::vector<float> out;
    const size_t lat = (size_t) l.latencySamples();
    for (size_t i = lat; i < dry.size(); ++i)
        out.push_back (dry[i - lat]);
    out.insert (out.end(), (size_t) (0.8 * fs), 0.0f);
    for (size_t i = lat; i < wet.size(); ++i)
        out.push_back (wet[i]);
    writeWav16 (argv[1], out, fs);
    std::printf ("wrote %s: dry riff, then leveled (amount %.2f)\n", argv[1], amount);
    return 0;
}

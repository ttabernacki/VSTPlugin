// Scrape Cello demo: renders WAV files that show the bow's pressure, position and speed.
// Usage: scrapecello_demo <output folder>
#include "../core/ScrapeCello.h"

#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace sc;

static void writeWav16 (const std::string& path, const std::vector<float>& x, double fs)
{
    float peak = 1e-9f;
    for (float v : x)
        peak = std::max (peak, std::fabs (v));
    const float g = 0.7f / peak; // -3 dBFS peak (each file on its own; the dynamics within it are kept)
    const uint32_t n = (uint32_t) x.size(), bytes = n * 2, rate = (uint32_t) fs;
    std::ofstream o (path, std::ios::binary);
    auto w32 = [&] (uint32_t v) { o.write ((const char*) &v, 4); };
    auto w16 = [&] (uint16_t v) { o.write ((const char*) &v, 2); };
    o.write ("RIFF", 4); w32 (36 + bytes); o.write ("WAVEfmt ", 8);
    w32 (16); w16 (1); w16 (1); w32 (rate); w32 (rate * 2); w16 (2); w16 (16);
    o.write ("data", 4); w32 (bytes);
    for (float v : x)
        w16 ((uint16_t) (int16_t) std::lround (std::max (-1.0f, std::min (1.0f, v * g)) * 32767.0f));
    std::printf ("wrote %s (%.1f s)\n", path.c_str(), x.size() / fs);
}

// a script: MIDI events at times, and curves for aftertouch (channel 1), the mod wheel and the pedal
struct Script
{
    struct E { double t; std::vector<uint8_t> m; };
    std::vector<E> ev;
    std::function<double (double)> pressure, mod, pedal;
    void on (double t, int n, double vel = 0.75, int ch = 1) { ev.push_back ({ t, { (uint8_t) (0x90 | (ch - 1)), (uint8_t) n, (uint8_t) (vel * 127) } }); }
    void off (double t, int n, int ch = 1) { ev.push_back ({ t, { (uint8_t) (0x80 | (ch - 1)), (uint8_t) n, 0 } }); }
    void note (double t0, double t1, int n, double vel = 0.75) { on (t0, n, vel); off (t1, n); }
    void raw (double t, std::vector<uint8_t> m) { ev.push_back ({ t, std::move (m) }); }
    std::vector<float> render (double seconds, CelloParams p)
    {
        const double fs = 48000.0;
        auto c = std::make_unique<ScrapeCello>();
        c->prepare (fs);
        c->setParams (p);
        std::stable_sort (ev.begin(), ev.end(), [] (const E& a, const E& b) { return a.t < b.t; });
        std::vector<float> out ((size_t) (seconds * fs));
        size_t k = 0;
        const int block = 64;
        for (size_t pos = 0; pos < out.size(); pos += block)
        {
            const double t = pos / fs;
            while (k < ev.size() && ev[k].t <= t)
            {
                c->midi (ev[k].m.data(), (int) ev[k].m.size());
                ++k;
            }
            if (pressure) { uint8_t m[2] = { 0xd0, (uint8_t) std::clamp ((int) std::lround (pressure (t) * 127), 0, 127) }; c->midi (m, 2); }
            if (mod) { uint8_t m[3] = { 0xb0, 1, (uint8_t) std::clamp ((int) std::lround (mod (t) * 127), 0, 127) }; c->midi (m, 3); }
            if (pedal) { uint8_t m[3] = { 0xb0, 11, (uint8_t) std::clamp ((int) std::lround (pedal (t) * 127), 0, 127) }; c->midi (m, 3); }
            c->process (out.data() + pos, (int) std::min<size_t> (block, out.size() - pos));
        }
        return out;
    }
};

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: scrapecello_demo <output folder>\n");
        return 2;
    }
    const std::string dir = std::string (argv[1]) + "/";

    { // 01: a clean phrase, legato (mono, glides), vibrato coming in on long notes
        Script s;
        CelloParams p;
        p.mono = true;
        const int mel[] = { 50, 55, 57, 59, 62, 59, 57, 55, 54, 55 };
        const double len[] = { 1.2, 0.8, 0.8, 1.6, 2.0, 0.8, 0.8, 0.8, 0.8, 3.0 };
        double t = 0.2;
        for (int i = 0; i < 10; ++i)
        {
            s.on (t, mel[i], 0.6);
            if (i > 0) s.off (t + 0.03, mel[i - 1]);
            t += len[i];
        }
        s.off (t, mel[9]);
        writeWav16 (dir + "01_clean_legato_phrase.wav", s.render (t + 1.5, p), 48000.0);
    }
    { // 02: pressure: a held D3 as aftertouch rises from a whisper through the gritty edge to a full grind, and back
        Script s;
        s.note (0.2, 13.5, 50, 0.6);
        s.pressure = [] (double t) { return t < 7.0 ? std::clamp ((t - 0.5) / 6.5, 0.0, 1.0) : std::clamp (1.0 - (t - 7.0) / 5.5, 0.0, 1.0); };
        CelloParams p;
        p.pressure = 0.15;
        writeWav16 (dir + "02_pressure_whisper_to_grind.wav", s.render (14.5, p), 48000.0);
    }
    { // 03: position: G2 bowed from over the fingerboard (soft, flutey) to right at the bridge (glassy) and back
        Script s;
        s.note (0.2, 11.5, 43, 0.6);
        s.mod = [] (double t) { return t < 6.0 ? std::clamp (t / 5.5, 0.0, 1.0) : std::clamp (1.0 - (t - 6.0) / 5.0, 0.0, 1.0); };
        CelloParams p;
        p.position = 0.0;
        p.pressure = 0.5;
        writeWav16 (dir + "03_position_tasto_to_ponticello.wav", s.render (12.5, p), 48000.0);
    }
    { // 04: a slow bow, pressed: creaks and crackles (the pedal brings the speed down, then up again)
        Script s;
        s.note (0.2, 11.0, 45, 0.5);
        s.pedal = [] (double t) { return t < 5.0 ? 0.9 - 0.8 * std::clamp (t / 4.0, 0.0, 1.0) : std::clamp (0.1 + 0.8 * (t - 7.0) / 3.0, 0.1, 0.9); };
        s.pressure = [] (double) { return 0.35; };
        CelloParams p;
        p.pressure = 0.4;
        writeWav16 (dir + "04_slow_bow_creak.wav", s.render (12.0, p), 48000.0);
    }
    { // 05: a drone: C2 G2 D3 bowed together, swelling into grinds and easing back, the bow drifting towards the bridge
        Script s;
        for (int n : { 36, 43, 50 })
            s.note (0.2, 15.0, n, 0.5);
        s.pressure = [] (double t) { return 0.15 + 0.85 * std::pow (0.5 - 0.5 * std::cos (2 * kPi * t / 7.0), 2.0); };
        s.mod = [] (double t) { return 0.2 + 0.6 * (0.5 - 0.5 * std::cos (2 * kPi * t / 11.0)); };
        CelloParams p;
        p.pressure = 0.2;
        writeWav16 (dir + "05_drone_swelling_grinds.wav", s.render (16.0, p), 48000.0);
    }
    { // 06: MPE: two notes on their own channels; one bends up a minor third and grinds while the other stays clean
        Script s;
        s.on (0.2, 45, 0.6, 2); // A2 on channel 2
        s.on (0.2, 52, 0.6, 3); // E3 on channel 3
        for (double t = 2.0; t < 6.0; t += 0.02)
        {
            const double x = std::clamp ((t - 2.0) / 2.0, 0.0, 1.0);
            const int bend = 8192 + (int) std::lround (8191.0 * 3.0 / 48.0 * x);
            s.raw (t, { 0xe2, (uint8_t) (bend & 0x7f), (uint8_t) (bend >> 7) });                // channel 3 bends up 3
            s.raw (t, { 0xd1, (uint8_t) std::lround (127 * std::clamp ((t - 3.0) / 2.0, 0.0, 1.0)) }); // channel 2 presses in
        }
        s.off (8.0, 45, 2);
        s.off (8.0, 52, 3);
        CelloParams p;
        writeWav16 (dir + "06_mpe_bend_and_grind.wav", s.render (9.0, p), 48000.0);
    }
    { // 07: separate strokes, each a bow change: hard keys bite, soft ones swell in
        Script s;
        const int notes[] = { 43, 43, 50, 50, 47, 45, 43, 38 };
        const double vel[] = { 0.95, 0.3, 0.9, 0.25, 0.7, 0.5, 0.95, 0.4 };
        double t = 0.2;
        for (int i = 0; i < 8; ++i)
        {
            s.note (t, t + 0.75, notes[i], vel[i]);
            t += 0.95;
        }
        CelloParams p;
        p.pressure = 0.5;
        writeWav16 (dir + "07_strokes_bite_and_swell.wav", s.render (t + 1.5, p), 48000.0);
    }
    return 0;
}

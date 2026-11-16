// Pump organ demo: renders WAV files that show the reeds, the wind and the stops.
// Usage: pumporgan_demo <output folder>
#include "../core/ReedOrgan.h"

#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <string>

using namespace ro;

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

// a script of events at times, and a pedal curve
struct Script
{
    struct E { double t; std::function<void (ReedOrgan&)> f; };
    std::vector<E> ev;
    std::function<double (double)> pedal = [] (double) { return 0.8; };
    void on (double t, int n) { ev.push_back ({ t, [=] (ReedOrgan& o) { o.noteOn (n, 0.8); } }); }
    void off (double t, int n) { ev.push_back ({ t, [=] (ReedOrgan& o) { o.noteOff (n); } }); }
    void chord (double t0, double t1, std::initializer_list<int> notes)
    {
        for (int n : notes) { on (t0, n); off (t1, n); }
    }
    void params (double t, OrganParams p) { ev.push_back ({ t, [=] (ReedOrgan& o) { o.setParams (p); } }); }
    std::vector<float> render (double seconds, OrganParams p)
    {
        const double fs = 48000.0;
        auto o = std::make_unique<ReedOrgan>();
        o->prepare (fs);
        o->setParams (p);
        std::stable_sort (ev.begin(), ev.end(), [] (const E& a, const E& b) { return a.t < b.t; });
        std::vector<float> out ((size_t) (seconds * fs));
        size_t k = 0;
        const int block = 64;
        for (size_t pos = 0; pos < out.size(); pos += block)
        {
            const double t = pos / fs;
            while (k < ev.size() && ev[k].t <= t)
                ev[k++].f (*o);
            o->pedal (pedal (t));
            o->process (out.data() + pos, (int) std::min<size_t> (block, out.size() - pos));
        }
        return out;
    }
};

static OrganParams stops (std::initializer_list<int> on, Wind w = Wind::Easy)
{
    OrganParams p;
    p.wind = w;
    p.stops.fill (false);
    for (int s : on)
        p.stops[(size_t) s] = true;
    return p;
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: pumporgan_demo <output folder>\n");
        return 2;
    }
    const std::string dir = std::string (argv[1]) + "/";

    { // 01: the Diapason up the compass, C2 to C6, steady wind: listen to the bass reeds speak slowly
        Script s;
        double t = 0.2;
        for (int n = 36; n <= 84; n += 12)
        {
            s.chord (t, t + 1.6, { n });
            t += 2.0;
        }
        writeWav16 (dir + "01_reeds_up_the_compass.wav", s.render (t + 0.5, stops ({ Diapason })), 48000.0);
    }
    { // 02: the wind: a held C major chord, the pedal from barely-there to full and back
        Script s;
        s.chord (0.3, 11.5, { 48, 55, 60, 64 });
        s.pedal = [] (double t) {
            if (t < 0.3) return 0.15;
            if (t < 6.0) return 0.15 + 0.85 * (t - 0.3) / 5.7;
            return std::max (0.1, 1.0 - 0.9 * (t - 6.0) / 5.5);
        };
        writeWav16 (dir + "02_wind_swell.wav", s.render (12.5, stops ({ Diapason, Melodia })), 48000.0);
    }
    { // 03: a hymn in Easy mode: the pedal breathing with each phrase
        Script s;
        const std::vector<std::vector<int>> chords = { { 48, 55, 64, 72 }, { 53, 57, 65, 72 }, { 48, 55, 64, 67 }, { 43, 55, 62, 71 },
                                                       { 48, 55, 64, 72 }, { 45, 57, 64, 69 }, { 50, 57, 65, 69 }, { 43, 55, 62, 67 },
                                                       { 48, 55, 60, 64 } };
        double t = 0.3;
        for (size_t i = 0; i < chords.size(); ++i)
        {
            const double len = i == 3 || i == 7 ? 2.4 : i + 1 == chords.size() ? 3.5 : 1.2;
            for (int n : chords[i])
            {
                s.on (t, n);
                s.off (t + len - 0.04, n);
            }
            t += len;
        }
        s.pedal = [] (double t) { // a swell into the middle of each 4-chord phrase, eased at the cadence
            const double ph = std::fmod (t, 6.0) / 6.0;
            return 0.45 + 0.4 * std::sin (kPi * ph) - (t > 12.0 ? 0.15 * (t - 12.0) / 3.0 : 0.0);
        };
        writeWav16 (dir + "03_hymn_easy_wind.wav", s.render (t + 0.8, stops ({ Diapason, Melodia, SubBass })), 48000.0);
    }
    { // 04: the stops, one chord each: Dulciana, Melodia, Diapason, + Celeste, + Sub Bass, + Flute 4', + Vox Humana, + Forte
        Script s;
        const std::vector<OrganParams> ps = [] {
            std::vector<OrganParams> v;
            v.push_back (stops ({ Dulciana }));
            v.push_back (stops ({ Melodia }));
            v.push_back (stops ({ Diapason }));
            v.push_back (stops ({ Dulciana, Celeste }));
            v.push_back (stops ({ Diapason, Melodia, SubBass }));
            v.push_back (stops ({ Diapason, Melodia, SubBass, Flute }));
            auto vox = stops ({ Melodia, Dulciana, Celeste });
            vox.voxHumana = true;
            v.push_back (vox);
            auto full = stops ({ Diapason, Melodia, Dulciana, Celeste, SubBass, Flute });
            full.forte = true;
            v.push_back (full);
            return v;
        }();
        double t = 0.2;
        for (const auto& p : ps)
        {
            s.params (t - 0.1, p);
            s.chord (t, t + 2.6, { 41, 53, 57, 60, 65 });
            t += 3.2;
        }
        writeWav16 (dir + "04_stops_tour.wav", s.render (t + 0.5, stops ({ Dulciana })), 48000.0);
    }
    { // 05: Reservoir: pump to fill it, play, keep pumping, then stop pumping: the wind runs out
        Script s;
        s.chord (2.0, 13.0, { 45, 52, 57, 60, 64 });
        s.pedal = [] (double t) {
            if (t > 8.5) return 0.5; // feet still
            return 0.5 + 0.48 * std::sin (2 * kPi * 0.9 * t); // steady treadling, ~1 stroke pair a second
        };
        writeWav16 (dir + "05_reservoir_pump_then_stop.wav", s.render (14.0, stops ({ Diapason, Melodia }, Wind::Reservoir)), 48000.0);
    }
    { // 06: Expression: the strokes are the wind: slow strokes, then quick, then a long push
        Script s;
        s.chord (0.3, 11.5, { 50, 57, 62, 65 });
        s.pedal = [] (double t) {
            if (t < 4.0) return 0.5 + 0.45 * std::sin (2 * kPi * 0.5 * t);
            if (t < 8.0) return 0.5 + 0.3 * std::sin (2 * kPi * 1.4 * t);
            return std::min (1.0, 0.05 + 0.95 * (t - 8.0) / 3.0);
        };
        writeWav16 (dir + "06_expression_strokes.wav", s.render (12.0, stops ({ Diapason, Melodia }, Wind::Expression)), 48000.0);
    }
    { // 07: ambient: Dulciana + Celeste, a slow cluster that breathes, Vox Humana at the end
        Script s;
        s.chord (0.3, 14.0, { 52, 59, 64, 66, 71 });
        s.chord (7.0, 14.0, { 76 });
        auto p = stops ({ Dulciana, Celeste, Flute });
        s.pedal = [] (double t) { return 0.25 + 0.35 * (0.5 - 0.5 * std::cos (2 * kPi * t / 7.0)); };
        auto pv = p;
        pv.voxHumana = true;
        pv.voxRate = 4.6;
        s.params (9.5, pv);
        writeWav16 (dir + "07_ambient_celeste.wav", s.render (15.5, p), 48000.0);
    }
    { // 08: the wind under load: one note, then a full chord on all stops: the single note sags as the chord comes in
        Script s;
        s.chord (0.3, 7.0, { 72 });
        s.chord (2.5, 6.0, { 36, 48, 52, 55, 60, 64, 67 });
        auto p = stops ({ Diapason, Melodia, Dulciana, SubBass, Flute });
        s.pedal = [] (double) { return 0.55; };
        writeWav16 (dir + "08_wind_sag_under_a_big_chord.wav", s.render (7.6, p), 48000.0);
    }
    { // 09: the treble with the higher stops (where the phasing was): Diapason + Flute 4' + Celeste, a melody over a held chord
        Script s;
        s.chord (0.3, 10.5, { 60, 64, 67 });
        const int mel[] = { 76, 79, 81, 79, 84, 83, 79, 76 };
        double t = 0.6;
        for (int n : mel)
        {
            s.chord (t, t + 1.15, { n });
            t += 1.2;
        }
        s.pedal = [] (double t) { return 0.55 + 0.2 * std::sin (2 * kPi * t / 9.0); };
        writeWav16 (dir + "09_treble_higher_stops.wav", s.render (11.5, stops ({ Diapason, Dulciana, Celeste, Flute })), 48000.0);
    }
    { // 10: worn vs new: the same hymn phrase with Tuning age 1 (worn in), then 0 (factory fresh)
        Script s;
        const std::vector<std::vector<int>> chords = { { 48, 55, 64, 72 }, { 53, 57, 65, 72 }, { 43, 55, 62, 71 }, { 48, 55, 64, 72 } };
        auto worn = stops ({ Diapason, Melodia, Dulciana });
        auto fresh = worn;
        fresh.tuningAge = 0.0;
        double t = 0.3;
        for (int pass = 0; pass < 2; ++pass)
        {
            s.params (t - 0.2, pass == 0 ? worn : fresh);
            for (const auto& c : chords)
            {
                for (int n : c) { s.on (t, n); s.off (t + 1.75, n); }
                t += 1.8;
            }
            t += 0.8;
        }
        s.pedal = [] (double) { return 0.6; };
        writeWav16 (dir + "10_worn_then_new.wav", s.render (t + 0.5, worn), 48000.0);
    }
    return 0;
}

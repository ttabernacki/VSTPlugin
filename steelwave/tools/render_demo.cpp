// Renders listening demos of the Steelwave string model (dry, DI-style, mono).
// Usage: steelwave_demo <output folder>
//   01_picks.wav      one string, soft to hard, then a single-note line across strings
//   02_ab_pedals.wav  E chord, A+B pedals into A (the I -> IV move), with volume swells
//   03_bar_slides.wav wound-string slide with bar noise, then a legato chord slide
//   04_swells.wav     picked behind the volume pedal and swelled in, ambient style
//   05_vibrato.wav    held chord with bar vibrato, then a hand block
#include "../core/SteelString.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>

using namespace sw;

static void writeWav16 (const std::string& path, const std::vector<float>& x, double fs)
{
    float peak = 1e-9f;
    for (float v : x)
        peak = std::max (peak, std::fabs (v));
    const float g = 0.7f / peak; // -3 dBFS peak
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

// A tiny instrument for scripting: 10 E9 strings, pickup, volume pedal.
struct Steel
{
    static constexpr double fs = 48000.0;
    std::array<SteelString, 10> s;
    Pickup pickup;
    Glide vol; // volume pedal 0..1, audio taper applied on output
    struct Ev { double t; std::function<void (Steel&)> f; };
    std::vector<Ev> ev;

    Steel()
    {
        const auto spec = e9Strings();
        for (int i = 0; i < 10; ++i)
            s[(size_t) i].prepare (fs, spec[(size_t) i]);
        pickup.prepare (fs);
        vol.setTime (0.05);
        vol.jump (1.0);
    }
    SteelString& str (int n) { return s[(size_t) (n - 1)]; } // 1-based, like players number them
    void at (double t, std::function<void (Steel&)> f) { ev.push_back ({ t, std::move (f) }); }
    void pick (double t, std::initializer_list<int> strs, double vel, double spread = 0.012)
    {
        double dt = 0;
        for (int n : strs)
        {
            at (t + dt, [n, vel] (Steel& st) { st.str (n).pluck (vel); });
            dt += spread;
        }
    }
    void bar (double t, double fret, double glide)
    {
        at (t, [fret, glide] (Steel& st) { for (auto& x : st.s) x.setBar (fret, glide); });
    }
    void pedal (double t, std::initializer_list<std::pair<int, double>> moves, double glide)
    {
        std::vector<std::pair<int, double>> m (moves);
        at (t, [m, glide] (Steel& st) { for (auto& p : m) st.str (p.first).setPedal (p.second, glide); });
    }
    void swell (double t, double level, double time)
    {
        at (t, [level, time] (Steel& st) { st.vol.setTime (time); st.vol.target = level; });
    }
    std::vector<float> render (double seconds, const std::function<void (Steel&, double)>& perSample = nullptr)
    {
        std::sort (ev.begin(), ev.end(), [] (const Ev& a, const Ev& b) { return a.t < b.t; });
        std::vector<float> out ((size_t) (seconds * fs));
        size_t e = 0;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const double t = (double) i / fs;
            while (e < ev.size() && ev[e].t <= t)
                ev[e++].f (*this);
            if (perSample)
                perSample (*this, t);
            double sum = 0;
            for (auto& x : s)
                sum += x.process();
            const double v = std::clamp (vol.step (1.0 / fs), 0.0, 1.0);
            out[i] = pickup.process ((float) sum) * (float) (v * v * v); // volume pedals are strongly tapered
        }
        return out;
    }
};

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: steelwave_demo <output folder>\n");
        return 2;
    }
    const std::string dir = std::string (argv[1]) + "/";

    { // 01: one string soft -> hard, then a line
        Steel st;
        double t = 0.2;
        for (double v : { 0.2, 0.45, 0.7, 1.0 })
        {
            st.pick (t, { 4 }, v);
            t += 1.3;
        }
        // single-note line in E: (string, fret) pairs, legato-free
        const int line[][2] = { { 5, 0 }, { 4, 0 }, { 3, 0 }, { 1, 0 }, { 3, 0 }, { 4, 0 }, { 6, 0 }, { 5, 0 },
                                { 4, 2 }, { 3, 2 }, { 4, 4 }, { 3, 4 }, { 4, 5 }, { 3, 5 } };
        t += 0.4;
        for (auto& nf : line)
        {
            const int n = nf[0], fr = nf[1];
            st.at (t - 0.01, [n] (Steel& s) { for (int k = 1; k <= 10; ++k) if (k != n) s.str (k).setDamp (0.6); });
            st.at (t, [n, fr] (Steel& s) { s.str (n).setBar (fr, 0.03); s.str (n).setDamp (0); s.str (n).pluck (0.7); });
            t += 0.32;
        }
        writeWav16 (dir + "01_picks.wav", st.render (t + 3.0), Steel::fs);
    }

    { // 02: E major at the nut, A+B pedals -> A major; then at fret 5 (A -> D)
        Steel st;
        auto phrase = [&] (double t, double fret) {
            st.bar (t - 0.3, fret, 0.0);
            st.pick (t, { 6, 5, 4, 3 }, 0.75);
            // A pedal raises strings 5 and 10 (B -> C#) a whole step; B pedal raises 3 and 6 (G# -> A) a half step
            st.pedal (t + 1.2, { { 5, 2.0 }, { 3, 1.0 }, { 6, 1.0 } }, 0.35);
            st.pedal (t + 2.8, { { 5, 0.0 }, { 3, 0.0 }, { 6, 0.0 } }, 0.35);
            st.pedal (t + 3.8, { { 5, 2.0 }, { 3, 1.0 }, { 6, 1.0 } }, 0.18);
            st.pedal (t + 4.4, { { 5, 0.0 }, { 3, 0.0 }, { 6, 0.0 } }, 0.25);
            st.at (t + 5.6, [] (Steel& s) { for (auto& x : s.s) x.setDamp (1); });
            st.at (t + 5.9, [] (Steel& s) { for (auto& x : s.s) x.setDamp (0); });
        };
        phrase (0.4, 0);
        phrase (6.6, 5);
        // swell version: pick with the volume pedal down, swell while the pedals go in
        st.bar (12.5, 0, 0);
        st.swell (12.75, 0.0, 0.02);
        st.pick (12.9, { 6, 5, 4, 3 }, 0.8);
        st.swell (12.95, 1.0, 0.7);
        st.pedal (13.6, { { 5, 2.0 }, { 3, 1.0 }, { 6, 1.0 } }, 0.6);
        st.swell (15.6, 0.0, 1.6);
        writeWav16 (dir + "02_ab_pedals.wav", st.render (17.5), Steel::fs);
    }

    { // 03: slides
        Steel st;
        st.pick (0.3, { 6 }, 0.8);
        st.at (0.8, [] (Steel& s) { s.str (6).setBar (5, 0.35); });
        st.at (1.8, [] (Steel& s) { s.str (6).setBar (3, 0.2); });
        st.at (2.6, [] (Steel& s) { s.str (6).setBar (10, 0.6); });
        st.at (4.0, [] (Steel& s) { s.str (6).setBar (0, 0.8); });
        st.at (5.4, [] (Steel& s) { s.str (6).setDamp (1); });
        // chord slide: grab strings 4-5-6 at fret 3 and slide up to 5, then 8 and back to 7
        st.at (5.6, [] (Steel& s) { s.str (6).setDamp (0); for (auto& x : s.s) x.setBar (3, 0); });
        st.pick (6.0, { 6, 5, 4 }, 0.75);
        st.bar (6.9, 5, 0.25);
        st.bar (8.0, 8, 0.3);
        st.bar (9.0, 7, 0.15);
        st.pick (10.2, { 8, 6, 4 }, 0.6);
        st.bar (10.9, 12, 0.9);
        writeWav16 (dir + "03_bar_slides.wav", st.render (14.0), Steel::fs);
    }

    { // 04: swells (ambient)
        Steel st;
        const double frets[] = { 0, 5, 7, 3 };
        double t = 0.3;
        for (double fr : frets)
        {
            st.bar (t - 0.1, fr, 0);
            st.swell (t - 0.05, 0.0, 0.01);
            st.pick (t, { 8, 6, 5, 4, 3 }, 0.6, 0.02);
            st.swell (t + 0.08, 1.0, 1.1);
            st.swell (t + 2.3, 0.0, 1.0);
            t += 3.0;
        }
        writeWav16 (dir + "04_swells.wav", st.render (t + 0.8), Steel::fs);
    }

    { // 05: vibrato on a held chord (bar rocking ~5.5 Hz, depth fading in)
        Steel st;
        st.bar (0.1, 7, 0);
        st.pick (0.3, { 6, 5, 4, 3 }, 0.75);
        st.at (4.2, [] (Steel& s) { for (auto& x : s.s) x.setDamp (1); });
        auto x = st.render (5.0, [] (Steel& s, double t) {
            const double depth = std::clamp ((t - 0.9) / 1.2, 0.0, 1.0) * 0.12;
            const double v = depth * std::sin (2 * kPi * 5.5 * t);
            for (auto& str : s.s)
                str.setVibrato (v);
        });
        writeWav16 (dir + "05_vibrato.wav", x, Steel::fs);
    }
    return 0;
}

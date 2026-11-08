// Tests for the Steelwave engine: E9 voicings, chord and single-note sets, MIDI behaviour (no JUCE).
#include "../core/SteelEngine.h"

#include <cstdio>
#include <functional>
#include <random>

using namespace sw;

static int failures = 0;
#define CHECK(cond, ...)                                        \
    do {                                                        \
        const bool ok_ = (cond);                                \
        std::printf ("  [%s] ", ok_ ? "PASS" : "FAIL");         \
        std::printf (__VA_ARGS__);                              \
        std::printf ("\n");                                     \
        if (! ok_) ++failures;                                  \
    } while (0)

static constexpr double kFs = 48000.0;
static double midiHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

static double dftMag (const std::vector<float>& x, double f, double t0, double len)
{
    const size_t i0 = (size_t) (t0 * kFs), L = (size_t) (len * kFs);
    double re = 0, im = 0;
    for (size_t j = 0; j < L && i0 + j < x.size(); ++j)
    {
        const double w = 0.5 - 0.5 * std::cos (2 * kPi * (j + 0.5) / L), ph = 2 * kPi * f * (double) j / kFs;
        re += w * x[i0 + j] * std::cos (ph);
        im -= w * x[i0 + j] * std::sin (ph);
    }
    return std::sqrt (re * re + im * im) * 4.0 / (double) L;
}
static double db (double v) { return 20.0 * std::log10 (v + 1e-15); }
static double rms (const std::vector<float>& x, double t0, double t1)
{
    double s = 0;
    size_t n = 0;
    for (size_t i = (size_t) (t0 * kFs); i < (size_t) (t1 * kFs) && i < x.size(); ++i, ++n)
        s += (double) x[i] * x[i];
    return std::sqrt (s / std::max<size_t> (n, 1));
}

// scripted render: events at times (seconds)
struct Script
{
    std::vector<std::pair<double, std::function<void (SteelEngine&)>>> ev;
    void at (double t, std::function<void (SteelEngine&)> f) { ev.push_back ({ t, std::move (f) }); }
};
static std::vector<float> render (SteelEngine& e, Script sc, double seconds)
{
    std::sort (sc.ev.begin(), sc.ev.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    std::vector<float> out ((size_t) (seconds * kFs));
    size_t pos = 0, k = 0;
    while (pos < out.size())
    {
        size_t next = out.size();
        if (k < sc.ev.size())
            next = std::min (next, (size_t) (sc.ev[k].first * kFs));
        if (next > pos)
        {
            e.process (out.data() + pos, (int) (next - pos));
            pos = next;
        }
        while (k < sc.ev.size() && (size_t) (sc.ev[k].first * kFs) <= pos)
            sc.ev[k++].second (e);
    }
    return out;
}
static std::vector<float> render (EngineParams p, int set, Script sc, double seconds)
{
    SteelEngine e;
    e.prepare (kFs);
    e.setParams (p);
    e.setSet (set);
    return render (e, std::move (sc), seconds);
}

// strongest of the given MIDI pitches present vs absent (dB)
static double presence (const std::vector<float>& x, double midi, double t0, double len = 0.3)
{
    return db (dftMag (x, midiHz (midi), t0, len));
}

static int pcOf (int s, int bar, double off) { return (kE9Open[(size_t) s] + bar + (int) std::lround (off) + 1200) % 12; }

int main()
{
    std::printf ("Copedent\n");
    {
        const auto o = copedentOffsets ((1u << PedA) | (1u << PedB));
        CHECK (o[2] == 1 && o[3] == 0 && o[4] == 2 && o[5] == 1 && o[9] == 2, "A+B raises 3,6 a half step and 5,10 a whole step");
        const auto c = copedentOffsets ((1u << PedA) | (1u << PedC));
        CHECK (c[4] == 2, "A and C both pulling string 5 to C# don't add up");
        CHECK (playableCombos().size() == 54, "54 playable combinations (feet x left knee x right knee)");
    }

    std::printf ("Chord voicings: every set, every key\n");
    {
        int bad = 0, badBend = 0, badBar = 0, real = 0, total = 0;
        for (int si = 0; si < kChordSets; ++si)
            for (int slot = 0; slot < 12; ++slot)
            {
                const auto& v = chordSets()[(size_t) si][(size_t) slot];
                const auto& sl = chordSlots()[(size_t) slot];
                auto pcs = [&] (const std::array<double, kStrings>& off) {
                    uint32_t m = 0;
                    for (int s : v.strings) m |= 1u << pcOf (s, v.bar, off[(size_t) s]);
                    return m;
                };
                const auto& def = chordSetDefs()[(size_t) si];
                if (pcs (v.rest) != chordPcs (sl.rest, 4) || (int) v.strings.size() < def.minSize || (int) v.strings.size() > def.maxSize)
                    ++bad;
                for (auto tgtp : { std::make_pair (&v.up, sl.up), std::make_pair (&v.down, sl.down) })
                {
                    const uint32_t want = chordPcs (tgtp.second, 4), got = pcs (*tgtp.first);
                    const int root = (4 + tgtp.second.root) % 12, third = (root + (tgtp.second.q == Maj ? 4 : 3)) % 12;
                    if ((got & ~want) || ! (got & (1u << root)) || ! (got & (1u << third)))
                        ++badBend;
                    for (int s : v.strings)
                        if (std::fabs ((*tgtp.first)[(size_t) s] - v.rest[(size_t) s]) > 3.0)
                            ++badBend;
                }
                real += v.upHow != "voice-led";
                real += v.downHow != "voice-led";
                total += 2;
                for (int key = 0; key < 12; ++key)
                {
                    const int b = fitBar (v.bar + keyShift (key));
                    if (b < 0 || b > 22) ++badBar;
                }
            }
        CHECK (bad == 0, "every chord is exactly its triad, within the set's string count (%d wrong)", bad);
        CHECK (badBend == 0, "every bend lands on the target chord (root and third, nothing else), strings move <= 3 semitones (%d wrong)", badBend);
        CHECK (badBar == 0, "every key keeps the bar between 0 and 22");
        std::printf ("    (%d of %d bends use real E9 pedals/levers)\n", real, total);
        const auto& I = chordSets()[0][0];
        CHECK (I.bar == 0 && I.restCombo == 0 && I.upHow == "A+B" && I.downHow == "LKR", "Classic I: open E at the nut, wheel up = A+B (IV), down = LKR (iii)");
        CHECK (chordSets()[0][1].bar != I.bar, "the black-key I is another voicing (bar %d)", chordSets()[0][1].bar);
    }

    std::printf ("Single-note sets\n");
    {
        static const int scale[7] = { 0, 2, 4, 5, 7, 9, 11 };
        int wrong = 0, bendWrong = 0, missing = 0;
        for (int si = 0; si < kSingleSets; ++si)
            for (int key = 0; key < 12; ++key)
            {
                SingleMap m;
                m.build (si, key);
                for (int note = 36; note <= 96; ++note)
                {
                    const auto n = m.lookup (note);
                    if (n.string < 0) { ++missing; continue; }
                    const int pc = note % 12;
                    const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
                    const int white = black ? pc - 1 : pc;
                    const int deg = white == 0 ? 0 : white == 2 ? 1 : white == 4 ? 2 : white == 5 ? 3 : white == 7 ? 4 : white == 9 ? 5 : 6;
                    const int sounding = kE9Open[(size_t) n.string] + m.bar() + (int) n.offset;
                    if (sounding != n.pitch || ((sounding - key - scale[deg]) % 12 + 12) % 12 != 0)
                        ++wrong;
                    if (black == n.bends)
                        ++bendWrong;
                    if (! black)
                    {
                        const int up = sounding + (int) n.bendUp, dn = sounding + (int) n.bendDown;
                        auto inScale = [&] (int p) { const int r = ((p - key) % 12 + 12) % 12; return r == 0 || r == 2 || r == 4 || r == 5 || r == 7 || r == 9 || r == 11; };
                        if (si != 2 && si != 3 && (! inScale (up) || ! inScale (dn) || n.bendUp <= 0 || n.bendDown >= 0 || n.bendUp > 2 || n.bendDown < -2))
                            ++bendWrong;
                    }
                }
            }
        CHECK (missing == 0 && wrong == 0, "C2..C7 in all keys: white keys play the scale degree they stand for (%d wrong, %d missing)", wrong, missing);
        CHECK (bendWrong == 0, "white keys bend to the next/previous scale note, black keys don't bend (%d wrong)", bendWrong);
        SingleMap m;
        m.build (0, 4);
        CHECK (m.lookup (48).pitch == 52 && m.lookup (60).pitch == 64, "in E, C3 plays E3 and C4 plays E4");
    }

    EngineParams p;
    p.strumMs = 0;
    std::printf ("Chords and the pitch wheel\n");
    {
        Script sc;
        sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.8); });
        sc.at (1.2, [] (SteelEngine& e) { e.pitchWheel (1.0); });
        auto x = render (p, 0, sc, 2.4);
        // E chord: G#3 B3 E4 G#4 ; with A+B: A3 C#4 E4 A4
        const double t1 = 0.5, t2 = 1.8;
        CHECK (presence (x, 56, t1) - presence (x, 57, t1) > 20 && presence (x, 68, t1) - presence (x, 69, t1) > 20, "E chord sounds G# (not A)");
        CHECK (presence (x, 57, t2) - presence (x, 56, t2) > 20 && presence (x, 61, t2) - presence (x, 59, t2) > 20,
               "wheel up: G# -> A and B -> C# (A+B pedals: E -> A)");
        Script hold;
        hold.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.8); });
        auto h = render (p, 0, hold, 2.4);
        CHECK (std::fabs (presence (x, 64, t2) - presence (h, 64, t2)) < 3, "E4 (string 4) holds through the bend");
        Script sd;
        sd.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.8); });
        sd.at (1.2, [] (SteelEngine& e) { e.pitchWheel (-1.0); });
        auto y = render (p, 0, sd, 2.4);
        CHECK (presence (y, 63, t2) - presence (y, 64, t2) > 20, "wheel down: E -> D# (LKR: E -> G#m)");
    }

    std::printf ("Keys\n");
    {
        EngineParams q = p;
        q.keyPc = 7; // G
        Script sc;
        sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.8); });
        auto x = render (q, 0, sc, 1.0);
        // G major, Classic grip at bar 3: B3 D4 G4 B4
        CHECK (presence (x, 59, 0.4) - presence (x, 56, 0.4) > 20 && presence (x, 67, 0.4) - presence (x, 64, 0.4) > 20, "key of G: C plays G major");
    }

    std::printf ("Legato slides\n");
    {
        Script sc;
        sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.8); });
        sc.at (1.0, [] (SteelEngine& e) { e.noteOn (67, 0.8); }); // V while I is held
        SteelEngine e;
        e.prepare (kFs);
        e.setParams (p);
        auto x = render (e, sc, 2.2);
        // V (Classic): bar 7, strings 3 4 5 -> D#5 B4 F#4
        CHECK (e.bar() == 7, "the bar slid to fret 7");
        CHECK (presence (x, 71, 1.7) - presence (x, 72, 1.7) > 20 && presence (x, 66, 1.7) - presence (x, 67, 1.7) > 20, "and the chord is B major");
        // no new attack: level during the slide stays below the first pick's
        CHECK (rms (x, 1.0, 1.3) < rms (x, 0.05, 0.35), "the slide doesn't re-pick ringing strings");
        // a long-held chord, nearly died away, slid legato: still no new attack
        for (double sym : { 0.0, 0.5 })
        {
            EngineParams q = p;
            q.sympathetic = sym;
            q.barNoise = 0; // the slide's own noise isn't what this is about
            Script sl;
            sl.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.75); });
            sl.at (5.0, [] (SteelEngine& e) { e.noteOn (67, 0.75); });
            auto z = render (q, 0, sl, 5.6);
            CHECK (rms (z, 5.0, 5.3) < 1.2 * rms (z, 4.7, 5.0), "sympathetic %.1f: sliding a chord that has rung for 5 s doesn't re-pick it", sym);
        }
        // after a slide nothing is left ringing outside the chord: the bar scraping over the strings
        // that aren't played (under the picking hand) mustn't set them ringing at their own pitches
        {
            auto offChord = [&] (double noise) {
                EngineParams q = p;
                q.barNoise = noise;
                Script sl;
                sl.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.8); });
                sl.at (0.8, [] (SteelEngine& e) { e.noteOn (65, 0.8); }); // legato E -> A, bar 0 -> 5
                sl.at (0.85, [] (SteelEngine& e) { e.noteOff (60); });
                auto z = render (q, 0, sl, 2.2);
                const double chord[4] = { 73, 69, 64, 61 };
                double harm = 0, off = 0;
                for (double f = 60; f < 6000; f += 2.5)
                {
                    const double m = std::pow (dftMag (z, f, 1.6, 0.4), 2);
                    bool h = false;
                    for (double cm : chord)
                        for (int k = 1; k < 20; ++k)
                            h |= std::fabs (f - k * midiHz (cm)) < 8 + 0.004 * k * midiHz (cm);
                    (h ? harm : off) += m;
                }
                return 10 * std::log10 (off / harm);
            };
            const double with = offChord (0.5), without = offChord (0.0);
            CHECK (with < without + 1.5, "0.5 s after a legato slide, sound outside the chord: %.1f dB with bar noise, %.1f dB without", with, without);
        }
        // the scrape of a legato chord slide, isolated (same render with and without bar noise):
        // through a pickup it's a faint texture, far below the picked chord
        {
            auto slide = [&] (double noise) {
                EngineParams q = p;
                q.barNoise = noise;
                Script sl;
                sl.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.75); });
                sl.at (0.8, [] (SteelEngine& e) { e.noteOn (65, 0.75); });
                sl.at (0.85, [] (SteelEngine& e) { e.noteOff (60); });
                return render (q, 0, sl, 1.2);
            };
            auto a = slide (0.5), z = slide (0.0);
            std::vector<float> d (a.size());
            for (size_t i = 0; i < a.size(); ++i) d[i] = a[i] - z[i];
            const double rel = db (rms (d, 0.8, 1.1)) - db (rms (z, 0.05, 0.55));
            CHECK (rel < -44 && rel > -60, "a legato chord slide's scrape: %.1f dB below the picked chord (44..60)", -rel);
        }
        Script sb;
        sb.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.8); });
        sb.at (0.8, [] (SteelEngine& e) { e.noteOff (60); });
        sb.at (1.0, [] (SteelEngine& e) { e.noteOn (67, 0.8); }); // not legato
        auto y = render (p, 0, sb, 1.6);
        CHECK (rms (y, 1.02, 1.2) > 2.0 * rms (y, 0.9, 0.98), "a detached chord is picked again");
    }

    std::printf ("Releases, sustain, volume pedal\n");
    {
        Script sc;
        sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.8); });
        sc.at (0.6, [] (SteelEngine& e) { e.noteOff (60); });
        auto x = render (p, 0, sc, 1.4);
        CHECK (db (rms (x, 0.4, 0.55)) - db (rms (x, 1.1, 1.3)) > 40, "key up blocks the chord (%.0f dB)", db (rms (x, 0.4, 0.55)) - db (rms (x, 1.1, 1.3)));
        Script ss = sc;
        ss.at (0.01, [] (SteelEngine& e) { e.controller (64, 1.0); });
        auto y = render (p, 0, ss, 1.4);
        CHECK (db (rms (y, 0.4, 0.55)) - db (rms (y, 1.1, 1.3)) < 15, "with sustain it rings on (%.0f dB natural decay)", db (rms (y, 0.4, 0.55)) - db (rms (y, 1.1, 1.3)));
        Script sv;
        sv.at (0.0, [] (SteelEngine& e) { e.controller (11, 0.0); });
        sv.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.8); });
        sv.at (0.6, [] (SteelEngine& e) { e.controller (11, 1.0); });
        auto v = render (p, 0, sv, 1.2);
        CHECK (rms (v, 0.1, 0.5) < 1e-6 && rms (v, 0.8, 1.1) > 1e-3, "volume pedal: silent heel-down, swells in");
    }

    std::printf ("Volume pedal tone\n");
    {
        // a held chord; heard at full volume and with the pedal at half level (divider t = 0.5)
        auto tone = [&] (EngineParams::VolumePedal type, double cc) {
            EngineParams q = p;
            q.volumePedal = type;
            Script sc;
            sc.at (0.0, [cc] (SteelEngine& e) { e.controller (11, cc); });
            sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.9); });
            auto x = render (q, 0, sc, 0.8);
            double hi = 0, lo = 0;
            for (double f = 3500; f < 7000; f *= 1.05) hi += std::pow (10, presence (x, 12 + 12 * std::log2 (f / 8.1758), 0.3, 0.4) / 10);
            for (double f = 300; f < 700; f *= 1.05) lo += std::pow (10, presence (x, 12 + 12 * std::log2 (f / 8.1758), 0.3, 0.4) / 10);
            return 10 * std::log10 (hi / lo);
        };
        const double half = std::pow (0.5, 1.0 / 2.5);
        const double p250 = tone (EngineParams::Passive250k, half) - tone (EngineParams::Passive250k, 1.0);
        const double p500 = tone (EngineParams::Passive500k, half) - tone (EngineParams::Passive500k, 1.0);
        const double act = tone (EngineParams::Active, half) - tone (EngineParams::Active, 1.0);
        CHECK (p250 < -3.0, "passive 250k pedal half down: highs %.1f dB vs full up", p250);
        CHECK (p500 < p250 - 2.0, "500k pot darker still (%.1f dB)", p500);
        CHECK (std::fabs (act) < 0.5, "active pedal: no tone change (%.2f dB)", act);
    }

    std::printf ("Single-note sets\n");
    {
        Script sc;
        sc.at (0.05, [] (SteelEngine& e) { e.noteOn (48, 0.8); }); // C3 -> E3 (string 8)
        sc.at (0.9, [] (SteelEngine& e) { e.pitchWheel (1.0); });
        auto x = render (p, kChordSets, sc, 1.8);
        CHECK (presence (x, 52, 0.5) > presence (x, 54, 0.5) + 20, "C3 plays E3");
        CHECK (presence (x, 54, 1.4) > presence (x, 52, 1.4) + 10, "wheel up bends it to F#3");
        Script ds;
        ds.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.8); e.noteOn (62, 0.8); }); // E4 + F#4 double stop
        auto y = render (p, kChordSets, ds, 1.0);
        CHECK (presence (y, 64, 0.5) > -60 && presence (y, 66, 0.5) > -60, "double stops sound both notes");
        SingleMap m;
        m.build (0, 4);
        int shared = 0, pairs = 0;
        for (int w : { 48, 50, 53, 55, 57, 60, 62, 65, 67, 69 })
        {
            const auto a = m.lookup (w), b = m.lookup (w + 1);
            pairs++;
            shared += a.string == b.string;
            if (a.pitch != b.pitch) shared += 100;
        }
        CHECK (shared < pairs / 2, "a black key mostly holds its note on a different string from the bending white key (%d of %d share one)", shared, pairs);
        Script ks;
        ks.at (0.01, [] (SteelEngine& e) { e.noteOn (kKeyswitchLo + 9, 1.0); });
        SteelEngine e;
        e.prepare (kFs);
        render (e, ks, 0.1);
        CHECK (e.currentSet() == 9, "keyswitch G#1+1 (A1) selects single set 2");
    }

    std::printf ("Real pedals on CCs\n");
    {
        Script sc;
        sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.8); });
        sc.at (0.8, [] (SteelEngine& e) { e.controller (kPedalCcFirst + PedB, 1.0); }); // B pedal: G# -> A on 3, 6
        auto x = render (p, 0, sc, 1.6);
        CHECK (presence (x, 57, 1.3) - presence (x, 56, 1.3) > 20 && presence (x, 59, 1.3) - presence (x, 61, 1.3) > 20,
               "CC21 = B pedal: G# -> A, B stays");
    }

    std::printf ("Pedal mechanics\n");
    {
        // hold an E chord (strings 3 4 5 6), move the wheel, read string pitches
        auto hold = [&] (std::function<void (Script&)> moves, double until, std::vector<std::pair<double, double>>* trace = nullptr, int traceString = -1) {
            SteelEngine e;
            e.prepare (kFs);
            e.setParams (p);
            Script sc;
            sc.at (0.05, [] (SteelEngine& en) { en.noteOn (60, 0.7); });
            moves (sc);
            std::sort (sc.ev.begin(), sc.ev.end(), [] (auto& a, auto& b) { return a.first < b.first; });
            std::vector<float> buf (64);
            size_t k = 0;
            for (double t = 0; t < until; t += 64.0 / kFs)
            {
                while (k < sc.ev.size() && sc.ev[k].first <= t) sc.ev[k++].second (e);
                e.process (buf.data(), 64);
                if (trace) trace->push_back ({ t, e.stringSemis (traceString) });
            }
            std::array<double, kStrings> semis {};
            for (int s = 0; s < kStrings; ++s) semis[(size_t) s] = e.stringSemis (s);
            return semis;
        };
        // half way through a whole-step pull: a little past a semitone (tension, not pitch, is linear)
        auto half = hold ([] (Script& sc) { sc.at (0.3, [] (SteelEngine& e) { e.pitchWheel (0.5); }); }, 0.8);
        CHECK (half[4] > 1.02 && half[4] < 1.09, "half wheel: string 5 (B -> C#) is %.3f semitones up (tension-linear: ~1.06)", half[4]);
        // cabinet drop: the strings that don't move sag a little when others are pulled sharp
        auto full = hold ([] (Script& sc) { sc.at (0.3, [] (SteelEngine& e) { e.pitchWheel (1.0); }); }, 0.8);
        const double drop = 100.0 * full[3];
        CHECK (drop < -0.5 && drop > -2.5, "A+B through the wheel: unpulled string 4 sags %.2f cents (cabinet drop)", drop);
        auto ab = hold ([] (Script& sc) { sc.at (0.3, [] (SteelEngine& e) { e.controller (kPedalCcFirst + PedA, 1.0); e.controller (kPedalCcFirst + PedB, 1.0); }); }, 0.8);
        CHECK (100.0 * ab[3] < -1.5 && 100.0 * ab[3] > -3.0, "A+B pedals on CCs (4 strings pulled): string 4 sags %.2f cents", 100.0 * ab[3]);
        auto lkr = hold ([] (Script& sc) { sc.at (0.3, [] (SteelEngine& e) { e.controller (kPedalCcFirst + LKR, 1.0); }); }, 0.8);
        CHECK (lkr[2] > 0.0, "lowering a lever relieves the cabinet: string 3 rises %.2f cents", 100.0 * lkr[2]);
        // stops: a quick stab overshoots and settles; an eased-in move doesn't
        auto overshoot = [&] (double rampSec) {
            std::vector<std::pair<double, double>> tr;
            hold ([rampSec] (Script& sc) {
                for (int i = 1; i <= 10; ++i)
                    sc.at (0.3 + rampSec * i / 10.0, [i] (SteelEngine& e) { e.pitchWheel (i / 10.0); });
            }, 1.4, &tr, 4);
            const double final = tr.back().second;
            double peak = -1e9, late = 0;
            for (auto& [t, v] : tr)
            {
                if (t > 0.3 && t < 0.3 + rampSec + 0.15) peak = std::max (peak, v);
                if (t > 0.3 + rampSec + 0.2 && t < 0.3 + rampSec + 0.3) late = std::max (late, std::fabs (v - final));
            }
            return std::make_pair (100.0 * (peak - final), 100.0 * late);
        };
        const auto fast = overshoot (0.02), slow = overshoot (0.6);
        CHECK (fast.first > 1.0 && fast.first < 5.0 && fast.second < 0.3, "a quick stab into the stop overshoots %.1f cents and settles (%.2f cents left after 0.2 s)", fast.first, fast.second);
        CHECK (slow.first < 0.5, "an eased-in move doesn't (%.2f cents)", slow.first);
    }

    std::printf ("Sympathetic strings\n");
    {
        auto run = [&] (double sym) {
            EngineParams q = p;
            q.sympathetic = sym;
            Script sc;
            sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.9); });
            return render (q, kChordSets, sc, 2.0); // single set: C4 -> E4 on string 4, the others free
        };
        auto a = run (0.0), b = run (1.0);
        double diff = 0, ref = 0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            diff += (double) (a[i] - b[i]) * (a[i] - b[i]);
            ref += (double) a[i] * a[i];
        }
        const double rel = 10 * std::log10 (diff / ref);
        // through the body: strongest near its resonances, so it depends on the note
        CHECK (rel > -70 && rel < -10, "coupling adds a quiet halo of other strings (%.1f dB re the note)", rel);
    }

    std::printf ("Stability\n");
    {
        std::mt19937 g (11);
        std::uniform_real_distribution<double> u (0, 1);
        EngineParams q;
        q.sympathetic = 1.0;
        q.sustain = 2.0;
        SteelEngine e;
        e.prepare (kFs);
        e.setParams (q);
        std::vector<float> buf (512);
        bool ok = true;
        double peak = 0;
        for (int blk = 0; blk < (int) (60.0 * kFs / 512); ++blk)
        {
            const double r = u (g);
            const int note = 24 + (int) (u (g) * 72);
            if (r < 0.15) e.noteOn (note, u (g));
            else if (r < 0.28) e.noteOff (note);
            else if (r < 0.36) e.pitchWheel (u (g) * 2 - 1);
            else if (r < 0.40) e.controller (1, u (g));
            else if (r < 0.43) e.controller (64, u (g));
            else if (r < 0.47) e.controller (kPedalCcFirst + (int) (u (g) * 7), u (g));
            else if (r < 0.48) { q.keyPc = (int) (u (g) * 12); e.setParams (q); }
            e.process (buf.data(), 512);
            for (float v : buf)
            {
                ok &= std::isfinite (v);
                peak = std::max (peak, (double) std::fabs (v));
            }
        }
        CHECK (ok && peak < 4.0, "60 s of random MIDI (keyswitches, legato, bends, pedals, keys, sustain): finite, peak %.2f", peak);
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

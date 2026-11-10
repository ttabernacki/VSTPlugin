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
    std::printf ("Footprint\n");
    // hosts and tests may hold the processor (and so the engine) on the stack; Windows gives a thread
    // 1 MB (an engine of 359 KB, two to a test, crashed the Windows build)
    CHECK (sizeof (SteelEngine) < 64 * 1024, "SteelEngine is %zu KB (< 64 KB: buffers live on the heap)", sizeof (SteelEngine) / 1024);

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
                    for (int note : { 48, 60, 72 })
                    {
                        const int b = chordBar (si, v, key, note);
                        if (b < 0 || b > kTopFret) ++badBar;
                    }
            }
        CHECK (bad == 0, "every chord is exactly its triad, within the set's string count (%d wrong)", bad);
        CHECK (badBend == 0, "every bend lands on the target chord (root and third, nothing else), strings move <= 3 semitones (%d wrong)", badBend);
        CHECK (badBar == 0, "every key and octave keeps the bar on the neck (frets 0-26)");
        // the chords climb as you go up the keys: going up the white keys always goes up in pitch,
        // in every set and every key (the user's report: C to D went down)
        auto mean = [] (int si, int slot, int key, int note) {
            const auto& v = chordSets()[(size_t) si][(size_t) slot];
            const int b = chordBar (si, v, key, note);
            double m = 0;
            for (int st : v.strings) m += kE9Open[(size_t) st] + b + v.rest[(size_t) st];
            return m / (double) v.strings.size();
        };
        int falls = 0, blackOut = 0;
        const int whites[] = { 0, 2, 4, 5, 7, 9, 11 };
        for (int si = 0; si < kChordSets; ++si)
            for (int key = 0; key < 12; ++key)
            {
                for (int w = 1; w < 7; ++w)
                    if (mean (si, whites[w], key, 60) <= mean (si, whites[w - 1], key, 60)) ++falls;
                for (int bk : { 3, 6, 8, 10 }) // black keys sit (within a semitone) between their neighbours
                    if (mean (si, bk, key, 60) < mean (si, bk - 1, key, 60) - 1.0 || mean (si, bk, key, 60) > mean (si, bk + 1, key, 60) + 1.0) ++blackOut;
            }
        CHECK (falls == 0, "going up the white keys always climbs, every set, every key (%d falls)", falls);
        CHECK (blackOut == 0, "black keys sit between their neighbours (%d out of place)", blackOut);
        CHECK (mean (0, 2, 0, 60) > mean (0, 0, 0, 60), "key of C, Classic: C (C major, %.1f) to D (Dm, %.1f) goes up", mean (0, 0, 0, 60), mean (0, 2, 0, 60));
        // the octave played picks the register
        CHECK (chordBar (0, chordSets()[0][0], 4, 72) == chordBar (0, chordSets()[0][0], 4, 60) + 12, "an octave up the keyboard: the bar moves up 12 frets");
        CHECK (chordBar (0, chordSets()[0][0], 7, 48) == chordBar (0, chordSets()[0][0], 7, 60) || chordBar (0, chordSets()[0][0], 7, 48) == chordBar (0, chordSets()[0][0], 7, 60) - 12, "an octave down: 12 frets lower where it fits");
        std::printf ("    (%d of %d bends use real E9 pedals/levers)\n", real, total);
        const auto& I = chordSets()[0][0];
        CHECK (I.bar == 0 && I.restCombo == 0 && I.upHow == "A+B" && I.downHow == "LKR", "Classic I: open E at the nut, wheel up = A+B (IV), down = LKR (iii)");
        CHECK (chordSets()[0][1].strings != I.strings || chordSets()[0][1].bar != I.bar, "the black-key I is another voicing");
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

    std::printf ("Performance controls\n");
    {
        // run an engine with timed events, sampling one string's pitch (semitones) as it goes
        auto play = [&] (EngineParams q, std::function<void (Script&)> moves, double until, int traceString,
                         std::vector<std::pair<double, double>>& trace, std::vector<float>* audio = nullptr) {
            SteelEngine e;
            e.prepare (kFs);
            e.setParams (q);
            Script sc;
            moves (sc);
            std::sort (sc.ev.begin(), sc.ev.end(), [] (auto& a, auto& b) { return a.first < b.first; });
            std::vector<float> buf (32);
            size_t k = 0;
            for (double t = 0; t < until; t += 32.0 / kFs)
            {
                while (k < sc.ev.size() && sc.ev[k].first <= t) sc.ev[k++].second (e);
                e.process (buf.data(), 32);
                trace.push_back ({ t, e.stringSemis (traceString) });
                if (audio) audio->insert (audio->end(), buf.begin(), buf.end());
            }
        };
        auto at = [] (const std::vector<std::pair<double, double>>& tr, double t) {
            for (auto& [tt, v] : tr) if (tt >= t) return v;
            return tr.back().second;
        };
        auto swing = [] (const std::vector<std::pair<double, double>>& tr, double t0, double t1) {
            double lo = 1e9, hi = -1e9;
            for (auto& [t, v] : tr) if (t >= t0 && t < t1) { lo = std::min (lo, v); hi = std::max (hi, v); }
            return hi - lo;
        };
        std::vector<std::pair<double, double>> tr;
        // pedal key B (MIDI 37) under an E chord: string 3 G# -> A, travelling like a pedal
        play (p, [] (Script& sc) {
            sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.7); });
            sc.at (0.5, [] (SteelEngine& e) { e.noteOn (kPedalKeyLo + PedB, 0.2); }); // gentle: ~210 ms
            sc.at (1.2, [] (SteelEngine& e) { e.noteOff (kPedalKeyLo + PedB); });
        }, 1.8, 2, tr);
        const double mid = at (tr, 0.6), down = at (tr, 1.0), up = at (tr, 1.7);
        CHECK (mid > 0.15 && mid < 0.85, "pedal key B pressed gently: 100 ms in, string 3 is %.2f of the way to A (travelling)", mid);
        CHECK (std::fabs (down - 1.0) < 0.03 && std::fabs (up) < 0.03, "held: A (%+.3f), let go: back to G# (%+.3f)", down - 1.0, up);
        tr.clear();
        play (p, [] (Script& sc) {
            sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.7); });
            sc.at (0.5, [] (SteelEngine& e) { e.noteOn (kPedalKeyLo + PedB, 1.0); }); // hard: ~80 ms
        }, 0.7, 2, tr);
        CHECK (at (tr, 0.6) > 0.95, "hit hard, the pedal is down within 100 ms (%.2f)", at (tr, 0.6));
        // pedal keys off: those keys play chords again
        {
            EngineParams q = p;
            q.pedalKeys = false;
            SteelEngine e;
            e.prepare (kFs);
            e.setParams (q);
            std::vector<float> b (4800);
            e.noteOn (kPedalKeyLo + PedB, 0.8);
            e.process (b.data(), 4800);
            CHECK (e.chordSlot() == 1, "pedal keys off: MIDI 37 plays the C# chord slot again");
        }
        // aftertouch shakes the bar
        tr.clear();
        play (p, [] (Script& sc) {
            sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.7); });
            sc.at (0.6, [] (SteelEngine& e) { e.aftertouch (1.0); });
        }, 1.4, 3, tr);
        CHECK (swing (tr, 0.1, 0.55) < 0.005 && swing (tr, 0.9, 1.4) > 0.2, "aftertouch: no vibrato, then +-%.0f cents with full pressure", 50 * swing (tr, 0.9, 1.4));
        // auto vibrato comes in on its own once a note has been held
        {
            EngineParams q = p;
            q.autoVibrato = 1.0;
            q.vibDelay = 0.5;
            tr.clear();
            play (q, [] (Script& sc) { sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.7); }); }, 1.8, 3, tr);
            CHECK (swing (tr, 0.1, 0.5) < 0.005 && swing (tr, 1.3, 1.8) > 0.2, "auto vibrato: still for %.1f s, then it comes in (+-%.0f cents)", q.vibDelay, 50 * swing (tr, 1.3, 1.8));
        }
        // mod wheel as the A+B pedals, or as a bar slide
        {
            EngineParams q = p;
            q.modWheel = EngineParams::ModPedalsAB;
            tr.clear();
            play (q, [] (Script& sc) { sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.7); e.controller (1, 1.0); }); }, 0.6, 4, tr);
            CHECK (std::fabs (tr.back().second - 2.0) < 0.05, "mod wheel as A+B: string 5 up a whole step (%.2f)", tr.back().second);
            q.modWheel = EngineParams::ModBarSlide;
            tr.clear();
            play (q, [] (Script& sc) {
                sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.7); });
                sc.at (0.3, [] (SteelEngine& e) { e.controller (1, 0.5); });
            }, 0.8, 3, tr);
            CHECK (std::fabs (tr.back().second - 1.0) < 0.05, "mod wheel as a bar slide: half way = 1 fret (%.2f)", tr.back().second);
        }
        // auto swell: soft notes fade in, firm ones pick
        {
            EngineParams q = p;
            q.swellBelow = 0.5;
            q.swellTime = 0.5;
            std::vector<float> soft, hard;
            tr.clear();
            play (q, [] (Script& sc) { sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.3); }); }, 1.0, 3, tr, &soft);
            tr.clear();
            play (q, [] (Script& sc) { sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.8); }); }, 1.0, 3, tr, &hard);
            CHECK (rms (soft, 0.05, 0.12) < 0.1 * rms (soft, 0.5, 0.6), "auto swell: a soft note fades in (%.0f dB in the first 70 ms re 0.5 s)", db (rms (soft, 0.05, 0.12) / rms (soft, 0.5, 0.6)));
            CHECK (rms (hard, 0.05, 0.12) > rms (hard, 0.5, 0.6), "a firm note picks as usual");
        }
    }

    std::printf ("Slides, timing, MPE\n");
    {
        auto trace = [&] (EngineParams q, std::function<void (Script&)> moves, double until, int str, std::vector<float>* audio = nullptr) {
            SteelEngine e;
            e.prepare (kFs);
            e.setParams (q);
            Script sc;
            moves (sc);
            std::sort (sc.ev.begin(), sc.ev.end(), [] (auto& a, auto& b) { return a.first < b.first; });
            std::vector<std::pair<double, double>> tr;
            std::vector<float> buf (32);
            size_t k = 0;
            for (double t = 0; t < until; t += 32.0 / kFs)
            {
                while (k < sc.ev.size() && sc.ev[k].first <= t) sc.ev[k++].second (e);
                e.process (buf.data(), 32);
                tr.push_back ({ t, e.stringSemis (str) });
                if (audio) audio->insert (audio->end(), buf.begin(), buf.end());
            }
            return tr;
        };
        auto at = [] (const std::vector<std::pair<double, double>>& tr, double t) {
            for (auto& [tt, v] : tr) if (tt >= t) return v;
            return tr.back().second;
        };
        auto reach = [] (const std::vector<std::pair<double, double>>& tr, double t0, double from, double to) {
            for (auto& [t, v] : tr)
                if (t >= t0 && std::fabs (v - to) <= 0.1 * std::fabs (to - from)) return t - t0;
            return 99.0;
        };
        // slide-in: in G (bar 3) a soft chord starts a semitone low and slides up; a firm one doesn't
        EngineParams q = p;
        q.keyPc = 7;
        q.slideInBelow = 0.5;
        q.slideFrom = 1.0;
        q.slideTime = 0.15;
        auto soft = trace (q, [] (Script& sc) { sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.3); }); }, 0.6, 3);
        auto firm = trace (q, [] (Script& sc) { sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.8); }); }, 0.6, 3);
        CHECK (std::fabs (at (soft, 0.06) - 2.0) < 0.1 && std::fabs (at (soft, 0.5) - 3.0) < 0.02,
               "slide-in: a soft chord picks at fret %.2f and slides to fret 3 (%.2f)", at (soft, 0.06), at (soft, 0.5));
        CHECK (std::fabs (at (firm, 0.07) - 3.0) < 0.05, "a firm chord is picked in place (fret %.2f)", at (firm, 0.07));
        // single notes slide in on their own string
        auto single = trace (q, [] (Script& sc) { sc.at (0.05, [] (SteelEngine& e) { e.setSet (kChordSets); e.noteOn (60, 0.3); }); }, 0.6, 3);
        CHECK (at (single, 0.5) - at (single, 0.065) > 0.8, "single note: slides up %.2f semitones into pitch", at (single, 0.5) - at (single, 0.065));

        // slide time follows the playing: a long jump takes longer than a short one
        auto jump = [&] (double scaling, int toNote, double vel) {
            EngineParams r = p;
            r.glideScaling = scaling;
            auto tr = trace (r, [toNote, vel] (Script& sc) {
                sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.8); });
                sc.at (0.5, [toNote, vel] (SteelEngine& e) { e.noteOn (toNote, vel); }); // legato
            }, 1.5, 3);
            const double from = at (tr, 0.49), to = tr.back().second;
            return reach (tr, 0.5, from, to);
        };
        const double far = jump (1.0, 67, 0.7), near = jump (1.0, 63, 0.7), hard = jump (1.0, 67, 1.0);
        const double farFixed = jump (0.0, 67, 0.7), nearFixed = jump (0.0, 63, 0.7);
        CHECK (far > 1.2 * near && std::fabs (farFixed - nearFixed) < 0.02, "Glide feel: 7 frets take %.0f ms, 2 frets %.0f ms (fixed: %.0f / %.0f ms)",
               1000 * far, 1000 * near, 1000 * farFixed, 1000 * nearFixed);
        CHECK (hard < 0.85 * far, "harder playing slides quicker (%.0f ms)", 1000 * hard);

        // bar range for the mod wheel's bar slide
        {
            EngineParams r = p;
            r.modWheel = EngineParams::ModBarSlide;
            r.barRange = 5.0;
            auto tr = trace (r, [] (Script& sc) { sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.7); e.controller (1, 1.0); }); }, 0.5, 3);
            CHECK (std::fabs (tr.back().second - 5.0) < 0.05, "Bar range 5: the mod wheel slides the bar 5 frets (%.2f)", tr.back().second);
        }

        // tight timing: the note speaks sooner
        auto onset = [&] (bool tight) {
            EngineParams r = p;
            r.tight = tight;
            std::vector<float> audio;
            trace (r, [] (Script& sc) { sc.at (0.05, [] (SteelEngine& e) { e.setSet (kChordSets); e.noteOn (60, 0.4); }); }, 0.2, 3, &audio);
            for (size_t i = (size_t) (0.05 * kFs); i < audio.size(); ++i)
                if (std::fabs (audio[i]) > 0.002) return 1000.0 * ((double) i / kFs - 0.05);
            return 99.0;
        };
        const double nat = onset (false), tgt = onset (true);
        CHECK (tgt < 0.6 * nat, "Tight: a soft note speaks after %.1f ms instead of %.1f ms", tgt, nat);

        // MPE: per-finger pitch, pressure and slide
        EngineParams m = p;
        m.mpe = true;
        auto chordBend = trace (m, [] (Script& sc) {
            sc.at (0.05, [] (SteelEngine& e) { e.noteOn (60, 0.7, 2); });
            sc.at (0.3, [] (SteelEngine& e) { e.pitchWheel (2.0 / 48.0, 2); }); // 2 semitones on 48
            sc.at (0.3, [] (SteelEngine& e) { e.pitchWheel (0.5, 3); });        // another channel, no note
        }, 0.7, 3);
        CHECK (std::fabs (chordBend.back().second - 2.0) < 0.05, "MPE chord: the finger's pitch slide moves the bar (%.2f frets); other channels don't", chordBend.back().second);
        SteelEngine e2;
        e2.prepare (kFs);
        e2.setParams (m);
        e2.setSet (kChordSets);
        std::vector<float> buf (4800);
        e2.noteOn (60, 0.7, 2); // E4 on string 4
        e2.noteOn (64, 0.7, 3); // G#4 on string 3
        e2.process (buf.data(), 4800);
        e2.pitchWheel (1.0 / 48.0, 3); // a semitone, only on the G#
        for (int i = 0; i < 5; ++i) e2.process (buf.data(), 4800);
        CHECK (std::fabs (e2.stringSemis (2) - 1.0) < 0.05 && std::fabs (e2.stringSemis (3)) < 0.02,
               "MPE single notes: only the bent finger's string moves (%.2f / %.2f)", e2.stringSemis (2), e2.stringSemis (3));
        e2.aftertouch (1.0, 3);
        double lo3 = 1e9, hi3 = -1e9, lo4 = 1e9, hi4 = -1e9;
        for (int i = 0; i < 300; ++i)
        {
            e2.process (buf.data(), 64);
            if (i > 100)
            {
                lo3 = std::min (lo3, e2.stringSemis (2)); hi3 = std::max (hi3, e2.stringSemis (2));
                lo4 = std::min (lo4, e2.stringSemis (3)); hi4 = std::max (hi4, e2.stringSemis (3));
            }
        }
        CHECK (hi3 - lo3 > 0.2 && hi4 - lo4 < 0.01, "MPE pressure: only that finger's note gets vibrato (+-%.0f vs +-%.1f cents)", 50 * (hi3 - lo3), 50 * (hi4 - lo4));
        {
            std::vector<float> full, faded;
            for (double y : { 1.0, 0.0 })
            {
                std::vector<float> audio;
                trace (m, [y] (Script& sc) {
                    sc.at (0.0, [y] (SteelEngine& e) { e.controller (74, y, 2); });
                    sc.at (0.05, [] (SteelEngine& e) { e.setSet (kChordSets); e.noteOn (60, 0.7, 2); });
                }, 0.6, 3, &audio);
                (y > 0.5 ? full : faded) = audio;
            }
            CHECK (rms (faded, 0.3, 0.6) < 0.01 * rms (full, 0.3, 0.6), "MPE slide (CC74) down: the note fades out");
        }
    }

    std::printf ("Smart notes\n");
    {
        struct Rig
        {
            SteelEngine e;
            std::vector<float> audio;
            Rig (EngineParams q) { e.prepare (kFs); e.setParams (q); e.setSet (kSmartSet); }
            void run (double sec) { const size_t n = (size_t) (sec * kFs), at = audio.size(); audio.resize (at + n); e.process (audio.data() + at, (int) n); }
            // pitches being played (MIDI, fractional): every string under a key, which must also be ringing
            std::vector<double> pitches()
            {
                std::vector<double> p;
                for (int s = 0; s < kStrings; ++s)
                    if (e.stringKey (s) >= 0) p.push_back (e.stringLevel (s) > 1e-6 ? kE9Open[(size_t) s] + e.stringSemis (s) : -1.0);
                std::sort (p.begin(), p.end());
                return p;
            }
            bool sounds (std::initializer_list<double> want, double tol = 0.03)
            {
                auto p = pitches();
                if (p.size() != want.size()) return false;
                size_t i = 0;
                for (double w : want) if (std::fabs (p[i++] - w) > tol) return false;
                return true;
            }
        };
        EngineParams q = p;
        q.keyPc = 0; // C: the wheel's scale
        {
            Rig r (q);
            r.e.noteOn (60, 0.7);
            r.run (0.3);
            CHECK (r.sounds ({ 60 }), "one key, one note: C4 sounds C4");
            r.e.noteOff (60);
            r.run (0.4);
            CHECK (rms (r.audio, 0.6, 0.7) < 0.01 * rms (r.audio, 0.1, 0.3), "letting go blocks it");
        }
        {
            Rig r (q);
            for (int n : { 60, 64, 67 }) r.e.noteOn (n, 0.7);
            r.run (0.4);
            CHECK (r.sounds ({ 60, 64, 67 }), "C-E-G together sounds C major");
            int open = 0;
            for (int s = 0; s < kStrings; ++s)
                open += r.e.stringKey (s) >= 0 && std::fabs (r.e.stringSemis (s) - r.e.bar()) < 0.01;
            CHECK (open == 3, "on a real grip: bar at fret %d, three strings, no pedals needed", r.e.bar());
            CHECK (r.e.smartRoot() == 0 && r.e.smartQuality() == 0, "and is recognised as C major");
            // press F while E is held: the E string bends up to F (no new pick); let F go: back to E
            const size_t before = r.audio.size();
            r.e.noteOn (65, 0.7);
            r.run (0.4);
            CHECK (r.sounds ({ 60, 65, 67 }), "F pressed against the held E: that string bends to F");
            double pk = 0, pkBefore = 0;
            for (size_t i = before; i < before + 4800; ++i) pk = std::max (pk, (double) std::fabs (r.audio[i]));
            for (size_t i = before - 4800; i < before; ++i) pkBefore = std::max (pkBefore, (double) std::fabs (r.audio[i]));
            CHECK (pk < 1.3 * pkBefore, "a bend, not a new pick (peak %.2fx)", pk / pkBefore);
            r.e.noteOff (65);
            r.run (0.4);
            CHECK (r.sounds ({ 60, 64, 67 }), "F let go with E still down: it bends back, like a pedal released");
            r.e.noteOn (65, 0.7);
            r.run (0.3);
            r.e.noteOff (64);
            r.run (0.2);
            CHECK (r.sounds ({ 60, 65, 67 }), "E let go after the bend: F stays");
            // the wheel: up = third and fifth up a scale step (C F A... from C F G here: reset first)
            for (int n : { 60, 65, 67 }) r.e.noteOff (n);
            r.run (0.3);
            for (int n : { 60, 64, 67 }) r.e.noteOn (n, 0.7);
            r.run (0.2);
            r.e.pitchWheel (1.0);
            r.run (0.4);
            CHECK (r.sounds ({ 60, 65, 69 }, 0.06), "wheel up: C-E-G becomes C-F-A (I to IV)");
            r.e.pitchWheel (-1.0);
            r.run (0.4);
            CHECK (r.sounds ({ 59, 64, 67 }, 0.06), "wheel down: the root drops a step, B-E-G (I to iii)");
        }
        {
            Rig r (q);
            r.e.noteOn (60, 0.7);
            r.e.noteOn (62, 0.7);
            r.run (0.3);
            CHECK (r.sounds ({ 60, 62 }), "C and D pressed together: both sound (a chord, not a bend)");
        }
        {
            Rig r (q);
            r.e.noteOn (60, 0.7);
            r.run (0.3);
            const int bar0 = r.e.bar();
            r.e.noteOn (67, 0.7);
            r.run (0.6);
            CHECK (r.sounds ({ 67 }) && r.e.bar() == bar0 + 7, "a single line played legato: C slides up the string to G (bar +7)");
            {
                // a long line keeps its level: slides after the note has rung a while are picked lightly too
                Rig l (q);
                l.e.noteOn (60, 0.7);
                l.run (0.7);
                const int line[] = { 64, 67, 69, 67, 64, 62 };
                int prev = 60;
                for (int n : line) { l.e.noteOn (n, 0.7); l.run (0.05); l.e.noteOff (prev); l.run (0.65); prev = n; }
                const double first = rms (l.audio, 0.05, 0.6), last = rms (l.audio, 4.3, 4.85);
                CHECK (last > 0.3 * first, "a seven-note legato line doesn't die away (last note %.1f dB re the first)", db (last / first));
            }
            r.e.noteOff (67);
            r.run (0.6);
            CHECK (r.sounds ({ 60 }) && r.e.bar() == bar0, "and back when G is let go");
            EngineParams off = q;
            off.legatoSlide = false;
            off.autoBend = false;
            Rig r2 (off);
            r2.e.noteOn (60, 0.7);
            r2.run (0.3);
            r2.e.noteOn (62, 0.7);
            r2.run (0.3);
            CHECK (r2.sounds ({ 60, 62 }), "auto bend and legato slide off: every key picks its own string");
        }
        {
            EngineParams sw = q;
            sw.sweeten = 1.0;
            Rig r (sw);
            for (int n : { 60, 64, 67 }) r.e.noteOn (n, 0.7);
            r.run (0.4);
            auto pp = r.pitches();
            CHECK (pp.size() == 3 && std::fabs (pp[0] - 60.0) < 0.01 && std::fabs (pp[1] - (64 - 0.137)) < 0.01 && std::fabs (pp[2] - 67.02) < 0.01,
                   "sweetened: the third is %.1f cents and the fifth %+.1f cents from equal temperament", pp.size() == 3 ? 100 * (pp[1] - 64) : 0.0, pp.size() == 3 ? 100 * (pp[2] - 67) : 0.0);
        }
        {
            Rig r (q);
            for (int n : { 48, 55, 60, 64, 67, 72 }) r.e.noteOn (n, 0.7);
            r.run (0.4);
            CHECK (r.sounds ({ 48, 55, 60, 64, 67, 72 }), "a six-note chord across three octaves: every note sounds");
            Rig lo (q);
            lo.e.setParams ([&] { auto x = q; x.pedalKeys = false; return x; }());
            lo.e.noteOn (36, 0.7);
            lo.run (0.3);
            CHECK (lo.sounds ({ 48 }), "below the neck's range: C2 folds up an octave to C3");
            SteelEngine k;
            k.prepare (kFs);
            k.noteOn (kSmartKeyswitch, 1.0);
            CHECK (k.currentSet() == kSmartSet, "keyswitch B0 selects smart notes");
        }
        // every note of every triad and seventh chord in every inversion lands exactly
        {
            int wrong = 0, total = 0;
            for (int root = 48; root < 72; ++root)
                for (auto shape : { std::vector<int> { 0, 4, 7 }, { 0, 3, 7 }, { 0, 4, 7, 10 }, { 0, 3, 7, 10 }, { 0, 4, 7, 11 }, { 4, 7, 12 }, { 7, 12, 16 }, { 0, 7, 16 }, { 0, 5, 7 }, { 0, 2, 7 } })
                {
                    Rig r (q);
                    std::vector<double> want;
                    for (int iv : shape) { r.e.noteOn (root + iv, 0.7); want.push_back (root + iv); }
                    r.run (0.15);
                    auto got = r.pitches();
                    bool ok = got.size() == want.size();
                    for (size_t i = 0; ok && i < got.size(); ++i) ok = std::fabs (got[i] - want[i]) < 0.03;
                    wrong += ! ok;
                    ++total;
                }
            CHECK (wrong == 0, "%d chords (triads, sevenths, inversions, sus) from C3 to B4: every one sounds exactly as played (%d wrong)", total, wrong);
        }
        // stress
        {
            std::mt19937 g (3);
            std::uniform_real_distribution<double> u (0, 1);
            EngineParams sq = q;
            sq.sweeten = 1.0;
            sq.sympathetic = 1.0;
            sq.slideInBelow = 0.3;
            Rig r (sq);
            std::vector<float> buf (256);
            bool ok = true;
            double peak = 0;
            for (int blk = 0; blk < (int) (60.0 * kFs / 256); ++blk)
            {
                const double x = u (g);
                const int note = 30 + (int) (u (g) * 70);
                if (x < 0.10) r.e.noteOn (note, u (g));
                else if (x < 0.19) r.e.noteOff (note);
                else if (x < 0.22) r.e.pitchWheel (u (g) * 2 - 1);
                else if (x < 0.24) r.e.controller (64, u (g));
                else if (x < 0.25) r.e.controller (1, u (g));
                else if (x < 0.255) r.e.setSet ((int) (u (g) * kSets));
                else if (x < 0.26) r.e.setSet (kSmartSet);
                r.e.process (buf.data(), 256);
                for (float v : buf) { ok &= std::isfinite (v); peak = std::max (peak, (double) std::fabs (v)); }
            }
            CHECK (ok && peak < 4.0, "60 s of random smart-notes playing: finite, peak %.2f", peak);
        }
    }

    std::printf ("Pitch wheel: melody note\n");
    {
        EngineParams q = p;
        q.wheelMelody = true;
        auto pitches = [] (SteelEngine& e, bool chordMode) {
            std::vector<double> v;
            double top = 1e-9;
            for (int s = 0; s < kStrings; ++s) top = std::max (top, e.stringLevel (s));
            for (int s = 0; s < kStrings; ++s)
                if (chordMode ? e.stringLevel (s) > top * 1e-2 : e.stringKey (s) >= 0) v.push_back (kE9Open[(size_t) s] + e.stringSemis (s));
            std::sort (v.begin(), v.end());
            return v;
        };
        auto near = [] (const std::vector<double>& got, std::initializer_list<double> want) {
            if (got.size() != want.size()) return false;
            size_t i = 0;
            for (double w : want) if (std::fabs (got[i++] - w) > 0.06) return false;
            return true;
        };
        std::vector<float> buf ((size_t) (0.4 * kFs));
        // chord set 1 in E: C plays E major, G#3 B3 E4 G#4. The wheel moves only the top G#.
        SteelEngine e;
        e.prepare (kFs);
        e.setParams (q);
        e.noteOn (60, 0.7);
        e.process (buf.data(), (int) buf.size());
        e.pitchWheel (1.0);
        e.process (buf.data(), (int) buf.size());
        CHECK (near (pitches (e, true), { 56, 59, 64, 69 }), "chord set: wheel up takes the top note up a scale step (G# to A); the chord holds");
        e.pitchWheel (-1.0);
        e.process (buf.data(), (int) buf.size());
        CHECK (near (pitches (e, true), { 56, 59, 64, 66 }), "wheel down takes it down a scale step (G# to F#)");
        e.pitchWheel (0.5);
        e.process (buf.data(), (int) buf.size());
        auto half = pitches (e, true);
        CHECK (half.size() == 4 && half[3] > 68.3 && half[3] < 68.7, "half way is half way (%.2f)", half.size() == 4 ? half[3] : 0.0);
        // every chord of every set in every key: only one string moves, to a note of the key's scale
        {
            int bad = 0;
            for (int si = 0; si < kChordSets; ++si)
                for (int key = 0; key < 12; ++key)
                    for (int slot = 0; slot < 12; ++slot)
                    {
                        SteelEngine c;
                        c.prepare (kFs);
                        EngineParams k = q;
                        k.keyPc = key;
                        c.setParams (k);
                        c.setSet (si);
                        std::vector<float> b (480);
                        c.noteOn (60 + slot, 0.7);
                        c.process (b.data(), 480);
                        double rest[kStrings];
                        for (int s = 0; s < kStrings; ++s) rest[s] = c.stringSemis (s);
                        c.pitchWheel (1.0);
                        for (int i = 0; i < 20; ++i) c.process (b.data(), 480);
                        int moved = 0;
                        bool inScale = true;
                        for (int s = 0; s < kStrings; ++s)
                            if (std::fabs (c.stringSemis (s) - rest[s]) > 0.05)
                            {
                                ++moved;
                                const int pc = (((int) std::lround (kE9Open[(size_t) s] + c.stringSemis (s)) - key) % 12 + 12) % 12;
                                inScale &= pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
                            }
                        bad += moved != 1 || ! inScale;
                    }
            CHECK (bad == 0, "all 8 sets x 12 keys x 12 chords: exactly one string moves, onto the key's scale (%d wrong)", bad);
        }
        // smart notes in C: C-E-G, the G moves
        EngineParams qc = q;
        qc.keyPc = 0;
        SteelEngine sm;
        sm.prepare (kFs);
        sm.setParams (qc);
        sm.setSet (kSmartSet);
        for (int n : { 60, 64, 67 }) sm.noteOn (n, 0.7);
        sm.process (buf.data(), (int) buf.size());
        sm.pitchWheel (1.0);
        sm.process (buf.data(), (int) buf.size());
        CHECK (near (pitches (sm, false), { 60, 64, 69 }), "smart notes: C-E-G, wheel up: G to A, C and E hold");
        sm.pitchWheel (-1.0);
        sm.process (buf.data(), (int) buf.size());
        CHECK (near (pitches (sm, false), { 60, 64, 65 }), "wheel down: G to F");
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

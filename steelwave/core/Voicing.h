#pragma once

// Steelwave: the E9 copedent, and the chord and single-note sets that sit on top of it (no JUCE).
//
// Keyboard layout (relative to the chosen key, like a C-major keyboard):
//   chord sets   white keys  = diatonic triads I ii iii IV V vi vii°
//                black keys  = I (other voicing), II, iv, #V°, bVII
//                pitch wheel = up: the chord a 4th up (I->IV, V->I ...); down: the chord a 3rd up,
//                              reached by lowering the root (I->iii, IV->vi ...). All diatonic.
//   single sets  white keys  = the major scale, bending to the next/previous scale note
//                black keys  = the white key below, not bending (bend some strings, hold others)
//
// Every voicing is found on the real E9 neck: one bar position for the whole chord, strings from
// the set's string group, and the standard pedals and knee levers. Bends use a real pedal/lever
// combination when one gets there; otherwise each string moves (by at most 2 semitones) to the
// nearest note of the target chord, like a custom changer would.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace sw
{
constexpr int kStrings = 10;
constexpr std::array<int, kStrings> kE9Open = { 66, 63, 68, 64, 59, 56, 54, 52, 50, 47 }; // F# D# G# E B G# F# E D B

// --- copedent ---------------------------------------------------------------------------
enum Control { PedA, PedB, PedC, LKL, LKR, RKL, RKR, kControls };
inline const char* controlName (int c)
{
    static const char* n[] = { "A", "B", "C", "LKL", "LKR", "RKL", "RKR" };
    return n[c];
}
// semitones each control moves each string (strings 1..10 -> index 0..9)
inline const std::array<std::array<int, kStrings>, kControls>& e9Copedent()
{
    static const std::array<std::array<int, kStrings>, kControls> c = { {
        { 0, 0, 0, 0, 2, 0, 0, 0, 0, 2 },   // A:   B -> C# on 5, 10
        { 0, 0, 1, 0, 0, 1, 0, 0, 0, 0 },   // B:   G# -> A on 3, 6
        { 0, 0, 0, 2, 2, 0, 0, 0, 0, 0 },   // C:   E -> F# on 4, B -> C# on 5
        { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0 },   // LKL: E -> F on 4, 8
        { 0, 0, 0, -1, 0, 0, 0, -1, 0, 0 }, // LKR: E -> D# on 4, 8
        { 1, 0, 0, 0, 0, 0, 1, 0, 0, 0 },   // RKL: F# -> G on 1, 7
        { 0, -1, 0, 0, 0, 0, 0, 0, -1, 0 }, // RKR: D# -> D on 2, D -> C# on 9
    } };
    return c;
}

// controls engaged, as a bit mask; per-string offset = largest raise + largest lower (pulls don't add)
inline std::array<int, kStrings> copedentOffsets (uint32_t mask)
{
    std::array<int, kStrings> off {};
    const auto& c = e9Copedent();
    for (int s = 0; s < kStrings; ++s)
    {
        int up = 0, dn = 0;
        for (int k = 0; k < kControls; ++k)
            if (mask & (1u << k))
            {
                up = std::max (up, c[(size_t) k][(size_t) s]);
                dn = std::min (dn, c[(size_t) k][(size_t) s]);
            }
        off[(size_t) s] = up + dn;
    }
    return off;
}
// Pitch part-way through a pull: a pedal changes tension in proportion to its travel, and pitch goes
// with the square root of tension, so a whole-step pull is a little past a semitone half way.
inline double tensionGlide (double fromSemis, double toSemis, double travel)
{
    const double ra = std::exp2 (fromSemis / 6.0), rb = std::exp2 (toSemis / 6.0); // tension ratios (f^2)
    return 6.0 * std::log2 (std::max (ra + (rb - ra) * std::clamp (travel, 0.0, 1.0), 1e-6));
}

// continuous version (each control 0..1 of its travel)
inline std::array<double, kStrings> copedentOffsets (const std::array<double, kControls>& travel)
{
    std::array<double, kStrings> off {};
    const auto& c = e9Copedent();
    for (int s = 0; s < kStrings; ++s)
    {
        double up = 0, dn = 0;
        for (int k = 0; k < kControls; ++k)
        {
            const double v = tensionGlide (0.0, c[(size_t) k][(size_t) s], travel[(size_t) k]);
            up = std::max (up, v);
            dn = std::min (dn, v);
        }
        off[(size_t) s] = up + dn;
    }
    return off;
}

// What a player can engage at once: one or two adjacent foot pedals (A, B, C, A+B, B+C), one
// lever per knee.
inline const std::vector<uint32_t>& playableCombos()
{
    static const std::vector<uint32_t> v = [] {
        std::vector<uint32_t> r;
        const uint32_t feet[] = { 0, 1u << PedA, 1u << PedB, 1u << PedC, (1u << PedA) | (1u << PedB), (1u << PedB) | (1u << PedC) };
        const uint32_t lk[] = { 0, 1u << LKL, 1u << LKR };
        const uint32_t rk[] = { 0, 1u << RKL, 1u << RKR };
        for (auto f : feet)
            for (auto l : lk)
                for (auto k : rk)
                    r.push_back (f | l | k);
        return r;
    }();
    return v;
}
inline int controlCount (uint32_t m)
{
    int n = 0;
    for (; m; m &= m - 1) ++n;
    return n;
}
inline std::string comboName (uint32_t m)
{
    if (m == 0) return "open";
    std::string s;
    for (int k = 0; k < kControls; ++k)
        if (m & (1u << k))
            s += (s.empty() ? "" : "+") + std::string (controlName (k));
    return s;
}

// --- chords -----------------------------------------------------------------------------
enum Quality { Maj, Min, Dim };
struct ChordName { int root; Quality q; }; // root in semitones above the key
inline uint32_t chordPcs (ChordName c, int keyPc) // bit set of pitch classes
{
    const int third = c.q == Maj ? 4 : 3, fifth = c.q == Dim ? 6 : 7;
    const int r = (keyPc + c.root) % 12;
    return (1u << r) | (1u << ((r + third) % 12)) | (1u << ((r + fifth) % 12));
}

// keyboard pitch class (C = 0) -> chord, wheel-up chord, wheel-down chord
struct Slot { ChordName rest, up, down; bool variant; };
inline const std::array<Slot, 12>& chordSlots()
{
    static const std::array<Slot, 12> s = { {
        { { 0, Maj }, { 5, Maj }, { 4, Min }, false },  // C : I    up IV   down iii
        { { 0, Maj }, { 5, Maj }, { 4, Min }, true },   // C#: I (another voicing)
        { { 2, Min }, { 7, Maj }, { 5, Maj }, false },  // D : ii   up V    down IV
        { { 2, Maj }, { 7, Maj }, { 2, Min }, false },  // D#: II   up V    down ii
        { { 4, Min }, { 9, Min }, { 7, Maj }, false },  // E : iii  up vi   down V
        { { 5, Maj }, { 7, Maj }, { 9, Min }, false },  // F : IV   up V    down vi
        { { 5, Min }, { 5, Maj }, { 0, Maj }, false },  // F#: iv   up IV   down I
        { { 7, Maj }, { 0, Maj }, { 2, Min }, false },  // G : V    up I    down ii
        { { 8, Dim }, { 9, Min }, { 7, Maj }, false },  // G#: #V°  up vi   down V
        { { 9, Min }, { 2, Min }, { 0, Maj }, false },  // A : vi   up ii   down I
        { { 10, Maj }, { 0, Maj }, { 9, Min }, false }, // A#: bVII up I    down vi
        { { 11, Dim }, { 4, Min }, { 2, Min }, false }, // B : vii° up iii  down ii
    } };
    return s;
}

struct ChordSetDef
{
    const char* name;
    std::vector<int> pool; // string indices 0..9
    int minSize, maxSize, barLo, barHi;
    double strumScale;     // pick spread multiplier
};
inline const std::array<ChordSetDef, 8>& chordSetDefs()
{
    static const std::array<ChordSetDef, 8> d = { {
        { "Classic", { 2, 3, 4, 5 }, 3, 4, 0, 11, 1.0 },
        { "Full", { 2, 3, 4, 5, 7, 9 }, 5, 6, 0, 11, 1.3 },
        { "High", { 0, 1, 2, 3, 4 }, 3, 4, 0, 11, 1.0 },
        { "Low", { 4, 5, 6, 7, 8, 9 }, 3, 4, 0, 11, 1.0 },
        { "Close", { 2, 3, 4, 5 }, 3, 3, 0, 11, 0.8 },
        { "Octave up", { 2, 3, 4, 5 }, 3, 4, 12, 22, 1.0 },
        { "Wide", { 2, 4, 5, 7, 9 }, 3, 4, 0, 11, 1.2 },
        { "Pad", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 }, 6, 8, 0, 11, 3.0 },
    } };
    return d;
}

// One playable chord: bar position (key of E), strings, and each string's offset at rest / bent.
struct ChordVoicing
{
    int bar = 0;
    std::vector<int> strings;
    std::array<double, kStrings> rest {}, up {}, down {};
    uint32_t restCombo = 0;
    std::string upHow, downHow; // e.g. "A+B" or "voice-led"
    double meanPitch = 0;       // average MIDI pitch of the strings at rest (key of E, bar as found)
};

namespace detail
{
inline int lowestPc (const std::vector<int>& strs, int bar, const std::array<int, kStrings>& off)
{
    int lo = 1000;
    for (int s : strs)
        lo = std::min (lo, kE9Open[(size_t) s] + bar + off[(size_t) s]);
    return lo % 12;
}
inline uint32_t pcsOf (const std::vector<int>& strs, int bar, const std::array<double, kStrings>& off)
{
    uint32_t m = 0;
    for (int s : strs)
        m |= 1u << ((kE9Open[(size_t) s] + bar + (int) std::lround (off[(size_t) s]) + 1200) % 12);
    return m;
}

// Best way to move `strs` (at `bar`, from `rest`) to chord `tgt`. dir: +1 prefers raising.
inline double bestTarget (const std::vector<int>& strs, int bar, const std::array<double, kStrings>& rest, ChordName tgt,
                          int keyPc, int dir, std::array<double, kStrings>& out, std::string& how)
{
    const uint32_t want = chordPcs (tgt, keyPc);
    const int root = (keyPc + tgt.root) % 12, third = (root + (tgt.q == Maj ? 4 : 3)) % 12;
    double best = 1e9;
    // real pedal / lever combinations
    for (uint32_t m : playableCombos())
    {
        const auto o = copedentOffsets (m);
        std::array<double, kStrings> od {};
        for (int s = 0; s < kStrings; ++s)
            od[(size_t) s] = o[(size_t) s];
        const uint32_t got = pcsOf (strs, bar, od);
        if ((got & ~want) != 0 || ! (got & (1u << root)) || ! (got & (1u << third)))
            continue;
        int moving = 0;
        double motion = 0;
        for (int s : strs)
        {
            const double d = od[(size_t) s] - rest[(size_t) s];
            moving += d != 0;
            motion += d * dir;
        }
        if (moving == 0)
            continue;
        const double c = 1.0 * controlCount (m) + 0.3 * moving + (got == want ? 0.0 : 1.5) + (motion < 0 ? 0.8 : 0.0);
        if (c < best)
        {
            best = c;
            out = od;
            how = comboName (m);
        }
    }
    // voice-led: each string to the nearest target note within 2 semitones
    {
        std::array<double, kStrings> od = rest;
        double motion = 0;
        bool ok = true;
        for (int s : strs)
        {
            const int p = kE9Open[(size_t) s] + bar + (int) rest[(size_t) s];
            // nearest target note, but moving with the wheel is worth 1.5 semitones (a bend up should
            // mostly go up: A F# D -> B G# E, not -> G# G# E)
            int bestD = 99;
            double bestC = 1e9;
            for (int d = -2; d <= 2; ++d)
                if (want & (1u << ((p + d + 1200) % 12)))
                {
                    const double c = std::abs (d) + (d * dir < 0 ? 1.5 : 0.0);
                    if (c < bestC) { bestC = c; bestD = d; }
                }
            if (bestD == 99) { ok = false; break; }
            od[(size_t) s] = rest[(size_t) s] + bestD;
            motion += std::abs (bestD);
        }
        if (ok)
        {
            const uint32_t got = pcsOf (strs, bar, od);
            if ((got & (1u << root)) && (got & (1u << third)) && motion > 0)
            {
                const double c = 4.0 + 0.3 * motion + (got == want ? 0.0 : 1.5);
                if (c < best)
                {
                    best = c;
                    out = od;
                    how = "voice-led";
                }
            }
        }
    }
    if (best > 1e8)
    {
        out = rest;
        how = "none";
        best = 20.0;
    }
    return best;
}

constexpr double kRegisterWeight = 1.5; // cost per semitone a voicing sits off its key's place in the register
constexpr double kOrderPenalty = 500.0; // a key's chord not above the key below it: only if nothing else fits

struct Candidate { ChordVoicing v; double cost; };

// The best `keep` voicings of a chord, cheapest first. targetMean >= 0 pulls the voicing's average
// pitch toward it (so chords climb with the keyboard); avoid: don't repeat this voicing.
inline std::vector<Candidate> searchCandidates (const ChordSetDef& def, const Slot& slot, const ChordVoicing* avoid,
                                               double targetMean, size_t keep)
{
    std::vector<Candidate> found; // sorted by cost, at most `keep`
    auto threshold = [&] { return found.size() < keep ? 1e18 : found.back().cost; };
    constexpr int keyPc = 4; // voicings are found in E (bar 0 = E); other keys move the bar
    const uint32_t want = chordPcs (slot.rest, keyPc);
    const int root = (keyPc + slot.rest.root) % 12;
    // chords placed by register get a few more frets to climb into
    const int barHi = targetMean >= 0 ? std::min (def.barHi + 9, 22) : def.barHi;
    for (int bar = def.barLo; bar <= barHi; ++bar)
    {

        for (uint32_t m : playableCombos())
        {
            if (controlCount (m) > 2)
                continue;
            const auto o = copedentOffsets (m);
            std::vector<int> valid;
            for (int s : def.pool)
                if (want & (1u << ((kE9Open[(size_t) s] + bar + o[(size_t) s]) % 12)))
                    valid.push_back (s);
            const int nv = (int) valid.size();
            if (nv < def.minSize)
                continue;
            for (uint32_t sub = 1; sub < (1u << nv); ++sub)
            {
                const int sz = controlCount (sub);
                if (sz < def.minSize || sz > def.maxSize)
                    continue;
                std::vector<int> strs;
                for (int i = 0; i < nv; ++i)
                    if (sub & (1u << i))
                        strs.push_back (valid[(size_t) i]);
                std::array<double, kStrings> od {};
                for (int s = 0; s < kStrings; ++s)
                    od[(size_t) s] = o[(size_t) s];
                if (pcsOf (strs, bar, od) != want)
                    continue;
                // pitches, low to high: no huge gaps, root in the bass
                std::vector<int> p;
                for (int s : strs)
                    p.push_back (kE9Open[(size_t) s] + bar + o[(size_t) s]);
                std::sort (p.begin(), p.end());
                double gaps = 0;
                bool unison = false;
                for (size_t i = 1; i < p.size(); ++i)
                {
                    gaps += std::max (0, p[i] - p[i - 1] - 9) * 0.5;
                    unison |= p[i] == p[i - 1];
                }
                if (unison)
                    continue;
                // skipped strings inside the grip are awkward to pick around
                int skipped = 0;
                for (size_t i = 1; i < strs.size(); ++i)
                    for (int s : def.pool)
                        skipped += s > strs[i - 1] && s < strs[i];
                double cost = 2.5 * controlCount (m) + 0.12 * (bar - def.barLo) + (p.front() % 12 != root ? 2.0 : 0.0) + gaps
                              + 0.8 * (def.maxSize - sz) + 0.4 * skipped;
                double mean = 0;
                for (int q : p)
                    mean += q;
                mean /= (double) p.size();
                if (avoid && p.size() == avoid->strings.size())
                {
                    // the same notes as the voicing to avoid (whatever pedals are down): not a new voicing
                    std::vector<int> q;
                    for (int s : avoid->strings)
                        q.push_back (kE9Open[(size_t) s] + avoid->bar + (int) avoid->rest[(size_t) s]);
                    std::sort (q.begin(), q.end());
                    if (q == p)
                        continue;
                }
                if (targetMean >= 0)
                    cost += kRegisterWeight * std::fabs (mean - targetMean);
                if (cost >= threshold())
                    continue;
                ChordVoicing v;
                v.bar = bar;
                v.strings = strs;
                v.rest = od;
                v.restCombo = m;
                v.meanPitch = mean;
                cost += bestTarget (strs, bar, od, slot.up, keyPc, +1, v.up, v.upHow);
                cost += bestTarget (strs, bar, od, slot.down, keyPc, -1, v.down, v.downHow);
                if (cost < threshold())
                {
                    // the same notes reached another way (a pedal that touches none of these strings) is
                    // the same voicing: keep only the cheapest, so the list holds different voicings
                    auto same = std::find_if (found.begin(), found.end(), [&] (const Candidate& x) {
                        if (x.v.strings != strs || x.v.bar != bar)
                            return false;
                        for (int q : strs)
                            if (x.v.rest[(size_t) q] != od[(size_t) q])
                                return false;
                        return true;
                    });
                    if (same != found.end())
                    {
                        if (same->cost <= cost)
                            continue;
                        found.erase (same);
                    }
                    auto at = std::upper_bound (found.begin(), found.end(), cost, [] (double c, const Candidate& x) { return c < x.cost; });
                    found.insert (at, Candidate { v, cost });
                    if (found.size() > keep)
                        found.pop_back();
                }
            }
        }
    }
    return found;
}

inline ChordVoicing searchChord (const ChordSetDef& def, const Slot& slot, const ChordVoicing* avoid = nullptr, double targetMean = -1.0)
{
    auto c = searchCandidates (def, slot, avoid, targetMean, 1);
    return c.empty() ? ChordVoicing {} : c.front().v;
}

// All 12 keys of a set, chosen together: I where it sits most naturally, then for every other key one
// of its best voicings, such that each key's chord sits above the key below it (the cheapest such
// sequence, by dynamic programming over the candidates)
inline std::array<ChordVoicing, 12> searchSet (const ChordSetDef& def)
{
    std::array<ChordVoicing, 12> out {};
    out[0] = searchChord (def, chordSlots()[0]);
    const double home = out[0].meanPitch;
    std::array<std::vector<Candidate>, 12> cand;
    cand[0] = { Candidate { out[0], 0.0 } };
    for (size_t k = 1; k < 12; ++k)
        cand[k] = searchCandidates (def, chordSlots()[k], chordSlots()[k].variant ? &out[0] : nullptr, home + (double) k, 150);
    // the alternate I (a variant, C#) sits near I but outside the climb: D follows straight on from C
    std::vector<size_t> chain;
    for (size_t k = 0; k < 12; ++k)
        if (chordSlots()[k].variant)
            out[k] = cand[k].empty() ? out[0] : cand[k].front().v;
        else
            chain.push_back (k);
    std::vector<std::vector<double>> total (chain.size());
    std::vector<std::vector<int>> from (chain.size());
    total[0] = { 0.0 };
    from[0] = { -1 };
    for (size_t c = 1; c < chain.size(); ++c)
    {
        const auto& now = cand[chain[c]];
        const auto& prev = cand[chain[c - 1]];
        total[c].assign (now.size(), 1e18);
        from[c].assign (now.size(), 0);
        for (size_t j = 0; j < now.size(); ++j)
            for (size_t i = 0; i < prev.size(); ++i)
            {
                const bool climbs = now[j].v.meanPitch > prev[i].v.meanPitch + 0.25;
                const double t = total[c - 1][i] + now[j].cost + (climbs ? 0.0 : kOrderPenalty);
                if (t < total[c][j])
                {
                    total[c][j] = t;
                    from[c][j] = (int) i;
                }
            }
    }
    int j = (int) (std::min_element (total.back().begin(), total.back().end()) - total.back().begin());
    for (size_t c = chain.size() - 1; c >= 1; --c)
    {
        out[chain[c]] = cand[chain[c]][(size_t) j].v;
        j = from[c][(size_t) j];
    }
    return out;
}
} // namespace detail

// All chord sets (8 sets x 12 keys), found once per process.
inline const std::array<std::array<ChordVoicing, 12>, 8>& chordSets()
{
    static const std::array<std::array<ChordVoicing, 12>, 8> all = [] {
        std::array<std::array<ChordVoicing, 12>, 8> a {};
        for (size_t si = 0; si < 8; ++si)
        {
            // I first, where it sits most naturally; every other key then aims a semitone higher per key
            // up the keyboard (C# = I + 1, D = I + 2 ... B = I + 11), so the chords climb as you go up
            // the keys, the way block chords do on a keyboard
            a[si] = detail::searchSet (chordSetDefs()[si]);
        }
        return a;
    }();
    return all;
}

// --- keys -------------------------------------------------------------------------------
// Bar shift for a key (pitch class, C = 0) relative to E, kept within -4..+7 frets.
inline int keyShift (int keyPc)
{
    int t = ((keyPc - 4) % 12 + 12) % 12;
    return t > 7 ? t - 12 : t;
}
// the highest bar position: the neck is marked to 24, and the bar can go a couple of frets past it (the
// string is still ~13 cm long there, beyond the pick and the pickup), so every set fits in every key
constexpr int kTopFret = 26;

// How far a whole chord set moves for a key: all 12 chords together (so they keep climbing up the
// keyboard), by the key's shift from E, or an octave either side of it, whichever keeps every chord
// on the neck (frets 0-26).
inline int setBarShift (int setIndex, int keyPc)
{
    const auto& set = chordSets()[(size_t) setIndex];
    int lo = 99, hi = -99;
    for (const auto& v : set)
    {
        lo = std::min (lo, v.bar);
        hi = std::max (hi, v.bar);
    }
    const int t = keyShift (keyPc);
    for (int s : { t, t + 12, t - 12 })
        if (lo + s >= 0 && hi + s <= kTopFret)
            return s;
    return t; // can't all fit: chords off the neck get wrapped individually
}

// The bar for a chord: the set's key shift, plus an octave up or down for the octave played
// (MIDI 60-71 is home; 72 and up an octave higher, 59 and below an octave lower) where it fits.
inline int chordBar (int setIndex, const ChordVoicing& v, int keyPc, int note)
{
    const int base = v.bar + setBarShift (setIndex, keyPc);
    const int oct = note < 0 ? 0 : std::clamp (note / 12 - 5, -1, 1);
    for (int o : { oct, 0 })
        if (base + 12 * o >= 0 && base + 12 * o <= kTopFret)
            return base + 12 * o;
    int b = base;
    while (b < 0) b += 12;
    while (b > kTopFret) b -= 12;
    return b;
}

inline int fitBar (int bar)
{
    while (bar < 0) bar += 12;
    while (bar > 22) bar -= 12;
    return bar;
}

// --- single-note sets ----------------------------------------------------------------------
struct SingleSetDef { const char* name; int baseBar; int fixedBend; }; // fixedBend 0 = to the next scale note
inline const std::array<SingleSetDef, 4>& singleSetDefs()
{
    static const std::array<SingleSetDef, 4> d = { {
        { "Scale", 0, 0 },
        { "Scale high", 12, 0 },
        { "Whole-step bends", 0, 2 },
        { "Half-step bends", 0, 1 },
    } };
    return d;
}

struct SingleNote { int string = -1; double offset = 0, bendUp = 0, bendDown = 0; bool bends = true; int pitch = 0; };

// Where every scale note lives at one bar position: each string open or moved by what its
// pedals/levers can do (most players play scales this way, at one bar position).
class SingleMap
{
public:
    void build (int setIndex, int keyPc)
    {
        const auto& def = singleSetDefs()[(size_t) setIndex];
        fixedBend_ = def.fixedBend;
        keyPc_ = keyPc;
        bar_ = fitBar (def.baseBar + keyShift (keyPc));
        // offsets each string can reach with real controls
        std::array<std::vector<int>, kStrings> reach;
        for (uint32_t m : playableCombos())
        {
            const auto o = copedentOffsets (m);
            for (int s = 0; s < kStrings; ++s)
                if (std::find (reach[(size_t) s].begin(), reach[(size_t) s].end(), o[(size_t) s]) == reach[(size_t) s].end())
                    reach[(size_t) s].push_back (o[(size_t) s]);
        }
        for (auto& e : table_)
            e = {};
        for (auto& e : alt_)
            e = {};
        lo_ = 200;
        hi_ = -1;
        for (int s = 0; s < kStrings; ++s)
            for (int off = -2; off <= 3; ++off)
            {
                const int p = kE9Open[(size_t) s] + bar_ + off;
                if (! inScale (p) || p < 0 || p >= 128)
                    continue;
                // prefer open strings, then raises, then lowers; then the middle strings. Pulls the
                // real E9 can't make are only used for the held black-key notes (cost >= 10)
                const bool real = std::find (reach[(size_t) s].begin(), reach[(size_t) s].end(), off) != reach[(size_t) s].end();
                const double cost = (off == 0 ? 0.0 : off > 0 ? 1.0 + 0.2 * off : 2.0) + 0.05 * std::abs (s - 4) + (real ? 0.0 : 10.0);
                auto& best = table_[(size_t) p];
                auto& alt = alt_[(size_t) p];
                SingleNote cand;
                cand.string = s;
                cand.offset = off;
                cand.pitch = p;
                if (best.string < 0 || cost < cost_[(size_t) p])
                {
                    if (best.string >= 0 && best.string != s)
                    {
                        alt = best;
                        altCost_[(size_t) p] = cost_[(size_t) p];
                    }
                    best = cand;
                    cost_[(size_t) p] = cost;
                }
                else if (s != best.string && (alt.string < 0 || cost < altCost_[(size_t) p]))
                {
                    alt = cand;
                    altCost_[(size_t) p] = cost;
                }
                if (real)
                {
                    lo_ = std::min (lo_, p);
                    hi_ = std::max (hi_, p);
                }
            }
        for (int p = 0; p < 128; ++p)
            if (table_[(size_t) p].string >= 0)
            {
                auto& e = table_[(size_t) p];
                e.bendUp = fixedBend_ ? fixedBend_ : nextScale (p, +1) - p;
                e.bendDown = fixedBend_ ? -fixedBend_ : nextScale (p, -1) - p;
            }
    }

    int bar() const { return bar_; }

    // MIDI note -> string, rest offset and bend; string < 0 if nothing fits
    SingleNote lookup (int midiNote) const
    {
        static const int white[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
        static const int scale[7] = { 0, 2, 4, 5, 7, 9, 11 };
        const int pc = midiNote % 12;
        const bool black = white[pc] < 0;
        const int d = white[black ? pc - 1 : pc];
        // C3 plays the key's root nearest E3, C4 the octave above ...
        int target = 52 + keyShift (keyPc_) + scale[d] + 12 * (midiNote / 12 - 4);
        while (target > hi_ && target - 12 >= 0) target -= 12;
        while (target < lo_) target += 12;
        SingleNote n;
        if (target >= 0 && target < 128 && table_[(size_t) target].string >= 0)
            n = table_[(size_t) target];
        // black keys: the same note held still, on another string when there is one, so it can
        // sound together with the bending white key
        if (black)
        {
            if (target >= 0 && target < 128 && alt_[(size_t) target].string >= 0)
                n = alt_[(size_t) target];
            n.bends = false;
        }
        return n;
    }

private:
    bool inScale (int p) const
    {
        static const bool sc[12] = { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 };
        return sc[((p - keyPc_) % 12 + 12) % 12];
    }
    int nextScale (int p, int dir) const
    {
        for (int q = p + dir;; q += dir)
            if (inScale (q))
                return q;
    }
    std::array<SingleNote, 128> table_ {}, alt_ {};
    std::array<double, 128> cost_ {}, altCost_ {};
    int bar_ = 0, keyPc_ = 4, fixedBend_ = 0, lo_ = 0, hi_ = 127;
};
} // namespace sw

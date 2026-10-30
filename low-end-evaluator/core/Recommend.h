#pragma once

// Recommendations: each rule fires on measured deviations from the profile, takes its parameters from the measurement,
// and predicts what the fix will do.
//
// Linear fixes (EQ, high-pass, mono below a frequency) are simulated: the fix is applied to a copy of the audio, which is
// analysed and scored again, so the predicted metric changes and composite gain are measured, not guessed. Dynamics and
// saturation advice is estimated (the targeted metrics moved to the edge of their band) and labelled as such.
//
// Rules fire at d >= 1 (a sigma outside the band), not at the band edge: with band = interquartile range, a quarter of
// the references themselves sit outside it on any one metric. Fixes are ordered: stereo/phase, then high-pass, then EQ,
// then dynamics, then saturation; the combined prediction applies all simulated fixes in that order.

#include "Scoring.h"

#include <cstdarg>
#include <functional>
#include <tuple>

namespace lee
{
struct Fix
{
    enum Kind { None, Peak, LowShelf, HighPass, MonoBelow };
    Kind kind = None;
    double hz = 0, gainDb = 0, q = 0.707;
};

struct Rec
{
    std::string id, title, diagnosis, advice;
    int category = Clarity;
    Fix fix;                          // simulated when kind != None
    std::vector<std::string> metrics; // the metrics it targets
    int effort = 2, order = 3;        // effort: 1 switch or nudge, 2 one plug-in move, 3 rebalance across tracks
    double severity = 0, conf = 1, delta = 0, deltaUncapped = 0, priority = 0, after = 0;
    bool simulated = false;
    std::vector<std::tuple<std::string, double, double>> predicted; // metric, now, after
};

inline std::string noteName (double hz)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int m = (int) std::lround (69 + 12 * std::log2 (hz / 440.0));
    return std::string (n[((m % 12) + 12) % 12]) + std::to_string (m / 12 - 1);
}

inline std::string fmt (const char* f, ...)
{
    char buf[512];
    va_list ap;
    va_start (ap, f);
    std::vsnprintf (buf, sizeof buf, f, ap);
    va_end (ap);
    return buf;
}

inline Stereo applyFix (const Stereo& in, const Fix& f)
{
    Stereo o = in;
    const double fs = in.fs;
    Chain c;
    switch (f.kind)
    {
        case Fix::None: return o;
        case Fix::Peak: c.push_back (Biquad::peak (f.hz, f.q, f.gainDb, fs)); break;
        case Fix::LowShelf: c.push_back (Biquad::shelf (true, f.hz, f.gainDb, fs)); break;
        case Fix::HighPass: c = butterworth (true, 4, f.hz, fs); break;
        case Fix::MonoBelow:
        {
            // M/S, side through a Linkwitz-Riley high-pass (two Butterworth 2nd-order): what a "mono below X" tool does
            std::vector<float> side (in.size());
            for (size_t i = 0; i < in.size(); ++i)
                side[i] = 0.5f * (in.L[i] - in.R[i]);
            Chain lr = butterworth (true, 2, f.hz, fs);
            lr.push_back (lr[0]);
            side = filtered (side, lr);
            for (size_t i = 0; i < in.size(); ++i)
            {
                const float mid = 0.5f * (in.L[i] + in.R[i]);
                o.L[i] = mid + side[i];
                o.R[i] = mid - side[i];
            }
            return o;
        }
    }
    o.L = filtered (in.L, c);
    o.R = filtered (in.R, c);
    return o;
}

inline std::string describe (const Fix& f)
{
    switch (f.kind)
    {
        case Fix::Peak: return fmt ("bell %+.1f dB at %.0f Hz, Q %.1f", f.gainDb, f.hz, f.q);
        case Fix::LowShelf: return fmt ("low shelf %+.1f dB below %.0f Hz", f.gainDb, f.hz);
        case Fix::HighPass: return fmt ("high-pass %.0f Hz, 24 dB/oct", f.hz);
        case Fix::MonoBelow: return fmt ("mono below %.0f Hz (side channel high-passed, 24 dB/oct)", f.hz);
        default: return {};
    }
}

struct RecommendOptions
{
    Stage stage = Stage::Mix;
    bool simulate = true;
    std::function<void (const std::string&)> progress; // optional: told about each simulation
};

struct Recommendations
{
    std::vector<Rec> recs;         // by priority
    bool combinedValid = false;
    Score combined;                // all simulated fixes applied in order
};

inline Recommendations recommend (const Stereo& audio, const Analysis& an, const Profile& pr, const Score& sc, const RecommendOptions& opt = {})
{
    Recommendations out;
    std::vector<Rec> recs;
    auto ms = [&] (const std::string& id) { return sc.find (id); };
    auto fires = [&] (const std::string& id, int side) { // d >= 1 on the given side, not muted, trusted
        auto* m = ms (id);
        return m && m->used && m->d >= 1.0 && (side == 0 || m->side == side);
    };
    auto target = [&] (const std::string& id) { auto it = pr.targets.find (id); return it == pr.targets.end() ? Target {} : it->second; };
    auto v = [&] (const std::string& id) { return an.get (id).v; };

    // 0: clipping and overs (gates)
    if (v ("T2") > 0 || (opt.stage == Stage::Master && v ("T1") > 0))
    {
        Rec r;
        r.id = "clipping";
        r.title = "Clipping";
        r.diagnosis = fmt ("%.0f clipped run(s) or inter-sample over(s); true peak %+.1f dBTP.", v ("T2"), v ("T1"));
        r.advice = opt.stage == Stage::Master ? "Set the limiter ceiling to -1 dBTP with true-peak detection on."
                                              : "Lower the master fader or the last gain stage until the bounce peaks below -1 dBTP; leave limiting to mastering.";
        r.category = Dynamics;
        r.effort = 1;
        r.order = 0;
        r.severity = 10;
        recs.push_back (r);
    }
    // 1: stereo low end
    if ((fires ("S1", -1) || fires ("S2", -1) || fires ("S3", 1)) && an.monoBelowHz > 0)
    {
        Rec r;
        r.id = "mono";
        r.title = "Low end is not mono-safe";
        r.diagnosis = fmt ("Correlation drops to %.2f (5th pct), mono fold-down loses %.1f dB, side is %.1f dB re mid below 120 Hz; "
                           "bands up to %.0f Hz are wide.", v ("S2"), v ("S1"), v ("S3"), an.monoBelowHz);
        r.fix = { Fix::MonoBelow, an.monoBelowHz, 0, 0 };
        r.metrics = { "S1", "S2", "S3" };
        r.effort = 1;
        r.order = 1;
        recs.push_back (r);
    }
    // 2: infrasonic rumble
    if (fires ("T4", 1))
    {
        Rec r;
        r.id = "infrasonic";
        r.title = "Infrasonic energy wasting headroom";
        r.diagnosis = fmt ("Energy below 25 Hz is %.1f dB re 25-120 Hz (references: %.1f dB).", v ("T4"), target ("T4").center);
        r.fix = { Fix::HighPass, 25.0, 0, 0 };
        r.advice = "On the master, or better on the sources that carry the rumble (bass, kick, pads).";
        r.metrics = { "T4" };
        r.effort = 1;
        r.order = 2;
        r.category = Dynamics;
        recs.push_back (r);
    }
    // 3: tonal balance against the references' curve (20-500 Hz), clustered into EQ moves
    {
        const auto& cf = curveFreqs();
        std::vector<double> dev (cf.size(), 0.0);
        for (size_t k = 0; k < cf.size() && k < an.curve.size(); ++k)
            if (cf[k] <= 500 && std::isfinite (pr.curveMed[k]))
                dev[k] = an.curve[k] - std::clamp (an.curve[k], pr.curveLo[k], pr.curveHi[k]);
        struct Cl { size_t a, b; double peak, mean, mass; };
        std::vector<Cl> cls;
        for (size_t k = 0; k < dev.size();)
        {
            if (std::fabs (dev[k]) < 1.5)
            {
                ++k;
                continue;
            }
            const double sg = dev[k] > 0 ? 1 : -1;
            Cl c { k, k, 0, 0, 0 };
            while (k < dev.size() && std::fabs (dev[k]) >= 1.5 && dev[k] * sg > 0)
            {
                c.b = k;
                if (std::fabs (dev[k]) > std::fabs (c.peak))
                    c.peak = dev[k];
                c.mean += dev[k];
                c.mass += std::fabs (dev[k]);
                ++k;
            }
            c.mean /= (double) (c.b - c.a + 1);
            cls.push_back (c);
        }
        std::sort (cls.begin(), cls.end(), [] (const Cl& x, const Cl& y) { return x.mass > y.mass; });
        if (cls.size() > 4)
            cls.resize (4);
        for (auto& c : cls)
        {
            double lw = 0, w = 0;
            for (size_t k = c.a; k <= c.b; ++k)
            {
                lw += std::fabs (dev[k]) * std::log2 (cf[k]);
                w += std::fabs (dev[k]);
            }
            const double center = std::pow (2.0, lw / w), widthOct = (double) (c.b - c.a + 1) / 6.0;
            const double flo = cf[c.a] * std::pow (2.0, -1.0 / 12), fhi = cf[c.b] * std::pow (2.0, 1.0 / 12); // grid cells
            Rec r;
            r.order = 3;
            r.severity = std::fabs (c.peak);
            if (c.a == 0 && widthOct >= 1.0)
            {
                r.fix = { Fix::LowShelf, fhi, std::clamp (-0.7 * c.mean, -6.0, 6.0), 0.707 };
                r.title = c.mean > 0 ? "Too much low end overall" : "Not enough low end overall";
            }
            else
            {
                const double bw = std::max (widthOct, 1.0 / 3.0), k2 = std::pow (2.0, bw);
                r.fix = { Fix::Peak, center, std::clamp (-0.7 * c.peak, -6.0, 6.0), std::sqrt (k2) / (k2 - 1.0) };
                // a narrow excess on a measured resonance: use its exact frequency and width
                for (auto& res : an.resonances)
                    if (c.peak > 0 && widthOct <= 1.0 / 3.0 + 1e-9 && std::fabs (std::log2 (res.hz / center)) < 1.0 / 6.0)
                    {
                        r.fix.hz = res.hz;
                        r.fix.q = res.q;
                        break;
                    }
                if (c.peak > 0 && center >= 150 && center <= 350)
                    r.title = "Low-mid mud";
                else if (center < 40)
                    r.title = c.peak > 0 ? "Too much sub" : "Sub is missing";
                else
                    r.title = c.peak > 0 ? fmt ("Excess around %.0f Hz", center) : fmt ("Dip around %.0f Hz", center);
            }
            if (std::fabs (r.fix.gainDb) < 1.0)
                continue;
            r.id = fmt ("eq@%.0f", r.fix.hz);
            r.diagnosis = fmt ("%.0f-%.0f Hz sits %+.1f dB %s the references' range (peak %+.1f dB).", flo, fhi, c.mean,
                               c.mean > 0 ? "above" : "below", c.peak);
            if (r.fix.kind == Fix::Peak && r.fix.gainDb < 0)
                r.advice = "Cut on the bass first, then on other low-mid sources (keys, guitars, pads).";
            for (auto& info : catalog())
            {
                const std::string id = info.id;
                double a = 0, b = 0;
                if (id.rfind ("B1_", 0) == 0)
                    std::sscanf (id.c_str() + 3, "%lf_%lf", &a, &b);
                else if (id == "B3")
                    a = 20, b = 120;
                else if (id == "B4a" || id == "B4b")
                    a = 150, b = 350;
                if (b > 0 && fhi > a && flo < b)
                    r.metrics.push_back (id);
            }
            for (const char* id : { "B2", "B5" })
                r.metrics.push_back (id);
            recs.push_back (r);
        }
    }
    // 3b: a resonance the 1/6-octave curve smoothed away
    if (fires ("B5", 1) && ! an.resonances.empty())
    {
        const Resonance& res = an.resonances[0];
        bool covered = false;
        for (auto& r : recs)
            covered = covered || (r.fix.kind == Fix::Peak && std::fabs (std::log2 (r.fix.hz / res.hz)) < 1.0 / 6.0);
        if (! covered)
        {
            Rec r;
            r.id = fmt ("res@%.0f", res.hz);
            r.title = "Resonant note or mode";
            r.diagnosis = fmt ("A peak at %.1f Hz (%s) stands %.1f dB above its surroundings (references: up to %.1f dB).", res.hz,
                               noteName (res.hz).c_str(), res.promDb, target ("B5").hi);
            r.fix = { Fix::Peak, res.hz, -std::min (6.0, 0.7 * (res.promDb - target ("B5").hi)), res.q };
            r.metrics = { "B5" };
            r.order = 3;
            recs.push_back (r);
        }
    }
    // a resonance that is really one note played louder: say so on any rec that cuts there
    for (auto& r : recs)
        if (r.fix.kind == Fix::Peak && r.fix.gainDb < 0)
            for (auto& nt : an.notes)
                if (std::fabs (12 * std::log2 (nt.f0 / r.fix.hz)) < 0.5 && nt.residualDb > 3.0)
                {
                    r.advice += fmt (" %s notes are %+.1f dB above the line: if only that note booms, fix it in the part or with "
                                     "dynamic EQ instead.", noteName (nt.f0).c_str(), nt.residualDb);
                    break;
                }

    // 4: dynamics (estimated)
    auto estimated = [&] (const std::string& id, const std::string& title, int cat, const std::vector<std::string>& metrics,
                          const std::string& diag, const std::string& advice, int effort, int order) {
        Rec r;
        r.id = id;
        r.title = title;
        r.category = cat;
        r.metrics = metrics;
        r.diagnosis = diag;
        r.advice = advice;
        r.effort = effort;
        r.order = order;
        recs.push_back (r);
    };
    if (fires ("D3", 1))
    {
        std::vector<const Note*> out;
        for (auto& nt : an.notes)
            out.push_back (&nt);
        std::sort (out.begin(), out.end(), [] (const Note* x, const Note* y) { return std::fabs (x->residualDb) > std::fabs (y->residualDb); });
        std::string list;
        for (size_t i = 0; i < out.size() && i < 4; ++i)
            list += fmt ("%s%s %+.1f dB at %.1f s", i ? ", " : "", noteName (out[i]->f0).c_str(), out[i]->residualDb, out[i]->t0);
        estimated ("notes", "Inconsistent note levels", Dynamics, { "D3" },
                   fmt ("Note levels vary by %.1f dB (std. dev., pitch trend removed); references: %.1f dB. Furthest off: %s.", v ("D3"),
                        target ("D3").center, list.c_str()),
                   "Slow compression on the bass: about 3:1, 10-30 ms attack, release timed to the note length (one 8th or 16th); "
                   "or ride the listed notes.", 2, 4);
    }
    if (fires ("D4", 0))
        estimated ("trend", v ("D4") > 0 ? "Higher notes louder than lower ones" : "Lower notes louder than higher ones", Dynamics, { "D4" },
                   fmt ("Note level changes %+.1f dB per octave of pitch; references: %+.1f dB/oct.", v ("D4"), target ("D4").center),
                   "Usually the room or the monitors: check on headphones. Fix with a broad shelf or dynamic EQ, not compression.", 2, 4);
    if (fires ("D1", -1) || fires ("D7", -1))
        estimated ("overcompressed", "Over-compressed low end", Dynamics, { "D1", "D7" },
                   fmt ("Low-band crest factor %.1f dB, PLR %.1f dB; references: %.1f / %.1f dB.", v ("D1"), v ("D7"), target ("D1").center,
                        target ("D7").center),
                   "Lower the ratio or raise the threshold on the bass; try parallel compression; check the bus limiter.", 2, 4);
    if (fires ("D1", 1) || fires ("D7", 1))
        estimated ("peaky", "Peaky low end", Dynamics, { "D1", "D7" },
                   fmt ("Low-band crest factor %.1f dB, PLR %.1f dB; references: %.1f / %.1f dB.", v ("D1"), v ("D7"), target ("D1").center,
                        target ("D7").center),
                   "Control peaks on the bass and kick: 3-4:1 with a fast attack, or a clipper before the bus.", 2, 4);
    if (fires ("D2", 0))
        estimated ("range", v ("D2") > target ("D2").hi ? "Low end swings too much" : "Low end too static", Dynamics, { "D2" },
                   fmt ("Low-band level range %.1f dB (400 ms windows, 10th-95th pct); references: %.1f dB.", v ("D2"), target ("D2").center),
                   v ("D2") > target ("D2").hi ? "Level the bass across sections (automation or slow compression)." : "Let sections breathe: less bus compression on the low end.",
                   3, 4);
    if (fires ("D6", 0))
        estimated ("transients", v ("D6") < target ("D6").lo ? "Smeared low-end attacks" : "Attacks too spiky", Dynamics, { "D6" },
                   fmt ("Low-band attack is %+.1f dB over the following 100 ms; references: %+.1f dB.", v ("D6"), target ("D6").center),
                   v ("D6") < target ("D6").lo ? "Transient shaper on kick or bass (+attack), shorten the bass release or the kick tail, or a gate on the bass."
                                             : "Soften attacks: slower compressor attack off, or a little transient-shaper sustain.",
                   2, 4);
    // 5: harmonics (estimated)
    if (fires ("C4", -1) || fires ("X2", -1))
        estimated ("harmonics", "Bass vanishes on small speakers", Translation, { "C4", "X2" },
                   fmt ("Harmonics 2-5 sit %.1f dB re the fundamental (references: %.1f dB); %.0f %% of notes stay audible through small "
                        "speakers (references: %.0f %%).", v ("C4"), target ("C4").center, 100 * v ("X2"), 100 * target ("X2").center),
                   "Add 2nd/3rd-harmonic saturation to the bass until it reads on a phone; then re-check 150-400 Hz for mud.", 2, 5);
    if (fires ("C4", 1))
        estimated ("distortion", "Bass harmonically heavy", Clarity, { "C4" },
                   fmt ("Harmonics 2-5 sit %.1f dB re the fundamental; references: %.1f dB.", v ("C4"), target ("C4").center),
                   "Back off saturation/distortion on the bass, or blend in a clean low layer.", 2, 5);

    // --- predictions -----------------------------------------------------------------------------------------------
    for (auto& r : recs)
    {
        double cmin = 1.0, sev = r.severity;
        for (auto& id : r.metrics)
            if (auto* m = ms (id); m && m->used)
            {
                cmin = std::min (cmin, m->conf);
                if (r.fix.kind == Fix::None || r.fix.kind == Fix::MonoBelow || r.fix.kind == Fix::HighPass)
                    sev = std::max (sev, m->d);
            }
        r.severity = sev;
        r.conf = cmin;
        if (r.fix.kind != Fix::None && opt.simulate)
        {
            if (opt.progress)
                opt.progress ("simulating " + describe (r.fix));
            const Analysis a2 = analyze (applyFix (audio, r.fix));
            const Score s2 = score (a2, pr, opt.stage);
            r.simulated = true;
            r.after = s2.composite;
            r.delta = s2.composite - sc.composite;
            r.deltaUncapped = s2.uncapped - sc.uncapped;
            for (auto& id : r.metrics)
                if (auto* m = ms (id); m && m->used)
                    r.predicted.emplace_back (id, an.get (id).v, a2.get (id).v);
        }
        else
        {
            auto m2 = an.m;
            if (r.id == "clipping") // the gates lift once the bounce is clean
            {
                m2["T2"].v = 0;
                m2["T1"].v = std::min (m2["T1"].v, -1.0);
            }
            for (auto& id : r.metrics)
                if (auto* m = ms (id); m && m->used && m->d > 0)
                {
                    const Target t = target (id);
                    m2[id].v = m->side < 0 ? t.lo : t.hi;
                    r.predicted.emplace_back (id, m->v, m2[id].v);
                }
            const Score s2 = scoreMetrics (m2, pr, opt.stage);
            r.after = s2.composite;
            r.delta = s2.composite - sc.composite;
            r.deltaUncapped = s2.uncapped - sc.uncapped;
        }
        // under a gate the capped composite cannot move, so rank by what the fix does to the uncapped score as well
        r.priority = std::max ({ 0.0, r.delta, r.deltaUncapped }) * r.conf / r.effort;
    }
    // a simulated move worth less than a point is noise, not advice
    recs.erase (std::remove_if (recs.begin(), recs.end(), [] (const Rec& r) { return r.simulated && r.delta < 1.0; }), recs.end());
    std::stable_sort (recs.begin(), recs.end(), [] (const Rec& x, const Rec& y) { return x.priority > y.priority; });

    if (opt.simulate)
    {
        std::vector<const Rec*> sim;
        for (auto& r : recs)
            if (r.simulated)
                sim.push_back (&r);
        if (sim.size() >= 2)
        {
            std::stable_sort (sim.begin(), sim.end(), [] (const Rec* x, const Rec* y) { return x->order < y->order; });
            Stereo s = audio;
            for (auto* r : sim)
                s = applyFix (s, r->fix);
            if (opt.progress)
                opt.progress ("simulating all EQ/stereo fixes together");
            out.combined = score (analyze (s), pr, opt.stage);
            out.combinedValid = true;
        }
    }
    out.recs = std::move (recs);
    return out;
}
} // namespace lee

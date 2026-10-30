#pragma once

// Scores a mix's metrics against a profile.
//
// Per metric: d = distance outside the band in sigmas, s = 100 exp(-d^2 / 2) (100 inside the band, 61 at 1 sigma, 14 at
// 2). Per category: weighted power mean (p = 0.5) of s with weight x confidence, so one bad metric pulls harder than in a
// plain average; metrics under 0.3 confidence are left out. Composite: category weights, renormalised over the categories
// that have data. Gates cap the composite for defects nothing else should hide; the uncapped value is kept.

#include "Profile.h"

#include <array>
#include <cstdarg>
#include <cstdio>

namespace lee
{
enum class Stage { Mix, Master };

struct MetricScore
{
    std::string id;
    double v = NAN, conf = 0, d = 0, s = 100;
    int side = 0;      // -1 below the band, +1 above, 0 inside
    bool used = false; // counted in its category
    bool muted = false;
};

struct Score
{
    std::vector<MetricScore> ms;
    std::array<double, 3> cat { 0, 0, 0 }, catConf { 0, 0, 0 };
    std::array<bool, 3> catValid { false, false, false };
    double composite = 0, uncapped = 0;
    std::vector<std::string> gates;

    const MetricScore* find (const std::string& id) const
    {
        for (auto& m : ms)
            if (m.id == id)
                return &m;
        return nullptr;
    }
    double d (const std::string& id) const
    {
        auto* m = find (id);
        return m ? m->d : 0.0;
    }
};

inline std::string fmt (const char* f, ...)
{
    char buf[512];
    va_list ap;
    va_start (ap, f);
    std::vsnprintf (buf, sizeof buf, f, ap);
    va_end (ap);
    return buf;
}

inline const char* grade (double s) { return s >= 90 ? "A" : s >= 80 ? "B" : s >= 65 ? "C" : s >= 50 ? "D" : "F"; }

inline Score scoreMetrics (const std::map<std::string, Metric>& m, const Profile& p, Stage stage)
{
    Score sc;
    double num[3] = {}, den[3] = {}, cw[3] = {};
    for (auto& info : catalog())
    {
        auto mit = m.find (info.id);
        if (info.category == Gate || mit == m.end())
            continue;
        MetricScore ms;
        ms.id = info.id;
        ms.v = mit->second.v;
        ms.conf = mit->second.conf;
        auto tit = p.targets.find (info.id);
        if (tit != p.targets.end() && mit->second.valid())
        {
            const Target& t = tit->second;
            ms.muted = t.muted;
            double hi = t.hi;
            if (stage == Stage::Mix && info.mixOneSided)
                hi = std::max (hi, info.hi); // an unmastered mix may be more dynamic than mastered references
            const double over = std::max ({ 0.0, t.lo - ms.v, ms.v - hi });
            ms.side = ms.v < t.lo ? -1 : ms.v > hi ? 1 : 0;
            ms.d = over / t.sigma;
            ms.s = 100.0 * std::exp (-0.5 * ms.d * ms.d);
            ms.used = ! t.muted && ms.conf >= 0.3 && t.weight > 0;
            if (ms.used)
            {
                const int c = info.category;
                const double w = t.weight * ms.conf;
                num[c] += w * std::sqrt (ms.s);
                den[c] += w;
                cw[c] += t.weight;
            }
        }
        sc.ms.push_back (ms);
    }
    double tot = 0, wsum = 0;
    for (int c = 0; c < 3; ++c)
        if (den[c] > 0)
        {
            sc.catValid[(size_t) c] = true;
            sc.cat[(size_t) c] = std::pow (num[c] / den[c], 2.0);
            sc.catConf[(size_t) c] = den[c] / cw[c];
            tot += p.catWeight[c] * sc.cat[(size_t) c];
            wsum += p.catWeight[c];
        }
    sc.uncapped = wsum > 0 ? tot / wsum : 0.0;
    double cap = 100;
    auto gate = [&] (double c, const std::string& why) {
        cap = std::min (cap, c);
        sc.gates.push_back (why + " (composite capped at " + std::to_string ((int) c) + ")");
    };
    // a metric is missing (NaN) on input too short to analyse: every comparison below is then false
    auto val = [&] (const char* id) { auto it = m.find (id); return it == m.end() ? NAN : it->second.v; };
    if (stage == Stage::Master && val ("T1") > 0.0)
        gate (59, fmt ("true peak %+.1f dBTP", val ("T1")));
    if (val ("T2") > 0)
        gate (59, fmt ("%.0f clipping event(s)", val ("T2")));
    if (auto s2 = m.find ("S2"); s2 != m.end() && s2->second.conf >= 0.3 && s2->second.v < -0.5)
        gate (64, fmt ("low-band correlation drops to %.2f (anti-phase low end)", s2->second.v));
    if (val ("T3") > 1.0)
        gate (69, fmt ("DC offset %.2f %% FS", val ("T3")));
    sc.composite = std::min (sc.uncapped, cap);
    return sc;
}

inline Score score (const Analysis& a, const Profile& p, Stage stage) { return scoreMetrics (a.m, p, stage); }
} // namespace lee

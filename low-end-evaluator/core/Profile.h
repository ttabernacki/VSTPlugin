#pragma once

// A profile is the judge: per-metric targets learned from a set of reference mixes the user picked.
//
// For each metric: center = median of the references, band (no penalty) = interquartile range (10th-90th percentile with
// fewer than 10 references, as quartiles of a handful of tracks are noise), sigma = half the 10th-90th percentile width,
// widened by sqrt(10/n) for small sets. Floors from the catalog keep a very consistent reference set from punishing
// inaudible differences. The references' 1/6-octave curve is kept the same way and drives EQ suggestions.

#include "Analyzer.h"
#include "Json.h"

namespace lee
{
constexpr int kProfileVersion = 1;
constexpr const char* kAnalyzerVersion = "lee-analyzer 0.1";

struct Target
{
    double center = 0, lo = 0, hi = 0, sigma = 1, weight = 1;
    int n = 0;
    bool muted = false;          // the user marked this metric as intentional: not scored, no recommendations
    std::vector<double> values;  // per reference, in reference order (NaN where not measured)
};

struct RefInfo
{
    std::string file;
    double seconds = 0;
};

struct Profile
{
    std::string name;
    std::vector<RefInfo> refs;
    std::map<std::string, Target> targets;
    std::vector<double> curveMed, curveLo, curveHi;
    double catWeight[3] = { 0.40, 0.30, 0.30 };
};

namespace detail
{
inline void fitBand (std::vector<double> v, double floorHalf, double floorSigma, double physLo, double physHi, Target& t)
{
    std::sort (v.begin(), v.end());
    t.n = (int) v.size();
    const double q = t.n >= 10 ? 0.25 : 0.10;
    t.center = percentile (v, 0.5);
    t.lo = std::min (percentile (v, q), t.center - floorHalf);
    t.hi = std::max (percentile (v, 1.0 - q), t.center + floorHalf);
    t.sigma = std::max (floorSigma, 0.5 * (percentile (v, 0.9) - percentile (v, 0.1))) * std::sqrt (std::max (1.0, 10.0 / t.n));
    t.center = std::clamp (t.center, physLo, physHi);
    t.lo = std::clamp (t.lo, physLo, physHi);
    t.hi = std::clamp (t.hi, physLo, physHi);
}
} // namespace detail

inline Profile buildProfile (const std::string& name, const std::vector<Analysis>& refs, std::vector<std::string>* warnings = nullptr)
{
    Profile p;
    p.name = name;
    for (auto& r : refs)
        p.refs.push_back ({ r.source, r.seconds });
    const size_t N = refs.size();
    for (auto& info : catalog())
    {
        if (info.category == Gate)
            continue;
        Target t;
        t.weight = info.weight;
        std::vector<double> good;
        for (auto& r : refs)
        {
            const Metric m = r.get (info.id);
            const bool ok = m.valid() && m.conf >= 0.3;
            t.values.push_back (ok ? m.v : NAN);
            if (ok)
                good.push_back (m.v);
        }
        if (good.empty() || good.size() * 2 < N)
        {
            if (warnings)
                warnings->push_back (std::string (info.id) + " (" + info.name + "): measured reliably in only " + std::to_string (good.size()) + " of "
                                     + std::to_string (N) + " references; left out of the profile");
            continue;
        }
        detail::fitBand (good, info.bandFloor, info.sigmaFloor, info.lo, info.hi, t);
        p.targets[info.id] = t;
    }
    const size_t nc = curveFreqs().size();
    p.curveMed.assign (nc, NAN);
    p.curveLo = p.curveHi = p.curveMed;
    for (size_t k = 0; k < nc; ++k)
    {
        std::vector<double> v;
        for (auto& r : refs)
            if (k < r.curve.size() && std::isfinite (r.curve[k]))
                v.push_back (r.curve[k]);
        if (v.empty())
            continue;
        Target t;
        detail::fitBand (v, 1.0, 1.0, -200, 100, t);
        p.curveMed[k] = t.center;
        p.curveLo[k] = t.lo;
        p.curveHi[k] = t.hi;
    }
    if (warnings && N < 5)
        warnings->push_back ("only " + std::to_string (N) + " reference(s): targets are wide and rough; 8-20 references give a usable profile");
    return p;
}

inline Json toJson (const Profile& p)
{
    Json j = Json::object();
    j.set ("format", Json::str ("low-end-evaluator profile"));
    j.set ("version", Json::num (kProfileVersion));
    j.set ("analyzer", Json::str (kAnalyzerVersion));
    j.set ("name", Json::str (p.name));
    Json cw = Json::object();
    for (int c = 0; c < 3; ++c)
        cw.set (categoryName (c), Json::num (p.catWeight[c]));
    j.set ("category_weights", cw);
    Json refs = Json::array();
    for (auto& r : p.refs)
    {
        Json o = Json::object();
        o.set ("file", Json::str (r.file));
        o.set ("seconds", Json::num (r.seconds));
        refs.push (o);
    }
    j.set ("references", refs);
    Json ms = Json::object();
    for (auto& info : catalog()) // catalog order, not alphabetical
    {
        auto it = p.targets.find (info.id);
        if (it == p.targets.end())
            continue;
        const Target& t = it->second;
        Json o = Json::object();
        o.set ("name", Json::str (info.name));
        o.set ("center", Json::num (t.center));
        o.set ("band", Json::numbers ({ t.lo, t.hi }));
        o.set ("sigma", Json::num (t.sigma));
        o.set ("weight", Json::num (t.weight));
        o.set ("muted", Json::boolean (t.muted));
        o.set ("n", Json::num (t.n));
        o.set ("values", Json::numbers (t.values));
        ms.set (info.id, o);
    }
    j.set ("metrics", ms);
    Json c = Json::object();
    c.set ("freqs", Json::numbers (curveFreqs()));
    c.set ("median", Json::numbers (p.curveMed));
    c.set ("lo", Json::numbers (p.curveLo));
    c.set ("hi", Json::numbers (p.curveHi));
    j.set ("curve", c);
    return j;
}

inline bool fromJson (const Json& j, Profile& p, std::string& err)
{
    if (j.t != Json::Obj || j.strOr ("format", "") != "low-end-evaluator profile")
    {
        err = "not a low-end-evaluator profile";
        return false;
    }
    if (const double ver = j.numOr ("version", 0); ver > kProfileVersion)
    {
        err = "profile version " + std::to_string ((int) std::min (ver, 1e6)) + " is newer than this build understands";
        return false;
    }
    p = Profile {};
    p.name = j.strOr ("name", "");
    if (auto* cw = j.get ("category_weights"))
        for (int c = 0; c < 3; ++c)
            p.catWeight[c] = std::max (0.0, cw->numOr (categoryName (c), p.catWeight[c]));
    if (auto* refs = j.get ("references"); refs && refs->t == Json::Arr)
        for (auto& r : refs->a)
            p.refs.push_back ({ r.strOr ("file", ""), r.numOr ("seconds", 0) });
    if (auto* ms = j.get ("metrics"); ms && ms->t == Json::Obj)
        for (auto& kv : ms->o)
        {
            const MetricInfo* info = findMetric (kv.first);
            if (! info || info->category == Gate)
                continue; // unknown or non-profiled metric: ignore
            const Json& o = kv.second;
            Target t;
            t.center = o.numOr ("center", NAN);
            const auto band = o.numsOr ("band");
            t.lo = band.size() == 2 ? band[0] : NAN;
            t.hi = band.size() == 2 ? band[1] : NAN;
            t.sigma = o.numOr ("sigma", info->sigmaFloor);
            t.weight = o.numOr ("weight", info->weight);
            t.muted = o.boolOr ("muted", false);
            t.n = (int) std::clamp (o.numOr ("n", 0), 0.0, 1e6);
            t.values = o.numsOr ("values");
            if (! std::isfinite (t.center) || ! std::isfinite (t.lo) || ! std::isfinite (t.hi) || t.lo > t.hi || ! (t.sigma > 0) || t.weight < 0)
            {
                err = std::string ("metric ") + info->id + ": needs center, band [lo, hi] with lo <= hi, sigma > 0 and weight >= 0";
                return false;
            }
            p.targets[info->id] = t;
        }
    const size_t nc = curveFreqs().size();
    if (auto* c = j.get ("curve"))
    {
        p.curveMed = c->numsOr ("median");
        p.curveLo = c->numsOr ("lo");
        p.curveHi = c->numsOr ("hi");
    }
    if (p.curveMed.size() != nc || p.curveLo.size() != nc || p.curveHi.size() != nc)
        p.curveMed.assign (nc, NAN), p.curveLo.assign (nc, NAN), p.curveHi.assign (nc, NAN);
    return true;
}
} // namespace lee

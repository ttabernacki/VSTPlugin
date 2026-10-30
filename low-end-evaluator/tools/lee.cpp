// lee: command-line front end of the Low-End Evaluator core (the plug-in will call the same functions).
//
//   lee profile -o my_vibe.json [--name "My vibe"] refs/*.wav refs_dir/ ...   learn a profile from reference mixes
//   lee judge mix.wav -p my_vibe.json [--stage mix|master] [--report r.json] [--compare old_report.json] [--all] [--no-simulate]
//   lee analyze file.wav ... [--json out.json]                                raw metrics

#include "../core/Recommend.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace lee;
namespace fs = std::filesystem;

static void usage()
{
    std::printf (
        "Low-End Evaluator: grades the 20-250 Hz region of a mix against a profile learned from your reference mixes.\n\n"
        "  lee profile -o PROFILE.json [--name NAME] REF.wav|DIR ...\n"
        "      Analyse reference mixes and write the profile (the judge). Directories are searched for .wav files.\n"
        "  lee judge MIX.wav -p PROFILE.json [--stage mix|master] [--report OUT.json] [--compare OLD.json] [--all] [--no-simulate]\n"
        "      Score a mix against a profile and list fixes. --stage mix (default) does not penalise an unmastered mix for\n"
        "      being more dynamic than mastered references; --compare marks which earlier recommendations were resolved.\n"
        "  lee analyze FILE.wav ... [--json OUT.json]\n"
        "      Print the raw metrics.\n");
}

static bool load (const std::string& path, Stereo& s48)
{
    Stereo raw;
    std::string err;
    if (! readWav (path, raw, err))
    {
        std::fprintf (stderr, "error: %s\n", err.c_str());
        return false;
    }
    s48 = toAnalysisRate (raw);
    return true;
}

static std::vector<std::string> expand (const std::vector<std::string>& args)
{
    std::vector<std::string> files;
    for (auto& a : args)
    {
        std::error_code ec;
        if (fs::is_directory (a, ec))
        {
            std::vector<std::string> in;
            for (auto& e : fs::recursive_directory_iterator (a, ec))
            {
                std::string ext = e.path().extension().string();
                std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
                if (e.is_regular_file() && (ext == ".wav" || ext == ".wave"))
                    in.push_back (e.path().string());
            }
            std::sort (in.begin(), in.end()); // directory order is not portable; profiles must be reproducible
            files.insert (files.end(), in.begin(), in.end());
        }
        else
            files.push_back (a);
    }
    return files;
}

static bool readText (const std::string& path, std::string& out)
{
    std::ifstream f (path, std::ios::binary);
    if (! f)
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static bool writeText (const std::string& path, const std::string& text)
{
    std::ofstream f (path, std::ios::binary);
    f << text;
    return (bool) f;
}

static std::string base (const std::string& p) { return fs::path (p).filename().string(); }

static Json metricsJson (const Analysis& a)
{
    Json o = Json::object();
    for (auto& info : catalog())
    {
        const Metric m = a.get (info.id);
        Json e = Json::object();
        e.set ("value", Json::num (m.v));
        e.set ("confidence", Json::num (m.conf));
        o.set (info.id, e);
    }
    return o;
}

static void printMetric (const MetricInfo& info, const Metric& m)
{
    if (m.valid())
        std::printf ("  %-14s %-46s %9.2f %-6s conf %.2f\n", info.id, info.name, m.v, info.unit, m.conf);
    else
        std::printf ("  %-14s %-46s %9s %-6s not enough data\n", info.id, info.name, "-", info.unit);
}

static int cmdAnalyze (const std::vector<std::string>& args, const std::string& jsonOut)
{
    Json all = Json::array();
    for (auto& f : expand (args))
    {
        Stereo s;
        if (! load (f, s))
            return 1;
        const Analysis a = analyze (s, base (f));
        std::printf ("%s (%.1f s, %zu notes, %d low-band onsets)\n", f.c_str(), a.seconds, a.notes.size(), a.onsets);
        for (auto& info : catalog())
            printMetric (info, a.get (info.id));
        Json o = Json::object();
        o.set ("file", Json::str (f));
        o.set ("seconds", Json::num (a.seconds));
        o.set ("metrics", metricsJson (a));
        o.set ("curve", Json::numbers (a.curve));
        all.push (o);
    }
    if (! jsonOut.empty() && ! writeText (jsonOut, dump (all)))
    {
        std::fprintf (stderr, "error: cannot write %s\n", jsonOut.c_str());
        return 1;
    }
    return 0;
}

static int cmdProfile (const std::vector<std::string>& args, std::string name, const std::string& out)
{
    const auto files = expand (args);
    if (files.empty() || out.empty())
    {
        usage();
        return 2;
    }
    std::vector<Analysis> refs;
    for (size_t i = 0; i < files.size(); ++i)
    {
        std::printf ("[%zu/%zu] %s\n", i + 1, files.size(), files[i].c_str());
        std::fflush (stdout);
        Stereo s;
        if (! load (files[i], s))
            return 1;
        refs.push_back (analyze (s, base (files[i])));
    }
    if (name.empty())
        name = fs::path (out).stem().string();
    std::vector<std::string> warn;
    const Profile p = buildProfile (name, refs, &warn);
    if (! writeText (out, dump (toJson (p))))
    {
        std::fprintf (stderr, "error: cannot write %s\n", out.c_str());
        return 1;
    }
    std::printf ("\nProfile \"%s\" from %zu reference(s) -> %s\n\n", name.c_str(), refs.size(), out.c_str());
    std::printf ("  %-14s %-46s %9s %19s %7s\n", "metric", "", "center", "band", "sigma");
    for (auto& info : catalog())
    {
        auto it = p.targets.find (info.id);
        if (it != p.targets.end())
            std::printf ("  %-14s %-46s %9.2f   [%7.2f, %7.2f] %7.2f %s\n", info.id, info.name, it->second.center, it->second.lo, it->second.hi,
                         it->second.sigma, info.unit);
    }
    for (auto& w : warn)
        std::printf ("  note: %s\n", w.c_str());
    // leave-one-out: each reference judged by a profile of the others. A reference that scores low does not fit the set:
    // check it by ear (and remove it only for a reason, not because it breaks the pattern).
    if (refs.size() >= 4)
    {
        std::printf ("\nLeave-one-out check (each reference judged against the others):\n");
        for (size_t i = 0; i < refs.size(); ++i)
        {
            std::vector<Analysis> others;
            for (size_t j = 0; j < refs.size(); ++j)
                if (j != i)
                    others.push_back (refs[j]);
            const Profile po = buildProfile (name, others);
            const Score sc = score (refs[i], po, Stage::Master);
            std::string worst;
            double wd = 0;
            for (auto& m : sc.ms)
                if (m.used && m.d > wd)
                {
                    wd = m.d;
                    worst = m.id;
                }
            std::printf ("  %5.1f %s  %-40s%s%s\n", sc.uncapped, grade (sc.uncapped), refs[i].source.c_str(),
                         sc.uncapped < 70 ? "  <- outlier, furthest off on " : "", sc.uncapped < 70 ? worst.c_str() : "");
        }
    }
    return 0;
}

static Json scoreJson (const Score& sc, const Profile& p)
{
    Json j = Json::object();
    j.set ("composite", Json::num (sc.composite));
    j.set ("uncapped", Json::num (sc.uncapped));
    j.set ("grade", Json::str (grade (sc.composite)));
    Json g = Json::array();
    for (auto& s : sc.gates)
        g.push (Json::str (s));
    j.set ("gates", g);
    Json cats = Json::object();
    for (int c = 0; c < 3; ++c)
    {
        Json o = Json::object();
        o.set ("score", sc.catValid[(size_t) c] ? Json::num (sc.cat[(size_t) c]) : Json());
        o.set ("confidence", Json::num (sc.catConf[(size_t) c]));
        cats.set (categoryName (c), o);
    }
    j.set ("categories", cats);
    Json ms = Json::object();
    for (auto& m : sc.ms)
    {
        Json o = Json::object();
        o.set ("value", Json::num (m.v));
        o.set ("confidence", Json::num (m.conf));
        o.set ("d", Json::num (m.d));
        o.set ("score", Json::num (m.s));
        o.set ("used", Json::boolean (m.used));
        if (auto it = p.targets.find (m.id); it != p.targets.end())
            o.set ("band", Json::numbers ({ it->second.lo, it->second.hi }));
        ms.set (m.id, o);
    }
    j.set ("metrics", ms);
    return j;
}

static int cmdJudge (const std::string& mix, const std::string& profPath, Stage stage, const std::string& reportOut,
                     const std::string& comparePath, bool all, bool simulate)
{
    std::string text, err;
    Json pj;
    Profile p;
    if (! readText (profPath, text) || ! parse (text, pj, err) || ! fromJson (pj, p, err))
    {
        std::fprintf (stderr, "error: profile %s: %s\n", profPath.c_str(), err.empty() ? "cannot read" : err.c_str());
        return 1;
    }
    Stereo s;
    if (! load (mix, s))
        return 1;
    const Analysis a = analyze (s, base (mix));
    const Score sc = score (a, p, stage);
    RecommendOptions opt;
    opt.stage = stage;
    opt.simulate = simulate;
    opt.progress = [] (const std::string& m) { std::fprintf (stderr, "  ... %s\n", m.c_str()); };
    const Recommendations rs = recommend (s, a, p, sc, opt);

    std::printf ("\n%s vs profile \"%s\" (%zu refs), %s stage\n\n", base (mix).c_str(), p.name.c_str(), p.refs.size(),
                 stage == Stage::Mix ? "mix" : "master");
    std::printf ("  Composite  %5.1f  %s", sc.composite, grade (sc.composite));
    if (! sc.gates.empty())
        std::printf ("   (uncapped %.1f %s)", sc.uncapped, grade (sc.uncapped));
    std::printf ("\n");
    for (auto& g : sc.gates)
        std::printf ("  GATE: %s\n", g.c_str());
    for (int c = 0; c < 3; ++c)
        if (sc.catValid[(size_t) c])
            std::printf ("  %-11s%5.1f  %s   confidence %.2f%s\n", categoryName (c), sc.cat[(size_t) c], grade (sc.cat[(size_t) c]), sc.catConf[(size_t) c],
                         sc.catConf[(size_t) c] < 0.5 ? "  (low confidence)" : "");
        else
            std::printf ("  %-11s    -   not enough data\n", categoryName (c));

    std::vector<const MetricScore*> off;
    for (auto& m : sc.ms)
        if (m.used && m.d > 0)
            off.push_back (&m);
    std::sort (off.begin(), off.end(), [] (auto* x, auto* y) { return x->d > y->d; });
    std::printf ("\n  Outside the references' range:\n");
    if (off.empty())
        std::printf ("    nothing\n");
    for (auto* m : off)
    {
        const MetricInfo* info = findMetric (m->id);
        const Target& t = p.targets.at (m->id);
        std::printf ("    %-14s %-44s %8.2f %-6s band [%.2f, %.2f]  %.1f sigma %s\n", m->id.c_str(), info->name, m->v, info->unit, t.lo, t.hi, m->d,
                     m->side < 0 ? "below" : "above");
    }
    std::vector<std::string> nodata;
    for (auto& m : sc.ms)
        if (! m.used && ! m.muted && p.targets.count (m.id))
            nodata.push_back (m.id);
    if (! nodata.empty())
    {
        std::printf ("  Not enough data:");
        for (auto& id : nodata)
            std::printf (" %s", id.c_str());
        std::printf ("\n");
    }

    std::printf ("\n  Recommendations (priority order):\n");
    if (rs.recs.empty())
        std::printf ("    none: the low end sits within the references' range\n");
    const size_t shown = all ? rs.recs.size() : std::min<size_t> (5, rs.recs.size());
    for (size_t i = 0; i < shown; ++i)
    {
        const Rec& r = rs.recs[i];
        // under a gate the composite cannot move, so show what the fix does to the uncapped score
        const std::string uncapped = sc.gates.empty() ? "" : fmt (", uncapped %+.1f", r.deltaUncapped);
        std::printf ("\n  %zu. [%s] %s  (%s %+.1f -> %.1f%s, confidence %.2f)\n", i + 1, categoryName (r.category), r.title.c_str(),
                     r.simulated ? "simulated" : "estimated", r.delta, r.after, uncapped.c_str(), r.conf);
        std::printf ("     %s\n", r.diagnosis.c_str());
        if (r.fix.kind != Fix::None)
            std::printf ("     Fix: %s\n", describe (r.fix).c_str());
        if (! r.advice.empty())
            std::printf ("     %s%s\n", r.fix.kind != Fix::None ? "" : "Fix: ", r.advice.c_str());
        for (auto& [id, now, after] : r.predicted)
            std::printf ("       %-12s %8.2f -> %8.2f\n", id.c_str(), now, after);
    }
    if (shown < rs.recs.size())
        std::printf ("\n  ... %zu more (--all)\n", rs.recs.size() - shown);
    if (rs.combinedValid)
        std::printf ("\n  All EQ/stereo fixes together (simulated, in order): composite %.1f -> %.1f %s\n", sc.composite, rs.combined.composite,
                     grade (rs.combined.composite));

    Json report = Json::object();
    report.set ("format", Json::str ("low-end-evaluator report"));
    report.set ("analyzer", Json::str (kAnalyzerVersion));
    report.set ("file", Json::str (mix));
    report.set ("profile", Json::str (p.name));
    report.set ("stage", Json::str (stage == Stage::Mix ? "mix" : "master"));
    report.set ("score", scoreJson (sc, p));
    Json rj = Json::array();
    for (auto& r : rs.recs)
    {
        Json o = Json::object();
        o.set ("id", Json::str (r.id));
        o.set ("title", Json::str (r.title));
        o.set ("category", Json::str (categoryName (r.category)));
        o.set ("diagnosis", Json::str (r.diagnosis));
        o.set ("fix", Json::str (r.fix.kind != Fix::None ? describe (r.fix) : r.advice));
        o.set ("simulated", Json::boolean (r.simulated));
        o.set ("delta", Json::num (r.delta));
        o.set ("priority", Json::num (r.priority));
        Json m = Json::array();
        for (auto& id : r.metrics)
            m.push (Json::str (id));
        o.set ("metrics", m);
        rj.push (o);
    }
    report.set ("recommendations", rj);

    // verification loop: compare against an earlier report of the same section
    if (! comparePath.empty())
    {
        Json old;
        if (! readText (comparePath, text) || ! parse (text, old, err))
        {
            std::fprintf (stderr, "error: cannot read report %s\n", comparePath.c_str());
            return 1;
        }
        const Json* os = old.get ("score");
        const Json* om = os ? os->get ("metrics") : nullptr;
        std::printf ("\n  Since %s: composite %.1f -> %.1f\n", comparePath.c_str(), os ? os->numOr ("composite", NAN) : NAN, sc.composite);
        if (auto* orecs = old.get ("recommendations"); orecs && om)
            for (auto& r : orecs->a)
            {
                double before = 0, now = 0;
                bool hit = false;
                if (auto* ids = r.get ("metrics"))
                    for (auto& id : ids->a)
                        if (auto* e = om->get (id.s); e && e->boolOr ("used", false))
                        {
                            before += e->numOr ("d", 0);
                            now += sc.d (id.s);
                            hit = true;
                        }
                // 0.25 sigma: below the run-to-run wobble of re-capturing the same section
                const char* status = ! hit ? "n/a" : now < 0.25 && before >= 0.25 ? "fixed" : now < before - 0.25 ? "improved"
                                                   : now > before + 0.25 ? "worsened" : "unchanged";
                std::printf ("    %-10s %s\n", status, r.strOr ("title", "").c_str());
            }
    }
    if (! reportOut.empty() && ! writeText (reportOut, dump (report)))
    {
        std::fprintf (stderr, "error: cannot write %s\n", reportOut.c_str());
        return 1;
    }
    std::printf ("\n  Targets are the references' norms, not rules. Mute a metric you mean (\"muted\": true in the profile).\n");
    return 0;
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        usage();
        return 2;
    }
    const std::string cmd = argv[1];
    std::vector<std::string> pos;
    std::string out, name, prof, report, compare, json;
    Stage stage = Stage::Mix;
    bool all = false, simulate = true;
    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> std::string {
            if (i + 1 >= argc)
            {
                std::fprintf (stderr, "error: %s needs a value\n", a.c_str());
                std::exit (2);
            }
            return argv[++i];
        };
        if (a == "-o" || a == "--out")
            out = next();
        else if (a == "--name")
            name = next();
        else if (a == "-p" || a == "--profile")
            prof = next();
        else if (a == "--report")
            report = next();
        else if (a == "--compare")
            compare = next();
        else if (a == "--json")
            json = next();
        else if (a == "--stage")
        {
            const std::string s = next();
            if (s != "mix" && s != "master")
            {
                std::fprintf (stderr, "error: --stage is mix or master\n");
                return 2;
            }
            stage = s == "mix" ? Stage::Mix : Stage::Master;
        }
        else if (a == "--all")
            all = true;
        else if (a == "--no-simulate")
            simulate = false;
        else if (a == "-h" || a == "--help")
        {
            usage();
            return 0;
        }
        else if (! a.empty() && a[0] == '-')
        {
            std::fprintf (stderr, "error: unknown option %s\n", a.c_str());
            return 2;
        }
        else
            pos.push_back (a);
    }
    if (cmd == "analyze" && ! pos.empty())
        return cmdAnalyze (pos, json);
    if (cmd == "profile")
        return cmdProfile (pos, name, out);
    if (cmd == "judge" && pos.size() == 1 && ! prof.empty())
        return cmdJudge (pos[0], prof, stage, report, compare, all, simulate);
    usage();
    return 2;
}

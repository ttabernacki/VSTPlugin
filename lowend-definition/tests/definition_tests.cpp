// Tests for the Low-End Definition DSP core (no JUCE needed).
#include "../core/Definition.h"
#include "../../bass-leveler/tests/synth.h"

#include <cstdio>
#include <cstdlib>
#include <random>

using namespace led;

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

struct Trace
{
    std::vector<float> out;
    std::vector<float> pitch, trans, contrast, pin, pout; // sampled once per 16-sample block
};

static Trace run (Definition& d, const std::vector<float>& in, int block, const Params& p, bool record = false, const std::vector<float>* sidechain = nullptr)
{
    d.setParams (p);
    Trace t;
    t.out = in;
    for (size_t i = 0; i < in.size(); i += (size_t) block)
    {
        float* c[1] = { t.out.data() + i };
        const float* s[1] = { sidechain ? sidechain->data() + i : nullptr };
        d.process (c, 1, (int) std::min<size_t> ((size_t) block, in.size() - i), sidechain ? s : nullptr, sidechain ? 1 : 0);
        if (record)
        {
            t.pitch.push_back (d.pitchHz());
            t.trans.push_back (d.transientGainDb());
            t.contrast.push_back (d.contrastGainDb());
            t.pin.push_back (d.meterValid() ? d.definitionIn() : -1.0f);
            t.pout.push_back (d.meterValid() ? d.definitionOut() : -1.0f);
        }
    }
    return t;
}

// 4th-order low-pass, to judge only the band the plug-in works on
static std::vector<float> lowpass (const std::vector<float>& x, double fs, double fc)
{
    std::vector<float> y = x;
    for (int stage = 0; stage < 2; ++stage)
    {
        const double q = stage == 0 ? 0.5411961 : 1.3065630, w = 2.0 * kPi * fc / fs, cw = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        const double b0 = (1 - cw) / 2 / a0, b1 = (1 - cw) / a0, b2 = b0, a1 = -2 * cw / a0, a2 = (1 - al) / a0;
        double z1 = 0, z2 = 0;
        for (auto& v : y)
        {
            const double o = b0 * v + z1;
            z1 = b1 * v - a1 * o + z2;
            z2 = b2 * v - a2 * o;
            v = (float) o;
        }
    }
    return y;
}

static std::array<double, 128> flat() { return {}; }

static std::vector<synth::Ev> line (double spacing, double dur, int repeats, unsigned seed, double& total)
{
    std::mt19937 g (seed);
    std::vector<synth::Ev> ev;
    double t = 0.3;
    for (int r = 0; r < repeats; ++r)
    {
        std::vector<int> pitches;
        for (int p = 28; p <= 43; ++p)
            pitches.push_back (p);
        std::shuffle (pitches.begin(), pitches.end(), g);
        for (int p : pitches)
        {
            ev.push_back ({ t, dur, p, std::uniform_real_distribution<double> (-3.0, 3.0) (g) });
            t += spacing;
        }
    }
    total = t + 0.5;
    return ev;
}

// amplitude (dB) of one frequency over a window holding a whole number of its cycles
static double ampDb (const std::vector<float>& x, double fs, double f, double t0, double cycles)
{
    const size_t i0 = (size_t) (t0 * fs), L = (size_t) std::llround (cycles * fs / f);
    const double w = 2.0 * kPi * cycles / (double) L;
    double re = 0, im = 0;
    for (size_t j = 0; j < L && i0 + j < x.size(); ++j)
    {
        re += x[i0 + j] * std::cos (w * (double) j);
        im -= x[i0 + j] * std::sin (w * (double) j);
    }
    return 20.0 * std::log10 (2.0 / (double) L * std::sqrt (re * re + im * im) + 1e-12);
}

// a held bass note with steady harmonics and an optional steady "mud" sine
static std::vector<float> held (double fs, double f0, double seconds, double mudHz, double mudAmp, double h2 = 0.5, double h3 = 0.3)
{
    std::vector<float> x ((size_t) (seconds * fs));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / fs, env = std::min (1.0, t / 0.03);
        double s = std::sin (2 * kPi * f0 * t) + h2 * std::sin (2 * kPi * 2 * f0 * t + 0.4) + h3 * std::sin (2 * kPi * 3 * f0 * t + 1.1);
        if (mudAmp > 0)
            s += mudAmp * std::sin (2 * kPi * mudHz * t + 0.7);
        x[i] = (float) (0.3 * env * s);
    }
    return x;
}

int main()
{
    Params neutral;
    neutral.contrast = neutral.punch = neutral.sustain = 0.0f;

    std::printf ("Latency, neutrality and the crossover\n");
    {
        Definition d;
        d.prepare (kFs);
        const int lat = d.latencySamples();
        CHECK (lat > 0 && lat % Definition::kSub == 0 && lat < (int) (0.15 * kFs), "look-ahead %d samples (%.0f ms)", lat, 1000.0 * lat / kFs);
        double total;
        const auto ev = line (0.55, 0.42, 2, 3, total);
        const auto sig = synth::render (kFs, ev, total, flat());
        const auto t = run (d, sig, 480, neutral);
        double md = 0;
        for (size_t i = (size_t) lat; i < sig.size(); ++i)
            md = std::max (md, (double) std::fabs (t.out[i] - sig[i - (size_t) lat]));
        CHECK (md < 1e-7, "all controls neutral: output is the input delayed by the look-ahead (max diff %.1e)", md);

        // with everything maxed, a signal far above the range must come out untouched
        std::vector<float> hi (96000), mix;
        for (size_t i = 0; i < hi.size(); ++i)
            hi[i] = (float) (0.3 * std::sin (2 * kPi * 3000.0 * (double) i / kFs));
        mix = hi;
        const auto bass = held (kFs, 41.2, 2.0, 0, 0);
        for (size_t i = 0; i < mix.size(); ++i)
            mix[i] += bass[i];
        Params maxed;
        maxed.contrast = 1.0f;
        maxed.punch = 1.0f;
        maxed.sustain = 1.0f;
        Definition d2;
        d2.prepare (kFs);
        const auto o = run (d2, mix, 480, maxed);
        const double a_in = ampDb (mix, kFs, 3000.0, 0.8, 600), a_out = ampDb (o.out, kFs, 3000.0, 0.8 + (double) lat / kFs, 600);
        CHECK (std::fabs (a_out - a_in) < 0.02, "a 3 kHz tone is left alone with every control up (%.3f dB)", a_out - a_in);
    }

    std::printf ("Pitch tracking under the look-ahead\n");
    for (auto spacing : { 0.55, 0.20 })
    {
        double total;
        const double dur = spacing < 0.3 ? 0.17 : 0.42;
        const auto ev = line (spacing, dur, 3, 5, total);
        const auto sig = synth::render (kFs, ev, total, flat(), 2);
        Definition d;
        d.prepare (kFs);
        const auto t = run (d, sig, 16, neutral, true);
        int good = 0, tried = 0;
        double covered = 0, frames = 0;
        for (const auto& e : ev)
        {
            const double f = synth::hz (e.midi);
            int in = 0, tot = 0;
            for (double s = e.t + 0.02; s < e.t + e.dur - 0.02; s += 0.004)
            {
                const size_t b = (size_t) ((s * kFs + d.latencySamples()) / 16.0);
                if (b >= t.pitch.size())
                    break;
                ++tot;
                if (t.pitch[b] > 0 && std::fabs (12.0 * std::log2 (t.pitch[b] / f)) < 0.5)
                    ++in;
            }
            if (tot)
            {
                ++tried;
                covered += in;
                frames += tot;
                good += in > 0.8 * tot;
            }
        }
        CHECK (good >= (int) (0.9 * tried), "%s notes (%.0f ms apart): %d/%d notes tracked for >80%% of their length (%.0f%% of all time)",
               spacing > 0.3 ? "sustained" : "fast", spacing * 1000, good, tried, 100.0 * covered / frames);
    }
    {
        // nothing to track: noise, silence, a pure tone far above the range
        Definition d;
        d.prepare (kFs);
        std::mt19937 g (4);
        std::vector<float> n (96000);
        for (auto& v : n)
            v = std::uniform_real_distribution<float> (-0.3f, 0.3f) (g);
        Params p;
        p.contrast = 1.0f;
        const auto t = run (d, n, 16, p, true);
        int voiced = 0;
        for (float f : t.pitch)
            voiced += f > 0;
        double mc = 0;
        for (float c : t.contrast)
            mc = std::max (mc, (double) std::fabs (c));
        CHECK ((double) voiced / t.pitch.size() < 0.2 && mc < 1.5, "white noise: %.0f%% of blocks pitched, largest contrast gain %.2f dB",
               100.0 * voiced / t.pitch.size(), mc);
    }

    std::printf ("Pitch-aware contrast\n");
    {
        const double f0 = 41.2, mud = 1.5 * f0;
        const auto sig = held (kFs, f0, 3.0, mud, 0.22);
        Definition dn, dp, dm;
        for (auto* d : { &dn, &dp, &dm })
            d->prepare (kFs);
        const int lat = dn.latencySamples();
        Params up, dn_;
        up.contrast = 1.0f;
        up.punch = up.sustain = 0.0f;
        dn_ = up;
        dn_.contrast = -1.0f;
        const auto pos = run (dp, sig, 16, up, true), neg = run (dm, sig, 480, dn_);
        const double t0 = 1.2 + (double) lat / kFs;
        const double fIn = ampDb (sig, kFs, f0, 1.2, 40), mIn = ampDb (sig, kFs, mud, 1.2, 60), hIn = ampDb (sig, kFs, 2 * f0, 1.2, 80);
        const double fPos = ampDb (pos.out, kFs, f0, t0, 40) - fIn, mPos = ampDb (pos.out, kFs, mud, t0, 60) - mIn, hPos = ampDb (pos.out, kFs, 2 * f0, t0, 80) - hIn;
        const double fNeg = ampDb (neg.out, kFs, f0, t0, 40) - fIn, mNeg = ampDb (neg.out, kFs, mud, t0, 60) - mIn;
        // judged against the untouched 2nd harmonic, because loudness matching moves the whole band a little
        CHECK (fPos - hPos > 4.0 && mPos - hPos < -2.0, "contrast +100%%: fundamental %+.1f dB, mud between harmonics %+.1f dB (re the 2nd harmonic)", fPos - hPos, mPos - hPos);
        CHECK (hPos > -4.5 && hPos < 0.5, "loudness matching trims the untouched 2nd harmonic by %+.1f dB", hPos);
        const double hNeg = ampDb (neg.out, kFs, 2 * f0, t0, 80) - hIn;
        CHECK (fNeg - hNeg < -3.5 && mNeg - hNeg > 2.0, "contrast -100%% softens: fundamental %+.1f dB, mud %+.1f dB (re the 2nd harmonic)", fNeg - hNeg, mNeg - hNeg);
        double pin = 0, pout = 0;
        int cnt = 0;
        for (size_t b = (size_t) (1.2 * kFs / 16); b < pos.pin.size(); ++b)
            if (pos.pin[b] >= 0)
            {
                pin += pos.pin[b];
                pout += pos.pout[b];
                ++cnt;
            }
        CHECK (cnt > 100 && pout / cnt > pin / cnt + 0.05, "definition meter: %.0f%% in, %.0f%% out", 100.0 * pin / cnt, 100.0 * pout / cnt);

        auto rmsDb = [&] (const std::vector<float>& v, size_t a) {
            const auto l = lowpass (v, kFs, 200.0);
            double e = 0;
            for (size_t i = a; i < a + (size_t) (1.0 * kFs); ++i)
                e += (double) l[i] * l[i];
            return 10.0 * std::log10 (e / kFs);
        };
        const double inDb = rmsDb (sig, (size_t) (1.2 * kFs));
        Params off = up;
        off.match = false;
        Definition dnm;
        dnm.prepare (kFs);
        const auto nomatch = run (dnm, sig, 480, off);
        const double dPos = rmsDb (pos.out, (size_t) (1.2 * kFs) + (size_t) lat) - inDb, dNeg = rmsDb (neg.out, (size_t) (1.2 * kFs) + (size_t) lat) - inDb,
                     dOff = rmsDb (nomatch.out, (size_t) (1.2 * kFs) + (size_t) lat) - inDb;
        CHECK (std::fabs (dPos) < 1.5 && std::fabs (dNeg) < 1.5 && dOff > 2.0, "loudness match: low band level %+.1f dB at +100%%, %+.1f dB at -100%% (%+.1f dB with match off)", dPos, dNeg, dOff);

        // an already pure tone is left almost alone
        std::vector<float> pure ((size_t) (3 * kFs));
        for (size_t i = 0; i < pure.size(); ++i)
            pure[i] = (float) (0.3 * std::sin (2 * kPi * 55.0 * (double) i / kFs) * std::min (1.0, (double) i / kFs / 0.03));
        Definition dpure;
        dpure.prepare (kFs);
        const auto tp = run (dpure, pure, 16, up, true);
        double mg = 0;
        for (float c : tp.contrast)
            mg = std::max (mg, (double) std::fabs (c));
        CHECK (mg < 0.5, "pure sine: the boost limits itself (largest bell gain %.2f dB)", mg);
    }

    std::printf ("Punch and sustain\n");
    {
        double total;
        const auto ev = line (0.55, 0.45, 2, 9, total);
        const auto sig = synth::render (kFs, ev, total, flat(), 3);
        Definition d0;
        d0.prepare (kFs);
        const int lat = d0.latencySamples();
        auto score = [&] (const Params& p) {
            Definition d;
            d.prepare (kFs);
            const auto t0 = run (d, sig, 480, p);
            const auto t = lowpass (t0.out, kFs, 200.0);
            if (std::getenv ("LED_DEBUG")) std::printf ("    punch %.1f sus %.1f\n", p.punch, p.sustain);
            double early = 0, late = 0;
            int n = 0;
            for (size_t i = 6; i < ev.size(); ++i)
            {
                const size_t s0 = (size_t) (ev[i].t * kFs) + (size_t) lat;
                double pk = 0, e2 = 0;
                const size_t na = (size_t) (0.030 * kFs);
                for (size_t j = 0; j < na; ++j)
                    pk += (double) t[s0 + j] * t[s0 + j];
                pk = std::sqrt (pk / (double) na);
                const size_t a = s0 + (size_t) (0.15 * kFs), b = s0 + (size_t) (0.35 * kFs);
                for (size_t j = a; j < b; ++j)
                    e2 += (double) t[j] * t[j];
                early += 20 * std::log10 (pk + 1e-9);
                late += 10 * std::log10 (e2 / (double) (b - a) + 1e-12);
                ++n;
            }
            if (std::getenv ("LED_DEBUG")) std::printf ("      early %.2f late %.2f (n=%d)\n", early / n, late / n, n);
            return std::array<double, 2> { early / n, late / n };
        };
        const auto base = score (neutral);
        Params pp = neutral, pm = neutral, sp = neutral, sm = neutral;
        pp.punch = 1.0f;
        pm.punch = -1.0f;
        sp.sustain = 1.0f;
        sm.sustain = -1.0f;
        const auto a = score (pp), b = score (pm), c = score (sp), e = score (sm);
        const double base_ratio = base[0] - base[1] / 2.0;
        auto ratio = [&] (const std::array<double, 2>& s) { return s[0] - s[1] / 2.0 - base_ratio; };
        CHECK (ratio (a) > 2.0, "punch +100%%: attack vs body %+.1f dB", ratio (a));
        CHECK (ratio (b) < -1.5, "punch -100%%: attack vs body %+.1f dB", ratio (b));
        CHECK (c[1] - base[1] > 1.5 && std::fabs (c[0] - base[0]) < 1.5, "sustain +100%%: body %+.1f dB, attack %+.1f dB", c[1] - base[1], c[0] - base[0]);
        CHECK (e[1] - base[1] < -1.5 && std::fabs (e[0] - base[0]) < 1.5, "sustain -100%%: body %+.1f dB, attack %+.1f dB", e[1] - base[1], e[0] - base[0]);

        Definition d;
        d.prepare (kFs);
        Params all;
        all.contrast = 1.0f;
        all.punch = 1.0f;
        all.sustain = 1.0f;
        const auto t = run (d, sig, 16, all, true);
        double steepest = 0, peak = 0;
        for (size_t i = 1; i < t.trans.size(); ++i)
        {
            steepest = std::max (steepest, (double) std::fabs (t.trans[i] - t.trans[i - 1]) * kFs / 16.0 / 1000.0);
            peak = std::max (peak, (double) std::fabs (t.trans[i]));
        }
        CHECK (steepest < 5.0 && peak <= 12.0, "transient gain never exceeds %.1f dB and moves at most %.2f dB per ms", peak, steepest);
    }


    std::printf("Kick: spectral duck, alignment\n");
    {
        // bass: a held E1 with harmonics and a steady muddy 64 Hz sine; kick: a 64 Hz burst every 0.6 s (the mud collides with it)
        const double f0 = 41.2, mudHz = 64.0;
        const auto bass = held (kFs, f0, 4.0, mudHz, 0.3);
        std::vector<float> kick (bass.size(), 0.0f);
        for (int b = 0; b < 6; ++b)
        {
            const size_t n0 = (size_t) ((0.5 + 0.6 * b) * kFs);
            for (size_t i = 0; i < (size_t) (0.18 * kFs) && n0 + i < kick.size(); ++i)
            {
                const double t = (double) i / kFs;
                kick[n0 + i] = (float) (0.8 * std::exp (-t / 0.06) * std::sin (2 * kPi * mudHz * t));
            }
        }
        Params off = neutral, on = neutral;
        off.kick = 0.0f;
        on.kick = 1.0f;
        Definition d0, d1, dn;
        for (auto* d : { &d0, &d1, &dn })
            d->prepare (kFs);
        const int lat = d0.latencySamples();
        const auto base = run (d0, bass, 480, off, false, &kick);
        const auto duck = run (d1, bass, 160, on, true, &kick);
        double md = 0;
        for (size_t i = (size_t) lat; i < bass.size(); ++i)
            md = std::max (md, (double) std::fabs (base.out[i] - bass[i - (size_t) lat]));
        CHECK (md < 1e-7, "kick amount 0 with a sidechain playing: bit-exact delay (%.1e)", md);
        Definition dns;
        dns.prepare (kFs);
        const auto nosc = run (dns, bass, 480, on);
        md = 0;
        for (size_t i = (size_t) lat; i < bass.size(); ++i)
            md = std::max (md, (double) std::fabs (nosc.out[i] - bass[i - (size_t) lat]));
        CHECK (md < 1e-7, "kick amount 100%% but no sidechain connected: bit-exact delay (%.1e)", md);

        // during a hit (window 40..140 ms after its start) versus the quiet part of the bass
        const double tHit = 0.5 + 0.6 * 3 + 0.04;
        const double mudHit = ampDb (duck.out, kFs, mudHz, tHit + (double) lat / kFs, 6) - ampDb (bass, kFs, mudHz, tHit, 6);
        const double fundHit = ampDb (duck.out, kFs, f0, tHit + (double) lat / kFs, 4) - ampDb (bass, kFs, f0, tHit, 4);
        const double tQuiet = 0.5 + 0.6 * 3 + 0.40;
        const double mudQuiet = ampDb (duck.out, kFs, mudHz, tQuiet + (double) lat / kFs, 8) - ampDb (bass, kFs, mudHz, tQuiet, 8);
        CHECK (mudHit < -3.0, "the mud under a kick hit is ducked by %.1f dB", -mudHit);
        CHECK (std::fabs (fundHit) < 1.5, "the bass note's own fundamental stays put during the hit (%+.1f dB)", fundHit);
        CHECK (std::fabs (mudQuiet) < 1.0, "between hits the mud is left alone (%+.1f dB)", mudQuiet);
        float deepest = 0;
        for (size_t b = 0; b < duck.pin.size(); ++b)
            deepest = std::max (deepest, 0.0f);
        (void) deepest;

        // block sizes with a sidechain
        for (int bs : { 1, 333, 4096 })
        {
            Definition d;
            d.prepare (kFs);
            const auto t = run (d, bass, bs, on, false, &kick);
            Definition dr;
            dr.prepare (kFs);
            const auto r = run (dr, bass, 512, on, false, &kick);
            double m2 = 0;
            for (size_t i = 0; i < bass.size(); ++i)
                m2 = std::max (m2, (double) std::fabs (t.out[i] - r.out[i]));
            CHECK (m2 == 0.0, "with a sidechain, block size %d is identical to 512-sample blocks (%.1e)", bs, m2);
        }
    }
    {
        // polarity: bass and kick are the same 55 Hz tone, in phase or in opposite phase, in bursts that overlap
        auto make = [&] (double sign, std::vector<float>& bass, std::vector<float>& kick) {
            bass.assign ((size_t) (8 * kFs), 0.0f);
            kick = bass;
            for (int b = 0; b < 12; ++b)
            {
                const size_t n0 = (size_t) ((0.3 + 0.6 * b) * kFs);
                for (size_t i = 0; i < (size_t) (0.4 * kFs) && n0 + i < bass.size(); ++i)
                {
                    const double t = (double) i / kFs, e = std::exp (-t / 0.25) * std::min (1.0, t / 0.01);
                    const double s = std::sin (2 * kPi * 55.0 * t);
                    bass[n0 + i] = (float) (0.4 * e * s * sign);
                    kick[n0 + i] = (float) (0.5 * e * s);
                }
            }
        };
        std::vector<float> bass, kick;
        Params a = neutral;
        a.kick = 0.0f;
        a.align = 1;
        make (-1.0, bass, kick);
        Definition dflip;
        dflip.prepare (kFs);
        const auto o = run (dflip, bass, 480, a, true, &kick);
        const int lat = dflip.latencySamples();
        double eSumIn = 0, eSumOut = 0;
        for (size_t i = (size_t) (4 * kFs); i < (size_t) (7 * kFs); ++i)
        {
            const double si = bass[i] + kick[i], so = o.out[i + (size_t) lat] + kick[i + (size_t) lat];
            eSumIn += si * si;
            eSumOut += so * so;
        }
        CHECK (dflip.polarityFlipped() && dflip.alignDb() < -3.0 && 10 * std::log10 (eSumOut / eSumIn) > 3.0,
               "opposite-phase bass: detected (%.1f dB), polarity flipped, kick+bass sum %+.1f dB", dflip.alignDb(), 10 * std::log10 (eSumOut / eSumIn));
        Definition dstay;
        dstay.prepare (kFs);
        make (1.0, bass, kick);
        run (dstay, bass, 480, a, false, &kick);
        CHECK (! dstay.polarityFlipped() && dstay.alignDb() > 3.0, "in-phase bass is left alone (%.1f dB)", dstay.alignDb());
        Definition dmanual;
        dmanual.prepare (kFs);
        Params man = a;
        man.align = 0;
        make (-1.0, bass, kick);
        const auto om = run (dmanual, bass, 480, man, false, &kick);
        double md = 0;
        for (size_t i = (size_t) lat; i < bass.size(); ++i)
            md = std::max (md, (double) std::fabs (om.out[i] - bass[i - (size_t) lat]));
        CHECK (! dmanual.polarityFlipped() && dmanual.alignDb() < -3.0 && md < 1e-7, "align off: it reports the cancellation (%.1f dB) and changes nothing", dmanual.alignDb());
    }


    std::printf("Kick, masking mode\n");
    {
        auto kickBursts = [&] (size_t n, double hz, double level) {
            std::vector<float> k (n, 0.0f);
            for (int b = 0; b < 6; ++b)
            {
                const size_t n0 = (size_t) ((0.5 + 0.6 * b) * kFs);
                for (size_t i = 0; i < (size_t) (0.18 * kFs) && n0 + i < k.size(); ++i)
                {
                    const double t = (double) i / kFs;
                    k[n0 + i] = (float) (level * std::exp (-t / 0.06) * std::sin (2 * kPi * hz * t));
                }
            }
            return k;
        };
        Params simple = neutral, mask = neutral;
        simple.kick = mask.kick = 1.0f;
        mask.kickMode = 1;
        const double f0 = 41.2, tHit = 0.5 + 0.6 * 3 + 0.04, tQuiet = 0.5 + 0.6 * 3 + 0.40;

        // 1. mud at 64 Hz that is far louder than the 64 Hz kick under it (over 30 dB): it covers the kick up
        {
            const auto bass = held (kFs, f0, 4.0, 64.0, 0.4);
            const auto kick = kickBursts (bass.size(), 64.0, 0.005);
            Definition d;
            d.prepare (kFs);
            const int lat = d.latencySamples();
            const auto o = run (d, bass, 480, mask, false, &kick);
            const double sh = (double) lat / kFs;
            const double mudHit = ampDb (o.out, kFs, 64.0, tHit + sh, 6) - ampDb (bass, kFs, 64.0, tHit, 6);
            const double fundHit = ampDb (o.out, kFs, f0, tHit + sh, 4) - ampDb (bass, kFs, f0, tHit, 4);
            const double mudQuiet = ampDb (o.out, kFs, 64.0, tQuiet + sh, 8) - ampDb (bass, kFs, 64.0, tQuiet, 8);
            CHECK (mudHit < -3.0 && std::fabs (fundHit) < 2.0 && std::fabs (mudQuiet) < 1.0,
                   "masking mode: mud under the kick %+.1f dB, the note %+.1f dB, between hits %+.1f dB", mudHit, fundHit, mudQuiet);
            Params off = mask;
            off.kick = 0.0f;
            Definition d0;
            d0.prepare (kFs);
            const auto z = run (d0, bass, 480, off, false, &kick);
            double md = 0;
            for (size_t i = (size_t) lat; i < bass.size(); ++i)
                md = std::max (md, (double) std::fabs (z.out[i] - bass[i - (size_t) lat]));
            Definition d4;
            d4.prepare (kFs);
            const auto b4 = run (d4, bass, 333, mask, false, &kick);
            double m4 = 0;
            for (size_t i = 0; i < bass.size(); ++i)
                m4 = std::max (m4, (double) std::fabs (b4.out[i] - o.out[i]));
            CHECK (md < 1e-7 && m4 == 0.0, "masking mode: bit-exact at Kick 0 %% (%.1e), identical across block sizes (%.1e)", md, m4);
        }

        // 2. only what masks the kick: a component 20 dB above the kick covers it up, a faint one does not,
        //    and a kick only a few dB below the bass is not masked either (a tone covers noise only from about 14 dB up)
        for (int scenario = 0; scenario < 3; ++scenario)
        {
            const double mudAmp = scenario == 0 ? 0.4 : (scenario == 1 ? 0.05 : 0.3), kickLevel = scenario == 0 ? 0.005 : (scenario == 1 ? 0.8 : 0.3);
            const auto bass = scenario == 1 ? held (kFs, f0, 4.0, 62.0, mudAmp, 0.0, 0.0) : held (kFs, f0, 4.0, 62.0, mudAmp);
            const auto kick = kickBursts (bass.size(), 62.0, kickLevel);
            Definition dm, ds;
            dm.prepare (kFs);
            ds.prepare (kFs);
            const double sh = (double) dm.latencySamples() / kFs;
            const auto om = run (dm, bass, 480, mask, false, &kick), os = run (ds, bass, 480, simple, false, &kick);
            const double ref = ampDb (bass, kFs, 62.0, tHit, 6);
            const double dMask = ampDb (om.out, kFs, 62.0, tHit + sh, 6) - ref, dSimple = ampDb (os.out, kFs, 62.0, tHit + sh, 6) - ref;
            if (scenario == 0)
                CHECK (dMask < -3.0, "a loud 62 Hz component far over the kick covers it: masking mode ducks it %+.1f dB (simple %+.1f dB)", dMask, dSimple);
            else if (scenario == 1)
                CHECK (std::fabs (dMask) < 1.5 && dSimple < -3.0,
                       "a faint 62 Hz component that does not cover the kick: masking mode leaves it (%+.1f dB), simple mode ducks it (%+.1f dB)", dMask, dSimple);
            else
                CHECK (std::fabs (dMask) < 1.5,
                       "a kick only a few dB under the bass is not masked: masking mode leaves it (%+.1f dB; simple mode %+.1f dB)", dMask, dSimple);
        }
    }

    std::printf ("Smoothness on note changes\n");
    {
        double total;
        const auto ev = line (0.30, 0.28, 2, 13, total);
        const auto sig = synth::render (kFs, ev, total, flat(), 5);
        Definition d;
        d.prepare (kFs);
        Params p;
        p.contrast = 1.0f;
        const auto t = run (d, sig, 480, p);
        const int lat = d.latencySamples();
        double stepAdded = 0, stepIn = 0;
        for (size_t i = (size_t) lat + 2; i < sig.size(); ++i)
        {
            const double a = (double) t.out[i] - sig[i - (size_t) lat], b = (double) t.out[i - 1] - sig[i - 1 - (size_t) lat];
            stepAdded = std::max (stepAdded, std::fabs (a - b));
            stepIn = std::max (stepIn, (double) std::fabs (sig[i - (size_t) lat] - sig[i - 1 - (size_t) lat]));
        }
        CHECK (stepAdded < 0.5 * stepIn, "what the plug-in adds never jumps (largest step %.4f vs %.4f in the dry signal)", stepAdded, stepIn);
    }

    std::printf ("Block sizes, channels, rates, stress\n");
    {
        double total;
        const auto ev = line (0.40, 0.30, 2, 17, total);
        const auto sig = synth::render (kFs, ev, total, flat(), 6);
        Params p;
        p.contrast = 0.8f;
        p.punch = 0.7f;
        p.sustain = -0.4f;
        Definition ref;
        ref.prepare (kFs);
        const auto r = run (ref, sig, 512, p);
        for (int bs : { 1, 7, 64, 333, 4096 })
        {
            Definition d;
            d.prepare (kFs);
            const auto t = run (d, sig, bs, p);
            double md = 0;
            for (size_t i = 0; i < sig.size(); ++i)
                md = std::max (md, (double) std::fabs (t.out[i] - r.out[i]));
            CHECK (md == 0.0, "block size %d: output identical to 512-sample blocks (max diff %.1e)", bs, md);
        }
        Definition ds;
        ds.prepare (kFs);
        ds.setParams (p);
        std::vector<float> L = sig, R = sig;
        for (size_t i = 0; i < sig.size(); i += 480)
        {
            float* c[2] = { L.data() + i, R.data() + i };
            ds.process (c, 2, (int) std::min<size_t> (480, sig.size() - i));
        }
        double md = 0, mr = 0;
        for (size_t i = 0; i < sig.size(); ++i)
        {
            md = std::max (md, (double) std::fabs (L[i] - R[i]));
            mr = std::max (mr, (double) std::fabs (L[i] - r.out[i]));
        }
        CHECK (md == 0.0 && mr < 1e-6, "identical channels stay identical and match the mono path (%.1e / %.1e)", md, mr);
    }
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        Definition d;
        d.prepare (sr);
        Params p;
        p.contrast = 1.0f;
        p.punch = 1.0f;
        p.sustain = 1.0f;
        std::mt19937 g (7);
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        std::vector<float> in ((size_t) sr * 3);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = 0.4f * std::sin (2.0f * 3.14159265f * 55.0f * (float) i / (float) sr) * 1.0f + 0.02f * u (g);
        in[1000] = std::numeric_limits<float>::quiet_NaN();
        in[2000] = 5.0f;
        const auto t = run (d, in, 16, p, true);
        bool finite = true;
        float peak = 0;
        for (float v : t.out)
        {
            finite = finite && std::isfinite (v);
            peak = std::max (peak, std::fabs (v));
        }
        int ok = 0, n = 0;
        for (size_t b = (size_t) (1.0 * sr / 16 + d.latencySamples() / 16); b < (size_t) (1.4 * sr / 16); ++b, ++n)
            ok += std::fabs (12.0 * std::log2 (t.pitch[b] / 55.0f)) < 0.5;
        CHECK (finite && peak < 6.0f && ok > 0.8 * n, "%.0f Hz: finite (peak %.2f), 55 Hz tone found in %d/%d blocks", sr, peak, ok, n);
    }
    {
        Definition d;
        d.prepare (kFs);
        Params p;
        p.contrast = 1.0f;
        p.punch = 1.0f;
        p.sustain = 1.0f;
        std::vector<float> dc (96000, 0.8f), sil (96000, 0.0f);
        const auto a = run (d, dc, 100, p), b = run (d, sil, 100, p);
        bool fin = true;
        for (float v : a.out)
            fin = fin && std::isfinite (v);
        for (float v : b.out)
            fin = fin && std::isfinite (v);
        double tail = 0;
        for (size_t i = 80000; i < b.out.size(); ++i)
            tail = std::max (tail, (double) std::fabs (b.out[i]));
        CHECK (fin && tail < 1e-4, "DC and then silence: finite, and no ringing left (%.1e)", tail);
    }


    std::printf("Robustness\n");
    {
        // 1. the duck must always be ready in time: at every sample rate, with the kick playing the whole time
        for (double sr : { 8000.0, 11025.0, 16000.0, 22050.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0 })
        {
            Definition d;
            d.prepare (sr);
            Params p;
            p.kick = 1.0f;
            p.contrast = 0.8f;
            p.punch = 0.5f;
            p.sustain = 0.5f;
            std::vector<float> b ((size_t) (4 * sr)), k (b.size());
            for (size_t i = 0; i < b.size(); ++i)
            {
                b[i] = (float) (0.3 * std::sin (2 * kPi * 41.2 * (double) i / sr) + 0.1 * std::sin (2 * kPi * 63.0 * (double) i / sr));
                k[i] = (float) (0.4 * std::sin (2 * kPi * 63.0 * (double) i / sr) * (((i / (size_t) (0.3 * sr)) % 2) ? 1.0 : 0.2));
            }
            d.setParams (p);
            bool finite = true;
            float peak = 0;
            for (size_t i = 0; i < b.size(); i += 333)
            {
                const size_t n = std::min<size_t> (333, b.size() - i);
                float* c[1] = { b.data() + i };
                const float* s[1] = { k.data() + i };
                d.process (c, 1, (int) n, s, 1);
            }
            for (float v : b)
            {
                finite = finite && std::isfinite (v);
                peak = std::max (peak, std::fabs (v));
            }
            CHECK (finite && peak < 2.0f && d.spectralGaps() == 0, "%.0f Hz: finite, peak %.2f, look-ahead %.0f ms, kick duck never late (%lld gaps)", sr, peak,
                   1000.0 * d.latencySamples() / sr, (long long) d.spectralGaps());
        }

        // 2. re-preparing at another rate leaves no state behind
        {
            Definition a, b;
            a.prepare (44100.0);
            std::vector<float> junk ((size_t) (2 * 44100.0), 0.3f);
            float* cj[1] = { junk.data() };
            Params p;
            p.contrast = 1.0f;
            p.kick = 1.0f;
            a.setParams (p);
            a.process (cj, 1, (int) junk.size(), nullptr, 0);
            a.prepare (kFs);
            b.prepare (kFs);
            std::vector<float> x ((size_t) (2 * kFs)), y;
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (float) (0.3 * std::sin (2 * kPi * 55.0 * (double) i / kFs));
            y = x;
            a.setParams (p);
            b.setParams (p);
            float* ca[1] = { x.data() };
            float* cb[1] = { y.data() };
            a.process (ca, 1, (int) x.size(), nullptr, 0);
            b.process (cb, 1, (int) y.size(), nullptr, 0);
            double md = 0;
            for (size_t i = 0; i < x.size(); ++i)
                md = std::max (md, (double) std::fabs (x[i] - y[i]));
            CHECK (md == 0.0, "prepare() again at another rate gives exactly a fresh instance (%.1e)", md);
        }

        // 3. knobs that jump must not click: contrast, punch, sustain, kick, range and match stepped while a note plays
        {
            const auto held1 = held (kFs, 55.0, 4.0, 0, 0);
            std::vector<float> kick (held1.size(), 0.0f);
            for (size_t i = 0; i < kick.size(); ++i)
                kick[i] = (float) (0.3 * std::sin (2 * kPi * 70.0 * (double) i / kFs) * (((i / 12000) % 2) ? 1.0 : 0.0));
            Definition d;
            d.prepare (kFs);
            std::vector<float> o = held1;
            double stepOut = 0, stepIn = 0;
            Params p;
            p.contrast = 0.0f;
            p.punch = 0.0f;
            p.sustain = 0.0f;
            p.kick = 0.0f;
            std::mt19937 g (21);
            for (size_t i = 0; i < o.size(); i += 480)
            {
                if ((i / 480) % 25 == 24) // every 0.25 s, jump to a random corner of the parameter space
                {
                    std::uniform_real_distribution<float> u (0.0f, 1.0f);
                    p.contrast = u (g) * 2 - 1;
                    p.punch = u (g) * 2 - 1;
                    p.sustain = u (g) * 2 - 1;
                    p.kick = u (g);
                    p.rangeHz = 60.0f + 240.0f * u (g);
                    p.match = u (g) > 0.5f;
                    p.kickMode = u (g) > 0.5f ? 1 : 0;
                }
                d.setParams (p);
                float* c[1] = { o.data() + i };
                const float* s[1] = { kick.data() + i };
                d.process (c, 1, (int) std::min<size_t> (480, o.size() - i), s, 1);
            }
            for (size_t i = (size_t) d.latencySamples() + 1; i < o.size(); ++i)
            {
                stepOut = std::max (stepOut, (double) std::fabs (o[i] - o[i - 1]));
                stepIn = std::max (stepIn, (double) std::fabs (held1[i - (size_t) d.latencySamples()] - held1[i - 1 - (size_t) d.latencySamples()]));
            }
            CHECK (stepOut < 2.5 * stepIn, "random parameter jumps every 250 ms: the largest sample step is %.4f vs %.4f in the dry note", stepOut, stepIn);
        }

        // 4. fuzz: random material, random parameters every few ms, sidechain on and off, odd block sizes
        {
            std::mt19937 g (99);
            std::uniform_real_distribution<float> u (0.0f, 1.0f);
            Definition d;
            d.prepare (kFs);
            bool finite = true;
            float peakIn = 0, peakOut = 0;
            std::vector<float> a (4096), k (4096);
            for (int blk = 0; blk < 600; ++blk)
            {
                const int n = 1 + (int) (u (g) * 3000);
                const int mode = (int) (u (g) * 5);
                for (int i = 0; i < n; ++i)
                {
                    const double t = (double) (blk * 3000 + i) / kFs;
                    float v = 0;
                    if (mode == 0) v = 0.5f * (u (g) - 0.5f);
                    else if (mode == 1) v = (float) (0.6 * std::sin (2 * kPi * (30.0 + 200.0 * u (g)) * t));
                    else if (mode == 2) v = (i % 997 == 0) ? 0.95f : 0.0f;
                    else if (mode == 3) v = (std::sin (2 * kPi * 50.0 * t) > 0) ? 0.8f : -0.8f;
                    else v = 1e-30f * (u (g) - 0.5f);
                    a[(size_t) i] = v;
                    k[(size_t) i] = (u (g) < 0.5f) ? 0.7f * (float) std::sin (2 * kPi * (40.0 + 80.0 * u (g)) * t) : 0.0f;
                    peakIn = std::max (peakIn, std::fabs (v));
                }
                Params p;
                p.contrast = u (g) * 2 - 1;
                p.punch = u (g) * 2 - 1;
                p.sustain = u (g) * 2 - 1;
                p.kick = u (g);
                p.rangeHz = 60.0f + 240.0f * u (g);
                p.match = u (g) > 0.5f;
                p.align = u (g) > 0.5f ? 1 : 0;
                p.kickMode = u (g) > 0.5f ? 1 : 0;
                d.setParams (p);
                float* c[1] = { a.data() };
                const float* s[1] = { k.data() };
                d.process (c, 1, n, u (g) < 0.8f ? s : nullptr, u (g) < 0.8f ? 1 : 0);
                for (int i = 0; i < n; ++i)
                {
                    finite = finite && std::isfinite (a[(size_t) i]);
                    peakOut = std::max (peakOut, std::fabs (a[(size_t) i]));
                }
            }
            CHECK (finite && peakOut < 20.0f * std::max (peakIn, 0.5f) && d.spectralGaps() == 0, "fuzz (600 random blocks, all parameters): finite, peak %.2f (in %.2f), no late duck", peakOut, peakIn);
        }

        // 5. a kick that goes away: the polarity decision must not outlive it
        {
            auto make = [&] (std::vector<float>& bass, std::vector<float>& kick) {
                bass.assign ((size_t) (40 * kFs), 0.0f);
                kick = bass;
                for (int b = 0; b < 12; ++b)
                {
                    const size_t n0 = (size_t) ((0.3 + 0.6 * b) * kFs);
                    for (size_t i = 0; i < (size_t) (0.4 * kFs); ++i)
                    {
                        const double t = (double) i / kFs, e = std::exp (-t / 0.25) * std::min (1.0, t / 0.01), s = std::sin (2 * kPi * 55.0 * t);
                        bass[n0 + i] = (float) (-0.4 * e * s);
                        kick[n0 + i] = (float) (0.5 * e * s);
                    }
                }
                for (size_t i = (size_t) (8 * kFs); i < bass.size(); ++i)
                    bass[i] = (float) (0.3 * std::sin (2 * kPi * 55.0 * (double) i / kFs)); // later: bass alone, no kick
            };
            std::vector<float> bass, kick;
            make (bass, kick);
            Params p = neutral;
            p.align = 1;
            Definition d;
            d.prepare (kFs);
            d.setParams (p);
            bool flippedEarly = false;
            for (size_t i = 0; i < bass.size(); i += 480)
            {
                float* c[1] = { bass.data() + i };
                const float* s[1] = { kick.data() + i };
                d.process (c, 1, (int) std::min<size_t> (480, bass.size() - i), s, 1);
                if (i < (size_t) (7 * kFs) && d.polarityFlipped())
                    flippedEarly = true;
            }
            CHECK (flippedEarly && ! d.polarityFlipped(), "polarity flipped while the kick played (%d), and released after 20 s without a kick (%d)", (int) flippedEarly, (int) d.polarityFlipped());
        }

        // 6. five minutes of continuous playing: nothing drifts, nothing overflows
        {
            Definition d;
            d.prepare (kFs);
            Params p;
            p.contrast = 0.6f;
            p.punch = 0.4f;
            p.sustain = 0.3f;
            p.kick = 0.7f;
            d.setParams (p);
            std::vector<float> a (4800), k (4800);
            bool finite = true;
            long long tracked = 0, total = 0;
            for (long long blk = 0; blk < 3000; ++blk)
            {
                for (int i = 0; i < 4800; ++i)
                {
                    const double t = (double) (blk * 4800 + i) / kFs;
                    const double note = 41.2 * std::pow (2.0, (double) ((long long) (t * 3) % 5) / 6.0);
                    a[(size_t) i] = (float) (0.3 * std::sin (2 * kPi * note * t) * (0.5 + 0.5 * std::sin (2 * kPi * 3.0 * t)));
                    k[(size_t) i] = (float) (0.5 * std::sin (2 * kPi * 55.0 * t) * std::exp (-std::fmod (t, 0.5) / 0.1));
                }
                float* c[1] = { a.data() };
                const float* s[1] = { k.data() };
                d.process (c, 1, 4800, s, 1);
                for (float v : a)
                    finite = finite && std::isfinite (v);
                if (blk > 2900)
                {
                    ++total;
                    tracked += d.pitchHz() > 0;
                }
            }
            CHECK (finite && d.spectralGaps() == 0 && tracked > total / 2, "5 minutes: finite, no late duck, still tracking the pitch at the end (%lld/%lld blocks)", tracked, total);
        }

        // 7. a sidechain that is the bass itself (a routing mistake) must not make the plug-in duck its own bass
        {
            const auto bass = held (kFs, 41.2, 6.0, 64.0, 0.4);
            Params p = neutral;
            p.kick = 1.0f;
            Definition d;
            d.prepare (kFs);
            const auto o = run (d, bass, 480, p, false, &bass);
            const int lat = d.latencySamples();
            double ea = 0, ei = 0;
            for (size_t i = (size_t) (3 * kFs) + (size_t) lat; i < (size_t) (5.5 * kFs); ++i)
            {
                const double a = o.out[i] - bass[i - (size_t) lat];
                ea += a * a;
                ei += (double) bass[i - (size_t) lat] * bass[i - (size_t) lat];
            }
            CHECK (d.sidechainIsBass() && 10 * std::log10 (ea / ei + 1e-20) < -50.0, "the bass as its own sidechain is recognised (%d) and left alone (%.0f dB re the bass)", (int) d.sidechainIsBass(), 10 * std::log10 (ea / ei + 1e-20));
            std::vector<float> kick (bass.size(), 0.0f);
            for (int b = 0; b < 8; ++b)
                for (size_t i = 0; i < (size_t) (0.18 * kFs); ++i)
                    kick[(size_t) ((0.5 + 0.6 * b) * kFs) + i] = (float) (0.8 * std::exp (-(double) i / kFs / 0.06) * std::sin (2 * kPi * 64.0 * (double) i / kFs));
            Definition dk;
            dk.prepare (kFs);
            run (dk, bass, 480, p, false, &kick);
            CHECK (! dk.sidechainIsBass(), "a real kick is not mistaken for the bass");
        }

        // 8. a kick that is only bleed (about -80 dBFS) is no reason to duck anything
        {
            const auto bass = held (kFs, 41.2, 4.0, 64.0, 0.4);
            for (int loud = 0; loud < 2; ++loud)
            {
                std::vector<float> kick (bass.size(), 0.0f);
                for (int b = 0; b < 6; ++b)
                    for (size_t i = 0; i < (size_t) (0.18 * kFs); ++i)
                        kick[(size_t) ((0.5 + 0.6 * b) * kFs) + i] = (float) ((loud ? 0.8 : 0.0001) * std::exp (-(double) i / kFs / 0.06) * std::sin (2 * kPi * 64.0 * (double) i / kFs));
                Params p = neutral;
                p.kick = 1.0f;
                Definition d;
                d.prepare (kFs);
                const auto o = run (d, bass, 480, p, false, &kick);
                const double tHit = 0.5 + 0.6 * 3 + 0.04, sh = (double) d.latencySamples() / kFs;
                const double mud = ampDb (o.out, kFs, 64.0, tHit + sh, 6) - ampDb (bass, kFs, 64.0, tHit, 6);
                if (loud)
                    CHECK (mud < -3.0, "a normal kick still ducks the mud (%+.1f dB)", mud);
                else
                    CHECK (std::fabs (mud) < 0.5, "kick bleed at -80 dBFS: the mud is not touched (%+.1f dB)", mud);
            }
        }
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

// Renders demo WAVs that exercise the spatialiser: move a test source around the head.
// Usage: render_demo hrtf.bin out_dir
// Listen on headphones.
#include "../Source/dsp/Spatializer.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <random>
#include <string>

using namespace spat;

static constexpr double kFs = 48000.0;
static constexpr float kPi = 3.14159265f;

static void writeWav16 (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    float peak = 1e-9f;
    for (size_t i = 0; i < l.size(); ++i)
        peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
    const float g = 0.89f / peak; // -1 dBFS
    const uint32_t n = (uint32_t) l.size(), dataBytes = n * 4, rate = (uint32_t) kFs;
    std::ofstream o (path, std::ios::binary);
    auto w32 = [&] (uint32_t v) { o.write ((const char*) &v, 4); };
    auto w16 = [&] (uint16_t v) { o.write ((const char*) &v, 2); };
    o.write ("RIFF", 4); w32 (36 + dataBytes); o.write ("WAVEfmt ", 8);
    w32 (16); w16 (1); w16 (2); w32 (rate); w32 (rate * 4); w16 (4); w16 (16);
    o.write ("data", 4); w32 (dataBytes);
    for (size_t i = 0; i < l.size(); ++i)
    {
        w16 ((uint16_t) (int16_t) std::lround (std::clamp (l[i] * g, -1.0f, 1.0f) * 32767.0f));
        w16 ((uint16_t) (int16_t) std::lround (std::clamp (r[i] * g, -1.0f, 1.0f) * 32767.0f));
    }
}

// position on a circle in the median plane: phi 0 = front, 90 = overhead, 180 = back, 270 = below
static void medianPlane (float phiDeg, float& az, float& el)
{
    const float p = std::fmod (phiDeg, 360.0f) * kPi / 180.0f;
    const float y = std::cos (p), z = std::sin (p);
    az = y >= 0.0f ? 0.0f : 180.0f;
    el = std::asin (std::clamp (z, -1.0f, 1.0f)) * 180.0f / kPi;
}

struct Segment
{
    float seconds;
    std::function<SpatParams (float t01)> at;
};

static void renderSegments (Spatializer& sp, const std::vector<Segment>& segs, const std::vector<float>& source,
                            std::vector<float>& outL, std::vector<float>& outR)
{
    size_t pos = 0;
    constexpr int block = 64;
    for (const auto& seg : segs)
    {
        const size_t len = (size_t) (seg.seconds * kFs);
        for (size_t i = 0; i < len; i += block)
        {
            const int n = (int) std::min<size_t> (block, len - i);
            const SpatParams p = seg.at ((float) i / (float) len);
            const size_t o = outL.size();
            outL.resize (o + (size_t) n);
            outR.resize (o + (size_t) n);
            sp.process (source.data() + pos + i, nullptr, outL.data() + o, outR.data() + o, n, p);
        }
        pos += len;
    }
}

static std::vector<float> pulsedPink (double seconds)
{
    std::mt19937 g (7);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    const size_t n = (size_t) (seconds * kFs);
    std::vector<float> v (n);
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    const int period = (int) (0.25 * kFs), on = (int) (0.14 * kFs);
    for (size_t i = 0; i < n; ++i)
    {
        const float w = d (g);
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        const float pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f) * 0.11f;
        b6 = w * 0.115926f;
        const int ph = (int) (i % (size_t) period);
        float env = 0.0f;
        if (ph < on)
            env = 0.5f * (1.0f - std::cos (2.0f * kPi * (float) ph / (float) on));
        v[i] = pink * env;
    }
    return v;
}

// A warm FM electric-piano arpeggio (Cm9), 8th notes at 84 bpm
static std::vector<float> keysLine (double seconds)
{
    const size_t n = (size_t) (seconds * kFs);
    std::vector<float> v (n, 0.0f);
    const float notes[] = { 48, 55, 58, 62, 65, 62, 58, 55, 51, 58, 62, 65, 67, 65, 62, 58 }; // MIDI
    const double step = 60.0 / 84.0 / 2.0;
    for (size_t k = 0; (double) k * step < seconds; ++k)
    {
        const float f = 440.0f * std::pow (2.0f, (notes[k % 16] + 12 - 69) / 12.0f);
        const size_t start = (size_t) ((double) k * step * kFs);
        const size_t dur = (size_t) (1.6 * kFs);
        for (size_t i = 0; i < dur && start + i < n; ++i)
        {
            const float t = (float) i / (float) kFs;
            const float idx = 0.3f + 2.2f * std::exp (-t * 5.0f);
            const float tine = std::exp (-t * 14.0f);
            const float mod = std::sin (2.0f * kPi * f * t) * idx;
            const float body = std::sin (2.0f * kPi * f * t + mod);
            const float bell = std::sin (2.0f * kPi * f * 14.0f * t + 0.5f * mod) * 0.12f * tine;
            const float att = std::min (1.0f, t * 400.0f);
            v[start + i] += 0.25f * (body + bell) * std::exp (-t * 1.7f) * att;
        }
    }
    return v;
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage: render_demo hrtf.bin out_dir\n");
        return 2;
    }
    std::ifstream f (argv[1], std::ios::binary);
    std::vector<char> blob ((std::istreambuf_iterator<char> (f)), {});
    const std::string dir = argv[2];

    auto base = [] (float az, float el, float dist, float focus = 0.8f, float room = 0.15f, float rear = 0.6f) {
        SpatParams p;
        p.azimuthDeg = az; p.elevationDeg = el; p.distance = dist; p.room = room; p.decay = 0.35f; p.focus = focus;
        p.rear = rear;
        return p;
    };

    // ---- demo 1: test signal tour ----------------------------------------------
    {
        std::vector<Segment> segs = {
            { 4.0f, [&] (float) { return base (0, 0, 0.25f); } },                                   // front, static
            { 12.0f, [&] (float t) { return base (720.0f * t, 0, 0.25f); } },                       // 2 laps clockwise
            { 3.0f, [&] (float) { return base (0, 0, 0.25f); } },
            { 3.0f, [&] (float) { return base (180, 0, 0.25f); } },                                 // directly behind
            { 3.0f, [&] (float) { return base (0, 60, 0.25f); } },                                  // above front
            { 3.0f, [&] (float) { return base (0, -40, 0.25f); } },                                 // below front
            { 12.0f, [&] (float t) { float az, el; medianPlane (360.0f * t, az, el); return base (az, el, 0.25f); } }, // front > over > back > under
            { 10.0f, [&] (float t) { const float d = 0.5f - 0.5f * std::cos (2.0f * kPi * t);       // far -> near -> far at 60 deg right
                                     return base (60, 0, 0.9f - 0.9f * d); } },
        };
        float total = 0;
        for (auto& s : segs) total += s.seconds;
        const auto src = pulsedPink (total);
        Spatializer sp;
        sp.prepare (kFs, 64, blob.data(), blob.size());
        std::vector<float> l, r;
        renderSegments (sp, segs, src, l, r);
        writeWav16 (dir + "/demo_1_test_signal_tour.wav", l, r);
        std::printf ("wrote demo_1_test_signal_tour.wav (%.0f s)\n", total);
    }

    // ---- demo 3: focus A/B. Same static positions at focus 0, 0.5, 1 (room nearly off) ----
    {
        std::vector<Segment> segs;
        const float positions[][2] = { { 0, 0 }, { 180, 0 }, { 0, 0 }, { 180, 0 }, { 0, 60 }, { 0, -40 }, { 0, 0 }, { 0, 60 } };
        for (float focus : { 0.0f, 0.5f, 1.0f })
        {
            for (const auto& pos : positions)
            {
                const float az = pos[0], el = pos[1];
                segs.push_back ({ 1.5f, [=] (float) { return base (az, el, 0.25f, focus, 0.05f); } });
            }
            segs.push_back ({ 1.5f, [=] (float) { return base (0, 0, 0.25f, focus, 0.05f); } }); // breath between sets
        }
        float total = 0;
        for (auto& s : segs) total += s.seconds;
        const auto src = pulsedPink (total);
        Spatializer sp;
        sp.prepare (kFs, 64, blob.data(), blob.size());
        std::vector<float> l, r;
        renderSegments (sp, segs, src, l, r);
        writeWav16 (dir + "/demo_3_focus_ab.wav", l, r);
        std::printf ("wrote demo_3_focus_ab.wav (%.0f s)\n", total);
    }

    // ---- demo 4: back cue A/B. Focus 1.0; Rear 0, 0.5, 1.0. front/back/front/back + 135/-135 deg ----
    {
        std::vector<Segment> segs;
        const float azs[] = { 0, 180, 0, 180, 135, 0, -135, 180 };
        for (float rear : { 0.0f, 0.5f, 1.0f })
        {
            for (float az : azs)
                segs.push_back ({ 1.5f, [=] (float) { return base (az, 0, 0.25f, 1.0f, 0.1f, rear); } });
            segs.push_back ({ 1.5f, [=] (float) { return base (0, 0, 0.25f, 1.0f, 0.1f, rear); } });
        }
        // then a slow lap so you can hear the image pass behind you
        segs.push_back ({ 12.0f, [=] (float t) { return base (360.0f * t, 0, 0.25f, 1.0f, 0.1f, 1.0f); } });
        float total = 0;
        for (auto& s : segs) total += s.seconds;
        const auto src = pulsedPink (total);
        Spatializer sp;
        sp.prepare (kFs, 64, blob.data(), blob.size());
        std::vector<float> l, r;
        renderSegments (sp, segs, src, l, r);
        writeWav16 (dir + "/demo_4_rear_ab.wav", l, r);
        std::printf ("wrote demo_4_rear_ab.wav (%.0f s)\n", total);
    }

    // ---- demo 5: depth. Keys dead ahead; Depth sweeps, then a lateral close-up ----
    {
        auto withDepth = [&] (float az, float depth) {
            SpatParams p = base (az, 0, 0.25f, 0.8f, 0.25f, 0.6f);
            p.depth = depth;
            return p;
        };
        std::vector<Segment> segs = {
            { 6.0f, [&] (float) { return withDepth (0, 0.0f); } },                                   // neutral reference
            { 10.0f, [&] (float t) { return withDepth (0, -t); } },                                   // glide to far away
            { 4.0f, [&] (float) { return withDepth (0, -1.0f); } },
            { 14.0f, [&] (float t) { return withDepth (0, -1.0f + 2.0f * t); } },                     // far -> right at your face
            { 4.0f, [&] (float) { return withDepth (0, 1.0f); } },
            { 10.0f, [&] (float t) { return withDepth (0, 1.0f - 2.0f * t); } },                      // back to far
            { 6.0f, [&] (float) { return withDepth (70, 1.0f); } },                                   // close to the right ear
            { 6.0f, [&] (float) { return withDepth (-70, 1.0f); } },                                  // close to the left ear
        };
        float total = 0;
        for (auto& s : segs) total += s.seconds;
        const auto src = keysLine (total);
        Spatializer sp;
        sp.prepare (kFs, 64, blob.data(), blob.size());
        std::vector<float> l, r;
        renderSegments (sp, segs, src, l, r);
        writeWav16 (dir + "/demo_5_depth.wav", l, r);
        std::printf ("wrote demo_5_depth.wav (%.0f s)\n", total);
    }

    // ---- demo 2: electric piano moving through space ---------------------------
    {
        std::vector<Segment> segs = {
            { 8.0f, [&] (float) { return base (0, 0, 0.3f); } },                                    // centred reference
            { 24.0f, [&] (float t) { return base (360.0f * t * 2.0f, 25.0f * std::sin (4.0f * kPi * t), 0.3f); } },
            { 16.0f, [&] (float t) { return base (90.0f * std::sin (2.0f * kPi * t * 2.0f), 0, 0.3f + 0.3f * t); } },
        };
        float total = 0;
        for (auto& s : segs) total += s.seconds;
        const auto src = keysLine (total);
        Spatializer sp;
        sp.prepare (kFs, 64, blob.data(), blob.size());
        std::vector<float> l, r;
        renderSegments (sp, segs, src, l, r);
        writeWav16 (dir + "/demo_2_keys_orbit.wav", l, r);
        std::printf ("wrote demo_2_keys_orbit.wav (%.0f s)\n", total);
    }
    return 0;
}

// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 16: the port's sharpening (shadps4/video_core/host_shaders/rcas_extra.glsl), its GLSL
// compiled as C++ over a small vec3: the three encodings there and back, 0 as off, AMD's RCAS on a
// display-encoded picture and in gamma 2.0, on the HDR scene through AMD's reversible tone mapper
// with its highlights sharpened and held under their cap, and the numbers its comments give.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

namespace glsl {
using uint = unsigned;
struct vec3 {
    float r = 0, g = 0, b = 0;
    vec3() = default;
    explicit vec3(float v) : r(v), g(v), b(v) {}
    vec3(float r_, float g_, float b_) : r(r_), g(g_), b(b_) {}
};
inline vec3 operator+(vec3 a, vec3 b) {
    return {a.r + b.r, a.g + b.g, a.b + b.b};
}
inline vec3 operator*(vec3 a, vec3 b) {
    return {a.r * b.r, a.g * b.g, a.b * b.b};
}
inline vec3 operator*(float s, vec3 a) {
    return {s * a.r, s * a.g, s * a.b};
}
inline vec3 operator/(vec3 a, float s) {
    return {a.r / s, a.g / s, a.b / s};
}
inline float max(float a, float b) {
    return a > b ? a : b;
}
inline float min(float a, float b) {
    return a < b ? a : b;
}
inline float abs(float a) {
    return std::fabs(a);
}
inline float sqrt(float a) {
    return std::sqrt(a);
}
inline float exp2(float a) {
    return std::exp2(a);
}
inline vec3 max(vec3 a, vec3 b) {
    return {max(a.r, b.r), max(a.g, b.g), max(a.b, b.b)};
}
inline vec3 min(vec3 a, vec3 b) {
    return {min(a.r, b.r), min(a.g, b.g), min(a.b, b.b)};
}
inline vec3 sqrt(vec3 a) {
    return {sqrt(a.r), sqrt(a.g), sqrt(a.b)};
}
#include "video_core/host_shaders/rcas_extra.glsl"
} // namespace glsl

using glsl::vec3;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

float Max3(vec3 c) {
    return std::fmax(c.r, std::fmax(c.g, c.b));
}
bool Same(vec3 a, vec3 b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}
bool Near(vec3 a, vec3 b, float relative) {
    const auto near = [&](float x, float y) {
        return std::fabs(x - y) <= relative * std::fmax(1e-6f, std::fmax(std::fabs(x), std::fabs(y)));
    };
    return near(a.r, b.r) && near(a.g, b.g) && near(a.b, b.b);
}
// fp32 resolves the tone mapper's 1 to about 6e-8, so the inverse's result to about 1e-7 times
// twice its value plus one (a third of a percent near fp16's largest).
float HdrTolerance(vec3 want) {
    return std::fmax(3e-3f, 4e-7f * (2.0f * Max3(want) + 2.0f));
}
// The five pixels' luma range (AMD's: luma times 2).
double LumaRange(const vec3 (&p)[5]) {
    double lo = 1e30, hi = -1e30;
    for (const vec3& c : p) {
        const double l = c.b * 0.5 + (c.r * 0.5 + c.g);
        lo = std::fmin(lo, l);
        hi = std::fmax(hi, l);
    }
    return hi - lo;
}
bool Finite(vec3 c) {
    return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b);
}

vec3 Sharpen(vec3 ring, vec3 centre, float s, unsigned encoding) {
    return glsl::RcasSharpen(ring, ring, centre, ring, ring, s, encoding);
}

// AMD's FsrRcasF (fsr/ffx_fsr1.h, FSR_RCAS_DENOISE) on values in 0..1, in double, with exact
// reciprocals (and a ring at 0 no limit, as AMD's 0 / 0 is on most GPUs), for comparison.
struct Rcas {
    double out[3];
};
Rcas AmdRcas(const vec3 (&p)[5], double con) {
    // p: b, d, e, f, h.
    double c[5][3];
    for (int i = 0; i < 5; ++i) {
        c[i][0] = p[i].r;
        c[i][1] = p[i].g;
        c[i][2] = p[i].b;
    }
    double l[5];
    for (int i = 0; i < 5; ++i) {
        l[i] = c[i][2] * 0.5 + (c[i][0] * 0.5 + c[i][1]);
    }
    double nz = 0.25 * l[0] + 0.25 * l[1] + 0.25 * l[3] + 0.25 * l[4] - l[2];
    const double lmax = std::fmax(std::fmax(std::fmax(l[0], l[1]), std::fmax(l[2], l[3])), l[4]);
    const double lmin = std::fmin(std::fmin(std::fmin(l[0], l[1]), std::fmin(l[2], l[3])), l[4]);
    nz = std::fmin(1.0, std::fabs(nz) / (lmax - lmin));
    nz = -0.5 * nz + 1.0;
    double lobe = -1e30;
    for (int k = 0; k < 3; ++k) {
        const double mn4 = std::fmin(std::fmin(c[0][k], c[1][k]), std::fmin(c[3][k], c[4][k]));
        const double mx4 = std::fmax(std::fmax(c[0][k], c[1][k]), std::fmax(c[3][k], c[4][k]));
        const double hit_min = mx4 > 0 ? std::fmin(mn4, c[2][k]) / (4.0 * mx4) : 1.0;
        const double hit_max = (1.0 - std::fmax(mx4, c[2][k])) / (4.0 * mn4 - 4.0);
        lobe = std::fmax(lobe, std::fmax(-hit_min, hit_max));
    }
    lobe = std::fmax(-(0.25 - 1.0 / 16.0), std::fmin(lobe, 0.0)) * con * nz;
    Rcas r{};
    for (int k = 0; k < 3; ++k) {
        r.out[k] = (lobe * (c[0][k] + c[1][k] + c[3][k] + c[4][k]) + c[2][k]) / (4.0 * lobe + 1.0);
    }
    return r;
}

// AMD's APrxMedRcpF1 (fsr/ffx_a.h).
float AmdApproxRcp(float a) {
    unsigned u;
    std::memcpy(&u, &a, 4);
    u = 0x7ef19fffu - u;
    float b;
    std::memcpy(&b, &u, 4);
    return b * (-b * a + 2.0f);
}

} // namespace

int main() {
    // The encodings there and back (a pixel RCAS leaves alone is returned as read, but every other
    // one goes through both).
    const float values[] = {0.0f, 1e-6f, 0.001f, 0.18f, 0.5f, 1.0f, 2.0f, 16.0f, 100.0f, 1000.0f};
    for (float x : values) {
        for (float y : values) {
            const vec3 c{x, y, 0.5f * x};
            check(Near(glsl::RcasDecode(glsl::RcasEncode(c, 0), 0), c, 2e-4f), "HDR: there and back");
            check(Max3(glsl::RcasEncode(c, 0)) < 1.0f, "HDR: encoded below 1");
            if (x <= 1 && y <= 1) {
                check(Near(glsl::RcasDecode(glsl::RcasEncode(c, 1), 1), c, 1e-5f), "linear: there and back");
            }
            check(Same(glsl::RcasDecode(glsl::RcasEncode(c, 2), 2), c), "encoded: as it is");
        }
    }
    // The inverse at the tone mapper's 1 (a result RCAS's guard allows): AMD's floor, 32768.
    check(Same(glsl::RcasDecode(vec3(1.0f, 0.5f, 0.0f), 0), vec3(32768.0f, 8192.0f, 0.0f)), "HDR: the inverse at 1");
    // Monotonic: a brighter input stays brighter encoded.
    for (unsigned encoding = 0; encoding < 3; ++encoding) {
        float last = glsl::RcasEncode(vec3(0.0f), encoding).r;
        for (float x = 1e-4f; x <= (encoding ? 1.0f : 10000.0f); x *= 1.01f) {
            const float e = glsl::RcasEncode(vec3(x), encoding).r;
            check(e > last, "the encodings keep the order of brightness");
            last = e;
        }
    }

    // 0 (or less) is off: the centre exactly as read, whatever the ring; above 1, 1.
    const vec3 edge_ring{0.2f, 0.25f, 0.3f}, edge_centre{0.6f, 0.55f, 0.5f};
    for (unsigned encoding = 0; encoding < 3; ++encoding) {
        check(Same(Sharpen(edge_ring, edge_centre, 0.0f, encoding), edge_centre), "0: off");
        check(Same(Sharpen(edge_ring, edge_centre, -1.0f, encoding), edge_centre), "below 0: off");
        check(Same(Sharpen(edge_ring, edge_centre, 2.0f, encoding), Sharpen(edge_ring, edge_centre, 1.0f, encoding)),
              "above 1: 1");
    }

    // A display-encoded picture: AMD's RCAS in full, the strength its lobe's share (FsrRcasCon's con).
    std::mt19937 random(16);
    std::uniform_real_distribution<float> unit(0.02f, 0.98f);
    for (int i = 0; i < 20000; ++i) {
        vec3 p[5];
        for (auto& c : p) {
            c = {unit(random), unit(random), unit(random)};
        }
        const float s = float(i % 11) / 10.0f;
        const vec3 got = glsl::RcasSharpen(p[0], p[1], p[2], p[3], p[4], s, 2);
        if (s == 0) {
            check(Same(got, p[2]), "encoded: 0 is the centre");
            continue;
        }
        const Rcas want = AmdRcas(p, s);
        check(Near(got, vec3(float(want.out[0]), float(want.out[1]), float(want.out[2])), 2e-5f),
              "encoded: AMD's RCAS");
        // Linear 0..1: the same on its square roots, squared after.
        vec3 lin[5];
        for (int k = 0; k < 5; ++k) {
            lin[k] = p[k] * p[k];
        }
        const vec3 got_linear = glsl::RcasSharpen(lin[0], lin[1], lin[2], lin[3], lin[4], s, 1);
        check(Near(got_linear, got * got, 1e-4f), "linear: AMD's RCAS in gamma 2.0");
    }
    // The strength: the change grows with it, about in proportion (the lobe's share).
    {
        float last = 0;
        for (float s = 0.1f; s <= 1.0f; s += 0.1f) {
            const float change = Sharpen(edge_ring, edge_centre, s, 2).r - edge_centre.r;
            check(change > last, "a larger strength sharpens more");
            last = change;
        }
        const float half = Sharpen(edge_ring, edge_centre, 0.5f, 2).r - edge_centre.r;
        const float full = Sharpen(edge_ring, edge_centre, 1.0f, 2).r - edge_centre.r;
        check(half > 0.4f * full && half < 0.6f * full, "50% about half of 100%");
        const float tiny = Sharpen(edge_ring, edge_centre, 0.01f, 2).r - edge_centre.r;
        check(tiny > 0 && tiny < 0.02f * full, "1% about a hundredth: no jump above 0");
    }
    // Where RCAS changes nothing (the centre at white beside a darker ring: AMD's guard), the
    // pixel as read.
    check(Same(Sharpen(vec3(0.5f), vec3(1.0f), 1.0f, 2), vec3(1.0f)), "a white centre: as read");
    // A saturated colour, its other channels at 0 across the ring (AMD's 0 / 0): still sharpened.
    {
        const vec3 red = Sharpen(vec3(0.2f, 0, 0), vec3(0.8f, 0, 0), 1.0f, 2);
        check(red.r > 0.8f && red.g == 0 && red.b == 0, "a pure red edge sharpened");
        check(Finite(Sharpen(vec3(0.0f), vec3(0.0f), 1.0f, 2)), "black: no division by zero");
        check(Same(Sharpen(vec3(1.0f), vec3(1.0f), 1.0f, 2), vec3(1.0f)), "white: as read");
        check(Same(Sharpen(vec3(1.5f), vec3(1.2f), 1.0f, 2), vec3(1.2f)), "above white: as read");
    }

    // The HDR scene: highlights sharpened, beside values above 1 as well (beta 20 left them).
    {
        const vec3 bright = Sharpen(vec3(1.5f), vec3(3.0f), 1.0f, 0);
        check(bright.r > 3.0f, "a highlight beside values above 1 sharpened up");
        const vec3 dark = Sharpen(vec3(1.5f), vec3(0.75f), 1.0f, 0);
        check(dark.r < 0.75f && dark.r >= 0, "a darker pixel beside them sharpened down");
        const vec3 lamp = Sharpen(vec3(0.05f), vec3(4.0f), 1.0f, 0);
        check(lamp.r > 4.0f && lamp.r <= 9.0f, "a lamp in the dark: sharpened, under its ceiling");
        // A pixel RCAS leaves (a channel at 0 beside a lit ring: AMD's guard): exactly as read, not
        // through the tone mapper and back.
        check(Same(Sharpen(vec3(1.1f, 0.5f, 2.2f), vec3(2.7182817f, 0.0f, 1.4142135f), 1.0f, 0),
                   vec3(2.7182817f, 0.0f, 1.4142135f)),
              "HDR: a pixel RCAS leaves, exactly as read");
        const vec3 flat = Sharpen(vec3(250.0f), vec3(250.0f), 1.0f, 0);
        check(Near(flat, vec3(250.0f), 1e-4f), "flat HDR: unchanged");
        const vec3 flat_dark = Sharpen(vec3(0.01f), vec3(0.01f), 1.0f, 0);
        check(Near(flat_dark, vec3(0.01f), 1e-5f), "flat dark: unchanged");
    }
    // AMD's RCAS on the encoded scene, the result held at twice the brightest of the five plus one
    // (scaled down whole), never below 0, always finite, whatever the light.
    std::uniform_real_distribution<float> stops(-12.0f, 14.0f);
    int capped = 0;
    for (int i = 0; i < 50000; ++i) {
        vec3 p[5], q[5];
        float brightest = 0;
        for (int k = 0; k < 5; ++k) {
            p[k] = {std::exp2(stops(random)), std::exp2(stops(random)), std::exp2(stops(random))};
            if (i % 7 == 0) {
                p[k].g = 0; // and with a channel at 0
            }
            brightest = std::fmax(brightest, Max3(p[k]));
            q[k] = glsl::RcasEncode(p[k], 0);
        }
        const vec3 got = glsl::RcasSharpen(p[0], p[1], p[2], p[3], p[4], 1.0f, 0);
        check(Finite(got) && got.r >= 0 && got.g >= 0 && got.b >= 0, "HDR: finite, at or above 0");
        const float cap = 2.0f * brightest + 1.0f;
        check(Max3(got) <= cap * 1.0001f, "HDR: under the cap");
        const Rcas amd = AmdRcas(q, 1.0);
        vec3 want = glsl::RcasDecode(vec3(float(amd.out[0]), float(amd.out[1]), float(amd.out[2])), 0);
        if (Max3(want) > cap) {
            want = (cap / Max3(want)) * want;
            ++capped;
        }
        if (LumaRange(q) >= 1e-4) {
            check(Near(got, want, HdrTolerance(want)), "HDR: AMD's RCAS on the encoded scene, capped");
        }
    }
    check(capped > 0, "HDR: the cap tested");
    // Grey and tinted edges and points never reach the cap: AMD's RCAS on the encoded scene alone.
    std::uniform_real_distribution<float> tint(0.2f, 1.0f);
    for (int i = 0; i < 20000; ++i) {
        const float a = std::exp2(stops(random) * 0.7f), c = std::exp2(stops(random) * 0.7f);
        const vec3 ta{tint(random), tint(random), tint(random)}, tc{tint(random), tint(random), tint(random)};
        static const int patterns[6][5] = {{1, 0, 1, 0, 0}, {1, 1, 1, 0, 0}, {0, 0, 1, 0, 0},
                                           {1, 1, 0, 0, 0}, {1, 0, 2, 0, 1}, {1, 1, 2, 0, 0}};
        const int* pattern = patterns[i % 6];
        const bool grey = i % 2 == 0;
        vec3 p[5], q[5];
        float brightest = 0;
        for (int k = 0; k < 5; ++k) {
            const vec3 bright = grey ? vec3(a) : a * ta, dark = grey ? vec3(c) : c * tc;
            p[k] = pattern[k] == 1 ? bright : pattern[k] == 0 ? dark : 0.5f * (bright + dark);
            brightest = std::fmax(brightest, Max3(p[k]));
            q[k] = glsl::RcasEncode(p[k], 0);
        }
        const Rcas amd = AmdRcas(q, 1.0);
        const vec3 want = glsl::RcasDecode(vec3(float(amd.out[0]), float(amd.out[1]), float(amd.out[2])), 0);
        check(Max3(want) <= (2.0f * brightest + 1.0f), "grey and tinted edges: under the cap already");
        // (Not where the bright and dark tints have almost the same luma: AMD's noise detection
        // divides by that difference, so float and double part there, as AMD's code does.)
        if (LumaRange(q) >= 1e-4) {
            check(Near(glsl::RcasSharpen(p[0], p[1], p[2], p[3], p[4], 1.0f, 0), want, HdrTolerance(want)),
                  "grey and tinted edges: AMD's RCAS on the encoded scene");
        }
    }

    // The comments' numbers: AMD's approximate reciprocal 0.32% low at 1 (nothing to sharpen), a
    // pixel at 100 back at 61 through the inverse; a blue of 0.47 standing out of its ring lifted
    // to 12 under AMD's ceiling alone, held at twice the brightest plus one.
    {
        const float rcp = AmdApproxRcp(1.0f);
        check(rcp > 0.9967f && rcp < 0.9969f, "APrxMedRcpF1(1) is 0.32% low");
        const vec3 hundred = glsl::RcasEncode(vec3(100.0f), 0);
        const float back = glsl::RcasDecode(rcp * hundred, 0).r;
        check(back > 60.5f && back < 61.5f, "100 back at 61");
        const vec3 p[5] = {{0.5582f, 0.3957f, 0.1405f}, {0.5097f, 0.2539f, 0.1224f}, {0.3933f, 0.1859f, 0.4694f},
                           {0.2822f, 0.3308f, 0.1403f}, {0.3269f, 0.3527f, 0.1253f}};
        vec3 q[5];
        for (int k = 0; k < 5; ++k) {
            q[k] = glsl::RcasEncode(p[k], 0);
        }
        const Rcas amd = AmdRcas(q, 1.0);
        const vec3 lifted = glsl::RcasDecode(vec3(float(amd.out[0]), float(amd.out[1]), float(amd.out[2])), 0);
        check(lifted.b > 11.5f && lifted.b < 13.0f, "AMD's ceiling alone: the blue at 12");
        const vec3 ours = glsl::RcasSharpen(p[0], p[1], p[2], p[3], p[4], 1.0f, 0);
        check(Max3(ours) <= 2.0f * 0.5582f + 1.0f + 1e-4f && ours.b > p[2].b, "the cap: sharpened, held");
        std::printf("rcas-test: a blue of 0.47 beside 0.12-0.14: %.2f under AMD's ceiling alone, %.2f held\n",
                    lifted.b, ours.b);
    }
    {
        const vec3 lamp = Sharpen(vec3(0.05f), vec3(4.0f), 1.0f, 0);
        std::printf("rcas-test: a lamp of 4 among 0.05 at 1: %.2f; an edge 1.5 | 3: %.2f, at 0.2: %.2f\n", lamp.r,
                    Sharpen(vec3(1.5f), vec3(3.0f), 1.0f, 0).r, Sharpen(vec3(1.5f), vec3(3.0f), 0.2f, 0).r);
    }

    if (g_fail) {
        std::printf("rcas-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("rcas-test: ok\n");
    return 0;
}

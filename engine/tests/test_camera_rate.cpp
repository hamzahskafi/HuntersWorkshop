// SPDX-License-Identifier: GPL-3.0-or-later
// The follow camera's weights every frame (gpu/shim/engine/camera_rate.cpp) on a synthetic image: no game files.
// The image holds 1.09's bytes at every site, the constructor's second set (the game's or Kyo's values), the debug
// menu's setup and the two constants the wall turn reads; Check must accept it for its tuning only and name the
// first address that is not as expected. Installed, each site is run natively, through its jump and its cave, by
// a harness that sets the camera (r13), the frame (rbp), every xmm register and the flags, and lands on a ret at
// the site's end (or the build-up's branch target): every converted value must be the game's conversion of it
// (to the bit at 30 FPS), every other register and the flags as they were, and the x87 stack empty.
#include "../gpu/shim/engine/camera_rate.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace {
using BbEngine::CameraRate::Convert;
using BbEngine::CameraRate::Field;
using BbEngine::CameraRate::Fpp;
constexpr std::uint64_t kBase = 0x400000;
constexpr std::size_t kSize = 0x4930000; // past the -0 mask (0x4cf2520) and 1 degree (0x4d25e58)

struct State {
    float xmm[16][4];
    std::uint64_t rbp, r14, rax, flags_before, flags_after;
    std::uint8_t fpu[28];
};
static_assert(offsetof(State, rbp) == 256 && offsetof(State, fpu) == 296);
} // namespace

// rdi: the code to call, rsi: the camera (r13), rdx: the state. Every xmm register, rbp, r14 and rax from the
// state; the flags from cmp 0, 1 (carry, sign, not zero); after the call the flags, rax, r14, the registers and the
// x87 environment back into it.
extern "C" __attribute__((sysv_abi)) void bb_run_cave(const void* code, void* camera, State* state);
asm(R"(
    .text
    .globl bb_run_cave
bb_run_cave:
    .intel_syntax noprefix
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    mov r12, rdi
    mov r13, rsi
    mov rbx, rdx
    vmovups xmm0, [rbx]
    vmovups xmm1, [rbx + 16]
    vmovups xmm2, [rbx + 32]
    vmovups xmm3, [rbx + 48]
    vmovups xmm4, [rbx + 64]
    vmovups xmm5, [rbx + 80]
    vmovups xmm6, [rbx + 96]
    vmovups xmm7, [rbx + 112]
    vmovups xmm8, [rbx + 128]
    vmovups xmm9, [rbx + 144]
    vmovups xmm10, [rbx + 160]
    vmovups xmm11, [rbx + 176]
    vmovups xmm12, [rbx + 192]
    vmovups xmm13, [rbx + 208]
    vmovups xmm14, [rbx + 224]
    vmovups xmm15, [rbx + 240]
    mov rbp, [rbx + 256]
    mov r14, [rbx + 264]
    mov rax, [rbx + 272]
    mov ecx, 0
    cmp ecx, 1
    pushfq
    pop qword ptr [rbx + 280]
    call r12
    pushfq
    pop qword ptr [rbx + 288]
    mov [rbx + 272], rax
    mov [rbx + 264], r14
    fnstenv [rbx + 296]
    fldenv [rbx + 296]
    vmovups [rbx], xmm0
    vmovups [rbx + 16], xmm1
    vmovups [rbx + 32], xmm2
    vmovups [rbx + 48], xmm3
    vmovups [rbx + 64], xmm4
    vmovups [rbx + 80], xmm5
    vmovups [rbx + 96], xmm6
    vmovups [rbx + 112], xmm7
    vmovups [rbx + 128], xmm8
    vmovups [rbx + 144], xmm9
    vmovups [rbx + 160], xmm10
    vmovups [rbx + 176], xmm11
    vmovups [rbx + 192], xmm12
    vmovups [rbx + 208], xmm13
    vmovups [rbx + 224], xmm14
    vmovups [rbx + 240], xmm15
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
    .att_syntax prefix
)");

namespace {
std::uint8_t* Allocate(std::size_t n) {
#ifdef _WIN32
    return static_cast<std::uint8_t*>(VirtualAlloc(nullptr, n, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
#else
    void* p = mmap(nullptr, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(p);
#endif
}
void Free(std::uint8_t* p, std::size_t n) {
#ifdef _WIN32
    (void)n;
    VirtualFree(p, 0, MEM_RELEASE);
#else
    munmap(p, n);
#endif
}
std::vector<std::uint8_t> Hex(const char* text) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; text[i] && text[i + 1]; i += 2) {
        unsigned v = 0;
        std::sscanf(text + i, "%2x", &v);
        out.push_back(std::uint8_t(v));
    }
    return out;
}
float F(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
std::uint32_t Bits(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
bool Near(double a, double b) { return std::fabs(a - b) <= 2e-7 * std::fmax(1.0, std::fabs(b)) + 1e-12; }
float Decay(float w, double n) { return float(1.0 - std::pow(1.0 - double(w), n)); }
float Keep(float w, double n) { return float(std::pow(double(w), n)); }

struct Image {
    std::uint8_t* base;
    std::uint8_t* at(std::uint64_t patch) const { return base + (patch - kBase); }
    void put(std::uint64_t patch, const std::vector<std::uint8_t>& bytes) const {
        std::memcpy(at(patch), bytes.data(), bytes.size());
    }
};

// 1.09's bytes at every site and constant, the constructor's second set with the game's or Kyo's values, the
// debug menu's setup, and a ret at each site's end (mov eax, 2; ret at the build-up's branch target).
Image Build(bool kyo) {
    Image image{Allocate(kSize)};
    assert(image.base);
    for (const auto& site : BbEngine::CameraRate::Sites()) {
        const auto bytes = Hex(site.bytes);
        image.put(site.at, bytes);
        image.put(site.at + bytes.size(), {0xc3});
    }
    image.put(0x183dc9b, {0xb8, 0x02, 0x00, 0x00, 0x00, 0xc3});
    image.put(0x4cf2520, Hex("00000080000000800000008000000080"));
    image.put(0x4d25e58, Hex("35fa8e3c"));
    for (const Field& f : BbEngine::CameraRate::Fields()) {
        std::uint8_t mov[10] = {0xc7, 0x83};
        std::memcpy(mov + 2, &f.field, 4);
        std::memcpy(mov + 6, kyo ? &f.kyo : &f.game, 4);
        image.put(f.at - 6, {mov, mov + 10});
    }
    image.put(0x183ff29, Hex("498bbd38030000"));
    image.put(0x1840799, Hex("498bbd38030000"));
    return image;
}

void TestFppValues() {
    for (const Field& f : BbEngine::CameraRate::Fields()) {
        const std::uint32_t v = BbEngine::CameraRate::FppValue(f);
        switch (f.field) {
        case 0x1a0:
        case 0x1b4: assert(Near(F(v), 0.0975)); break;                                     // his 0.05 at 60
        case 0x1a4: assert(v == f.kyo); break;                                             // a share: as it is
        case 0x1a8: assert(Near(F(v), 0.0025)); break;                                      // kept 0.05 at 60
        case 0x238: assert(v == f.kyo); break;                                             // 40 degrees
        case 0x23c: assert(Near(F(v), 0.0784)); break;                                      // his 0.04 at 60
        default: assert(f.fpp == Fpp::Game && v == f.game); break;                          // halved: the game's
        }
        // At 60 FPS (n = 1/2) the conversion gives his own value back.
        if (f.fpp == Fpp::Decay) assert(Near(Decay(F(v), 0.5), F(f.kyo)));
        if (f.fpp == Fpp::Keep) assert(Near(Keep(F(v), 0.5), F(f.kyo)));
    }
}

void TestCheck() {
    std::uint64_t bad = 0;
    {
        const Image image = Build(false);
        assert(BbEngine::CameraRate::Check(image.base, kSize, false, &bad));
        assert(!BbEngine::CameraRate::Check(image.base, kSize, true, &bad) && bad == 0x183a8c1);
        image.at(0x183e964)[3] ^= 1; // a site's byte
        assert(!BbEngine::CameraRate::Check(image.base, kSize, false, &bad) && bad == 0x183e964);
        assert(!BbEngine::CameraRate::Check(image.base, 0x1000000, false, &bad)); // too small an image
        Free(image.base, kSize);
    }
    {
        const Image image = Build(true);
        assert(BbEngine::CameraRate::Check(image.base, kSize, true, &bad));
        assert(!BbEngine::CameraRate::Check(image.base, kSize, false, &bad) && bad == 0x183a8c1);
        image.put(0x183ff29, Hex("41c7859c010000")); // the FPS++ list's stores in the debug menu's setup
        assert(!BbEngine::CameraRate::Check(image.base, kSize, true, &bad) && bad == 0x183ff29);
        Free(image.base, kSize);
    }
}

// What each site's own instructions write, besides what the test checks: xmm registers (a mask), rax, the flags.
struct Effect {
    unsigned xmm;
    bool rax, flags;
};
constexpr Effect kEffects[] = {
    {1u << 1 | 1u << 4, false, false},           // settings blend
    {1u << 2 | 1u << 3 | 1u << 4, false, false}, // character chase
    {1u << 0 | 1u << 10, false, false},          // lock-on turn
    {1u << 4 | 1u << 5, false, false},           // auto turn (ecx)
    {1u << 0, true, false},                      // pitch return (eax)
    {1u << 0 | 1u << 2, false, true},            // sideways pull-in
    {1u << 4, true, true},                       // wall turn build-up (test; r14; the branch target's eax)
    {1u << 0 | 1u << 1 | 1u << 2, false, false}, // wall turn fade and limit
    {1u << 1, true, true},                       // wall turn cut-off (eax; and)
    {1u << 0 | 1u << 1, false, false},           // point chase
    {1u << 0 | 1u << 12, false, false},          // point distance chase
    {1u << 0 | 1u << 2, false, false},           // fast turn pitch (up)
    {1u << 1, false, true},                      // fast turn pitch (down)
};

struct Run {
    State in{}, out{};
};

void TestCaves(bool kyo) {
    const Image image = Build(kyo);
    assert(BbEngine::CameraRate::Install(image.base, kSize, kyo));
    // FPS++: the second set holds the base plus Kyo's changes, as a 30 FPS frame's.
    for (const Field& f : BbEngine::CameraRate::Fields()) {
        std::uint32_t v;
        std::memcpy(&v, image.at(f.at), 4);
        assert(v == (kyo ? BbEngine::CameraRate::FppValue(f) : f.game));
    }
    // The site's first bytes: a jump to its cave.
    for (const auto& site : BbEngine::CameraRate::Sites()) assert(image.at(site.at)[0] == 0xff && image.at(site.at)[1] == 0x25);

    alignas(16) static std::uint8_t camera[0x400];
    alignas(16) static std::uint8_t frame[0x400];
    const auto field = [&](std::uint32_t offset, float v) { std::memcpy(camera + offset, &v, 4); };
    const auto run = [&](std::size_t index, Run& r) {
        r.in.rbp = reinterpret_cast<std::uint64_t>(frame + 0x300);
        r.out = r.in;
        bb_run_cave(image.at(BbEngine::CameraRate::Sites()[index].at), camera, &r.out);
        // The x87 stack is empty (the tag word all 1s) and nothing else changed.
        std::uint16_t tags;
        std::memcpy(&tags, r.out.fpu + 8, 2);
        assert(tags == 0xffff);
        const Effect& e = kEffects[index];
        for (unsigned x = 0; x < 16; ++x)
            if (!(e.xmm & (1u << x))) assert(!std::memcmp(r.in.xmm[x], r.out.xmm[x], 16));
        if (!e.rax) assert(r.out.rax == r.in.rax);
        constexpr std::uint64_t kStatus = 0x8d5; // carry, parity, adjust, zero, sign, overflow
        if (!e.flags) assert((r.out.flags_after & kStatus) == (r.out.flags_before & kStatus));
    };
    const auto fill = [](Run& r) {
        for (unsigned x = 0; x < 16; ++x)
            for (unsigned l = 0; l < 4; ++l) r.in.xmm[x][l] = float(100 * x + l) + 0.5f;
        r.in.rax = 0x1122334455667701ull;
        r.in.r14 = 0x7766554433221102ull;
    };

    for (const double n : {1.0, 0.5, 0.25, 0.125, 0.3017}) {
        BbEngine::CameraRate::Frame(n / 30.0);
        const float frames = BbEngine::CameraRate::Frames();
        assert(n == 1.0 ? frames == 1.0f : Near(frames, n));
        const auto decay = [&](float w) { return n == 1.0 ? w : Decay(w, frames); };
        const auto keep = [&](float w) { return n == 1.0 ? w : Keep(w, frames); };
        const auto same = [&](float got, float want) { return n == 1.0 ? Bits(got) == Bits(want) : Near(got, want); };
        for (const float w : {0.05f, 0.3f, 0.1f, 0.97f}) {
            Run r;
            // Settings blend: xmm4 = w', xmm1 = (xmm1 - xmm2) * w'.
            fill(r), field(0x330, w), run(0, r);
            assert(same(r.out.xmm[4][0], decay(w)) && r.out.xmm[4][1] == 0 && r.out.xmm[4][3] == 0);
            assert(r.out.xmm[1][0] == (r.in.xmm[1][0] - r.in.xmm[2][0]) * r.out.xmm[4][0]);
            // Character chase: xmm3 = w' in every lane; xmm4 += xmm2 * w'.
            fill(r), field(0x198, w), run(1, r);
            for (unsigned l = 0; l < 4; ++l) {
                assert(same(r.out.xmm[3][l], decay(w)));
                assert(r.out.xmm[4][l] == r.in.xmm[4][l] + r.in.xmm[2][l] * r.out.xmm[3][l]);
            }
            // Lock-on turn and auto turn: xmm10 / xmm5 = w'.
            fill(r), field(0x240, w), run(2, r);
            assert(same(r.out.xmm[10][0], decay(w)) && r.out.xmm[0][0] == r.out.xmm[10][0] * r.in.xmm[11][0]);
            fill(r), run(3, r);
            assert(same(r.out.xmm[5][0], decay(w)) && r.out.xmm[4][0] == r.in.xmm[2][0] - r.in.xmm[3][0]);
            // Pitch return: xmm0 = xmm1 + xmm0 * w kept.
            fill(r), field(0x1a8, w), run(4, r);
            const float kept = r.in.xmm[0][0] * (n == 1.0 ? w : float(std::pow(double(w), double(frames))));
            assert(same(r.out.xmm[0][0], r.in.xmm[1][0] + kept));
            // Point distance chase and the fast turn's pitch.
            fill(r), field(0x1c4, w), run(10, r);
            assert(same(r.out.xmm[0][0], r.in.xmm[1][0] + r.in.xmm[0][0] * decay(w)));
            assert(r.out.xmm[12][0] == r.out.xmm[0][0] / r.in.xmm[11][0]);
            fill(r), field(0x23c, w), run(11, r);
            assert(same(r.out.xmm[2][0], r.in.xmm[4][0] + r.in.xmm[0][0] * decay(w)));
            float stored;
            std::memcpy(&stored, frame + 0x300 - 0x238, 4);
            assert(stored == r.out.xmm[2][0]);
            fill(r), run(12, r);
            assert(same(r.out.xmm[1][0], r.in.xmm[4][0] + r.in.xmm[1][0] * decay(w)));
            (void)keep;
        }
        {
            // Sideways pull-in: the divisor times n; vucomiss against 0 after (above: no carry, no zero).
            Run r;
            fill(r), field(0x170, 0.3f), run(5, r);
            assert(same(r.out.xmm[0][0], n == 1.0 ? 0.3f : float(0.3f * frames)) && r.out.xmm[2][0] == 0);
            assert(!(r.out.flags_after & 0x41));
            // Weights outside (0, 1) as they are: 0, 1, above, negative.
            for (const float w : {0.0f, 1.0f, 1.5f, -0.25f}) {
                fill(r), field(0x198, w), run(1, r);
                assert(r.out.xmm[3][0] == w);
                fill(r), field(0x1a8, w), run(4, r);
                assert(r.out.xmm[0][0] == r.in.xmm[1][0] + r.in.xmm[0][0] * w);
            }
        }
        {
            // Point chase: the weight per axis, x, y and z converted, w (0) as it is.
            Run r;
            fill(r);
            const float rates[4] = {0.1f, 0.03f, 0.1f, 0.0f};
            for (unsigned l = 0; l < 4; ++l) r.in.xmm[4][l] = rates[l], r.in.xmm[0][l] = l < 3 ? 1 - rates[l] : 0;
            field(0x130, 0.25f), run(9, r);
            for (unsigned l = 0; l < 4; ++l) {
                const float share = rates[l] + r.in.xmm[0][l] * 0.25f;
                assert(l == 3 ? r.out.xmm[0][l] == share : same(r.out.xmm[0][l], decay(share)));
                assert(r.out.xmm[1][l] == 0.25f);
            }
        }
        {
            // Wall turn build-up: the step (lane 0 of [rbp - 0x250]) times n into xmm4; one probe (al & r14b 0)
            // takes the game's je to 0x183dc9b, both fall through with r14d cleared.
            alignas(16) const float step[4] = {1 / 60.0f, 7.0f, 8.0f, 9.0f};
            std::memcpy(frame + 0x300 - 0x250, step, 16);
            Run r;
            fill(r), r.in.rax = 1, r.in.r14 = 2, run(6, r);
            assert(r.out.rax == 2 && r.out.r14 == 2);
            assert(same(r.out.xmm[4][0], n == 1.0 ? step[0] : float(step[0] * frames)));
            assert(r.out.xmm[4][1] == 7 && r.out.xmm[4][2] == 8 && r.out.xmm[4][3] == 9);
            fill(r), r.in.rax = 1, r.in.r14 = 0x101, run(6, r);
            assert(r.out.rax == 1 && r.out.r14 == 0);
            // Fade and limit: xmm1 = the fade kept (1 as it is) times the turn; xmm2 = the step times +0x2b0;
            // xmm0 = -xmm2.
            for (const float fade : {0.55f, 1.0f}) {
                fill(r), r.in.xmm[1][0] = fade, r.in.xmm[0][0] = 0.02f, r.in.xmm[4][0] = 123.0f;
                field(0x2b0, 4.18879f), run(7, r);
                assert(same(r.out.xmm[1][0], keep(fade) * 0.02f));
                assert(r.out.xmm[2][0] == step[0] * 4.18879f && r.out.xmm[0][0] == -r.out.xmm[2][0]);
            }
            // Cut-off: |turn| below 1 degree times n.
            const float degree = F(0x3c8efa35), threshold = n == 1.0 ? degree : float(degree * frames);
            for (const float turn : {threshold * 0.99f, -threshold * 0.99f, threshold * 1.01f}) {
                fill(r), r.in.xmm[0][0] = turn, run(8, r);
                std::uint32_t mask;
                std::memcpy(&mask, &r.out.xmm[1][0], 4);
                assert(mask == (std::fabs(turn) < threshold ? 0xffffffffu : 0u));
                assert(r.out.rax == (Bits(turn) & 0x7fffffffu));
            }
        }
    }
    Free(image.base, kSize);
}
} // namespace

int main() {
    TestFppValues();
    TestCheck();
    TestCaves(false);
    TestCaves(true);
    std::puts("camera-rate-test: ok");
    return 0;
}

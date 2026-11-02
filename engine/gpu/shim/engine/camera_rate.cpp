// SPDX-License-Identifier: GPL-3.0-or-later
// The follow camera at every frame rate (camera_rate.h). Its update (ChrExFollowCam, patch 0x183ac60, once a frame
// on the main thread) eases, chases and turns by weights made for one 30 FPS frame; at 120 FPS each acted four times
// per PS4 frame, so the camera chased, swung and settled faster than on the PS4. Every per-frame quantity in the
// update was found with HW's own tools on the 1.09 executable (tools/re; the debug menu's camera names and bbhost's
// decompilation, droogie/bbhost src/decomp/camera/follow_camera.cpp, as a map only; no code taken), and each one
// is converted where the game uses it, from the game's 30 FPS frame to the frame's own length, n = 30 x seconds:
//
//   the settings blend (+0x330: distance, pitch floor, lock-on framing, look-at height, field of view toward the
//     LockCamParam row)                                                             a weight: 1 - (1 - w)^n
//   the chase of the character's position (+0x198, itself easing between +0x19c and +0x1a0)        a weight
//   lock-on's turn and the auto turn (+0x240, two sites)                                             a weight
//   the pitch's return to its reference (+0x1a8)                                              a share kept: w^n
//   the pull-in by the character's sideways move this frame (the move over +0x170: per frame, so the
//     divisor times n)                                                                       an amount: w x n
//   the wall turn (the new camera's probes; +0x294 is a turn applied each frame): its build-up (the escape rate
//     times the step added each frame: times n), its fade (+0x2b4: kept, w^n) and its 1 degree cut-off, which
//     zeroes and stores a smaller turn: times n while it fades, times n^2 on a frame that builds it up (at 30 a
//     turn starts only if its first step, rate / 30, reaches 1 degree; above 60 the game's never started, its
//     first step under the cut-off every frame); its limit is the step times +0x2b0 already, taken from the
//     game's own copy of the step
//   lock-on's play angle (+0x244): a turn whose step this frame stays inside it eases toward the play circle's
//     edge instead of the target; the step is compared as at 30 (times w / w', the weight over its conversion)
//   the camera point's chase of where it should be (+0x1ac/+0x1b8, under lock-on or an auto turn +0x1c8/
//     +0x1cc, cut by the angle to it and raised toward 1 by the pitch return's weight +0x130): the
//     resulting weight, per axis                                                                    a weight
//   the chase of that point's distance (+0x1c4)                                                       a weight
//   the fast turn's pitch (+0x23c, two sites)                                                         a weight
//
// The distance ease (patch 0x4D25E68) and the chase-rate ease (0x4D25E94) are constants frame_rate.cpp reads from
// per-frame slots. Not converted, because they already follow time: +0x1a4 with the follow weight +0x154 (the
// share of the character's move this frame the aimed-for point takes at once: shares of every frame's move add up
// to the same share of the whole move at any rate), the safe margin (+0x190, a placement each frame), the stick's
// turns, the timers and the moving floor (the step already). Left as they are: lock-on's stick offsets (+0x288,
// only with +0x276, which the constructor clears and the update never sets).
//
// Each site jumps to a cave of its own (engine_hooks Divert): it runs the displaced instructions, and around the
// value's use keeps every register and the flags as they were: past the red zone, rax and the flags pushed, a
// scratch on the stack, the conversion on the x87 stack (n log2 v, then 2^x as 2^fraction x 2^integer; empty
// before and after) with its own control word (64-bit precision, round to nearest; the caller's put back), the
// value put back from the scratch. At n = 1 every value is the game's to the bit.
#include "camera_rate.h"
#include "engine_hooks.h"
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace BbEngine::CameraRate {
namespace {
constexpr std::uint64_t kBase = 0x400000;

enum class Special : std::uint8_t {
    None,
    BuildUp,   ///< the displaced je goes to its target in the image; the step's lane 0 times n
    FadeLimit, ///< the fade kept, the limit from the game's step, the -0 mask copied into the cave
    Cutoff,    ///< the threshold (its rip-relative constant) times n into the scratch, n again on a build-up frame
    PlayAngle, ///< the step's square times (w / w')^2, w the lock-on turn's weight (+0x240), w' its conversion
};

/// A site's cave: `before` displaced bytes run first; the value into the scratch (a field of the camera, r13, or
/// `spill`); the lanes converted; `use` takes the converted scratch; after the restore, the displaced bytes from
/// `after` on; back to the site's end.
struct CaveDef {
    std::uint8_t before;
    std::int32_t field;
    const char* spill;
    std::uint8_t lanes;
    Convert kind;
    const char* use;
    std::uint8_t after;
    Special special;
};

constexpr Site kSites[] = {
    // vmovss xmm4, [r13 + 0x330]; vsubss xmm1, xmm1, xmm2; vmulss xmm1, xmm1, xmm4
    {"settings blend", 0x183ae75, "c4c17a10a530030000c5f25ccac5f259cc"},
    // vbroadcastss xmm3, [r13 + 0x198]; vmulps xmm2, xmm2, xmm3; vaddps xmm4, xmm4, xmm2
    {"character chase", 0x183ba97, "c4c279189d98010000c5e859d3c5d858e2"},
    // vmovss xmm10, [r13 + 0x240]; vmulss xmm0, xmm10, xmm11
    {"lock-on turn", 0x183c30f, "c4417a109540020000c4c12a59c3"},
    // vmovss xmm5, [r13 + 0x240]; vsubss xmm4, xmm2, xmm3; vmovmskps ecx, xmm4
    {"auto turn", 0x183c784, "c4c17a10ad40020000c5ea5ce3c5f850cc"},
    // vmulss xmm0, xmm0, [r13 + 0x1a8]; vaddss xmm0, xmm1, xmm0; vmovmskps eax, xmm0
    {"pitch return", 0x183cef0, "c4c17a5985a8010000c5f258c0c5f850c0"},
    // vmovss xmm0, [r13 + 0x170]; vxorps xmm2, xmm2, xmm2; vucomiss xmm0, xmm2
    {"sideways pull-in", 0x183d18f, "c4c17a108570010000c5e857d2c5f82ec2"},
    // test al, r14b; vmovaps xmm4, [rbp - 0x250]; je 0x183dc9b; xor r14d, r14d
    {"wall turn build-up", 0x183dc69, "4484f0c5f828a5b0fdffff74254531f6"},
    // vmulss xmm1, xmm1, xmm0; vmulss xmm2, xmm4, [r13 + 0x2b0]; vxorps xmm0, xmm2, [rip + -0 mask]
    {"wall turn fade and limit", 0x183de3d, "c5f259c8c4c15a5995b0020000c5e85705ce464b03"},
    // vmovd eax, xmm0; and eax, 0x7fffffff; vmovd xmm1, eax; vcmpltss xmm1, xmm1, [rip + 1 degree]
    {"wall turn cut-off", 0x183de5c, "c5f97ec025ffffff7fc5f96ec8c5f2c20de67f4e0301"},
    // vbroadcastss xmm1, [r13 + 0x130]; vmulps xmm0, xmm0, xmm1; vaddps xmm0, xmm4, xmm0
    {"point chase", 0x183e7e6, "c4c279188d30010000c5f859c1c5d858c0"},
    // vmulss xmm0, xmm0, [r13 + 0x1c4]; vaddss xmm0, xmm1, xmm0; vdivss xmm12, xmm0, xmm11
    {"point distance chase", 0x183e964, "c4c17a5985c4010000c5f258c0c4417a5ee3"},
    // vmulss xmm0, xmm0, [r13 + 0x23c]; vaddss xmm2, xmm4, xmm0; vmovss [rbp - 0x238], xmm2
    {"fast turn pitch (up)", 0x183f97e, "c4c17a59853c020000c5da58d0c5fa1195c8fdffff"},
    // vmulss xmm1, xmm1, [r13 + 0x23c]; vaddss xmm1, xmm4, xmm1; vucomiss xmm0, xmm1
    {"fast turn pitch (down)", 0x183fa9f, "c4c172598d3c020000c5da58c9c5f82ec1"},
    // vmulps xmm1, xmm1, xmm1; vpshufd xmm2, xmm1, 1; vaddss xmm1, xmm1, xmm2 (the step's square); vucomiss xmm0,
    // xmm1 (against the play angle's)
    {"lock-on play angle", 0x183c473, "c5f059c9c5f970d101c5f258cac5f82ec1"},
};

constexpr CaveDef kCaves[] = {
    {0, 0x330, nullptr, 1, Convert::Decay, "c5fa102424", 9, Special::None},          // vmovss xmm4, [rsp]
    {0, 0x198, nullptr, 1, Convert::Decay, "c4e279181c24", 9, Special::None},        // vbroadcastss xmm3, [rsp]
    {0, 0x240, nullptr, 1, Convert::Decay, "c57a101424", 9, Special::None},          // vmovss xmm10, [rsp]
    {0, 0x240, nullptr, 1, Convert::Decay, "c5fa102c24", 9, Special::None},          // vmovss xmm5, [rsp]
    {0, 0x1a8, nullptr, 1, Convert::Keep, "c5fa590424", 9, Special::None},           // vmulss xmm0, xmm0, [rsp]
    {0, 0x170, nullptr, 1, Convert::Amount, "c5fa100424", 9, Special::None},         // vmovss xmm0, [rsp]
    // vmovups [rsp], xmm4 / vmovups xmm4, [rsp]: the step's lane 0 times n (the build-up multiplies by it)
    {11, -1, "c5f8112424", 1, Convert::Amount, "c5f8102424", 13, Special::BuildUp},
    // vmovss [rsp], xmm1 (1, or the fade +0x2b4) / vmovss xmm1, [rsp]; vmovss xmm2, [rsp + 4] (the limit)
    {0, -1, "c5fa110c24", 1, Convert::Keep, "c5fa100c24c5fa10542404", 4, Special::FadeLimit},
    // the threshold into the scratch / vcmpltss xmm1, xmm1, [rsp]
    {13, -1, nullptr, 1, Convert::Amount, "c5f2c20c2401", 22, Special::Cutoff},
    // vmovups [rsp], xmm0 / vmovups xmm0, [rsp]: the weight per axis (x, y, z; w stays 0)
    {17, -1, "c5f8110424", 7, Convert::Decay, "c5f8100424", 17, Special::None},
    {0, 0x1c4, nullptr, 1, Convert::Decay, "c5fa590424", 9, Special::None},          // vmulss xmm0, xmm0, [rsp]
    {0, 0x23c, nullptr, 1, Convert::Decay, "c5fa590424", 9, Special::None},          // vmulss xmm0, xmm0, [rsp]
    {0, 0x23c, nullptr, 1, Convert::Decay, "c5f2590c24", 9, Special::None},          // vmulss xmm1, xmm1, [rsp]
    // vmovups [rsp], xmm1 / vmovups xmm1, [rsp]: lane 0 scaled (PlayAngle), then the compare
    {13, -1, "c5f8110c24", 0, Convert::Decay, "c5f8100c24", 13, Special::PlayAngle},
};
static_assert(std::size(kSites) == std::size(kCaves));

// The constructor's second set (patch 0x183a8bb on). Kyo's 60 FPS list: seven of them halved (his conversion to
// 60; the game's, converted every frame, replace them: at 60 up to 14.5% quicker than his halving on the larger
// ones, as 1 - (1 - w)^(1/2) is more than w / 2), six retuned, the angle among them (his seventh retune is the
// distance ease, frame_rate.cpp's).
constexpr Field kFields[] = {
    {0x183a8c1, 0x19c, 0x3dcccccd, 0x3d4ccccd, Fpp::Game},  // ChrTransChaseRateForNormal 0.1 (halved)
    {0x183a8cb, 0x1a0, 0x3e800000, 0x3d4ccccd, Fpp::Decay}, // ChrTransChaseRateForEscapeWall 0.25, his 0.05
    {0x183a8d5, 0x1a4, 0x3e99999a, 0x3d4ccccd, Fpp::Kyo},   // TargetTransChaseRate 0.3, his 0.05 (a share)
    {0x183a8df, 0x1a8, 0x3e99999a, 0x3d4ccccd, Fpp::Keep},  // TargetRotXChaseRate 0.3, his 0.05 (kept)
    {0x183a8f3, 0x1b0, 0x3dcccccd, 0x3d4ccccd, Fpp::Game},  // TargetChaseRateXZForNormal 0.1 (halved)
    {0x183a8fd, 0x1b4, 0x3e800000, 0x3d4ccccd, Fpp::Decay}, // TargetChaseRateXZForEscapeWall 0.25, his 0.05
    {0x183a907, 0x1b8, 0x3cf5c28f, 0x3c75c28f, Fpp::Game},  // TargetChaseRateY 0.03 (halved)
    {0x183a925, 0x1c4, 0x3ecccccd, 0x3e4ccccd, Fpp::Game},  // TargetChaseDistRate 0.4 (halved)
    {0x183a92f, 0x1c8, 0x3ecccccd, 0x3e4ccccd, Fpp::Game},  // TargetChaseRateAtLockXZ 0.4 (halved)
    {0x183a939, 0x1cc, 0x3e99999a, 0x3e19999a, Fpp::Game},  // TargetChaseRateAtLockY 0.3 (halved)
    {0x183aa47, 0x238, 0x3f860a92, 0x3f32b8c2, Fpp::Kyo},   // DirectPitchAng 60 degrees, his 40
    {0x183aa51, 0x23c, 0x3d75c28f, 0x3d23d70a, Fpp::Decay}, // DirectPitchRate 0.06, his 0.04
    {0x183aa5b, 0x240, 0x3e99999a, 0x3e19999a, Fpp::Game},  // LockRotChaseRate 0.3 (halved)
};

// The debug menu's camera setup (patch 0x183fb60), where the FPS++ lists put stores of their values instead of two
// blocks of slider registrations: as the game has it (its first instruction, mov rdi, [r13 + 0x338]).
constexpr std::pair<std::uint64_t, const char*> kSetup[] = {{0x183ff29, "498bbd38030000"},
                                                            {0x1840799, "498bbd38030000"}};

float g_frames = 1.0f;
std::array<Hook, std::size(kSites)> g_hooks{};

std::vector<std::uint8_t> FromHex(const char* text) {
    const auto digit = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; text[i] && text[i + 1]; i += 2) out.push_back(std::uint8_t(digit(text[i]) * 16 + digit(text[i + 1])));
    return out;
}
float FromBits(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}
std::uint32_t ToBits(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}

class Code {
public:
    std::vector<std::uint8_t> out;
    void Hex(const char* text) {
        const auto bytes = FromHex(text);
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    void Bytes(const std::uint8_t* p, std::size_t n) { out.insert(out.end(), p, p + n); }
    void U8(std::uint8_t v) { out.push_back(v); }
    void U32(std::uint32_t v) { Bytes(reinterpret_cast<const std::uint8_t*>(&v), 4); }
    void U64(std::uint64_t v) { Bytes(reinterpret_cast<const std::uint8_t*>(&v), 8); }
    // lea rsp, [rsp - 0x80] (past the red zone); pushfq; push rax; sub rsp, 32 (the scratch at [rsp]: values at
    // 0 to 23, the x87 control words at 24 and 26)
    void Enter() { Hex("488d642480" "9c" "50" "4883ec20"); }
    // add rsp, 32; pop rax; popfq; lea rsp, [rsp + 0x80]
    void Leave() { Hex("4883c420" "58" "9d" "488da42480000000"); }
    // fnstcw [rsp + 24]; mov word [rsp + 26], 0x37f (exceptions masked, 64-bit precision, to nearest); fldcw
    // [rsp + 26]: the conversions do not depend on the caller's (24-bit precision would lose n = 1's bits)
    void Precise() { Hex("d97c2418" "66c744241a7f03" "d96c241a"); }
    // fldcw [rsp + 24]: the caller's back
    void Restore() { Hex("d96c2418"); }
    // jmp [rip + 0]; the target
    void Jump(std::uint64_t target) {
        Hex("ff2500000000");
        U64(target);
    }
    // A short conditional jump's opcode to an absolute target: the opposite condition over the jump.
    void JumpIf(std::uint8_t opcode, std::uint64_t target) {
        U8(opcode ^ 1);
        U8(14);
        Jump(target);
    }
    // The scratch's float at [rsp + at], for a frame of n 30 FPS frames (at `frames`). Decay and Keep convert only
    // 0 < w < 1 (an integer compare of the bits: negative, zero, one and above, infinity and NaN stay).
    void Convert(std::uint8_t at, CameraRate::Convert kind, const float* frames) {
        std::size_t low = 0, high = 0;
        if (kind != CameraRate::Convert::Amount) {
            Hex("8b4424"); // mov eax, [rsp + at]
            U8(at);
            Hex("85c0");   // test eax, eax
            Hex("7e00");   // jle: w <= 0
            low = out.size() - 1;
            Hex("3d0000803f"); // cmp eax, 1.0
            Hex("7d00");       // jge: w >= 1, or not a number
            high = out.size() - 1;
        }
        Hex("48b8"); // movabs rax, frames
        U64(reinterpret_cast<std::uint64_t>(frames));
        Hex("d900"); // fld dword [rax]: n
        if (kind == CameraRate::Convert::Amount) {
            Hex("d84c24"); // fmul dword [rsp + at]
            U8(at);
        } else {
            Hex(kind == CameraRate::Convert::Decay ? "d9e8d86424" : "d94424"); // fld1; fsub [rsp + at] / fld [rsp + at]
            U8(at);
            // fyl2x (x = n log2 v); fld st0; frndint (i); fsub st1, st0 (x - i); fxch st1; f2xm1; fld1; faddp st1
            // (2^(x - i)); fscale (times 2^i); fstp st1
            Hex("d9f1d9c0d9fcdce9d9c9d9f0d9e8dec1d9fdddd9");
            if (kind == CameraRate::Convert::Decay) Hex("d9e8dee1"); // fld1; fsubrp st1: 1 - v^n
        }
        Hex("d95c24"); // fstp dword [rsp + at]
        U8(at);
        if (low) {
            out[low] = std::uint8_t(out.size() - (low + 1));
            out[high] = std::uint8_t(out.size() - (high + 1));
        }
    }
};

std::uint32_t Load32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, sizeof v);
    return v;
}
} // namespace

std::span<const Site> Sites() { return kSites; }
std::span<const Field> Fields() { return kFields; }

std::uint32_t FppValue(const Field& field) {
    const double w = FromBits(field.kyo);
    switch (field.fpp) {
    case Fpp::Game: return field.game;
    case Fpp::Kyo: return field.kyo;
    case Fpp::Decay: return ToBits(float(1.0 - (1.0 - w) * (1.0 - w)));
    case Fpp::Keep: return ToBits(float(w * w));
    }
    return field.kyo;
}

std::vector<std::uint8_t> Cave(std::size_t index, const std::uint8_t* image, const std::uint8_t* displaced,
                               const float* frames) {
    const Site& site = kSites[index];
    const CaveDef& cave = kCaves[index];
    const std::size_t size = FromHex(site.bytes).size();
    const auto guest = [&](std::uint64_t patch) { return reinterpret_cast<std::uint64_t>(image + (patch - kBase)); };
    Code code;
    code.Bytes(displaced, cave.before);
    code.Enter();
    if (cave.field >= 0) {
        code.Hex("418b85"); // mov eax, [r13 + field]; mov [rsp], eax
        code.U32(std::uint32_t(cave.field));
        code.Hex("890424");
    } else if (cave.special == Special::Cutoff) {
        // vcmpltss's rip-relative constant: its displacement 4 bytes in, the next instruction at the site's end.
        const std::uint64_t constant = site.at + size + std::int32_t(Load32(displaced + 13 + 4));
        code.Hex("c70424"); // mov dword [rsp], the threshold
        code.U32(Load32(image + (constant - kBase)));
    } else {
        code.Hex(cave.spill);
    }
    if (cave.special == Special::PlayAngle) {
        code.Hex("418b85"); // mov eax, [r13 + 0x240] (the lock-on turn's weight); mov [rsp + 16], eax; [rsp + 20]
        code.U32(0x240);
        code.Hex("89442410" "89442414");
    }
    code.Precise();
    for (std::uint8_t lane = 0; lane < 4; ++lane)
        if (cave.lanes & (1u << lane)) code.Convert(std::uint8_t(4 * lane), cave.kind, frames);
    if (cave.special == Special::FadeLimit) {
        // The turn's limit a frame, as the game has it: its step (its own copy at rbp - 0x250; xmm4 is the step
        // times n on the build-up's path) times +0x2b0. fld dword [rbp - 0x250]; fmul dword [r13 + 0x2b0];
        // fstp dword [rsp + 4]
        code.Hex("d985b0fdffff" "41d88db0020000" "d95c2404");
    }
    if (cave.special == Special::Cutoff) {
        // A frame that built the turn up (r14b, one probe hit) compares it against 1 degree times n^2: its step,
        // the rate times the step times n, against the cut-off as at 30 (rate / 30 against 1 degree). rax is
        // still `frames` (Convert's). test r14b, r14b; jz; fld dword [rax]; fmul dword [rsp]; fstp dword [rsp]
        code.Hex("4584f6" "7408" "d900" "d80c24" "d91c24");
    }
    if (cave.special == Special::PlayAngle) {
        // w' (the conversion the lock-on turn's cave makes) at [rsp + 20]; unless it is w (not converted: n = 1, or
        // w outside (0, 1)), the step's square times (w / w')^2: compared with the play angle's as at 30.
        // mov eax, [rsp + 20]; cmp eax, [rsp + 16]; je; test eax, eax; jle; fld [rsp + 16]; fdiv [rsp + 20];
        // fmul st0, st0; fmul [rsp]; fstp [rsp]
        code.Convert(20, Convert::Decay, frames);
        code.Hex("8b442414" "3b442410" "7414" "85c0" "7e10" "d9442410" "d8742414" "d8c8" "d80c24" "d91c24");
    }
    code.Restore();
    code.Hex(cave.use);
    code.Leave();
    if (cave.special == Special::BuildUp) {
        // je rel8 at 11: to its target in the image; then xor r14d, r14d; back to the site's end.
        const std::uint64_t target = site.at + 13 + std::int8_t(displaced[12]);
        code.JumpIf(displaced[11], guest(target));
        code.Bytes(displaced + cave.after, size - cave.after);
        code.Jump(guest(site.at + size));
        return code.out;
    }
    if (cave.special == Special::FadeLimit) {
        // vmulss xmm1, xmm1, xmm0; then vxorps xmm0, xmm2, [the game's -0 mask], the mask copied after the jump.
        code.Bytes(displaced, cave.after);
        const std::uint64_t mask = site.at + size + std::int32_t(Load32(displaced + 13 + 4));
        code.Hex("c5e85705");
        const std::size_t rel = code.out.size();
        code.U32(0);
        code.Jump(guest(site.at + size));
        const std::uint32_t distance = std::uint32_t(code.out.size() - (rel + 4));
        std::memcpy(code.out.data() + rel, &distance, sizeof distance);
        code.Bytes(image + (mask - kBase), 16);
        return code.out;
    }
    if (cave.after < size) code.Bytes(displaced + cave.after, size - cave.after);
    code.Jump(guest(site.at + size));
    return code.out;
}

bool Check(const std::uint8_t* image, std::size_t size, bool fpp, std::uint64_t* bad) {
    const auto inside = [&](std::uint64_t address, std::size_t n) {
        return address >= kBase && address - kBase <= size && n <= size - (address - kBase);
    };
    const auto fail = [&](std::uint64_t address) {
        if (bad) *bad = address;
        return false;
    };
    for (const Site& site : kSites) {
        const auto bytes = FromHex(site.bytes);
        if (!inside(site.at, bytes.size()) || std::memcmp(image + (site.at - kBase), bytes.data(), bytes.size()))
            return fail(site.at);
    }
    // The wall turn's two rip-relative constants (the -0 mask, 1 degree) in the image.
    for (const std::size_t index : {std::size_t(7), std::size_t(8)}) {
        const Site& site = kSites[index];
        const std::size_t n = FromHex(site.bytes).size();
        const std::uint64_t constant = site.at + n + std::int32_t(Load32(image + (site.at - kBase) + 13 + 4));
        if (!inside(constant, 16)) return fail(site.at);
    }
    for (const Field& field : kFields) {
        // mov dword ptr [rbx + field], imm32: c7 83, the field, the value
        if (!inside(field.at - 6, 10)) return fail(field.at);
        const std::uint8_t* p = image + (field.at - 6 - kBase);
        if (p[0] != 0xc7 || p[1] != 0x83 || Load32(p + 2) != field.field || Load32(p + 6) != (fpp ? field.kyo : field.game))
            return fail(field.at);
    }
    for (const auto& [at, text] : kSetup) {
        const auto bytes = FromHex(text);
        if (!inside(at, bytes.size()) || std::memcmp(image + (at - kBase), bytes.data(), bytes.size())) return fail(at);
    }
    return true;
}

bool Install(std::uint8_t* image, std::size_t size, bool fpp) {
    for (Hook& hook : g_hooks) Discard(hook);
    g_frames = 1.0f;
    if (!Check(image, size, fpp, nullptr)) return false;
    for (std::size_t i = 0; i < std::size(kSites); ++i) {
        const Site& site = kSites[i];
        const auto bytes = FromHex(site.bytes);
        const auto cave = Cave(i, image, image + (site.at - kBase), &g_frames);
        if (!Divert(g_hooks[i], image, size, site.at - kBase, bytes, cave)) {
            for (Hook& hook : g_hooks) Discard(hook);
            return false;
        }
    }
    if (fpp) {
        for (const Field& field : kFields) {
            const std::uint32_t value = FppValue(field);
            if (!WriteCode(image + (field.at - kBase), &value, sizeof value)) {
                for (Hook& hook : g_hooks) Discard(hook);
                return false;
            }
        }
    }
    Commit(g_hooks);
    return true;
}

int Fallback(std::uint8_t* image, std::size_t size, bool fpp, double rate) {
    if (!(rate > 30)) return 0;
    const auto inside = [&](std::uint64_t address) {
        return address >= kBase && address - kBase <= size && 4 <= size - (address - kBase);
    };
    int written = 0;
    // A value as `was`, converted once: a weight a frame from a frame of `from` FPS to the rate's.
    const auto convert = [&](std::uint64_t at, std::uint32_t was, double from, bool keep) {
        std::uint32_t now = 0;
        if (!inside(at)) return;
        std::memcpy(&now, image + (at - kBase), 4);
        const double w = FromBits(was);
        if (now != was || !(0 < w && w < 1)) return;
        const double frames = from / rate;
        const std::uint32_t value = ToBits(float(keep ? std::pow(w, frames) : 1 - std::pow(1 - w, frames)));
        if (WriteCode(image + (at - kBase), &value, sizeof value)) ++written;
    };
    if (fpp) {
        for (const Field& field : kFields)
            if (field.fpp != Fpp::Kyo) convert(field.at, field.kyo, 60, field.fpp == Fpp::Keep);
    }
    // The eases' constants (frame_rate.cpp's kEases read them per frame from slots instead): the distance ease
    // (the game's 0.1, Kyo's 0.025) and the chase-rate ease (1/30 in both).
    convert(0x4d25e68, fpp ? 0x3ccccccd : 0x3dcccccd, fpp ? 60 : 30, false);
    convert(0x4d25e94, 0x3d088889, 30, false);
    convert(0x183a7de, 0x3d4ccccd, 30, false); // the settings blend (+0x330), the constructor's 0.05
    return written;
}

void Frame(double seconds) {
    const double n = 30.0 * seconds;
    g_frames = !(n > 0) ? 1.0f : n >= 1.0 - 1e-9 ? 1.0f : float(n);
}

float Frames() { return g_frames; }
} // namespace BbEngine::CameraRate

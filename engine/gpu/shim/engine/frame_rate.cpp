// SPDX-License-Identifier: GPL-3.0-or-later
// Frame-rate corrections at 60 FPS and above (frame_rate.h), adapted from bbhost's src/engine/frame_rate.cpp
// (droogie/bbhost 566184a; licenses/BBHOST-PORTS.md). Kyo's "60 FPS++" findings are the base at every rate, as
// in bbhost: our table of them (start/frame_rate_base.inc, which the Windows start program writes; on Linux the
// patch file's list, the same bytes). These are the systems it leaves on a fixed 30 FPS step, the durations it
// took for steps, the play
// time's clock, and above 60 the frame pacer's target, the values and counts that follow the rate and the
// cloth (frame_rate_sites.inc says what each one is). Every byte is checked before anything is written; a
// group with a mismatch is left out whole, the others still go in.
//
// The rates: 60, 90, 120 and 144 as they are; uncapped with the pacer's target at its limit and, for what
// cannot follow the frame's time (code immediates, frame counts), the values of 120, as bbhost and Kyo's
// uncapped list take them. The list's fixed steps (1/30 made 1/60) follow the frame here, at 60 too: each one
// was read and is either the frame's step now (the step-infos handed to updates, "steps") or, where it is
// not a step at all, the game's value back (docs/frame-rate-survey.md has every entry and why).
//
// The per-frame values are read from slots of ours past the end of the game's data segment (1.09: image
// offset 0x56d30f4), in that segment's last page: the loader protects each segment in whole pages, so
// those 3,852 bytes are writable with the data, and the linked modules (libc...) start after the page
// (0x56e0000). Not past the whole image: the last module's page there is not writable.
//
// The frame's step. The 60 FPS++ cave (0x2418e3d, right after the game's frame pacer returns) hands the
// game's task step the pacer's measured frame time kept in [target, 1/30], and eases a copy of it by 0.15 a
// frame into 0x575a908, which the list's sites read. Without the frame timer the values here follow that
// copy, worked out at the start of the next frame (the frame hook, at the pacer's entry): a frame late and
// eased, so after a long frame they catch up over 10 to 15 frames while the task step has already. With it
// (BB_FRAME_TIMER=1) the cave calls ours: the same step as the task step's, from the same measurement, into
// the task step, 0x575a908 and every value here before the frame's tasks run - one step for all of it.
#include "frame_rate.h"
#include "engine_hooks.h"
#include "engine_state.h"
#include "skip_list.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace BbEngine::FrameRate {
namespace {
constexpr std::uint64_t kBase = 0x400000;       // the patch lists' addresses: the image offset + this
constexpr std::uint64_t kFrameTime = 0x575a908; // the 60 FPS++ cave's step
constexpr std::size_t kPage = 4096, kSlot = 16;
constexpr std::size_t kDataEnd = 0x56d30f4; // the end of 1.09's data segment (its VA + memsz)
constexpr std::uint32_t kListedTarget = 0x3c888889; // the 60 FPS++ list's 1/60

enum class Group : std::uint8_t {
    Clips, Physics, Grass, Targeting, Jump, Sprint, Slope, Online, Hitbox, Timers, Menus, Durations, PlayTime,
    Pace, Sound, Cloth, Steps, Lens, Timer, Camera, Count
};
constexpr const char* kGroupNames[] = {"clips",  "physics", "grass",  "targeting", "jump",      "sprint",
                                       "slope",  "online",  "hitbox", "timers",    "menus",     "durations",
                                       "playtime", "pace",  "sound",  "cloth",     "steps",     "lens",
                                       "timer",  "camera"};
static_assert(std::size(kGroupNames) == std::size_t(Group::Count));

/// How a count follows the rate.
enum class Counted : std::uint8_t {
    Frames, ///< a number of frames: the game's times rate / 30
    Micros, ///< microseconds: the game's times 30 / rate
    Digits, ///< one frame's microseconds as ASCII digits
};

struct Site {
    Group group;
    std::uint64_t at;
    const char *was, *listed, *now;
};
struct Load {
    Group group;
    std::uint64_t insn;
    std::uint8_t length, disp;
    std::uint64_t constant;
    Kind kind;
    std::uint8_t lanes;
    const char* bytes;
    std::array<std::uint32_t, 4> value;
};
struct Value {
    Group group;
    std::uint64_t at;
    Kind kind;
    std::uint32_t game, listed;
};
struct CountSite {
    Group group;
    std::uint64_t at;
    std::uint8_t size;
    std::uint32_t game;
    Counted kind;
};
struct Target {
    std::uint64_t at;
    const char* store; // the instruction up to its immediate
    std::uint32_t game;
};
struct ClothScale {
    std::uint64_t at;
    const char* was;
};
struct Ease {
    std::uint64_t insn;
    std::uint8_t length, disp;
    std::uint64_t constant;
    const char* bytes;
    std::uint32_t game, listed;
};
struct Require {
    std::uint64_t at;
    const char* bytes;
};
struct PlayTimeSite {
    Group group;
    std::uint64_t at;
    const char* was;
};

// Each table is the .inc read with only its own macro defined.
#define FRAME_SITE(...)
#define FRAME_LOAD(...)
#define FRAME_VALUE(...)
#define FRAME_COUNT(...)
#define FRAME_TARGET(...)
#define FRAME_CLOTH_SCALE(...)
#define FRAME_EASE(...)
#define FRAME_REQUIRE(...)
#define FRAME_PLAY_TIME(...)
#undef FRAME_SITE
#define FRAME_SITE(g, at, was, listed, now) {Group::g, at, was, listed, now},
constexpr Site kSites[] = {
#include "frame_rate_sites.inc"
};
#undef FRAME_SITE
#define FRAME_SITE(...)
#undef FRAME_LOAD
#define FRAME_LOAD(g, insn, length, disp, constant, kind, lanes, bytes, ...)                                   \
    {Group::g, insn, length, disp, constant, Kind::kind, lanes, bytes, {__VA_ARGS__}},
constexpr Load kLoads[] = {
#include "frame_rate_sites.inc"
};
#undef FRAME_LOAD
#define FRAME_LOAD(...)
#undef FRAME_VALUE
#define FRAME_VALUE(g, at, kind, game, listed) {Group::g, at, Kind::kind, game, listed},
constexpr Value kValues[] = {
#include "frame_rate_sites.inc"
};
#undef FRAME_VALUE
#define FRAME_VALUE(...)
#undef FRAME_COUNT
#define FRAME_COUNT(g, at, size, game, kind) {Group::g, at, size, game, Counted::kind},
constexpr CountSite kCounts[] = {
#include "frame_rate_sites.inc"
};
#undef FRAME_COUNT
#define FRAME_COUNT(...)
#undef FRAME_TARGET
#define FRAME_TARGET(at, store, game) {at, store, game},
constexpr Target kTargets[] = {
#include "frame_rate_sites.inc"
};
#undef FRAME_TARGET
#define FRAME_TARGET(...)
#undef FRAME_CLOTH_SCALE
#define FRAME_CLOTH_SCALE(at, was) {at, was},
constexpr ClothScale kClothScales[] = {
#include "frame_rate_sites.inc"
};
#undef FRAME_CLOTH_SCALE
#define FRAME_CLOTH_SCALE(...)
#undef FRAME_EASE
#define FRAME_EASE(insn, length, disp, constant, bytes, game, listed) {insn, length, disp, constant, bytes, game, listed},
constexpr Ease kEases[] = {
#include "frame_rate_sites.inc"
};
#undef FRAME_EASE
#define FRAME_EASE(...)
#undef FRAME_REQUIRE
#define FRAME_REQUIRE(at, bytes) {at, bytes},
constexpr Require kRequire[] = {
#include "frame_rate_sites.inc"
};
#undef FRAME_REQUIRE
#define FRAME_REQUIRE(...)
#undef FRAME_PLAY_TIME
#define FRAME_PLAY_TIME(g, at, was) {Group::g, at, was},
constexpr PlayTimeSite kPlayTime[] = {
#include "frame_rate_sites.inc"
};
#undef FRAME_SITE
#undef FRAME_LOAD
#undef FRAME_VALUE
#undef FRAME_COUNT
#undef FRAME_TARGET
#undef FRAME_CLOTH_SCALE
#undef FRAME_EASE
#undef FRAME_REQUIRE
#undef FRAME_PLAY_TIME

std::vector<std::uint8_t> Hex(const char* text) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; text[i] && text[i + 1]; i += 2) {
        out.push_back(std::uint8_t(std::stoi(std::string(text + i, 2), nullptr, 16)));
    }
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

/// What the corrections are for. Fixed rates: all three the rate. Uncapped (BB_FPS=uncap): the pacer's target
/// its limit (BB_FPS_LIMIT, 60 to 480; none: 480, the vblank it runs on), the values for 120 (no more than the
/// limit), the sound's updates one a frame up to 240 a second.
struct Rate {
    double values = 60, target = 60, sound = 60;
    bool uncapped = false;
};
bool RateOf(const char* fps, Rate& rate) {
    if (!fps) return false;
    for (const double fixed : {60.0, 90.0, 120.0, 144.0}) {
        if (std::to_string(int(fixed)) == fps) {
            rate = {fixed, fixed, fixed, false};
            return true;
        }
    }
    if (std::strcmp(fps, "uncap") != 0) return false;
    const char* limit = std::getenv("BB_FPS_LIMIT");
    const long n = limit ? std::strtol(limit, nullptr, 10) : 240;
    const double target = n > 0 ? std::clamp(double(n), 60.0, 480.0) : 480.0;
    rate = {std::min(120.0, target), target, std::min(240.0, target), true};
    return true;
}

/// A count's bytes at `rate` (its own rate: the sound's for the update time, the values' for the rest).
std::vector<std::uint8_t> CountBytes(const CountSite& c, double rate) {
    double n = c.game;
    if (c.kind == Counted::Frames) n = std::round(c.game * rate / 30.0);
    else if (c.kind == Counted::Micros) n = std::round(c.game * 30.0 / rate);
    else if (rate != 30.0) n = std::floor(1e6 / rate); // whole microseconds, as the game's own 33333
    const auto value = std::uint32_t(n);
    std::vector<std::uint8_t> out(c.size);
    if (c.kind == Counted::Digits) {
        char text[16];
        std::snprintf(text, sizeof text, "%0*u", int(c.size), value);
        std::memcpy(out.data(), text, c.size);
    } else {
        for (std::size_t i = 0; i < c.size; ++i) out[i] = std::uint8_t(value >> (8 * i));
    }
    return out;
}
double CountRate(const CountSite& c, const Rate& rate) {
    return c.kind == Counted::Digits ? rate.sound : rate.values;
}

// A per-frame value: its slot, the constant it follows (live, or the game's when the list changed it).
struct Slot {
    std::uint8_t* at;
    const std::uint8_t* live;
    std::array<float, 4> was;
    Kind kind;
    std::uint8_t lanes;
};
std::uint8_t* g_image = nullptr;
std::vector<Slot> g_slots;
std::uint8_t* g_play_ms = nullptr;
PlayTime g_play;
double g_fallback = 1.0 / 60.0; // the step before the 60 FPS++ cave has one: a frame at the rate
bool g_on = false;
bool g_checked = false; // the slots' page found writable (first Tick, after the loader's protections)
// The frame timer: the step's slot, which the cave loads into the task step, and the camera's eases.
bool g_timer = false;
std::uint8_t* g_step = nullptr;
struct EaseSlot {
    std::uint8_t* at;
    double was; // a 30 FPS frame's weight
};
std::vector<EaseSlot> g_eases;
Hook g_timer_hook;
constexpr std::uint64_t kCave = 0x2418e3d;           // the 60 FPS++ cave, kRequire's first
constexpr std::uint64_t kCameraSkip = 0x183dc96;     // the FPS++ retune's jump over the game's follow camera
constexpr std::uint8_t kCameraSkipped[] = {0xe9, 0xd6, 0x05, 0x00, 0x00};
constexpr double kShortest = 1.0 / 480;              // a step's floor without a target (a loading screen)
constexpr double kLongest = 1.0 / 30;                // and its ceiling, as the cave's (the game slows below 30)

// The slots' page must have come out of the loader's protections writable; once, at the first frame.
bool Writable() {
    if (g_checked) return g_on;
    g_checked = true;
    const void* slots = g_step ? g_step : g_slots.empty() ? g_play_ms : g_slots.front().at;
    bool writable = true;
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION info{};
    writable = VirtualQuery(slots, &info, sizeof info) &&
               (info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
#else
    if (FILE* maps = std::fopen("/proc/self/maps", "r")) {
        const auto a = reinterpret_cast<std::uintptr_t>(slots);
        char line[512];
        while (std::fgets(line, sizeof line, maps)) {
            unsigned long lo = 0, hi = 0;
            char perms[5]{};
            if (std::sscanf(line, "%lx-%lx %4s", &lo, &hi, perms) == 3 && lo <= a && a < hi) {
                writable = perms[1] == 'w';
                break;
            }
        }
        std::fclose(maps);
    }
#endif
    if (!writable) {
        host_log("Frame rate fixes: the per-frame values' page is not writable; they stay at the rate's");
        g_on = false;
    }
    return g_on;
}

void WriteSlot(const Slot& s, double seconds);

// A frame of `seconds`: the slots, the play time and the eases.
void Update(double seconds) {
    for (const Slot& s : g_slots) WriteSlot(s, seconds);
    if (g_play_ms) {
        const std::int32_t ms = g_play.Add(seconds);
        std::memcpy(g_play_ms, &ms, sizeof ms);
    }
    for (const EaseSlot& e : g_eases) {
        const float w = Rated(Kind::Decay, float(e.was), seconds);
        std::memcpy(e.at, &w, sizeof w);
    }
}

// The Bloodletter's transformed L2 (the Hitbox group), after droogie's bbhost (src/engine/frame_rate.cpp,
// 6f735f4): its frenzy aura comes from one attack that hits only its wielder, AtkParam_Pc 2900931 (selfTarget,
// no damage), a 0.4 m sphere on the weapon live for one 30 FPS frame of the slam. Posed, it overlaps the right
// hand's ragdoll capsule by 5 cm; when those trail the pose by a fraction of a frame it misses, so above 30 the
// aura never came. At 0.6 m it overlaps by 25 cm. hit0_Radius is ATK_PARAM_ST's first field. The tables load
// at the start and the row is looked at again every 120 frames, for a table loaded again.
constexpr std::uint32_t kSelfHit = 2900931;
constexpr std::uint32_t kSelfHitWas = 0x3ecccccd, kSelfHitNow = 0x3f19999a; // 0.4, 0.6
bool g_self_hit = false; // the Hitbox group in
int g_self_hit_wait = 0; // frames until the row is looked at again

void SelfHit() {
    if (!g_self_hit || --g_self_hit_wait > 0) return;
    g_self_hit_wait = 120;
    std::size_t bytes = 0;
    auto* row = static_cast<std::uint8_t*>(params_row("AtkParam_Pc", kSelfHit, &bytes));
    std::uint32_t radius = 0;
    if (!row || (bytes && bytes < sizeof radius) || !host_read_safe(row, &radius, sizeof radius) || radius == kSelfHitNow)
        return; // not loaded yet, or done
    if (radius != kSelfHitWas) {
        host_log("Frame rate fixes: AtkParam_Pc %u's hit0_Radius is %g, not the game's 0.4: the Bloodletter's L2 "
                 "self-hit left as it is", kSelfHit, double(FromBits(radius)));
        g_self_hit = false;
        return;
    }
    std::memcpy(row, &kSelfHitNow, sizeof kSelfHitNow);
    host_log("Frame rate fixes: the Bloodletter's L2 self-hit 0.4 -> 0.6 m (AtkParam_Pc %u's hit0_Radius): its "
             "frenzy aura comes above 30",
             kSelfHit);
}

std::int64_t __attribute__((sysv_abi)) StepHook(std::uint64_t, const std::uint64_t* saved) {
    Step(saved[5]); // rdi: the frame pacer (the cave's rbx)
    return 0;
}

void WriteSlot(const Slot& s, double seconds) {
    float out[4]{};
    for (std::uint8_t i = 0; i < s.lanes; ++i) {
        float was = s.was[i];
        if (s.live) std::memcpy(&was, s.live + 4 * i, sizeof was);
        out[i] = Rated(s.kind, was, seconds);
    }
    std::memcpy(s.at, out, 4 * s.lanes);
}
} // namespace

float Rated(Kind kind, float was, double seconds) {
    const double w = was, frames = 30.0 * seconds; // this frame in frames of the game's 30 FPS
    switch (kind) {
    case Kind::Step: return float(seconds);
    case Kind::Amount: return float(w * frames);
    case Kind::Decay: return float(1.0 - std::pow(1.0 - w, frames));
    case Kind::Keep: return float(std::pow(w, frames));
    case Kind::Rate: return float(w / frames);
    case Kind::Game: return was;
    case Kind::Cloth: {
        const double scale = std::min(1.0, 60.0 * seconds);
        return float(scale * scale);
    }
    }
    return was;
}

std::int32_t PlayTime::Add(double seconds) {
    carry += seconds * 1000.0;
    const double whole = std::floor(carry);
    carry -= whole;
    return std::int32_t(whole);
}

bool Install(std::uint8_t* image, std::size_t size) {
    g_on = g_checked = g_timer = g_self_hit = false;
    g_self_hit_wait = 0;
    g_slots.clear();
    g_play_ms = g_step = nullptr;
    g_play = {};
    g_eases.clear();
    Discard(g_timer_hook);
    if (const char* off = std::getenv("BB_FRAME_FIXES"); off && off[0] == '0') {
        host_log("Frame rate fixes: off (BB_FRAME_FIXES=0)");
        return false;
    }
    const char* fps = std::getenv("BB_FPS");
    Rate rate;
    if (!RateOf(fps, rate)) {
        host_log("Frame rate fixes: not at %s FPS (60, 90, 120, 144 or uncapped)", fps ? fps : "an unknown");
        return false;
    }
    const auto at = [&](std::uint64_t address) { return image + (address - kBase); };
    const auto inside = [&](std::uint64_t address, std::size_t n) {
        return address >= kBase && address - kBase <= size && n <= size - (address - kBase);
    };
    const auto holds = [&](std::uint64_t address, const std::vector<std::uint8_t>& bytes) {
        return !bytes.empty() && inside(address, bytes.size()) && !std::memcmp(at(address), bytes.data(), bytes.size());
    };
    for (const Require& r : kRequire) {
        if (!holds(r.at, Hex(r.bytes))) {
            host_log("Frame rate fixes: off: the 60 FPS++ frame-time code is not at 0x%llx", (unsigned long long)r.at);
            return false;
        }
    }

    // The groups: left out by name (BB_FRAME_FIXES_SKIP), then by a byte that is not as expected.
    std::array<bool, std::size_t(Group::Count)> on;
    on.fill(true);
    for (const std::string& name : SkipList("BB_FRAME_FIXES_SKIP", kGroupNames, on))
        host_log("Frame rate fixes: no group \"%s\" to skip", name.c_str());
    // The frame timer and the camera's per-frame eases with it: only when asked for (the launcher's Experimental).
    if (const char* timer = std::getenv("BB_FRAME_TIMER"); !timer || timer[0] != '1') {
        on[std::size_t(Group::Timer)] = on[std::size_t(Group::Camera)] = false;
    }
    const auto fail = [&](Group g, std::uint64_t address) {
        if (!on[std::size_t(g)]) return;
        on[std::size_t(g)] = false;
        host_log("Frame rate fixes: %s left out: 0x%llx holds neither 1.09's bytes nor the 60 FPS++ list's",
                 kGroupNames[std::size_t(g)], (unsigned long long)address);
    };

    // The pacer's target first: without all of them the game keeps the list's 1/60, and what follows is for 60.
    const auto target_bits = [&] { return ToBits(float(1.0 / rate.target)); };
    for (const Target& t : kTargets) {
        std::uint32_t imm = 0;
        const auto store = Hex(t.store);
        const bool fits = inside(t.at, store.size() + 4) && holds(t.at, store);
        if (fits) std::memcpy(&imm, at(t.at) + store.size(), 4);
        if (!fits || (imm != t.game && imm != kListedTarget && imm != target_bits())) fail(Group::Pace, t.at);
    }
    if (!on[std::size_t(Group::Pace)] && rate.target != 60) {
        host_log("Frame rate fixes: the frame pacer's target left as the 60 FPS++ list has it: the game runs at 60");
        rate = Rate{};
    }
    // One cloth substep a frame (the list's) is what the cloth was made for at 60: its scale only above.
    if (rate.target <= 60) on[std::size_t(Group::Cloth)] = false;
    g_fallback = 1.0 / rate.values;
    const double step = 1.0 / rate.values;

    for (const Site& s : kSites) {
        const auto was = Hex(s.was), listed = Hex(s.listed), now = Hex(s.now);
        if (!holds(s.at, was) && !holds(s.at, listed) && !holds(s.at, now)) fail(s.group, s.at);
    }
    // A load holds its instruction, or one of its group's sites writes it (the cloth caves').
    const auto load_holds = [&](const Load& l) {
        const auto bytes = Hex(l.bytes);
        if (holds(l.insn, bytes)) return true;
        for (const Site& s : kSites) {
            const auto now = Hex(s.now);
            if (s.group == l.group && l.insn >= s.at && l.insn - s.at + bytes.size() <= now.size() &&
                !std::memcmp(now.data() + (l.insn - s.at), bytes.data(), bytes.size()))
                return true;
        }
        return false;
    };
    for (const Load& l : kLoads) {
        if (!load_holds(l) || !inside(l.constant, 4u * l.lanes)) fail(l.group, l.insn);
    }
    for (const Value& v : kValues) {
        std::uint32_t now = 0;
        if (!inside(v.at, 4)) {
            fail(v.group, v.at);
            continue;
        }
        std::memcpy(&now, at(v.at), 4);
        const std::uint32_t ours = ToBits(Rated(v.kind, FromBits(v.game), step));
        if (now != v.game && now != v.listed && now != ours) fail(v.group, v.at);
    }
    for (const CountSite& c : kCounts) {
        const auto as_game = CountBytes(c, 30.0), at_sixty = CountBytes(c, 60.0), ours = CountBytes(c, CountRate(c, rate));
        if (!holds(c.at, as_game) && !holds(c.at, at_sixty) && !holds(c.at, ours)) fail(c.group, c.at);
    }
    for (const ClothScale& c : kClothScales) {
        // The game's text, or a scale a list wrote (four equal lanes in (0, 1]).
        bool fine = holds(c.at, Hex(c.was));
        if (!fine && inside(c.at, 16)) {
            float lanes[4];
            std::memcpy(lanes, at(c.at), sizeof lanes);
            fine = lanes[0] > 0 && lanes[0] <= 1 && lanes[1] == lanes[0] && lanes[2] == lanes[0] && lanes[3] == lanes[0];
        }
        if (!fine) fail(Group::Cloth, c.at);
    }
    for (const PlayTimeSite& p : kPlayTime) {
        if (!holds(p.at, Hex(p.was))) fail(p.group, p.at);
    }
    for (const Ease& e : kEases) {
        if (!holds(e.insn, Hex(e.bytes)) || !inside(e.constant, 4)) fail(Group::Camera, e.insn);
    }

    // The slots: from the first 16-byte boundary past the game's data to the end of that page, which must
    // be in the image and unused (zero, as the loader leaves it).
    std::size_t slots = 0;
    for (const Load& l : kLoads) slots += on[std::size_t(l.group)];
    for (const PlayTimeSite& p : kPlayTime) slots += on[std::size_t(p.group)];
    if (on[std::size_t(Group::Timer)]) slots += 1 + (on[std::size_t(Group::Camera)] ? std::size(kEases) : 0);
    const std::size_t first = (kDataEnd + kSlot - 1) & ~(kSlot - 1), end = (kDataEnd + kPage - 1) & ~(kPage - 1);
    bool per_frame = end <= size && first + slots * kSlot <= end;
    for (std::size_t i = kDataEnd; per_frame && i < end; ++i) per_frame = image[i] == 0;
    if (slots && !per_frame) {
        host_log("Frame rate fixes: per-frame values left out: no free room past the game's data (0x%zx..0x%zx)",
                 kDataEnd, end);
    }

    g_image = image;
    std::array<int, std::size_t(Group::Count)> applied{};
    std::uint8_t* next = image + first;
    const auto write = [&](std::uint64_t address, const void* bytes, std::size_t n) {
        if (!WriteCode(at(address), bytes, n)) {
            host_log("Frame rate fixes: could not write 0x%llx", (unsigned long long)address);
            return false;
        }
        return true;
    };
    if (on[std::size_t(Group::Pace)]) {
        const std::uint32_t bits = target_bits();
        for (const Target& t : kTargets)
            if (write(t.at + Hex(t.store).size(), &bits, sizeof bits)) ++applied[std::size_t(Group::Pace)];
    }
    for (const Site& s : kSites) {
        if (!on[std::size_t(s.group)]) continue;
        const auto now = Hex(s.now);
        if (holds(s.at, now) || write(s.at, now.data(), now.size())) ++applied[std::size_t(s.group)];
    }
    for (const Load& l : kLoads) {
        if (!on[std::size_t(l.group)] || !per_frame) continue;
        Slot slot{next, nullptr, {}, l.kind, l.lanes};
        // The constant as the game has it: read live (a .data tunable keeps working), unless the list
        // changed it, in which case the game's value is ours to start from.
        bool game = true;
        for (std::uint8_t i = 0; i < l.lanes; ++i) {
            slot.was[i] = FromBits(l.value[i]);
            std::uint32_t now;
            std::memcpy(&now, at(l.constant) + 4 * i, 4);
            game &= now == l.value[i];
        }
        if (game) slot.live = at(l.constant);
        WriteSlot(slot, step);
        const auto disp = std::int32_t(next - (at(l.insn) + l.length));
        if (!write(l.insn + l.disp, &disp, sizeof disp)) continue;
        g_slots.push_back(slot);
        next += kSlot;
        ++applied[std::size_t(l.group)];
    }
    for (const Value& v : kValues) {
        if (!on[std::size_t(v.group)]) continue;
        const std::uint32_t ours = ToBits(Rated(v.kind, FromBits(v.game), step));
        if (write(v.at, &ours, sizeof ours)) ++applied[std::size_t(v.group)];
    }
    for (const CountSite& c : kCounts) {
        if (!on[std::size_t(c.group)]) continue;
        const auto ours = CountBytes(c, CountRate(c, rate));
        if (write(c.at, ours.data(), ours.size())) ++applied[std::size_t(c.group)];
    }
    // Where the caves' loads have no slot, they read the scale for the rate.
    for (const ClothScale& c : kClothScales) {
        if (!on[std::size_t(Group::Cloth)] || per_frame) continue;
        const float lane = Rated(Kind::Cloth, 0, step), lanes[4] = {lane, lane, lane, lane};
        if (write(c.at, lanes, sizeof lanes)) ++applied[std::size_t(Group::Cloth)];
    }
    for (const PlayTimeSite& p : kPlayTime) {
        if (!on[std::size_t(p.group)] || !per_frame) continue;
        const std::int32_t zero = 0;
        std::memcpy(next, &zero, sizeof zero);
        // mov ecx, [slot]; nop7 (0f 1f 80 00 00 00 00) over the 13 bytes of the multiply and convert.
        std::uint8_t code[13] = {0x8b, 0x0d, 0, 0, 0, 0, 0x0f, 0x1f, 0x80, 0, 0, 0, 0};
        const auto disp = std::int32_t(next - (at(p.at) + 6));
        std::memcpy(code + 2, &disp, sizeof disp);
        if (!write(p.at, code, sizeof code)) continue;
        g_play_ms = next;
        next += kSlot;
        ++applied[std::size_t(p.group)];
    }

    // The frame timer: the cave calls ours (Step) right after the pacer returns, and the task step loads the step
    // from our slot. push rdi; mov rdi, rbx (the pacer, for the hook); the 14 bytes the hook's jump takes (nops
    // until then, run after the call); pop rdi; movss xmm0, [step]; movss [rbp-0x38], xmm0 (the task step's
    // step-info, as the cave stores it); nop14. The list's ease (0x2418e9e) is no longer called.
    if (on[std::size_t(Group::Timer)] && per_frame) {
        std::uint8_t cave[] = {0x57, 0x48, 0x89, 0xdf,
                               0x0f, 0x1f, 0x84, 0, 0, 0, 0, 0, 0x66, 0x0f, 0x1f, 0x44, 0, 0,
                               0x5f, 0xf3, 0x0f, 0x10, 0x05, 0, 0, 0, 0, 0xf3, 0x0f, 0x11, 0x45, 0xc8,
                               0x0f, 0x1f, 0x84, 0, 0, 0, 0, 0, 0x66, 0x0f, 0x1f, 0x44, 0, 0};
        static_assert(sizeof cave == 46, "the cave's 46 bytes");
        constexpr std::size_t kSite = 4, kLoad = 19, kLoadEnd = 27;
        const auto kyo = Hex(kRequire[0].bytes);
        g_step = next;
        const float first_step = float(step);
        std::memcpy(g_step, &first_step, sizeof first_step);
        const auto disp = std::int32_t(g_step - (at(kCave) + kLoadEnd));
        std::memcpy(cave + kLoad + 4, &disp, sizeof disp);
        bool hooked = kyo.size() == sizeof cave && write(kCave, cave, sizeof cave) &&
                      Prepare(g_timer_hook, image, size, kCave - kBase + kSite, {cave + kSite, 14}, StepHook);
        if (hooked) {
            std::uint8_t jump[14] = {0xff, 0x25, 0, 0, 0, 0};
            const auto stub = reinterpret_cast<std::uint64_t>(g_timer_hook.stub);
            std::memcpy(jump + 6, &stub, sizeof stub);
            hooked = write(kCave + kSite, jump, sizeof jump);
        }
        if (!hooked) {
            write(kCave, kyo.data(), kyo.size());
            Discard(g_timer_hook);
            g_step = nullptr;
            host_log("Frame rate fixes: the frame timer left out: its hook could not be placed");
        } else {
            g_timer = true;
            next += kSlot;
            ++applied[std::size_t(Group::Timer)];
            // The camera's eases: the retune's (its jump over the game's camera is in) a 60 FPS frame's, unless
            // BB_CAMERA_FIX=1; the game's a 30 FPS frame's otherwise. As a 30 FPS frame's: 1 - (1 - w)^2.
            const auto skipped = std::vector<std::uint8_t>(std::begin(kCameraSkipped), std::end(kCameraSkipped));
            const char* fix = std::getenv("BB_CAMERA_FIX");
            const bool sixty = holds(kCameraSkip, skipped) && !(fix && fix[0] == '1');
            for (const Ease& e : kEases) {
                if (!on[std::size_t(Group::Camera)]) break;
                const double w = FromBits(sixty ? e.listed : e.game);
                const auto ease_disp = std::int32_t(next - (at(e.insn) + e.length));
                if (!write(e.insn + e.disp, &ease_disp, sizeof ease_disp)) continue;
                g_eases.push_back({next, sixty ? 1 - (1 - w) * (1 - w) : w});
                const float now = Rated(Kind::Decay, float(g_eases.back().was), step);
                std::memcpy(next, &now, sizeof now);
                next += kSlot;
                ++applied[std::size_t(Group::Camera)];
            }
        }
    }

    std::string list;
    for (std::size_t g = 0; g < applied.size(); ++g) {
        if (!applied[g]) continue;
        list += (list.empty() ? "" : ", ") + std::string(kGroupNames[g]) + " " + std::to_string(applied[g]);
    }
    if (list.empty()) {
        host_log("Frame rate fixes: none applied");
        return false;
    }
    g_on = !g_slots.empty() || g_play_ms || g_timer;
    g_self_hit = on[std::size_t(Group::Hitbox)];
    char where[96];
    if (rate.uncapped)
        std::snprintf(where, sizeof where, "uncapped, target %.0f FPS, values for %.0f", rate.target, rate.values);
    else
        std::snprintf(where, sizeof where, "at %.0f FPS", rate.values);
    host_log("Frame rate fixes: on %s (%s); %zu per-frame values at guest+0x%zx%s", where, list.c_str(),
             g_slots.size() + (g_play_ms ? 1 : 0) + g_eases.size() + (g_step ? 1 : 0), first,
             g_timer ? "; the frame timer ours: one step for the game and every value" : "");
    return true;
}

void Tick() {
    SelfHit();
    if (!g_on || g_timer || !Writable()) return; // with the timer, Step from the cave does it
    float step = 0;
    std::memcpy(&step, g_image + (kFrameTime - kBase), sizeof step);
    Update(std::isfinite(step) && step >= 0.001f && step <= 0.1f ? double(step) : g_fallback);
}

void Step(std::uint64_t pacer) {
    if (!g_on || !g_timer || !Writable()) return;
    // The cave's own: the pacer's measured frame time (+0x264) kept in [its target (+0x18), 1/30].
    float measured = 0, target = 0;
    if (pacer) {
        std::memcpy(&measured, reinterpret_cast<const void*>(pacer + 0x264), sizeof measured);
        std::memcpy(&target, reinterpret_cast<const void*>(pacer + 0x18), sizeof target);
    }
    const double floor = std::isfinite(target) && target > 0 ? double(target) : kShortest;
    const double seconds = std::isfinite(measured) && measured > 0 ? std::min(std::max(double(measured), floor), kLongest)
                                                                   : g_fallback;
    const float step = float(seconds);
    std::memcpy(g_step, &step, sizeof step);
    std::memcpy(g_image + (kFrameTime - kBase), &step, sizeof step);
    Update(seconds);
}
} // namespace BbEngine::FrameRate

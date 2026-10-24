// SPDX-License-Identifier: GPL-3.0-or-later
// Frame-rate corrections at 60 FPS (frame_rate.h), adapted from bbhost's src/engine/frame_rate.cpp
// (droogie/bbhost 566184a; licenses/BBHOST-PORTS.md). Kyo's "60 FPS++" stays the base: these are the
// systems it leaves on a fixed 30 FPS step, the durations it took for steps, and the play time's clock
// (frame_rate_sites.inc says what each one is). Every byte is checked before anything is written; a
// group with a mismatch is left out whole, the others still go in.
//
// The per-frame values are read from slots of ours past the end of the game's data segment (1.09: image
// offset 0x56d30f4), in that segment's last page: the loader protects each segment in whole pages, so
// those 3,852 bytes are writable with the data, and the linked modules (libc...) start after the page
// (0x56e0000). Not past the whole image: the last module's page there is not writable. Each frame the
// values are worked out from the frame's step the 60 FPS++ cave keeps in 0x575a908. This hook runs
// before the cave updates it, so they follow the frame before; the cave eases the step by 0.15 a frame,
// which keeps that difference small.
#include "frame_rate.h"
#include "engine_hooks.h"
#include "engine_state.h"
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
constexpr double kSixtieth = 1.0 / 60.0;
constexpr std::size_t kPage = 4096, kSlot = 16;
constexpr std::size_t kDataEnd = 0x56d30f4; // the end of 1.09's data segment (its VA + memsz)

enum class Group : std::uint8_t {
    Clips, Physics, Grass, Targeting, Jump, Sprint, Slope, Online, Hitbox, Timers, Menus, Durations, PlayTime,
    Count
};
constexpr const char* kGroupNames[] = {"clips",  "physics", "grass",  "targeting", "jump",      "sprint",  "slope",
                                       "online", "hitbox",  "timers", "menus",     "durations", "playtime"};
static_assert(std::size(kGroupNames) == std::size_t(Group::Count));

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
struct Require {
    std::uint64_t at;
    const char* bytes;
};
struct PlayTimeSite {
    Group group;
    std::uint64_t at;
    const char* was;
};

#define FRAME_SITE(g, at, was, listed, now) {Group::g, at, was, listed, now},
#define FRAME_LOAD(...)
#define FRAME_VALUE(...)
#define FRAME_REQUIRE(...)
#define FRAME_PLAY_TIME(...)
constexpr Site kSites[] = {
#include "frame_rate_sites.inc"
};
#undef FRAME_SITE
#undef FRAME_LOAD
#define FRAME_SITE(...)
#define FRAME_LOAD(g, insn, length, disp, constant, kind, lanes, bytes, ...)                                   \
    {Group::g, insn, length, disp, constant, Kind::kind, lanes, bytes, {__VA_ARGS__}},
constexpr Load kLoads[] = {
#include "frame_rate_sites.inc"
};
#undef FRAME_LOAD
#undef FRAME_VALUE
#define FRAME_LOAD(...)
#define FRAME_VALUE(g, at, kind, game, listed) {Group::g, at, Kind::kind, game, listed},
constexpr Value kValues[] = {
#include "frame_rate_sites.inc"
};
#undef FRAME_VALUE
#undef FRAME_REQUIRE
#define FRAME_VALUE(...)
#define FRAME_REQUIRE(at, bytes) {at, bytes},
constexpr Require kRequire[] = {
#include "frame_rate_sites.inc"
};
#undef FRAME_REQUIRE
#undef FRAME_PLAY_TIME
#define FRAME_REQUIRE(...)
#define FRAME_PLAY_TIME(g, at, was) {Group::g, at, was},
constexpr PlayTimeSite kPlayTime[] = {
#include "frame_rate_sites.inc"
};
#undef FRAME_SITE
#undef FRAME_LOAD
#undef FRAME_VALUE
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
bool g_on = false;
bool g_checked = false; // the slots' page found writable (first Tick, after the loader's protections)

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
    g_on = g_checked = false;
    g_slots.clear();
    g_play_ms = nullptr;
    g_play = {};
    if (const char* off = std::getenv("BB_FRAME_FIXES"); off && off[0] == '0') {
        host_log("Frame rate fixes: off (BB_FRAME_FIXES=0)");
        return false;
    }
    const char* fps = std::getenv("BB_FPS");
    if (!fps || std::strcmp(fps, "60") != 0) {
        host_log("Frame rate fixes: not at %s FPS (60 only for now)", fps ? fps : "an unknown");
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
    if (const char* skip = std::getenv("BB_FRAME_FIXES_SKIP")) {
        std::string name;
        for (const char* c = skip;; ++c) {
            if (*c && *c != ',') {
                if (!std::isspace(static_cast<unsigned char>(*c))) name += char(std::tolower(static_cast<unsigned char>(*c)));
                continue;
            }
            bool known = name.empty();
            for (std::size_t g = 0; g < std::size(kGroupNames); ++g) {
                if (name == kGroupNames[g]) {
                    on[g] = false;
                    known = true;
                }
            }
            if (!known) host_log("Frame rate fixes: no group \"%s\" to skip", name.c_str());
            name.clear();
            if (!*c) break;
        }
    }
    const auto fail = [&](Group g, std::uint64_t address) {
        if (!on[std::size_t(g)]) return;
        on[std::size_t(g)] = false;
        host_log("Frame rate fixes: %s left out: 0x%llx holds neither 1.09's bytes nor the 60 FPS++ list's",
                 kGroupNames[std::size_t(g)], (unsigned long long)address);
    };
    for (const Site& s : kSites) {
        const auto was = Hex(s.was), listed = Hex(s.listed), now = Hex(s.now);
        if (!holds(s.at, was) && !holds(s.at, listed) && !holds(s.at, now)) fail(s.group, s.at);
    }
    for (const Load& l : kLoads) {
        if (!holds(l.insn, Hex(l.bytes)) || !inside(l.constant, 4u * l.lanes)) fail(l.group, l.insn);
    }
    for (const Value& v : kValues) {
        std::uint32_t now = 0;
        if (!inside(v.at, 4)) {
            fail(v.group, v.at);
            continue;
        }
        std::memcpy(&now, at(v.at), 4);
        const std::uint32_t ours = ToBits(Rated(v.kind, FromBits(v.game), kSixtieth));
        if (now != v.game && now != v.listed && now != ours) fail(v.group, v.at);
    }
    for (const PlayTimeSite& p : kPlayTime) {
        if (!holds(p.at, Hex(p.was))) fail(p.group, p.at);
    }

    // The slots: from the first 16-byte boundary past the game's data to the end of that page, which must
    // be in the image and unused (zero, as the loader leaves it).
    std::size_t slots = 0;
    for (const Load& l : kLoads) slots += on[std::size_t(l.group)];
    for (const PlayTimeSite& p : kPlayTime) slots += on[std::size_t(p.group)];
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
        WriteSlot(slot, kSixtieth);
        const auto disp = std::int32_t(next - (at(l.insn) + l.length));
        if (!write(l.insn + l.disp, &disp, sizeof disp)) continue;
        g_slots.push_back(slot);
        next += kSlot;
        ++applied[std::size_t(l.group)];
    }
    for (const Value& v : kValues) {
        if (!on[std::size_t(v.group)]) continue;
        const std::uint32_t ours = ToBits(Rated(v.kind, FromBits(v.game), kSixtieth));
        if (write(v.at, &ours, sizeof ours)) ++applied[std::size_t(v.group)];
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

    std::string list;
    for (std::size_t g = 0; g < applied.size(); ++g) {
        if (!applied[g]) continue;
        list += (list.empty() ? "" : ", ") + std::string(kGroupNames[g]) + " " + std::to_string(applied[g]);
    }
    if (list.empty()) {
        host_log("Frame rate fixes: none applied");
        return false;
    }
    g_on = !g_slots.empty() || g_play_ms;
    host_log("Frame rate fixes: on at 60 FPS (%s); %zu per-frame values at guest+0x%zx", list.c_str(),
             g_slots.size() + (g_play_ms ? 1 : 0), first);
    return true;
}

void Tick() {
    if (!g_on) return;
    if (!g_checked) {
        // The loader sets each segment's protection after Install: the slots' page must have come out
        // writable. Otherwise they keep the values they were given for a 1/60 s frame.
        g_checked = true;
        const void* slots = g_slots.empty() ? g_play_ms : g_slots.front().at;
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
            host_log("Frame rate fixes: the per-frame values' page is not writable; they stay at 60 FPS's");
            g_on = false;
            return;
        }
    }
    float step = 0;
    std::memcpy(&step, g_image + (kFrameTime - kBase), sizeof step);
    const double seconds = std::isfinite(step) && step >= 0.001f && step <= 0.1f ? double(step) : kSixtieth;
    for (const Slot& s : g_slots) WriteSlot(s, seconds);
    if (g_play_ms) {
        const std::int32_t ms = g_play.Add(seconds);
        std::memcpy(g_play_ms, &ms, sizeof ms);
    }
}
} // namespace BbEngine::FrameRate

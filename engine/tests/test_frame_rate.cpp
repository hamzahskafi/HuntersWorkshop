// SPDX-License-Identifier: GPL-3.0-or-later
// The frame-rate corrections (gpu/shim/engine/frame_rate.cpp) on a synthetic image built from their own
// table (frame_rate_sites.inc): no game executable is needed. The image holds the game's bytes at every
// entry, with the 60 FPS++ list's where the list changes them, the list's frame-time code, and one
// group's bytes spoiled; Install must apply the rest, leave that group and a skipped one alone, and Tick
// must fill the slots from the frame's step. At 90, 120, 144 and uncapped it must also set the frame pacer's
// target, the values and counts for the rate and the cloth; without the target the game stays at 60. With the
// frame timer the cave must call ours, and Step (what its hook runs) must hand one step to the task step, the
// list's slot and every value, the camera's eases among them. The
// image is laid out and protected as the loader does it (src/probe.c): linked modules after the game's data,
// every page closed, then each segment opened in whole pages; a slot outside the game's data segment would
// fault in Tick (0.2.0's first build put them past the whole image, in a closed page).
#include "../gpu/shim/engine/frame_rate.h"
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace BbEngine {
void host_log(const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::putchar('\n');
}
// The param tables: AtkParam_Pc's row 2900931 alone (the Bloodletter's L2 self-hit), hit0_Radius first.
std::uint32_t g_self_hit_row[4] = {0x3ecccccd, 0, 0, 0};
bool g_params_loaded = false;
void* params_row(const char* table, std::uint32_t id, std::size_t* size) {
    if (!g_params_loaded || std::strcmp(table, "AtkParam_Pc") || id != 2900931) return nullptr;
    if (size) *size = sizeof g_self_hit_row;
    return g_self_hit_row;
}
bool host_read_safe(const void* p, void* out, std::size_t n) {
    std::memcpy(out, p, n);
    return true;
}
} // namespace BbEngine

namespace {
using BbEngine::FrameRate::Kind;
using BbEngine::FrameRate::Rated;
constexpr std::uint64_t kBase = 0x400000, kFrameTime = 0x575a908;
constexpr std::size_t kDataEnd = 0x56d30f4; // 1.09: the end of the game's data segment
constexpr std::size_t kSize = 0x594ba40;    // the whole image with the linked modules after it (a 0.2.0 log)
constexpr std::size_t kPage = 4096;
constexpr std::uint32_t kSixtieth = 0x3c888889;
constexpr float kFrameTimeProbe = 0.0123f;
std::size_t Pages(std::size_t n) { return (n + kPage - 1) & ~(kPage - 1); }

std::uint8_t* Allocate(std::size_t n) {
#ifdef _WIN32
    return static_cast<std::uint8_t*>(VirtualAlloc(nullptr, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
    void* p = mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
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
// flags as the boot image's segments: 1 execute, 2 write, 4 read; 0 closes the range.
void Protect(std::uint8_t* at, std::size_t n, unsigned flags) {
#ifdef _WIN32
    DWORD mode = PAGE_NOACCESS, old;
    if (flags & 1) mode = (flags & 2) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    else if (flags & 2) mode = PAGE_READWRITE;
    else if (flags & 4) mode = PAGE_READONLY;
    assert(VirtualProtect(at, n, mode, &old));
#else
    assert(!mprotect(at, n, (flags & 4 ? PROT_READ : 0) | (flags & 2 ? PROT_WRITE | PROT_READ : 0) |
                                (flags & 1 ? PROT_EXEC | PROT_READ : 0)));
#endif
}
void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

enum class Counted { Frames, Micros, Digits };
struct Site { std::string group; std::uint64_t at; std::string was, listed, now; };
struct Load {
    std::string group;
    std::uint64_t insn;
    int length, disp;
    std::uint64_t constant;
    Kind kind;
    int lanes;
    std::string bytes;
    std::vector<std::uint32_t> value;
};
struct Value { std::string group; std::uint64_t at; Kind kind; std::uint32_t game, listed; };
struct Count { std::string group; std::uint64_t at; int size; std::uint32_t game; Counted kind; };
std::vector<Site> sites;
std::vector<Load> loads;
std::vector<Value> values;
std::vector<Count> counts;
struct Target { std::uint64_t at; std::string store; std::uint32_t game; std::size_t imm() const { return store.size() / 2; } };
std::vector<Target> targets;
std::vector<std::pair<std::uint64_t, std::string>> required, cloth_scales;
struct Ease { std::uint64_t insn; int length, disp; std::uint64_t constant; std::string bytes; std::uint32_t game, listed; };
std::vector<Ease> eases;
std::pair<std::uint64_t, std::string> play_time;

void Collect() {
#define FRAME_SITE(g, at, was, listed, now) sites.push_back({#g, at, was, listed, now});
#define FRAME_LOAD(g, insn, length, disp, constant, kind, lanes, bytes, ...)                                   \
    loads.push_back({#g, insn, length, disp, constant, Kind::kind, lanes, bytes, {__VA_ARGS__}});
#define FRAME_VALUE(g, at, kind, game, listed) values.push_back({#g, at, Kind::kind, game, listed});
#define FRAME_COUNT(g, at, size, game, kind) counts.push_back({#g, at, size, game, Counted::kind});
#define FRAME_TARGET(at, store, game) targets.push_back({at, store, game});
#define FRAME_CLOTH_SCALE(at, was) cloth_scales.push_back({at, was});
#define FRAME_EASE(insn, length, disp, constant, bytes, game, listed)                                         \
    eases.push_back({insn, length, disp, constant, bytes, game, listed});
#define FRAME_REQUIRE(at, bytes) required.push_back({at, bytes});
#define FRAME_PLAY_TIME(g, at, was) play_time = {at, was};
#include "../gpu/shim/engine/frame_rate_sites.inc"
#undef FRAME_SITE
#undef FRAME_LOAD
#undef FRAME_VALUE
#undef FRAME_COUNT
#undef FRAME_TARGET
#undef FRAME_CLOTH_SCALE
#undef FRAME_EASE
#undef FRAME_REQUIRE
#undef FRAME_PLAY_TIME
}

std::vector<std::uint8_t> Hex(const std::string& text) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) out.push_back(std::uint8_t(std::stoi(text.substr(i, 2), nullptr, 16)));
    return out;
}
float F(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
bool Near(double a, double b) { return std::fabs(a - b) <= 1e-6 * std::max(1.0, std::fabs(b)); }

// A count as the engine writes it at `rate` (frame_rate.cpp CountBytes).
std::vector<std::uint8_t> CountAt(const Count& c, double rate) {
    double n = c.game;
    if (c.kind == Counted::Frames) n = std::round(c.game * rate / 30.0);
    else if (c.kind == Counted::Micros) n = std::round(c.game * 30.0 / rate);
    else if (rate != 30.0) n = std::floor(1e6 / rate);
    std::vector<std::uint8_t> out(std::size_t(c.size));
    if (c.kind == Counted::Digits) {
        const std::string text = std::to_string(std::uint32_t(n));
        const std::string padded = std::string(std::size_t(c.size) - text.size(), '0') + text;
        std::memcpy(out.data(), padded.data(), out.size());
    } else {
        for (std::size_t i = 0; i < out.size(); ++i) out[i] = std::uint8_t(std::uint32_t(n) >> (8 * i));
    }
    return out;
}

// The loads an entry's site writes (the cloth caves'): no instruction of the game's to put there.
bool InSite(const Load& l) {
    for (const Site& s : sites)
        if (s.group == l.group && l.insn >= s.at && l.insn < s.at + Hex(s.now).size()) return true;
    return false;
}

struct Image {
    std::uint8_t* base;
    std::size_t size;
    std::uint8_t* at(std::uint64_t address) const { return base + (address - kBase); }
    void put(std::uint64_t address, const std::vector<std::uint8_t>& bytes) const {
        std::memcpy(at(address), bytes.data(), bytes.size());
    }
    template <typename T> T get(std::uint64_t address) const {
        T v;
        std::memcpy(&v, at(address), sizeof v);
        return v;
    }
    const std::uint8_t* slot_of(const Load& l) const {
        std::int32_t disp;
        std::memcpy(&disp, at(l.insn) + l.disp, 4);
        return at(l.insn) + l.length + disp;
    }
    // A site holds `bytes`, but for the displacements of the loads in it (pointed at slots).
    bool holds(const Site& s, const std::vector<std::uint8_t>& bytes) const {
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            bool disp = false;
            for (const Load& l : loads)
                disp = disp || (s.at + i >= l.insn + std::uint64_t(l.disp) && s.at + i < l.insn + std::uint64_t(l.disp) + 4);
            if (!disp && at(s.at)[i] != bytes[i]) return false;
        }
        return true;
    }
};

// The game's bytes everywhere, with the 60 FPS++ list's where it writes (`listed`), or the game's alone.
Image Build(std::size_t size, bool listed) {
    Image image{Allocate(Pages(size)), size};
    assert(image.base);
    for (const auto& [address, bytes] : required) image.put(address, Hex(bytes));
    for (const Site& s : sites) image.put(s.at, Hex(listed && !s.listed.empty() ? s.listed : s.was));
    for (const Load& l : loads) {
        if (!InSite(l)) image.put(l.insn, Hex(l.bytes));
        std::memcpy(image.at(l.constant), l.value.data(), 4 * l.value.size());
    }
    if (listed) {
        // The list's momentum (0.983, its 90 FPS value) over the game's 0.95: the slot starts from 0.95.
        const float momentum[4] = {0.9830475f, 0.9830475f, 0.9830475f, 0.9830475f};
        std::memcpy(image.at(0x4d0b5b0), momentum, sizeof momentum);
    }
    for (const Value& v : values) std::memcpy(image.at(v.at), listed ? &v.listed : &v.game, 4);
    for (const Count& c : counts) image.put(c.at, CountAt(c, listed && c.kind != Counted::Digits ? 60.0 : 30.0));
    for (const Target& t : targets) {
        image.put(t.at, Hex(t.store));
        std::memcpy(image.at(t.at) + t.imm(), listed ? &kSixtieth : &t.game, 4);
    }
    for (const auto& [address, was] : cloth_scales) image.put(address, Hex(was));
    for (const Ease& e : eases) {
        image.put(e.insn, Hex(e.bytes));
        std::memcpy(image.at(e.constant), listed ? &e.listed : &e.game, 4);
    }
    image.put(play_time.first, Hex(play_time.second));
    return image;
}
// The loader's protections after Install (probe.c): all closed, then the game's code and data segments
// (1.09: RX 0x0 + 0x50d96dc, RW 0x50dc000 + 0x5f70f4) in whole pages; the modules' pages stay closed.
void Protections(const Image& image) {
    Protect(image.base, Pages(image.size), 0);
    Protect(image.base, Pages(0x50d96dc), 5);
    Protect(image.base + 0x50dc000, Pages(0x5f70f4), 6);
}
void Step(const Image& image, double seconds) {
    const float s = float(seconds);
    std::memcpy(image.at(kFrameTime), &s, 4);
    BbEngine::FrameRate::Tick();
}

void TestRated() {
    assert(Near(Rated(Kind::Keep, 0.95f, 1 / 30.0), 0.95f));
    assert(Near(Rated(Kind::Keep, 0.95f, 1 / 60.0), std::sqrt(0.95)));
    assert(Near(Rated(Kind::Keep, 1.2f, 1 / 60.0), std::sqrt(double(1.2f))));
    assert(Near(Rated(Kind::Rate, 30.0f, 1 / 60.0), 60.0));
    assert(Near(Rated(Kind::Amount, 1.0f, 1 / 60.0), 0.5));
    assert(Near(Rated(Kind::Amount, -1 / 30.0f, 1 / 60.0), -1 / 60.0));
    assert(Near(Rated(Kind::Step, 1 / 30.0f, 1 / 60.0), 1 / 60.0));
    assert(Near(Rated(Kind::Decay, 0.1f, 1 / 60.0), 1 - std::sqrt(0.9)));
    assert(Rated(Kind::Game, 30.0f, 1 / 60.0) == 30.0f);
    assert(Near(Rated(Kind::Cloth, 0, 1 / 90.0), 4 / 9.0)); // Kyo's 1.3: 4/9 at 90, 1/4 at 120
    assert(Near(Rated(Kind::Cloth, 0, 1 / 120.0), 0.25));
    assert(Rated(Kind::Cloth, 0, 1 / 45.0) == 1.0f); // a slow frame: the cloth's own tuning, no more
}

void TestPlayTime() {
    BbEngine::FrameRate::PlayTime clock;
    long total = 0;
    for (int i = 0; i < 600; ++i) total += clock.Add(1 / 60.0);
    assert(total == 10000 || total == 9999); // ten seconds; the game's truncation gave 9,600
}

void TestTable() {
    Collect();
    assert(sites.size() == 70 && loads.size() == 31 && values.size() == 38 && counts.size() == 4 &&
           targets.size() == 5 && cloth_scales.size() == 1 && eases.size() == 2 && required.size() == 2);
}

// 60 FPS, the list's bytes, Online spoiled and Slope skipped by name: the rest goes in, the cloth stays the
// list's (one substep a frame is what it was made for), the sound's update time is one frame.
void TestSixty() {
    const Image image = Build(kSize, true);
    const Load* online = nullptr;
    for (const Load& l : loads) if (l.group == "Online" && !online) online = &l;
    image.at(online->insn)[0] ^= 0xff;
    SetEnv("BB_FPS", "60");
    SetEnv("BB_FRAME_FIXES_SKIP", "Slope");
    assert(BbEngine::FrameRate::Install(image.base, image.size));
    SetEnv("BB_FRAME_FIXES_SKIP", nullptr);
    Protections(image);

    for (const Site& s : sites) {
        const auto expect = Hex(s.group == "Cloth" ? s.was : s.now);
        assert(image.holds(s, expect)); // the wind's step: a site the Grass load in it points at a slot
    }
    for (const Value& v : values) {
        const float now = image.get<float>(v.at);
        if (v.kind == Kind::Game) assert(now == F(v.game));
        else assert(Near(now, Rated(v.kind, F(v.game), 1 / 60.0)));
    }
    for (const Count& c : counts) assert(!std::memcmp(image.at(c.at), CountAt(c, 60).data(), std::size_t(c.size)));
    assert(!std::memcmp(image.at(0x4b33c51), "16666", 5));
    for (const Target& t : targets) assert(image.get<std::uint32_t>(t.at + t.imm()) == kSixtieth);
    for (const auto& [address, was] : cloth_scales) assert(!std::memcmp(image.at(address), Hex(was).data(), 16));
    const std::size_t first = (kDataEnd + 15) & ~std::size_t(15), end = Pages(kDataEnd);
    for (const Load& l : loads) {
        if (l.group == "Slope" || l.group == "Online" || l.group == "Cloth") {
            if (l.group == "Slope") assert(!std::memcmp(image.at(l.insn), Hex(l.bytes).data(), std::size_t(l.length)));
            continue;
        }
        const std::uint8_t* slot = image.slot_of(l);
        assert(slot >= image.base + first && slot + 16 <= image.base + end);
    }

    // A frame of 1/60, then one of 1/30 (the clamp's longest): the slots follow it.
    for (double step : {1 / 60.0, 1 / 30.0}) {
        Step(image, step);
        for (const Load& l : loads) {
            if (l.group == "Slope" || l.group == "Online" || l.group == "Cloth") continue;
            const std::uint8_t* slot = image.slot_of(l);
            for (int i = 0; i < l.lanes; ++i) {
                float got;
                std::memcpy(&got, slot + 4 * i, 4);
                assert(Near(got, Rated(l.kind, F(l.value[i]), float(step))));
            }
        }
    }
    for (const Load& l : loads) {
        if (l.insn != 0x1e4abca) continue;
        float jump;
        std::memcpy(&jump, image.slot_of(l), 4);
        assert(Near(jump, 0.95)); // the last frame was 1/30: the game's own 0.95
    }
    // The play time: mov ecx, [slot] over the multiply, the slot a frame's milliseconds.
    const std::uint8_t* code = image.at(play_time.first);
    assert(code[0] == 0x8b && code[1] == 0x0d);
    std::int32_t disp, ms;
    std::memcpy(&disp, code + 2, 4);
    std::memcpy(&ms, code + 6 + disp, 4);
    assert(ms == 33 || ms == 34); // the last frame was 1/30 s
    Free(image.base, Pages(image.size));
}

// 90, 120 and 144 on the 60 FPS++ list: the pacer's target, the values and counts for the rate, the cloth
// caves with their scale per frame.
void TestFixedRate(int fps) {
    const Image image = Build(kSize, true);
    SetEnv("BB_FPS", std::to_string(fps).c_str());
    assert(BbEngine::FrameRate::Install(image.base, image.size));
    Protections(image);
    const double frame = 1.0 / fps;
    for (const Target& t : targets) assert(image.get<float>(t.at + t.imm()) == float(frame));
    for (const Site& s : sites) assert(image.holds(s, Hex(s.now)));
    for (const Value& v : values) {
        const float now = image.get<float>(v.at);
        if (v.kind == Kind::Game) assert(now == F(v.game));
        else assert(Near(now, Rated(v.kind, F(v.game), frame)));
    }
    for (const Count& c : counts) assert(!std::memcmp(image.at(c.at), CountAt(c, fps).data(), std::size_t(c.size)));
    assert(image.get<std::int32_t>(0x192c405) == fps / 3 && image.get<std::int32_t>(0x25629f1) == 990000 / fps);
    // The caves' loads read a slot: (60 / fps)^2 at the rate, 1 for a frame of 1/30; the game's text stays.
    for (const auto& [address, was] : cloth_scales) assert(!std::memcmp(image.at(address), Hex(was).data(), 16));
    for (double step : {frame, 1 / 30.0}) {
        Step(image, step);
        int caves = 0;
        for (const Load& l : loads) {
            if (l.group != "Cloth") continue;
            ++caves;
            const std::uint8_t* slot = image.slot_of(l);
            assert(slot >= image.base + kDataEnd && slot + 16 <= image.base + Pages(kDataEnd));
            for (int i = 0; i < 4; ++i) {
                float got;
                std::memcpy(&got, slot + 4 * i, 4);
                assert(Near(got, step == frame ? (60.0 / fps) * (60.0 / fps) : 1.0));
            }
        }
        assert(caves == 6);
    }
    Free(image.base, Pages(image.size));
}

// Uncapped: the target its limit (none: 480), the values for 120, the sound's update one frame up to 240.
void TestUncapped() {
    for (const char* limit : {"0", "144"}) {
        const Image image = Build(kSize, true);
        SetEnv("BB_FPS", "uncap");
        SetEnv("BB_FPS_LIMIT", limit);
        assert(BbEngine::FrameRate::Install(image.base, image.size));
        const double target = limit[0] == '0' ? 480 : 144;
        for (const Target& t : targets) assert(image.get<float>(t.at + t.imm()) == float(1 / target));
        for (const Value& v : values)
            if (v.kind != Kind::Game) assert(Near(image.get<float>(v.at), Rated(v.kind, F(v.game), 1 / 120.0)));
        assert(image.get<std::int32_t>(0x192c405) == 40);
        assert(!std::memcmp(image.at(0x4b33c51), target == 480 ? "04166" : "06944", 5));
        Free(image.base, Pages(image.size));
    }
    SetEnv("BB_FPS_LIMIT", nullptr);
}

// At 90 with a target the list does not hold: the pacer left alone, so everything is for 60 and no cloth.
void TestNoPace() {
    const Image image = Build(kSize, true);
    image.at(targets[1].at + targets[1].imm() + 1)[0] ^= 0x10;
    SetEnv("BB_FPS", "90");
    assert(BbEngine::FrameRate::Install(image.base, image.size));
    for (std::size_t i = 0; i < targets.size(); ++i)
        if (i != 1) assert(image.get<std::uint32_t>(targets[i].at + targets[i].imm()) == kSixtieth);
    for (const Value& v : values)
        if (v.kind != Kind::Game) assert(Near(image.get<float>(v.at), Rated(v.kind, F(v.game), 1 / 60.0)));
    for (const Count& c : counts) assert(!std::memcmp(image.at(c.at), CountAt(c, 60).data(), std::size_t(c.size)));
    for (const Site& s : sites)
        if (s.group == "Cloth") assert(!std::memcmp(image.at(s.at), Hex(s.was).data(), Hex(s.was).size()));
    Free(image.base, Pages(image.size));
}

// An image that ends inside the game's data segment's last page: no room for the slots, so the per-frame
// values are left out and the rest still goes in; above 60 the cloth caves read a scale for the rate.
void TestNoRoom() {
    for (const char* fps : {"60", "90"}) {
        const std::size_t size = kDataEnd + 0x100;
        const Image image = Build(size, false);
        SetEnv("BB_FPS", fps);
        assert(BbEngine::FrameRate::Install(image.base, image.size));
        const bool ninety = fps[0] == '9';
        for (const Site& s : sites) {
            const auto expect = Hex(s.group == "Cloth" && !ninety ? s.was : s.now);
            assert(!std::memcmp(image.at(s.at), expect.data(), expect.size()));
        }
        for (const Load& l : loads) {
            if (l.group == "Cloth") continue;
            const auto b = Hex(l.bytes);
            assert(!std::memcmp(image.at(l.insn), b.data(), b.size())); // as it was: no slot to point it at
        }
        const float scale = image.get<float>(cloth_scales[0].first);
        assert(ninety ? Near(scale, 4 / 9.0) : !std::memcmp(image.at(cloth_scales[0].first), "DLLi", 4));
        const auto play = Hex(play_time.second);
        assert(!std::memcmp(image.at(play_time.first), play.data(), play.size()));
        BbEngine::FrameRate::Tick(); // nothing per frame
        Free(image.base, Pages(image.size));
    }
}
} // namespace

// The frame pacer's two fields the timer reads: the measured frame time (+0x264) and the target (+0x18).
struct Pacer {
    alignas(16) std::uint8_t bytes[0x300]{};
    void set(float measured, float target) {
        std::memcpy(bytes + 0x264, &measured, 4);
        std::memcpy(bytes + 0x18, &target, 4);
    }
    std::uint64_t address() const { return reinterpret_cast<std::uint64_t>(bytes); }
};
const std::uint8_t* Pointed(const Image& image, std::uint64_t insn, int length, int disp) {
    std::int32_t d;
    std::memcpy(&d, image.at(insn) + disp, 4);
    return image.at(insn) + length + d;
}

// The frame timer at 60 and 120, the FPS++ retune's camera or the game's: the cave calls ours, and Step hands
// one step to the task step (through its slot), the list's 0x575a908 and every value; Tick does nothing.
void TestTimer() {
    for (const bool retune : {false, true}) {
        for (const char* fps : {"60", "120"}) {
            const Image image = Build(kSize, true);
            constexpr std::uint64_t kCave = 0x2418e3d, kSkip = 0x183dc96;
            if (retune) image.put(kSkip, {0xe9, 0xd6, 0x05, 0x00, 0x00});
            SetEnv("BB_FPS", fps);
            SetEnv("BB_FRAME_TIMER", "1");
            assert(BbEngine::FrameRate::Install(image.base, image.size));
            SetEnv("BB_FRAME_TIMER", nullptr);
            Protections(image);
            const double rate = fps[0] == '6' ? 60 : 120;
            // push rdi; mov rdi, rbx; jmp [rip+0] to the hook; pop rdi; movss xmm0, [step]; movss [rbp-0x38], xmm0.
            const std::uint8_t* cave = image.at(kCave);
            assert(cave[0] == 0x57 && cave[1] == 0x48 && cave[2] == 0x89 && cave[3] == 0xdf);
            assert(cave[4] == 0xff && cave[5] == 0x25 && cave[18] == 0x5f);
            assert(cave[19] == 0xf3 && cave[20] == 0x0f && cave[21] == 0x10 && cave[22] == 0x05);
            assert(cave[27] == 0xf3 && cave[28] == 0x0f && cave[29] == 0x11 && cave[30] == 0x45 && cave[31] == 0xc8);
            const std::uint8_t* step_slot = Pointed(image, kCave + 19, 8, 4);
            assert(step_slot >= image.base + kDataEnd && step_slot + 4 <= image.base + Pages(kDataEnd));
            float step;
            std::memcpy(&step, step_slot, 4);
            assert(step == float(1 / rate)); // until the first frame: a frame at the rate

            Pacer pacer;
            const auto check = [&](float measured, float target, double expect) {
                pacer.set(measured, target);
                BbEngine::FrameRate::Step(pacer.address());
                float got;
                std::memcpy(&got, step_slot, 4);
                assert(Near(got, expect));
                assert(image.get<float>(kFrameTime) == got); // the list's slot: the same step, not eased
                for (const Load& l : loads) {
                    if (l.group != "Jump") continue;
                    float momentum;
                    std::memcpy(&momentum, image.slot_of(l), 4);
                    assert(Near(momentum, Rated(Kind::Keep, 0.95f, got)));
                }
                for (const Ease& e : eases) {
                    float w;
                    std::memcpy(&w, Pointed(image, e.insn, e.length, e.disp), 4);
                    const double base = retune ? 1 - (1 - double(F(e.listed))) * (1 - double(F(e.listed))) : double(F(e.game));
                    assert(Near(w, 1 - std::pow(1 - base, 30.0 * got)));
                }
            };
            check(1 / 45.0f, float(1 / rate), 1 / 45.0);       // a long frame: its own length, at once
            check(1 / 200.0f, float(1 / rate), 1 / rate);      // a short one: no shorter than the target
            check(0.2f, float(1 / rate), 1 / 30.0);            // the game slows below 30, as the cave has it
            check(1 / 300.0f, 0, 1 / 300.0);                   // no target (a loading screen): the measured time
            check(std::nanf(""), float(1 / rate), 1 / rate);   // no measurement: a frame at the rate
            if (retune && rate == 60) {
                // The FPS++ retune at 60 eases exactly as the list's values did.
                check(1 / 60.0f, 1 / 60.0f, 1 / 60.0);
                for (const Ease& e : eases) {
                    float w;
                    std::memcpy(&w, Pointed(image, e.insn, e.length, e.disp), 4);
                    assert(Near(w, F(e.listed)));
                }
            }
            // The play time: the steps' milliseconds; Tick (the frame hook) leaves everything to Step.
            const std::uint8_t* code = image.at(play_time.first);
            std::int32_t disp, ms;
            std::memcpy(&disp, code + 2, 4);
            std::memcpy(&ms, code + 6 + disp, 4);
            assert(ms >= 0 && ms <= 34);
            float before, after;
            std::memcpy(&before, step_slot, 4);
            std::memcpy(image.at(kFrameTime), &kFrameTimeProbe, 4);
            BbEngine::FrameRate::Tick();
            std::memcpy(&after, step_slot, 4);
            assert(before == after);
            Free(image.base, Pages(image.size));
        }
    }
}

// The cave run as the game runs it: a trampoline sets rbx (the pacer) and rbp (the task function's frame) and
// calls into it with the stack as the game's at that point, a ret after its 46 bytes. It must reach Step
// through the hook's stub, come back, and leave the step in the task step's step-info ([rbp-0x38]).
void TestCaveRuns() {
    const Image image = Build(kSize, true);
    constexpr std::uint64_t kCave = 0x2418e3d;
    SetEnv("BB_FPS", "60");
    SetEnv("BB_FRAME_TIMER", "1");
    assert(BbEngine::FrameRate::Install(image.base, image.size));
    SetEnv("BB_FRAME_TIMER", nullptr);
    Protections(image);
    // A ret where the task function goes on (its page opened for it: code pages are read and execute).
    std::uint8_t* const page = image.at(kCave + 46) - (reinterpret_cast<std::uintptr_t>(image.at(kCave + 46)) % kPage);
    Protect(page, kPage, 7);
    image.put(kCave + 46, {0xc3});
    Protect(page, kPage, 5);
    // push rbx; push rbp; mov rbx, rdi; mov rbp, rsi; call rdx; pop rbp; pop rbx; ret. The cave starts with the
    // stack 16-byte aligned, as before a call in the game.
    static const std::uint8_t kTrampoline[] = {0x53, 0x55, 0x48, 0x89, 0xfb, 0x48, 0x89, 0xf5,
                                               0xff, 0xd2, 0x5d, 0x5b, 0xc3};
    std::uint8_t* code = Allocate(kPage);
    std::memcpy(code, kTrampoline, sizeof kTrampoline);
    Protect(code, kPage, 5);
    using Trampoline = void(__attribute__((sysv_abi)) *)(std::uint64_t, std::uint64_t, std::uint64_t);
    Pacer pacer;
    alignas(16) std::uint8_t frame[0x100]{};
    for (const float measured : {1 / 60.0f, 1 / 40.0f}) {
        pacer.set(measured, 1 / 60.0f);
        reinterpret_cast<Trampoline>(code)(pacer.address(), reinterpret_cast<std::uint64_t>(frame + 0x80),
                                          reinterpret_cast<std::uint64_t>(image.at(kCave)));
        float task_step;
        std::memcpy(&task_step, frame + 0x80 - 0x38, 4);
        assert(task_step == measured);
        assert(image.get<float>(kFrameTime) == measured);
    }
    Free(code, kPage);
    Free(image.base, Pages(image.size));
}

// The Bloodletter's L2 self-hit: 0.4 m made 0.6 once the table is loaded (looked for again while it is not),
// another value left alone, and nothing with the Hitbox group left out.
void TestSelfHit() {
    using BbEngine::g_params_loaded;
    using BbEngine::g_self_hit_row;
    const auto run = [](const char* skip, std::uint32_t radius, int frames) {
        const Image image = Build(kSize, true);
        SetEnv("BB_FPS", "60");
        SetEnv("BB_FRAME_FIXES_SKIP", skip);
        g_params_loaded = false;
        g_self_hit_row[0] = radius;
        assert(BbEngine::FrameRate::Install(image.base, image.size));
        SetEnv("BB_FRAME_FIXES_SKIP", nullptr);
        Protections(image);
        BbEngine::FrameRate::Tick(); // not loaded yet
        assert(g_self_hit_row[0] == radius);
        g_params_loaded = true;
        for (int i = 0; i < frames; ++i) BbEngine::FrameRate::Tick();
        return g_self_hit_row[0];
    };
    assert(run(nullptr, 0x3ecccccd, 119) == 0x3ecccccd); // looked at again 120 frames later
    assert(run(nullptr, 0x3ecccccd, 120) == 0x3f19999a); // 0.6
    assert(run(nullptr, 0x3f000000, 240) == 0x3f000000); // a mod's 0.5 stays
    assert(run("hitbox", 0x3ecccccd, 240) == 0x3ecccccd);
    g_params_loaded = false;
}

int main() {
    TestRated();
    TestPlayTime();
    TestTable();
    TestSixty();
    for (int fps : {90, 120, 144}) TestFixedRate(fps);
    TestUncapped();
    TestNoPace();
    TestNoRoom();
    TestTimer();
    TestCaveRuns();
    TestSelfHit();
    std::puts("frame-rate-test: all ok");
    return 0;
}

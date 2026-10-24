// SPDX-License-Identifier: GPL-3.0-or-later
// The frame-rate corrections (gpu/shim/engine/frame_rate.cpp) on a synthetic image built from their own
// table (frame_rate_sites.inc): no game executable is needed. The image holds the game's bytes at every
// entry, with the 60 FPS++ list's where the list changes them, the list's frame-time code, and one
// group's bytes spoiled; Install must apply the rest, leave that group and a skipped one alone, and Tick
// must fill the slots from the frame's step.
#include "../gpu/shim/engine/frame_rate.h"
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace BbEngine {
void host_log(const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::putchar('\n');
}
} // namespace BbEngine

namespace {
using BbEngine::FrameRate::Kind;
constexpr std::uint64_t kBase = 0x400000, kFrameTime = 0x575a908;
constexpr std::size_t kSize = 0x56d30f4; // 1.09's image: the end of its data segment

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
std::vector<Site> sites;
std::vector<Load> loads;
std::vector<Value> values;
std::vector<std::pair<std::uint64_t, std::string>> required;
std::pair<std::uint64_t, std::string> play_time;

void Collect() {
#define FRAME_SITE(g, at, was, listed, now) sites.push_back({#g, at, was, listed, now});
#define FRAME_LOAD(g, insn, length, disp, constant, kind, lanes, bytes, ...)                                   \
    loads.push_back({#g, insn, length, disp, constant, Kind::kind, lanes, bytes, {__VA_ARGS__}});
#define FRAME_VALUE(g, at, kind, game, listed) values.push_back({#g, at, Kind::kind, game, listed});
#define FRAME_REQUIRE(at, bytes) required.push_back({at, bytes});
#define FRAME_PLAY_TIME(g, at, was) play_time = {at, was};
#include "../gpu/shim/engine/frame_rate_sites.inc"
#undef FRAME_SITE
#undef FRAME_LOAD
#undef FRAME_VALUE
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

void TestRated() {
    using BbEngine::FrameRate::Rated;
    assert(Near(Rated(Kind::Keep, 0.95f, 1 / 30.0), 0.95f));
    assert(Near(Rated(Kind::Keep, 0.95f, 1 / 60.0), std::sqrt(0.95)));
    assert(Near(Rated(Kind::Keep, 1.2f, 1 / 60.0), std::sqrt(double(1.2f))));
    assert(Near(Rated(Kind::Rate, 30.0f, 1 / 60.0), 60.0));
    assert(Near(Rated(Kind::Amount, 1.0f, 1 / 60.0), 0.5));
    assert(Near(Rated(Kind::Amount, -1 / 30.0f, 1 / 60.0), -1 / 60.0));
    assert(Near(Rated(Kind::Step, 1 / 30.0f, 1 / 60.0), 1 / 60.0));
    assert(Near(Rated(Kind::Decay, 0.1f, 1 / 60.0), 1 - std::sqrt(0.9)));
    assert(Rated(Kind::Game, 30.0f, 1 / 60.0) == 30.0f);
}

void TestPlayTime() {
    BbEngine::FrameRate::PlayTime clock;
    long total = 0;
    for (int i = 0; i < 600; ++i) total += clock.Add(1 / 60.0);
    assert(total == 10000 || total == 9999); // ten seconds; the game's truncation gave 9,600
}

void TestInstall() {
    Collect();
    assert(sites.size() == 7 && loads.size() == 23 && values.size() == 22 && required.size() == 2);
    std::vector<std::uint8_t> image((kSize + 4095) & ~std::size_t(4095));
    const auto at = [&](std::uint64_t address) { return image.data() + (address - kBase); };
    const auto put = [&](std::uint64_t address, const std::vector<std::uint8_t>& bytes) {
        std::memcpy(at(address), bytes.data(), bytes.size());
    };
    for (const auto& [address, bytes] : required) put(address, Hex(bytes));
    for (const Site& s : sites) put(s.at, Hex(s.listed.empty() ? s.was : s.listed));
    for (const Load& l : loads) {
        put(l.insn, Hex(l.bytes));
        std::memcpy(at(l.constant), l.value.data(), 4 * l.value.size());
    }
    // The list's momentum (0.983, its 90 FPS value) over the game's 0.95: the slot starts from 0.95.
    const std::uint64_t momentum = 0x4d0b5b0;
    const float listed_momentum[4] = {0.9830475f, 0.9830475f, 0.9830475f, 0.9830475f};
    std::memcpy(at(momentum), listed_momentum, sizeof listed_momentum);
    for (const Value& v : values) std::memcpy(at(v.at), &v.listed, 4);
    put(play_time.first, Hex(play_time.second));
    // Online spoiled: left out, the others still in. Slope skipped by name.
    const Load* online = nullptr;
    for (const Load& l : loads) if (l.group == "Online" && !online) online = &l;
    at(online->insn)[0] ^= 0xff;
#ifdef _WIN32
    _putenv_s("BB_FPS", "60");
    _putenv_s("BB_FRAME_FIXES_SKIP", "Slope");
#else
    setenv("BB_FPS", "60", 1);
    setenv("BB_FRAME_FIXES_SKIP", "Slope", 1);
#endif
    assert(BbEngine::FrameRate::Install(image.data(), kSize));

    for (const Site& s : sites) assert(!std::memcmp(at(s.at), Hex(s.now).data(), Hex(s.now).size()));
    for (const Value& v : values) {
        float now;
        std::memcpy(&now, at(v.at), 4);
        if (v.kind == Kind::Game) assert(now == F(v.game));
        else assert(Near(now, BbEngine::FrameRate::Rated(v.kind, F(v.game), 1 / 60.0)));
    }
    const std::size_t first = (kSize + 15) & ~std::size_t(15), end = image.size();
    const auto slot_of = [&](const Load& l) -> const std::uint8_t* {
        std::int32_t disp;
        std::memcpy(&disp, at(l.insn) + l.disp, 4);
        return at(l.insn) + l.length + disp;
    };
    for (const Load& l : loads) {
        const bool left = l.group == "Slope" || l.group == "Online";
        if (left) {
            const auto bytes = Hex(l.bytes);
            assert(l.group == "Online" || !std::memcmp(at(l.insn), bytes.data(), bytes.size()));
            continue;
        }
        const std::uint8_t* slot = slot_of(l);
        assert(slot >= image.data() + first && slot + 16 <= image.data() + end);
    }

    // A frame of 1/60, then one of 1/30 (the clamp's longest): the slots follow it.
    for (double step : {1 / 60.0, 1 / 30.0}) {
        const float s = float(step);
        std::memcpy(at(kFrameTime), &s, 4);
        BbEngine::FrameRate::Tick();
        for (const Load& l : loads) {
            if (l.group == "Slope" || l.group == "Online") continue;
            const std::uint8_t* slot = slot_of(l);
            for (int i = 0; i < l.lanes; ++i) {
                float got;
                std::memcpy(&got, slot + 4 * i, 4);
                assert(Near(got, BbEngine::FrameRate::Rated(l.kind, F(l.value[i]), s)));
            }
        }
    }
    float jump;
    for (const Load& l : loads) {
        if (l.insn != 0x1e4abca) continue;
        std::memcpy(&jump, slot_of(l), 4);
        assert(Near(jump, 0.95)); // the last frame was 1/30: the game's own 0.95
    }
    // The play time: mov ecx, [slot] over the multiply, the slot a frame's milliseconds.
    const std::uint8_t* code = at(play_time.first);
    assert(code[0] == 0x8b && code[1] == 0x0d);
    std::int32_t disp, ms;
    std::memcpy(&disp, code + 2, 4);
    std::memcpy(&ms, code + 6 + disp, 4);
    assert(ms == 33 || ms == 34); // the last frame was 1/30 s
}
} // namespace

int main() {
    TestRated();
    TestPlayTime();
    TestInstall();
    std::puts("frame-rate-test: all ok");
    return 0;
}

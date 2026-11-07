// SPDX-License-Identifier: GPL-3.0-or-later
// Where the player is (areas.h).
#include "areas.h"
#include "engine_state.h"
#include "loading.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

extern "C" void runtime_area_hook(void (*fn)(int area));

namespace BbEngine::Areas {
namespace {
using Set = std::bitset<kAreas>;

constexpr double kHalfLifeSeconds = 600.0;
constexpr std::uint32_t kGoneSeconds = 120; // an area not present this long is left
constexpr float kLikelyShare = 0.25f;       // of a row of the next-area table

struct Visit {
    std::uint32_t id = 0;
    std::uint32_t start = 0;
    Set areas;
    bool by_load = false;
};

std::mutex g_mutex;
std::array<Visit, kRing> g_visits;
Set g_previous; // the areas of the visit before
bool g_in_load = false, g_destination = false, g_installed = false;
std::uint32_t g_load_start = 0, g_logged = 0;
std::array<std::uint32_t, kAreas> g_last_present{}, g_entries{}, g_score_at{}, g_next_at{};
std::array<float, kAreas> g_score{};
std::array<std::array<float, kAreas>, kAreas> g_next{};

std::uint32_t NowSecond() {
    return std::uint32_t(std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count());
}

float Decay(float value, std::uint32_t at, std::uint32_t now) {
    return now > at ? value * float(std::exp2(-double(now - at) / kHalfLifeSeconds)) : value;
}

Visit& Current() {
    return g_visits[current_visit.load(std::memory_order_relaxed) % kRing];
}

std::string Names(const Set& set) {
    std::string out;
    for (int a = 0; a < kAreas; ++a) {
        if (set.test(a)) {
            char name[8];
            std::snprintf(name, sizeof(name), "%sm%02d", out.empty() ? "" : " ", a);
            out += name;
        }
    }
    return out.empty() ? "none" : out;
}

void StartVisit(std::uint32_t now, bool by_load) {
    Visit& was = Current();
    if (was.areas.any()) {
        g_previous = was.areas;
    }
    if (g_logged < 200) {
        ++g_logged;
        host_log("Areas: visit %u (%s, %u s) ends, %s", was.id, Names(was.areas).c_str(), now - was.start,
                 by_load ? "a loading screen" : "another area's files opened");
    }
    const std::uint32_t id = current_visit.load(std::memory_order_relaxed) + 1;
    Visit& next = g_visits[id % kRing];
    next = {id, now, {}, by_load};
    g_destination = false;
    current_visit.store(id, std::memory_order_relaxed);
}

void Add(int area, std::uint32_t now) {
    Visit& v = Current();
    g_last_present[area] = now;
    if (v.areas.test(area)) {
        return;
    }
    v.areas.set(area);
    ++g_entries[area];
    g_score[area] = Decay(g_score[area], g_score_at[area], now) + 1.0f;
    g_score_at[area] = now;
    // Where the player went next from the areas before (not counting staying).
    for (int from = 0; from < kAreas; ++from) {
        if (!g_previous.test(from) || from == area || v.areas.test(from)) {
            continue;
        }
        auto& row = g_next[from];
        if (g_next_at[from] != now) {
            for (float& x : row) {
                x = Decay(x, g_next_at[from], now);
            }
            g_next_at[from] = now;
        }
        row[area] += 1.0f;
    }
}

void NoteOpen(int area) {
    if (area < 0 || area >= kAreas) {
        return;
    }
    const std::uint32_t now = NowSecond();
    std::scoped_lock lock{g_mutex};
    if (Loading::ScreenUp()) {
        if (!g_in_load) {
            StartVisit(now, true);
            g_in_load = true;
            g_load_start = now;
        }
        g_destination = true;
    } else if (!Current().areas.test(area) && Current().areas.any() && now - g_last_present[area] > kGoneSeconds) {
        StartVisit(now, false);
    }
    Add(area, now);
}

// Called by the file layer (C) for every map file the game opens.
void NoteOpenC(int area) {
    NoteOpen(area);
}
} // namespace

void Install() {
    if (const char* env = std::getenv("BB_VRAM_AREAS"); env && env[0] == '0') {
        return;
    }
    g_visits[1] = {1, NowSecond(), {}, false};
    runtime_area_hook(&NoteOpenC);
    g_installed = true;
}

void Tick() {
    if (!g_installed) {
        return;
    }
    const std::uint32_t now = NowSecond();
    const bool up = Loading::ScreenUp();
    std::scoped_lock lock{g_mutex};
    if (up && !g_in_load) {
        StartVisit(now, true);
        g_in_load = true;
        g_load_start = now;
    } else if (!up && g_in_load) {
        g_in_load = false;
        // A loading screen that opened no map files (a respawn where the area stayed loaded): the
        // same place as before.
        if (Current().areas.none()) {
            Current().areas = g_previous;
        }
    }
    const Set areas = Current().areas;
    for (int a = 0; a < kAreas; ++a) {
        if (areas.test(a)) {
            g_last_present[a] = now;
        }
    }
}

bool DestinationKnown() {
    if (!g_installed) {
        return Loading::ScreenUp();
    }
    std::scoped_lock lock{g_mutex};
    return g_in_load && (g_destination || NowSecond() - g_load_start >= 1);
}

View Snapshot() {
    View view;
    view.guard.assign(kRing, 0);
    view.keep.assign(kRing, 0.0f);
    view.id.assign(kRing, 0);
    const std::uint32_t now = NowSecond();
    std::scoped_lock lock{g_mutex};
    view.visit = current_visit.load(std::memory_order_relaxed);
    // Protected: where the player is (or the place before, while the new visit has no areas yet), and
    // the likely next areas from there.
    const Set here = Current().areas.any() ? Current().areas : g_previous;
    Set guarded = here;
    for (int from = 0; from < kAreas; ++from) {
        if (!here.test(from)) {
            continue;
        }
        float total = 0.0f;
        for (int to = 0; to < kAreas; ++to) {
            total += Decay(g_next[from][to], g_next_at[from], now);
        }
        for (int to = 0; total > 0.0f && to < kAreas; ++to) {
            const float share = Decay(g_next[from][to], g_next_at[from], now);
            if (share >= 1.0f && share >= kLikelyShare * total) {
                guarded.set(to);
            }
        }
    }
    std::array<float, kAreas> score;
    for (int a = 0; a < kAreas; ++a) {
        score[a] = Decay(g_score[a], g_score_at[a], now);
    }
    for (std::uint32_t i = 0; i < kRing; ++i) {
        const Visit& v = g_visits[i];
        if (v.id == 0 || view.visit - v.id >= kRing) {
            continue;
        }
        view.id[i] = v.id;
        view.guard[i] = v.id == view.visit || (v.areas & guarded).any();
        float keep = 0.0f;
        for (int a = 0; a < kAreas; ++a) {
            if (v.areas.test(a)) {
                keep = std::max(keep, score[a]);
            }
        }
        view.keep[i] = keep;
    }
    return view;
}

void LogMinute() {
    if (!g_installed) {
        return;
    }
    const std::uint32_t now = NowSecond();
    std::scoped_lock lock{g_mutex};
    // The three best-kept areas.
    std::array<std::pair<float, int>, kAreas> ranked;
    for (int a = 0; a < kAreas; ++a) {
        ranked[a] = {Decay(g_score[a], g_score_at[a], now), a};
    }
    std::partial_sort(ranked.begin(), ranked.begin() + 3, ranked.end(),
                      [](const auto& x, const auto& y) { return x.first > y.first; });
    std::string learned;
    for (int i = 0; i < 3 && ranked[i].first >= 0.05f; ++i) {
        char item[32];
        std::snprintf(item, sizeof(item), "%sm%02d %.1f (%u visits)", learned.empty() ? "" : ", ",
                      ranked[i].second, ranked[i].first, g_entries[ranked[i].second]);
        learned += item;
    }
    host_log("Areas: now %s (visit %u, %u s); kept longest: %s", Names(Current().areas).c_str(), Current().id,
             now - Current().start, learned.empty() ? "nothing yet" : learned.c_str());
}
} // namespace BbEngine::Areas

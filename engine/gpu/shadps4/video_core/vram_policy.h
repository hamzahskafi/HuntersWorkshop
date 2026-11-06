// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.5 beta 14: one VRAM manager for the texture cache and the buffer cache.
//
// Both caches read the same number, the game's VRAM use against the driver's current budget (which
// shrinks when other programs take VRAM). Until beta 13 each had marks and age rules of its own:
// images loaded from the game's memory went 20 s after their last use at any VRAM use (always on
// cards up to about 10 GB), any image unused for 1 s from 80%; the game's data unused for 3 minutes
// at any use, 30 s from 85%, 10 s from 90%. Now one rule decides for both, once a second:
//
//   Room  below the tight mark: nothing is freed for its age.
//   Tight from the tight mark (80%) until back under it by 5 points: what has gone unused longest
//         goes first, textures and the game's data alike, in steps: unused for 5 minutes, then 2,
//         1 and 30 s, a step down after each second that freed less than 16 MiB.
//   Full  from the full mark (90%), or for 30 s after an allocation failed: unused for 10 s, then
//         1 s; textures the GPU drew are stored into the game's memory and freed as well.
//
// Each second the bytes to free are the use above the tight mark less 5 points; the caches stop once
// that much is freed (NoteFreed) and the next second measures again.
//
// BB_LESS_VRAM=1 (the launcher's "Use less VRAM"): tight 65%, full 75%. BB_VRAM_TIGHT_PERCENT=N:
// tight N% (30 to 90), full 10 points higher (at most 95). BB_VRAM_TEST_BUDGET_MB=N: the budget is
// N MiB, so a card acts as a smaller one would (testing).
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "bbport_toggles.h"
#include "common/types.h"

namespace VideoCore::VramPolicy {

enum class Level : u8 { Room = 0, Tight = 1, Full = 2 };

struct Config {
    u64 tight_percent;
    u64 full_percent;
    u64 test_budget; ///< bytes; 0: the driver's
    bool less;
};

[[nodiscard]] inline const Config& Settings() {
    static const Config config = [] {
        Config c{80, 90, 0, false};
        if (const char* env = std::getenv("BB_LESS_VRAM"); env && env[0] == '1') {
            c = {65, 75, 0, true};
        }
        if (const char* env = std::getenv("BB_VRAM_TIGHT_PERCENT"); env && env[0]) {
            c.tight_percent = std::clamp<u64>(std::strtoull(env, nullptr, 10), 30, 90);
            c.full_percent = std::min<u64>(c.tight_percent + 10, 95);
        }
        if (const char* env = std::getenv("BB_VRAM_TEST_BUDGET_MB"); env && env[0]) {
            c.test_budget = std::strtoull(env, nullptr, 10) << 20;
        }
        return c;
    }();
    return config;
}

/// Unused for at least this many seconds, by step: Tight 0-3, Full 4-5.
inline constexpr u32 StepSeconds[] = {300, 120, 60, 30, 10, 1};
inline constexpr u32 LastTightStep = 3, FirstFullStep = 4, LastFullStep = 5;
/// A step that freed less than this in a second has nothing more of that age: the next one.
inline constexpr s64 StepDownBelow = 16ll << 20;
/// The game's data freed at most this much a second while Tight (each block is a sparse unbind).
inline constexpr u64 DataBytesPerSecondTight = 256ull << 20;

struct State {
    std::mutex mutex;
    std::atomic<u32> second{0};
    std::atomic<Level> level{Level::Room};
    std::atomic<u32> step{0};
    std::atomic<s64> to_free{0};
    std::atomic<s64> freed_second{0};
    std::atomic<u64> used{0}, budget{0};
    u32 full_until = 0;
    u32 reports = 0;
    /// Since the last TakeMinute.
    std::atomic<u64> texture_bytes{0}, data_bytes{0};
    std::atomic<u32> youngest{~0u}, tight_seconds{0}, full_seconds{0};
};
inline State state;

[[nodiscard]] inline u32 NowSecond() {
    return u32(std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count());
}

[[nodiscard]] inline Level CurrentLevel() {
    return state.level.load(std::memory_order_relaxed);
}
/// How long something must have gone unused to be freed now (~0: nothing is, VRAM has room).
[[nodiscard]] inline u32 MinAgeSeconds() {
    return CurrentLevel() == Level::Room ? ~0u
                                         : StepSeconds[state.step.load(std::memory_order_relaxed)];
}
/// Whether more should be freed this second.
[[nodiscard]] inline bool WantBytes() {
    return state.to_free.load(std::memory_order_relaxed) > 0;
}
[[nodiscard]] inline u64 TightBytes() {
    return state.budget.load(std::memory_order_relaxed) / 100 * Settings().tight_percent;
}

/// A cache freed `bytes` unused for at least `age` seconds to make room.
inline void NoteFreed(bool texture, u64 bytes, u32 age) {
    state.to_free.fetch_sub(s64(bytes), std::memory_order_relaxed);
    state.freed_second.fetch_add(s64(bytes), std::memory_order_relaxed);
    (texture ? state.texture_bytes : state.data_bytes).fetch_add(bytes, std::memory_order_relaxed);
    u32 young = state.youngest.load(std::memory_order_relaxed);
    while (age < young &&
           !state.youngest.compare_exchange_weak(young, age, std::memory_order_relaxed)) {
    }
}

/// The level and step from this second's measurement (one caller a second).
inline void Decide(u32 now, u64 used, u64 budget) {
    std::scoped_lock lock{state.mutex};
    const Config& config = Settings();
    const Level was = state.level.load(std::memory_order_relaxed);
    Level next = Level::Room;
    u64 low = 0;
    if (budget != 0) {
        const u64 tight = budget / 100 * config.tight_percent;
        const u64 full = budget / 100 * config.full_percent;
        low = budget / 100 * (config.tight_percent - 5);
        next = used >= full                              ? Level::Full
               : used >= tight                           ? Level::Tight
               : was != Level::Room && used >= low       ? Level::Tight
                                                         : Level::Room;
    }
    if (now < state.full_until) {
        next = Level::Full;
    }
    const s64 freed = state.freed_second.exchange(0, std::memory_order_relaxed);
    u32 step = state.step.load(std::memory_order_relaxed);
    if (next == Level::Room) {
        step = 0;
    } else if (was == Level::Room) {
        step = next == Level::Full ? FirstFullStep : 0;
    } else if (next == Level::Full && step < FirstFullStep) {
        step = FirstFullStep;
    } else if (next == Level::Tight && step > LastTightStep) {
        step = LastTightStep;
    } else if (freed < StepDownBelow) {
        step = std::min(step + 1, next == Level::Full ? LastFullStep : LastTightStep);
    }
    s64 to_free = 0;
    if (next != Level::Room) {
        to_free = used > low ? s64(used - low) : 0;
        if (now < state.full_until) {
            to_free = std::max<s64>(to_free, 256ll << 20); // an allocation failed: room for it
        }
        (next == Level::Full ? state.full_seconds : state.tight_seconds)
            .fetch_add(1, std::memory_order_relaxed);
    }
    state.step.store(step, std::memory_order_relaxed);
    state.to_free.store(to_free, std::memory_order_relaxed);
    state.used.store(used, std::memory_order_relaxed);
    state.budget.store(budget, std::memory_order_relaxed);
    state.level.store(next, std::memory_order_relaxed);
    BbStats::gc_used_bytes.store(used, std::memory_order_relaxed);
    BbStats::gc_trigger_bytes.store(budget / 100 * config.tight_percent, std::memory_order_relaxed);
    if (next != was && state.reports < 40) {
        ++state.reports;
        static constexpr const char* kWhat[] = {
            "room again: nothing is freed for its age",
            "tight: what has gone unused longest is freed, textures and the game's data alike",
            "full: what has gone unused for 10 s is freed, textures the GPU drew included",
        };
        std::printf("VRAM manager: %llu of %llu MiB (tight from %llu%%%s), %s\n",
                    (unsigned long long)(used >> 20), (unsigned long long)(budget >> 20),
                    (unsigned long long)config.tight_percent,
                    config.less ? ", Use less VRAM" : "", kWhat[u32(next)]);
        std::fflush(stdout);
    }
}

/// Measures once a second, whichever cache calls first: the driver's count of the game's VRAM
/// against its current budget (without VK_EXT_memory_budget, the engine's own Vulkan memory
/// against the budget at the start).
template <typename InstanceT>
inline void Update(const InstanceT& instance) {
    const u32 now = NowSecond();
    u32 last = state.second.load(std::memory_order_relaxed);
    if (last == now || !state.second.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
        return;
    }
    u64 used = 0, budget = 0;
    if (instance.CanReportMemoryUsage()) {
        used = instance.GetDeviceMemoryUsage();
        budget = instance.GetDeviceMemoryBudgetNow();
    } else {
        used = BbStats::device_alloc_bytes.load(std::memory_order_relaxed) -
               BbStats::device_free_bytes.load(std::memory_order_relaxed);
        budget = instance.GetTotalMemoryBudget();
    }
    if (Settings().test_budget != 0) {
        budget = Settings().test_budget;
    }
    Decide(now, used, budget);
}

/// An allocation of VRAM failed: Full for 30 s.
inline void NoteAllocationFailed() {
    {
        std::scoped_lock lock{state.mutex};
        state.full_until = NowSecond() + 30;
    }
    state.second.store(0, std::memory_order_relaxed); // measured again at the next call
}

/// What was freed for room since the last call, for the minute's line.
struct Minute {
    u64 texture_bytes, data_bytes;
    u32 youngest, tight_seconds, full_seconds;
};
[[nodiscard]] inline Minute TakeMinute() {
    return {state.texture_bytes.exchange(0), state.data_bytes.exchange(0),
            state.youngest.exchange(~0u), state.tight_seconds.exchange(0),
            state.full_seconds.exchange(0)};
}

} // namespace VideoCore::VramPolicy

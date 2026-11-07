// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.5 beta 14-15: one VRAM manager for the texture cache and the buffer cache.
//
// Both caches read the same number, the game's VRAM use against the driver's current budget (which
// shrinks when other programs take VRAM). Beta 15 adds where the player is (engine/areas.h) and when
// work is free:
//
//   Lines (of the budget): comfortable 70%, tight 80%, full 90% (BB_LESS_VRAM=1, the launcher's
//   "Use less VRAM": 55/65/75). The comfortable line rises 5 points while freed data keeps coming
//   back soon after (refaults over a quarter of what was freed in the last 10 minutes).
//
//   What each cache holds is in use (the last second), garbage (the game wrote over its memory, or GPU
//   work through a buffer did, and it has gone 10 s unused), only in VRAM (drawn by the GPU), or
//   cache: a valid copy, idle. Each is tagged with the area visit it was last used in. Protected:
//   the visits of the current areas, the loading screen's destination and the likely next areas.
//
//   When (Mode):
//     garbage    any level, once a second, up to 64 textures, while frames have slack or a loading
//                screen is up (GarbageAllowed), outside protected visits;
//     Load       a loading screen is up and its destination known, VRAM over the comfortable line:
//                trim to it (the screen hides the work);
//     Tight      in play from the tight line until back under it by 5 points: the same order, only
//                while frames have slack, at most 12 ms a second and one game-data chunk a second;
//     Full       from the full line, or for 30 s after an allocation failed: at once, protected idle
//                data too (unused 10 s, then 1 s) and textures the GPU drew.
//   Order: garbage, then cache of unprotected visits by their areas' learned score (lowest first),
//   what returns memory to the driver first (textures with memory of their own, whole game-data
//   chunks), small textures last; protected data unused for 5 minutes only while tight or at a load.
//
// BB_VRAM_TIGHT_PERCENT=N: tight N% (30 to 90), comfortable N - 10, full N + 10 (at most 95).
// BB_VRAM_TEST_BUDGET_MB=N: the budget is N MiB (a smaller card on this one). BB_VRAM_AREAS=0: no
// areas (every visit the same: ages only).
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "bbport_toggles.h"
#include "common/types.h"
#include "engine/areas.h"
#include "engine/loading.h"

namespace VideoCore::VramPolicy {

enum class Level : u8 { Room = 0, Tight = 1, Full = 2 };
enum class Mode : u8 { None = 0, Load = 1, Tight = 2, Full = 3 };

struct Config {
    u64 comfort_percent;
    u64 tight_percent;
    u64 full_percent;
    u64 test_budget; ///< bytes; 0: the driver's
    bool less;
};

[[nodiscard]] inline const Config& Settings() {
    static const Config config = [] {
        Config c{70, 80, 90, 0, false};
        if (const char* env = std::getenv("BB_LESS_VRAM"); env && env[0] == '1') {
            c = {55, 65, 75, 0, true};
        }
        if (const char* env = std::getenv("BB_VRAM_TIGHT_PERCENT"); env && env[0]) {
            c.tight_percent = std::clamp<u64>(std::strtoull(env, nullptr, 10), 30, 90);
            c.comfort_percent = c.tight_percent - 10;
            c.full_percent = std::min<u64>(c.tight_percent + 10, 95);
        }
        if (const char* env = std::getenv("BB_VRAM_TEST_BUDGET_MB"); env && env[0]) {
            c.test_budget = std::strtoull(env, nullptr, 10) << 20;
        }
        return c;
    }();
    return config;
}

/// While full: unused for at least 10 s, then 1 s.
inline constexpr u32 FullSeconds[] = {10, 1};
inline constexpr u32 GarbageSeconds = 10;        ///< unused this long, garbage goes
inline constexpr u32 CacheSeconds = 30;          ///< unprotected cache unused this long may go
inline constexpr u32 ProtectedIdleSeconds = 300; ///< protected data unused this long, while tight
inline constexpr u32 GarbagePerSecond = 64;      ///< textures, while frames have slack
inline constexpr s64 PlayNsPerSecond = 12'000'000; ///< play trims: about 0.2 ms a frame at 60 FPS
inline constexpr u32 SlackFlips = 30;            ///< frames on time in a row: the frame has slack
inline constexpr s64 StepDownBelow = 16ll << 20;

/// What was freed (NoteFreed).
enum class Freed : u8 { Garbage = 0, Texture = 1, Data = 2 };

struct State {
    std::mutex mutex;
    std::atomic<u32> second{0};
    std::atomic<Level> level{Level::Room};
    std::atomic<Mode> mode{Mode::None};
    std::atomic<u32> full_step{0};
    std::atomic<s64> to_free{0};
    std::atomic<s64> freed_second{0};
    std::atomic<s64> play_ns{0};
    std::atomic<u64> used{0}, budget{0};
    std::atomic<u32> comfort_bonus{0}; ///< points over the comfortable line (refaults)
    u32 full_until = 0;
    u32 reports = 0;
    // A loading-screen trim under way: VRAM when it began, and what it freed.
    Mode last_mode = Mode::None;
    u64 load_start_used = 0;
    std::atomic<u64> load_freed{0};
    // This minute.
    std::array<std::atomic<u64>, 3> freed_bytes{};
    std::atomic<u64> returned_bytes{0}, refault_bytes{0}, loads_trimmed{0}, play_seconds{0};
    std::atomic<u32> tight_seconds{0}, full_seconds{0};
    // The last 10 minutes: freed and refaulted (for the comfortable line).
    std::array<u64, 10> history_freed{}, history_refault{};
    u32 history_at = 0;
    // What the caches hold, by kind (their minute passes).
    std::atomic<u64> tex_in_use{0}, tex_cache{0}, tex_garbage{0}, tex_gpu{0};
    std::atomic<u64> data_in_use{0}, data_cache{0}, data_empty{0};
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
[[nodiscard]] inline Mode CurrentMode() {
    return state.mode.load(std::memory_order_relaxed);
}
/// While full: how long something must have gone unused.
[[nodiscard]] inline u32 FullAgeSeconds() {
    return FullSeconds[std::min<u32>(state.full_step.load(std::memory_order_relaxed), 1)];
}
[[nodiscard]] inline bool WantBytes() {
    return state.to_free.load(std::memory_order_relaxed) > 0;
}
/// Frames have been on time for a while: in-play work does not show.
[[nodiscard]] inline bool Slack() {
    return BbStats::flips_on_time.load(std::memory_order_relaxed) >= SlackFlips;
}
/// Whether a trim may run now: at a loading screen or when full always; while tight only with slack
/// and within this second's time.
[[nodiscard]] inline bool Allowed() {
    switch (CurrentMode()) {
    case Mode::Load:
    case Mode::Full:
        return true;
    case Mode::Tight:
        return Slack() && state.play_ns.load(std::memory_order_relaxed) < PlayNsPerSecond;
    default:
        return false;
    }
}
[[nodiscard]] inline bool GarbageAllowed() {
    return BbEngine::Loading::ScreenUp() || Slack();
}
/// Time a trim took in play (counted against this second's).
inline void NoteWork(s64 ns) {
    if (CurrentMode() == Mode::Tight) {
        state.play_ns.fetch_add(ns, std::memory_order_relaxed);
    }
}
[[nodiscard]] inline u64 TightBytes() {
    return state.budget.load(std::memory_order_relaxed) / 100 * Settings().tight_percent;
}

/// A cache freed `bytes`; `returned`: of them, what went back to the driver at once (memory of its
/// own, or a whole chunk).
inline void NoteFreed(Freed kind, u64 bytes, u64 returned) {
    state.to_free.fetch_sub(s64(bytes), std::memory_order_relaxed);
    state.freed_second.fetch_add(s64(bytes), std::memory_order_relaxed);
    state.freed_bytes[u32(kind)].fetch_add(bytes, std::memory_order_relaxed);
    state.returned_bytes.fetch_add(returned, std::memory_order_relaxed);
    if (CurrentMode() == Mode::Load) {
        state.load_freed.fetch_add(bytes, std::memory_order_relaxed);
    }
}
/// Something freed in the last 5 minutes was needed again.
inline void NoteRefault(u64 bytes) {
    state.refault_bytes.fetch_add(bytes, std::memory_order_relaxed);
}
inline void PublishTextures(u64 in_use, u64 cache, u64 garbage, u64 gpu) {
    state.tex_in_use.store(in_use, std::memory_order_relaxed);
    state.tex_cache.store(cache, std::memory_order_relaxed);
    state.tex_garbage.store(garbage, std::memory_order_relaxed);
    state.tex_gpu.store(gpu, std::memory_order_relaxed);
}
inline void PublishData(u64 in_use, u64 cache, u64 empty) {
    state.data_in_use.store(in_use, std::memory_order_relaxed);
    state.data_cache.store(cache, std::memory_order_relaxed);
    state.data_empty.store(empty, std::memory_order_relaxed);
}

/// The level and mode from this second's measurement (one caller a second).
inline void Decide(u32 now, u64 used, u64 budget) {
    BbEngine::Areas::Tick();
    std::scoped_lock lock{state.mutex};
    const Config& config = Settings();
    const Level was = state.level.load(std::memory_order_relaxed);
    const u64 comfort_percent =
        std::min<u64>(config.comfort_percent + state.comfort_bonus.load(std::memory_order_relaxed),
                      config.tight_percent - 5);
    Level next = Level::Room;
    u64 low = 0, comfort = 0;
    if (budget != 0) {
        const u64 tight = budget / 100 * config.tight_percent;
        const u64 full = budget / 100 * config.full_percent;
        low = budget / 100 * (config.tight_percent - 5);
        comfort = budget / 100 * comfort_percent;
        next = used >= full                        ? Level::Full
               : used >= tight                     ? Level::Tight
               : was != Level::Room && used >= low ? Level::Tight
                                                   : Level::Room;
    }
    if (now < state.full_until) {
        next = Level::Full;
    }
    const bool loading = BbEngine::Loading::ScreenUp();
    Mode mode = Mode::None;
    s64 to_free = 0;
    if (next == Level::Full) {
        mode = Mode::Full;
        to_free = used > low ? s64(used - low) : 0;
        if (now < state.full_until) {
            to_free = std::max<s64>(to_free, 256ll << 20); // an allocation failed: room for it
        }
    } else if (next == Level::Tight) {
        mode = loading ? Mode::Load : Mode::Tight;
        to_free = s64(used - std::min(used, loading ? std::min(low, comfort) : low));
    } else if (loading && budget != 0 && used > comfort && BbEngine::Areas::DestinationKnown()) {
        mode = Mode::Load;
        to_free = s64(used - comfort);
    }
    // While full, one step faster after a second that freed little.
    const s64 freed = state.freed_second.exchange(0, std::memory_order_relaxed);
    const u32 step = state.full_step.load(std::memory_order_relaxed);
    state.full_step.store(next != Level::Full ? 0 : was != Level::Full ? 0 : freed < StepDownBelow ? 1 : step,
                          std::memory_order_relaxed);
    state.play_ns.store(0, std::memory_order_relaxed);
    if (mode == Mode::Tight) {
        state.play_seconds.fetch_add(1, std::memory_order_relaxed);
    }
    if (next == Level::Tight) {
        state.tight_seconds.fetch_add(1, std::memory_order_relaxed);
    } else if (next == Level::Full) {
        state.full_seconds.fetch_add(1, std::memory_order_relaxed);
    }
    // A loading-screen trim: its start, and its result once it ends.
    if (mode == Mode::Load && state.last_mode != Mode::Load) {
        state.load_start_used = used;
        state.load_freed.store(0, std::memory_order_relaxed);
    } else if (mode != Mode::Load && state.last_mode == Mode::Load) {
        state.loads_trimmed.fetch_add(1, std::memory_order_relaxed);
        if (state.reports < 200) {
            ++state.reports;
            std::printf("VRAM manager: loading screen trim, %llu MiB in use at its start, %llu MiB freed "
                        "(%llu MiB in use now, comfortable at %llu%%)\n",
                        (unsigned long long)(state.load_start_used >> 20),
                        (unsigned long long)(state.load_freed.load(std::memory_order_relaxed) >> 20),
                        (unsigned long long)(used >> 20), (unsigned long long)comfort_percent);
        }
    }
    state.last_mode = mode;
    state.to_free.store(to_free, std::memory_order_relaxed);
    state.used.store(used, std::memory_order_relaxed);
    state.budget.store(budget, std::memory_order_relaxed);
    state.level.store(next, std::memory_order_relaxed);
    state.mode.store(mode, std::memory_order_relaxed);
    BbStats::gc_used_bytes.store(used, std::memory_order_relaxed);
    BbStats::gc_trigger_bytes.store(budget / 100 * config.tight_percent, std::memory_order_relaxed);
    if (next != was && state.reports < 200) {
        ++state.reports;
        static constexpr const char* kWhat[] = {
            "room again: only garbage is freed (and trims at loading screens over the comfortable line)",
            "tight: idle data of areas the player is not in is freed while frames have slack",
            "full: idle data of any area is freed at once, textures the GPU drew included",
        };
        std::printf("VRAM manager: %llu of %llu MiB (comfortable %llu%%, tight %llu%%%s), %s\n",
                    (unsigned long long)(used >> 20), (unsigned long long)(budget >> 20),
                    (unsigned long long)comfort_percent, (unsigned long long)config.tight_percent,
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

/// An allocation of VRAM failed: full for 30 s.
inline void NoteAllocationFailed() {
    {
        std::scoped_lock lock{state.mutex};
        state.full_until = NowSecond() + 30;
    }
    state.second.store(0, std::memory_order_relaxed); // measured again at the next call
}

/// The minute's lines (driver.cpp), and the comfortable line's adjustment from refaults.
inline void LogMinute() {
    auto mib = [](u64 bytes) { return (unsigned long long)(bytes >> 20); };
    const Config& config = Settings();
    const u64 garbage = state.freed_bytes[0].exchange(0), textures = state.freed_bytes[1].exchange(0),
              data = state.freed_bytes[2].exchange(0);
    const u64 returned = state.returned_bytes.exchange(0), refault = state.refault_bytes.exchange(0);
    const u64 loads = state.loads_trimmed.exchange(0), play = state.play_seconds.exchange(0);
    const u32 tight = state.tight_seconds.exchange(0), full = state.full_seconds.exchange(0);
    // Refaults over a quarter of what was freed in the last 10 minutes: keep 5 points more.
    u64 freed10 = 0, refault10 = 0;
    {
        std::scoped_lock lock{state.mutex};
        state.history_freed[state.history_at % 10] = garbage + textures + data;
        state.history_refault[state.history_at % 10] = refault;
        ++state.history_at;
        for (u32 i = 0; i < 10; ++i) {
            freed10 += state.history_freed[i];
            refault10 += state.history_refault[i];
        }
    }
    const u32 bonus = state.comfort_bonus.load(std::memory_order_relaxed);
    const u32 want = freed10 >= (64ull << 20) && refault10 * 4 > freed10 ? 5u : refault10 == 0 ? 0u : bonus;
    if (want != bonus) {
        state.comfort_bonus.store(want, std::memory_order_relaxed);
        std::printf("VRAM manager: %s (%llu of %llu MiB freed in the last 10 minutes came back within 5)\n",
                    want ? "freed data keeps coming back: the comfortable line rises 5 points"
                         : "nothing freed came back for 10 minutes: the comfortable line is back",
                    mib(refault10), mib(freed10));
    }
    const u64 used = state.used.load(), budget = state.budget.load();
    const u64 ws = state.tex_in_use.load() + state.data_in_use.load();
    std::printf("VRAM manager: %llu of %llu MiB, working set %llu MiB%s; comfortable %llu%%, tight %llu%%, full "
                "%llu%%%s\n",
                mib(used), mib(budget), mib(ws),
                budget && ws * 100 > budget * config.tight_percent ? " (over the tight line by itself: lower "
                                                                       "the resolution or the upscaler preset)"
                                                                     : "",
                (unsigned long long)std::min<u64>(config.comfort_percent + state.comfort_bonus.load(),
                                                  config.tight_percent - 5),
                (unsigned long long)config.tight_percent, (unsigned long long)config.full_percent,
                config.less ? " (Use less VRAM)" : "");
    std::printf("  textures: in use %llu MiB, cache %llu, garbage %llu, drawn by the GPU %llu; the game's data: "
                "in use %llu MiB, cache %llu, empty %llu\n",
                mib(state.tex_in_use.load()), mib(state.tex_cache.load()), mib(state.tex_garbage.load()),
                mib(state.tex_gpu.load()), mib(state.data_in_use.load()), mib(state.data_cache.load()),
                mib(state.data_empty.load()));
    if (garbage + textures + data == 0 && tight == 0 && full == 0 && loads == 0) {
        std::printf("  this minute: nothing freed\n");
    } else {
        std::printf("  this minute: freed garbage %llu MiB, textures %llu, the game's data %llu; returned to the "
                    "driver at once %llu; came back within 5 minutes %llu; %llu loading screen trims, %llu s of "
                    "play trims; tight %u s, full %u s\n",
                    mib(garbage), mib(textures), mib(data), mib(returned), mib(refault),
                    (unsigned long long)loads, (unsigned long long)play, tight, full);
    }
    BbEngine::Areas::LogMinute();
}

} // namespace VideoCore::VramPolicy

// SPDX-License-Identifier: GPL-3.0-or-later
// The fork-join dispatcher's waits (job_barrier.h).
#include "job_barrier.h"
#include "job_pool.h"
#include "bbport_counter.h"

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#endif

extern "C" std::uint64_t runtime_tsc_frequency(void);

namespace BbEngine::JobBarrier {
namespace {

BbStats::Counter g_count_waits, g_count_ticks, g_slots_waits, g_slots_ticks, g_yielded, g_yields;

std::uint64_t Budget() {
    static const std::uint64_t ticks = [] {
        const std::uint64_t hz = runtime_tsc_frequency();
        return static_cast<std::uint64_t>(static_cast<double>(hz ? hz : 3'000'000'000ull) * kSpinUs / 1e6);
    }();
    return ticks;
}

void YieldCore() {
#ifdef _WIN32
    SwitchToThread(); // a thread ready on this processor gets it; none: back at once
#else
    sched_yield();
#endif
}

void Note(const Waited& w, BbStats::Counter& waits, BbStats::Counter& ticks) {
    waits.Add();
    ticks.Add(w.ticks);
    if (w.yields) {
        g_yielded.Add();
        g_yields.Add(w.yields);
    }
}

} // namespace

bool Ready(const std::uint8_t* image, std::size_t size, const char** why) {
    if (kDispatch + kDispatchSize > size) {
        *why = "the image is too small";
        return false;
    }
    const auto print = JobPool::Fingerprint(image + kDispatch, kDispatchSize, {});
    if (!print || *print != kDispatchHash) {
        *why = "the fork-join dispatcher differs from 1.09";
        return false;
    }
    return true;
}

__attribute__((sysv_abi)) std::int32_t CountWait(const void* counter, std::int32_t need, const void* more) {
    const Waited w = Wait([&] { return CountReady(counter, need, more); }, Budget(), YieldCore);
    Note(w, g_count_waits, g_count_ticks);
    return w.result == 1 ? 0 : 1;
}

__attribute__((sysv_abi)) void SlotsWait(std::uint64_t base, std::uint32_t first, std::uint32_t end) {
    std::uint32_t next = first;
    const Waited w = Wait([&] { return SlotsReady(base, next, end); }, Budget(), YieldCore);
    Note(w, g_slots_waits, g_slots_ticks);
}

void Report(double frames, double seconds) {
    const std::uint64_t count = g_count_waits.Exchange(), slots = g_slots_waits.Exchange();
    const std::uint64_t count_ticks = g_count_ticks.Exchange(), slots_ticks = g_slots_ticks.Exchange();
    const std::uint64_t yielded = g_yielded.Exchange(), yields = g_yields.Exchange();
    const double hz = static_cast<double>(runtime_tsc_frequency());
    if (!count || frames <= 0 || seconds <= 0 || hz <= 0) {
        return;
    }
    const double ms = 1e3 / hz / frames;
    std::printf("Fork-join waits: %.1f/frame, %.2f ms/frame (all in %.2f, the workers' slots %.2f); past %.0f us "
                "%.1f%% of them, yields %.1f/frame\n",
                (count + slots) / frames, (count_ticks + slots_ticks) * ms, count_ticks * ms, slots_ticks * ms,
                kSpinUs, 100.0 * yielded / double(count + slots), yields / frames);
}

} // namespace BbEngine::JobBarrier

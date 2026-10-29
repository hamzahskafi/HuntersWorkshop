// SPDX-License-Identifier: GPL-3.0-or-later
// See crash_guards.h.
#include "crash_guards.h"
#include "engine_hooks.h"
#include "engine_state.h"
#include <array>
#include <atomic>
#include <cstring>

namespace BbEngine::CrashGuards {
namespace {
std::atomic<unsigned> g_hits_dropped{0}, g_failed{0}, g_recreated{0}, g_skipped{0};
std::array<std::atomic<std::uint64_t>, kFailedSlots> g_failed_queries{}; // 0: free
std::uint64_t g_create = 0, g_device_begin = 0, g_device_end = 0;

// The first few, then at powers of two.
bool Report(unsigned n) {
    return n <= 5 || (n & (n - 1)) == 0;
}

template <typename T> T Load(std::uint64_t at) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof value);
    return value;
}
template <typename T> void Store(std::uint64_t at, T value) {
    std::memcpy(reinterpret_cast<void*>(at), &value, sizeof value);
}

bool Failed(std::uint64_t query) {
    for (const auto& slot : g_failed_queries)
        if (slot.load(std::memory_order_relaxed) == query) return true;
    return false;
}

// A creation's result: a failed one's query kept (once), a made one's forgotten.
void Note(std::uint64_t query, bool made) {
    if (made) {
        for (auto& slot : g_failed_queries) {
            std::uint64_t expected = query;
            slot.compare_exchange_strong(expected, 0, std::memory_order_relaxed);
        }
        return;
    }
    if (!Failed(query)) {
        for (auto& slot : g_failed_queries) {
            std::uint64_t expected = 0;
            if (slot.compare_exchange_strong(expected, query, std::memory_order_relaxed)) break;
        }
    }
    const unsigned n = g_failed.fetch_add(1) + 1;
    if (Report(n))
        host_log("Game fixes: a GX occlusion query could not be created (out of the device's room; %u so far): "
                 "it is created again before its begin",
                 n);
}

bool Create(std::uint64_t query, std::uint64_t kind) {
    const bool made = (Call(g_create, query, kind) & 0xff) != 0;
    Note(query, made);
    return made;
}
} // namespace

bool HitFull(std::uint64_t collector) {
    const auto count = Load<std::int32_t>(collector + kHitCount);
    return count >= kHitCap || (count > 0 && !Load<std::uint64_t>(collector + kHitData));
}

CRASH_GUARDS_ABI std::int64_t HitPush(std::uint64_t, const std::uint64_t* saved) {
    const std::uint64_t collector = saved[5];
    if (!collector || !HitFull(collector)) return 0; // the game's push
    const unsigned n = g_hits_dropped.fetch_add(1) + 1;
    if (Report(n))
        host_log("Game fixes: a physics hit collector full (%d hits): a hit dropped (%u so far)",
                 Load<std::int32_t>(collector + kHitCount), n);
    return 1; // skipped; the push returns nothing
}

void SetQueryGame(std::uint64_t create, std::uint64_t device_begin, std::uint64_t device_end) {
    g_create = create;
    g_device_begin = device_begin;
    g_device_end = device_end;
}

CRASH_GUARDS_ABI std::int64_t QueryCreated(std::uint64_t, const std::uint64_t* saved) {
    const_cast<std::uint64_t*>(saved)[-1] = Create(saved[5], saved[4]) ? 1 : 0;
    return 1;
}

CRASH_GUARDS_ABI std::uint64_t QueryBegin(std::uint64_t query, std::uint64_t context) {
    if (Load<std::uint32_t>(query + 8) != 0) return 1; // begun or ended already
    std::uint64_t handle = Load<std::uint64_t>(query);
    if (!handle) {
        const bool failed = Failed(query);
        if (failed && Create(query, 1)) handle = Load<std::uint64_t>(query);
        if (!handle) {
            const unsigned n = g_skipped.fetch_add(1) + 1;
            if (Report(n))
                host_log("Game fixes: a GX occlusion query without a handle not begun (%s; %u so far): it reads "
                         "nothing visible",
                         failed ? "its creation failed again" : "no failed creation of it seen", n);
            return 1; // its state stays 0
        }
        const unsigned n = g_recreated.fetch_add(1) + 1;
        if (Report(n)) host_log("Game fixes: a GX occlusion query created again before its begin (%u so far)", n);
    }
    Store<std::uint32_t>(query + 8, 2);
    const auto device = Load<std::uint64_t>(context + 8);
    Call(g_device_begin, device, device + kDeviceQueries, handle);
    return 1;
}

CRASH_GUARDS_ABI std::uint64_t QueryEnd(std::uint64_t query, std::uint64_t context) {
    const auto handle = Load<std::uint64_t>(query);
    if (!handle) return 1; // never begun: not ended, its state stays 0
    Store<std::uint32_t>(query + 8, 1);
    const auto device = Load<std::uint64_t>(context + 8);
    Call(g_device_end, device, device + kDeviceQueries, handle);
    return 1;
}

Counts Counted() {
    return {g_hits_dropped.load(), g_failed.load(), g_recreated.load(), g_skipped.load()};
}

void Reset() {
    g_hits_dropped = g_failed = g_recreated = g_skipped = 0;
    for (auto& slot : g_failed_queries) slot.store(0);
}
} // namespace BbEngine::CrashGuards

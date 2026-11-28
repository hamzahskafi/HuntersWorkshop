// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 18 (docs/handoffs-and-placement.md, section 3): a counter many threads add to without
// sharing a cache line. Each thread adds to a slot of its own (a cache line each, numbered at the
// thread's first use), and a read sums the slots. A shared atomic counter in a hot path is one cache
// line every thread writes, moved between cores on every add; the thread sampler found the game's
// threads on those adds (every guest memcpy, every draw record) before beta 18.
#pragma once

#include <atomic>
#include <cstdint>

namespace BbStats {

class Counter {
public:
    void Add(std::uint64_t n = 1) {
        slots_[Index()].value.fetch_add(n, std::memory_order_relaxed);
    }
    /// The sum of every thread's adds so far.
    std::uint64_t Sum() const {
        std::uint64_t sum = 0;
        for (const Slot& s : slots_) {
            sum += s.value.load(std::memory_order_relaxed);
        }
        return sum;
    }
    /// The sum, and every slot back at 0 (adds made meanwhile go to the next read).
    std::uint64_t Exchange() {
        std::uint64_t sum = 0;
        for (Slot& s : slots_) {
            sum += s.value.exchange(0, std::memory_order_relaxed);
        }
        return sum;
    }

private:
    /// More threads than slots share slots (still exact: the add is atomic).
    static constexpr unsigned kSlots = 64;
    struct alignas(64) Slot {
        std::atomic<std::uint64_t> value{0};
    };
    static unsigned Index() {
        static std::atomic<unsigned> next{0};
        thread_local const unsigned index = next.fetch_add(1, std::memory_order_relaxed) % kSlots;
        return index;
    }
    Slot slots_[kSlots];
};

} // namespace BbStats

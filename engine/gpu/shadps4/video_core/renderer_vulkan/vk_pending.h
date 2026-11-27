// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 19 (docs/stage-a-cost.md, item 3): stage A's record of guest memory the draw pipe's
// queued packets will read or write, kept smaller than the hashed tables before it (their notes and
// lookups were about 9.5% of the GPU command thread). Stage A only. Plain code:
// tests/test_pending.cpp.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace Vulkan {

/// The bytes the constant engine's dumps (DumpConstRam) write, page-aligned and merged. The read
/// record answers one question, for those dumps: so a read is noted only where it touches them.
struct DumpPages {
    using Addr = std::uint64_t;
    static constexpr std::size_t Max = 64; ///< more ranges than this: every read counts
    std::vector<std::pair<Addr, Addr>> ranges; ///< sorted, disjoint, apart [lo, hi)
    Addr lo = ~Addr{0}, hi = 0;               ///< their span (a quick miss)
    bool overflow = false;

    [[nodiscard]] bool Touches(Addr address, std::uint64_t size) const {
        if (overflow) {
            return true;
        }
        const Addr end = address + size;
        if (end <= lo || address >= hi) {
            return false;
        }
        for (const auto& [a, b] : ranges) {
            if (address < b && end > a) {
                return true;
            }
        }
        return false;
    }
    /// Whole within the pages dumps have written.
    [[nodiscard]] bool Covers(Addr address, std::uint64_t size) const {
        if (overflow) {
            return true;
        }
        for (const auto& [a, b] : ranges) {
            if (address >= a && address + size <= b) {
                return true;
            }
        }
        return false;
    }
    void Add(Addr address, std::uint64_t size) {
        Addr a = address & ~Addr{4095};
        Addr b = (address + size + 4095) & ~Addr{4095};
        std::vector<std::pair<Addr, Addr>> merged;
        merged.reserve(ranges.size() + 1);
        for (const auto& r : ranges) {
            if (r.second < a || r.first > b) {
                merged.push_back(r); // apart; touching ranges merge
            } else {
                a = std::min(a, r.first);
                b = std::max(b, r.second);
            }
        }
        merged.emplace_back(a, b);
        std::sort(merged.begin(), merged.end());
        ranges = std::move(merged);
        lo = ranges.front().first;
        hi = ranges.back().second;
        overflow = ranges.size() > Max;
    }
};

/// The GPU writes the draw pipe's stage B hasn't reached, oldest first (packet numbers rise): few
/// at any time, as stage B keeps up. Full, a write goes to the caller's fallback.
class PendingWriteList {
public:
    using Addr = std::uint64_t;
    static constexpr std::uint32_t Capacity = 64;

    /// Drops the writes stage B has reached (`reached(packet)`).
    template <typename Reached>
    void Prune(Reached&& reached) {
        while (count && reached(entries[first].packet)) {
            first = (first + 1) % Capacity;
            --count;
        }
    }
    /// False when full (the caller keeps the write elsewhere). The same range as the newest entry
    /// only moves that entry's packet up.
    bool Note(Addr address, std::uint64_t size, std::uint64_t packet) {
        if (count) {
            Entry& last = entries[(first + count - 1) % Capacity];
            if (last.lo == address && last.hi == address + size) {
                last.packet = std::max(last.packet, packet);
                return true;
            }
        }
        if (count == Capacity) {
            return false;
        }
        entries[(first + count) % Capacity] = {address, address + size, packet};
        ++count;
        return true;
    }
    [[nodiscard]] bool Overlaps(Addr address, std::uint64_t size) const {
        const Addr end = address + size;
        for (std::uint32_t i = 0; i < count; ++i) {
            const Entry& e = entries[(first + i) % Capacity];
            if (address < e.hi && end > e.lo) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] std::uint32_t Size() const {
        return count;
    }

private:
    struct Entry {
        Addr lo, hi;
        std::uint64_t packet;
    };
    std::array<Entry, Capacity> entries{};
    std::uint32_t first = 0, count = 0;
};

} // namespace Vulkan

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

/// The GPU writes the draw pipe's stage B hasn't reached, oldest first (packet numbers rise). Full,
/// a write goes to the caller's fallback. 0.4.6 beta 20 (docs/chain-waits.md, item 5): 1,024 entries
/// (the 64 of beta 19 filled 31-34 thousand times a second in the tester's profile), and a counting
/// filter over 64 KB pages of the live entries, so most lookups (every constant binding) read one or
/// two counters and only a hit scans the list. A write of a range among the newest few entries
/// moves that entry to the end (draws writing the same buffers in turn keep one entry each), which
/// keeps the list in packet order, so pruning from the front always moves.
class PendingWriteList {
public:
    using Addr = std::uint64_t;
    static constexpr std::uint32_t Capacity = 1024;
    static constexpr std::uint32_t kPageShift = 16;  ///< 64 KB
    static constexpr std::uint32_t kFilterBits = 12; ///< 4,096 counters
    static constexpr std::uint64_t kMaxPages = 32;   ///< wider writes are "wide": checked one by one
    static constexpr std::uint32_t kRecent = 8;      ///< how far back a repeated range is found
    /// Engine id 138: the wide writes live are checked one by one (a storage buffer bound whole,
    /// a DMA or copy into a large range); up to this many, the filter still answers for the rest.
    /// In 136 one live wide write sent every lookup to the scan (the tester's run: the filter
    /// answered 8-27% of lookups, and the draw step took about 0.1 us a draw more than in 134).
    /// 0.4.6 beta 21: 64 (16 before; 138's busy view had up to 17 live). A lookup checks the ones
    /// live, not the limit, so the limit costs nothing until they are that many.
    static constexpr std::uint32_t kMaxWide = 64;

    /// BB_PENDING_FILTER=0 (this beta only): every lookup scans the list.
    bool use_filter = true;
    /// Statistics: lookups answered without the scan (by the filter, after the wide ones), those
    /// that scanned because a page held a write or because more than kMaxWide wide writes were
    /// live, and the entries those scans went through.
    std::uint64_t filtered = 0, scanned = 0, scanned_wide = 0, scanned_entries = 0;

    /// Drops the writes stage B has reached (`reached(packet)`), and the moved ones' old places.
    template <typename Reached>
    void Prune(Reached&& reached) {
        while (count) {
            Entry& e = entries[first];
            if (e.live) {
                if (!reached(e.packet)) {
                    break;
                }
                Forget(e);
            }
            first = (first + 1) & (Capacity - 1);
            --count;
        }
    }
    /// False when full (the caller keeps the write elsewhere).
    bool Note(Addr address, std::uint64_t size, std::uint64_t packet) {
        const Addr end = address + size;
        const std::uint32_t recent = count < kRecent ? count : kRecent;
        for (std::uint32_t i = 0; i < recent; ++i) {
            Entry& e = entries[(first + count - 1 - i) & (Capacity - 1)];
            if (e.live && e.lo == address && e.hi == end) {
                if (i == 0) {
                    e.packet = std::max(e.packet, packet); // already the newest
                    return true;
                }
                if (count == Capacity) {
                    return false;
                }
                Forget(e); // its place stays, dead, until pruning passes it
                break;
            }
        }
        if (count == Capacity) {
            return false;
        }
        Entry& e = entries[(first + count) & (Capacity - 1)];
        e = {address, end, packet, true, false};
        Remember(e);
        ++count;
        return true;
    }
    [[nodiscard]] bool Overlaps(Addr address, std::uint64_t size) {
        if (live == 0) {
            ++filtered;
            return false;
        }
        const Addr end = address + size;
        if (use_filter && !wide_lost) {
            for (std::uint32_t i = 0; i < wide; ++i) {
                const Entry& e = entries[wide_at[i]];
                if (address < e.hi && end > e.lo) {
                    return true;
                }
            }
            if (live == wide) {
                ++filtered;
                return false;
            }
            bool hit = false;
            for (Addr page = address >> kPageShift; page <= (end - 1) >> kPageShift; ++page) {
                if (filter[Slot(page)]) {
                    hit = true;
                    break;
                }
            }
            if (!hit) {
                ++filtered;
                return false;
            }
        } else if (use_filter) {
            ++scanned_wide;
        }
        ++scanned;
        scanned_entries += count;
        for (std::uint32_t i = 0; i < count; ++i) {
            const Entry& e = entries[(first + i) & (Capacity - 1)];
            if (e.live && address < e.hi && end > e.lo) {
                return true;
            }
        }
        return false;
    }
    /// Live entries.
    [[nodiscard]] std::uint32_t Size() const {
        return live;
    }

private:
    struct Entry {
        Addr lo, hi;
        std::uint64_t packet;
        bool live;
        bool wide;
    };
    static std::uint32_t Slot(Addr page) {
        return static_cast<std::uint32_t>((page * 0x9E3779B97F4A7C15ull) >> (64 - kFilterBits));
    }
    template <int Delta>
    void Count(const Entry& e) {
        for (Addr page = e.lo >> kPageShift; page <= (e.hi - 1) >> kPageShift; ++page) {
            filter[Slot(page)] += Delta;
        }
    }
    void Remember(Entry& e) {
        ++live;
        e.wide = ((e.hi - 1) >> kPageShift) - (e.lo >> kPageShift) + 1 > kMaxPages;
        if (!e.wide) {
            Count<1>(e);
            return;
        }
        if (wide < kMaxWide && !wide_lost) {
            wide_at[wide] = Index(e);
        } else {
            wide_lost = true; // more than kMaxWide: every lookup scans until they are fewer
        }
        ++wide;
    }
    void Forget(Entry& e) {
        e.live = false;
        --live;
        if (!e.wide) {
            Count<-1>(e);
            return;
        }
        --wide;
        if (wide_lost) {
            if (wide <= kMaxWide) {
                RebuildWide();
            }
            return;
        }
        const std::uint32_t at = Index(e);
        for (std::uint32_t i = 0; i <= wide; ++i) {
            if (wide_at[i] == at) {
                wide_at[i] = wide_at[wide];
                break;
            }
        }
    }
    std::uint32_t Index(const Entry& e) const {
        return static_cast<std::uint32_t>(&e - entries.data());
    }
    /// The wide entries' places again, once few enough are live.
    void RebuildWide() {
        std::uint32_t n = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t at = (first + i) & (Capacity - 1);
            if (entries[at].live && entries[at].wide) {
                wide_at[n++] = at;
            }
        }
        wide = n;
        wide_lost = false;
    }

    std::array<Entry, Capacity> entries{};
    std::array<std::uint16_t, std::size_t{1} << kFilterBits> filter{};
    std::array<std::uint32_t, kMaxWide> wide_at{}; ///< the live wide entries' places (unless lost)
    std::uint32_t first = 0, count = 0, live = 0, wide = 0;
    bool wide_lost = false;

public:
    /// Live wide writes now (statistics).
    [[nodiscard]] std::uint32_t Wide() const {
        return wide;
    }
};

} // namespace Vulkan

// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 19 (vk_pending.h): the dumps' page set and the list of GPU writes stage B hasn't
// reached, against a plain model.
#include "video_core/renderer_vulkan/vk_pending.h"

#include <cstdio>
#include <random>
#include <vector>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}
} // namespace

int main() {
    using Vulkan::DumpPages;
    using Vulkan::PendingWriteList;
    // The page set: what is added is covered and touched, page by page; nothing else.
    DumpPages pages;
    check(!pages.Touches(0x10000, 64) && !pages.Covers(0x10000, 64), "an empty set touches nothing");
    pages.Add(0x100010, 0x20);
    check(pages.Covers(0x100000, 4096) && !pages.Covers(0x100000, 4097), "a dump covers its page");
    check(pages.Touches(0x0fffff, 2) && !pages.Touches(0x0ff000, 0x1000), "touching is by bytes");
    pages.Add(0x102000, 0x1000); // apart from the first: two ranges
    check(pages.ranges.size() == 2, "apart ranges stay apart");
    check(!pages.Touches(0x101000, 0x1000), "the gap between them is not touched");
    pages.Add(0x101800, 0x10); // fills the gap: one range
    check(pages.ranges.size() == 1 && pages.Covers(0x100000, 0x3000), "touching ranges merge");
    std::mt19937_64 rng{7};
    DumpPages many;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> added;
    for (int i = 0; i < 40; ++i) {
        const std::uint64_t at = 0x200000000ull + (rng() % 4096) * 4096 * 3;
        many.Add(at, 1 + rng() % 9000);
        added.emplace_back(at, 0);
    }
    for (int i = 0; i < 20000; ++i) {
        const std::uint64_t at = 0x200000000ull + rng() % (4096ull * 4096 * 3 + 50000);
        const std::uint64_t size = 1 + rng() % 20000;
        bool model = false;
        for (const auto& [a, b] : many.ranges) {
            model = model || (at < b && at + size > a);
        }
        if (many.Touches(at, size) != model) {
            check(false, "Touches agrees with the ranges");
            break;
        }
    }
    DumpPages full;
    for (std::uint64_t i = 0; i <= DumpPages::Max; ++i) {
        full.Add(0x300000000ull + i * 0x10000, 16);
    }
    check(full.overflow && full.Touches(1, 1) && full.Covers(1, 1), "too many ranges: everything counts");

    // The write list: overlaps while not reached, gone once reached, full at its capacity.
    PendingWriteList list;
    std::uint64_t reached = 0;
    const auto is_reached = [&](std::uint64_t p) { return p <= reached; };
    check(list.Note(0x1000, 0x100, 5) && list.Overlaps(0x10ff, 1) && !list.Overlaps(0x1100, 4),
          "a noted write overlaps by bytes");
    check(list.Note(0x1000, 0x100, 9) && list.Size() == 1, "the same range again moves its packet");
    reached = 5;
    list.Prune(is_reached);
    check(list.Overlaps(0x1000, 1), "not pruned before its newest packet is reached");
    reached = 9;
    list.Prune(is_reached);
    check(list.Size() == 0 && !list.Overlaps(0x1000, 1), "pruned once reached");
    for (std::uint32_t i = 0; i < PendingWriteList::Capacity; ++i) {
        list.Note(0x10000 + i * 0x100, 0x10, 100 + i);
    }
    check(!list.Note(0x900000, 16, 500), "full: the caller keeps the write");
    reached = 100 + 9;
    list.Prune(is_reached);
    check(list.Size() == PendingWriteList::Capacity - 10 && list.Note(0x900000, 16, 501),
          "pruning makes room, oldest first");
    check(!list.Overlaps(0x10000, 0x10) && list.Overlaps(0x10000 + 10 * 0x100, 1),
          "the reached ones are gone, the rest stay");
    if (g_fail) {
        std::printf("pending-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("pending-test: ok\n");
    return 0;
}

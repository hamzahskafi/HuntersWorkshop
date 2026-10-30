// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.4 beta 16: which GPU write last touched each 4 KiB page of tracked memory,
// for object motion. A page stays GPU-modified (MemoryTracker) until the CPU writes it again, so a
// small skeleton's bone palette the GPU wrote once counts as GPU-written for good, and was stored
// every frame though it may never change again: facing the lamps of the dense area, 357 of 360
// such draws a frame (3.7 M of 6.5 M vertices) were not written by the copy shader this frame or
// the last. Every GPU write of tracked memory marks its pages in MemoryTracker::ForEachUploadRange,
// where this numbers it: a palette whose number did not change since the last frame did not
// change (Rasterizer: gated like a palette the CPU holds).
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace VideoCore {

class GpuWriteStamps {
public:
    static GpuWriteStamps& Get() {
        static GpuWriteStamps instance;
        return instance;
    }

    /// Writes are numbered while object motion looks at them (ObjectMotion::OnFrameStart). Turned
    /// on, earlier numbers are dropped: a page written while nobody looked reads as not written
    /// since, and what was written then predates the history that starts now.
    void SetWanted(bool on) {
        if (on && !wanted.load(std::memory_order_relaxed)) {
            std::scoped_lock lock{mutex};
            chunks.clear();
        }
        wanted.store(on, std::memory_order_relaxed);
    }

    /// A GPU write of the range (MemoryTracker::ForEachUploadRange, as its pages become
    /// GPU-modified).
    void Note(uint64_t addr, uint64_t size) {
        if (size == 0 || !wanted.load(std::memory_order_relaxed)) {
            return;
        }
        std::scoped_lock lock{mutex};
        const uint64_t stamp = ++sequence;
        for (uint64_t page = addr >> PageShift; page <= (addr + size - 1) >> PageShift; ++page) {
            auto& chunk = chunks[page >> ChunkShift];
            if (!chunk) {
                chunk = std::make_unique<Chunk>(); // zeros: not written since numbering began
            }
            (*chunk)[page & (ChunkPages - 1)] = stamp;
        }
    }

    /// The number of the newest write over the range, 0 when none since numbering began.
    [[nodiscard]] uint64_t Last(uint64_t addr, uint64_t size) {
        if (size == 0) {
            return 0;
        }
        std::scoped_lock lock{mutex};
        uint64_t last = 0;
        for (uint64_t page = addr >> PageShift; page <= (addr + size - 1) >> PageShift; ++page) {
            const auto it = chunks.find(page >> ChunkShift);
            if (it != chunks.end()) {
                last = std::max(last, (*it->second)[page & (ChunkPages - 1)]);
            }
        }
        return last;
    }

private:
    static constexpr uint64_t PageShift = 12;
    static constexpr uint64_t ChunkShift = 10; ///< 1024 pages: 4 MiB of memory, 8 KiB of numbers
    static constexpr uint64_t ChunkPages = uint64_t{1} << ChunkShift;
    using Chunk = std::array<uint64_t, ChunkPages>;
    std::mutex mutex;
    std::unordered_map<uint64_t, std::unique_ptr<Chunk>> chunks;
    uint64_t sequence = 0;
    std::atomic<bool> wanted{false};
};

} // namespace VideoCore

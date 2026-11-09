// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include "common/alignment.h"
#include "common/types.h"
#include "video_core/buffer_cache//region_definitions.h"

namespace Vulkan {
class Rasterizer;
}

namespace VideoCore {

struct SignalImpl;

class PageManager {
    // PAGE_SIZE and PAGE_BITS conflicts with machine/param.h definitions on freebsd!
    // Use the same page size as the tracker.
    static constexpr size_t PM_PAGE_BITS = TRACKER_PAGE_BITS;
    static constexpr size_t PM_PAGE_SIZE = TRACKER_BYTES_PER_PAGE;

    // Keep the lock granularity the same as region granularity. (since each regions has
    // itself a lock)
    static constexpr size_t PAGES_PER_LOCK = NUM_PAGES_PER_REGION;

public:
    explicit PageManager(Vulkan::Rasterizer* rasterizer);
    ~PageManager();

    /// bbport: the guest code behind write faults on tracked pages over the last window (frame
    /// stats): which game functions write GPU memory, to replace tracking by explicit hooks.
    static void ReportFaultSites();
    /// bbport: the write fault being handled on this thread hit an image (diagnostics: which guest
    /// code writes textures).
    static void NoteImageFault(VAddr image_address, u64 image_size, u32 width, u32 height,
                               u32 format, u32 tiling);
    /// 0.4.6 beta 13: what watched the page of the write fault this thread handles (bits: 1 the GPU's
    /// data, 2 the buffer cache's uploaded copy, 4 an image, 8 none), for its fault site's line.
    static void NoteFaultWatch(u32 watched);

    /// Register a range of mapped gpu memory.
    void OnGpuMap(VAddr address, size_t size);

    /// Unregister a range of gpu memory that was unmapped.
    void OnGpuUnmap(VAddr address, size_t size);

    /// Updates watches in the pages touching the specified region.
    template <bool track>
    void UpdatePageWatchers(VAddr addr, u64 size) const;

    /// 0.4.6 beta 4: a write watcher more on the pages of each range (sorted by address), one
    /// protection change per run of pages that become watched.
    void WatchRanges(std::span<const std::pair<VAddr, u64>> ranges) const;

    /// Updates watches in the pages touching the specified region using a mask.
    template <bool track, bool is_read = false>
    void UpdatePageWatchersForRegion(VAddr base_addr, RegionBits& mask) const;

    /// Returns page aligned address.
    static constexpr VAddr GetPageAddr(VAddr addr) {
        return Common::AlignDown(addr, PM_PAGE_SIZE);
    }

    /// Returns address of the next page.
    static constexpr VAddr GetNextPageAddr(VAddr addr) {
        return Common::AlignUp(addr + 1, PM_PAGE_SIZE);
    }

private:
    friend struct SignalImpl;
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace VideoCore

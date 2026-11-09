// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <cstdlib>

#include "bbport_toggles.h"
#include "common/div_ceil.h"
#include "common/logging/log.h"
#include "core/emulator_settings.h"

#ifdef __unix__
#include "common/adaptive_mutex.h"
#else
#include "common/spin_lock.h"
#endif
#include "common/debug.h"
#include "common/types.h"
#include "video_core/buffer_cache/region_definitions.h"
#include "video_core/page_manager.h"

namespace VideoCore {

#ifdef PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
using LockType = Common::AdaptiveMutex;
#else
using LockType = Common::SpinLock;
#endif

/**
 * Allows tracking CPU and GPU modification of pages in a contigious 16MB virtual address region.
 * Information is stored in bitsets for spacial locality and fast update of single pages.
 */
class RegionManager {
public:
    explicit RegionManager(PageManager* tracker_, VAddr cpu_addr_)
        : tracker{tracker_}, cpu_addr{cpu_addr_} {
        cpu.Fill();
        gpu.Clear();
        writeable.Fill();
        readable.Fill();
        calls.Clear();
    }
    explicit RegionManager() = default;

    void SetCpuAddress(VAddr new_cpu_addr) {
        cpu_addr = new_cpu_addr;
    }

    VAddr GetCpuAddr() const {
        return cpu_addr;
    }

    static constexpr size_t SanitizeAddress(size_t address) {
        return static_cast<size_t>(std::max<s64>(static_cast<s64>(address), 0LL));
    }

    template <Type type>
    RegionBits& GetRegionBits() noexcept {
        if constexpr (type == Type::CPU) {
            return cpu;
        } else if constexpr (type == Type::GPU) {
            return gpu;
        }
    }

    template <Type type>
    const RegionBits& GetRegionBits() const noexcept {
        if constexpr (type == Type::CPU) {
            return cpu;
        } else if constexpr (type == Type::GPU) {
            return gpu;
        }
    }

    /**
     * Change the state of a range of pages
     *
     * @param dirty_addr    Base address to mark or unmark as modified
     * @param size          Size in bytes to mark or unmark as modified
     */
    /// 0.4.6 beta 5: `count` false for the cache's own marking (its VRAM copy left, the pages handed
    /// back to the game): not a write of the game's, so not counted as its write faults or as a
    /// rewrite of pre-uploaded data (beta 4 logs: those were most of the "write faults" at loads).
    template <Type type, bool enable, bool count = true>
    void ChangeRegionState(u64 dirty_addr, u64 size) noexcept(type == Type::GPU) {
        RENDERER_TRACE;
        const size_t offset = dirty_addr - cpu_addr;
        const size_t start_page = SanitizeAddress(offset) / TRACKER_BYTES_PER_PAGE;
        const size_t end_page =
            Common::DivCeil(SanitizeAddress(offset + size), TRACKER_BYTES_PER_PAGE);
        if (start_page >= NUM_PAGES_PER_REGION || end_page <= start_page) {
            return;
        }

        RegionBits& bits = GetRegionBits<type>();
        if constexpr (type == Type::CPU && enable && count) {
            CountWriteFaults(start_page, end_page);
            last_cpu_write_frame = BbStats::frame_number.load(std::memory_order_relaxed);
            NotePreuploadChurn();
        }
        if constexpr (enable) {
            bits.SetRange(start_page, end_page);
        } else {
            bits.UnsetRange(start_page, end_page);
        }
        if constexpr (type == Type::CPU) {
            UpdateProtection<!enable, false>();
        } else {
            if (EmulatorSettings.GetReadbacksMode() == GpuReadbacksMode::Precise) {
                UpdateProtection<enable, true>();
            }
            // 0.4.6 beta 12: a page the GPU wrote leaves the calls' writable set (and comes back).
            if (calls.Any()) {
                ApplyWriteProtection();
            }
        }
    }

    /// bbport: after a guest write fault, also unprotects the other pages of the window that
    /// are protected for CPU writes and hold no GPU-modified data (see MarkFaultWindow).
    void ExtendWriteFault(VAddr window_addr, u64 size) {
        const size_t offset = window_addr - cpu_addr;
        const size_t start_page = SanitizeAddress(offset) / TRACKER_BYTES_PER_PAGE;
        const size_t end_page =
            Common::DivCeil(SanitizeAddress(offset + size), TRACKER_BYTES_PER_PAGE);
        if (start_page >= NUM_PAGES_PER_REGION || end_page <= start_page) {
            return;
        }
        RegionBits add(~(gpu | writeable), start_page, end_page);
        if (add.None()) {
            return;
        }
        cpu |= add;
        UpdateProtection<false, false>();
    }

    /**
     * Loop over each page in the given range, turn off those bits and notify the tracker if
     * needed. Call the given function on each turned off range.
     *
     * @param query_cpu_range Base CPU address to loop over
     * @param size            Size in bytes of the CPU range to loop over
     * @param func            Function to call for each turned off region
     */
    template <Type type, bool clear>
    void ForEachModifiedRange(VAddr query_cpu_range, s64 size, auto&& func) {
        RENDERER_TRACE;
        const size_t offset = query_cpu_range - cpu_addr;
        const size_t start_page = SanitizeAddress(offset) / TRACKER_BYTES_PER_PAGE;
        const size_t end_page =
            Common::DivCeil(SanitizeAddress(offset + size), TRACKER_BYTES_PER_PAGE);
        if (start_page >= NUM_PAGES_PER_REGION || end_page <= start_page) {
            return;
        }

        RegionBits& bits = GetRegionBits<type>();
        if constexpr (type == Type::CPU) {
            CountUses(start_page, end_page);
        }
        // bbport: nothing modified in the range means nothing to clear or re-protect (CPU bits
        // always match `writeable` after a change); this runs for every buffer binding, so
        // it is checked before building the range mask.
        if (type == Type::CPU && !bits.AnyInRange(start_page, end_page) &&
            !BbToggle::Disabled(BbToggle::PageTrackingEarlyExit)) {
            return;
        }
        RegionBits mask(bits, start_page, end_page);

        if constexpr (clear) {
            bits.UnsetRange(start_page, end_page);
            if constexpr (type == Type::CPU) {
                KeepHotPagesModified(start_page, end_page);
                UpdateProtection<true, false>();
            } else {
                if (EmulatorSettings.GetReadbacksMode() != GpuReadbacksMode::Disabled) {
                    UpdateProtection<false, true>();
                }
                if (calls.Any()) {
                    ApplyWriteProtection(); // 0.4.6 beta 12: back to writable once the GPU's data is down
                }
            }
        }

        for (const auto& [start, end] : mask) {
            func(cpu_addr + start * TRACKER_BYTES_PER_PAGE, (end - start) * TRACKER_BYTES_PER_PAGE);
        }
    }

    /**
     * Returns true when a region has been modified
     *
     * @param offset Offset in bytes from the start of the buffer
     * @param size   Size in bytes of the region to query for modifications
     */
    template <Type type>
    [[nodiscard]] bool IsRegionModified(u64 offset, u64 size) noexcept {
        RENDERER_TRACE;
        const size_t start_page = SanitizeAddress(offset) / TRACKER_BYTES_PER_PAGE;
        const size_t end_page =
            Common::DivCeil(SanitizeAddress(offset + size), TRACKER_BYTES_PER_PAGE);
        if (start_page >= NUM_PAGES_PER_REGION || end_page <= start_page) {
            return false;
        }

        const RegionBits& bits = GetRegionBits<type>();
        return bits.AnyInRange(start_page, end_page);
    }

    /// bbport (pre-upload): whether some page is CPU-modified but not GPU-modified.
    [[nodiscard]] bool HasCpuOnlyPages() const noexcept {
        return (cpu & ~gpu).Any();
    }
    /// bbport (pre-upload): the frame of the newest guest write seen in this region.
    u32 last_cpu_write_frame = 0;
    /// bbport (pre-upload): when it was last pre-uploaded, and no pre-upload before this frame:
    /// a region the game writes again soon after a pre-upload backs off, longer each time.
    u32 preupload_frame = 0;
    u32 preupload_backoff_until = 0;
    u8 preupload_strikes = 0;
    u32 preupload_count = 0;
    void NotePreuploadChurn() noexcept {
        constexpr u32 ChurnFrames = 600; // ~10 s: regions the game rewrites every few seconds
        if (preupload_frame == 0 || last_cpu_write_frame - preupload_frame > ChurnFrames) {
            return;
        }
        // One quick rewrite may be the game loading new data there (worth pre-uploading); from
        // the second on it is data it rewrites now and then.
        preupload_strikes = std::min<u8>(preupload_strikes + 1, 7);
        if (preupload_strikes >= 2) {
            preupload_backoff_until = last_cpu_write_frame + (240u << (preupload_strikes - 1));
        }
        preupload_frame = 0;
    }

    /// 0.4.6 beta 12 (BB_WATCH_GAME_BUFFERS): the pages of a live GX buffer are watched by the game's
    /// calls (`calls`), not by protection: writable after an upload (WriteTarget: cpu | (calls & ~gpu)),
    /// but for pages the GPU wrote. Only pages wholly inside the buffer; one it shares with other memory
    /// stays watched. The caller holds `lock`.
    ///
    /// Marks [offset, offset + size) to upload, but for pages the GPU wrote (they keep their handling:
    /// a CPU write there faults, the GPU's data is read back first). Never counted as the game's write
    /// faults. Safe on its own: a page to upload only costs an upload at its next use.
    void MarkToUploadButGpu(u64 offset, u64 size) {
        const auto [start, end] = Span(offset, size, false);
        if (start >= end) {
            return;
        }
        RegionBits range{};
        range.SetRange(start, end);
        cpu |= range & ~gpu;
        ApplyWriteProtection();
    }
    /// 0.4.6 beta 13: the engine stored at [offset, offset + size) through the backing view (a label),
    /// which never faults: its pages holding an uploaded copy (neither to upload nor the GPU's) are
    /// marked to upload, as a fault would have marked them. Returns the pages marked and the pages the
    /// GPU wrote there (left as they are: an upload would put older bytes over the GPU's).
    std::pair<u32, u32> MarkStoreUploaded(u64 offset, u64 size) {
        const auto [start, end] = Span(offset, size, false);
        if (start >= end) {
            return {0, 0};
        }
        RegionBits range{};
        range.SetRange(start, end);
        const RegionBits uploaded = range & ~cpu & ~gpu;
        const u32 gpu_pages = static_cast<u32>(CountPages(range & gpu));
        if (!uploaded.Any()) {
            return {0, gpu_pages};
        }
        cpu |= uploaded;
        ApplyWriteProtection();
        return {static_cast<u32>(CountPages(uploaded)), gpu_pages};
    }
    /// 0.4.6 beta 13 (diagnostics, the write fault breakdown): this cache's watch on the page at
    /// `offset`: 2 the GPU's data (a write faults to read it back first), 1 an uploaded copy, 0 none (a
    /// page to upload is writable). Read without the lock: the faulting thread may hold it.
    u8 PageWatch(u64 offset) const noexcept {
        const size_t page = std::min<size_t>(SanitizeAddress(offset) / TRACKER_BYTES_PER_PAGE,
                                             NUM_PAGES_PER_REGION - 1);
        if (gpu.Get(page)) {
            return 2;
        }
        return cpu.Get(page) ? 0 : 1;
    }
    /// A GX buffer made at [offset, offset + size): marked to upload (MarkToUploadButGpu), and its inside
    /// pages kept writable. Made with its data, the GPU's older data on its inside pages is dropped (the
    /// creation's copy overwrote it). Returns how many pages became `calls`.
    s64 CallsCreate(u64 offset, u64 size, bool with_data) {
        const auto [start, end] = Span(offset, size, false);
        if (start >= end) {
            return 0;
        }
        RegionBits range{};
        range.SetRange(start, end);
        RegionBits inner{};
        if (const auto [inner_start, inner_end] = Span(offset, size, true); inner_start < inner_end) {
            inner.SetRange(inner_start, inner_end);
        }
        if (with_data && (gpu & inner).Any()) {
            gpu &= ~inner;
            if (EmulatorSettings.GetReadbacksMode() != GpuReadbacksMode::Disabled) {
                UpdateProtection<false, true>();
            }
        }
        cpu |= range & ~gpu;
        const s64 added = CountPages(inner & ~calls);
        calls |= inner;
        ApplyWriteProtection();
        return added;
    }
    /// The buffer no longer owns [offset, offset + size) (released, unmapped, or something newer made
    /// over it): its pages (`all`: every page in range, else the inside ones) leave `calls`, and the
    /// range is marked to upload but for pages the GPU wrote. Returns how many pages left `calls`.
    s64 CallsRelease(u64 offset, u64 size, bool all) {
        const auto [start, end] = Span(offset, size, false);
        if (start >= end) {
            return 0;
        }
        RegionBits range{};
        range.SetRange(start, end);
        RegionBits leave{};
        if (all) {
            leave = range;
        } else if (const auto [inner_start, inner_end] = Span(offset, size, true); inner_start < inner_end) {
            leave.SetRange(inner_start, inner_end);
        }
        const s64 removed = CountPages(calls & leave);
        calls &= ~leave;
        cpu |= range & ~gpu;
        ApplyWriteProtection();
        return removed;
    }

    LockType lock;

private:
    /// 0.4.6 beta 12: the pages [offset, offset + size) touches (`inside`: only those wholly in it).
    static std::pair<size_t, size_t> Span(u64 offset, u64 size, bool inside) {
        const u64 first = SanitizeAddress(offset);
        const u64 last = SanitizeAddress(offset + size);
        size_t start = inside ? Common::DivCeil(first, TRACKER_BYTES_PER_PAGE) : first / TRACKER_BYTES_PER_PAGE;
        size_t end = inside ? last / TRACKER_BYTES_PER_PAGE : Common::DivCeil(last, TRACKER_BYTES_PER_PAGE);
        end = std::min<size_t>(end, NUM_PAGES_PER_REGION);
        start = std::min(start, end);
        return {start, end};
    }
    static s64 CountPages(const RegionBits& bits) {
        s64 pages = 0;
        for (const auto& [first, last] : bits) {
            pages += static_cast<s64>(last - first);
        }
        return pages;
    }
    /// 0.4.6 beta 12: the writable pages: those to upload, and the calls' that hold no GPU data (a CPU
    /// write on a page the GPU wrote still faults, its data read back first).
    RegionBits WriteTarget() const {
        return cpu | (calls & ~gpu);
    }
    /// 0.4.6 beta 12: protection brought to WriteTarget both ways (pages opened and closed).
    void ApplyWriteProtection() {
        const RegionBits target = WriteTarget();
        RegionBits open = target & ~writeable;
        RegionBits close = writeable & ~target;
        writeable = target;
        if (open.Any()) {
            tracker->UpdatePageWatchersForRegion<false, false>(cpu_addr, open);
        }
        if (close.Any()) {
            tracker->UpdatePageWatchersForRegion<true, false>(cpu_addr, close);
        }
    }

    /**
     * Notify tracker about changes in the CPU tracking state of a word in the buffer
     *
     * @param word_index   Index to the word to notify to the tracker
     * @param current_bits Current state of the word
     * @param new_bits     New state of the word
     *
     * @tparam track True when the tracker should start tracking the new pages
     */
    template <bool track, bool is_read>
    void UpdateProtection() {
        RENDERER_TRACE;
        // 0.4.6 beta 12: pages the game's calls watch stay writable (`calls`), unless the GPU wrote them.
        RegionBits mask = is_read ? (~gpu ^ readable) : (WriteTarget() ^ writeable);
        if (mask.None()) {
            return;
        }
        if constexpr (is_read) {
            readable = ~gpu;
        } else {
            writeable = WriteTarget();
        }
        tracker->UpdatePageWatchersForRegion<track, is_read>(cpu_addr, mask);
    }

    // bbport: pages the guest writes again and again (per-frame constants, skinning output)
    // cost a protection fault in the writing thread plus an mprotect with TLB shootdowns on
    // every upload. A hot page stays writable and counts as always CPU modified, so it is
    // uploaded on every use instead. The set is rebuilt every HotPeriod.
    //
    // A page becomes hot only when the guest wrote it before nearly every use - at least
    // HotMinFaults faults in a HotWindow, and no more than HotUsesPerFault uses per fault.
    // Re-uploading it on each use then copies what the faults made it copy anyway, without the
    // fault and the re-protect; a page written once and read many times stays protected.
    // HotUsesPerFault was 2 until 0.9.37 beta 2: per-frame constants read by several draws never
    // qualified (14-62 hot pages at 2800 write faults/s), and each fault cost stage B a re-protect
    // (0.35 us per packet, ~7% of it); the uploads run on the copy threads. At most HotMaxPages
    // pages are hot at once (8 MiB), so a scene full of them cannot grow the set without bound
    // (the earlier rule, hot after two faults, grew it to ~14k pages in Hunter's Nightmare and
    // cost 33 FPS; removed in 0.4.4 beta 28). On by default since 0.4.4 beta 28 (the launcher's
    // since Lantern 0.9.37 beta 4); BB_HOT_PAGES=0: off.
    static bool HotPagesEnabled() {
        static const bool on = [] {
            const char* env = std::getenv("BB_HOT_PAGES");
            return !env || env[0] != '0';
        }();
        return on && !BbToggle::Disabled(BbToggle::HotPages);
    }
    static constexpr u8 HotMinFaults = 16;
    static constexpr u32 HotUsesPerFault = 8;
    static constexpr auto HotWindow = std::chrono::seconds(2);
    static constexpr s64 HotMaxPages = 2048;

    void CountUses(size_t start_page, size_t end_page) {
        if (!HotPagesEnabled()) {
            return;
        }
        for (size_t page = start_page; page < end_page; ++page) {
            if (uses[page] != 0xffff) {
                ++uses[page];
            }
        }
    }
    // A short period re-protects thousands of pages at once and each faults again before it is
    // hot, a fault storm (a 2 s period gave ~20k faults/s and frame spikes).
    static constexpr auto HotPeriod = std::chrono::seconds(20);

    void CountWriteFaults(size_t start_page, size_t end_page) {
        const bool hot_enabled = HotPagesEnabled();
        for (size_t page = start_page; page < end_page; ++page) {
            if (writeable.Get(page)) {
                continue; // not protected: no fault
            }
            BbStats::tracker_faults.fetch_add(1, std::memory_order_relaxed);
            if (!hot_enabled) {
                continue;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now - window_start > HotWindow) {
                write_faults.fill(0);
                uses.fill(0);
                window_start = now;
            }
            if (write_faults[page] != 0xff) {
                ++write_faults[page];
            }
            if (write_faults[page] < HotMinFaults || hot.Get(page) ||
                uses[page] > HotUsesPerFault * write_faults[page] ||
                BbStats::hot_pages.load(std::memory_order_relaxed) >= HotMaxPages) {
                continue;
            }
            if (num_hot == 0) {
                hot_since = now;
            }
            hot.Set(page);
            ++num_hot;
            BbStats::hot_pages.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void KeepHotPagesModified(size_t start_page, size_t end_page) {
        if (num_hot == 0) {
            return;
        }
        if (!HotPagesEnabled() ||
            std::chrono::steady_clock::now() - hot_since > HotPeriod) {
            // Re-protect them (on this upload) and start counting again.
            BbStats::hot_pages.fetch_sub(num_hot, std::memory_order_relaxed);
            hot.Clear();
            write_faults.fill(0);
            num_hot = 0;
            return;
        }
        cpu |= RegionBits(hot, start_page, end_page);
    }

    std::array<u8, NUM_PAGES_PER_REGION> write_faults{};
    std::array<u16, NUM_PAGES_PER_REGION> uses{};
    std::chrono::steady_clock::time_point window_start{};
    RegionBits hot{};
    u32 num_hot = 0;
    std::chrono::steady_clock::time_point hot_since{};

    PageManager* tracker;
    VAddr cpu_addr = 0;
    RegionBits cpu;
    RegionBits gpu;
    RegionBits writeable;
    RegionBits readable;
    /// 0.4.6 beta 12: pages the game's calls watch (CallsCreate), writable whatever `cpu` says.
    RegionBits calls{};
};

} // namespace VideoCore

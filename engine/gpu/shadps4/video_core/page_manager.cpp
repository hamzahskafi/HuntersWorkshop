// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bbport_copy.h"
#include "bbport_gx_ring.h"
#include "bbport_sections.h"
#include "bbport_threads.h"
#include "bbport_toggles.h"
#include "common/assert.h"
#include "common/debug.h"
#include "common/div_ceil.h"
#include "common/error.h"
#include "common/range_lock.h"
#include "common/signal_context.h"
#include "common/thread.h"
#include "core/emulator_settings.h"
#include "core/memory.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>
#include "bbport_platform.h"
#ifndef _WIN32
#include <dlfcn.h>
#include <ucontext.h>
#include <unistd.h>
#endif
#include <string>
#include <fmt/format.h>
#include "core/signals.h"
#include "video_core/page_manager.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"

#ifndef _WIN64
#include <sys/mman.h>
#include "common/adaptive_mutex.h"
#else
#include <windows.h>
#endif

#ifdef __linux__
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <unistd.h>
#else
#include "common/spin_lock.h"
#endif

namespace VideoCore {

constexpr size_t PM_PAGE_SIZE = 4_KB;
constexpr size_t PM_PAGE_BITS = 12;

namespace {
/// bbport: write fault sites (guest code), an open-addressing table keyed by the instruction.
struct FaultSite {
    std::atomic<u64> rip{0};
    std::atomic<u64> caller{0};
    std::atomic<u64> count{0};
    std::atomic<u64> last_address{0};
};
std::array<FaultSite, 512> fault_sites;
std::atomic<u64> fault_sites_dropped{0};
constexpr u64 GuestImage = 0x800000000ull, GuestImageEnd = 0x810000000ull;
constexpr u64 HostSite = 1ull << 63;
constexpr u64 ReadSite = 1ull << 62; ///< a read fault (BB_READBACKS=2 protects GPU data from reads)

/// "+offset" for guest code, the symbol for host code.
std::string SiteName(u64 key) {
    const char* access = key & ReadSite ? "read:" : "";
    key &= ~ReadSite;
    if (!(key & HostSite)) {
        return fmt::format("{}+{:#x}", access, key);
    }
    const u64 address = key & ~HostSite;
#ifdef _WIN32
    char text[512];
    BbPlatform::DescribeAddress(reinterpret_cast<void*>(address), text, sizeof(text));
    return fmt::format("{}host:{}", access, text);
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(address), &info) && info.dli_sname) {
        return fmt::format("{}host:{}+{:#x}", access, info.dli_sname, address - u64(info.dli_saddr));
    }
    if (info.dli_fname) {
        return fmt::format("{}host:{}+{:#x}", access, info.dli_fname, address - u64(info.dli_fbase));
    }
    return fmt::format("{}host:{:#x}", access, address);
#endif
}

/// The write fault this thread is handling (guest offsets; 0: none).
thread_local u64 current_fault_rip = 0, current_fault_caller = 0;

/// bbport (BB_FRAME_STATS): what the protection changes this thread makes are for
/// (BbStats::ProtectKind), set by PageManager's entry points.
thread_local u32 protect_kind = BbStats::ProtectOther;
struct ProtectKindScope {
    explicit ProtectKindScope(u32 kind) : saved{protect_kind} {
        protect_kind = kind;
    }
    ~ProtectKindScope() {
        protect_kind = saved;
    }
    u32 saved;
};

/// Guest code that wrote image (texture) memory: keyed by instruction and image.
struct ImageFaultSite {
    std::atomic<u64> key{0}; // a mix of the site and the image address
    std::atomic<u64> rip{0}, caller{0}, count{0}, image{0}, size{0};
    std::atomic<u32> width{0}, height{0}, format{0}, tiling{0};
};
std::array<ImageFaultSite, 256> image_fault_sites;

void NoteFaultSite(void* context, VAddr address) {
    current_fault_rip = 0;
    // Collecting a stack for every streamed page adds OS reads to the normal load path.
    static const bool enabled = std::getenv("BB_FRAME_STATS") || std::getenv("BB_FAULT_SITES");
    if (!enabled) return;
#ifdef _WIN32
    const auto* registers = static_cast<const EXCEPTION_POINTERS*>(context)->ContextRecord;
    const u64 rip = registers->Rip, rbp = registers->Rbp, rsp = registers->Rsp;
#else
    const auto* g = static_cast<const ucontext_t*>(context)->uc_mcontext.gregs;
    const u64 rip = u64(g[REG_RIP]), rbp = u64(g[REG_RBP]), rsp = u64(g[REG_RSP]);
#endif
    const bool guest_code = rip >= GuestImage && rip < GuestImageEnd;
    u64 caller = 0;
    if (guest_code) {
        // The caller: [rbp + 8] when the guest code keeps frames (its memcpy-like leaves do not).
        u64 saved[2] = {};
        if (BbPlatform::ReadProcessMemory(reinterpret_cast<void*>(rbp), saved, sizeof(saved)) &&
            saved[1] >= GuestImage && saved[1] < GuestImageEnd) {
            caller = saved[1];
        }
    } else {
        // Host code (a libc import the port runs natively, the runtime): the first guest return
        // address on the stack is its guest caller.
        std::array<u64, 64> stack{};
        const std::size_t got = BbPlatform::ReadProcessMemory(reinterpret_cast<void*>(rsp), stack.data(), sizeof(stack)) ? stack.size() : 0;
        for (std::size_t i = 0; i < got; ++i) {
            if (stack[i] >= GuestImage && stack[i] < GuestImageEnd) {
                caller = stack[i];
                break;
            }
        }
    }
    // Guest code: its offset in the image; host code: its address with HostSite set.
    const u64 key = (guest_code ? rip - GuestImage : rip | HostSite) |
                    (Common::IsWriteError(context) ? 0 : ReadSite);
    current_fault_rip = key;
    current_fault_caller = caller ? caller - GuestImage : 0;
    for (u64 i = 0, slot = (key * 0x9E3779B97F4A7C15ull) >> 55; i < fault_sites.size(); ++i) {
        auto& site = fault_sites[(slot + i) % fault_sites.size()];
        u64 expected = 0;
        if (site.rip.load(std::memory_order_relaxed) == key ||
            site.rip.compare_exchange_strong(expected, key)) {
            site.caller.store(caller ? caller - GuestImage : 0, std::memory_order_relaxed);
            site.last_address.store(address, std::memory_order_relaxed);
            site.count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    fault_sites_dropped.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

static void ReportImageFaultSites() {
    bool any = false;
    for (auto& site : image_fault_sites) {
        const u64 count = site.count.exchange(0, std::memory_order_relaxed);
        if (count == 0) {
            continue;
        }
        std::printf("%s %s (from +%#llx) %llu into image %#llx size %llu %ux%u fmt %u tile %u",
                    any ? ";" : "Image write sites (guest code -> texture, this window):",
                    SiteName(site.rip.load()).c_str(), (unsigned long long)site.caller.load(),
                    (unsigned long long)count, (unsigned long long)site.image.load(),
                    (unsigned long long)site.size.load(), site.width.load(), site.height.load(),
                    site.format.load(), site.tiling.load());
        any = true;
    }
    if (any) {
        std::printf("\n");
    }
}

void PageManager::NoteImageFault(VAddr image_address, u64 image_size, u32 width, u32 height,
                                 u32 format, u32 tiling) {
    if (current_fault_rip == 0) {
        return;
    }
    const u64 key = (current_fault_rip * 0x9E3779B97F4A7C15ull) ^ image_address;
    for (u64 i = 0, slot = (key * 0x9E3779B97F4A7C15ull) >> 56; i < image_fault_sites.size(); ++i) {
        auto& site = image_fault_sites[(slot + i) % image_fault_sites.size()];
        u64 expected = 0;
        if (site.key.load(std::memory_order_relaxed) == key ||
            site.key.compare_exchange_strong(expected, key)) {
            site.rip.store(current_fault_rip, std::memory_order_relaxed);
            site.caller.store(current_fault_caller, std::memory_order_relaxed);
            site.image.store(image_address, std::memory_order_relaxed);
            site.size.store(image_size, std::memory_order_relaxed);
            site.width.store(width, std::memory_order_relaxed);
            site.height.store(height, std::memory_order_relaxed);
            site.format.store(format, std::memory_order_relaxed);
            site.tiling.store(tiling, std::memory_order_relaxed);
            site.count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
}

void PageManager::ReportFaultSites() {
    struct Row {
        u64 rip, caller, count, address;
    };
    std::array<Row, 512> rows{};
    std::size_t n = 0;
    u64 total = 0;
    for (auto& site : fault_sites) {
        const u64 count = site.count.exchange(0, std::memory_order_relaxed);
        if (count != 0) {
            rows[n++] = {site.rip.load(), site.caller.load(), count, site.last_address.load()};
            total += count;
        }
    }
    if (total == 0) {
        return;
    }
    ReportImageFaultSites();
    std::sort(rows.begin(), rows.begin() + n, [](const Row& a, const Row& b) { return a.count > b.count; });
    std::printf("Write fault sites (guest code, this window): %llu faults", (unsigned long long)total);
    for (std::size_t i = 0; i < std::min<std::size_t>(n, 12); ++i) {
        std::printf("%s %s (from +%#llx) %llu at %#llx%s", i ? ";" : ":",
                    SiteName(rows[i].rip).c_str(), (unsigned long long)rows[i].caller,
                    (unsigned long long)rows[i].count, (unsigned long long)rows[i].address,
                    BbGxRing::Contains(rows[i].address, 1) ? " (GX range allocator)" : "");
    }
    std::printf("\n");
}

/// bbport: a watcher count that would leave its range, reported (a few times) instead of the
/// assertion that ended the game (exit 23), or the 7-bit count wrapping to 0 with the page still
/// protected.
static void NoteWatcherCount(u64 page, const char* what) {
    static std::atomic<u32> notes{0};
    if (notes.fetch_add(1, std::memory_order_relaxed) < 8) {
        std::printf("GPU: page tracking: %s at page %#llx; tracking goes on\n", what,
                    static_cast<unsigned long long>(page << 12));
    }
}

struct PageManager::Impl {
    struct PageState {
        u8 num_write_watchers : 7;
        // At the moment only buffer cache can request read watchers.
        // And buffers cannot overlap, thus only 1 can exist per page.
        u8 num_read_watchers : 1;

        static constexpr u8 MaxWriteWatchers = 127;

        Core::MemoryPermission WritePerm() const noexcept {
            return num_write_watchers == 0 ? Core::MemoryPermission::Write
                                           : Core::MemoryPermission::None;
        }

        Core::MemoryPermission ReadPerm() const noexcept {
            return num_read_watchers == 0 ? Core::MemoryPermission::Read
                                          : Core::MemoryPermission::None;
        }

        Core::MemoryPermission Perms() const noexcept {
            return ReadPerm() | WritePerm();
        }

        /// bbport: `page` (its number) for the counts past the bit fields (SpillWatcher). Called
        /// with the page's lock held.
        template <s32 delta, bool is_read>
        u8 AddDelta(u64 page) {
            if constexpr (is_read) {
                if constexpr (delta == 1) {
                    if (num_read_watchers == 1) [[unlikely]] {
                        NoteWatcherCount(page, "a second read watcher");
                        return num_read_watchers;
                    }
                    return ++num_read_watchers;
                } else if (delta == -1) {
                    if (num_read_watchers == 0) [[unlikely]] {
                        NoteWatcherCount(page, "not enough read watchers");
                        return 0;
                    }
                    return --num_read_watchers;
                } else {
                    return num_read_watchers;
                }
            } else {
                if constexpr (delta == 1) {
                    // 128 or more watchers (images piling up on one page): the count stays at
                    // its maximum, the page protected, and the excess is kept aside.
                    if (num_write_watchers == MaxWriteWatchers) [[unlikely]] {
                        if (!SpillWatcher(page, 1)) {
                            NoteWatcherCount(page, "write watchers beyond the overflow table");
                        }
                        return num_write_watchers;
                    }
                    return ++num_write_watchers;
                } else if (delta == -1) {
                    if (num_write_watchers == MaxWriteWatchers && SpillWatcher(page, -1))
                        [[unlikely]] {
                        return num_write_watchers;
                    }
                    if (num_write_watchers == 0) [[unlikely]] {
                        NoteWatcherCount(page, "not enough write watchers");
                        return 0;
                    }
                    return --num_write_watchers;
                } else {
                    return num_write_watchers;
                }
            }
        }
    };

    /// bbport: write watchers past a page's 7-bit count. +1 keeps one more (false: the table is
    /// full, the watcher is lost and the page may be unprotected early); -1 takes one back
    /// (false: none kept for the page). Nests inside the page locks; nothing else takes it.
    struct SpilledWatchers {
        u64 page;
        u32 extra;
    };
    inline static std::array<SpilledWatchers, 256> spilled_watchers{};
    inline static std::atomic_flag spill_lock{};

    static bool SpillWatcher(u64 page, s32 delta) {
        while (spill_lock.test_and_set(std::memory_order_acquire)) {
        }
        bool done = false;
        SpilledWatchers* free_entry = nullptr;
        for (auto& entry : spilled_watchers) {
            if (entry.extra != 0 && entry.page == page) {
                entry.extra += delta;
                done = true;
                break;
            }
            if (entry.extra == 0 && !free_entry) {
                free_entry = &entry;
            }
        }
        if (!done && delta > 0 && free_entry) {
            *free_entry = {page, 1};
            done = true;
        }
        spill_lock.clear(std::memory_order_release);
        return done;
    }

    static constexpr size_t ADDRESS_BITS = 40;
    static constexpr size_t NUM_ADDRESS_PAGES = 1ULL << (40 - PM_PAGE_BITS);
    static constexpr size_t NUM_ADDRESS_LOCKS = NUM_ADDRESS_PAGES / PAGES_PER_LOCK;
    inline static Vulkan::Rasterizer* rasterizer;

    Impl() = default;
    virtual ~Impl() = default;

    virtual void OnMap(VAddr address, size_t size) {
        // No-op
    }

    virtual void OnUnmap(VAddr address, size_t size) {
        // No-op
    }

    virtual void Protect(VAddr address, size_t size, Core::MemoryPermission perms) = 0;

    /// bbport: Protect, counted by thread and purpose (BB_FRAME_STATS) and timed on the draw
    /// recording thread (BB_SECTIONS "protect").
    void ApplyProtect(VAddr address, size_t size, Core::MemoryPermission perms) {
        BbSections::Scope scope{BbSections::Protect};
        if (!BbStats::enabled) {
            Protect(address, size, perms);
            return;
        }
        const auto start = std::chrono::steady_clock::now();
        Protect(address, size, perms);
        const u64 ns = static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                            std::chrono::steady_clock::now() - start)
                                            .count());
        if (Vulkan::DrawPipe::OnStageB()) {
            BbStats::stage_b_protect_calls[protect_kind].fetch_add(1, std::memory_order_relaxed);
            BbStats::stage_b_protect_ns[protect_kind].fetch_add(ns, std::memory_order_relaxed);
        } else {
            BbStats::other_protect_calls.fetch_add(1, std::memory_order_relaxed);
            BbStats::other_protect_ns.fetch_add(ns, std::memory_order_relaxed);
        }
    }

    static bool GuestFaultSignalHandler(void* context, void* fault_address) {
        const auto addr = reinterpret_cast<VAddr>(fault_address);
        NoteFaultSite(context, addr);
        // bbport: the draw recording thread handles its faults inline too (vk_draw_pipe.h).
        const auto is_gpu_thread = rasterizer->IsGpuSideThread();
        // bbport: copy threads working for a GPU-side thread fault on its behalf, several at once:
        // one at a time, as that thread's own faults are.
        static std::mutex for_gpu_side_mutex;
        std::unique_lock for_gpu_side_lock{for_gpu_side_mutex, std::defer_lock};
        if (BbCopy::ForGpuSide()) {
            for_gpu_side_lock.lock();
        }
        if (is_gpu_thread) {
            BbStats::gpu_signal_faults.fetch_add(1, std::memory_order_relaxed);
        }
        if (Common::IsWriteError(context)) {
            BbStats::Timer timer{BbStats::t_write_faults};
            const bool handled = rasterizer->OnWriteFault(addr, is_gpu_thread);
            current_fault_rip = 0;
            return handled;
        } else {
            BbStats::read_faults.fetch_add(1, std::memory_order_relaxed);
            BbStats::Timer timer{BbStats::t_read_faults};
            // bbport: within the faulting page (see Rasterizer::OnWriteFault).
            return rasterizer->ReadMemory(addr, std::min<u64>(8, GetNextPageAddr(addr) - addr),
                                          is_gpu_thread);
        }
        return false;
    }

    template <bool track, bool is_read>
    void UpdatePageWatchers(VAddr addr, u64 size) {
        RENDERER_TRACE;

        size_t page = addr >> PM_PAGE_BITS;
        const u64 page_end = Common::DivCeil(addr + size, PM_PAGE_SIZE);

        // Acquire locks for the range of pages
        const size_t first_lock = page / PAGES_PER_LOCK;
        const size_t last_lock = Common::DivCeil(page_end, PAGES_PER_LOCK);
        Common::RangeLockGuard lk(locks.begin() + first_lock, locks.begin() + last_lock);

        auto perms = cached_pages[page].Perms();
        u64 range_begin = 0;
        u64 range_bytes = 0;
        u64 potential_range_bytes = 0;

        const auto release_pending = [&] {
            if (range_bytes > 0) {
                RENDERER_TRACE;
                // Perform pending (un)protect action
                ApplyProtect(range_begin << PM_PAGE_BITS, range_bytes, perms);
                range_bytes = 0;
                potential_range_bytes = 0;
            }
        };

        // Iterate requested pages
        // bbport: no check that the range is GPU mapped (it asked the rasterizer's mapped ranges,
        // a shared lock and an interval lookup, on every protection change); a range that is not
        // mapped is skipped by the protection change anyway.
        for (; page != page_end; ++page) {
            PageState& state = cached_pages[page];

            // Apply the change to the page state
            const u8 new_count = state.AddDelta<track ? 1 : -1, is_read>(page);

            if (auto new_perms = state.Perms(); new_perms != perms) [[unlikely]] {
                // If the protection changed add pending (un)protect action
                release_pending();
                perms = new_perms;
            } else if (range_bytes != 0) {
                // If the protection did not change, extend the potential range
                potential_range_bytes += PM_PAGE_SIZE;
            }

            // Only start a new range if the page must be (un)protected
            if ((new_count == 0 && !track) || (new_count == 1 && track)) {
                if (range_bytes == 0) {
                    // Start a new potential range
                    range_begin = page;
                    potential_range_bytes = PM_PAGE_SIZE;
                }
                // Extend current range up to potential range
                range_bytes = potential_range_bytes;
            }
        }

        // Add pending (un)protect action
        release_pending();
    }

    template <bool track, bool is_read>
    void UpdatePageWatchersForRegion(VAddr base_addr, RegionBits& mask) {
        RENDERER_TRACE;
        auto start_range = mask.FirstRange();
        auto end_range = mask.LastRange();

        if (start_range.second == end_range.second) {
            // if all pages are contiguous, use the regular UpdatePageWatchers
            const VAddr start_addr = base_addr + (start_range.first << PM_PAGE_BITS);
            const u64 size = (start_range.second - start_range.first) << PM_PAGE_BITS;
            return UpdatePageWatchers<track, is_read>(start_addr, size);
        }

        size_t base_page = (base_addr >> PM_PAGE_BITS);
        ASSERT(base_page % PAGES_PER_LOCK == 0);
        std::scoped_lock lk(locks[base_page / PAGES_PER_LOCK]);
        auto perms = cached_pages[base_page + start_range.first].Perms();
        u64 range_begin = 0;
        u64 range_bytes = 0;
        u64 potential_range_bytes = 0;

        const auto release_pending = [&] {
            if (range_bytes > 0) {
                RENDERER_TRACE;
                // Perform pending (un)protect action
                ApplyProtect((range_begin << PM_PAGE_BITS), range_bytes, perms);
                range_bytes = 0;
                potential_range_bytes = 0;
            }
        };

        // Iterate pages
        for (size_t page = start_range.first; page < end_range.second; ++page) {
            PageState& state = cached_pages[base_page + page];
            const bool update = mask.Get(page);

            // Apply the change to the page state
            const u8 new_count =
                update ? state.AddDelta<track ? 1 : -1, is_read>(base_page + page)
                       : state.AddDelta<0, is_read>(base_page + page);

            if (auto new_perms = state.Perms(); new_perms != perms) [[unlikely]] {
                // If the protection changed add pending (un)protect action
                release_pending();
                perms = new_perms;
            } else if (range_bytes != 0) {
                // If the protection did not change, extend the potential range
                potential_range_bytes += PM_PAGE_SIZE;
            }

            // If the page is not being updated, skip it
            if (!update) {
                continue;
            }

            // If the page must be (un)protected
            if ((new_count == 0 && !track) || (new_count == 1 && track)) {
                if (range_bytes == 0) {
                    // Start a new potential range
                    range_begin = base_page + page;
                    potential_range_bytes = PM_PAGE_SIZE;
                }
                // Extend current rango up to potential range
                range_bytes = potential_range_bytes;
            }
        }

        // Add pending (un)protect action
        release_pending();
    }

    std::array<PageState, NUM_ADDRESS_PAGES> cached_pages{};
#ifdef PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
    using LockType = Common::AdaptiveMutex;
#else
    using LockType = Common::SpinLock;
#endif
    std::array<LockType, NUM_ADDRESS_LOCKS> locks{};
};

struct SignalImpl : public PageManager::Impl {
    SignalImpl(Vulkan::Rasterizer* rasterizer_) : Impl() {
        rasterizer = rasterizer_;

        // Should be called first.
        constexpr auto priority = std::numeric_limits<u32>::min();
        Core::Signals::Instance()->RegisterAccessViolationHandler(GuestFaultSignalHandler,
                                                                  priority);
    }

    void Protect(VAddr address, size_t size, Core::MemoryPermission perms) override {
        RENDERER_TRACE;
        auto* memory = Core::Memory::Instance();
        auto& impl = memory->GetAddressSpace();
        // bbport: write-only (a read watcher on a page without write watchers, BB_READBACKS=2)
        // cannot be expressed; reads must fault, so writes do too (the assertion ended the game).
        if (perms == Core::MemoryPermission::Write) {
            perms = Core::MemoryPermission::None;
        }
        impl.Protect(address, size, perms);
    }

};

PageManager::PageManager(Vulkan::Rasterizer* rasterizer_) {
#ifdef __linux__
    LOG_INFO(Config, "Memory tracking method: signals");
#endif
    impl = std::make_unique<SignalImpl>(rasterizer_);
}

PageManager::~PageManager() = default;

void PageManager::OnGpuMap(VAddr address, size_t size) {
    impl->OnMap(address, size);
}

void PageManager::OnGpuUnmap(VAddr address, size_t size) {
    impl->OnUnmap(address, size);
}

template <bool track>
void PageManager::UpdatePageWatchers(VAddr addr, u64 size) const {
    ProtectKindScope kind{BbStats::ProtectTexture};
    impl->UpdatePageWatchers<track, false>(addr, size);
}

template <bool track, bool is_read>
void PageManager::UpdatePageWatchersForRegion(VAddr base_addr, RegionBits& mask) const {
    ProtectKindScope kind{track && !is_read ? BbStats::ProtectUpload : BbStats::ProtectOther};
    impl->UpdatePageWatchersForRegion<track, is_read>(base_addr, mask);
}

template void PageManager::UpdatePageWatchers<true>(VAddr addr, u64 size) const;
template void PageManager::UpdatePageWatchers<false>(VAddr addr, u64 size) const;
template void PageManager::UpdatePageWatchersForRegion<true, true>(VAddr base_addr,
                                                                   RegionBits& mask) const;
template void PageManager::UpdatePageWatchersForRegion<true, false>(VAddr base_addr,
                                                                    RegionBits& mask) const;
template void PageManager::UpdatePageWatchersForRegion<false, true>(VAddr base_addr,
                                                                    RegionBits& mask) const;
template void PageManager::UpdatePageWatchersForRegion<false, false>(VAddr base_addr,
                                                                     RegionBits& mask) const;

} // namespace VideoCore

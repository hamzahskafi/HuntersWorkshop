// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "bbport_copy.h"

#include <deque>
#include <condition_variable>
#include <future>
#include <map>
#include <mutex>
#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <boost/container/small_vector.hpp>

#include "common/interval_set.h"
#include "common/types.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/buffer_cache/fault_manager.h"
#include "video_core/buffer_cache/range_set.h"
#include "engine/gx_resources.h"
#include "video_core/renderer_vulkan/vk_semaphore.h"

namespace AmdGpu {
struct Liverpool;
}

namespace Core {
class MemoryManager;
}

namespace Vulkan {
class GraphicsPipeline;
struct SubmitInfo;
class Runtime;
class StagingBufferPool;
} // namespace Vulkan

namespace VideoCore {

class TextureCache;
class MemoryTracker;
class PageManager;

/// bbport: an IntervalList whose every change advances `generation`: lookups of the buffer
/// cache's residency remember their answers for as long as nothing changed (ResidencyMemo).
template <class IV = Interval>
class TrackedIntervalList : public IntervalList<IV> {
public:
    void Add(IV value) {
        ++generation;
        IntervalList<IV>::Add(value);
    }
    void Subtract(u64 start, u64 end) {
        ++generation;
        IntervalList<IV>::Subtract(start, end);
    }
    void Clear() {
        ++generation;
        IntervalList<IV>::Clear();
    }
    static inline u64 generation = 1;
};

class BufferCache {
    static constexpr u64 ADDRESS_SPACE_BITS = 40;
    static constexpr u64 ARENA_PAGE_BITS = 32;
    static constexpr u64 ARENA_PAGE_SIZE = u64{1} << ARENA_PAGE_BITS;
    static constexpr u64 NUM_ARENA_PAGES = u64{1} << (ADDRESS_SPACE_BITS - ARENA_PAGE_BITS);
    static constexpr u64 MIN_BLOCK_SIZE = 16_KB;

public:
    /// Read-only bindings up to this size are copied into a stream buffer (bbport: public for
    /// the draw pipeline's constant ring).
    static constexpr u64 STREAM_THRESHOLD = 16_KB;

    explicit BufferCache(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler,
                         Vulkan::Runtime& runtime, AmdGpu::Liverpool* liverpool,
                         TextureCache& texture_cache, PageManager& tracker);
    ~BufferCache();

    /// Returns a pointer to GDS device local buffer.
    [[nodiscard]] const Buffer* GetGdsBuffer() const noexcept {
        return &gds_buffer;
    }

    /// Retrieves the device local DBA page table buffer.
    [[nodiscard]] Buffer* GetBdaPageTableBuffer() noexcept {
        return bda_pagetable_buffer.get();
    }

    /// Retrieves the fault buffer.
    [[nodiscard]] Buffer* GetFaultBuffer() noexcept {
        return fault_manager->GetFaultBuffer();
    }

    /// Retrieves the stream buffer.
    StreamBuffer& GetStreamBuffer() noexcept {
        return stream_buffer;
    }

    /// Returns minimum granularity of a sparse memory bind.
    u32 GetSparsePageShift() const noexcept {
        return block_shift;
    }

    /// Invalidates any buffer in the logical page range.
    void InvalidateMemory(VAddr device_addr, u64 size, bool assume_locks = false);

    /// bbport: a guest write fault at `device_addr` was handled; unprotect its neighbourhood.
    void ExtendWriteFault(VAddr device_addr);

    /// Flushes any GPU modified buffer in the logical page range back to CPU memory.
    void ReadMemory(VAddr device_addr, u64 size, bool is_write = false, bool assume_locks = false);

    /// bbport: a new draw or dispatch is being recorded: the per-binding housekeeping of
    /// EnsureResident (the GX region's import, unmaps, idle VRAM copies) runs once for it, not per
    /// binding.
    void NewPacket() noexcept {
        ++packet_epoch;
    }

    /// bbport (BB_FRAME_STATS): ObtainBuffer calls made for the copy shader HLE (set around
    /// them on the draw recording thread), counted by path: [read, written] x [in place, stream
    /// copy, VRAM], with their bytes.
    static inline bool hle_obtain = false;
    /// The copy shader's source spans (vk_shader_hle.cpp), set around their ObtainBuffer calls:
    /// read-only data the game wrote just before the copy and reads once. Up to
    /// CopySourceStreamLimit they take the stream path, untracked (see ObtainBuffer).
    static inline thread_local bool copy_source = false;
    static inline std::array<u64, 6> hle_path_calls{}, hle_path_bytes{};
    /// 0.4.5 beta 9: the path this thread's last ObtainBuffer took: 0 in place (the game's
    /// memory), 1 the stream buffer (a snapshot), 2 the arena (the VRAM copy); 0xFF if none of
    /// them. For the copy shader's held copies (vk_copy_hold.h), reset before each call there.
    static inline thread_local u8 last_obtain_path = 0xFF;
    /// Bindings inside the game's GX region (bbport_gx_ring.h), by how they are served, with their
    /// bytes (the stats, in vk_shader_hle.cpp): small read-only ones copied (the stream path), read,
    /// texel and written ones in place (BB_GX_IN_PLACE), read-only ones across its edge copied, and
    /// tracked ones (through the arena: before the import, without it, or across its edge).
    enum GxUse : u32 {
        GxCopied, GxInPlace, GxTexel, GxWritten, GxEdgeCopied, GxTailCopied, GxTracked, GxUses
    };
    static inline std::array<std::atomic<u64>, GxUses> gx_ring_calls{}, gx_ring_bytes{};
    /// bbport: whether bindings may be in place: once the game's GX region is bound in place
    /// (BB_GX_IN_PLACE).
    [[nodiscard]] bool InPlaceActive() const noexcept {
        return gx_ready.load(std::memory_order_acquire);
    }

    /// Finds a buffer for the specified region.
    [[nodiscard]] std::pair<const Buffer*, u64> ObtainBuffer(VAddr device_addr, u32 size,
                                                             bool is_written,
                                                             bool is_texel_buffer = false);

    /// 0.4.4 beta 14: the guest range of an image made for a copy shader's copy
    /// (Rasterizer::IsComputeImageCopy): the image holds the copy, the game's memory there is
    /// older. Texel buffer reads are filled from the image, textures read it; a raw read-only
    /// binding over one of the 16 most recent is logged once (ObtainBuffer), a written binding
    /// over one ends it (its memory is newer then).
    void NoteImageMadeForCopy(VAddr address, u64 size);

    /// Attempts to obtain a buffer without modifying the cache contents.
    [[nodiscard]] std::pair<const Buffer*, u64> ObtainBufferForImage(VAddr device_addr, u32 size);

    /// bbport (BB_DRAWREC_WORKERS, the lookahead thread): asks the cache for what ObtainBuffer
    /// reads first for this range (the arena table entry, the page tracking words); nothing is
    /// changed or read through. Returns the cache lines requested.
    u32 PrefetchBinding(VAddr device_addr, u32 size) const noexcept;

    /// bbport BB_GX_IN_PLACE: where a command processor write (WRITE_DATA) the GPU performs in
    /// stream order lands: the GX region's own buffer, or the arena where the region is bound in
    /// place (BB_GX_OWN_BUFFER=0). The range counts as GPU-modified until the GPU has done it, so no
    /// copy of it is taken before (IsRegionGpuModified).
    [[nodiscard]] std::optional<std::pair<const Buffer*, u64>> CommandWriteTarget(VAddr device_addr,
                                                                                u32 size);

    /// Return true when a region is modified from the CPU
    [[nodiscard]] bool IsRegionCpuModified(VAddr addr, size_t size);

    /// Return true when a region is modified from the GPU
    [[nodiscard]] bool IsRegionGpuModified(VAddr addr, size_t size);
    /// 0.4.4 beta 16: for a range the memory tracker holds GPU-modified, the number of its newest
    /// GPU write (0: none since GpuWriteStamps began numbering); ~0 otherwise (not GPU-modified, or
    /// a write in place not done yet, which is not numbered).
    [[nodiscard]] u64 GpuWriteVersion(VAddr addr, u64 size);

    /// Processes the fault buffer.
    void ProcessFaultBuffer();

    /// Synchronizes all buffers needed for DMA.
    void SynchronizeDmaBuffers();

    /// bbport (BB_PREUPLOAD): notes a guest mapping or unmapping (Rasterizer::MapMemory): the
    /// game's GPU memory (direct memory of type 3 with GPU access) is pre-uploaded.
    void NotePreuploadMapping(VAddr addr, u64 size, bool mapped);
    /// bbport (BB_PREUPLOAD): uploads up to `budget` bytes of that memory which the CPU changed and
    /// then left alone for a few frames into the arena, ahead of its use (draw recording thread).
    void Preupload(u64 budget);
    /// bbport (BB_GPU_WRITE_TWINS): forgets the twins of unmapped guest memory.
    void DropTwins(VAddr addr, u64 size);

    /// Commits pending sparse buffer memory binds. Must be called before every scheduler submit.
    void SubmitPendingArenaBinds(Vulkan::SubmitInfo& info);

    /// bbport BB_GX_IN_PLACE: the game unmapped [addr, addr + size) (a guest thread): arena
    /// blocks bound to the GX region's memory there are unbound before the next binding (GPU side).
    void UnmapInPlace(VAddr addr, u64 size);
    /// Whether the range is bound to the game's own memory (no uploads, tracking or readbacks).
    [[nodiscard]] bool IsInPlace(VAddr addr, u64 size) const;
    /// Whether any part of the range is.
    [[nodiscard]] bool IsAnyInPlace(VAddr addr, u64 size) const {
        return InPlaceActive() && size != 0 &&
               (GxOverlaps(addr, size) ||
                in_place_blocks.Overlaps(addr >> block_shift,
                                         ((addr + size - 1) >> block_shift) + 1));
    }

private:
    struct ArenaBinds {
        const Buffer* arena;
        boost::container::small_vector<vk::SparseMemoryBind, 32> binds;
    };

    ArenaBinds* BindsForArena(const Buffer* arena) {
        auto it = std::ranges::find(pending_binds, arena, &ArenaBinds::arena);
        if (it != pending_binds.end()) {
            return std::addressof(*it);
        }
        return &pending_binds.emplace_back(arena);
    }

    const Buffer* GetArena(u64 first_block, u64 last_block);

    void EnsureResident(const Buffer* arena, u64 first_block, u64 last_block);
    /// Blocks [start, end) bound to `memory` at byte `offset` (VRAM, or the GX region's import).
    struct ResidentBind {
        u64 start, end; ///< blocks
        vk::DeviceMemory memory;
        u64 offset;
    };
    /// bbport: the housekeeping EnsureResident did before every binding (Process*), once a packet.
    void Maintain();
    u64 packet_epoch = 1, maintained_epoch = 0;
    /// bbport: answers of EnsureResident ("all resident") and IsInPlace for block ranges, valid
    /// while resident_ranges and in_place_blocks keep their generation.
    struct ResidencyMemo {
        u64 first = ~0ull, last = 0, generation = 0;
        bool resident = false, in_place = false;
    };
    std::array<ResidencyMemo, 256> residency_memo{};
    static u64 ResidencyGeneration() noexcept {
        return TrackedIntervalList<Backing>::generation + TrackedIntervalList<>::generation;
    }
    void ProcessPendingUnmaps();
    /// BB_FRAME_STATS: GPU-written bindings over 64 KiB, reported every 5 s (where they are bound,
    /// which images lie over them).
    void NoteLargeGpuWrite(VAddr addr, u64 size);
    /// NoteImageMadeForCopy's ranges, looked at by ObtainBuffer while any is live: written by one
    /// thread at a time (the mutex), read without it (a range being replaced is skipped).
    void CheckMadeForCopy(VAddr device_addr, u32 size, bool is_written);
    struct MadeForCopy {
        std::atomic<VAddr> begin{0}, end{0}; ///< begin 0: free
        std::atomic<bool> told{false};
    };
    std::array<MadeForCopy, 16> made_for_copy{};
    std::atomic<u32> made_for_copy_live{0};
    u32 made_for_copy_next = 0; ///< the slot replaced next when none is free
    std::mutex made_for_copy_mutex;
    /// BB_TRACE_SIZE=bytes (diagnostics): bindings over GPU-written buffers of that size, counted by
    /// the path they take, reported every 5 s.
    void TraceBinding(VAddr addr, u64 size, bool is_written, int kind);
    std::vector<std::pair<VAddr, u64>> traced_ranges;
    std::array<u64, 9> trace_counts{};
    u64 trace_texel = 0, trace_min = ~0ull, trace_max = 0;
    std::chrono::steady_clock::time_point trace_report{};
    struct LargeWrite {
        VAddr addr = 0;
        u64 size = 0, count = 0;
        bool in_place = false;
        std::string images;
    };
    std::unordered_map<u64, LargeWrite> large_writes;
    std::chrono::steady_clock::time_point large_writes_report{};
    /// Arena residency memory from the 64 MiB blocks: the memory and the byte offset in it.
    std::pair<vk::DeviceMemory, u64> AllocateResidency(u64 bytes);
    /// bbport: BbStats::residency_unused_bytes after the free list or the current block changed.
    void NoteResidencyUnused();
    /// bbport: idle VRAM blocks (see group_use).
    void NoteUse(VAddr address, u64 size);
    void ProcessIdleBlocks();
    /// vram_level from the VRAM manager (vram_policy.h; 0.4.5 beta 14), once a second.
    void UpdateVramLevel();
    /// A VRAM slot of a block that left it: reused by the next move to VRAM.
    void ReleaseResidencySlot(vk::DeviceMemory memory, u64 offset);
    /// Unbinds a VRAM block of memory the game unmapped (uploaded again if mapped and used).
    void EvictVramBlock(u64 block);
    /// While VRAM is tight or full (the VRAM manager): VRAM copies of the game's data unused for
    /// the manager's time leave VRAM, the longest unused first; their slots take the next blocks
    /// instead of new VRAM, and they are uploaded again from the game's memory when bound.
    void EvictIdleCopies(u32 now);
    /// Residency chunks without blocks for 30 s (5 s while VRAM is tight), the rebinds away from
    /// them done: back to the driver.
    void FreeEmptyChunks(u32 now);
    void DownloadMemory(const Buffer* arena, VAddr device_addr, u64 size);

    bool SynchronizeMemory(const Buffer* arena, VAddr device_addr, u32 size, bool is_written,
                           bool is_texel_buffer);

    /// bbport: batched small copies on the copy threads (BbCopy::QueueCopy).
    static void RunGuestCopy(const BbCopy::Item& item);
    void SmallGuestCopy(const BbCopy::Item& item);

    const Buffer* UploadCopies(const Buffer* arena, std::span<vk::BufferCopy> copies,
                               size_t total_size_bytes);

    /// The image over [device_addr, device_addr + size), if any, written into `buffer` at `offset`
    /// (the arena, or the GX region's own buffer).
    bool SynchronizeMemoryFromImage(const Buffer* buffer, u64 offset, VAddr device_addr, u32 size);

    const Vulkan::Instance& instance;
    Vulkan::Scheduler& scheduler;
    // bbport: texel buffer ranges known not to alias an image, per image registry generation.
    struct ImageMiss {
        VAddr address = 0;
        u32 size = 0;
        u64 generation = ~0ULL;
    };
    std::array<ImageMiss, 256> image_miss_cache{};
    // bbport: the last fills of texel buffer ranges from the image over
    // them. A fill is left out while the image, its scene proxy, the buffer range and its
    // residency are all as they were after the previous one.
    struct TileMemo {
        VAddr address = 0;
        u32 size = 0;
        const Buffer* arena = nullptr;
        u64 arena_offset = 0;
        u64 image_uid = 0;
        const void* backing = nullptr;
        u64 write_seq = 0;
        u32 flags = 0;
        u64 watch = 0;
        u64 residency = 0;
        u64 write_tick = 0;
    };
    std::array<TileMemo, 8> tile_memos{};
    u32 tile_memo_next = 0;
    Vulkan::Runtime& runtime;
    Vulkan::StagingBufferPool& staging_pool;
    AmdGpu::Liverpool* liverpool;
    Core::MemoryManager* memory;
    TextureCache& texture_cache;
    std::unique_ptr<MemoryTracker> memory_tracker;

    StreamBuffer stream_buffer;
    Buffer gds_buffer;
    RangeSet gpu_modified_ranges;

    std::unique_ptr<FaultManager> fault_manager;
    std::unique_ptr<Buffer> bda_pagetable_buffer;

    std::array<const Buffer*, NUM_ARENA_PAGES> address_space{};
    std::deque<Buffer> arenas;
    std::vector<ArenaBinds> pending_binds;
    std::mutex preupload_mutex;               ///< the map hooks come from guest threads
    std::map<VAddr, VAddr> preupload_ranges; ///< start -> end of pre-uploaded guest memory
    VAddr preupload_cursor = 0;
    u64 preupload_first_bytes = 0, preupload_again_bytes = 0; ///< BB_FRAME_STATS report
    Vulkan::Semaphore memory_semaphore;

    struct Backing : public Interval {
        vk::DeviceMemory memory;
        u64 offset; ///< in blocks, like start and end (SubRange, CanMergeWith)
        constexpr bool CanMergeWith(const Backing& other) const noexcept {
            return memory == other.memory && offset + (end - start) == other.offset;
        }
        constexpr Backing SubRange(u64 a, u64 b) const noexcept {
            return {{a, b}, memory, offset + (a - start)};
        }
    };
    TrackedIntervalList<Backing> resident_ranges;
    /// bbport BB_GX_IN_PLACE: blocks bound to the game's own memory (the GX region's import), and
    /// guest unmaps to apply.
    TrackedIntervalList<> in_place_blocks;
    std::mutex pending_unmaps_mutex;
    std::vector<std::pair<VAddr, u64>> pending_unmaps;
    std::atomic<bool> unmaps_pending{false};
    /// VRAM slots of blocks that left VRAM, for the next blocks moved there (in their chunks).
    u64 free_slot_count = 0;
    u64 bind_wait_tick = 0; ///< the next arena binds wait for it

    u32 arena_memory_type_index{};
    /// bbport: a memory type the arena takes outside the card's own heaps (discrete GPUs): new
    /// blocks of the game's data go there while VRAM is tight or full (AllocateResidency).
    std::optional<u32> system_memory_type_index;
    u64 system_residency_bytes = 0;
    u32 system_residency_reports = 0;
    vk::DeviceMemory residency_memory{}; ///< bbport: the 64 MiB block arena residency comes from
    u64 residency_size = 0, residency_used = 0;
    /// bbport: the next block, allocated in the background. Stage B waits for it at most 50 ms
    /// (TakeSpareResidency): a std::future here once never became ready and froze the game.
    struct SpareBlock {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false, abandoned = false;
        vk::DeviceMemory memory{};
    };
    std::shared_ptr<SpareBlock> spare_residency;
    u32 spare_residency_timeouts = 0;
    vk::DeviceMemory TakeSpareResidency();
    /// bbport: VRAM blocks no longer needed. Per 2 MiB of guest addresses, the second
    /// (BbStats::coarse_second) it was last bound: blocks in VRAM unused for a while leave it
    /// (EvictIdleCopies, while VRAM is tight), and residency chunks left without blocks
    /// go back to the driver (ProcessIdleBlocks). Blocks of unmapped memory leave VRAM too.
    static constexpr u64 USE_GROUP_BITS = 21;
    std::vector<u32> group_use;
    /// Beta 15: groups EvictIdleCopies took out of VRAM, and when (refaults: back within 5 minutes).
    std::unordered_map<u64, u32> evicted_group_second;
    u32 minute_second = 0;
    /// Beta 16 (research): the buffers the game released (engine/gx_resources.h).
    std::vector<BbEngine::GxResources::Gone> gx_released;
    struct ResidencyChunk {
        u64 size;
        u64 used;         ///< bytes handed out and not returned
        u32 empty_second; ///< when `used` dropped to 0
        u64 empty_tick;   ///< the submission then being recorded (it carries the rebinds)
        std::vector<u64> free_offsets; ///< slots of blocks that left (byte offsets)
    };
    std::unordered_map<VkDeviceMemory, ResidencyChunk> residency_chunks;
    u32 idle_scan_second = 0;
    u32 gx_ring_looks = 0; ///< ProcessIdleBlocks: seconds without the GX range allocator's region
    /// The VRAM manager's level, once a second (UpdateVramLevel): 0 room, 1 tight (unused VRAM
    /// copies leave, new blocks go to system memory where it can hold them), 2 full.
    std::atomic<int> vram_level{0};
    /// bbport: the second a shader last read the game's memory through the page table
    /// (SynchronizeDmaBuffers). Those reads do not mark blocks as used, so nothing leaves VRAM
    /// while such shaders run (EvictIdleCopies).
    std::atomic<u32> dma_second{0};
    u64 evicted_bytes = 0;
    /// Blocks EvictIdleCopies took out of VRAM: they come back with their whole 2 MiB group.
    TrackedIntervalList<> evicted_blocks;
    u32 evict_reports = 0, dma_kept_reports = 0, dma_kept_second = 0;
    u32 block_size{};
    u32 block_shift{};
    u32 blocks_per_arena_page{};
    /// bbport: arena pages a new arena covers at most (an aligned group, within the driver's
    /// largest buffer): the game's memory in one arena, never migrated (GetArena).
    u64 arena_group_pages{1};
    u32 blocks_per_arena_page_shift{};

    /// bbport BB_GX_IN_PLACE (default on): the game's GX region as a PC upload heap. Once the game
    /// has made it (BbGxRing), its blocks [gx_first, gx_end), from the one holding its first byte
    /// (whose bytes before the region are the game's allocation's, in place with it: NoteBeforeGx)
    /// to the last it covers whole, are imported for the GPU as they are
    /// (BbGuestMemory::ImportRange, on a thread of its own) into a buffer of their own, for good:
    /// what is bound inside them is read and written in place through that buffer (gx_own), with no
    /// write tracking, uploads or VRAM copy; small read-only bindings are still copied when bound
    /// (the stream path), and so are read-only ones across its edge. The page table points the
    /// blocks' device addresses at the buffer. The arena keeps its own blocks there unbound;
    /// anything that still needs them (a GPU write across the edge) binds them to the import as
    /// well (GxArenaBound), as 0.4.4 betas 5 to 8 did for all of it, outside the Vulkan spec on
    /// NVIDIA. BB_GX_OWN_BUFFER=0: that from the start, as then. A binding over the region waits
    /// for the import; one that fails leaves it tracked as before.
    enum class GxState : u8 { Off, Waiting, Importing, Imported, Ready, Failed };
    std::atomic<u8> gx_state{u8(GxState::Off)};
    std::atomic<bool> gx_ready{false}, gx_unmapped{false};
    u64 gx_first = 0, gx_end = 0; ///< blocks
    VAddr gx_head = 0;            ///< the region's first byte (BbGxRing::g_start)
    VAddr gx_tail = 0;            ///< the byte after its last (BbGxRing::g_end)
    /// The end of the page holding the region's last bytes when it is imported with the region
    /// (0: not): read-only bindings reaching into it are read in place (InGxTail). The page table
    /// keeps that block on the tracked copy: the rest of it is the game's next allocation.
    std::atomic<VAddr> gx_tail_end{0};
    u64 gx_import_bytes = 0; ///< the import's size: the blocks, and the tail page if taken
    std::atomic<bool> gx_before_noted{false};
    std::array<std::atomic<bool>, 2> gx_edge_noted{};
    /// The first binding across each edge of the region's buffer (copied when bound): logged once,
    /// with where it begins and ends against the region, to tell a binding of the region's own
    /// bytes in its partly covered end blocks from one running beyond the game's allocation.
    void NoteGxEdge(VAddr addr, u64 size, bool first_edge, bool texel);
    /// A binding in the region's first block before the region itself (bytes the game's allocation
    /// puts in front of it: 64 in 1.09): logged once, as nothing of the game's GPU work should.
    void NoteBeforeGx(VAddr addr, u64 size);
    vk::Buffer gx_buffer{};               ///< the import's, until gx_view takes it
    vk::DeviceAddress gx_address = 0;     ///< its device address
    vk::DeviceMemory gx_memory{};
    std::unique_ptr<Buffer> gx_view;      ///< the import's buffer, from the region's readiness
    std::atomic<const Buffer*> gx_own{nullptr}; ///< gx_view, while bindings go through it
    std::atomic<bool> gx_arena{false};    ///< the arena's blocks there bound to the import too
    void PollGxRegion(VAddr addr, u64 size);
    void StartGxImport();
    /// The arena's blocks over the region about to be bound to its import (EnsureResident): noted
    /// and logged once, from then on accesses through the arena there alias gx_own's.
    void GxArenaBound();
    [[nodiscard]] bool InGxRegion(VAddr addr, u64 size) const noexcept {
        return gx_ready.load(std::memory_order_acquire) && size != 0 &&
               (addr >> block_shift) >= gx_first && ((addr + size - 1) >> block_shift) < gx_end;
    }
    /// A binding from the region's blocks into the tail page (what the game binds at the
    /// region's end: 256 KiB and 16 bytes past it at 1.09's title screen).
    [[nodiscard]] bool InGxTail(VAddr addr, u64 size) const noexcept {
        const VAddr tail_end = gx_tail_end.load(std::memory_order_acquire);
        return tail_end != 0 && size != 0 && (addr >> block_shift) >= gx_first &&
               addr + size <= tail_end && ((addr + size - 1) >> block_shift) >= gx_end;
    }
    [[nodiscard]] bool GxOverlaps(VAddr addr, u64 size) const noexcept {
        return gx_ready.load(std::memory_order_acquire) && size != 0 &&
               (addr >> block_shift) < gx_end && ((addr + size - 1) >> block_shift) >= gx_first;
    }
    static void CountGxUse(GxUse use, u64 size) {
        gx_ring_calls[use].fetch_add(1, std::memory_order_relaxed);
        gx_ring_bytes[use].fetch_add(size, std::memory_order_relaxed);
    }
    /// The import's thread; last, so that it is waited for before anything it writes goes.
    std::future<void> gx_import;
};

} // namespace VideoCore

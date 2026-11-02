// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <thread>
#include <array>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <magic_enum/magic_enum.hpp>
#include "bbport_copy.h"
#include "bbport_platform.h"
#include "bbport_toggles.h"
#include "bbport_sections.h"
#include "bbport_free_check.h"
#include "bbport_guest_memory.h"
#include "bbport_gx_ring.h"
#include "common/alignment.h"
#include "core/memory.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/buffer_cache/memory_tracker.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_gpu_savings.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scene_resolution.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"

#include <mutex>
#ifndef _WIN32
#include <pthread.h>
#endif
#include <x86intrin.h>
#include <vk_mem_alloc.h>

extern "C" int runtime_memory_vma_info(uintptr_t address, int* prot, int* type, uintptr_t* end);
extern "C" int runtime_memory_direct_phys(uintptr_t address, uint64_t* phys, uintptr_t* end);

namespace VideoCore {

namespace {
// bbport: the tick of the newest GPU write per 64 KiB of guest memory, per 1 MiB for writes over
// 16 MiB (ObtainBuffer with is_written: the only place ranges become GPU-modified). A readback
// waits for that submission only and copies on the readback queue, not behind the queued
// frames; a coarser grain let a buffer the GPU writes every frame hold up its neighbours.
// Read for every small constant binding of every draw (the GPU command thread, thousands of times
// a frame) while the draw recording thread notes writes: lock-free sparse page tables (a mutex and
// hash maps here took a third of that thread). Guest addresses below 2^40.
struct GpuWriteTicks {
    static constexpr u64 FineShift = 16, CoarseShift = 20;
    static constexpr u64 FineLimit = 16_MB;
    static constexpr u64 AddressBits = 40, LeafBits = 12, LeafMask = (1ull << LeafBits) - 1;
    struct Table {
        explicit Table(u64 shift_)
            : shift{shift_}, leaves((1ull << (AddressBits - shift_)) >> LeafBits) {}
        u64 shift;
        /// Page -> tick of the newest write; a leaf of 4096 pages is made at its first write.
        std::vector<std::atomic<std::atomic<u64>*>> leaves;
        std::atomic<u64>* Leaf(u64 page) const {
            return leaves[page >> LeafBits].load(std::memory_order_acquire);
        }
    };
    Table fine{FineShift};   ///< 64 KiB pages
    Table coarse{CoarseShift}; ///< 1 MiB pages (writes over FineLimit)
    std::mutex mutex;          ///< leaf creation
    void Note(u64 address, u64 size, u64 tick) {
        if (size == 0 || (address + size - 1) >> AddressBits) {
            return;
        }
        auto& table = size <= FineLimit ? fine : coarse;
        for (u64 page = address >> table.shift; page <= (address + size - 1) >> table.shift;
             ++page) {
            std::atomic<u64>* leaf = table.Leaf(page);
            if (!leaf) {
                std::scoped_lock lk{mutex};
                leaf = table.Leaf(page);
                if (!leaf) {
                    leaf = new std::atomic<u64>[1ull << LeafBits]{};
                    table.leaves[page >> LeafBits].store(leaf, std::memory_order_release);
                }
            }
            leaf[page & LeafMask].store(tick, std::memory_order_release);
        }
    }
    /// The newest write tick in the range, 0 when none is known.
    u64 Newest(u64 address, u64 size) const {
        if (size == 0 || (address + size - 1) >> AddressBits) {
            return 0;
        }
        u64 newest = 0;
        for (const Table* table : {&fine, &coarse}) {
            const u64 last = (address + size - 1) >> table->shift;
            for (u64 page = address >> table->shift; page <= last; ++page) {
                if (const std::atomic<u64>* leaf = table->Leaf(page)) {
                    newest = std::max(newest, leaf[page & LeafMask].load(std::memory_order_acquire));
                } else {
                    page |= LeafMask; // nothing written in this leaf
                }
            }
        }
        return newest;
    }
};
GpuWriteTicks& WriteTicks() {
    static GpuWriteTicks write_ticks;
    return write_ticks;
}

// bbport (BB_READBACK_LOG): the newest GPU-written bindings, to show which writes a readback that
// waited for the GPU overlapped (a binding wider than what the shader writes would show here).
struct WrittenBinding {
    u64 address, size, tick;
};
std::array<WrittenBinding, 4096> written_bindings;
std::atomic<u64> written_head{0};

// bbport: BB_READBACK_LOG=path — every download of GPU-written buffer memory back to guest
// memory (a CPU write to a range the GPU wrote): who asked, where, how much, how long it took,
// how old the GPU's newest write there was, and whether the readback queue served it.
struct ReadbackLog {
    FILE* file = nullptr;
    std::mutex mutex;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};
ReadbackLog* Readbacks() {
    static ReadbackLog* const log = [] () -> ReadbackLog* {
        const char* path = std::getenv("BB_READBACK_LOG");
        if (!path || !*path) {
            return nullptr;
        }
        auto* readbacks = new ReadbackLog;
        readbacks->file = std::fopen(path, "w");
        if (!readbacks->file) {
            delete readbacks;
            return nullptr;
        }
        std::fprintf(readbacks->file, "t_s,requester,address,window_kb,downloaded_kb,copies,is_write,"
                                      "wait_ms,ticks_since_write,write_done,readback_queue\n");
        return readbacks;
    }();
    return log;
}
thread_local const char* readback_requester = "";
thread_local bool readback_is_write = false;

/// bbport: copies finished GPU data out of an arena on the readback queue (Instance), waiting
/// only for those writes (the timeline semaphore at their tick), not for the queued frames.
class FastReadback {
public:
    explicit FastReadback(const Vulkan::Instance& instance)
        : device{instance.GetDevice()}, queue{instance.GetReadbackQueue()},
          allocator{instance.GetAllocator()} {
        if (!queue) {
            return;
        }
        pool = Vulkan::Check(device.createCommandPoolUnique({
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = instance.GetReadbackQueueFamilyIndex(),
        }));
        cmdbuf = Vulkan::Check(device.allocateCommandBuffers({
            .commandPool = *pool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1,
        }))[0];
        fence = Vulkan::Check(device.createFenceUnique({}));
    }
    ~FastReadback() {
        if (buffer) {
            vmaDestroyBuffer(allocator, buffer, allocation);
        }
    }

    [[nodiscard]] bool Available() const noexcept {
        return bool(queue);
    }

    /// Copies `copies` (destination offsets from 0) of `arena` once `semaphore` reaches `tick`;
    /// returns the host copy, or null on failure (the caller takes the regular path).
    const u8* Copy(const Buffer* arena, std::span<const vk::BufferCopy> copies, u64 total,
                   vk::Semaphore semaphore, u64 tick) {
        if (!Ensure(total)) {
            return nullptr;
        }
        Vulkan::Check(cmdbuf.reset());
        Vulkan::Check(cmdbuf.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}));
        cmdbuf.copyBuffer(arena->Handle(), vk::Buffer{buffer}, copies);
        const vk::MemoryBarrier2 to_host{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eHost,
            .dstAccessMask = vk::AccessFlagBits2::eHostRead,
        };
        cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &to_host});
        Vulkan::Check(cmdbuf.end());
        const vk::SemaphoreSubmitInfo wait{
            .semaphore = semaphore,
            .value = tick,
            .stageMask = vk::PipelineStageFlagBits2::eTransfer,
        };
        const vk::CommandBufferSubmitInfo command{.commandBuffer = cmdbuf};
        const vk::SubmitInfo2 submit{
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &wait,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &command,
        };
        if (queue.submit2(submit, *fence) != vk::Result::eSuccess) {
            return nullptr;
        }
        const auto result = device.waitForFences(*fence, vk::True, UINT64_MAX);
        Vulkan::Check(device.resetFences(*fence));
        if (result != vk::Result::eSuccess) {
            return nullptr;
        }
        vmaInvalidateAllocation(allocator, allocation, 0, VK_WHOLE_SIZE);
        return mapped;
    }

private:
    bool Ensure(u64 size) {
        if (size <= capacity) {
            return true;
        }
        if (buffer) {
            vmaDestroyBuffer(allocator, buffer, allocation);
            buffer = VK_NULL_HANDLE;
            capacity = 0;
        }
        const u64 new_capacity = std::max<u64>(std::bit_ceil(size), 1_MB);
        const VkBufferCreateInfo buffer_ci{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = new_capacity,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        const VmaAllocationCreateInfo alloc_ci{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        };
        VmaAllocationInfo info{};
        if (vmaCreateBuffer(allocator, &buffer_ci, &alloc_ci, &buffer, &allocation, &info) !=
            VK_SUCCESS) {
            buffer = VK_NULL_HANDLE;
            return false;
        }
        mapped = static_cast<u8*>(info.pMappedData);
        capacity = new_capacity;
        return true;
    }

    vk::Device device;
    vk::Queue queue;
    VmaAllocator allocator;
    vk::UniqueCommandPool pool;
    vk::CommandBuffer cmdbuf;
    vk::UniqueFence fence;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    u8* mapped = nullptr;
    u64 capacity = 0;
};
std::mutex fast_readback_mutex;

// bbport: BB_GPU_WRITE_TWINS=1 — a guest write into a page that holds bytes the GPU is still
// writing (counters, small compute outputs rewritten every frame) does not wait for the GPU: the
// guest's copy of those bytes is kept as a "twin" and the page becomes CPU-modified at once.
// Uploads leave the GPU's bytes in the arena while the guest bytes still match their twin (the
// guest did not write them); downloads do not overwrite guest bytes that no longer match it (the
// guest wrote them since). Only pages with at most BB_GPU_WRITE_TWINS_MAX (1024) GPU-written
// bytes: larger outputs (vertices the CPU reads back, FaceGen) keep waiting for the GPU.
bool TwinsEnabled() {
    static const bool enabled = [] {
        const char* env = std::getenv("BB_GPU_WRITE_TWINS");
        return env && env[0] == '1';
    }();
    return enabled;
}
u64 TwinsMaxBytes() {
    static const u64 max_bytes = [] {
        const char* env = std::getenv("BB_GPU_WRITE_TWINS_MAX");
        return env ? std::strtoull(env, nullptr, 10) : 1024ull;
    }();
    return max_bytes;
}
std::mutex twins_mutex;
std::map<VAddr, std::vector<u8>> twins; ///< start -> guest bytes when the twin was made
std::atomic<u64> twin_faults{0}, twin_kept_bytes{0}, twin_guest_bytes{0};

enum class TwinRun { None, Unchanged, Changed };

/// Calls func(start, end, TwinRun) over [begin, end) in order (twins_mutex held).
template <typename Func>
void ForEachTwinRun(VAddr begin, VAddr end, Func&& func) {
    VAddr cursor = begin;
    auto it = twins.upper_bound(begin);
    if (it != twins.begin()) {
        --it;
    }
    for (; it != twins.end() && it->first < end; ++it) {
        const VAddr start = it->first, stop = it->first + it->second.size();
        if (stop <= begin) {
            continue;
        }
        const VAddr from = std::max(start, begin), to = std::min(stop, end);
        if (cursor < from) {
            func(cursor, from, TwinRun::None);
        }
        const u8* guest = reinterpret_cast<const u8*>(from);
        const u8* twin = it->second.data() + (from - start);
        VAddr run = from;
        bool same = guest[0] == twin[0];
        for (VAddr at = from + 1; at <= to; ++at) {
            const bool now_same = at < to && guest[at - from] == twin[at - from];
            if (at == to || now_same != same) {
                func(run, at, same ? TwinRun::Unchanged : TwinRun::Changed);
                run = at;
                same = now_same;
            }
        }
        cursor = to;
    }
    if (cursor < end) {
        func(cursor, end, TwinRun::None);
    }
}

/// Drops the twins' parts in [begin, end), keeping the parts outside it (twins_mutex held).
void EraseTwins(VAddr begin, VAddr end) {
    auto it = twins.upper_bound(begin);
    if (it != twins.begin()) {
        --it;
    }
    while (it != twins.end() && it->first < end) {
        const VAddr start = it->first, stop = it->first + it->second.size();
        if (stop <= begin) {
            ++it;
            continue;
        }
        std::vector<u8> bytes = std::move(it->second);
        it = twins.erase(it);
        if (start < begin) {
            twins[start] = std::vector<u8>(bytes.begin(), bytes.begin() + (begin - start));
        }
        if (stop > end) {
            twins[end] = std::vector<u8>(bytes.begin() + (end - start), bytes.end());
        }
    }
}

/// bbport: BB_PREUPLOAD — background upload of the game's GPU memory into the arena ahead of use.
/// 1 (run.sh default): only memory already resident in the arena that the game rewrote, and
/// textures from the arena only where it is resident: no new VRAM. 2: all of the game's GPU
/// memory, textures always from the arena (~3 GB more VRAM, the shared memory on a Steam Deck).
/// 0: off.
int PreuploadMode() {
    static const int mode = [] {
        const char* env = std::getenv("BB_PREUPLOAD");
        return env && (env[0] == '1' || env[0] == '2') ? env[0] - '0' : 0;
    }();
    return mode;
}
bool PreuploadEnabled() {
    return PreuploadMode() != 0;
}
} // namespace

/// bbport BB_GX_IN_PLACE: the game's GX region read through a buffer of its own, within the
/// Vulkan spec (default); BB_GX_OWN_BUFFER=0 binds its blocks in the arena to the import instead,
/// as 0.4.4 betas 5 to 8 did (NVIDIA's arena lists no memory type an import may use).
bool GxOwnBuffer() {
    static const bool on = [] {
        const char* env = std::getenv("BB_GX_OWN_BUFFER");
        return !(env && env[0] == '0');
    }();
    return on;
}

static constexpr size_t GDS_BUFFER_SIZE = 64_KB;
static constexpr size_t STREAM_BUFFER_SIZE = 128_MB;

static constexpr auto ARENA_USAGE =
    vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
    vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer |
    vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;

std::optional<u32> FindMemoryType(const vk::PhysicalDeviceMemoryProperties& properties,
                                  vk::MemoryPropertyFlags wanted, u32 memory_type_bits) {
    for (u32 i = 0; i < properties.memoryTypeCount; ++i) {
        if (((memory_type_bits >> i) & 1) == 0) {
            continue;
        }
        const auto flags = properties.memoryTypes[i].propertyFlags;
        if ((flags & wanted) == wanted) {
            return i;
        }
    }
    return std::nullopt;
}

BufferCache::BufferCache(const Vulkan::Instance& instance_, Vulkan::Scheduler& scheduler_,
                         Vulkan::Runtime& runtime_, AmdGpu::Liverpool* liverpool_,
                         TextureCache& texture_cache_, PageManager& tracker)
    : instance{instance_}, scheduler{scheduler_}, runtime{runtime_},
      staging_pool{runtime_.GetStagingPool()}, liverpool{liverpool_},
      memory{Core::Memory::Instance()}, texture_cache{texture_cache_},
      memory_tracker{std::make_unique<MemoryTracker>(tracker)},
      stream_buffer{instance, scheduler, MemoryType::Stream, STREAM_BUFFER_SIZE},
      gds_buffer{instance, 0, GDS_BUFFER_SIZE, MemoryType::Stream, "GDS Buffer"},
      memory_semaphore{instance} {
    // bbport BB_GX_IN_PLACE (default on; Windows: the region is imported from the game's memory
    // pool, which Linux cannot yet): the game's GX region in place once it exists.
    if (BbGxRing::InPlaceWanted()) {
        gx_state.store(u8(GxState::Waiting), std::memory_order_relaxed);
    }
    const std::array<u32, 2> families = {instance.GetGraphicsQueueFamilyIndex(),
                                         instance.GetReadbackQueueFamilyIndex()};
    const bool shared = bool(instance.GetReadbackQueue()); // as Buffer creates the arenas
    const vk::BufferCreateInfo probe_ci = {
        .flags =
            vk::BufferCreateFlagBits::eSparseBinding | vk::BufferCreateFlagBits::eSparseResidency,
        .size = ARENA_PAGE_SIZE,
        .usage = ARENA_USAGE,
        .sharingMode = shared ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
        .queueFamilyIndexCount = shared ? 2u : 0u,
        .pQueueFamilyIndices = shared ? families.data() : nullptr,
    };
    const vk::DeviceBufferMemoryRequirements req_info = {
        .pCreateInfo = &probe_ci,
    };
    const auto device = instance.GetDevice();
    const auto reqs = device.getBufferMemoryRequirements(req_info).memoryRequirements;
    block_size = Common::AlignUp(std::max<u64>(reqs.alignment, MIN_BLOCK_SIZE), reqs.alignment);
    ASSERT_MSG(std::popcount(block_size) == 1, "Sparse block size {} is not a power of 2",
               block_size);
    block_shift = std::bit_width(block_size) - 1;
    blocks_per_arena_page = ARENA_PAGE_SIZE / block_size;
    {
        // bbport: a request across two arenas merges them (GetArena's migration); after one,
        // objects and textures went missing and loads hung (2026-10-15 reports, all with the
        // game's memory past 4 GiB: 9-12 GiB of direct memory with larger heaps). A new arena
        // takes its whole aligned group of up to 4 pages (16 GiB; a sparse buffer reserves
        // addresses, not memory) when the driver allows buffers that large.
        const auto props = instance.GetPhysicalDevice()
                               .getProperties2<vk::PhysicalDeviceProperties2,
                                               vk::PhysicalDeviceVulkan13Properties>();
        const u64 max_buffer = props.get<vk::PhysicalDeviceVulkan13Properties>().maxBufferSize;
        while (arena_group_pages < 4 && (arena_group_pages * 2) * ARENA_PAGE_SIZE <= max_buffer) {
            arena_group_pages *= 2;
        }
        std::printf("Guest memory: GPU arenas of %llu GiB (largest buffer %llu GiB)\n",
                    (unsigned long long)((arena_group_pages * ARENA_PAGE_SIZE) >> 30),
                    (unsigned long long)(max_buffer >> 30));
    }
    blocks_per_arena_page_shift = ARENA_PAGE_BITS - block_shift;
    group_use.assign(u64{1} << (ADDRESS_SPACE_BITS - USE_GROUP_BITS), 0);
    arena_memory_type_index =
        FindMemoryType(instance.GetMemoryProperties(), vk::MemoryPropertyFlagBits::eDeviceLocal,
                       reqs.memoryTypeBits)
            .value();
    // bbport: every block of the game's memory the GPU uses gets VRAM and keeps it, so a 4 GB card
    // ran out (exit 23). System memory the arena takes, read by the GPU over PCIe, holds new blocks
    // once VRAM is tight instead (not on an integrated GPU, where VRAM is the same memory).
    if (!instance.IsIntegrated()) {
        const auto& props = instance.GetMemoryProperties();
        for (u32 i = 0; i < props.memoryTypeCount; ++i) {
            const auto heap = props.memoryHeaps[props.memoryTypes[i].heapIndex];
            if ((reqs.memoryTypeBits & (1u << i)) != 0 &&
                !(heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal)) {
                system_memory_type_index = i;
                break;
            }
        }
    }
    if (system_memory_type_index) {
        std::printf("Guest memory: the game's data in VRAM (memory type %u), in system memory "
                    "(type %u) while VRAM is tight\n",
                    arena_memory_type_index, *system_memory_type_index);
    } else {
        std::printf("Guest memory: the game's data in VRAM (memory type %u); no system memory "
                    "type for it on this GPU\n",
                    arena_memory_type_index);
    }

    const u64 bda_pagetable_size =
        (blocks_per_arena_page * NUM_ARENA_PAGES) * sizeof(vk::DeviceAddress);
    fault_manager = std::make_unique<FaultManager>(instance, scheduler, *this, block_shift,
                                                   blocks_per_arena_page * NUM_ARENA_PAGES);
    bda_pagetable_buffer = std::make_unique<Buffer>(
        instance, 0, bda_pagetable_size, MemoryType::DeviceLocal, "BDA Page Table Buffer");
    runtime.FillBuffer(bda_pagetable_buffer.get(), 0u, bda_pagetable_size, 0u);
}

BufferCache::~BufferCache() {
    // bbport BB_GX_IN_PLACE: the import's thread done, then its buffer and memory freed. The GPU
    // is idle by now (Presenter::~Presenter finishes its schedulers first); arenas bound to the
    // memory (GxArenaBound) go after this, unused.
    if (gx_import.valid()) {
        gx_import.wait();
    }
    gx_own.store(nullptr, std::memory_order_release);
    gx_view.reset();
    if (gx_memory) {
        const vk::Device device = instance.GetDevice();
        if (gx_buffer) {
            device.destroyBuffer(gx_buffer);
        }
        device.freeMemory(gx_memory);
    }
}

void BufferCache::InvalidateMemory(VAddr device_addr, u64 size, bool assume_locks) {
    memory_tracker->InvalidateRegion(device_addr, size, [this, device_addr, size, assume_locks] {
        ReadMemory(device_addr, size, true, assume_locks);
    });
}

// bbport: the game fills its per-frame buffers (constants, skinning output) sequentially and
// every 4 KiB page cost a protection fault (~110k/s in Hunter's Nightmare, a fifth of each
// render worker's time in the kernel). A fault unprotects the aligned window around it instead;
// pages marked CPU-modified without being written only cost an upload when bound.
// Item: context = BufferCache, source = guest address, destination = host pointer, size,
// extra = the Buffer whose mapping holds the destination (flushed after the copy).
void BufferCache::RunGuestCopy(const BbCopy::Item& item) {
    auto* cache = static_cast<BufferCache*>(item.context);
    auto* dst = reinterpret_cast<u8*>(item.destination);
    cache->memory->CopySparseMemory(item.source, dst, item.size);
    auto* buffer = reinterpret_cast<Buffer*>(item.extra);
    buffer->Flush(dst - buffer->mapped_data.data(), item.size);
}

// Small guest copies run on the recording thread (it spins for work: no wakeup, and it is
// idle most of the time); PoolSmallCopies (toggle 524288) batches them for the copy threads.
// Each source is noted once the copy is queued (a WaitHostCopies started after the note covers it).
void BufferCache::SmallGuestCopy(const BbCopy::Item& item) {
    if (!scheduler.IsRecordingDeferred()) {
        // bbport: commands recorded on this thread (direct mode: the draw or dispatch after the
        // upscaler's passes; or BB_VK_RECORD_THREAD=0): the copy runs now, before the commands
        // that read it. In this thread's copy batch it ran at the next submission at the
        // earliest, possibly after a WRITE_DATA had changed its source (that wait runs only the
        // GpuComm thread's own batch). Suspected in the vertex explosions with
        // BB_VK_RECORD_THREAD=0 (issue #39).
        item.run(item);
    } else if (!BbToggle::Disabled(BbToggle::PoolSmallCopies)) {
        scheduler.RecordHostCopy([item] { item.run(item); });
    } else {
        BbCopy::QueueCopy(item);
    }
    scheduler.NoteHostCopySource(item.source, item.size);
}

void BufferCache::ExtendWriteFault(VAddr device_addr) {
    static const u64 window = [] {
        const char* env = std::getenv("BB_FAULT_WINDOW");
        const u64 kib = env ? std::strtoull(env, nullptr, 10) : 256;
        return std::bit_ceil(std::clamp<u64>(kib, 4, 1024)) * 1024;
    }();
    if (window <= TRACKER_BYTES_PER_PAGE || BbToggle::Disabled(BbToggle::FaultWindow)) {
        return;
    }
    memory_tracker->ExtendWriteFault(Common::AlignDown(device_addr, window), window);
}

void BufferCache::ReadMemory(VAddr device_addr, u64 size, bool is_write, bool assume_locks) {
    BbStats::readbacks.fetch_add(1, std::memory_order_relaxed);
    std::array<char, 16> requester{};
    if (Readbacks()) {
#ifdef _WIN32
        std::snprintf(requester.data(), requester.size(), "tid:%u", BbPlatform::CurrentThreadId());
#else
        pthread_getname_np(pthread_self(), requester.data(), requester.size());
#endif
    }
    const auto flush_request = [this, device_addr, size, is_write, requester] {
        readback_requester = requester.data();
        readback_is_write = is_write;
        const u32 first_block = device_addr >> block_shift;
        const u32 last_block = (device_addr + size - 1) >> block_shift;
        const auto* arena = GetArena(first_block, last_block);

        // GPU-modified ranges come as many small scattered islands,
        // so the download is widened to a window around the request
        constexpr u64 WindowSize = 512_KB;
        const VAddr arena_end = arena->cpu_addr + arena->size_bytes;
        const VAddr window_start =
            std::max<VAddr>(Common::AlignDown(device_addr, WindowSize), arena->cpu_addr);
        const VAddr window_end = std::min<VAddr>(
            std::max<VAddr>(window_start + WindowSize, device_addr + size), arena_end);
        // bbport (BB_GPU_WRITE_TWINS): bytes the GPU is still writing on the faulting page are
        // twinned instead of waited for (see TwinsEnabled).
        if (is_write && TwinsEnabled()) {
            const VAddr page_begin = Common::AlignDown(device_addr, TRACKER_BYTES_PER_PAGE);
            const VAddr page_end = Common::AlignUp(device_addr + size, TRACKER_BYTES_PER_PAGE);
            u64 tick = 0, gpu_bytes = 0;
            gpu_modified_ranges.ForEachInRange(page_begin, page_end - page_begin,
                                               [&](VAddr start, VAddr end) {
                                                   gpu_bytes += end - start;
                                                   tick = std::max(tick, WriteTicks().Newest(
                                                                             start, end - start));
                                               });
            if (gpu_bytes != 0 && gpu_bytes <= TwinsMaxBytes() && tick != 0 &&
                !scheduler.IsFree(tick)) {
                {
                    std::scoped_lock lk{twins_mutex};
                    gpu_modified_ranges.ForEachInRange(
                        page_begin, page_end - page_begin, [&](VAddr start, VAddr end) {
                            // Parts with a twin keep their older snapshot: the guest bytes may
                            // have been written since it was taken.
                            ForEachTwinRun(start, end, [&](VAddr from, VAddr to, TwinRun run) {
                                if (run == TwinRun::None) {
                                    const u8* guest = reinterpret_cast<const u8*>(from);
                                    twins[from] = std::vector<u8>(guest, guest + (to - from));
                                }
                            });
                        });
                }
                memory_tracker->MarkRegionAsCpuModified(device_addr, size);
                twin_faults.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        // bbport (BB_GPU_WRITE_TWINS): the window around the fault is downloaded only when the GPU has
        // done its writes there; else just the faulting page (the other pages stay GPU-modified
        // and protected), instead of waiting for GPU writes that have nothing to do with it.
        VAddr download_start = window_start, download_end = window_end;
        if (is_write && TwinsEnabled()) {
            u64 window_tick = 0;
            gpu_modified_ranges.ForEachInRange(
                window_start, window_end - window_start, [&](VAddr start, VAddr end) {
                    window_tick = std::max(window_tick, WriteTicks().Newest(start, end - start));
                });
            if (window_tick != 0 && !scheduler.IsFree(window_tick)) {
                download_start = Common::AlignDown(device_addr, TRACKER_BYTES_PER_PAGE);
                download_end = Common::AlignUp(device_addr + size, TRACKER_BYTES_PER_PAGE);
            }
        }
        DownloadMemory(arena, download_start, download_end - download_start);
        if (is_write) {
            memory_tracker->MarkRegionAsCpuModified(device_addr, size);
        }
    };
    if (assume_locks) {
        flush_request();
    } else {
        liverpool->SendCommand<true>(std::move(flush_request));
    }
}

void BufferCache::DownloadMemory(const Buffer* arena, VAddr device_addr, u64 size) {
    boost::container::small_vector<vk::BufferCopy, 1> copies;
    u64 total_size_bytes = 0;
    const VAddr arena_base = arena->cpu_addr;
    memory_tracker->ForEachDownloadRange<false>(device_addr, size, [&](u64 address, u64 size) {
        const auto add_download = [&](VAddr start, VAddr end) {
            const u64 new_offset = start - arena_base;
            const u64 new_size = end - start;
            copies.push_back(vk::BufferCopy{
                .srcOffset = new_offset,
                .dstOffset = total_size_bytes,
                .size = new_size,
            });
            // Align up to avoid cache conflicts
            constexpr u64 align = 64ULL;
            constexpr u64 mask = ~(align - 1ULL);
            total_size_bytes += (new_size + align - 1) & mask;
        };
        gpu_modified_ranges.ForEachInRange(address, size, add_download);
        gpu_modified_ranges.Subtract(address, size);
    });
    if (total_size_bytes == 0) {
        return;
    }
    const auto write_range = [&](VAddr guest, const u8* src, u64 bytes) {
        BbFreeCheck::Check(guest, bytes, src, BbFreeCheck::BufferDownload);
        memory->TryWriteBacking(std::bit_cast<u8*>(guest), src, bytes);
    };
    const auto write_back = [&](const u8* data, u64 data_offset) {
        std::unique_lock twins_lock{twins_mutex, std::defer_lock};
        if (TwinsEnabled()) {
            twins_lock.lock();
        }
        for (const auto& copy : copies) {
            const VAddr guest = arena_base + copy.srcOffset;
            const u8* src = data + (copy.dstOffset - data_offset);
            if (!TwinsEnabled() || twins.empty()) {
                write_range(guest, src, copy.size);
                continue;
            }
            // bbport (BB_GPU_WRITE_TWINS): guest bytes written since their twin was made stay;
            // the page is CPU-modified, so its next upload carries them to the arena.
            ForEachTwinRun(guest, guest + copy.size, [&](VAddr from, VAddr to, TwinRun run) {
                if (run != TwinRun::Changed) {
                    write_range(from, src + (from - guest), to - from);
                }
            });
            EraseTwins(guest, guest + copy.size);
        }
    };
    const auto wait_start = std::chrono::steady_clock::now();
    const u64 tick_before = scheduler.CurrentTick();
    // bbport: the readback queue copies the data once the GPU has done the newest write into
    // it (waiting for that submission only when it is still queued); the graphics queue would
    // first run all queued work (~a frame: 20-30 ms stutters whenever the game wrote into
    // memory a GPU pass had written).
    u64 write_tick = 0;
    for (const auto& copy : copies) {
        write_tick = std::max(write_tick, WriteTicks().Newest(arena_base + copy.srcOffset, copy.size));
    }
    const bool write_done = write_tick != 0 && scheduler.IsFree(write_tick);
    bool fast = false;
    static FastReadback readback{instance};
    if (write_tick != 0 && readback.Available()) {
        if (!write_done) {
            scheduler.Wait(write_tick);
        }
        {
            std::scoped_lock lk{fast_readback_mutex};
            if (const u8* data = readback.Copy(arena, copies, total_size_bytes,
                                               scheduler.GetWorkSemaphore()->Handle(),
                                               write_tick)) {
                write_back(data, 0);
                fast = true;
            }
        }
    }
    if (!fast) {
        const auto download =
            staging_pool.Request(total_size_bytes, VideoCore::MemoryType::HostCached);
        for (auto& copy : copies) {
            copy.dstOffset += download.offset;
        }
        runtime.CopyBuffer(arena, download.buffer, copies);
        scheduler.Finish();
        download.buffer->Invalidate(download.offset, download.size);
        write_back(download.mapped, download.offset);
    }
    if (auto* log = Readbacks()) {
        const auto now = std::chrono::steady_clock::now();
        std::scoped_lock lk{log->mutex};
        std::fprintf(log->file, "%.4f,%s,%#llx,%llu,%llu,%zu,%d,%.3f,%lld,%d,%d\n",
                     std::chrono::duration<double>(now - log->start).count(), readback_requester,
                     (unsigned long long)device_addr, (unsigned long long)(size >> 10),
                     (unsigned long long)(total_size_bytes >> 10), copies.size(),
                     readback_is_write ? 1 : 0,
                     std::chrono::duration<double, std::milli>(now - wait_start).count(),
                     write_tick ? (long long)(tick_before - write_tick) : -1LL, write_done ? 1 : 0,
                     fast ? 1 : 0);
        if (!write_done && write_tick != 0) {
            // The GPU-written bindings this readback waited for (newest first).
            const u64 n = written_head.load();
            u32 shown = 0;
            for (u64 i = n; i-- > (n > written_bindings.size() ? n - written_bindings.size() : 0) &&
                            shown < 4;) {
                const WrittenBinding b = written_bindings[i % written_bindings.size()];
                bool overlaps = false;
                u64 cpu_lo = ~0ull, cpu_hi = 0;
                for (const auto& copy : copies) {
                    const u64 a = arena_base + copy.srcOffset;
                    overlaps |= b.address < a + copy.size && a < b.address + b.size;
                    cpu_lo = std::min(cpu_lo, a);
                    cpu_hi = std::max(cpu_hi, a + copy.size);
                }
                if (overlaps) {
                    std::fprintf(log->file,
                                 "# waited for GPU write %#llx+%llu (%lld ticks before), downloaded "
                                 "%#llx..%#llx\n",
                                 (unsigned long long)b.address, (unsigned long long)b.size,
                                 (long long)(tick_before - b.tick), (unsigned long long)cpu_lo,
                                 (unsigned long long)cpu_hi);
                    ++shown;
                }
            }
        }
        std::fflush(log->file);
    }
    memory_tracker->UnmarkRegionAsGpuModified(device_addr, size);
}

namespace {
// bbport: BB_BUFFER_STATS=1 — how buffer bindings reach the GPU, by guest region (256 MiB),
// printed every 5 s: small read-only copies into the stream buffer, arena bindings, and the
// bytes those re-upload after CPU writes. Input for engine-level short paths.
struct BufferStats {
    struct Region {
        u64 stream_count{}, stream_bytes{}, arena_count{}, arena_bytes{}, upload_bytes{};
    };
    std::map<u64, Region> regions;
    /// Hot window: frame of the last binding per 64 KiB block, and the distribution of the
    /// frames between uses (1, 2, 3, 4, 5-8, 9-16, 17+): the ring's reuse distance.
    std::unordered_map<u64, u64> last_use;
    std::array<u64, 7> reuse{};
    std::chrono::steady_clock::time_point window = std::chrono::steady_clock::now();
    u64 calls = 0;
};
/// 256 MiB regions; 1 MiB inside the hot 0x104xxxxxxx window (the engine's frame data).
BufferStats& Stats();
void NoteHotUse(VAddr address, u32 size) {
    if ((address >> 28) != 0x104) {
        return;
    }
    auto& st = Stats();
    const u64 frame = BbStats::gpu_frames.load(std::memory_order_relaxed);
    for (u64 block = address >> 16; block <= (address + size - 1) >> 16; ++block) {
        auto [it, inserted] = st.last_use.try_emplace(block, frame);
        if (!inserted && it->second != frame) {
            const u64 d = frame - it->second;
            ++st.reuse[d <= 4 ? d - 1 : d <= 8 ? 4 : d <= 16 ? 5 : 6];
            it->second = frame;
        }
    }
}
u64 RegionKey(VAddr address) {
    return (address >> 28) == 0x104 ? (address >> 20) | (1ull << 40) : address >> 28;
}
bool BufferStatsEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("BB_BUFFER_STATS");
        return value && value[0] == '1';
    }();
    return enabled;
}
BufferStats& Stats() {
    static BufferStats stats;
    return stats;
}
void PrintBufferStats() {
    auto& st = Stats();
    if ((++st.calls & 4095) != 0) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - st.window).count();
    if (seconds < 5.0) {
        return;
    }
    std::printf("Buffer stats (%.1f s, per second): region, stream copies/bytes, arena bindings, "
                "re-uploaded bytes\n", seconds);
    std::printf("  hot window reuse distance in frames (1,2,3,4,5-8,9-16,17+): %llu %llu %llu %llu "
                "%llu %llu %llu\n",
                (unsigned long long)st.reuse[0], (unsigned long long)st.reuse[1],
                (unsigned long long)st.reuse[2], (unsigned long long)st.reuse[3],
                (unsigned long long)st.reuse[4], (unsigned long long)st.reuse[5],
                (unsigned long long)st.reuse[6]);
    st.reuse = {};
    for (const auto& [region, r] : st.regions) {
        const u64 base = (region >> 40) ? (region & ((1ull << 40) - 1)) << 20 : region << 28;
        if ((region >> 40) && r.stream_bytes + r.upload_bytes < 1e6 * seconds) {
            continue; // quiet MiB of the hot window
        }
        std::printf("  %#012llx: %8.0f copies %8.2f MB, %8.0f arena %8.2f MB bound, %8.2f MB uploads\n",
                    static_cast<unsigned long long>(base), r.stream_count / seconds,
                    r.stream_bytes / seconds / 1e6, r.arena_count / seconds,
                    r.arena_bytes / seconds / 1e6,
                    r.upload_bytes / seconds / 1e6);
    }
    st.regions.clear();
    st.window = now;
}
} // namespace

void BufferCache::NoteImageMadeForCopy(VAddr address, u64 size) {
    if (address == 0 || size == 0) {
        return;
    }
    std::scoped_lock lock{made_for_copy_mutex};
    MadeForCopy* slot = nullptr;
    for (auto& range : made_for_copy) {
        const VAddr begin = range.begin.load(std::memory_order_relaxed);
        if (begin == address && range.end.load(std::memory_order_relaxed) == address + size) {
            return; // already noted (told or not)
        }
        if (!slot && begin == 0) {
            slot = &range;
        }
    }
    if (slot) {
        made_for_copy_live.fetch_add(1, std::memory_order_relaxed);
    } else {
        slot = &made_for_copy[made_for_copy_next++ % made_for_copy.size()];
    }
    // Readers skip a slot whose begin changes while they look at it.
    slot->begin.store(0, std::memory_order_release);
    slot->end.store(address + size, std::memory_order_release);
    slot->told.store(false, std::memory_order_release);
    slot->begin.store(address, std::memory_order_release);
}

void BufferCache::CheckMadeForCopy(VAddr device_addr, u32 size, bool is_written) {
    const VAddr end = device_addr + size;
    for (auto& range : made_for_copy) {
        const VAddr begin = range.begin.load(std::memory_order_acquire);
        const VAddr range_end = range.end.load(std::memory_order_acquire);
        if (begin == 0 || begin >= end || device_addr >= range_end ||
            range.begin.load(std::memory_order_acquire) != begin) {
            continue;
        }
        if (is_written) {
            // The GPU writes that memory (or a download puts the image there): newer from now on.
            std::scoped_lock lock{made_for_copy_mutex};
            if (range.begin.load(std::memory_order_relaxed) == begin) {
                range.begin.store(0, std::memory_order_release);
                made_for_copy_live.fetch_sub(1, std::memory_order_relaxed);
            }
            continue;
        }
        // Once per range, and 32 lines a session at most.
        static std::atomic<u32> lines{0};
        if (!range.told.exchange(true, std::memory_order_relaxed) &&
            lines.fetch_add(1, std::memory_order_relaxed) < 32) {
            std::printf("Copy shader: memory of an image made for a copy is read as a buffer at "
                        "%#llx, %u bytes (the image holds newer data)\n",
                        static_cast<unsigned long long>(device_addr), size);
        }
    }
}

std::pair<const Buffer*, u64> BufferCache::ObtainBuffer(VAddr device_addr, u32 size,
                                                        bool is_written, bool is_texel_buffer) {
    BB_SECTION(ObtainBuffer);
    // 0.4.4 beta 14: raw reads of images made for a copy shader's copy (NoteImageMadeForCopy);
    // texel buffers are filled from the image. One load while there is none.
    if (made_for_copy_live.load(std::memory_order_relaxed) != 0 &&
        (is_written || !is_texel_buffer)) [[unlikely]] {
        CheckMadeForCopy(device_addr, size, is_written);
    }
    const bool stats = BufferStatsEnabled();
    const auto note_hle = [&](u32 path) {
        if (hle_obtain) {
            ++hle_path_calls[(is_written ? 3 : 0) + path];
            hle_path_bytes[(is_written ? 3 : 0) + path] += size;
        }
    };
    if (stats) {
        PrintBufferStats();
        NoteHotUse(device_addr, size);
    }
    // bbport BB_GX_IN_PLACE: the game's GX region, bound in place once imported (PollGxRegion):
    // what is bound inside it is read and written where it is. Small read-only bindings
    // (constants) are still copied when bound (the stream path below): commands decoded later
    // (WriteData, constant RAM dumps) change them on the CPU in decode order, and a draw decoded
    // before must keep its values (buffer renaming). GPU writes are noted for that: a range the
    // GPU has yet to write is not copied. Its first binding waits for the import.
    if (const u8 state = gx_state.load(std::memory_order_acquire);
        state == u8(GxState::Waiting) || state == u8(GxState::Importing) ||
        state == u8(GxState::Imported)) {
        PollGxRegion(device_addr, size);
    }
    // Read-only bindings reaching into the tail page (InGxTail) too, unless the GPU has written
    // there through the tracked path (its copy is then the newer): read in place, what the CPU
    // wrote is what the tracked copy would hold.
    const bool gx_tail =
        !is_written && InGxTail(device_addr, size) &&
        gx_own.load(std::memory_order_acquire) != nullptr &&
        !IsRegionGpuModified(gx_end << block_shift, device_addr + size - (gx_end << block_shift));
    const bool gx_in_place = InGxRegion(device_addr, size) || gx_tail;
    if (gx_in_place && device_addr < gx_head) [[unlikely]] {
        NoteBeforeGx(device_addr, size);
    }
    const Buffer* const gx = gx_in_place ? gx_own.load(std::memory_order_acquire) : nullptr;
    // 0.4.4 beta 21: whether the GPU has written the range or has a write there pending
    // (IsRegionGpuModified), asked once for the three paths below instead of once each. Only the
    // thread binding buffers makes a range GPU-modified (bindings for writes, the command
    // processor's writes in stream order); the GPU's progress and EnsureResident's housekeeping
    // only clear it. So a "no" holds for the whole binding, and a "yes" is asked again after the
    // in-place path (below). The tail check above asks about another range (the part in the tail
    // page).
    std::optional<bool> gpu_modified;
    const auto is_gpu_modified = [&] {
        if (!gpu_modified) {
            gpu_modified = IsRegionGpuModified(device_addr, size);
        }
        return *gpu_modified;
    };
    if (gx && (is_written || size > STREAM_THRESHOLD || is_gpu_modified())) {
        // Through the region's own buffer: no arena, residency or sparse binding. A write counts
        // as GPU-modified until the GPU has done it (IsRegionGpuModified).
        if (is_written) {
            WriteTicks().Note(device_addr, size, scheduler.CurrentTick());
            BbStats::bound_in_place_written_bytes.fetch_add(size, std::memory_order_relaxed);
        }
        CountGxUse(is_written ? GxWritten : is_texel_buffer ? GxTexel : GxInPlace, size);
        BbStats::bound_in_place_bytes.fetch_add(size, std::memory_order_relaxed);
        TraceBinding(device_addr, size, is_written, is_written ? 0 : 2);
        note_hle(0);
        const std::pair<const Buffer*, u64> target{gx, gx->Offset(device_addr)};
        if (is_texel_buffer && !is_written) {
            SynchronizeMemoryFromImage(target.first, target.second, device_addr, size);
        }
        return target;
    }
    if (gx_in_place && (is_written || size > STREAM_THRESHOLD || is_gpu_modified())) {
        // Through the arena, bound to the region's import there (BB_GX_OWN_BUFFER=0).
        const u64 first_block = device_addr >> block_shift;
        const u64 last_block = (device_addr + size - 1) >> block_shift;
        const auto* arena = GetArena(first_block, last_block);
        EnsureResident(arena, first_block, last_block);
        if (is_written && size > 64_KB && BbStats::enabled) {
            NoteLargeGpuWrite(device_addr, size);
        }
        if (IsInPlace(device_addr, size)) {
            if (is_written) {
                WriteTicks().Note(device_addr, size, scheduler.CurrentTick());
            }
            CountGxUse(is_written ? GxWritten : is_texel_buffer ? GxTexel : GxInPlace, size);
            BbStats::bound_in_place_bytes.fetch_add(size, std::memory_order_relaxed);
            if (is_written) {
                BbStats::bound_in_place_written_bytes.fetch_add(size, std::memory_order_relaxed);
            }
            TraceBinding(device_addr, size, is_written, is_written ? 0 : 2);
            note_hle(0);
            const std::pair<const Buffer*, u64> target{arena, arena->Offset(device_addr)};
            if (is_texel_buffer && !is_written) {
                SynchronizeMemoryFromImage(target.first, target.second, device_addr, size);
            }
            return target;
        }
        // Not in place. Only a "yes" (or no answer yet) gets here: asked again below, since
        // EnsureResident may have evicted the blocks the GPU wrote, and the GPU may have done its
        // write meanwhile.
        gpu_modified.reset();
    }
    // For read-only buffers use device local stream buffer to reduce renderpass breaks.
    // The copy shader's sources too, up to 64 KiB (BB_HLE_STREAM_KB; 16 or less: as any buffer):
    // the game writes them into its per-frame upload area just before the copy, which reads them
    // once. Its merged spans (BB_COPY_MERGE_KB) mostly exceeded STREAM_THRESHOLD, so the tracked
    // path walked, uploaded and write-protected them, and the game's next lap over those pages
    // faulted: 38% of all write faults (its memcpy, 0x26adb90), and 18% more from the copy's range
    // list beside them (0x26b2810; 0.4.3 beta 5's profile). Here they are copied as they are, on
    // the copy threads, which guest fences wait for. A span the GPU wrote stays on the tracked path.
    static const u64 copy_source_limit = [] {
        const char* env = std::getenv("BB_HLE_STREAM_KB");
        const u64 kib = env ? std::strtoull(env, nullptr, 10) : 64;
        return std::max<u64>(std::min<u64>(kib, 1024) * 1024, STREAM_THRESHOLD);
    }();
    const u64 stream_limit = copy_source ? copy_source_limit : STREAM_THRESHOLD;
    // The game's GX region (bbport_gx_ring.h), counted by how its bindings are served (the stats).
    const bool in_gx_region = BbGxRing::Contains(device_addr, size);
    // Read-only bindings across the edge of the region's own buffer (part of them in it, part
    // in the arena): copied when bound, up to 8 MiB, rather than binding the arena there too.
    // Counted by edge: the first should see none (it is the game allocation's own start), the
    // last only bindings that reach the region's final 64 bytes (its last block is shared).
    constexpr u64 GxEdgeCopyLimit = 8_MB;
    const bool gx_edge = !gx_in_place && size <= GxEdgeCopyLimit && GxOverlaps(device_addr, size) &&
                         gx_own.load(std::memory_order_acquire) != nullptr;
    if (!is_written && (size <= stream_limit || gx_edge) && !is_gpu_modified()) {
        BB_SECTION(ObtainStream);
        note_hle(1);
        if (gx_edge) {
            const bool first_edge = device_addr < (gx_first << block_shift);
            CountGxUse(first_edge ? GxEdgeCopied : GxTailCopied, size);
            NoteGxEdge(device_addr, size, first_edge, is_texel_buffer);
        } else if (in_gx_region || gx_tail) {
            CountGxUse(GxCopied, size);
        }
        if (stats) {
            auto& region = Stats().regions[RegionKey(device_addr)];
            ++region.stream_count;
            region.stream_bytes += size;
        }
        // bbport: the guest data is copied on a copy thread, started now; submission and
        // guest-visible fences wait for it (Scheduler::WaitHostCopies).
        if (!stream_buffer.mapped_data.empty() &&
            !BbToggle::Disabled(BbToggle::DeferredStreamCopies)) {
            if (const auto offset = stream_buffer.Reserve(size, instance.UniformMinAlignment())) {
                SmallGuestCopy({
                    .run = &RunGuestCopy,
                    .context = this,
                    .source = device_addr,
                    .destination = reinterpret_cast<u64>(stream_buffer.mapped_data.data() + *offset),
                    .size = size,
                    .extra = reinterpret_cast<u64>(static_cast<Buffer*>(&stream_buffer)),
                });
                TraceBinding(device_addr, size, false, 3);
                return {&stream_buffer, *offset};
            }
        }
        const auto [data, offset] = stream_buffer.Map(size, instance.UniformMinAlignment());
        memory->CopySparseMemory(device_addr, data, size);
        stream_buffer.Commit();
        TraceBinding(device_addr, size, false, 3);
        return {&stream_buffer, offset};
    }
    if (in_gx_region || GxOverlaps(device_addr, size)) {
        CountGxUse(GxTracked, size);
    }
    const u64 first_block = device_addr >> block_shift;
    const u64 last_block = (device_addr + size - 1) >> block_shift;
    const auto* arena = GetArena(first_block, last_block);
    EnsureResident(arena, first_block, last_block);
    BB_SECTION(ObtainVram);
    note_hle(2);
    const u64 uploaded_before = BbStats::buffer_upload_bytes.load(std::memory_order_relaxed);
    SynchronizeMemory(arena, device_addr, size, is_written, is_texel_buffer);
    if (stats) {
        auto& region = Stats().regions[RegionKey(device_addr)];
        ++region.arena_count;
        region.arena_bytes += size;
        region.upload_bytes +=
            BbStats::buffer_upload_bytes.load(std::memory_order_relaxed) - uploaded_before;
    }
    if (is_written) {
        BB_SECTION(VramWritten);
        gpu_modified_ranges.Add(device_addr, size);
        WriteTicks().Note(device_addr, size, scheduler.CurrentTick());
        if (Readbacks()) {
            written_bindings[written_head.fetch_add(1) % written_bindings.size()] =
                WrittenBinding{device_addr, size, scheduler.CurrentTick()};
        }
    }
    if (GxOverlaps(device_addr, size) && gx_own.load(std::memory_order_acquire)) {
        // The region's memory reached through the arena too (GxArenaBound): barriers on both sides.
        runtime.NoteAliasedAccess(arena, arena->Offset(device_addr), size);
    }
    TraceBinding(device_addr, size, is_written, is_written ? 1 : 4);
    return {arena, arena->Offset(device_addr)};
}

std::optional<std::pair<const Buffer*, u64>> BufferCache::CommandWriteTarget(VAddr device_addr,
                                                                           u32 size) {
    if (!InGxRegion(device_addr, size)) {
        return std::nullopt;
    }
    // bbport BB_GX_IN_PLACE: inside the game's GX region, through its own buffer.
    if (const Buffer* gx = gx_own.load(std::memory_order_acquire);
        gx && InGxRegion(device_addr, size)) {
        if (device_addr < gx_head) [[unlikely]] {
            NoteBeforeGx(device_addr, size);
        }
        WriteTicks().Note(device_addr, size, scheduler.CurrentTick());
        return std::pair<const Buffer*, u64>{gx, gx->Offset(device_addr)};
    }
    const u64 first_block = device_addr >> block_shift;
    const u64 last_block = (device_addr + size - 1) >> block_shift;
    const auto* arena = GetArena(first_block, last_block);
    EnsureResident(arena, first_block, last_block);
    if (!IsInPlace(device_addr, size)) {
        return std::nullopt;
    }
    WriteTicks().Note(device_addr, size, scheduler.CurrentTick());
    return std::pair<const Buffer*, u64>{arena, arena->Offset(device_addr)};
}

u32 BufferCache::PrefetchBinding(VAddr device_addr, u32 size) const noexcept {
    if (size == 0) {
        return 0;
    }
    u32 lines = 0;
    const u64 first_page = (device_addr >> block_shift) >> blocks_per_arena_page_shift;
    if (first_page < address_space.size()) {
        __builtin_prefetch(&address_space[first_page]);
        ++lines;
    }
    return lines + memory_tracker->Prefetch(device_addr, size);
}

std::pair<const Buffer*, u64> BufferCache::ObtainBufferForImage(VAddr device_addr, u32 size) {
    // bbport BB_GX_IN_PLACE: inside the game's GX region, through its own buffer (or the arena
    // where it is bound in place, BB_GX_OWN_BUFFER=0).
    if (const Buffer* gx = gx_own.load(std::memory_order_acquire);
        gx && InGxRegion(device_addr, size)) {
        if (device_addr < gx_head) [[unlikely]] {
            NoteBeforeGx(device_addr, size);
        }
        TraceBinding(device_addr, size, false, 5);
        return {gx, gx->Offset(device_addr)};
    }
    if (InGxRegion(device_addr, size) && IsInPlace(device_addr, size)) {
        const u64 first_block = device_addr >> block_shift;
        const u64 last_block = (device_addr + size - 1) >> block_shift;
        const auto* arena = GetArena(first_block, last_block);
        TraceBinding(device_addr, size, false, 5);
        return {arena, arena->Offset(device_addr)};
    }
    if (IsRegionGpuModified(device_addr, size)) {
        TraceBinding(device_addr, size, false, 6);
        return ObtainBuffer(device_addr, size, false);
    }
    // bbport: BB_PREUPLOAD — texture data comes from the arena (device-local): only the pages
    // the CPU changed since are uploaded into it, and the background pre-upload keeps it
    // current, so a streaming burst detiles from VRAM instead of copying everything through
    // the CPU and the PCIe bus in that frame.
    if (PreuploadEnabled()) {
        const u64 first_block = device_addr >> block_shift;
        const u64 last_block = (device_addr + size - 1) >> block_shift;
        // Mode 1 (no new VRAM): only where the arena already holds the range.
        bool resident = true;
        if (PreuploadMode() != 2) {
            resident_ranges.ForEachGap(first_block, last_block + 1,
                                       [&](u64, u64) { resident = false; });
        }
        if (resident) {
            const auto* arena = GetArena(first_block, last_block);
            EnsureResident(arena, first_block, last_block);
            SynchronizeMemory(arena, device_addr, size, false, false);
            TraceBinding(device_addr, size, false, 7);
            return {arena, arena->Offset(device_addr)};
        }
    }
    TraceBinding(device_addr, size, false, 8);
    const auto staging = staging_pool.Request(size, VideoCore::MemoryType::HostUncached,
                                              instance.StorageMinAlignment());
    if (!BbToggle::Disabled(BbToggle::DeferredUploads)) {
        // bbport: texture data is copied on the copy threads (streaming: 100+ MB per frame);
        // the upload reads the staging only after submission, which waits for the copies.
        constexpr u64 Chunk = 1_MB;
        for (u64 offset = 0; offset < staging.size; offset += Chunk) {
            const BbCopy::Item item{
                .run = &RunGuestCopy,
                .context = this,
                .source = device_addr + offset,
                .destination = reinterpret_cast<u64>(staging.mapped + offset),
                .size = std::min(Chunk, staging.size - offset),
                .extra = reinterpret_cast<u64>(staging.buffer),
            };
            if (staging.size < Chunk) {
                SmallGuestCopy(item);
            } else {
                BbCopy::Async([item] { item.run(item); });
                scheduler.NoteHostCopySource(item.source, item.size);
            }
        }
        return {staging.buffer, staging.offset};
    }
    memory->CopySparseMemory(device_addr, staging.mapped, staging.size);
    staging.Flush();
    return {staging.buffer, staging.offset};
}

bool BufferCache::IsRegionCpuModified(VAddr addr, size_t size) {
    return memory_tracker->IsRegionCpuModified(addr, size);
}

bool BufferCache::IsRegionGpuModified(VAddr addr, size_t size) {
    if (memory_tracker->IsRegionGpuModified(addr, size)) {
        return true;
    }
    // bbport BB_GX_IN_PLACE: GPU writes in the GX region in place are not tracked per page; one
    // not done yet counts.
    if (GxOverlaps(addr, size)) {
        const u64 tick = WriteTicks().Newest(addr, size);
        if (tick == 0 || scheduler.GetWorkSemaphore()->IsFree(tick)) {
            return false;
        }
        // Asking the driver is a system call, and the GPU command thread asks for thousands of
        // constant bindings a frame: at most every ~250 us per thread (until then the write
        // counts as not done, which only takes the slower, always correct path; the GPU signal
        // thread refreshes the known tick on every fence anyway).
        thread_local u64 last_refresh = 0;
        if (const u64 now = __rdtsc(); now - last_refresh > 800000) {
            last_refresh = now;
            scheduler.GetWorkSemaphore()->Refresh();
        }
        return !scheduler.GetWorkSemaphore()->IsFree(tick);
    }
    return false;
}

u64 BufferCache::GpuWriteVersion(VAddr addr, u64 size) {
    if (!memory_tracker->IsRegionGpuModified(addr, size)) {
        return ~u64{0};
    }
    return GpuWriteStamps::Get().Last(addr, size);
}

void BufferCache::ProcessFaultBuffer() {
    fault_manager->ProcessFaultBuffer();
}

void BufferCache::SynchronizeDmaBuffers() {
    dma_second.store(BbStats::coarse_second.load(std::memory_order_relaxed),
                     std::memory_order_relaxed);
    for (const auto& range : resident_ranges) {
        const u64 page = range.start >> (ARENA_PAGE_BITS - block_shift);
        const VAddr device_addr = range.start << block_shift;
        const u64 size = (range.end - range.start) << block_shift;
        SynchronizeMemory(address_space[page], device_addr, size, false, false);
    }
}

const Buffer* BufferCache::GetArena(u64 first_block, u64 last_block) {
    const u64 first_page = first_block >> blocks_per_arena_page_shift;
    const u64 last_page = last_block >> blocks_per_arena_page_shift;
    ASSERT_MSG(last_page - first_page <= 1,
               "Buffer request cannot span more than two VA arena pages");

    const auto* first_arena = address_space[first_page];
    const auto* last_arena = address_space[last_page];
    if (first_arena == last_arena) {
        if (!first_arena) {
            // bbport: the free pages of first_page's aligned group (arena_group_pages).
            u64 begin = first_page, end = last_page + 1;
            const u64 group = Common::AlignDownPow2<u64>(first_page, arena_group_pages);
            while (begin > group && !address_space[begin - 1]) --begin;
            while (end < std::min<u64>(group + arena_group_pages, NUM_ARENA_PAGES) &&
                   !address_space[end]) {
                ++end;
            }
            const auto* new_arena =
                &arenas.emplace_back(instance, begin << ARENA_PAGE_BITS,
                                     (end - begin) << ARENA_PAGE_BITS, MemoryType::Sparse);
            for (u64 page = begin; page < end; ++page) {
                address_space[page] = new_arena;
            }
            std::printf("Guest memory: arena [%#llx, %#llx) at GPU address %#llx\n",
                        static_cast<unsigned long long>(new_arena->cpu_addr),
                        static_cast<unsigned long long>(new_arena->cpu_addr + new_arena->size_bytes),
                        static_cast<unsigned long long>(new_arena->BufferDeviceAddress()));
        }
        return address_space[first_page];
    }

    LOG_WARNING(Render, "Migrating arena");

    const u64 first_addr = first_arena ? first_arena->cpu_addr : (first_page << ARENA_PAGE_BITS);
    const u64 first_size = first_arena ? first_arena->size_bytes : ARENA_PAGE_SIZE;
    const u64 last_size = last_arena ? last_arena->size_bytes : ARENA_PAGE_SIZE;

    const u64 base_block = first_addr >> block_shift;
    const u64 total_size = first_size + last_size;
    const u64 end_block = (first_addr + total_size) >> block_shift;
    auto* new_arena = &arenas.emplace_back(instance, first_addr, total_size, MemoryType::Sparse);
    std::printf("Guest memory: arena migrated to [%#llx, %#llx) at GPU address %#llx\n",
                static_cast<unsigned long long>(first_addr),
                static_cast<unsigned long long>(first_addr + total_size),
                static_cast<unsigned long long>(new_arena->BufferDeviceAddress()));
    auto* bind = BindsForArena(new_arena);
    resident_ranges.ForEachInRange(base_block, end_block, [&](const Backing& backing) {
        const u64 start = std::max(base_block, backing.start);
        const u64 end = std::min(end_block, backing.end);
        bind->binds.push_back(vk::SparseMemoryBind{
            .resourceOffset = (start - base_block) << block_shift,
            .size = (end - start) << block_shift,
            .memory = backing.memory,
            .memoryOffset = (backing.offset + (start - backing.start)) << block_shift,
        });
    });

    u64 base_page = first_addr >> ARENA_PAGE_BITS;
    for (u32 page = 0; page < (first_size >> ARENA_PAGE_BITS); ++page) {
        address_space[base_page + page] = new_arena;
    }
    base_page = last_page;
    for (u32 page = 0; page < (last_size >> ARENA_PAGE_BITS); ++page) {
        address_space[base_page + page] = new_arena;
    }
    return new_arena;
}

void BufferCache::EnsureResident(const Buffer* arena, u64 first_block, u64 last_block) {
    BB_SECTION(EnsureResident);
    NoteUse(first_block << block_shift, (last_block - first_block + 1) << block_shift);
    if (maintained_epoch != packet_epoch) {
        Maintain();
    }
    // bbport: the blocks were all resident the last time and nothing changed since: no interval
    // lookup (it ran for every buffer binding).
    thread_local std::array<ResidencyMemo, 256> resident_memo{};
    auto& memo = resident_memo[first_block & (resident_memo.size() - 1)];
    if (memo.first == first_block && memo.last == last_block &&
        memo.generation == ResidencyGeneration()) {
        return;
    }
    IntervalList bind_ranges;
    resident_ranges.ForEachGap(first_block, last_block + 1,
                               [&](u64 start, u64 end) { bind_ranges.Add({start, end}); });
    // bbport: blocks evicted from the 2 MiB groups of this request (EvictIdleCopies) come back
    // with it, whole. A draw reads past its own range into bytes another binding uploaded (vertex
    // buffers are bound without a size): back in VRAM with only this range uploaded, those read
    // zeros until that binding came again, and geometry went missing.
    IntervalList restored;
    if (!evicted_blocks.Empty() && block_shift <= USE_GROUP_BITS) {
        const u64 group_shift = USE_GROUP_BITS - block_shift;
        const u64 from = (first_block >> group_shift) << group_shift;
        const u64 to = ((last_block >> group_shift) + 1) << group_shift;
        evicted_blocks.ForEachInRange(from, to, [&](const Interval& range) {
            const Interval part{std::max(from, range.start), std::min(to, range.end)};
            restored.Add(part);
            bind_ranges.Add(part);
        });
        if (!restored.Empty()) {
            evicted_blocks.Subtract(from, to);
        }
    }
    if (bind_ranges.Empty()) {
        memo = {first_block, last_block, ResidencyGeneration()};
        return;
    }
    BbStats::Timer timer{BbStats::t_resident};

    std::vector<ResidentBind> binds;
    if (gx_ready.load(std::memory_order_acquire)) {
        // bbport BB_GX_IN_PLACE: the region's blocks to its imported memory, for good (with its
        // own buffer only for what needs the arena there: GxArenaBound); the rest gets VRAM.
        IntervalList rest;
        for (const auto& range : bind_ranges) {
            const u64 a = std::max(range.start, gx_first), b = std::min(range.end, gx_end);
            if (a >= b) {
                rest.Add(range);
                continue;
            }
            GxArenaBound();
            if (range.start < a) {
                rest.Add({range.start, a});
            }
            if (b < range.end) {
                rest.Add({b, range.end});
            }
            binds.push_back({a, b, gx_memory, (a - gx_first) << block_shift});
            in_place_blocks.Add({a, b});
        }
        bind_ranges = std::move(rest);
    }
    u64 vram_blocks = 0;
    for (const auto& range : bind_ranges) {
        vram_blocks += range.end - range.start;
    }

    // bbport: blocks put into slots other blocks left, cleared below.
    boost::container::small_vector<u64, 16> reused;
    if (vram_blocks != 0) {
        // bbport: the slots of blocks that left VRAM first, block by block. A request of several
        // blocks only ever took new space, and the free slots piled up while new 64 MiB chunks
        // were allocated.
        IntervalList rest_ranges;
        for (const auto& range : bind_ranges) {
            u64 block = range.start;
            for (; block < range.end && free_slot_count != 0; ++block) {
                reused.push_back(block);
                const auto [memory, memory_offset] = AllocateResidency(block_size);
                auto* last = binds.empty() ? nullptr : &binds.back();
                if (last && last->end == block && last->memory == memory &&
                    last->offset + ((block - last->start) << block_shift) == memory_offset) {
                    last->end = block + 1;
                } else {
                    binds.push_back({block, block + 1, memory, memory_offset});
                }
                --vram_blocks;
            }
            if (block < range.end) {
                rest_ranges.Add({block, range.end});
            }
        }
        if (vram_blocks != 0) {
            auto [memory, memory_offset] = AllocateResidency(vram_blocks << block_shift);
            for (const auto& range : rest_ranges) {
                binds.push_back({range.start, range.end, memory, memory_offset});
                memory_offset += (range.end - range.start) << block_shift;
            }
        }
    }

    u64 total_blocks = 0;
    for (const auto& bind : binds) {
        total_blocks += bind.end - bind.start;
    }
    boost::container::small_vector<vk::BufferCopy, 8> copies;
    const auto staging =
        staging_pool.Request(total_blocks * sizeof(vk::DeviceAddress), MemoryType::HostUncached);
    auto* bda_addrs = reinterpret_cast<vk::DeviceAddress*>(staging.mapped);
    u64 offset = staging.offset;
    for (const auto& bind : binds) {
        Backing backing;
        backing.start = bind.start;
        backing.end = bind.end;
        backing.memory = bind.memory;
        backing.offset = bind.offset >> block_shift;
        resident_ranges.Add(backing);

        const vk::SparseMemoryBind sparse = BindsForArena(arena)->binds.emplace_back(vk::SparseMemoryBind{
            .resourceOffset = (bind.start << block_shift) - arena->cpu_addr,
            .size = (bind.end - bind.start) << block_shift,
            .memory = bind.memory,
            .memoryOffset = bind.offset,
        });
        for (u64 block = 0; block < sparse.size; block += block_size) {
            *(bda_addrs++) = arena->BufferDeviceAddress() + sparse.resourceOffset + block;
        }
        // bbport: an arena replaced by a migration (GetArena) still covers these addresses and
        // can still be bound by a draw; it gets the same memory, as eviction unbinds it from
        // every arena. Bound only in the new one, a block evicted and back after a migration
        // read zeros through the old arena: objects and textures went missing for good.
        const VAddr bind_begin = bind.start << block_shift;
        const VAddr bind_end = bind.end << block_shift;
        for (const auto& other : arenas) {
            if (&other == arena || other.cpu_addr >= bind_end ||
                other.cpu_addr + other.size_bytes <= bind_begin) {
                continue;
            }
            const VAddr begin = std::max<VAddr>(bind_begin, other.cpu_addr);
            const VAddr end = std::min<VAddr>(bind_end, other.cpu_addr + other.size_bytes);
            BindsForArena(&other)->binds.emplace_back(vk::SparseMemoryBind{
                .resourceOffset = begin - other.cpu_addr,
                .size = end - begin,
                .memory = bind.memory,
                .memoryOffset = bind.offset + (begin - bind_begin),
            });
        }
        const u64 copy_size = (bind.end - bind.start) * sizeof(vk::DeviceAddress);
        copies.emplace_back(offset, bind.start * sizeof(vk::DeviceAddress), copy_size);
        offset += copy_size;
    }

    staging.Flush();
    runtime.CopyBuffer(staging.buffer, bda_pagetable_buffer.get(), copies);

    // bbport: a slot another block left still holds that block's data; only the ranges bound are
    // uploaded into it. Vertex buffers are bound without a size (dynamic vertex input), so a draw
    // reading past its range read zeros in new VRAM (the driver clears it) and read the other
    // block's data in a reused slot: stretched polygons, flashing lights. Cleared like new VRAM,
    // before the uploads that follow.
    for (size_t i = 0; i < reused.size();) {
        size_t j = i + 1;
        while (j < reused.size() && reused[j] == reused[j - 1] + 1) {
            ++j;
        }
        runtime.FillBuffer(arena, (reused[i] << block_shift) - arena->cpu_addr,
                           u64(j - i) * block_size, 0u);
        i = j;
    }
    // Evicted blocks back from the game's memory, whole (marked CPU-modified when they left).
    for (const auto& range : restored) {
        SynchronizeMemory(arena, range.start << block_shift,
                          static_cast<u32>((range.end - range.start) << block_shift), false, false);
    }
}

std::pair<vk::DeviceMemory, u64> BufferCache::AllocateResidency(u64 bytes) {
    if (bytes == block_size && free_slot_count != 0) {
        // bbport: a slot of the fullest chunk: the emptier ones drain and go back to the driver.
        auto best = residency_chunks.end();
        for (auto it = residency_chunks.begin(); it != residency_chunks.end(); ++it) {
            if (!it->second.free_offsets.empty() &&
                (best == residency_chunks.end() || it->second.used > best->second.used)) {
                best = it;
            }
        }
        if (best != residency_chunks.end()) {
            const u64 offset = best->second.free_offsets.back();
            best->second.free_offsets.pop_back();
            best->second.used += block_size;
            --free_slot_count;
            NoteResidencyUnused();
            return {vk::DeviceMemory{best->first}, offset};
        }
    }
    // bbport: residency comes out of 64 MiB blocks (a larger request gets its own): a streaming
    // burst bound hundreds of small ranges, each its own vkAllocateMemory (~8 ms a burst). At most
    // one block is partly unused. The next block is allocated in the background once half of the
    // current one is used: the kernel clears new memory, on a Steam Deck with the CPU (16-22 ms a
    // burst). At most one spare block.
    constexpr u64 ResidencyBlock = 64_MB;
    const auto try_allocate = [this](u64 size, u32 type) -> vk::DeviceMemory {
        const auto [result, memory] = instance.GetDevice().allocateMemory({
            .allocationSize = size,
            .memoryTypeIndex = type,
        });
        if (result != vk::Result::eSuccess) {
            return {};
        }
        BbStats::device_alloc_bytes.fetch_add(size, std::memory_order_relaxed);
        BbStats::residency_alloc_bytes.fetch_add(size, std::memory_order_relaxed);
        return memory;
    };
    // bbport: out of VRAM is no longer the end of the game. VRAM while there is room; while it is
    // tight (vram_level) or once an allocation failed (full for 30 s), system memory. Without
    // that, the empty chunks go back to the driver and VRAM is tried once more; only a failure
    // then stops, as before.
    const auto allocate = [this, &try_allocate](u64 size) {
        const u32 now = BbStats::coarse_second.load(std::memory_order_relaxed);
        const bool tight = vram_level.load(std::memory_order_relaxed) >= 1;
        if (!tight || !system_memory_type_index) {
            if (const auto memory = try_allocate(size, arena_memory_type_index)) {
                return memory;
            }
            vram_full_until = now + 30;
            vram_level.store(2, std::memory_order_relaxed);
            std::printf("Guest memory: out of VRAM for %llu MiB of the game's data\n",
                        (unsigned long long)(size >> 20));
        }
        if (system_memory_type_index) {
            if (const auto memory = try_allocate(size, *system_memory_type_index)) {
                system_residency_bytes += size;
                if (system_residency_reports < 20 &&
                    (system_residency_bytes == size ||
                     (system_residency_bytes >> 28) != ((system_residency_bytes - size) >> 28))) {
                    ++system_residency_reports;
                    std::printf("Guest memory: VRAM tight, %llu MiB of the game's data in system "
                                "memory so far\n",
                                (unsigned long long)(system_residency_bytes >> 20));
                }
                return memory;
            }
            std::printf("Guest memory: no system memory either for %llu MiB of the game's data\n",
                        (unsigned long long)(size >> 20));
        }
        if (spare_residency) {
            if (const auto spare = TakeSpareResidency()) {
                instance.GetDevice().freeMemory(spare);
                BbStats::residency_alloc_bytes.fetch_sub(64_MB, std::memory_order_relaxed);
                BbStats::device_free_bytes.fetch_add(64_MB, std::memory_order_relaxed);
            }
        }
        // Empty chunks the GPU is done with (as ProcessIdleBlocks, without its 5 s wait).
        for (auto it = residency_chunks.begin(); it != residency_chunks.end();) {
            if (it->second.used != 0 || it->first == static_cast<VkDeviceMemory>(residency_memory) ||
                !pending_binds.empty() || !scheduler.GetWorkSemaphore()->IsFree(it->second.empty_tick + 1)) {
                ++it;
                continue;
            }
            free_slot_count -= it->second.free_offsets.size();
            instance.GetDevice().freeMemory(vk::DeviceMemory{it->first});
            BbStats::residency_alloc_bytes.fetch_sub(it->second.size, std::memory_order_relaxed);
            BbStats::device_free_bytes.fetch_add(it->second.size, std::memory_order_relaxed);
            it = residency_chunks.erase(it);
        }
        NoteResidencyUnused();
        return Vulkan::Check(instance.GetDevice().allocateMemory({
            .allocationSize = size,
            .memoryTypeIndex = arena_memory_type_index,
        }));
    };
    if (residency_used + bytes > residency_size) {
        const u64 size = std::max(bytes, ResidencyBlock);
        vk::DeviceMemory spare{};
        if (size == ResidencyBlock && spare_residency) {
            spare = TakeSpareResidency(); // null when it failed or came late
        }
        residency_memory = spare ? spare : allocate(size);
        residency_size = size;
        residency_used = 0;
        residency_chunks[static_cast<VkDeviceMemory>(residency_memory)] = {size, 0, 0, 0};
    }
    const u64 offset = residency_used;
    residency_used += bytes;
    residency_chunks[static_cast<VkDeviceMemory>(residency_memory)].used += bytes;
    if (!spare_residency && residency_used * 2 >= residency_size &&
        vram_level.load(std::memory_order_relaxed) == 0) {
        auto spare = std::make_shared<SpareBlock>();
        spare_residency = spare;
        std::thread([this, spare, try_allocate, type = arena_memory_type_index] {
            const vk::DeviceMemory memory = try_allocate(ResidencyBlock, type);
            std::scoped_lock lk{spare->mutex};
            if (spare->abandoned) {
                // Stage B stopped waiting and allocated on its own.
                if (memory) {
                    instance.GetDevice().freeMemory(memory);
                    BbStats::residency_alloc_bytes.fetch_sub(ResidencyBlock,
                                                             std::memory_order_relaxed);
                    BbStats::device_free_bytes.fetch_add(ResidencyBlock, std::memory_order_relaxed);
                }
                return;
            }
            spare->memory = memory;
            spare->done = true;
            spare->cv.notify_all();
        }).detach();
    }
    NoteResidencyUnused();
    return {residency_memory, offset};
}

void BufferCache::UpdateVramLevel(u32 now) {
    static const u64 percent = [] {
        const char* env = std::getenv("BB_VRAM_PRESSURE_PERCENT");
        return std::min<u64>(env ? std::strtoull(env, nullptr, 10) : 85, 95);
    }();
    int level = 0;
    u64 used = 0, budget = 0;
    if (percent != 0 && instance.CanReportMemoryUsage()) {
        used = instance.GetDeviceMemoryUsage();
        budget = instance.GetDeviceMemoryBudgetNow();
        if (budget != 0) {
            level = used * 100 >= budget * (percent + 5) ? 2 : used * 100 >= budget * percent ? 1 : 0;
        }
    }
    if (now < vram_full_until) {
        level = 2;
    }
    const int was = vram_level.exchange(level, std::memory_order_relaxed);
    if (level != was && vram_level_reports < 40) {
        ++vram_level_reports;
        static constexpr const char* kCopies[] = {
            "room again: the game's data stays in VRAM",
            "tight: the game's data unused for 30 s leaves VRAM, new blocks go to system memory",
            "full: the game's data unused for 10 s leaves VRAM, new blocks go to system memory",
        };
        static constexpr const char* kCopiesVram[] = {
            "room again: the game's data stays in VRAM",
            "tight: the game's data unused for 30 s leaves VRAM",
            "full: the game's data unused for 10 s leaves VRAM",
        };
        const char* what = system_memory_type_index ? kCopies[level] : kCopiesVram[level];
        std::printf("Guest memory: VRAM %llu of %llu MiB, %s\n", (unsigned long long)(used >> 20),
                    (unsigned long long)(budget >> 20), what);
    }
}

vk::DeviceMemory BufferCache::TakeSpareResidency() {
    const auto spare = std::move(spare_residency);
    spare_residency.reset();
    if (!spare) {
        return {};
    }
    std::unique_lock lk{spare->mutex};
    if (!spare->cv.wait_for(lk, std::chrono::milliseconds(50), [&] { return spare->done; })) {
        spare->abandoned = true;
        if (spare_residency_timeouts++ < 10) {
            std::printf("Guest memory: the background 64 MiB block was not ready after 50 ms; "
                        "allocated directly (it is freed when it comes)\n");
        }
        return {};
    }
    return spare->memory;
}

void BufferCache::NoteResidencyUnused() {
    const u64 unused = free_slot_count * block_size + (residency_size - residency_used) +
                       (spare_residency ? 64_MB : 0);
    BbStats::residency_unused_bytes.store(unused, std::memory_order_relaxed);
    BbStats::residency_chunk_count.store(residency_chunks.size(), std::memory_order_relaxed);
}

u32 BufferCache::VramIdleSeconds() {
    static const u32 seconds = [] {
        const char* env = std::getenv("BB_VRAM_IDLE_SECONDS");
        return env ? u32(std::strtoul(env, nullptr, 10)) : 60u;
    }();
    return seconds;
}

void BufferCache::NoteUse(VAddr address, u64 size) {
    const u32 now = BbStats::coarse_second.load(std::memory_order_relaxed);
    const u64 last = std::min<u64>((address + size - 1) >> USE_GROUP_BITS, group_use.size() - 1);
    for (u64 group = address >> USE_GROUP_BITS; group <= last; ++group) {
        if (group_use[group] != now) {
            group_use[group] = now;
        }
    }
}

void BufferCache::ReleaseResidencySlot(vk::DeviceMemory memory, u64 offset) {
    if (const auto it = residency_chunks.find(static_cast<VkDeviceMemory>(memory));
        it != residency_chunks.end()) {
        it->second.free_offsets.push_back(offset);
        ++free_slot_count;
        it->second.used -= block_size;
        if (it->second.used == 0) {
            it->second.empty_second = BbStats::coarse_second.load(std::memory_order_relaxed);
            it->second.empty_tick = scheduler.CurrentTick();
        }
    }
    NoteResidencyUnused();
}

void BufferCache::EvictVramBlock(u64 block) {
    const auto old = resident_ranges.Find(block);
    if (old == resident_ranges.end() || in_place_blocks.Contains(block) ||
        !residency_chunks.contains(static_cast<VkDeviceMemory>(old->memory))) {
        return;
    }
    const vk::DeviceMemory memory = old->memory;
    const u64 offset = (old->offset + (block - old->start)) << block_shift;
    const VAddr address = block << block_shift;
    for (const auto& arena : arenas) {
        if (arena.cpu_addr <= address && address - arena.cpu_addr < arena.size_bytes) {
            BindsForArena(&arena)->binds.emplace_back(vk::SparseMemoryBind{
                .resourceOffset = address - arena.cpu_addr,
                .size = block_size,
                .memory = {},
                .memoryOffset = 0,
            });
        }
    }
    resident_ranges.Subtract(block, block + 1);
    memory_tracker->UnmarkRegionAsGpuModified(address, block_size);
    gpu_modified_ranges.Subtract(address, block_size);
    memory_tracker->MarkRegionAsCpuModified(address, block_size);
    ReleaseResidencySlot(memory, offset);
    // After the work submitted so far: it may still read the block.
    bind_wait_tick = std::max(bind_wait_tick, scheduler.CurrentTick() - 1);
    BbStats::vram_unmapped_bytes.fetch_add(block_size, std::memory_order_relaxed);
}

void BufferCache::ProcessIdleBlocks() {
    const u32 now = BbStats::coarse_second.load(std::memory_order_relaxed);
    if (now == idle_scan_second) {
        return;
    }
    idle_scan_second = now;
    UpdateVramLevel(now);
    // bbport: the game's GX region comes from the engine's hook on its init (engine/gx_region.cpp)
    // when the game makes its graphics device; a minute on without it, the log says so once.
    if (gx_state.load(std::memory_order_relaxed) == u8(GxState::Waiting) && !BbGxRing::Known() &&
        ++gx_ring_looks == 60) {
        BbGxRing::ReportMissing();
    }
    if (VramIdleSeconds() == 0) {
        return;
    }
    EvictIdleCopies(now);
    FreeEmptyChunks(now);
}

void BufferCache::EvictIdleCopies(u32 now) {
    // bbport: every block of the game's memory the GPU used kept its VRAM copy, and a 4 GB card
    // ran out (exit 23). While VRAM is tight the copies unused for a while go. Kept: blocks with
    // data the GPU wrote (only VRAM has it), and all of them while shaders read the game's memory
    // through the page table (dma_second).
    const int level = vram_level.load(std::memory_order_relaxed);
    if (TwinsEnabled()) {
        return;
    }
    // Unused for 30 s while tight, 10 s when full: an evicted block costs an upload and can show
    // as late geometry when it comes back.
    // bbport: with room in VRAM too, copies unused for BB_VRAM_CLEANUP_SECONDS (default 180; 0:
    // only while VRAM is tight) go: without it every block the game's GPU work ever touched kept
    // its copy, and long sessions on 8 GB cards (laptops, RTX 4060) went from 2.6 to 5 GB of VRAM
    // in 90 minutes until a crash or a lost device. A block is used again within seconds of its
    // area loading, so three minutes unused is an area left behind.
    static const u32 cleanup = [] {
        const char* env = std::getenv("BB_VRAM_CLEANUP_SECONDS");
        return env && env[0] ? u32(std::strtoul(env, nullptr, 10)) : 180u;
    }();
    if (level == 0 && (cleanup == 0 || now - last_cleanup_scan < 10)) {
        return;
    }
    if (level == 0) {
        last_cleanup_scan = now;
    }
    const u32 idle = level == 0 ? std::max(cleanup, VramIdleSeconds())
                                : std::min(VramIdleSeconds(), level >= 2 ? 10u : 30u);
    const u32 dma = dma_second.load(std::memory_order_relaxed);
    if (dma != 0 && now - dma < idle) {
        if (dma_kept_reports < 10 && now - dma_kept_second >= 60) {
            ++dma_kept_reports;
            dma_kept_second = now;
            std::printf("Guest memory: VRAM tight, but shaders read the game's memory directly: "
                        "its unused data stays in VRAM\n");
        }
        return;
    }
    std::vector<u64> blocks;
    for (const auto& range : resident_ranges) {
        if (!residency_chunks.contains(static_cast<VkDeviceMemory>(range.memory))) {
            continue;
        }
        for (u64 block = range.start; block < range.end; ++block) {
            const VAddr address = block << block_shift;
            const u64 group = std::min<u64>(address >> USE_GROUP_BITS, group_use.size() - 1);
            if (now - group_use[group] < idle || gpu_modified_ranges.Intersects(address, block_size) ||
                memory_tracker->IsRegionGpuModified(address, block_size)) {
                continue;
            }
            blocks.push_back(block);
        }
    }
    if (blocks.empty()) {
        return;
    }
    for (const u64 block : blocks) {
        EvictVramBlock(block);
        evicted_blocks.Add({block, block + 1});
    }
    // Their page table entries are cleared: a shader reading one through it reports a fault and
    // the block is bound again (ProcessFaultBuffer) instead of the shader reading unbound memory.
    for (size_t i = 0; i < blocks.size();) {
        size_t j = i + 1;
        while (j < blocks.size() && blocks[j] == blocks[j - 1] + 1) {
            ++j;
        }
        runtime.FillBuffer(bda_pagetable_buffer.get(), blocks[i] * sizeof(vk::DeviceAddress),
                           (j - i) * sizeof(vk::DeviceAddress), 0u);
        i = j;
    }
    const u64 bytes = u64(blocks.size()) * block_size;
    evicted_bytes += bytes;
    // Once the first whole MiB is out, then at each 256 MiB (a first block under a MiB said "0 MiB").
    const u64 before = evicted_bytes - bytes;
    if (evict_reports < 20 && (((before >> 20) == 0 && (evicted_bytes >> 20) != 0) ||
                               (evicted_bytes >> 28) != (before >> 28))) {
        ++evict_reports;
        std::printf("Guest memory: %s%llu MiB of the game's data unused for %u s left VRAM "
                    "so far (uploaded again when used)\n",
                    level == 0 ? "cleanup, " : "VRAM tight, ",
                    (unsigned long long)(evicted_bytes >> 20), idle);
    }
}

void BufferCache::FreeEmptyChunks(u32 now) {
    // Chunks without blocks for 5 s, the rebinds away from them long done: back to the driver.
    // bbport: with room in VRAM, BB_VRAM_CHUNK_KEEP_SECONDS (30) instead, and the 64 MiB chunk
    // emptied last stays: blocks coming back (an area entered again) take its slots instead of
    // new memory the driver clears first. While VRAM is tight, or with 0: 5 s, none kept.
    static const u32 keep_seconds = [] {
        const char* env = std::getenv("BB_VRAM_CHUNK_KEEP_SECONDS");
        return env && env[0] ? u32(std::strtoul(env, nullptr, 10)) : 30u;
    }();
    const bool room = keep_seconds != 0 && vram_level.load(std::memory_order_relaxed) == 0;
    const u32 wait = room ? std::max(keep_seconds, 5u) : 5u;
    VkDeviceMemory spare = VK_NULL_HANDLE;
    if (room) {
        u32 newest = 0;
        for (const auto& [memory, chunk] : residency_chunks) {
            if (chunk.used == 0 && chunk.size == 64_MB &&
                memory != static_cast<VkDeviceMemory>(residency_memory) &&
                (spare == VK_NULL_HANDLE || chunk.empty_second >= newest)) {
                spare = memory;
                newest = chunk.empty_second;
            }
        }
    }
    for (auto it = residency_chunks.begin(); it != residency_chunks.end();) {
        const ResidencyChunk& chunk = it->second;
        const VkDeviceMemory memory = it->first;
        if (chunk.used != 0 || memory == static_cast<VkDeviceMemory>(residency_memory) ||
            memory == spare || now - chunk.empty_second < wait || !pending_binds.empty() ||
            !scheduler.GetWorkSemaphore()->IsFree(chunk.empty_tick + 1)) {
            ++it;
            continue;
        }
        free_slot_count -= chunk.free_offsets.size();
        instance.GetDevice().freeMemory(vk::DeviceMemory{memory});
        BbStats::residency_alloc_bytes.fetch_sub(chunk.size, std::memory_order_relaxed);
        BbStats::device_free_bytes.fetch_add(chunk.size, std::memory_order_relaxed);
        BbStats::vram_chunks_freed_bytes.fetch_add(chunk.size, std::memory_order_relaxed);
        it = residency_chunks.erase(it);
        NoteResidencyUnused();
    }
}

void BufferCache::TraceBinding(VAddr addr, u64 size, bool is_written, int kind) {
    static const u64 trace_size = [] {
        const char* env = std::getenv("BB_TRACE_SIZE");
        return env ? std::strtoull(env, nullptr, 0) : u64(0);
    }();
    if (trace_size == 0) {
        return;
    }
    if (is_written && size == trace_size &&
        std::ranges::find(traced_ranges, std::pair<VAddr, u64>{addr, size}) == traced_ranges.end()) {
        if (traced_ranges.size() >= 16) {
            traced_ranges.erase(traced_ranges.begin());
        }
        traced_ranges.emplace_back(addr, size);
    }
    const bool traced = std::ranges::any_of(traced_ranges, [&](const auto& range) {
        return addr < range.first + range.second && range.first < addr + size;
    });
    if (traced) {
        ++trace_counts[kind];
        if (!is_written) {
            trace_min = std::min(trace_min, size);
            trace_max = std::max(trace_max, size);
        }
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - trace_report < std::chrono::seconds(5)) {
        return;
    }
    trace_report = now;
    std::printf("Trace %#llx-byte GPU buffers (%zu, 5 s): write in place %llu, write VRAM %llu; read "
                "in place %llu, snapshot %llu, VRAM %llu (sizes %llu-%llu); image source: in place "
                "%llu, GPU data %llu, preupload %llu, staging %llu\n",
                (unsigned long long)trace_size, traced_ranges.size(),
                (unsigned long long)trace_counts[0], (unsigned long long)trace_counts[1],
                (unsigned long long)trace_counts[2], (unsigned long long)trace_counts[3],
                (unsigned long long)trace_counts[4], (unsigned long long)(trace_max ? trace_min : 0),
                (unsigned long long)trace_max, (unsigned long long)trace_counts[5],
                (unsigned long long)trace_counts[6], (unsigned long long)trace_counts[7],
                (unsigned long long)trace_counts[8]);
    trace_counts = {};
    trace_min = ~0ull;
    trace_max = 0;
}

void BufferCache::NoteLargeGpuWrite(VAddr addr, u64 size) {
    auto& write = large_writes[addr ^ (size << 40)];
    if (write.count++ == 0) {
        write.addr = addr;
        write.size = size;
        write.images = texture_cache.DescribeImagesIn(addr, size);
    }
    write.in_place = IsInPlace(addr, size);
    const auto now = std::chrono::steady_clock::now();
    if (now - large_writes_report < std::chrono::seconds(5)) {
        return;
    }
    large_writes_report = now;
    std::vector<const LargeWrite*> rows;
    for (const auto& [key, entry] : large_writes) {
        rows.push_back(&entry);
    }
    std::ranges::sort(rows, [](const auto* a, const auto* b) { return a->count > b->count; });
    std::printf("GPU-written over 64 KiB (5 s):");
    for (std::size_t i = 0; i < std::min<std::size_t>(rows.size(), 12); ++i) {
        std::printf("%s %#llx+%#llx x%llu %s%s", i ? ";" : "", (unsigned long long)rows[i]->addr,
                    (unsigned long long)rows[i]->size, (unsigned long long)rows[i]->count,
                    rows[i]->in_place ? "in-place" : "VRAM", rows[i]->images.c_str());
    }
    std::printf("\n");
    large_writes.clear();
}

void BufferCache::UnmapInPlace(VAddr addr, u64 size) {
    if (size == 0) {
        return;
    }
    // bbport BB_GX_IN_PLACE: memory of the region unmapped (only seen at the game's exit): every
    // block of it unbound (ProcessPendingUnmaps), then tracked as before.
    // The tail page holds the game's next allocation too: unmapped there, it is no longer read
    // in place (its memory may be mapped elsewhere).
    if (const VAddr tail_end = gx_tail_end.load(std::memory_order_acquire);
        tail_end != 0 && addr < tail_end && addr + size > (gx_end << block_shift)) {
        gx_tail_end.store(0, std::memory_order_release);
    }
    if (!GxOverlaps(addr, size) || gx_unmapped.exchange(true)) {
        return;
    }
    std::scoped_lock lk{pending_unmaps_mutex};
    pending_unmaps.emplace_back(gx_first << block_shift, (gx_end - gx_first) << block_shift);
    unmaps_pending.store(true, std::memory_order_release);
}

void BufferCache::Maintain() {
    maintained_epoch = packet_epoch;
    if (gx_state.load(std::memory_order_acquire) == u8(GxState::Waiting) && BbGxRing::Known()) {
        PollGxRegion(0, 0);
    }
    ProcessPendingUnmaps();
    ProcessIdleBlocks();
}

void BufferCache::PollGxRegion(VAddr addr, u64 size) {
    u8 state = gx_state.load(std::memory_order_acquire);
    if (state == u8(GxState::Waiting)) {
        if (!BbGxRing::Known()) {
            return;
        }
        u8 expected = u8(GxState::Waiting);
        if (gx_state.compare_exchange_strong(expected, u8(GxState::Importing),
                                             std::memory_order_acq_rel)) {
            StartGxImport();
        }
        state = gx_state.load(std::memory_order_acquire);
    }
    if (state == u8(GxState::Importing)) {
        // Only a binding over the region waits: bound now, its blocks would get VRAM for good.
        const u64 start = BbGxRing::g_start.load(std::memory_order_acquire);
        if (size == 0 || addr >= BbGxRing::g_end.load(std::memory_order_relaxed) ||
            addr + size <= start) {
            return;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (gx_state.load(std::memory_order_acquire) == u8(GxState::Importing) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        u8 expected = u8(GxState::Importing);
        if (gx_state.compare_exchange_strong(expected, u8(GxState::Failed),
                                             std::memory_order_acq_rel)) {
            std::printf("GPU memory: the GX region's import took over 10 s: tracked as before\n");
        }
        state = gx_state.load(std::memory_order_acquire);
    }
    if (state == u8(GxState::Imported)) {
        u8 expected = u8(GxState::Imported);
        if (gx_state.compare_exchange_strong(expected, u8(GxState::Ready),
                                             std::memory_order_acq_rel)) {
            const u64 blocks = gx_end - gx_first;
            gx_view = std::make_unique<Buffer>(instance, gx_first << block_shift,
                                               gx_import_bytes, gx_buffer, gx_address,
                                               "GX region");
            gx_buffer = vk::Buffer{};
            if (GxOwnBuffer()) {
                // The blocks' device addresses (shaders reading memory through the page table) are
                // the buffer's; set before any binding goes through it.
                const auto staging = staging_pool.Request(blocks * sizeof(vk::DeviceAddress),
                                                          MemoryType::HostUncached);
                auto* addresses = reinterpret_cast<vk::DeviceAddress*>(staging.mapped);
                for (u64 i = 0; i < blocks; ++i) {
                    addresses[i] = gx_view->BufferDeviceAddress() + (i << block_shift);
                }
                staging.Flush();
                const vk::BufferCopy copy{staging.offset, gx_first * sizeof(vk::DeviceAddress),
                                          blocks * sizeof(vk::DeviceAddress)};
                runtime.CopyBuffer(staging.buffer, bda_pagetable_buffer.get(), std::span{&copy, 1});
                gx_own.store(gx_view.get(), std::memory_order_release);
                gx_ready.store(true, std::memory_order_release);
                std::printf("GPU memory: the GX region read through a buffer of its own (%llu "
                            "MiB): no sparse binding, within the Vulkan spec (BB_GX_OWN_BUFFER=0: "
                            "bound in the arena, as 0.4.4 beta 8)\n",
                            (unsigned long long)((blocks << block_shift) >> 20));
                if (const VAddr tail = gx_tail_end.load(std::memory_order_acquire)) {
                    std::printf("GPU memory: with the page holding its last %llu bytes "
                                "(%#llx-%#llx): read-only bindings reaching into it are read in "
                                "place\n",
                                (unsigned long long)(gx_tail - (gx_end << block_shift)),
                                (unsigned long long)(gx_end << block_shift),
                                (unsigned long long)tail);
                }
            } else {
                gx_ready.store(true, std::memory_order_release);
                // All of it bound now, in one run (EnsureResident: one sparse bind, the page table
                // with it), at the game's start: not block by block as the game first uses each,
                // in its first frames (many sparse binds stalled NVIDIA GPUs).
                EnsureResident(GetArena(gx_first, gx_end - 1), gx_first, gx_end - 1);
                std::printf("GPU memory: the GX region bound in place in the arena (%llu MiB in "
                            "one run, BB_GX_OWN_BUFFER=0)\n",
                            (unsigned long long)((blocks << block_shift) >> 20));
            }
        }
    }
}

void BufferCache::NoteBeforeGx(VAddr addr, u64 size) {
    if (gx_before_noted.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    std::printf("GPU memory: a binding of the GX region's first block before the region itself "
                "(%#llx, %llu bytes; the region begins at %#llx): read in place with the block\n",
                (unsigned long long)addr, (unsigned long long)size, (unsigned long long)gx_head);
}

void BufferCache::NoteGxEdge(VAddr addr, u64 size, bool first_edge, bool texel) {
    if (gx_edge_noted[first_edge ? 0 : 1].exchange(true, std::memory_order_relaxed)) {
        return;
    }
    const VAddr end = addr + size;
    std::printf("GPU memory: the first binding across the GX region's %s edge: %#llx, %llu bytes%s; "
                "it begins %lld bytes from the region's first byte and ends %lld bytes from the "
                "byte after its last (%s); the region's buffer is %#llx-%#llx\n",
                first_edge ? "first" : "last", (unsigned long long)addr, (unsigned long long)size,
                texel ? " (texel buffer)" : "", (long long)(addr - gx_head),
                (long long)(end - gx_tail),
                end <= gx_tail && addr >= gx_head ? "inside the region"
                                                  : "reaching beyond the region",
                (unsigned long long)(gx_first << block_shift),
                (unsigned long long)(gx_end << block_shift));
}

void BufferCache::GxArenaBound() {
    if (gx_arena.exchange(true) || !gx_own.load(std::memory_order_acquire)) {
        return; // done before, or the arena is the region's way in (BB_GX_OWN_BUFFER=0)
    }
    std::printf("GPU memory: the arena was needed over the GX region (a binding across its edge, "
                "or a shader's page fault there): its blocks bound to the region's memory as "
                "well%s, as 0.4.4 beta 8 did for all of it\n",
                BbGuestMemory::ArenaTakesImports() ? "" : " (outside the Vulkan spec on this GPU)");
}

void BufferCache::StartGxImport() {
    // The region's blocks: one run of direct memory (its graphics heap), across the game's
    // mappings of it if they were split (by a protection change) but still follow each other.
    // From the block holding its first byte: the game's allocation puts 64 bytes in front of the
    // region (its base is 0x40 into a block in 1.09), and nothing else lies in that block, so the
    // whole of it is in place, for bindings and the page table alike. 0.4.4 beta 9 began at the
    // next block: bindings starting in the region's first 64 KiB crossed into the buffer and were
    // copied, every frame (about one a frame at the title, 160 KiB; 30 to 100 a second in play).
    // Not the block holding its last bytes: the rest of that one is the game's next allocation.
    const u64 start = BbGxRing::g_start.load(std::memory_order_acquire);
    const u64 end = BbGxRing::g_end.load(std::memory_order_relaxed);
    const u64 stop = end >> block_shift;
    u64 phys = 0;
    // One run of direct memory from block `from` up to address `until`.
    const auto run = [&](u64 from, u64 until) {
        uintptr_t mapping_end = 0;
        bool contiguous = from < stop &&
                          runtime_memory_direct_phys(from << block_shift, &phys, &mapping_end) &&
                          (phys & (block_size - 1)) == 0;
        for (uintptr_t at = mapping_end; contiguous && at < until;) {
            u64 next_phys = 0;
            uintptr_t next_end = 0;
            contiguous = runtime_memory_direct_phys(at, &next_phys, &next_end) &&
                         next_phys == phys + (at - (from << block_shift));
            at = next_end;
        }
        return contiguous;
    };
    u64 first = start >> block_shift;
    bool contiguous = run(first, stop << block_shift);
    if (!contiguous && (start & (block_size - 1)) != 0) {
        // The block before the region is not the same run of memory: from its first whole block,
        // as beta 9 did.
        first = (start + block_size - 1) >> block_shift;
        contiguous = run(first, stop << block_shift);
        if (contiguous) {
            std::printf("GPU memory: the GX region's first block is not one run of memory with the "
                        "rest: imported from its first whole block (bindings in its first %llu "
                        "bytes cross into it)\n",
                        (unsigned long long)((first << block_shift) - start));
        }
    }
    if (!contiguous) {
        std::printf("GPU memory: the GX region is not one block-aligned run of the game's direct "
                    "memory: tracked as before\n");
        gx_state.store(u8(GxState::Failed), std::memory_order_release);
        return;
    }
    gx_first = first;
    gx_end = stop;
    gx_head = start;
    gx_tail = end;
    const u64 bytes = (stop - first) << block_shift;
    // The page holding the region's last bytes as well, for read-only bindings (InGxTail): the
    // game binds the region's last 256 KiB and 16 bytes past it at the title screen and after
    // loads (0.4.4 beta 10's edge log), which crossed the buffer's end and were copied. Taken
    // when that page is the same run of memory and the driver imports at that granularity (its
    // minimum host pointer alignment); else the import ends at the blocks, as in beta 10.
    u64 tail_bytes = 0;
    VAddr tail_end = 0;
    if ((end & (block_size - 1)) != 0) {
        const u64 align = std::max<u64>(
            4_KB, instance.GetPhysicalDevice()
                      .getProperties2<vk::PhysicalDeviceProperties2,
                                      vk::PhysicalDeviceExternalMemoryHostPropertiesEXT>()
                      .get<vk::PhysicalDeviceExternalMemoryHostPropertiesEXT>()
                      .minImportedHostPointerAlignment);
        tail_end = (end + 4_KB - 1) & ~VAddr(4_KB - 1);
        const VAddr import_end = (end + align - 1) & ~VAddr(align - 1);
        if (import_end - (stop << block_shift) < block_size && run(first, import_end)) {
            tail_bytes = import_end - (stop << block_shift);
        } else {
            tail_end = 0;
            std::printf("GPU memory: the page holding the GX region's last bytes is not imported "
                        "with it (%s): bindings reaching into it are copied\n",
                        align >= block_size ? "the driver imports in larger steps"
                                            : "not the same run of memory");
        }
    }
    gx_import = std::async(std::launch::async, [this, phys, bytes, tail_bytes, tail_end] {
        const auto began = std::chrono::steady_clock::now();
        vk::Buffer buffer;
        vk::DeviceMemory memory;
        vk::DeviceAddress address = 0;
        const char* step = "";
        bool imported = false;
        if (tail_bytes != 0) {
            imported = BbGuestMemory::ImportRange(instance, phys, bytes + tail_bytes, AllFlags,
                                                  buffer, memory, address, &step);
            if (imported) {
                gx_import_bytes = bytes + tail_bytes;
            } else {
                std::printf("GPU memory: importing the GX region with its last page failed at "
                            "%s: imported without it (bindings reaching into it are copied)\n",
                            step);
            }
        }
        if (!imported) {
            gx_import_bytes = bytes;
            imported = BbGuestMemory::ImportRange(instance, phys, bytes, AllFlags, buffer, memory,
                                                  address, &step);
        }
        if (!imported) {
            std::printf("GPU memory: importing the GX region for the GPU failed at %s: tracked as "
                        "before\n",
                        step);
            u8 expected = u8(GxState::Importing);
            gx_state.compare_exchange_strong(expected, u8(GxState::Failed), std::memory_order_acq_rel);
            return;
        }
        gx_buffer = buffer;
        gx_address = address;
        gx_memory = memory;
        u8 expected = u8(GxState::Importing);
        if (gx_import_bytes > bytes) {
            gx_tail_end.store(tail_end, std::memory_order_release); // read once it is Ready
        }
        if (!gx_state.compare_exchange_strong(expected, u8(GxState::Imported),
                                              std::memory_order_acq_rel)) {
            gx_tail_end.store(0, std::memory_order_release);
            return; // given up on meanwhile: not used
        }
        std::printf("GPU memory: the GX region (%llu MiB) imported for the GPU in %.0f ms: what is "
                    "bound inside it is read and written in place, untracked; small read-only "
                    "bindings still copied (BB_GX_IN_PLACE=0: tracked as before)\n",
                    (unsigned long long)(bytes >> 20),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                              began)
                        .count());
    });
}

void BufferCache::ProcessPendingUnmaps() {
    if (!unmaps_pending.load(std::memory_order_acquire)) {
        return;
    }
    std::vector<std::pair<VAddr, u64>> unmaps;
    {
        std::scoped_lock lk{pending_unmaps_mutex};
        unmaps.swap(pending_unmaps);
        unmaps_pending.store(false, std::memory_order_release);
    }
    // The blocks bound to memory the game no longer maps there: unbound, and bound again (to
    // whatever is mapped then) when next used. Partly unmapped blocks go too.
    for (const auto& [addr, size] : unmaps) {
        const u64 first = addr >> block_shift, end = (addr + size + block_size - 1) >> block_shift;
        IntervalList<> unbind;
        in_place_blocks.ForEachInRange(first, end, [&](const Interval& iv) {
            unbind.Add({std::max(first, iv.start), std::min(end, iv.end)});
        });
        for (const auto& range : unbind) {
            const VAddr from = range.start << block_shift, to = range.end << block_shift;
            for (const auto& arena : arenas) {
                const VAddr a = std::max<VAddr>(from, arena.cpu_addr);
                const VAddr b = std::min<VAddr>(to, arena.cpu_addr + arena.size_bytes);
                if (a < b) {
                    BindsForArena(&arena)->binds.emplace_back(vk::SparseMemoryBind{
                        .resourceOffset = a - arena.cpu_addr,
                        .size = b - a,
                        .memory = {},
                        .memoryOffset = 0,
                    });
                }
            }
            resident_ranges.Subtract(range.start, range.end);
            in_place_blocks.Subtract(range.start, range.end);
        }
        // bbport: blocks wholly unmapped that have a VRAM copy leave VRAM too (their slots go
        // to the next blocks moved there; uploaded again if mapped and used).
        for (u64 block = (addr + block_size - 1) >> block_shift; block < (addr + size) >> block_shift;
             ++block) {
            EvictVramBlock(block);
        }
    }
    if (gx_unmapped.load(std::memory_order_relaxed) && gx_ready.exchange(false)) {
        gx_state.store(u8(GxState::Failed), std::memory_order_release);
        // Its own buffer stays alive (recorded work may still read it) until the cache goes; the
        // blocks' page table entries are cleared (a shader reading one reports a fault and the
        // block is bound again, as any evicted block).
        if (gx_own.exchange(nullptr, std::memory_order_acq_rel)) {
            runtime.FillBuffer(bda_pagetable_buffer.get(), gx_first * sizeof(vk::DeviceAddress),
                               (gx_end - gx_first) * sizeof(vk::DeviceAddress), 0u);
        }
        std::printf("GPU memory: the game unmapped its GX region: tracked as before from now on\n");
    }
}

bool BufferCache::IsInPlace(VAddr addr, u64 size) const {
    if (!InPlaceActive() || size == 0) {
        return false;
    }
    if (InGxRegion(addr, size) && gx_own.load(std::memory_order_acquire)) {
        return true; // through the region's own buffer
    }
    const u64 first = addr >> block_shift, last = (addr + size - 1) >> block_shift;
    // bbport: asked for every binding; the answer holds while the residency is unchanged.
    thread_local std::array<ResidencyMemo, 256> in_place_memo{};
    auto& memo = in_place_memo[first & (in_place_memo.size() - 1)];
    const u64 generation = ResidencyGeneration();
    if (memo.first != first || memo.last != last || memo.generation != generation) {
        memo = {first, last, generation, false, in_place_blocks.Contains(first, last + 1)};
    }
    return memo.in_place;
}

bool BufferCache::SynchronizeMemory(const Buffer* arena, VAddr device_addr, u32 size,
                                    bool is_written, bool is_texel_buffer) {
    // bbport BB_GX_IN_PLACE: the arena is the game's memory there: nothing to upload, no write
    // tracking, and GPU writes need no readback. Images aliasing it still sync.
    if (IsInPlace(device_addr, size)) {
        return is_texel_buffer && !is_written &&
               SynchronizeMemoryFromImage(arena, arena->Offset(device_addr), device_addr, size);
    }
    // Partly in place: only the parts in VRAM are synced. An upload over a block in place would
    // write an older copy of the game's memory back into it after the game wrote it again.
    if (InPlaceActive() && in_place_blocks.Overlaps(device_addr >> block_shift,
                                                   ((device_addr + size - 1) >> block_shift) + 1)) {
        const VAddr end = device_addr + size;
        bool image = false;
        in_place_blocks.ForEachGap(
            device_addr >> block_shift, ((end - 1) >> block_shift) + 1, [&](u64 first, u64 last) {
                const VAddr from = std::max<VAddr>(device_addr, first << block_shift);
                const VAddr to = std::min<VAddr>(end, last << block_shift);
                if (from < to) {
                    image |= SynchronizeMemory(arena, from, static_cast<u32>(to - from),
                                               is_written, is_texel_buffer);
                }
            });
        return image;
    }
    boost::container::small_vector<vk::BufferCopy, 4> copies;
    size_t total_size_bytes{};
    const Buffer* src_buffer{};
    BbSections::Scope track_scope{BbSections::VramTrack};
    memory_tracker->ForEachUploadRange(
        device_addr, size, is_written,
        [&](u64 addr, u64 size) {
            copies.emplace_back(total_size_bytes, addr, size);
            total_size_bytes += size;
        },
        [&] {
            // bbport (BB_GPU_WRITE_TWINS): bytes whose guest copy still matches its twin keep the
            // GPU's data in the arena; twin bytes the guest wrote since are uploaded and no
            // longer GPU-modified.
            if (TwinsEnabled() && !copies.empty()) {
                std::scoped_lock lk{twins_mutex};
                if (!twins.empty()) {
                    boost::container::small_vector<vk::BufferCopy, 4> kept;
                    boost::container::small_vector<std::pair<VAddr, VAddr>, 4> changed;
                    u64 offset = 0;
                    for (const auto& copy : copies) {
                        ForEachTwinRun(copy.dstOffset, copy.dstOffset + copy.size,
                                       [&](VAddr from, VAddr to, TwinRun run) {
                                           if (run == TwinRun::Unchanged) {
                                               twin_kept_bytes.fetch_add(to - from);
                                               return;
                                           }
                                           if (run == TwinRun::Changed) {
                                               twin_guest_bytes.fetch_add(to - from);
                                               gpu_modified_ranges.Subtract(from, to - from);
                                               changed.emplace_back(from, to);
                                           }
                                           kept.push_back({offset, from, to - from});
                                           offset += to - from;
                                       });
                    }
                    // Changed parts are uploaded now: their twins are done.
                    for (const auto& [from, to] : changed) {
                        EraseTwins(from, to);
                    }
                    copies.assign(kept.begin(), kept.end());
                    total_size_bytes = offset;
                }
            }
            BB_SECTION(VramUpload);
            src_buffer = UploadCopies(arena, copies, total_size_bytes);
        });
    track_scope.End();

    if (src_buffer) {
        runtime.CopyBuffer(src_buffer, arena, copies);
    }
    if (is_texel_buffer && !is_written) {
        return SynchronizeMemoryFromImage(arena, arena->Offset(device_addr), device_addr, size);
    }
    return false;
}

const Buffer* BufferCache::UploadCopies(const Buffer* arena, std::span<vk::BufferCopy> copies,
                                        size_t total_size_bytes) {
    if (copies.empty()) {
        return nullptr;
    }
    BbStats::buffer_upload_bytes.fetch_add(total_size_bytes, std::memory_order_relaxed);
    const auto staging = staging_pool.Request(total_size_bytes, MemoryType::HostUncached);
    // bbport: the guest memory is copied into staging on the copy threads, started now in
    // groups of about 1 MiB; submission and guest-visible fences wait for them
    // (Scheduler::WaitHostCopies).
    if (!BbToggle::Disabled(BbToggle::DeferredUploads) && total_size_bytes < 1_MB) {
        // Small uploads join the calling thread's batch.
        for (auto& copy : copies) {
            SmallGuestCopy({
                .run = &RunGuestCopy,
                .context = this,
                .source = copy.dstOffset,
                .destination = reinterpret_cast<u64>(staging.mapped + copy.srcOffset),
                .size = copy.size,
                .extra = reinterpret_cast<u64>(staging.buffer),
            });
            copy.srcOffset += staging.offset;
            copy.dstOffset -= arena->cpu_addr;
        }
        return staging.buffer;
    }
    if (!BbToggle::Disabled(BbToggle::DeferredUploads)) {
        struct HostCopy {
            VAddr source;
            u8* destination;
            u64 size;
        };
        using Group = boost::container::small_vector<HostCopy, 8>;
        auto group = std::make_shared<Group>();
        u64 group_bytes = 0;
        const auto launch = [&] {
            BbCopy::Async([group, memory = memory, staging] {
                for (const auto& copy : *group) {
                    memory->CopySparseMemory(copy.source, copy.destination, copy.size);
                }
                staging.Flush();
            });
            for (const auto& copy : *group) {
                scheduler.NoteHostCopySource(copy.source, copy.size);
            }
        };
        for (auto& copy : copies) {
            group->push_back({copy.dstOffset, staging.mapped + copy.srcOffset, copy.size});
            group_bytes += copy.size;
            copy.srcOffset += staging.offset;
            copy.dstOffset -= arena->cpu_addr;
            if (group_bytes >= 1_MB) {
                launch();
                group = std::make_shared<Group>();
                group_bytes = 0;
            }
        }
        if (!group->empty()) {
            launch();
        }
        return staging.buffer;
    }
    const auto copy = [&](std::size_t i) {
        memory->CopySparseMemory(copies[i].dstOffset, staging.mapped + copies[i].srcOffset,
                                 copies[i].size);
    };
    if (total_size_bytes >= 2_MB && copies.size() > 1) {
        BbCopy::ParallelFor(copies.size(), copy);
    } else {
        for (std::size_t i = 0; i < copies.size(); ++i) {
            copy(i);
        }
    }
    for (auto& copy : copies) {
        copy.srcOffset += staging.offset;
        copy.dstOffset -= arena->cpu_addr;
    }
    staging.Flush();
    return staging.buffer;
}

bool BufferCache::SynchronizeMemoryFromImage(const Buffer* buffer, u64 offset, VAddr device_addr,
                                             u32 size) {
    BB_SECTION(VramImage);
    if (auto type = texture_cache.IsMeta(device_addr)) {
        if (*type == TextureCache::MetaType::HTile) {
            static constexpr u32 ZmaskUncompressed = 0xf;
            runtime.FillBuffer(buffer, offset, size, ZmaskUncompressed);
            return true;
        } else {
            LOG_WARNING(Render_Vulkan, "Unhandled metadata type {}", magic_enum::enum_name(*type));
        }
    }
    // bbport: most texel buffers alias no image; remember misses until images change.
    const u64 generation = texture_cache.RegistryGeneration();
    auto& miss = image_miss_cache[((device_addr >> 6) ^ size * 0x9E3779B1u) % image_miss_cache.size()];
    if (miss.address == device_addr && miss.size == size && miss.generation == generation &&
        !BbToggle::Disabled(BbToggle::TextureBindingMemo)) {
        return false;
    }
    const ImageId image_id = texture_cache.FindImageFromRange(device_addr, size);
    if (!image_id) {
        miss = {device_addr, size, generation};
        return false;
    }
    Image& image = texture_cache.GetImage(image_id);
    ASSERT_MSG(device_addr == image.info.guest_address,
               "Texel buffer aliases image subresources {:x} : {:x}", device_addr,
               image.info.guest_address);
    const u64 arena_offset = offset;

    // bbport: left out when the range already holds the image as it is.
    // The image: no write access since (Runtime::Transit counts them in write_seq; every GPU
    // write needs a layout change after the fill's transfer read), same backing and flags, no
    // CPU write seen (tracked) and no proxy write pending. The range: no GPU write recorded
    // over it (Runtime::AccessBuffer), no newer in-place write tick, same residency; uploads of
    // CPU writes into a VRAM copy are GPU writes there. Otherwise filled as before.
    auto& savings = Vulkan::GpuSavings::Get();
    const u64 watched_size = std::max<u64>(size, image.info.guest_size);
    const bool in_place = IsInPlace(device_addr, size);
    const bool cpu_writes_seen =
        !in_place || (image.IsTracked() && image.track_addr <= device_addr &&
                      device_addr + watched_size <= image.track_addr_end);
    const auto memo_flags = static_cast<u32>(image.flags);
    const bool image_clean = False(image.flags & ImageFlagBits::Dirty);
    const bool proxy_stale = runtime.scene_targets && runtime.scene_targets->NativeStale(image);
    for (auto& memo : tile_memos) {
        if (memo.address != device_addr || memo.size != size || memo.arena != buffer ||
            memo.arena_offset != arena_offset) {
            continue;
        }
        const bool same_image = memo.image_uid == image.image_uid &&
                                memo.backing == image.backing &&
                                memo.write_seq == image.write_seq &&
                                memo.flags == memo_flags && image_clean;
        const bool same_buffer = runtime.BufferUnwritten(memo.watch) &&
                                 memo.write_tick == WriteTicks().Newest(device_addr, watched_size);
        const bool same_other = cpu_writes_seen && memo.residency == ResidencyGeneration();
        if (same_image && same_buffer && same_other && !proxy_stale) {
            savings.tile_skips.fetch_add(1, std::memory_order_relaxed);
            savings.tile_bytes_skipped.fetch_add(image.info.guest_size,
                                                 std::memory_order_relaxed);
            return true;
        }
        auto& why = !same_image   ? savings.tile_miss_image
                    : !same_buffer ? savings.tile_miss_buffer
                    : proxy_stale  ? savings.tile_miss_proxy
                                   : savings.tile_miss_other;
        why.fetch_add(1, std::memory_order_relaxed);
        memo.address = 0;
        break;
    }
    savings.tile_fills.fetch_add(1, std::memory_order_relaxed);

    boost::container::small_vector<vk::BufferImageCopy, 8> buffer_copies;
    for (u32 mip = 0; mip < image.info.resources.levels; mip++) {
        const auto& mip_info = image.info.mips_layout[mip];
        const u32 width = std::max(image.info.size.width >> mip, 1u);
        const u32 height = std::max(image.info.size.height >> mip, 1u);
        const u32 depth = std::max(image.info.size.depth >> mip, 1u);
        if (arena_offset + mip_info.offset + mip_info.size > buffer->size_bytes) {
            break;
        }
        buffer_copies.push_back(vk::BufferImageCopy{
            .bufferOffset = mip_info.offset,
            .bufferRowLength = mip_info.pitch,
            .bufferImageHeight = mip_info.height,
            .imageSubresource{
                .aspectMask = image.aspect_mask & ~vk::ImageAspectFlagBits::eStencil,
                .mipLevel = mip,
                .baseArrayLayer = 0,
                .layerCount = image.info.resources.layers,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {width, height, depth},
        });
    }
    if (buffer_copies.empty()) {
        return false;
    }
    auto& tile_manager = texture_cache.GetTileManager();
    tile_manager.TileImage(image, buffer_copies, buffer, arena_offset);
    // After the fill: its own writes are not watched, the resolve of a proxy it caused is in
    // write_seq. Images with CPU writes pending are filled every time.
    if (False(image.flags & ImageFlagBits::Dirty) && cpu_writes_seen) {
        tile_memos[tile_memo_next++ % tile_memos.size()] = {
            .address = device_addr,
            .size = size,
            .arena = buffer,
            .arena_offset = arena_offset,
            .image_uid = image.image_uid,
            .backing = image.backing,
            .write_seq = image.write_seq,
            .flags = static_cast<u32>(image.flags),
            .watch = runtime.WatchBufferWrites(buffer, arena_offset, watched_size),
            .residency = ResidencyGeneration(),
            .write_tick = WriteTicks().Newest(device_addr, watched_size),
        };
    }
    return true;
}


void BufferCache::NotePreuploadMapping(VAddr addr, u64 size, bool mapped) {
    if (!PreuploadEnabled() || size == 0) {
        return;
    }
    if (mapped) {
        // Only the game's GPU memory: direct memory of type 3 (write-combined "garlic") that the
        // GPU may access. Other memory is the CPU's; pre-uploading it would only cost VRAM.
        int prot = 0, type = -1;
        uintptr_t end = 0;
        if (!runtime_memory_vma_info(addr, &prot, &type, &end) || type != 3 || !(prot & 0x30)) {
            return;
        }
    }
    std::scoped_lock lk{preupload_mutex};
    const VAddr end = addr + size;
    // Remove what overlaps [addr, end), keeping the parts outside it.
    for (auto it = preupload_ranges.lower_bound(addr) == preupload_ranges.begin()
                       ? preupload_ranges.begin()
                       : std::prev(preupload_ranges.lower_bound(addr));
         it != preupload_ranges.end() && it->first < end;) {
        const VAddr start = it->first, stop = it->second;
        if (stop <= addr) {
            ++it;
            continue;
        }
        it = preupload_ranges.erase(it);
        if (start < addr) {
            preupload_ranges[start] = addr;
        }
        if (stop > end) {
            preupload_ranges[end] = stop;
        }
    }
    if (mapped) {
        preupload_ranges[addr] = end;
    }
}

void BufferCache::DropTwins(VAddr addr, u64 size) {
    if (TwinsEnabled()) {
        std::scoped_lock lk{twins_mutex};
        EraseTwins(addr, addr + size);
    }
}

void BufferCache::Preupload(u64 budget) {
    if (!PreuploadEnabled()) {
        return;
    }
    const bool full = PreuploadMode() == 2;
    // BB_FRAME_STATS: what the pre-upload put into the arena: regions for the first time, or again
    // (rewritten by the game since).
    static u32 report_frame = 0;
    if (BbStats::enabled && BbStats::frame_number.load(std::memory_order_relaxed) - report_frame >= 600) {
        report_frame = BbStats::frame_number.load(std::memory_order_relaxed);
        std::printf("Pre-upload: %.1f MB first, %.1f MB again in 600 frames; GPU write twins: %llu "
                    "writes without waiting, %.1f KB kept for the GPU, %.1f KB written by the game\n",
                    preupload_first_bytes / 1e6, preupload_again_bytes / 1e6,
                    (unsigned long long)twin_faults.exchange(0), twin_kept_bytes.exchange(0) / 1e3,
                    twin_guest_bytes.exchange(0) / 1e3);
        preupload_first_bytes = preupload_again_bytes = 0;
    }
    // Regions written within these frames are left alone: the game is still filling them.
    constexpr u32 QuietFrames = 10;
    // Regions looked at per call (CPU time bound): 256 x 4 MiB.
    constexpr u32 MaxRegions = 256;
    std::vector<std::pair<VAddr, VAddr>> ranges;
    {
        std::scoped_lock lk{preupload_mutex};
        ranges.assign(preupload_ranges.begin(), preupload_ranges.end());
    }
    if (ranges.empty()) {
        return;
    }
    const u32 frame = BbStats::frame_number.load(std::memory_order_relaxed);
    const u32 quiet_since = frame > QuietFrames ? frame - QuietFrames : 0;
    const u64 uploaded_before = BbStats::buffer_upload_bytes.load(std::memory_order_relaxed);
    // Start at the range holding the cursor (or the next one), wrap around once.
    std::size_t first = 0;
    while (first < ranges.size() && ranges[first].second <= preupload_cursor) {
        ++first;
    }
    if (first == ranges.size()) {
        first = 0;
    }
    u32 visited = 0;
    for (std::size_t n = 0; n < ranges.size() && visited < MaxRegions; ++n) {
        const auto [start, end] = ranges[(first + n) % ranges.size()];
        VAddr at = n == 0 ? std::max(start, preupload_cursor) : start;
        if (at >= end) {
            at = start;
        }
        while (at < end && visited < MaxRegions) {
            const VAddr region = at & ~(TRACKER_HIGHER_PAGE_SIZE - 1);
            const VAddr stop = std::min(end, region + TRACKER_HIGHER_PAGE_SIZE);
            ++visited;
            preupload_cursor = stop;
            if (memory_tracker->PreuploadCandidate(region, quiet_since, !full)) {
                const u64 first_block = at >> block_shift;
                const u64 last_block = (stop - 1) >> block_shift;
                const auto* arena = GetArena(first_block, last_block);
                const u64 region_before =
                    BbStats::buffer_upload_bytes.load(std::memory_order_relaxed);
                if (full) {
                    EnsureResident(arena, first_block, last_block);
                    SynchronizeMemory(arena, at, static_cast<u32>(stop - at), false, false);
                } else {
                    // Mode 1: only the parts already resident in the arena (no new VRAM).
                    const auto sync = [&](u64 block_begin, u64 block_end) {
                        const VAddr from = std::max<VAddr>(at, block_begin << block_shift);
                        const VAddr to = std::min<VAddr>(stop, block_end << block_shift);
                        if (from < to) {
                            SynchronizeMemory(arena, from, static_cast<u32>(to - from), false,
                                              false);
                        }
                    };
                    u64 cursor = first_block;
                    resident_ranges.ForEachGap(first_block, last_block + 1,
                                               [&](u64 gap_begin, u64 gap_end) {
                                                   sync(cursor, gap_begin);
                                                   cursor = gap_end;
                                               });
                    sync(cursor, last_block + 1);
                }
                const u64 region_bytes =
                    BbStats::buffer_upload_bytes.load(std::memory_order_relaxed) - region_before;
                if (memory_tracker->NotePreuploaded(region, frame) > 0) {
                    preupload_again_bytes += region_bytes;
                } else {
                    preupload_first_bytes += region_bytes;
                }
                const u64 uploaded =
                    BbStats::buffer_upload_bytes.load(std::memory_order_relaxed) - uploaded_before;
                if (uploaded >= budget) {
                    BbStats::preupload_bytes.fetch_add(uploaded, std::memory_order_relaxed);
                    return;
                }
            }
            at = stop;
        }
    }
    BbStats::preupload_bytes.fetch_add(
        BbStats::buffer_upload_bytes.load(std::memory_order_relaxed) - uploaded_before,
        std::memory_order_relaxed);
}

void BufferCache::SubmitPendingArenaBinds(Vulkan::SubmitInfo& info) {
    if (pending_binds.empty()) {
        return;
    }

    // bbport: blocks unbound from VRAM wait for the submissions that may still read them
    // (EvictVramBlock).
    const u64 wait_tick = bind_wait_tick;
    const auto wait_sema = scheduler.GetWorkSemaphore()->Handle();
    bind_wait_tick = 0;
    const u64 signal_tick = memory_semaphore.NextTick();
    const auto signal_sema = memory_semaphore.Handle();
    info.AddWait(signal_sema, signal_tick);

    // The binds go into the queue right before the submission (BB_ASYNC_SUBMIT: later, from a
    // recording thread), after the submissions still on their way there.
    auto binds = std::make_shared<std::vector<ArenaBinds>>(std::move(pending_binds));
    pending_binds.clear();
    info.before_submit.push_back([this, binds, wait_tick, wait_sema, signal_tick, signal_sema] {
        std::vector<vk::SparseBufferMemoryBindInfo> buffer_binds;
        buffer_binds.reserve(binds->size());
        for (const auto& arena_binds : *binds) {
            buffer_binds.emplace_back(vk::SparseBufferMemoryBindInfo{
                .buffer = arena_binds.arena->Handle(),
                .bindCount = static_cast<u32>(arena_binds.binds.size()),
                .pBinds = arena_binds.binds.data(),
            });
        }
        const vk::TimelineSemaphoreSubmitInfo timeline_si = {
            .waitSemaphoreValueCount = wait_tick ? 1u : 0u,
            .pWaitSemaphoreValues = wait_tick ? &wait_tick : nullptr,
            .signalSemaphoreValueCount = 1u,
            .pSignalSemaphoreValues = &signal_tick,
        };
        const vk::BindSparseInfo sparse_info = {
            .pNext = &timeline_si,
            .waitSemaphoreCount = wait_tick ? 1u : 0u,
            .pWaitSemaphores = wait_tick ? &wait_sema : nullptr,
            .bufferBindCount = static_cast<u32>(buffer_binds.size()),
            .pBufferBinds = buffer_binds.data(),
            .signalSemaphoreCount = 1u,
            .pSignalSemaphores = &signal_sema,
        };
        auto submit_result = instance.GetGraphicsQueue().bindSparse(sparse_info);
        if (submit_result == vk::Result::eErrorDeviceLost) {
            Vulkan::Breadcrumbs::ReportDeviceLost("submit");
        }
        ASSERT_MSG(submit_result != vk::Result::eErrorDeviceLost, "Device lost during submit");
    });
}

} // namespace VideoCore

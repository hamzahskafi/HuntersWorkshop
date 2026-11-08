// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <xxhash.h>
#include <algorithm>
#include <cstring>
#include <optional>

#include "bbport_copy.h"
#include "bbport_sections.h"
#include "bbport_toggles.h"
#include "bbport_free_check.h"
#include "engine/key_prompts.h"
#include "common/assert.h"
#include "common/debug.h"
#include "common/div_ceil.h"
#include "common/hash.h"
#include "common/scope_exit.h"
#include "core/emulator_settings.h"
#include "core/memory.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/page_manager.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/vk_gpu_savings.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/host_compatibility.h"
#include "video_core/texture_cache/overlap_diagnostics.h"
#include "video_core/texture_cache/texture_cache.h"
#include "video_core/texture_cache/tile_manager.h"
#include "video_core/vram_policy.h"

namespace VideoCore {

static constexpr u64 PageShift = 12;
static constexpr u64 NumFramesBeforeRemoval = 32;

namespace {
using Binding = TextureCache::BindingType;
/// Beta 16: this thread is building an image ahead of its first draw (RegisterImage counts no miss).
thread_local bool t_building_ahead = false;

void TraceOverlap(const ImageInfo& requested, Binding binding, ImageId id, const Image* candidate,
                  std::string_view action, std::string_view reason, int mip = -1, int slice = -1) {
    if (!ImageOverlapLogging()) {
        return;
    }

    // Detailed Logs is meant for short diagnostic captures. Skip the extremely common
    // exact-reuse path and rate-limit the rest so a loading screen cannot create a multi-GB log.
    if (action == "reuse") {
        return;
    }
    static std::atomic<u64> emitted{0};
    const u64 index = emitted.fetch_add(1, std::memory_order_relaxed);
    if (index >= 512 && (index % 1024) != 0) {
        return;
    }

    const ImageInfo empty{};
    const auto& cached = candidate ? candidate->info : empty;
    const auto requested_format = vk::to_string(requested.pixel_format);
    const auto cached_format = vk::to_string(cached.pixel_format);
    std::printf(
        "TextureDiag overlap[%llu]: action=%.*s reason=%.*s req_addr=0x%llx req_size=%llu "
        "req_fmt=%s req=%ux%ux%u mips=%u layers=%u type=%llu binding=%u "
        "candidate=%u cand_addr=0x%llx cand_size=%llu cand_fmt=%s cand=%ux%ux%u "
        "cand_mips=%u cand_layers=%u cand_type=%llu bound=%u target=%u view_mip=%d "
        "view_slice=%d\n",
        static_cast<unsigned long long>(index), static_cast<int>(action.size()), action.data(),
        static_cast<int>(reason.size()), reason.data(),
        static_cast<unsigned long long>(requested.guest_address),
        static_cast<unsigned long long>(requested.guest_size), requested_format.c_str(),
        requested.size.width, requested.size.height, requested.size.depth,
        requested.resources.levels, requested.resources.layers,
        static_cast<unsigned long long>(u64(requested.type)), u32(binding), id.index,
        static_cast<unsigned long long>(cached.guest_address),
        static_cast<unsigned long long>(cached.guest_size), cached_format.c_str(),
        cached.size.width, cached.size.height, cached.size.depth, cached.resources.levels,
        cached.resources.layers, static_cast<unsigned long long>(u64(cached.type)),
        candidate ? unsigned(candidate->binding.is_bound) : 0u,
        candidate ? unsigned(candidate->binding.is_target) : 0u, mip, slice);
    std::fflush(stdout);
}

bool ReusableFormat(const ImageInfo& requested, const ImageInfo& cached, Binding binding,
                    bool exact) {
    if (cached.pixel_format == vk::Format::eUndefined ||
        (exact && requested.pixel_format != cached.pixel_format)) {
        return false;
    }
    if (binding == Binding::DepthTarget) {
        return cached.props.is_depth && requested.props.has_stencil == cached.props.has_stencil &&
               requested.num_bits == cached.num_bits;
    }
    if (binding == Binding::Storage || binding == Binding::RenderTarget) {
        if (cached.props.is_depth) {
            return false;
        }
    }
    if (binding == Binding::Texture && cached.props.is_depth &&
        Vulkan::LiverpoolToVK::IsFormatDepthCompatible(requested.pixel_format)) {
        // ImageView promotes R16/R32 sampling to the cached depth image's actual format.
        return true;
    }
    return IsVulkanFormatCompatible(cached.pixel_format, requested.pixel_format);
}

std::string_view ReuseRejection(const ImageInfo& requested, const ImageInfo& cached, Binding binding,
                               bool exact, int& mip, int& slice) {
    mip = slice = 0;
    if (requested.guest_address != cached.guest_address) {
        mip = requested.MipOf(cached);
        slice = mip >= 0 ? requested.SliceOf(cached, mip) : -1;
        if (mip < 0 || slice < 0) {
            return "not an aligned, contained mip/slice";
        }
    }
    if (const auto reason = requested.ViewRejection(cached, {u32(mip), u32(slice)}); !reason.empty()) {
        return reason;
    }
    return ReusableFormat(requested, cached, binding, exact)
               ? std::string_view{} : "format or binding requires another backing image";
}
} // namespace

TextureCache::TextureCache(const Vulkan::Instance& instance_, Vulkan::Scheduler& scheduler_,
                           Vulkan::Runtime& runtime_, AmdGpu::Liverpool* liverpool_,
                           BufferCache& buffer_cache_, PageManager& tracker_)
    : instance{instance_}, scheduler{scheduler_}, runtime{runtime_}, liverpool{liverpool_},
      buffer_cache{buffer_cache_}, tracker{tracker_}, blit_helper{instance, scheduler},
      tile_manager{instance, scheduler, runtime, buffer_cache.GetStreamBuffer()},
      readback_linear_images{EmulatorSettings.IsReadbackLinearImagesEnabled()} {

    u32 max_samplers = instance.GetMaxSamplerAllocationCount();
    trigger_gc_samplers = max_samplers * 3 / 4;
    pressure_gc_samplers = max_samplers * 7 / 8;
    critical_gc_samplers = max_samplers * 15 / 16;
    // Images: the VRAM manager decides when they go (vram_policy.h, GarbageCollectImages).
}

namespace {
/// bbport (GPU hang guard): the collector never frees, nor writes back, an image used in the
/// last this many collector passes, one per guest submission and several a frame
/// (BB_GC_RECENT_FRAMES, default 48; 0: off). The memo hits that bind most
/// images (texture sets, binding and target memos: MarkFound) do not touch the LRU, so an image
/// sampled every frame looked idle: it was freed while in use, re-created and uploaded again,
/// and GPU-written render targets went through a synchronous write-back each time (stalls of
/// 0.3-2 s in area loads before a VK_ERROR_DEVICE_LOST in a full-screen blit).
u64 GcRecentFrames() {
    static const u64 frames = [] {
        const char* env = std::getenv("BB_GC_RECENT_FRAMES");
        return std::min<u64>(env && env[0] ? std::strtoull(env, nullptr, 10) : 48, 60);
    }();
    return frames;
}
/// bbport (GPU hang guard): a deleted image's Vulkan objects outlive its unregistration by this
/// many collector passes (submissions), then the GPU's completion of the tick of that pass
/// (BB_IMAGE_FREE_DELAY_FRAMES, default 32; 0: only the GPU tick, as shadPS4). A reference that outlived the registry change
/// (a memo, a proxy, the upscaler) then reads a still valid image instead of freed memory.
u64 ImageFreeDelayFrames() {
    static const u64 frames = [] {
        const char* env = std::getenv("BB_IMAGE_FREE_DELAY_FRAMES");
        return std::min<u64>(env && env[0] ? std::strtoull(env, nullptr, 10) : 32, 240);
    }();
    return frames;
}
} // namespace

TextureCache::~TextureCache() = default;

/// bbport BB_REFRESH_TRACE=1 (diagnostics): for images of 4 MiB and up, the writes that mark one
/// dirty (and their kind, InvalidateReason) and its refreshes, with the flags they found and what
/// they did: each combination once, 150 lines at most (loading screens would use up a plain
/// count before the frames that repeat one).
namespace RefreshTrace {
static bool On() {
    static const bool on = [] {
        const char* env = std::getenv("BB_REFRESH_TRACE");
        return env && env[0] == '1';
    }();
    return on;
}
static bool Large(const Image& image) {
    return On() && image.info.guest_size >= (4ull << 20);
}
static bool First(VAddr image, const char* kind, const char* what, u32 flags) {
    static std::mutex mutex;
    static std::vector<std::tuple<VAddr, const char*, const char*, u32>> seen;
    std::scoped_lock lock{mutex};
    const auto key = std::make_tuple(image, kind, what, flags);
    if (seen.size() >= 150 || std::find(seen.begin(), seen.end(), key) != seen.end()) {
        return false;
    }
    seen.push_back(key);
    return true;
}
static void Mark(const Image& image, const char* what, VAddr addr, u64 size) {
    if (!Large(image) ||
        !First(image.info.guest_address, invalidate_reason, what, u32(image.flags))) {
        return;
    }
    const auto format = vk::to_string(image.info.pixel_format);
    std::printf("Refresh trace: %s write %#llx (+%#llx, %#llx bytes) marks image %#llx %s "
                "%ux%u (%#llx bytes, flags %#x) %s\n",
                invalidate_reason, static_cast<unsigned long long>(addr),
                static_cast<unsigned long long>(addr - image.info.guest_address),
                static_cast<unsigned long long>(size),
                static_cast<unsigned long long>(image.info.guest_address), format.c_str(),
                image.info.size.width, image.info.size.height,
                static_cast<unsigned long long>(image.info.guest_size), u32(image.flags), what);
}
static void Refresh(const Image& image, u32 flags, const char* outcome) {
    if (!Large(image) || !First(image.info.guest_address, "refresh", outcome, flags)) {
        return;
    }
    const auto format = vk::to_string(image.info.pixel_format);
    std::printf("Refresh trace: image %#llx %s %ux%u, %u mips, flags %#x: %s\n",
                static_cast<unsigned long long>(image.info.guest_address), format.c_str(),
                image.info.size.width, image.info.size.height, image.info.resources.levels, flags,
                outcome);
}
} // namespace RefreshTrace

namespace {
// 0.4.6 beta 8, part 1 (BB_EMPTY_SURFACES=0: off): a surface the game made without its data for the GPU
// to write starts from its predecessor's image or empty, never from the game's memory.
bool EmptySurfaces() {
    static const bool on = [] {
        const char* env = std::getenv("BB_EMPTY_SURFACES");
        return !(env && env[0] == '0');
    }();
    return on;
}
// 0.4.6 beta 8, part 3 (BB_TARGET_POOL=0: off): a new surface takes the Vulkan image of a dead target of
// its shape (TextureCache::TakeDeadDonor) instead of allocating one.
bool DeadTargetPool() {
    static const bool on = [] {
        const char* env = std::getenv("BB_TARGET_POOL");
        return !(env && env[0] == '0');
    }();
    return on;
}
// 0.4.6 beta 8, part 4; beta 9 the default: the pages of the game's targets (surfaces made without data
// for the GPU, textures with a render-target or depth view) are not write-protected: the game's calls tell
// what writes them (beta 8's runs: none of them written by the CPU). 0.4.6 beta 10 the default: textures
// made with their data are not either (beta 9's run: their CPU writes are the create call's own copy, told
// by the created event, ProcessGxCreated, and asset loads, told by Rasterizer::NoteAssetWrite).
// BB_WATCH_GAME_TEXTURES=1 watches every image as before; =0 is the default; =targets is beta 9's.
enum class Unwatch { None, Targets, TargetsAndData };
Unwatch UnwatchKinds() {
    static const Unwatch kinds = [] {
        const char* env = std::getenv("BB_WATCH_GAME_TEXTURES");
        if (env && env[0] == '1') {
            return Unwatch::None;
        }
        return env && std::strcmp(env, "targets") == 0 ? Unwatch::Targets : Unwatch::TargetsAndData;
    }();
    return kinds;
}
// 0.4.6 beta 9 (BB_TAKE_EARLIER_WRITES=0: off; on by default): a surface whose memory the GPU wrote as a
// buffer before the game made it takes those writes (another owner's) at its first use, as before.
bool TakeEarlierWrites() {
    static const bool on = [] {
        const char* env = std::getenv("BB_TAKE_EARLIER_WRITES");
        return !(env && env[0] == '0');
    }();
    return on;
}
} // namespace

std::string TextureCache::DescribeImagesIn(VAddr addr, u64 size) {
    std::unique_lock lock{mutex, std::try_to_lock}; // the caller may hold it already
    if (!lock.owns_lock()) {
        return " [images: not looked up]";
    }
    std::string out;
    int shown = 0;
    ForEachImageInRegion(addr, size, [&](ImageId, Image& image) {
        if (shown++ < 3) {
            out += fmt::format(" [image {:#x}+{:#x} {}x{} fmt {} tile {}{}{}]", image.info.guest_address,
                               image.info.guest_size, image.info.size.width,
                               image.info.size.height, u32(image.info.pixel_format),
                               u32(image.info.tile_mode), image.usage.render_target ? " RT" : "",
                               True(image.flags & ImageFlagBits::GpuModified) ? " gpu" : "");
        }
    });
    if (shown > 3) {
        out += fmt::format(" +{} more", shown - 3);
    }
    return out;
}

void TextureCache::ProcessDownloadImages() {
    // bbport: called for every fence (~450 a frame): the lock only when something is queued.
    if (!downloads_queued.load(std::memory_order_acquire)) {
        return;
    }
    std::unique_lock lk{download_images_mutex};
    downloads_queued.store(false, std::memory_order_relaxed);
    for (const ImageId image_id : download_images) {
        DownloadImageMemory(image_id, true);
    }
    download_images.clear();
}

void TextureCache::DownloadImageMemory(ImageId image_id, bool sync) {
    Image& image = slot_images[image_id];
    if (False(image.flags & ImageFlagBits::GpuModified)) {
        return;
    }
    const u32 download_size = image.info.pitch * image.info.size.height * image.info.size.depth *
                              image.info.resources.layers * (image.info.num_bits / 8);
    ASSERT(download_size <= image.info.guest_size);
    image.mip_hashes_current = false; // its data lands in guest memory
    const auto download =
        runtime.GetStagingPool().Request(download_size, MemoryType::HostCached, 16, !sync);
    const vk::BufferImageCopy image_download = {
        .bufferOffset = download.offset,
        .bufferRowLength = image.info.pitch,
        .bufferImageHeight = image.info.size.height,
        .imageSubresource =
            {
                .aspectMask = image.info.props.is_depth ? vk::ImageAspectFlagBits::eDepth
                                                        : vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = image.info.resources.layers,
            },
        .imageOffset = {0, 0, 0},
        .imageExtent = {image.info.size.width, image.info.size.height, image.info.size.depth},
    };
    runtime.DownloadImage(&image, download.buffer, std::span{&image_download, 1});
    if (sync) {
        scheduler.Finish();
        download.Invalidate();
        BbFreeCheck::Check(image.info.guest_address, download_size, download.mapped,
                           BbFreeCheck::ImageDownload);
        Core::Memory::Instance()->TryWriteBacking(std::bit_cast<u8*>(image.info.guest_address),
                                                  download.mapped, download_size);
    } else {
        scheduler.DeferPriorityOperation(
            [this, device_addr = image.info.guest_address, download, download_size] {
                download.Invalidate();
                BbFreeCheck::Check(device_addr, download_size, download.mapped,
                                   BbFreeCheck::ImageDownload);
                Core::Memory::Instance()->TryWriteBacking(std::bit_cast<u8*>(device_addr),
                                                          download.mapped, download_size);
                runtime.GetStagingPool().FreeDeferred(download);
            });
    }
}

// Hunter's Workshop: freeing an image the GPU drew while VRAM is full (0.4.5 beta 14: without
// waiting for the GPU; texture_cache.h, BeginStore). Its contents exist only in VRAM, in linear
// form; the game's memory holds the surface in its own layout. A tiled image is copied out
// linearly and tiled by the tiling shader into a host buffer that already holds the guest bytes,
// writing only texels inside the image's extent (TileImage's extent_only): padding rows and
// columns, and whatever lies beyond a render target's hinted size, keep the game's bytes. A linear
// one is copied out as DownloadImageMemory does. The buffer then replaces the guest bytes.
bool TextureCache::BeginStore(ImageId image_id) {
    Image& image = slot_images[image_id];
    if (False(image.flags & ImageFlagBits::GpuModified) || image.store_pending) {
        return false;
    }
    const ImageInfo& info = image.info;
    const VAddr address = info.guest_address;
    const bool tiled = info.props.is_tiled;
    if (address == 0 || info.guest_size == 0 || !image.backing) {
        return false;
    }
    u64 size = info.guest_size;
    if (tiled) {
        // What the tiling shader can express: one colour plane, single-sampled, thin tiling, no
        // compression metadata, whole texels of a size it has a pipeline for; not a scene proxy's
        // source (its newest contents may be in the reduced-resolution copy).
        const bool plain_colour =
            image.aspect_mask == vk::ImageAspectFlags{vk::ImageAspectFlagBits::eColor} &&
            !info.props.is_depth && !info.props.has_stencil && info.stencil_addr == 0 &&
            !info.props.is_block && !info.props.is_volume && info.num_samples == 1 &&
            AmdGpu::GetMicroTileThickness(info.array_mode) == 1 && !image.scene_proxy &&
            info.meta_info.cmask_addr == 0 && info.meta_info.fmask_addr == 0 &&
            info.meta_info.htile_addr == 0;
        const u32 bits = info.num_bits;
        const bool texel_size =
            bits == 8 || bits == 16 || bits == 32 || bits == 64 || bits == 96 || bits == 128;
        if (!plain_colour || !texel_size) {
            return false;
        }
        const u32 texel_bytes = bits / 8;
        // The shader runs in groups of 64 texels over the whole surface, and finds a texel's mip by
        // walking the mip sizes: the surface must be whole groups, its mips back to back, each
        // pitch x height x layers texels.
        const u32 levels = info.resources.levels;
        const u32 layers = info.resources.layers;
        if (size % (u64{texel_bytes} * 64) != 0 || levels == 0 || layers == 0 ||
            levels > info.mips_layout.size()) {
            return false;
        }
        u64 next_offset = 0;
        for (u32 level = 0; level < levels; ++level) {
            const auto& mip = info.mips_layout[level];
            const u64 expected = u64{mip.pitch} * mip.height * texel_bytes * layers;
            if (mip.pitch == 0 || mip.height == 0 || (mip.pitch | mip.height) % 8 != 0 ||
                mip.offset != next_offset || mip.size != expected) {
                return false;
            }
            next_offset += mip.size;
        }
        if (next_offset != size) {
            return false;
        }
    } else {
        size = u64(info.pitch) * info.size.height * info.size.depth * info.resources.layers *
               (info.num_bits / 8);
        if (size == 0 || size > info.guest_size) {
            return false;
        }
    }

    // Guest memory must take all of it, and nothing else may have GPU data there that storing
    // this image would overwrite (the buffer cache's, or another image's).
    auto* const memory = Core::Memory::Instance();
    if (memory->ClampRangeSize(address, size) != size ||
        buffer_cache.IsRegionGpuModified(address, size)) {
        return false;
    }
    PendingStore store{};
    bool others_hold_gpu_data = false;
    ForEachImageInRegion(address, size, [&](ImageId other_id, Image& other) {
        if (other_id != image_id) {
            store.others.emplace_back(other_id, other.image_uid);
            others_hold_gpu_data |= True(other.flags & ImageFlagBits::GpuModified);
        }
    });
    if (others_hold_gpu_data) {
        return false;
    }

    // Held until the store is done (FreeDeferred in CompletePendingStores).
    store.staging = runtime.GetStagingPool().Request(
        size, MemoryType::HostCached, std::max<u64>(instance.StorageMinAlignment(), 16), true);
    if (!store.staging.mapped) {
        runtime.GetStagingPool().FreeDeferred(store.staging);
        return false;
    }
    if (tiled) {
        // The guest bytes first (the shader leaves the padding as it finds it), then the image.
        memory->CopySparseMemory(address, store.staging.mapped, size);
        store.staging.Flush();
        boost::container::small_vector<vk::BufferImageCopy, 14> copies;
        for (u32 level = 0; level < info.resources.levels; ++level) {
            const auto& mip = info.mips_layout[level];
            copies.push_back(vk::BufferImageCopy{
                .bufferOffset = mip.offset,
                .bufferRowLength = mip.pitch,
                .bufferImageHeight = mip.height,
                .imageSubresource{vk::ImageAspectFlagBits::eColor, level, 0,
                                  info.resources.layers},
                .imageOffset = {0, 0, 0},
                .imageExtent = {std::min(mip.pitch, std::max(info.size.width >> level, 1u)),
                                std::min(mip.height, std::max(info.size.height >> level, 1u)), 1},
            });
        }
        scheduler.EndRendering(Vulkan::GpuSavings::PassBreak::TextureCache);
        tile_manager.TileImage(image, copies, store.staging.buffer, store.staging.offset,
                               /*extent_only=*/true);
    } else {
        const vk::BufferImageCopy copy = {
            .bufferOffset = store.staging.offset,
            .bufferRowLength = info.pitch,
            .bufferImageHeight = info.size.height,
            .imageSubresource =
                {
                    .aspectMask = info.props.is_depth ? vk::ImageAspectFlagBits::eDepth
                                                      : vk::ImageAspectFlagBits::eColor,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = info.resources.layers,
                },
            .imageOffset = {0, 0, 0},
            .imageExtent = {info.size.width, info.size.height, info.size.depth},
        };
        runtime.DownloadImage(&image, store.staging.buffer, std::span{&copy, 1});
    }
    runtime.FlushBarriers();
    store.image_id = image_id;
    store.image_uid = image.image_uid;
    store.address = address;
    store.size = size;
    store.tick = scheduler.CurrentTick();
    store.accessed = image.tick_accessed_last;
    store.tiled = tiled;
    image.store_pending = true;
    pending_stores.push_back(std::move(store));
    // Its pages all watched, a game write there meets it (InvalidateMemory, LandStore). Otherwise
    // the store lands now, waiting for the GPU, as before beta 14.
    if (image.track_addr != address || image.track_addr_end != address + info.guest_size) {
        scheduler.Finish();
        LandStore(pending_stores.back(), true);
    }
    return true;
}

bool TextureCache::LandStore(PendingStore& store, bool from_collector) {
    if (store.landed) {
        return store.stored;
    }
    // GPU work wrote the game's memory there meanwhile, through a buffer (InvalidateMemoryFromGPU
    // marks the image GpuDirty; the buffer cache holds the newer bytes): the copy is older than
    // that and is dropped. (The buffer cache is asked on the collector's thread only.)
    Image& image = slot_images[store.image_id];
    if (True(image.flags & ImageFlagBits::GpuDirty) ||
        (from_collector && buffer_cache.IsRegionGpuModified(store.address, store.size))) {
        store.landed = true;
        store.stored = false;
        return false;
    }
    if (!scheduler.IsFree(store.tick)) {
        // A game write met the image before the GPU had copied it out: this copy alone, already
        // sent (the collector submits at the end of its pass).
        scheduler.WaitSubmitted(store.tick);
        scheduler.GetWorkSemaphore()->Wait(store.tick);
    }
    store.landed = true;
    store.staging.Invalidate();
    BbFreeCheck::Check(store.address, store.size, store.staging.mapped, BbFreeCheck::ImageDownload);
    store.stored = Core::Memory::Instance()->TryWriteBacking(std::bit_cast<u8*>(store.address),
                                                             store.staging.mapped, store.size);
    if (!store.stored) {
        return false;
    }
    // The game's memory now holds what the image does.
    image.flags &= ~ImageFlagBits::GpuModified;
    image.mip_hashes_current = false;
    // The other images over the range read guest memory again at their next use.
    for (const auto& [other_id, uid] : store.others) {
        if (!slot_images.is_allocated(other_id) || slot_images[other_id].image_uid != uid) {
            continue;
        }
        Image& other = slot_images[other_id];
        RefreshTrace::Mark(other, "CPU dirty (image stored)", store.address, store.size);
        other.flags |= ImageFlagBits::CpuDirty;
        UntrackImage(other_id);
    }
    return true;
}

void TextureCache::CompletePendingStores() {
    for (size_t i = 0; i < pending_stores.size();) {
        PendingStore& store = pending_stores[i];
        if (!store.orphaned && !store.landed && !scheduler.IsFree(store.tick)) {
            ++i;
            continue;
        }
        if (!store.orphaned) {
            Image& image = slot_images[store.image_id];
            image.store_pending = false;
            if (!store.landed && image.tick_accessed_last != store.accessed) {
                // Used again since: it stays (drawn into again, the copy is out of date).
                ++gc_stores_cancelled;
            } else if (LandStore(store, true)) {
                ++(store.tiled ? gc_tiled_written : gc_downloads);
                ++gc_evictions;
                BbStats::gc_freed_images.fetch_add(1, std::memory_order_relaxed);
                NoteCollected(image, gc_second, VramPolicy::Freed::Texture);
                FreeImage(store.image_id);
            } else {
                // The game's memory did not take it (a mapping lost meanwhile): it stays, tried
                // again later, twice as late after each further failure (up to 1920 passes).
                image.store_retry_tick = gc_tick + (u64{60} << std::min<u8>(image.store_failures, 5));
                image.store_failures = static_cast<u8>(std::min(image.store_failures + 1, 255));
                ++gc_tiled_kept;
            }
        }
        runtime.GetStagingPool().FreeDeferred(store.staging);
        pending_stores[i] = std::move(pending_stores.back());
        pending_stores.pop_back();
    }
}

void TextureCache::OrphanStore(ImageId image_id) {
    Image& image = slot_images[image_id];
    image.store_pending = false;
    for (PendingStore& store : pending_stores) {
        if (!store.orphaned && store.image_id == image_id && store.image_uid == image.image_uid) {
            store.orphaned = true;
        }
    }
}

void TextureCache::DumpImagesAt(VAddr address, const char* dir) {
    boost::container::small_vector<ImageId, 4> ids;
    {
        std::scoped_lock lock{mutex};
        ForEachImageInRegion(address, 1, [&](ImageId image_id, Image& image) {
            if (image.info.guest_address == address) {
                ids.push_back(image_id);
            }
        });
    }
    std::printf("Image dump %#llx: %zu images\n", static_cast<unsigned long long>(address),
                ids.size());
    u32 n = 0;
    for (const ImageId image_id : ids) {
        Image& image = slot_images[image_id];
        const auto format = vk::to_string(image.info.pixel_format);
        const u32 width = image.info.size.width, height = image.info.size.height;
        std::printf("  image %u: %s %ux%u tile %u, flags %#x, layers %u, mips %u, %s\n",
                    image_id.index, format.c_str(), width, height,
                    static_cast<u32>(image.info.tile_mode), static_cast<u32>(image.flags),
                    image.info.resources.layers, image.info.resources.levels,
                    image.info.props.is_depth ? "depth" : "color");
        if (image.info.props.is_block) {
            continue;
        }
        const u32 bytes_per_pixel = image.info.props.is_depth ? 4 : image.info.num_bits / 8;
        const u64 size = u64(width) * height * bytes_per_pixel;
        const auto download = runtime.GetStagingPool().Request(size, MemoryType::HostCached, 16);
        const vk::BufferImageCopy copy = {
            .bufferOffset = download.offset,
            .imageSubresource = {image.info.props.is_depth ? vk::ImageAspectFlagBits::eDepth
                                                           : vk::ImageAspectFlagBits::eColor,
                                 0, 0, 1},
            .imageExtent = {width, height, 1},
        };
        runtime.DownloadImage(&image, download.buffer, std::span{&copy, 1});
        scheduler.Finish();
        download.Invalidate();
        const std::string path = std::format("{}/img_{:x}_{}_{}x{}_{}.raw", dir, address, n++, width,
                                             height, format);
        if (FILE* f = std::fopen(path.c_str(), "wb")) {
            std::fwrite(download.mapped, 1, size, f);
            std::fclose(f);
        }
    }
}

/// bbport: a hash of guest memory for the refresh checks. From 1 MiB on, 256 KiB chunks are
/// hashed on the copy threads and then their hashes: a 1080p R32F image the game refreshes every
/// frame (its own compute copy of the depth buffer) cost ~1 ms of hashing on the draw recording
/// thread. Only compared with hashes made here.
/// 0.4.6 beta 7: the steady clock in ns (the registry's clock: engine/gx_resources.cpp NowNs).
static u64 NowSteadyNs() {
    return u64(std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count());
}

static u64 GuestMemoryHash(const void* data, u64 size) {
    constexpr u64 Chunk = 256 * 1024;
    if (size < 4 * Chunk || !BbCopy::Enabled()) {
        return XXH3_64bits(data, size);
    }
    const auto* bytes = static_cast<const u8*>(data);
    const u64 count = (size + Chunk - 1) / Chunk;
    boost::container::small_vector<u64, 128> hashes(count);
    BbCopy::ParallelFor(count, [&](std::size_t i) {
        const u64 offset = i * Chunk;
        hashes[i] = XXH3_64bits(bytes + offset, std::min(Chunk, size - offset));
    });
    return XXH3_64bits(hashes.data(), count * sizeof(u64));
}

/// 0.4.6 beta 4 (Image::ResolveDeferredHashes): the hashes of an upload made without them. The
/// memory as uploaded if it is unchanged since (tracked the whole time, no write), else none: a
/// value no hash takes in practice, so the next refresh of the GPU's contents uploads (as a hash
/// of changed memory would have).
void Image::ResolveDeferredHashesNow() {
    hashes_deferred = false;
    constexpr u64 NoHash = 0x9e3779b97f4a7c15ull;
    constexpr u32 CpuDirtyBits = u32(ImageFlagBits::MaybeCpuDirty) | u32(ImageFlagBits::CpuDirty);
    const bool unchanged = mip_hashes_current && (u32(flags) & CpuDirtyBits) == 0;
    for (u32 m = 0; m < info.resources.levels && m < mip_hashes.size(); ++m) {
        if (!unchanged) {
            mip_hashes[m] = NoHash;
            continue;
        }
        const auto [mip_size, mip_pitch, mip_height, mip_offset] = info.mips_layout[m];
        mip_hashes[m] = GuestMemoryHash(std::bit_cast<u8*>(info.guest_address) + mip_offset, mip_size);
    }
    if (!unchanged) {
        mip_hashes_current = false;
    }
}

/// bbport: the guest memory a MaybeCpuDirty check compares, the same at marking and at
/// refresh (they hashed different ranges, the whole image and its first 8x8 pixels, so the
/// first check never matched). Such an image lies within the faulting page: cheap to hash
/// whole, and a CPU write past its first pixels still counts.
u64 TextureCache::MaybeDirtyHash(const Image& image) {
    return GuestMemoryHash(std::bit_cast<const u8*>(image.info.guest_address), image.info.guest_size);
}

void TextureCache::MarkAsMaybeDirty(ImageId image_id, Image& image) {
    if (image.hash == 0) {
        // Initialize hash. bbport: from a copy read by CopySparseMemory (through the backing view
        // with BB_READBACKS=2), the same bytes MaybeDirtyHash reads. This runs in the fault
        // handlers with `mutex` held: a read fault on a GPU-written page of the image sent a
        // guest thread to wait for the GPU command thread, which drains the draw recording
        // thread first, which waited for `mutex`.
        std::vector<u8> bytes(image.info.guest_size);
        Core::Memory::Instance()->CopySparseMemory(image.info.guest_address, bytes.data(),
                                                   bytes.size());
        image.hash = GuestMemoryHash(bytes.data(), bytes.size());
    }
    image.flags |= ImageFlagBits::MaybeCpuDirty;
    UntrackImage(image_id);
}

void TextureCache::InvalidateMemory(VAddr addr, size_t size) {
    std::scoped_lock lock{mutex};
    const auto pages_start = PageManager::GetPageAddr(addr);
    const auto pages_end = PageManager::GetNextPageAddr(addr + size - 1);
    ForEachImageInRegion(pages_start, pages_end - pages_start, [&](ImageId image_id, Image& image) {
        const auto image_begin = image.info.guest_address;
        const auto image_end = image.info.guest_address + image.info.guest_size;
        if (image.store_pending) {
            // Beta 14: a store into these pages is under way (BeginStore): it lands first, then
            // the game's write goes on top, as with a store done on the spot.
            for (PendingStore& store : pending_stores) {
                if (!store.orphaned && store.image_id == image_id) {
                    LandStore(store, false);
                }
            }
        }
        image.mip_hashes_current = false;
        // 0.4.6 beta 8: a surface of the game's whose resource lives (part 1): the game's calls say what
        // writes it, and none writes it from the CPU; a write in its pages is counted, its contents kept.
        bool kept = false;
        if (image.surface_ns != 0) {
            kept = BbEngine::GxResources::StillLive(image_begin, image.surface_ns);
            if (!kept) {
                image.surface_ns = 0;
            }
        }
        if (image.Overlaps(addr, size) && BbEngine::GxResources::Active()) {
            // Part 5: the write faults on the game's resources, by the live one there.
            const auto owner = BbEngine::GxResources::Classify(image_begin, std::max(image_end, image_begin + 1));
            BbEngine::GxResources::NoteCpuWrite(owner, kept, image_begin,
                                                image.backing ? image.GetHostImageSize() : image.info.guest_size,
                                                image.info.size.width, image.info.size.height);
            // 0.4.6 beta 9: who writes a texture made with its data (the first few, with the code).
            if (owner == BbEngine::GxResources::Owner::TextureData) {
                BbEngine::GxResources::NoteDataTextureWrite(image_begin, image.info.guest_size, image.info.size.width,
                                                            image.info.size.height);
            }
        }
        if (kept) {
            RefreshTrace::Mark(image, "CPU write counted, a live surface's contents kept", addr, size);
            // Untracked as the paths below untrack (the pages are unprotected now), without marking it.
            if (image.Overlaps(addr, size)) {
                UntrackImage(image_id);
            } else if (pages_end < image_end) {
                UntrackImageHead(image_id);
            } else if (image_begin < pages_start) {
                UntrackImageTail(image_id);
            } else {
                UntrackImage(image_id);
            }
            return;
        }
        if (image.Overlaps(addr, size)) {
            // Modified region overlaps image, so the image was definitely accessed by this fault.
            // Untrack the image, so that the range is unprotected and the guest can write freely.
            RefreshTrace::Mark(image, "CPU dirty", addr, size);
            // Beta 15: written over by the game; garbage once it has gone unused (GarbagePass).
            if (False(image.flags & ImageFlagBits::CpuDirty) && garbage_candidates.size() < 16384) {
                garbage_candidates.emplace_back(image_id, image.image_uid);
            }
            image.flags |= ImageFlagBits::CpuDirty;
            PageManager::NoteImageFault(image.info.guest_address, image.info.guest_size,
                                        image.info.size.width, image.info.size.height,
                                        u32(image.info.pixel_format), u32(image.info.tile_mode));
            UntrackImage(image_id);
        } else if (pages_end < image_end) {
            // This page access may or may not modify the image.
            // We should not mark it as dirty now. If it really was modified
            // it will receive more invalidations on its other pages.
            // Remove tracking from this page only.
            UntrackImageHead(image_id);
        } else if (image_begin < pages_start) {
            // This page access does not modify the image but the page should be untracked.
            // We should not mark this image as dirty now. If it really was modified
            // it will receive more invalidations on its other pages.
            UntrackImageTail(image_id);
        } else {
            // Image begins and ends on this page so it can not receive any more invalidations.
            // We will check it's hash later to see if it really was modified.
            MarkAsMaybeDirty(image_id, image);
        }
    });
}

void TextureCache::ReleaseDestroyed(VAddr addr, size_t size) {
    if (size == 0 || !WatchesPages(addr, size)) {
        return; // no image watches it: nothing to release (most textures not drawn yet)
    }
    const VideoCore::InvalidateReason reason{"the game destroyed its texture"};
    std::scoped_lock lock{mutex};
    ForEachImageInRegion(addr, size, [&](ImageId image_id, Image& image) {
        const VAddr begin = image.info.guest_address, end = begin + image.info.guest_size;
        if (begin < addr || end > addr + size) {
            return; // a neighbour sharing a page: its watch stays
        }
        // Beta 19: only an image that is the destroyed texture's alone. One the GPU drew (or draws
        // into) keeps its contents, and one another live texture of the game's covers (the game
        // places textures in the same memory) is that texture's: dirtying either brought stale
        // memory back over it at its next draw (beta 17-18: a broken surface in the Hunter's Dream,
        // a white streak at an inside/outside boundary).
        if (True(image.flags & ImageFlagBits::GpuModified) || image.binding.is_target ||
            BbEngine::GxResources::LiveTextureOverlaps(begin, end)) {
            return;
        }
        if (image.store_pending) {
            for (PendingStore& store : pending_stores) {
                if (!store.orphaned && store.image_id == image_id) {
                    LandStore(store, false);
                }
            }
        }
        image.mip_hashes_current = false;
        if (False(image.flags & ImageFlagBits::CpuDirty) && garbage_candidates.size() < 16384) {
            garbage_candidates.emplace_back(image_id, image.image_uid);
        }
        image.flags |= ImageFlagBits::CpuDirty;
        UntrackImage(image_id);
    });
}

bool TextureCache::KeyPromptData(const Image& image, std::vector<u8>& data) {
    const ImageInfo& info = image.info;
    const bool icon = info.guest_size == KeyPrompts::kIconBytes && info.size.width == 32;
    const bool hud = info.guest_size == KeyPrompts::kHudBytes && info.size.width == 1024;
    if ((!icon && !hud) || info.resources.levels != 1 || info.resources.layers != 1 ||
        (info.pixel_format != vk::Format::eBc7UnormBlock &&
         info.pixel_format != vk::Format::eBc7SrgbBlock)) {
        return false;
    }
    const u8* guest = std::bit_cast<const u8*>(info.guest_address);
    const int texture = KeyPrompts::Identify(guest, info.guest_size, info.size.width, info.size.height);
    if (texture < 0) {
        return false;
    }
    {
        std::scoped_lock lock{key_icons_mutex};
        const std::pair<VAddr, u32> where{info.guest_address, info.guest_size};
        if (std::find(key_icons.begin(), key_icons.end(), where) == key_icons.end()) {
            // The game loads its icons once; a few reloads elsewhere at most.
            if (key_icons.size() >= 256) {
                key_icons.erase(key_icons.begin());
            }
            key_icons.push_back(where);
        }
    }
    data.assign(guest, guest + info.guest_size);
    return KeyPrompts::Draw(texture, data.data(), data.size());
}

void TextureCache::UpdateKeyPrompts() {
    if (!KeyPrompts::Changed()) {
        return;
    }
    std::vector<std::pair<VAddr, u32>> icons;
    {
        std::scoped_lock lock{key_icons_mutex};
        icons = key_icons;
    }
    // As a CPU write to them would: uploaded again where next drawn.
    for (const auto& [addr, size] : icons) {
        InvalidateMemory(addr, size);
    }
}

void TextureCache::InvalidateMemoryFromGPU(VAddr address, size_t max_size) {
    std::scoped_lock lock{mutex};
    ForEachImageInRegion(address, max_size, [&](ImageId image_id, Image& image) {
        // Only consider images that match base address.
        // TODO: Maybe also consider subresources
        if (image.info.guest_address != address) {
            return;
        }
        // Ensure image is reuploaded when accessed again.
        RefreshTrace::Mark(image, "GPU dirty", address, max_size);
        if (False(image.flags & ImageFlagBits::GpuDirty) && garbage_candidates.size() < 16384) {
            garbage_candidates.emplace_back(image_id, image.image_uid);
        }
        image.flags |= ImageFlagBits::GpuDirty;
    });
}

void TextureCache::UnmapMemory(VAddr cpu_addr, size_t size) {
    std::scoped_lock lk{mutex};

    ImageIds deleted_images;
    ForEachImageInRegion(cpu_addr, size, [&](ImageId id, Image&) { deleted_images.push_back(id); });
    for (const ImageId id : deleted_images) {
        // TODO: Download image data back to host.
        FreeImage(id);
    }
}

ImageId TextureCache::ResolveDepthOverlap(const ImageInfo& requested_info, BindingType binding,
                                          ImageId cache_image_id, bool exact_fmt) {
    auto& cache_image = slot_images[cache_image_id];

    if (!cache_image.info.props.is_depth && !requested_info.props.is_depth) {
        return {};
    }

    const bool stencil_match =
        requested_info.props.has_stencil == cache_image.info.props.has_stencil;
    const bool bpp_match = requested_info.num_bits == cache_image.info.num_bits;

    // If an image in the cache has less slices we need to expand it
    bool recreate = !requested_info.ViewRejection(cache_image.info).empty() ||
                    (exact_fmt && requested_info.pixel_format != cache_image.info.pixel_format);

    switch (binding) {
    case BindingType::Texture:
        // The guest requires a depth sampled texture, but cache can offer only Rxf. Need to
        // recreate the image.
        recreate |= requested_info.props.is_depth && !cache_image.info.props.is_depth;
        break;
    case BindingType::Storage:
        // If the guest is going to use previously created depth as storage, the image needs to be
        // recreated. (TODO: Probably a case with linear rgba8 aliasing is legit)
        recreate |= cache_image.info.props.is_depth;
        break;
    case BindingType::RenderTarget:
        // Render target can have only Rxf format. If the cache contains only Dx[S8] we need to
        // re-create the image.
        ASSERT(!requested_info.props.is_depth);
        recreate |= cache_image.info.props.is_depth;
        break;
    case BindingType::DepthTarget:
        // The guest has requested previously allocated texture to be bound as a depth target.
        // In this case we need to convert Rx float to a Dx[S8] as requested
        recreate |= !cache_image.info.props.is_depth;

        // The guest is trying to bind a depth target and cache has it. Need to be sure that aspects
        // and bpp match
        recreate |= cache_image.info.props.is_depth && !(stencil_match && bpp_match);
        break;
    default:
        break;
    }

    if (recreate) {
        // Use a real covering allocation's layout. Combining independently larger mip/layer
        // counts or recomputing attachment padding can invent guest memory/shift later mips.
        // The existing color-channel -> MS depth conversion has different samples/bpp but the
        // same byte footprint; normalize only these two fields for the geometry check.
        auto source_layout = cache_image.info;
        source_layout.num_bits = requested_info.num_bits;
        source_layout.num_samples = requested_info.num_samples;
        const bool request_covers = source_layout.ViewRejection(requested_info).empty();
        const bool cache_covers = requested_info.ViewRejection(source_layout).empty();
        if (!request_covers && !cache_covers) {
            TraceOverlap(requested_info, binding, cache_image_id, &cache_image, "reject",
                         "depth conversion has incompatible guest subresource layout");
            return {};
        }
        auto new_info = request_covers ? requested_info : source_layout;
        new_info.pixel_format = requested_info.pixel_format;
        new_info.props.is_depth = requested_info.props.is_depth;
        new_info.props.has_stencil = requested_info.props.has_stencil;
        new_info.meta_info = requested_info.meta_info;
        new_info.stencil_addr = requested_info.stencil_addr;
        new_info.stencil_size = requested_info.stencil_size;
        int mip{}, slice{};
        if (!ReuseRejection(requested_info, new_info, binding, exact_fmt, mip, slice).empty()) {
            return {};
        }
        const auto new_image_id = slot_images.insert(instance, runtime, slot_image_views, new_info);
        RegisterImage(new_image_id);

        // SlotVector insertion may relocate images. Reacquire the source after allocation.
        auto& source_image = slot_images[cache_image_id];
        auto& new_image = slot_images[new_image_id];
        new_image.usage = source_image.usage;
        RefreshImage(new_image);
        RefreshImage(source_image);
        // When creating a depth buffer through overlap resolution don't clear it on first use.
        new_image.info.meta_info.htile_clear_mask = 0;
        // 0.4.4 beta 18: the conversions between kinds, in the frame statistics.
        if (source_image.info.props.is_depth != new_image.info.props.is_depth) {
            const u32 kind = source_image.info.props.is_depth ? 1 : 0;
            Vulkan::GpuSavings::Count(Vulkan::GpuSavings::Get().overlap_conversions[kind]);
        }
        runtime.CopyColorAndDepth(&source_image, &new_image);

        // Keep bound attachments valid until the renderer notices the replacement.
        if (source_image.binding.is_bound || source_image.binding.is_target) {
            source_image.binding.needs_rebind = 1u;
        }
        new_image.binding.is_target = source_image.binding.is_target;
        FreeImage(cache_image_id);
        TrackImage(new_image_id);
        return new_image_id;
    }

    int mip{}, slice{};
    return ReuseRejection(requested_info, cache_image.info, binding, exact_fmt, mip, slice).empty()
               ? cache_image_id : ImageId{};
}

std::tuple<ImageId, int, int> TextureCache::ResolveOverlap(const ImageInfo& image_info,
                                                         BindingType binding,
                                                         ImageId cache_image_id,
                                                         ImageId merged_image_id,
                                                         bool exact_fmt) {
    auto& cached = slot_images[cache_image_id];
    if (cache_image_id == merged_image_id || False(cached.flags & ImageFlagBits::Registered) ||
        cached.info.pixel_format == vk::Format::eUndefined) {
        // Undefined images are stencil/depth association records, not reusable Vulkan images.
        return {{}, -1, -1};
    }
    const auto reject = [&](std::string_view reason) -> std::tuple<ImageId, int, int> {
        TraceOverlap(image_info, binding, cache_image_id, &cached, "reject", reason);
        // Retain the old cheap retirement of cold aliases, but leave bound/GPU-modified
        // images to normal GC (which can write them back). Rejection must not lose GPU data.
        if (!cached.binding.is_bound && !cached.binding.is_target &&
            False(cached.flags & ImageFlagBits::GpuModified) &&
            scheduler.CurrentTick() - cached.tick_accessed_last > NumFramesBeforeRemoval) {
            FreeImage(cache_image_id);
        }
        return {{}, -1, -1};
    };

    // Once a covering image has been selected, only merge its actual child subresources.
    // Do not replace it with a later, smaller alias and lose the previously selected view.
    if (!merged_image_id && image_info.guest_address == cached.info.guest_address) {
        if (image_info.BlockDim() != cached.info.BlockDim() ||
            image_info.num_bits * image_info.num_samples !=
                cached.info.num_bits * cached.info.num_samples) {
            return reject("different block dimensions or sample footprint");
        }
        if (image_info.array_mode != cached.info.array_mode ||
            image_info.tile_mode != cached.info.tile_mode ||
            image_info.alt_tile != cached.info.alt_tile) {
            return reject("different guest tiling layout");
        }
        if (image_info.type != cached.info.type) {
            const bool volume_copy =
                (image_info.type == AmdGpu::ImageType::Color3D &&
                 cached.info.type == AmdGpu::ImageType::Color2D &&
                 image_info.size.depth == cached.info.resources.layers) ||
                (cached.info.type == AmdGpu::ImageType::Color3D &&
                 image_info.type == AmdGpu::ImageType::Color2D &&
                 cached.info.size.depth == image_info.resources.layers);
            if (volume_copy && image_info.guest_size == cached.info.guest_size &&
                image_info.resources.levels == cached.info.resources.levels &&
                image_info.IsCompatible(cached.info)) {
                TraceOverlap(image_info, binding, cache_image_id, &cached, "expand",
                             "2D/3D backing conversion");
                return {ExpandImage(image_info, cache_image_id), -1, -1};
            }
            return reject("different image type");
        }
        const bool cached_covers = cached.info.resources.Contains(image_info.resources);
        const bool request_covers = image_info.resources.Contains(cached.info.resources);
        if (!cached_covers && !request_covers) {
            // Taking a component-wise maximum here could invent guest memory beyond BOTH
            // allocations (e.g. more mips versus more layers). Preserve this legitimate alias.
            return reject("crossed mip/layer extents; neither image contains the other");
        }
        if (const auto depth = ResolveDepthOverlap(image_info, binding, cache_image_id, exact_fmt)) {
            TraceOverlap(image_info, binding, depth, &slot_images[depth],
                         depth == cache_image_id ? "view" : "expand", "depth/color binding");
            return {depth, -1, -1};
        }

        int mip{}, slice{};
        const auto reason = ReuseRejection(image_info, cached.info, binding, exact_fmt, mip, slice);
        if (reason.empty()) {
            TraceOverlap(image_info, binding, cache_image_id, &cached, "view", "covering backing");
            return {cache_image_id, mip, slice};
        }
        if (request_covers && image_info.IsCompatible(cached.info) &&
            cached.info.ViewRejection(image_info).empty() &&
            // Changing the number of layers relocates later guest mips. Only a one-mip
            // source, or an unchanged layer stride, can be preserved by CopyImage.
            (cached.info.resources.levels == 1 ||
             image_info.resources.layers == cached.info.resources.layers)) {
            TraceOverlap(image_info, binding, cache_image_id, &cached, "expand", reason);
            return {ExpandImage(image_info, cache_image_id), -1, -1};
        }
        return reject(reason);
    }

    if (!merged_image_id && image_info.guest_address > cached.info.guest_address) {
        int mip{}, slice{};
        const auto reason = ReuseRejection(image_info, cached.info, binding, exact_fmt, mip, slice);
        if (!reason.empty()) {
            return reject(reason);
        }
        TraceOverlap(image_info, binding, cache_image_id, &cached, "view",
                     "contained subresource", mip, slice);
        return {cache_image_id, mip, slice};
    }

    // Preserve a cached mip/array slice, including a currently bound render target, before
    // retiring it. Previously the target branch freed it before any larger image/copy existed.
    const int mip = cached.info.MipOf(image_info);
    const int slice = mip >= 0 ? cached.info.SliceOf(image_info, mip) : -1;
    if (mip < 0 || slice < 0 ||
        !cached.info.ViewRejection(image_info, {u32(mip), u32(slice)}).empty() ||
        !IsVulkanFormatCompatible(image_info.pixel_format, cached.info.pixel_format)) {
        return reject("cached image is not a covered child subresource");
    }
    if (!merged_image_id) {
        merged_image_id = slot_images.insert(instance, runtime, slot_image_views, image_info);
        RegisterImage(merged_image_id);
        RefreshImage(slot_images[merged_image_id]);
    }
    // Inserting the parent may relocate SlotVector storage.
    auto& source = slot_images[cache_image_id];
    auto& merged = slot_images[merged_image_id];
    RefreshImage(source);
    runtime.CopyMip(&source, &merged, mip, slice);
    if (source.binding.is_bound || source.binding.is_target) {
        source.binding.needs_rebind = 1u;
    }
    merged.binding.is_target |= source.binding.is_target;
    TraceOverlap(image_info, binding, cache_image_id, &source, "merge",
                 "copy child before retiring backing", mip, slice);
    FreeImage(cache_image_id);
    TrackImage(merged_image_id);
    return {merged_image_id, -1, -1};
}

ImageId TextureCache::ExpandImage(const ImageInfo& info, ImageId image_id) {
    const auto new_info = info;
    const auto new_image_id = slot_images.insert(instance, runtime, slot_image_views, new_info);
    RegisterImage(new_image_id);

    auto& src_image = slot_images[image_id];
    auto& new_image = slot_images[new_image_id];

    RefreshImage(new_image);
    RefreshImage(src_image);
    runtime.CopyImage(&src_image, &new_image);
    new_image.binding.is_target = src_image.binding.is_target;

    if (src_image.binding.is_bound || src_image.binding.is_target) {
        src_image.binding.needs_rebind = 1u;
    }

    FreeImage(image_id);
    TrackImage(new_image_id);
    return new_image_id;
}

ImageId TextureCache::FindImage(ImageDesc& desc, bool exact_fmt) {
    const auto& info = desc.info;
    ASSERT(info.guest_address != 0);

    std::scoped_lock lock{mutex};

    // 0.4.6 beta 10: the game's pending destroys and creates are taken before a draw looks (a texture
    // made with its data marks the images over it written, ProcessGxCreated).
    if (BbEngine::GxResources::Following() && BbEngine::GxResources::CreatedWaiting()) {
        ProcessGxDestroyed();
        ProcessGxCreated();
    }

    u64 key_hash = info.guest_address ^ info.guest_size << 17 ^ u64(info.pixel_format) << 40 ^
                   u64(info.size.width) << 24 ^ info.size.height ^ u64(desc.type) << 58 ^
                   u64(exact_fmt) << 63;
    key_hash = (key_hash ^ key_hash >> 33) * 0xFF51AFD7ED558CCDull;
    key_hash = (key_hash ^ key_hash >> 33) * 0xC4CEB9FE1A85EC53ull;
    key_hash ^= key_hash >> 33;
    auto& cached = find_image_cache[key_hash % find_image_cache.size()];
    // An exact match stays valid while that image is registered with the same description;
    // resolved overlaps (views into other images) only until any image registration changes.
    const auto still_exact = [&] {
        if (cached.view_mip >= 0 || cached.view_slice >= 0 || !cached.image_id ||
            !slot_images.is_allocated(cached.image_id)) {
            return false;
        }
        const Image& image = slot_images[cached.image_id];
        return True(image.flags & ImageFlagBits::Registered) &&
               image.info.guest_address == info.guest_address &&
               image.info.guest_size == info.guest_size && image.info.size == info.size &&
               image.info.pixel_format == info.pixel_format && image.info.type == info.type &&
               image.info.resources.Contains(info.resources) &&
               image.info.LayoutKey() == info.LayoutKey();
    };
    if ((cached.generation == registry_generation.load(std::memory_order_relaxed) || still_exact()) &&
        cached.address == info.guest_address &&
        cached.size == info.guest_size && cached.extent == info.size &&
        cached.format == info.pixel_format && cached.type == info.type &&
        cached.exact_fmt == exact_fmt && cached.binding == desc.type &&
        cached.levels == info.resources.levels && cached.layers == info.resources.layers &&
        cached.layout_key == info.LayoutKey() &&
        !BbToggle::Disabled(BbToggle::FindImageCache)) {
        Image& image = slot_images[cached.image_id];
        NoteOwnerTake(image);
        image.tick_accessed_last = scheduler.CurrentTick();
        TouchImage(image);
        if (cached.view_mip > 0) {
            desc.view_info.range.base.level += cached.view_mip;
        }
        if (cached.view_slice > 0) {
            desc.view_info.range.base.layer += cached.view_slice;
        }
        return cached.image_id;
    }

    ImageIds image_ids;
    ForEachImageInRegion(info.guest_address, info.guest_size,
                         [&](ImageId image_id, Image& image) { image_ids.push_back(image_id); });

    ImageId image_id{};

    int view_mip{-1};
    int view_slice{-1};
    // Preserve the exact-match fast path, but validate independent mip/layer coverage too.
    // Other covering parents go through resolution so modified child targets can be merged.
    for (const auto cache_id : image_ids) {
        const auto& candidate = slot_images[cache_id];
        if (candidate.info.pixel_format == vk::Format::eUndefined ||
            candidate.info.guest_address != info.guest_address ||
            candidate.info.guest_size != info.guest_size || candidate.info.size != info.size ||
            candidate.info.resources != info.resources) {
            continue;
        }
        int mip{}, slice{};
        const auto reason = ReuseRejection(info, candidate.info, desc.type, exact_fmt, mip, slice);
        if (reason.empty()) {
            image_id = cache_id;
            view_mip = mip ? mip : -1;
            view_slice = slice ? slice : -1;
            TraceOverlap(info, desc.type, cache_id, &candidate,
                         mip || slice ? "view" : "reuse", "covers requested resources", mip, slice);
            break;
        }
        TraceOverlap(info, desc.type, cache_id, &candidate, "reject reuse", reason, mip, slice);
    }

    if (!image_id) {
        for (const auto cache_id : image_ids) {
            const auto& candidate = slot_images[cache_id];
            if (cache_id == image_id || False(candidate.flags & ImageFlagBits::Registered)) {
                continue;
            }
            // Exact-format callers cannot end up with a differently formatted covering view.
            // A conversion/expansion may still create the requested format, so let it proceed.
            const auto& merged_info = image_id ? slot_images[image_id].info : info;
            const auto [resolved, mip, slice] =
                ResolveOverlap(merged_info, desc.type, cache_id, image_id, exact_fmt);
            if (!resolved || resolved == image_id) {
                continue; // Keep the original subresource location on non-matches/child merges.
            }
            int original_mip{}, original_slice{};
            const auto reason = ReuseRejection(info, slot_images[resolved].info, desc.type,
                                               exact_fmt, original_mip, original_slice);
            if (!reason.empty()) {
                TraceOverlap(info, desc.type, resolved, &slot_images[resolved], "reject result", reason);
                continue;
            }
            image_id = resolved;
            view_mip = original_mip ? original_mip : -1;
            view_slice = original_slice ? original_slice : -1;
        }
    }
    // Create and register a new image
    if (!image_id) {
        view_mip = view_slice = -1;
        TraceOverlap(info, desc.type, {}, nullptr, "create", "no candidate covers requested view");
        // 0.4.6 beta 8 (part 3): a new surface takes a dead target's Vulkan image of its shape. The
        // game's destroys and creates are taken first, so a target re-created over the same memory has
        // had its old image handed over (beta 7) before any is given to another address.
        ImageId donor_id{};
        UniqueImage donated{};
        if (DeadTargetPool() && EmptySurfaces() && BbEngine::GxResources::Following() &&
            BbEngine::GxResources::SurfaceAt(info.guest_address,
                                             info.guest_address + std::max<u64>(info.guest_size, 1))
                .found) {
            if (BbEngine::GxResources::DestroyedWaiting() || BbEngine::GxResources::CreatedWaiting()) {
                ProcessGxDestroyed();
                ProcessGxCreated();
            }
            donor_id = TakeDeadDonor(info);
            if (donor_id) {
                donated = std::move(slot_images[donor_id].backing->image);
                t_adopt_image = &donated;
            } else {
                BbEngine::GxResources::NotePool(BbEngine::GxResources::PoolEvent::NoShape, 0);
            }
        }
        image_id = slot_images.insert(instance, runtime, slot_image_views, info);
        t_adopt_image = nullptr;
        if (donor_id) {
            Image& donor = slot_images[donor_id];
            if (donated.image) {
                donor.backing->image = std::move(donated); // another creation info: it stays the donor's
                BbEngine::GxResources::NotePool(BbEngine::GxResources::PoolEvent::NoShape, 0);
            } else {
                BbEngine::GxResources::NotePool(BbEngine::GxResources::PoolEvent::Taken,
                                                slot_images[image_id].GetHostImageSize());
                FreeImage(donor_id); // its views go once the GPU is done; its Vulkan image is the new one's
            }
        }
        RegisterImage(image_id);
    }

    Image& image = slot_images[image_id];
    NoteOwnerTake(image);
    image.tick_accessed_last = scheduler.CurrentTick();
    TouchImage(image);

    // If the image requested is a subresource of the image from cache record its location.
    if (view_mip > 0) {
        desc.view_info.range.base.level += view_mip;
    }
    if (view_slice > 0) {
        desc.view_info.range.base.layer += view_slice;
    }

    cached = FindImageCacheEntry{
        .address = info.guest_address,
        .size = info.guest_size,
        .extent = info.size,
        .format = info.pixel_format,
        .type = info.type,
        .exact_fmt = exact_fmt,
        .binding = desc.type,
        .levels = info.resources.levels,
        .layers = info.resources.layers,
        .layout_key = info.LayoutKey(),
        .generation = registry_generation.load(std::memory_order_relaxed),
        .image_id = image_id,
        .view_mip = view_mip,
        .view_slice = view_slice,
    };
    return image_id;
}

bool TextureCache::IsExactMatch(ImageId image_id, const ImageInfo& info) const {
    if (!image_id || !slot_images.is_allocated(image_id)) {
        return false;
    }
    const Image& image = slot_images[image_id];
    return True(image.flags & ImageFlagBits::Registered) &&
           image.info.guest_address == info.guest_address &&
           image.info.guest_size == info.guest_size && image.info.size == info.size &&
           image.info.pixel_format == info.pixel_format && image.info.type == info.type &&
           image.info.resources == info.resources && image.info.LayoutKey() == info.LayoutKey();
}

ImageId TextureCache::FindImageFromRange(VAddr address, size_t size, bool ensure_valid) {
    ImageIds image_ids;
    ForEachImageInRegion(address, size, [&](ImageId image_id, Image& image) {
        if (image.info.guest_address != address) {
            return;
        }
        if (ensure_valid && !image.SafeToDownload()) {
            return;
        }
        image_ids.push_back(image_id);
    });
    if (image_ids.size() == 1) {
        // Sometimes image size might not exactly match with requested buffer size
        // If we only found 1 candidate image use it without too many questions.
        return image_ids.back();
    }
    if (!image_ids.empty()) {
        for (s32 i = 0; i < image_ids.size(); ++i) {
            Image& image = slot_images[image_ids[i]];
            if (image.info.guest_size == size) {
                return image_ids[i];
            }
        }
        LOG_WARNING(Render_Vulkan,
                    "Failed to find exact image match for copy addr={:#x}, size={:#x}", address,
                    size);
    }
    return {};
}

ImageId TextureCache::FindImageLayer(VAddr address, size_t size, u32& layer) {
    // A layer is the image's guest size over its layers (one level: a point light's shadow cube).
    ImageId found{};
    ForEachImageInRegion(address, size, [&](ImageId image_id, Image& image) {
        const auto& info = image.info;
        const u32 layers = info.resources.layers;
        if (found || info.resources.levels != 1 || layers < 2 || info.guest_size % layers != 0 ||
            address < info.guest_address) {
            return;
        }
        const u64 layer_size = info.guest_size / layers;
        const u64 offset = address - info.guest_address;
        if (size == layer_size && offset % layer_size == 0 && offset / layer_size < layers) {
            layer = u32(offset / layer_size);
            found = image_id;
        }
    });
    return found;
}

ImageView& TextureCache::FindTexture(ImageId image_id, const ImageDesc& desc, ViewMemo* memo) {
    Image& image = slot_images[image_id];
    // Beta 16: built ahead and now drawn (counted once; several recording threads may get here).
    if (image.ahead == 1 && __atomic_exchange_n(&image.ahead, u8(2), __ATOMIC_RELAXED) == 1) {
        BbEngine::GxResources::NoteAheadHit();
    }
    if (desc.type == BindingType::Storage) {
        image.ResolveDeferredHashes(); // 0.4.6 beta 4: before the GPU draws into it
        image.flags |= ImageFlagBits::GpuModified;
        if (readback_linear_images && (!image.info.props.is_tiled || image.info.size.width <= 8) &&
            image.info.guest_address != 0) {
            std::unique_lock lk{download_images_mutex};
            download_images.emplace(image_id);
            downloads_queued.store(true, std::memory_order_release);
        }
    }
    UpdateImage(image_id);
    if (memo && !BbToggle::Disabled(BbToggle::TextureViewMemo)) {
        if (memo->image_id == image_id && memo->backing == image.backing && memo->view_id) {
            return slot_image_views[memo->view_id];
        }
        ImageView& view = image.FindView(desc.view_info);
        const auto& ids = image.backing->image_view_ids;
        const u32 last = image.backing->last_view;
        memo->image_id = image_id;
        memo->backing = image.backing;
        memo->view_id = last < ids.size() && &slot_image_views[ids[last]] == &view ? ids[last]
                                                                                    : ids.back();
        return view;
    }
    return image.FindView(desc.view_info);
}

ImageView& TextureCache::FindRenderTarget(ImageId image_id, const ImageDesc& desc) {
    Image& image = slot_images[image_id];
    image.ResolveDeferredHashes(); // 0.4.6 beta 4: before the GPU draws into it
    image.flags |= ImageFlagBits::GpuModified;
    if (readback_linear_images && (!image.info.props.is_tiled || image.info.size.width <= 8)) {
        std::unique_lock lk{download_images_mutex};
        download_images.emplace(image_id);
        downloads_queued.store(true, std::memory_order_release);
    }
    image.usage.render_target = 1u;
    UpdateImage(image_id);

    // Register meta data for this color buffer
    if (desc.info.meta_info.cmask_addr) {
        surface_metas.emplace(desc.info.meta_info.cmask_addr,
                              MetaDataInfo{.type = MetaType::CMask});
        image.info.meta_info.cmask_addr = desc.info.meta_info.cmask_addr;
    }

    if (desc.info.meta_info.fmask_addr) {
        surface_metas.emplace(desc.info.meta_info.fmask_addr,
                              MetaDataInfo{.type = MetaType::FMask});
        image.info.meta_info.fmask_addr = desc.info.meta_info.fmask_addr;
    }

    return image.FindView(desc.view_info, false);
}

ImageView& TextureCache::FindDepthTarget(ImageId image_id, const ImageDesc& desc) {
    Image& image = slot_images[image_id];
    image.ResolveDeferredHashes(); // 0.4.6 beta 4: before the GPU draws into it
    image.flags |= ImageFlagBits::GpuModified;
    image.usage.depth_target = 1u;
    UpdateImage(image_id);

    // Register meta data for this depth buffer
    if (desc.info.meta_info.htile_addr) {
        surface_metas.emplace(desc.info.meta_info.htile_addr,
                              MetaDataInfo{.type = MetaType::HTile,
                                           .clear_mask = image.info.meta_info.htile_clear_mask});
        image.info.meta_info.htile_addr = desc.info.meta_info.htile_addr;
    }

    // If there is a stencil attachment, link depth and stencil.
    if (desc.info.stencil_addr != 0) {
        ImageId stencil_id{};
        ForEachImageInRegion(
            desc.info.stencil_addr, desc.info.stencil_size, [&](ImageId image_id, Image& image) {
                if (image.info.guest_address != desc.info.stencil_addr) {
                    return;
                }
                // Not a color image that reuses the memory: its reads and storage writes would reach
                // the depth buffer through an invalid view (shadPS4#5118).
                if (image.info.pixel_format == vk::Format::eUndefined ||
                    Vulkan::LiverpoolToVK::IsFormatStencilCompatible(image.info.pixel_format)) {
                    stencil_id = image_id;
                }
            });
        if (!stencil_id) {
            ImageInfo info{};
            info.guest_address = desc.info.stencil_addr;
            info.guest_size = desc.info.stencil_size;
            info.size = desc.info.size;
            stencil_id = slot_images.insert(instance, runtime, slot_image_views, info);
            RegisterImage(stencil_id);
        }
        const auto depth_uid = slot_images[image_id].image_uid;
        Image& stencil_image = slot_images[stencil_id];
        TouchImage(stencil_image);
        stencil_image.AssociateDepth(image_id, depth_uid);
    }

    return slot_images[image_id].FindView(desc.view_info, false);
}

bool TextureCache::StartSurface(Image& image) {
    using namespace BbEngine::GxResources;
    const auto owner = Owner(image.gx_owner);
    const u64 bytes = image.backing ? image.GetHostImageSize() : image.info.guest_size;
    if (image.info.num_samples > 1 || image.info.props.is_block || image.info.props.is_volume ||
        image.info.pixel_format == vk::Format::eUndefined || !image.HasMemory()) {
        return false;
    }
    // The GPU wrote this memory as a buffer before (a compute shader, a copy): the fill takes that, as
    // bbhost's target takes a texture a shader stored there first.
    bool skip_earlier = false;
    if (buffer_cache.IsRegionGpuModified(image.info.guest_address, image.info.guest_size)) {
        // 0.4.6 beta 9: writes made before the resource was (its memory's previous owner's) or since.
        if (BbEngine::GxResources::CreatedWaiting()) {
            ProcessGxDestroyed();
            ProcessGxCreated();
        }
        const auto at = created_gpu_version.find(image.info.guest_address);
        const bool earlier = at != created_gpu_version.end() && at->second.first == image.surface_ns &&
                             buffer_cache.GpuWriteVersion(image.info.guest_address, image.info.guest_size) ==
                                 at->second.second;
        if (!earlier) {
            NoteSurfaceStart(owner, SurfaceStart::Taken, bytes);
            return false;
        }
        if (TakeEarlierWrites()) {
            NoteSurfaceStart(owner, SurfaceStart::TakenEarlier, bytes);
            return false;
        }
        skip_earlier = true;
    }
    if (!StillLive(image.info.guest_address, image.surface_ns)) {
        image.surface_ns = 0; // its resource is gone: the image is filled and followed as any other
        NoteSurfaceStart(owner, SurfaceStart::Gone, bytes);
        return false;
    }
    // On the console its memory holds whatever was there; the game draws or clears before it reads.
    // Zero, as bbhost starts its new targets (BBHOST_RT_ZERO): a glare level read past the part its
    // pass draws there kept the heap's values, NaNs among them.
    const SubresourceRange range{.base = {0, 0},
                                 .extent = {image.info.resources.levels, image.info.resources.layers}};
    vk::ClearValue value{};
    SurfaceStart how = SurfaceStart::Empty;
    if (image.info.props.is_depth) {
        value.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        how = image.info.meta_info.htile_clear_mask != 0 ? SurfaceStart::DepthCleared : SurfaceStart::DepthEmpty;
    } else {
        value.color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};
    }
    const u32 entry_flags = u32(image.flags);
    runtime.ClearImage(&image, range, value);
    image.skip_hashes_once = false;
    image.hashes_deferred = false;
    image.mip_hashes_current = false;
    RefreshTrace::Refresh(image, entry_flags, "started empty: a surface the game made without its data");
    NoteSurfaceStart(owner, skip_earlier ? SurfaceStart::EarlierSkipped : how, bytes);
    return true;
}

void TextureCache::RefreshImage(Image& image) {
    if (False(image.flags & ImageFlagBits::Dirty) || image.info.num_samples > 1) {
        return;
    }
    // 0.4.6 beta 8: an image nothing has written yet (no upload, clear, copy or draw): a surface of the
    // game's starts empty (part 1); any other is filled from memory, counted by the resource there.
    if (image.write_seq == 0) {
        if (image.surface_ns != 0 && StartSurface(image)) {
            return;
        }
        BbEngine::GxResources::NoteFirstFill(BbEngine::GxResources::Owner(image.gx_owner),
                                             image.backing ? image.GetHostImageSize() : image.info.guest_size);
    }
    BbStats::Timer timer{BbStats::t_refresh};
    BbStats::image_checks.fetch_add(1, std::memory_order_relaxed);

    RENDERER_TRACE;
    TRACE_HINT(fmt::format("{:x}:{:x}", image.info.guest_address, image.info.guest_size));
    // 0.4.6 beta 4: the hashes of an upload made without them come first; the upload of an image
    // built when the game created its texture takes none (it was made from the game's data just
    // now and holds no GPU contents: the hashes are only compared once the GPU has drawn into it,
    // Image::ResolveDeferredHashes). That hashing was a third of the GPU thread's time in the
    // game's bursts of new textures.
    image.ResolveDeferredHashes();
    const bool skip_hashes = image.skip_hashes_once && False(image.flags & ImageFlagBits::GpuModified);
    image.skip_hashes_once = false;
    const u32 entry_flags = u32(image.flags);

    if (True(image.flags & ImageFlagBits::MaybeCpuDirty) &&
        False(image.flags & ImageFlagBits::CpuDirty)) {
        const u64 hash = MaybeDirtyHash(image);
        if (image.hash == hash) {
            image.flags &= ~ImageFlagBits::MaybeCpuDirty;
            RefreshTrace::Refresh(image, entry_flags, "unchanged (maybe dirty check)");
            return;
        }
        image.hash = hash;
    }

    const u32 num_layers = image.info.resources.layers;
    const u32 num_mips = image.info.resources.levels;
    const bool is_gpu_modified = True(image.flags & ImageFlagBits::GpuModified);
    const bool is_gpu_dirty = True(image.flags & ImageFlagBits::GpuDirty);
    // bbport: a GPU-dirty refresh uploads the buffer cache's data, and the guest memory the mip
    // hashes record is unchanged unless something wrote it since they were taken (which clears
    // mip_hashes_current): they are kept instead of hashing it again. The game's compute copy of
    // the depth buffer into a 1080p R32F image made that ~1 ms of every frame on the draw
    // recording thread.
    constexpr u32 CpuDirtyBits = u32(ImageFlagBits::MaybeCpuDirty) | u32(ImageFlagBits::CpuDirty);
    const bool keep_hashes = is_gpu_dirty && image.mip_hashes_current &&
                             (entry_flags & CpuDirtyBits) == 0;

    BbStats::image_upload_bytes.fetch_add(image.info.guest_size, std::memory_order_relaxed);
    boost::container::small_vector<vk::BufferImageCopy, 14> image_copies;
    for (u32 m = 0; m < num_mips; m++) {
        const u32 width = std::max(image.info.size.width >> m, 1u);
        const u32 height = std::max(image.info.size.height >> m, 1u);
        const u32 depth =
            image.info.props.is_volume ? std::max(image.info.size.depth >> m, 1u) : 1u;
        const auto [mip_size, mip_pitch, mip_height, mip_offset] = image.info.mips_layout[m];

        // Protect GPU modified resources from accidental CPU reuploads.
        // bbport: every upload records the guest memory it saw, not only uploads of images
        // the GPU had already written. An image the GPU writes after an upload from the buffer
        // cache (GpuDirty: a storage image's first binding) or from a plain texture otherwise
        // had no reference, and the first CPU write anywhere in its page replaced the GPU's
        // contents with stale guest memory (a 1x1 exposure texture computed once: the
        // character creation preview went black after one frame).
        const u8* mip_addr = std::bit_cast<u8*>(image.info.guest_address) + mip_offset;
        u64 mip_hash = image.mip_hashes[m];
        if (!keep_hashes && !skip_hashes) {
            BB_SECTION(RefreshHash);
            BbStats::Timer hash_timer{BbStats::t_refresh_hash};
            mip_hash = GuestMemoryHash(mip_addr, mip_size);
        }
        if (is_gpu_modified && !is_gpu_dirty && image.mip_hashes[m] == mip_hash) {
            continue;
        }
        image.mip_hashes[m] = mip_hash;

        const u32 extent_width = mip_pitch ? std::min(mip_pitch, width) : width;
        const u32 extent_height = mip_height ? std::min(mip_height, height) : height;
        image_copies.push_back({
            .bufferOffset = mip_offset,
            .bufferRowLength = mip_pitch,
            .bufferImageHeight = mip_height,
            .imageSubresource{
                .aspectMask = image.aspect_mask & ~vk::ImageAspectFlagBits::eStencil,
                .mipLevel = m,
                .baseArrayLayer = 0,
                .layerCount = num_layers,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {extent_width, extent_height, depth},
        });
    }

    // Only a tracked image hears of the writes that would outdate them (with the hashes deferred:
    // whether the memory is still as uploaded).
    image.mip_hashes_current =
        image.track_addr == image.guest_begin && image.track_addr_end == image.guest_end;
    image.hashes_deferred = skip_hashes;

    if (image_copies.empty()) {
        image.flags &= ~ImageFlagBits::Dirty;
        RefreshTrace::Refresh(image, entry_flags, "unchanged guest memory, the GPU's contents kept");
        return;
    }
    RefreshTrace::Refresh(image, entry_flags,
                          !is_gpu_dirty ? "uploaded"
                          : keep_hashes ? "uploaded from the buffer cache, hashes kept"
                                        : "uploaded from the buffer cache");

    scheduler.EndRendering(Vulkan::GpuSavings::PassBreak::TextureCache);

    std::optional<BbStats::Timer> part_timer{std::in_place, BbStats::t_refresh_obtain};
    BbSections::Scope obtain_scope{BbSections::RefreshObtain};
    std::pair<const Buffer*, u64> source;
    // bbport: the game's button icons, as the keys they are bound to (engine/key_prompts.h).
    std::vector<u8> keycaps;
    if (KeyPromptData(image, keycaps)) {
        auto& stream = buffer_cache.GetStreamBuffer();
        source = {&stream, stream.Copy(keycaps.data(), keycaps.size(), 256)};
    } else {
        source = buffer_cache.ObtainBufferForImage(image.info.guest_address, image.info.guest_size);
    }
    const auto [in_buffer, in_offset] = source;
    obtain_scope.End();
    part_timer.emplace(BbStats::t_refresh_detile);
    BbSections::Scope detile_scope{BbSections::RefreshDetile};
    const auto [buffer, offset] = tile_manager.DetileImage(in_buffer, in_offset, image.info);
    detile_scope.End();
    part_timer.reset();
    for (auto& copy : image_copies) {
        copy.bufferOffset += offset;
    }

    BB_SECTION(RefreshUpload);
    runtime.UploadImage(&image, buffer, image_copies);
}

vk::Sampler TextureCache::GetSampler(const AmdGpu::Sampler& sampler,
                                     AmdGpu::BorderColorBuffer border_color_base,
                                     const bool is_depth, float extra_lod_bias,
                                     size_t* lru_id) {
    // Compare and plain uses of one S# need separate samplers; so do extra LOD biases.
    const u64 hash = HashCombine(HashCombine(XXH3_64bits(&sampler, sizeof(sampler)), is_depth),
                                 u64(std::bit_cast<u32>(extra_lod_bias)));

    std::scoped_lock lock{samplers_mutex};
    const auto [it, new_sampler] = samplers.try_emplace(hash, instance, sampler, border_color_base,
                                                        is_depth, extra_lod_bias);
    if (new_sampler) {
        samplers.at(hash).lru_id = sampler_lru_cache.Insert(hash, gc_tick);
    } else {
        sampler_lru_cache.Touch(it->second.lru_id, gc_tick);
    }
    if (lru_id) {
        *lru_id = it->second.lru_id;
    }

    return it->second.Handle();
}

void TextureCache::RegisterImage(ImageId image_id) {
    BbStats::images_registered.fetch_add(1, std::memory_order_relaxed);
    Image& image = slot_images[image_id];
    ASSERT_MSG(False(image.flags & ImageFlagBits::Registered),
               "Trying to register an already registered image");
    image.flags |= ImageFlagBits::Registered;
    ++registry_generation;
    BbStats::live_image_bytes.fetch_add(Common::AlignUp(image.info.guest_size, 1024),
                                        std::memory_order_relaxed);
    BbStats::live_images.fetch_add(1, std::memory_order_relaxed);
    image.lru_id = lru_cache.Insert(image_id, gc_tick);
    image.lru_touched_tick = gc_tick;
    image.gx_dead_tick = 0;
    // 0.4.6 beta 5: when (ProcessGxCreated), and every new image's first upload takes no hashes (beta
    // 4: the images built ahead only): they are only compared once the GPU has drawn into it, and
    // until then the image holds uploaded data only (Image::ResolveDeferredHashes).
    image.registered_ns = u64(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count());
    image.dead_by_reuse = false;
    image.dead_grace_ns = 0;
    image.skip_hashes_once = true;
    ForEachPage(image.info.guest_address, image.info.guest_size,
                [this, image_id](u64 page) { page_table[page].push_back(image_id); });
    // 0.4.6 beta 5 (research): an image a draw made (not prepared ahead), by the game resource at its
    // memory: what the draws still make in play, and the hitches' images.
    // 0.4.6 beta 8: the resource there, and whether the image is a surface of one the game made without
    // its data for the GPU to write (part 1).
    image.surface_ns = 0;
    image.gx_owner = 0;
    image.gx_whole = false;
    if (BbEngine::GxResources::Active()) {
        const VAddr begin = image.info.guest_address;
        const VAddr end = begin + std::max<u64>(image.info.guest_size, 1);
        bool at_start = false;
        const auto owner = BbEngine::GxResources::Classify(begin, end, &at_start);
        image.gx_owner = u8(owner);
        image.gx_whole = at_start;
        if (EmptySurfaces()) {
            if (const auto surface = BbEngine::GxResources::SurfaceAt(begin, end); surface.found) {
                image.surface_ns = surface.ns;
            }
        }
        if (!t_building_ahead) {
            const u64 bytes = image.backing ? image.GetHostImageSize() : 0;
            BbEngine::GxResources::NoteDrawMade(owner, bytes);
            if (owner == BbEngine::GxResources::Owner::None) {
                BbEngine::GxResources::NoteDrawMadeNone(begin, bytes, image.info.size.width, image.info.size.height,
                                                        image.info.resources.levels, image.info.resources.layers,
                                                        u32(image.info.pixel_format), u32(image.info.tile_mode));
            }
        }
    }
    // Beta 15 (research): whether the game made a texture at this address, and how long ago.
    BbEngine::GxResources::NoteImage(BbEngine::GxResources::TextureAt(image.info.guest_address));
    // Beta 16: a draw made an image where one was built ahead: the two differ (a miss).
    if (!t_building_ahead && !ahead_at.empty()) {
        if (const auto it = ahead_at.find(image.info.guest_address); it != ahead_at.end()) {
            BbEngine::GxResources::NoteAheadMiss();
            const AheadBuilt& b = it->second;
            if (ahead_misses_logged < 16) {
                ++ahead_misses_logged;
                std::printf("GX follow: a draw made another image at 0x%llx: built ahead %ux%u, %u mips, %u layers, "
                            "format %u, tile %u; the draw's %ux%u, %u mips, %u layers, format %u, tile %u\n",
                            (unsigned long long)image.info.guest_address, b.width, b.height, b.levels, b.layers,
                            b.format, b.tile, image.info.size.width, image.info.size.height,
                            image.info.resources.levels, image.info.resources.layers,
                            u32(image.info.pixel_format), u32(image.info.tile_mode));
            }
            if (slot_images.is_allocated(b.image_id) && slot_images[b.image_id].image_uid == b.image_uid &&
                slot_images[b.image_id].ahead == 1) {
                slot_images[b.image_id].ahead = 3; // its miss is counted
            }
            ahead_at.erase(it);
        }
    }
    // Beta 15: freed by the collector in the last 5 minutes and needed again (a refault).
    if (const auto it = freed_recent.find(image.info.guest_address); it != freed_recent.end()) {
        if (it->second.first == image.info.guest_size && gc_second - it->second.second <= 300) {
            VramPolicy::NoteRefault(image.backing ? image.GetHostImageSize() : image.info.guest_size);
        }
        freed_recent.erase(it);
    }
}

void TextureCache::UnregisterImage(ImageId image_id) {
    Image& image = slot_images[image_id];
    ASSERT_MSG(True(image.flags & ImageFlagBits::Registered),
               "Trying to unregister an already unregistered image");
    image.flags &= ~ImageFlagBits::Registered;
    ++registry_generation;
    lru_cache.Free(image.lru_id);
    BbStats::live_image_bytes.fetch_sub(Common::AlignUp(image.info.guest_size, 1024),
                                        std::memory_order_relaxed);
    BbStats::live_images.fetch_sub(1, std::memory_order_relaxed);
    ForEachPage(image.info.guest_address, image.info.guest_size, [this, image_id](u64 page) {
        const auto page_it = page_table.find(page);
        if (page_it == nullptr) {
            UNREACHABLE_MSG("Unregistering unregistered page=0x{:x}", page << PageShift);
            return;
        }
        auto& image_ids = *page_it;
        const auto vector_it = std::ranges::find(image_ids, image_id);
        if (vector_it == image_ids.end()) {
            ASSERT_MSG(false, "Unregistering unregistered image in page=0x{:x}", page << PageShift);
            return;
        }
        image_ids.erase(vector_it);
    });
}

void TextureCache::WatchPages(VAddr addr, u64 size) {
    watched_pages.Add(addr, size, 1);
    tracker.UpdatePageWatchers<1>(addr, size);
}

void TextureCache::UnwatchPages(VAddr addr, u64 size) {
    tracker.UpdatePageWatchers<false>(addr, size);
    watched_pages.Add(addr, size, -1);
}

void TextureCache::TrackImage(ImageId image_id) {
    auto& image = slot_images[image_id];
    if (!(image.flags & ImageFlagBits::Registered)) {
        return;
    }
    const auto image_begin = image.info.guest_address;
    const auto image_end = image.info.guest_address + image.info.guest_size;
    if (image_begin == image.track_addr && image_end == image.track_addr_end) {
        return;
    }

    if (!image.IsTracked()) {
        // Re-track the whole image
        image.track_addr = image_begin;
        image.track_addr_end = image_end;
        // 0.4.6 beta 8 (part 4; UnwatchKinds): a texture of the game's whose writes its calls tell (made
        // with its data, a target, a surface) is not write-protected; its range is kept.
        using BbEngine::GxResources::Owner;
        const Owner owner = Owner(image.gx_owner);
        const Unwatch kinds = UnwatchKinds();
        if (kinds != Unwatch::None && image.gx_whole &&
            (image.surface_ns != 0 || owner == Owner::RenderTarget || owner == Owner::DepthTarget ||
             (kinds == Unwatch::TargetsAndData && owner == Owner::TextureData))) {
            image.calls_watch = true;
            BbEngine::GxResources::NoteUnwatched(1, image.backing ? image.GetHostImageSize() : 0);
            return;
        }
        image.calls_watch = false;
        WatchPages(image_begin, image.info.guest_size);
    } else {
        if (image_begin < image.track_addr) {
            TrackImageHead(image_id);
        }
        if (image.track_addr_end < image_end) {
            TrackImageTail(image_id);
        }
    }
}

void TextureCache::TrackImageHead(ImageId image_id) {
    auto& image = slot_images[image_id];
    if (!(image.flags & ImageFlagBits::Registered)) {
        return;
    }
    const auto image_begin = image.info.guest_address;
    if (image_begin == image.track_addr) {
        return;
    }
    ASSERT(image.track_addr != 0 && image_begin < image.track_addr);
    const auto size = image.track_addr - image_begin;
    image.track_addr = image_begin;
    if (!image.calls_watch) {
        WatchPages(image_begin, size);
    }
}

void TextureCache::TrackImageTail(ImageId image_id) {
    auto& image = slot_images[image_id];
    if (!(image.flags & ImageFlagBits::Registered)) {
        return;
    }
    const auto image_end = image.info.guest_address + image.info.guest_size;
    if (image_end == image.track_addr_end) {
        return;
    }
    ASSERT(image.track_addr_end != 0 && image.track_addr_end < image_end);
    const auto addr = image.track_addr_end;
    const auto size = image_end - image.track_addr_end;
    image.track_addr_end = image_end;
    if (!image.calls_watch) {
        WatchPages(addr, size);
    }
}

void TextureCache::UntrackImage(ImageId image_id) {
    auto& image = slot_images[image_id];
    image.mip_hashes_current = false; // writes go unseen until it is tracked again
    if (!image.IsTracked()) {
        return;
    }
    const auto addr = image.track_addr;
    const auto size = image.track_addr_end - image.track_addr;
    image.track_addr = 0;
    image.track_addr_end = 0;
    if (image.calls_watch) {
        image.calls_watch = false; // never watched (part 4)
        return;
    }
    if (size != 0) {
        UnwatchPages(addr, size);
    }
}

void TextureCache::UntrackImageHead(ImageId image_id) {
    auto& image = slot_images[image_id];
    image.mip_hashes_current = false;
    const auto image_begin = image.info.guest_address;
    if (!image.IsTracked() || image_begin < image.track_addr) {
        return;
    }
    const auto addr = tracker.GetNextPageAddr(image_begin);
    const auto size = addr - image_begin;
    image.track_addr = addr;
    if (image.calls_watch) {
        return; // never watched (part 4): another image's watch faulted the page; its calls tell its writes
    }
    if (image.track_addr == image.track_addr_end) {
        // This image spans only 2 pages and both are modified,
        // but the image itself was not directly affected.
        // Cehck its hash later.
        MarkAsMaybeDirty(image_id, image);
    }
    UnwatchPages(image_begin, size);
}

void TextureCache::UntrackImageTail(ImageId image_id) {
    auto& image = slot_images[image_id];
    image.mip_hashes_current = false;
    const auto image_end = image.info.guest_address + image.info.guest_size;
    if (!image.IsTracked() || image.track_addr_end < image_end) {
        return;
    }
    ASSERT(image.track_addr_end != 0);
    const auto addr = tracker.GetPageAddr(image_end);
    const auto size = image_end - addr;
    image.track_addr_end = addr;
    if (image.calls_watch) {
        return; // never watched (part 4)
    }
    if (image.track_addr == image.track_addr_end) {
        // This image spans only 2 pages and both are modified,
        // but the image itself was not directly affected.
        // Cehck its hash later.
        MarkAsMaybeDirty(image_id, image);
    }
    UnwatchPages(addr, size);
}

void TextureCache::GarbageCollectImages() {
    const u64 second = u64(std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count());
    // Hunter's Workshop 0.4.5 beta 14-15: what goes, when and in which order is the VRAM manager's
    // call, for this cache and the buffer cache alike (vram_policy.h): garbage at any level while
    // frames have slack, trims at loading screens, while tight and when full. Until beta 13 an image
    // loaded from the game's memory went 20 s after its last use at any VRAM use on cards up to about
    // 10 GB, and any image 1 s after from 80% (BB_GC_PRESSURE_PERCENT).
    VramPolicy::Update(instance);
    // bbport: gc_tick (one per guest submission, hundreds a second) and the scheduler's tick at each
    // of the last 512 seconds, for ages in seconds (UsedSince).
    const bool new_second = second != gc_second;
    if (new_second) {
        gc_second = second;
        BbStats::coarse_second.store(u32(second), std::memory_order_relaxed);
        gc_tick_at_second[second % gc_tick_at_second.size()] = gc_tick;
        sched_tick_at_second[second % sched_tick_at_second.size()] = scheduler.CurrentTick();
    }
    // Beta 16: textures the game created with their data, built ahead of their first draw (0.4.6 beta
    // 4: prepared on the game's threads, registered here; it takes the lock itself).
    BuildAhead();
    // 0.4.6 beta 4: the game's destroys are taken, and their images freed once the GPU is done with
    // them, at every pass (beta 16-3: once a second, a second or more after the game let them go).
    const bool dead_work = BbEngine::GxResources::Following() &&
                           (!dead_images.empty() || BbEngine::GxResources::DestroyedWaiting() ||
                            BbEngine::GxResources::CreatedWaiting() || BbEngine::GxResources::WrittenWaiting());
    // Nothing to do (no trim, no store under way, not a new second): no lock taken (one pass per
    // guest submission). pending_stores changes size on this thread only.
    const VramPolicy::Mode mode = VramPolicy::CurrentMode();
    const bool trim = mode != VramPolicy::Mode::None && !BbToggle::Disabled(BbToggle::TextureCollector);
    if (!trim && !new_second && pending_stores.empty() && !dead_work) {
        return;
    }
    std::scoped_lock lock{mutex};
    // Stores begun in earlier passes: their images go once the GPU has copied them out.
    CompletePendingStores();
    if (new_second || dead_work) {
        ProcessGxDestroyed();
        ProcessGxCreated();
        ProcessWritten();
        FreeDeadImages(gc_second);
    }
    if (new_second) {
        if (second - minute_second >= 60) {
            minute_second = second;
            ClassifyForMinute(second);
        }
        if (!BbToggle::Disabled(BbToggle::TextureCollector)) {
            GarbagePass(second);
        }
        // The plan is made only when it may run (while tight, frames with slack): a pass over every
        // image, counted against the second's time in play.
        if (trim && VramPolicy::Allowed()) {
            const auto start = std::chrono::steady_clock::now();
            BuildPlan(second);
            VramPolicy::NoteWork(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - start)
                                     .count());
        } else {
            plan.clear();
            plan_pos = 0;
        }
    }
    if (trim) {
        ExecutePlan();
    }
    // bbport: what was freed for room, at most every 5 s.
    const auto now = std::chrono::steady_clock::now();
    if (now - gc_report_time >= std::chrono::seconds(5) &&
        (gc_evictions != 0 || gc_stores_cancelled != 0 || gc_tiled_written + gc_downloads != 0)) {
        // 0.4.6 beta 4: by why (until beta 3 all were named after the VRAM manager's mode, "garbage"
        // when it had none: the images of destroyed textures were most of them).
        static constexpr const char* kMode[] = {"none", "loading screen trim", "tight", "full"};
        const u64 by_manager = gc_evictions - std::min<u64>(gc_evictions, gc_freed_destroyed + gc_freed_orphans +
                                                                            gc_freed_superseded);
        std::printf("Texture cache: %llu images freed (%llu of textures the game destroyed or memory it reused, "
                    "%llu stale with no game texture there, %llu stale drawn by the GPU and written over since, "
                    "%llu by the VRAM manager, its mode %s), %llu of them drawn by the GPU "
                    "and stored first (%llu tiled), %llu stores cancelled (used again), %llu in use kept since "
                    "the last report\n",
                    (unsigned long long)gc_evictions, (unsigned long long)gc_freed_destroyed,
                    (unsigned long long)gc_freed_orphans, (unsigned long long)gc_freed_superseded,
                    (unsigned long long)by_manager, kMode[u32(mode)],
                    (unsigned long long)(gc_downloads + gc_tiled_written), (unsigned long long)gc_tiled_written,
                    (unsigned long long)gc_stores_cancelled, (unsigned long long)gc_recent_kept);
        gc_report_time = now;
        gc_evictions = gc_downloads = gc_tiled_written = gc_tiled_kept = gc_recent_kept = 0;
        gc_stores_cancelled = 0;
        gc_freed_destroyed = gc_freed_orphans = gc_freed_superseded = 0;
    }
}

bool TextureCache::UsedSince(const Image& image, u64 second, u32 seconds) const {
    seconds = std::min<u32>(seconds, u32(gc_tick_at_second.size() - 1));
    const size_t at = (second - seconds) % gc_tick_at_second.size();
    const u64 gc = gc_tick_at_second[at], sched = sched_tick_at_second[at];
    if (gc == 0 && sched == 0) {
        return true; // the session's first seconds: no tick to compare with
    }
    return image.tick_accessed_last >= sched ||
           (image.lru_touched_tick != ~0ULL && image.lru_touched_tick >= gc);
}

void TextureCache::NoteCollected(const Image& image, u64 second, VramPolicy::Freed kind) {
    static const u64 dedicated = [] {
        const char* env = std::getenv("BB_VMA_DEDICATED_MB"); // image.cpp: memory of its own from this size
        return (env && env[0] ? std::strtoull(env, nullptr, 10) : 4) << 20;
    }();
    const u64 bytes = image.backing ? image.GetHostImageSize() : 0;
    VramPolicy::NoteFreed(kind, bytes, dedicated != 0 && bytes >= dedicated ? bytes : 0);
    const VAddr address = image.info.guest_address;
    // Refaults: only idle copies (garbage and destroyed textures come back as new contents).
    if (address == 0 || kind != VramPolicy::Freed::Texture) {
        return;
    }
    if (freed_recent.size() >= 8192 && !freed_order.empty()) {
        freed_recent.erase(freed_order.front());
        freed_order.pop_front();
    }
    if (freed_recent.insert_or_assign(address, std::pair{image.info.guest_size, second}).second) {
        freed_order.push_back(address);
    }
}

void TextureCache::ProcessGxDestroyed() {
    if (!BbEngine::GxResources::Active()) {
        return;
    }
    BbEngine::GxResources::TakeDestroyed(gx_gone);
    for (const auto& gone : gx_gone) {
        bool had = false;
        u64 bytes = 0;
        // 0.4.6 beta 4: every image wholly inside the texture, not only one starting where it does (a
        // mip or a layer the game sampled as a texture of its own): with nothing else to free them
        // (BB_VRAM_MANAGER=0) they stayed for the session (beta 3: 1.3-2 GB of stale images by 20 min).
        const VAddr gone_end = gone.memory + std::max<u64>(gone.bytes, 1);
        ForEachImageInRegion(gone.memory, std::max<u64>(gone.bytes, 1), [&](ImageId image_id, Image& image) {
            if (image.info.guest_address >= gone.memory &&
                image.info.guest_address + image.info.guest_size <= gone_end) {
                image.gx_dead_tick = gc_tick;
                // 0.4.6 beta 7: YEBIS destroys and re-creates its targets together (300-750 at a
                // load, beta 6's log): a few seconds for the new one to take this image.
                image.dead_grace_ns = gone.grace ? NowSteadyNs() + 3'000'000'000ull : 0;
                had = true;
                bytes += image.backing ? image.GetHostImageSize() : 0;
                // 0.4.6 beta 10: an image whose pages the calls watch (ReleaseDestroyed, on the game's
                // thread, passes over pages nothing write-protects) is marked as a watched one is there:
                // what the game writes in that memory next is the next resource's.
                if (image.calls_watch && False(image.flags & ImageFlagBits::GpuModified) &&
                    !image.binding.is_target && !image.store_pending &&
                    !BbEngine::GxResources::LiveTextureOverlaps(
                        image.info.guest_address, image.info.guest_address + image.info.guest_size)) {
                    image.mip_hashes_current = false;
                    if (False(image.flags & ImageFlagBits::CpuDirty) && garbage_candidates.size() < 16384) {
                        garbage_candidates.emplace_back(image_id, image.image_uid);
                    }
                    image.flags |= ImageFlagBits::CpuDirty;
                }
                // Beta 16: freed once the GPU is done with it (FreeDeadImages).
                if (BbEngine::GxResources::Following() && dead_images.size() < 65536) {
                    dead_images.emplace_back(image_id, image.image_uid);
                }
            }
        });
        // Built ahead there: whatever happens to that image now is not a draw's miss.
        ahead_at.erase(gone.memory);
        BbEngine::GxResources::NoteDestroyedImage(had, bytes);
    }
    gx_gone.clear();
}

void TextureCache::ProcessGxCreated() {
    if (!BbEngine::GxResources::Following() || !BbEngine::GxResources::CreatedWaiting()) {
        return;
    }
    BbEngine::GxResources::TakeCreated(gx_created);
    for (const auto& made : gx_created) {
        // The scheduler's tick at the newest collector pass before the resource was made: an image
        // used from then on may have been used for it (unknown when older than the passes kept).
        u64 before_tick = 0;
        for (u64 back = 0; back < ns_at_gc.size() && back <= gc_tick; ++back) {
            const size_t at = (gc_tick - back) % ns_at_gc.size();
            if (ns_at_gc[at] != 0 && ns_at_gc[at] <= made.ns) {
                before_tick = sched_tick_at_gc[at];
                break;
            }
        }
        const VAddr made_end = made.memory + std::max<u64>(made.bytes, 1);
        u64 bytes = 0, images = 0, kept = 0, kept_bytes = 0, written = 0, written_bytes = 0, surfaces_kept = 0;
        // 0.4.6 beta 9: the GPU's newest write to its memory when the game made it (StartSurface tells
        // the writes since from those of the memory's previous owner).
        if (EmptySurfaces()) {
            if (created_gpu_version.size() > 65536) {
                created_gpu_version.clear();
            }
            const u64 version = buffer_cache.IsRegionGpuModified(made.memory, std::max<u64>(made.bytes, 1))
                                    ? buffer_cache.GpuWriteVersion(made.memory, std::max<u64>(made.bytes, 1))
                                    : 0;
            created_gpu_version[made.memory] = {made.ns, version};
        }
        ForEachImageInRegion(made.memory, std::max<u64>(made.bytes, 1), [&](ImageId image_id, Image& image) {
            // 0.4.6 beta 10: made with its data: the create call copied that data into the memory, a CPU
            // write the images there hear of whether their pages are watched or not (since beta 9 a
            // target's are not, since beta 10 a texture made with its data's are not). Each takes the
            // memory at its next use (its hashes tell which mips changed), as a watched one did, except a
            // live surface reaching past the new texture: its CPU writes are kept (beta 8), and the
            // memory outside the new texture's is older than what the GPU drew. A surface wholly inside
            // it is the new texture's memory now (the case beta 9 left open: a draw for the new texture
            // took that image with the target's old contents).
            if (made.with_data && True(image.flags & ImageFlagBits::Registered) && image.registered_ns < made.ns) {
                const VAddr image_end = image.info.guest_address + std::max<u64>(image.info.guest_size, 1);
                const bool within = image.info.guest_address >= made.memory && image_end <= made_end;
                if (within || image.surface_ns == 0) {
                    RefreshTrace::Mark(image, "a texture made with its data over it", made.memory, made.bytes);
                    if (False(image.flags & ImageFlagBits::CpuDirty) && garbage_candidates.size() < 16384) {
                        garbage_candidates.emplace_back(image_id, image.image_uid); // unused from now: garbage
                    }
                    image.flags |= ImageFlagBits::CpuDirty;
                    image.mip_hashes_current = false;
                    image.surface_ns = 0;
                    ++written;
                    written_bytes += image.backing ? image.GetHostImageSize() : 0;
                } else {
                    ++surfaces_kept;
                }
            }
            // 0.4.6 beta 7: YEBIS re-created a target over the same memory, made without data: the image
            // of the target it replaced, of the same shape, is this one's (bbhost keeps them too). It is
            // drawn into again; nothing of the old target's is read.
            if (made.hand_over && True(image.flags & ImageFlagBits::Registered) &&
                image.registered_ns < made.ns && image.info.guest_address == made.memory &&
                image.info.size.width == made.width && image.info.size.height == made.height &&
                (made.levels == 0 || image.info.resources.levels == made.levels) &&
                (made.layers == 0 || image.info.resources.layers == made.layers)) {
                image.gx_dead_tick = 0;
                image.dead_by_reuse = false;
                image.dead_grace_ns = 0;
                // 0.4.6 beta 8: the image is the new target's surface now (its writes told by its calls).
                image.surface_ns = 0;
                if (EmptySurfaces()) {
                    if (const auto surface = BbEngine::GxResources::SurfaceAt(made.memory, made_end);
                        surface.found && surface.ns == made.ns) {
                        image.surface_ns = surface.ns;
                    }
                }
                ++kept;
                kept_bytes += image.backing ? image.GetHostImageSize() : 0;
                return;
            }
            // 0.4.6 beta 6: one reaching outside it too when its texture was destroyed (beta 5 kept a
            // 64 MiB shadow map whose memory small textures had taken: only images wholly inside).
            const bool inside = image.info.guest_address >= made.memory &&
                                image.info.guest_address + image.info.guest_size <= made_end;
            if (False(image.flags & ImageFlagBits::Registered) || image.registered_ns >= made.ns ||
                image.dead_by_reuse || (!inside && image.gx_dead_tick == 0)) {
                return; // newer than the resource (its own), already retired, or a live one reaching outside it
            }
            image.dead_by_reuse = true;
            image.reuse_tick = before_tick;
            image.gx_dead_tick = gc_tick;
            ++images;
            bytes += image.backing ? image.GetHostImageSize() : 0;
            if (dead_images.size() < 65536) {
                dead_images.emplace_back(image_id, image.image_uid);
            }
        });
        if (images != 0) {
            ahead_at.erase(made.memory);
            BbEngine::GxResources::NoteReuseDead(images, bytes);
        }
        if (kept != 0) {
            BbEngine::GxResources::NoteHandedOver(kept, kept_bytes);
        }
        if (written != 0 || surfaces_kept != 0) {
            BbEngine::GxResources::NoteCreationWrites(written, written_bytes, surfaces_kept);
        }
    }
    gx_created.clear();
}

void TextureCache::ProcessWritten() {
    if (!BbEngine::GxResources::Following() || !BbEngine::GxResources::WrittenWaiting()) {
        return;
    }
    BbEngine::GxResources::TakeWritten(gx_written);
    for (const auto& written : gx_written) {
        ForEachImageInRegion(written.memory, std::max<u64>(written.bytes, 1), [&](ImageId, Image& image) {
            if (False(image.flags & ImageFlagBits::Registered)) {
                return;
            }
            // Written by the CPU through the game's own call: its next use takes the memory (by its
            // hashes when the GPU drew it too); no longer a surface whose writes only the GPU makes.
            RefreshTrace::Mark(image, "CPU written through its call", written.memory, written.bytes);
            image.flags |= ImageFlagBits::CpuDirty;
            image.mip_hashes_current = false;
            image.surface_ns = 0;
        });
    }
    gx_written.clear();
}

void TextureCache::FreeDeadImages(u64 second) {
    if (dead_images.empty()) {
        return;
    }
    size_t kept = 0;
    for (size_t i = 0; i < dead_images.size(); ++i) {
        const auto [image_id, uid] = dead_images[i];
        if (!slot_images.is_allocated(image_id) || slot_images[image_id].image_uid != uid) {
            continue; // gone already
        }
        Image& image = slot_images[image_id];
        switch (DeadImageState(image)) {
        case Dead::No:
            // 0.4.6 beta 10 (counted only): kept by a guess, not by a use since its texture went; the
            // owner rule would retire an image of a game's texture once the GPU is done with it.
            if (image.gx_dead_tick != 0 && image.gx_whole && image.gx_owner != 0 &&
                True(image.flags & ImageFlagBits::Registered) &&
                !(image.lru_touched_tick != ~0ULL && image.lru_touched_tick > image.gx_dead_tick)) {
                BbEngine::GxResources::NoteOwnerGuess(image.dead_by_reuse
                                                          ? BbEngine::GxResources::OwnerGuess::KeptUsedBefore
                                                          : BbEngine::GxResources::OwnerGuess::KeptOverlap,
                                                      image.backing ? image.GetHostImageSize() : 0);
            }
            image.dead_by_reuse = false; // in use: the next resource made over it is checked again
            continue; // used since, or kept for a live texture (DeadImageState)
        case Dead::Busy:
            dead_images[kept++] = dead_images[i]; // the GPU is still at it: a later pass
            continue;
        case Dead::Free:
            break;
        }
        // The game is done with it: not stored back even if the GPU drew it (that memory is the next
        // texture's).
        const u64 bytes = image.backing ? image.GetHostImageSize() : 0;
        ++gc_evictions;
        ++gc_freed_destroyed;
        BbStats::gc_freed_images.fetch_add(1, std::memory_order_relaxed);
        NoteCollected(image, second, VramPolicy::Freed::Destroyed);
        BbEngine::GxResources::NoteDeadFreed(bytes);
        FreeImage(image_id);
    }
    dead_images.resize(kept);
}

ImageId TextureCache::TakeDeadDonor(const ImageInfo& info) {
    for (size_t i = dead_images.size(); i-- > 0;) {
        const auto [donor_id, uid] = dead_images[i];
        if (!slot_images.is_allocated(donor_id) || slot_images[donor_id].image_uid != uid) {
            continue;
        }
        const Image& d = slot_images[donor_id];
        if (d.info.pixel_format != info.pixel_format || d.info.type != info.type || d.info.size != info.size ||
            d.info.resources != info.resources || d.info.num_samples != info.num_samples ||
            d.info.props.is_depth != info.props.is_depth || !d.HasMemory() || d.backing_images.size() != 1 ||
            d.backing->num_samples != d.info.num_samples || d.backing->image.in_system_memory || d.scene_proxy ||
            d.store_pending || d.depth_id) {
            continue; // another shape, or state of its own to keep
        }
        {
            std::unique_lock lk{download_images_mutex};
            if (download_images.contains(donor_id)) {
                continue;
            }
        }
        // Dead and the GPU done with it; a YEBIS texture's grace is over by now for this purpose (the
        // game's creates were taken first: none re-created over its memory with its shape).
        if (DeadImageState(d, 0, true) == Dead::Free) {
            return donor_id;
        }
    }
    return {};
}

TextureCache::Dead TextureCache::DeadImageState(const Image& image, VAddr skip_at, bool ignore_grace) {
    // Not dead: not registered or not the game's destroyed texture; used since the game destroyed
    // its texture, or a texture of the game's lives there again (a draw or a build ahead may use the
    // image for it); or, beta 19, one the GPU drew (or draws into) that another live texture covers
    // (its contents may be that texture's, ReleaseDestroyed). 0.4.6 beta 3: an image of the game's
    // uploaded data is dead even where the game made a new texture in its memory (not used since, it
    // is no one's: the new texture's draw makes its own from memory), which beta 19 kept.
    // 0.4.6 beta 5: of memory the game made a new resource in since (dead_by_reuse), the live
    // resource there is that one, not a sharer of this image's contents; one the GPU drew is kept
    // only when used from the pass before that resource was made (perhaps drawn for it).
    if (image.gx_dead_tick == 0 || False(image.flags & ImageFlagBits::Registered) ||
        (image.lru_touched_tick != ~0ULL && image.lru_touched_tick > image.gx_dead_tick)) {
        return Dead::No;
    }
    const bool drawn = True(image.flags & ImageFlagBits::GpuModified) || image.binding.is_target;
    if (image.dead_by_reuse ? drawn && image.tick_accessed_last >= image.reuse_tick
                            : drawn && BbEngine::GxResources::LiveTextureOverlaps(
                                           image.info.guest_address,
                                           image.info.guest_address + image.info.guest_size, skip_at)) {
        return Dead::No;
    }
    // 0.4.6 beta 7: a YEBIS texture's image waits for the target YEBIS re-creates over it.
    if (!ignore_grace && image.dead_grace_ns != 0 && NowSteadyNs() < image.dead_grace_ns) {
        return Dead::Busy;
    }
    // bbport (GPU hang guard): as the collector's, the GPU's work with it done first.
    const u64 recent_frames = GcRecentFrames();
    const u64 recent_tick =
        recent_frames == 0
            ? 0
            : (gc_tick >= recent_frames
                   ? std::max<u64>(sched_tick_at_gc[(gc_tick - recent_frames) % sched_tick_at_gc.size()], 1)
                   : ~0ull);
    if (image.store_pending || (recent_tick != 0 && (image.tick_accessed_last >= recent_tick ||
                                                     !scheduler.IsFree(image.tick_accessed_last)))) {
        return Dead::Busy;
    }
    return Dead::Free;
}

void TextureCache::PrepareCreated(const BbEngine::GxResources::Ahead& job) {
    using BbEngine::GxResources::AheadResult;
    // With the VRAM manager on, below its comfortable line only, as beta 16-3 (the game creates about
    // twice the textures it draws: the rest is the manager's call); BB_VRAM_MANAGER=0: always, the
    // game's creates decide alone.
    if (!VramPolicy::Off() && !VramPolicy::Comfortable()) {
        BbEngine::GxResources::NoteAhead(AheadResult::NoRoom, 0);
        return;
    }
    AmdGpu::Image sharp{};
    std::memcpy(&sharp, job.tsharp.data(), sizeof(sharp));
    if (sharp.GetDataFmt() == AmdGpu::DataFormat::FormatInvalid || sharp.Address() != job.memory ||
        sharp.NumSamples() != 1) {
        BbEngine::GxResources::NoteAhead(AheadResult::Invalid, 0);
        return;
    }
    // As a draw sampling it would ask (a colour texture, read only).
    ImageDesc desc{sharp, Shader::ImageResource{}};
    if (desc.info.guest_size == 0 || desc.info.pixel_format == vk::Format::eUndefined) {
        BbEngine::GxResources::NoteAhead(AheadResult::Invalid, 0);
        return;
    }
    // The Vulkan image and its memory, here on the game's thread (the allocator and the format
    // queries are thread-safe); not the cache's lists, which the GPU thread reads without the lock.
    auto image = std::make_unique<Image>(instance, runtime, slot_image_views, desc.info, true);
    if (!image->HasMemory()) {
        BbEngine::GxResources::NoteAhead(AheadResult::NoMemory, 0); // its first draw makes it
        return;
    }
    std::scoped_lock lock{prepared_mutex};
    if (prepared_in.size() >= 16384) {
        BbEngine::GxResources::NoteAhead(AheadResult::TooOld, 0); // no renderer pass for a long while
        return;
    }
    prepared_in.push_back({job, std::move(image)});
}

void TextureCache::WatchPagesBatch(std::vector<std::pair<VAddr, u64>>& ranges) {
    if (ranges.empty()) {
        return;
    }
    std::sort(ranges.begin(), ranges.end());
    for (const auto& [addr, size] : ranges) {
        watched_pages.Add(addr, size, 1);
    }
    tracker.WatchRanges(ranges);
}

void TextureCache::BuildAhead() {
    if (!BbEngine::GxResources::Following()) {
        return;
    }
    {
        std::scoped_lock lock{prepared_mutex};
        ahead_take.swap(prepared_in);
    }
    using BbEngine::GxResources::AheadResult;
    for (auto& prepared : ahead_take) {
        if (ahead_jobs.size() >= 16384) {
            ahead_jobs.pop_front(); // built at its first draw instead
            BbEngine::GxResources::NoteAhead(AheadResult::TooOld, 0);
        }
        ahead_jobs.push_back(std::move(prepared));
    }
    ahead_take.clear();
    if (ahead_jobs.empty()) {
        return;
    }
    // 0.4.6 beta 4: no time budget. The costly part (the image and its memory) was done in the game's
    // create call on the game's thread, and the upload hashes nothing (RefreshImage): what is left
    // here is registering, one protection change per run of textures, and recording the upload.
    // Every texture the game created since the last submission is ready at this one.
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    // 0.4.6 beta 3: the game's destroys first (beta 16-19 took them once a second, after this pass,
    // so a texture made where one was destroyed in the same second found the old image still live).
    {
        std::scoped_lock lock{mutex};
        ProcessGxDestroyed();
        ProcessGxCreated();
        ProcessWritten();
    }
    const u64 now_ns = u64(std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count());
    constexpr u64 MaxWaitNs = 30'000'000'000ull;
    using BbEngine::GxResources::Occupant;
    ahead_built.clear();
    ahead_refresh.clear();
    while (!ahead_jobs.empty()) {
        Prepared prepared = std::move(ahead_jobs.front());
        ahead_jobs.pop_front();
        const BbEngine::GxResources::Ahead& job = prepared.job;
        if (now_ns > job.ns && now_ns - job.ns > MaxWaitNs) {
            BbEngine::GxResources::NoteAhead(AheadResult::TooOld, 0); // waited too long: its first draw
            continue;
        }
        if (!BbEngine::GxResources::StillLive(job.memory, job.ns)) {
            BbEngine::GxResources::NoteAhead(AheadResult::Gone, 0);
            continue;
        }
        const ImageInfo& info = prepared.image->info;
        // Beta 19: only into memory no live image holds. Asking for it where one does (a render
        // target, a texture in the same memory) made the cache resolve the overlap now, expanding,
        // copying or retiring images the game was still using, at the game's creation instead of
        // its first draw (beta 17-18: 0-815 a minute, a broken surface and a white streak). Those are
        // left to their first draw, as before beta 16.
        // 0.4.6 beta 3: a destroyed texture's image there goes now (FreeDeadImages' rules; the new
        // texture over it does not count as another live one, beta 4); while the GPU still uses it,
        // the job waits a pass. 0.4.6 beta 4: the same texture again over its live image (same
        // address and shape) refreshes that image, as its first draw would, with nothing to resolve.
        std::scoped_lock lock{mutex};
        bool occupied = false, busy = false;
        Occupant why = Occupant::NotDestroyed;
        ImageId exact{};
        boost::container::small_vector<ImageId, 4> dead;
        ForEachImageInRegion(job.memory, info.guest_size, [&](ImageId image_id, Image& image) {
            if (False(image.flags & ImageFlagBits::Registered)) {
                return;
            }
            switch (DeadImageState(image, job.memory)) {
            case Dead::Free:
                dead.push_back(image_id);
                break;
            case Dead::Busy:
                busy = true;
                break;
            case Dead::No:
                if (!exact && IsExactMatch(image_id, info)) {
                    exact = image_id;
                    break;
                }
                if (!occupied) {
                    why = image.gx_dead_tick == 0 ? Occupant::NotDestroyed
                          : (image.lru_touched_tick != ~0ULL && image.lru_touched_tick > image.gx_dead_tick)
                              ? Occupant::UsedSince
                              : Occupant::GpuDrawn;
                }
                occupied = true;
                break;
            }
        });
        if (occupied) {
            BbEngine::GxResources::NoteAhead(AheadResult::Existed, 0);
            BbEngine::GxResources::NoteOccupant(why);
            continue;
        }
        if (busy) {
            ahead_wait.push_back(std::move(prepared)); // the next pass (until MaxWaitNs)
            continue;
        }
        for (const ImageId image_id : dead) {
            Image& image = slot_images[image_id];
            const u64 bytes = image.backing ? image.GetHostImageSize() : 0;
            ++gc_evictions;
            ++gc_freed_destroyed;
            BbStats::gc_freed_images.fetch_add(1, std::memory_order_relaxed);
            NoteCollected(image, gc_second, VramPolicy::Freed::Destroyed);
            BbEngine::GxResources::NoteDeadFreed(bytes);
            FreeImage(image_id);
        }
        if (exact) {
            if (job.upload) {
                ahead_refresh.emplace_back(exact, slot_images[exact].image_uid);
            }
            BbEngine::GxResources::NoteAhead(AheadResult::Refreshed, 0);
            continue; // the prepared image goes (its memory back to the allocator)
        }
        t_building_ahead = true;
        const ImageId image_id = slot_images.insert(std::move(*prepared.image));
        RegisterImage(image_id);
        t_building_ahead = false;
        Image& image = slot_images[image_id];
        image.tick_accessed_last = scheduler.CurrentTick();
        TouchImage(image);
        image.ahead = 1;
        ahead_at[job.memory] = {image_id,
                                image.image_uid,
                                image.info.size.width,
                                image.info.size.height,
                                image.info.resources.levels,
                                image.info.resources.layers,
                                u32(image.info.pixel_format),
                                u32(image.info.tile_mode)};
        // 0.4.6 beta 7: a texture made without its data (sampled only): not watched or uploaded now
        // (the game has not written it yet); its first draw does both (it stays Dirty).
        if (job.upload) {
            ahead_built.emplace_back(image_id, image.image_uid);
        }
        BbEngine::GxResources::NoteAhead(AheadResult::Built, image.GetHostImageSize());
    }
    for (auto& prepared : ahead_wait) {
        ahead_jobs.push_back(std::move(prepared));
    }
    ahead_wait.clear();
    if (!ahead_built.empty()) {
        // Watched together (one protection change per run of textures side by side), then uploaded
        // (the copies go out with the next submission; the game's data is copied on the copy threads).
        std::scoped_lock lock{mutex};
        ahead_watch.clear();
        size_t kept = 0;
        for (const auto& [image_id, uid] : ahead_built) {
            if (!slot_images.is_allocated(image_id) || slot_images[image_id].image_uid != uid) {
                continue;
            }
            Image& image = slot_images[image_id];
            if (False(image.flags & ImageFlagBits::Registered) || image.IsTracked()) {
                continue;
            }
            image.track_addr = image.guest_begin;
            image.track_addr_end = image.guest_end;
            // 0.4.6 beta 8 (part 4): a texture made with its data, unwatched as TrackImage leaves it.
            if (UnwatchKinds() == Unwatch::TargetsAndData && image.gx_whole &&
                BbEngine::GxResources::Owner(image.gx_owner) == BbEngine::GxResources::Owner::TextureData) {
                image.calls_watch = true;
                BbEngine::GxResources::NoteUnwatched(1, image.backing ? image.GetHostImageSize() : 0);
            } else {
                image.calls_watch = false;
                ahead_watch.emplace_back(image.guest_begin, image.info.guest_size);
            }
            ahead_built[kept++] = {image_id, uid};
        }
        ahead_built.resize(kept);
        WatchPagesBatch(ahead_watch);
        for (const auto& [image_id, uid] : ahead_built) {
            RefreshImage(slot_images[image_id]);
        }
    }
    for (const auto& [image_id, uid] : ahead_refresh) {
        if (slot_images.is_allocated(image_id) && slot_images[image_id].image_uid == uid) {
            UpdateImage(image_id);
        }
    }
    BbEngine::GxResources::NoteBuildPass(
        u64(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()),
        ahead_built.size() + ahead_refresh.size());
    BbEngine::GxResources::NoteAheadWaiting(ahead_jobs.size());
}

TextureCache::Overwritten TextureCache::DrawnOverwritten(const Image& image, u64& budget) {
    if (True(image.flags & ImageFlagBits::GpuDirty)) {
        return Overwritten::Yes; // the GPU wrote the memory as a buffer: the next use uploads that
    }
    if (False(image.flags & ImageFlagBits::CpuDirty) || image.hashes_deferred || image.mip_hashes.empty()) {
        return Overwritten::No;
    }
    const u32 levels = std::min<u32>(image.info.resources.levels, u32(image.mip_hashes.size()));
    u64 bytes = 0;
    for (u32 m = 0; m < levels; ++m) {
        bytes += image.info.mips_layout[m].size;
    }
    if (bytes > budget) {
        return Overwritten::Later;
    }
    budget -= bytes;
    BbEngine::GxResources::NoteSupersededHashed(bytes);
    // Through the backing, as MaybeDirtyHash's first hash is: the GPU's pages may be read-protected.
    for (u32 m = 0; m < levels; ++m) {
        const auto [mip_size, mip_pitch, mip_height, mip_offset] = image.info.mips_layout[m];
        const VAddr address = image.info.guest_address + mip_offset;
        if (mip_size == 0 || !Core::Memory::Instance()->IsValidMapping(address, mip_size)) {
            return Overwritten::No;
        }
        hash_scratch.resize(mip_size);
        Core::Memory::Instance()->CopySparseMemory(address, hash_scratch.data(), mip_size);
        if (GuestMemoryHash(hash_scratch.data(), mip_size) == image.mip_hashes[m]) {
            return Overwritten::No; // this mip's memory is as the image took it: its drawing is kept
        }
    }
    return Overwritten::Yes;
}

void TextureCache::GarbagePass(u64 second) {
    // 0.4.6 beta 4: following the game, a stale image (written over, unused 10 s) with no live game
    // texture over it goes too, at any VRAM level and with the manager off: the game has nothing
    // there, and its next use would upload it again anyway (not one the GPU drew, nor a target).
    const bool general = VramPolicy::GarbageAllowed();
    const bool orphans = BbEngine::GxResources::Following();
    if (garbage_candidates.empty() || (!general && !orphans)) {
        return;
    }
    const u64 recent_frames = GcRecentFrames();
    const u64 recent_tick =
        recent_frames == 0
            ? 0
            : (gc_tick >= recent_frames
                   ? std::max<u64>(sched_tick_at_gc[(gc_tick - recent_frames) % sched_tick_at_gc.size()], 1)
                   : ~0ull);
    u32 freed = 0, checked = 0;
    size_t kept = 0;
    // 0.4.6 beta 6: the game's memory hashed this pass to tell whether drawn images were written over
    // (copied through the backing first: about 2 ms for a 1080p target).
    u64 hash_budget = 32ull << 20;
    for (size_t i = 0; i < garbage_candidates.size(); ++i) {
        const auto [image_id, uid] = garbage_candidates[i];
        const bool look = (freed < VramPolicy::GarbagePerSecond || !general) && checked < 1024;
        if (!slot_images.is_allocated(image_id) || slot_images[image_id].image_uid != uid) {
            continue; // gone already
        }
        Image& image = slot_images[image_id];
        if (False(image.flags & ImageFlagBits::Registered) ||
            False(image.flags & (ImageFlagBits::CpuDirty | ImageFlagBits::GpuDirty))) {
            continue; // refreshed: not garbage any more
        }
        bool go = false, orphan = false, superseded = false;
        if (look) {
            ++checked;
            go = !image.store_pending && !UsedSince(image, second, VramPolicy::GarbageSeconds) &&
                 !(recent_tick != 0 && (image.tick_accessed_last >= recent_tick ||
                                        !scheduler.IsFree(image.tick_accessed_last)));
            if (go && !general) {
                const bool drawn = True(image.flags & ImageFlagBits::GpuModified) || image.binding.is_target;
                if (!drawn) {
                    orphan = !BbEngine::GxResources::LiveTextureOverlaps(
                        image.info.guest_address, image.info.guest_address + image.info.guest_size);
                    go = orphan;
                } else {
                    // 0.4.6 beta 6: the GPU drew it and the memory under it was written since, over
                    // every mip: its next use would upload over all of it (DrawnOverwritten).
                    superseded = DrawnOverwritten(image, hash_budget) == Overwritten::Yes;
                    go = superseded;
                }
            }
        }
        if (!go) {
            garbage_candidates[kept++] = garbage_candidates[i];
            continue;
        }
        ++freed;
        ++gc_evictions;
        if (orphan) {
            ++gc_freed_orphans;
            BbEngine::GxResources::NoteOrphanFreed(image.backing ? image.GetHostImageSize() : 0);
        }
        if (superseded) {
            ++gc_freed_superseded;
            BbEngine::GxResources::NoteSuperseded(image.backing ? image.GetHostImageSize() : 0);
        }
        // 0.4.6 beta 10 (counted only): a stale image of a game's texture, which the owner rule retires
        // when that texture goes.
        if ((orphan || superseded) && image.gx_whole && image.gx_owner != 0) {
            BbEngine::GxResources::NoteOwnerGuess(BbEngine::GxResources::OwnerGuess::StaleOwned,
                                                  image.backing ? image.GetHostImageSize() : 0);
        }
        BbStats::gc_freed_images.fetch_add(1, std::memory_order_relaxed);
        NoteCollected(image, second, VramPolicy::Freed::Garbage);
        FreeImage(image_id);
    }
    garbage_candidates.resize(kept);
}

void TextureCache::BuildPlan(u64 second) {
    using VramPolicy::Mode;
    const Mode mode = VramPolicy::CurrentMode();
    static const u64 dedicated = [] {
        const char* env = std::getenv("BB_VMA_DEDICATED_MB");
        return (env && env[0] ? std::strtoull(env, nullptr, 10) : 4) << 20;
    }();
    const u32 full_age = VramPolicy::FullAgeSeconds();
    // bbport (GPU hang guard): the submission tick GcRecentFrames() collector passes ago; images
    // accessed at or after it are in use, whatever else says.
    const u64 recent_frames = GcRecentFrames();
    const u64 recent_tick =
        recent_frames == 0
            ? 0
            : (gc_tick >= recent_frames
                   ? std::max<u64>(sched_tick_at_gc[(gc_tick - recent_frames) % sched_tick_at_gc.size()], 1)
                   : ~0ull);
    plan.clear();
    plan_pos = 0;
    plan_second = second;
    plan_sched_tick = scheduler.CurrentTick();
    plan_gc_tick = gc_tick;
    lru_cache.ForEachItemBelow(gc_tick, [&](ImageId image_id) {
        const Image& image = slot_images[image_id];
        if (image.store_pending ||
            (recent_tick != 0 && (image.tick_accessed_last >= recent_tick ||
                                  !scheduler.IsFree(image.tick_accessed_last)))) {
            ++gc_recent_kept;
            return false;
        }
        const bool dirty = True(image.flags & (ImageFlagBits::CpuDirty | ImageFlagBits::GpuDirty));
        u8 major = 0xff;
        if (image.SafeToDownload()) {
            // Drawn by the GPU: only VRAM has it. Stored first, only when full.
            if (mode == Mode::Full && !UsedSince(image, second, full_age)) {
                major = 4;
            }
        } else if (dirty && !UsedSince(image, second, VramPolicy::GarbageSeconds)) {
            major = 0;
        } else if (!UsedSince(image, second, VramPolicy::CacheSeconds)) {
            major = 1;
        } else if (mode == Mode::Full && !UsedSince(image, second, full_age)) {
            major = 3;
        }
        if (major != 0xff) {
            const u64 bytes = image.backing ? image.GetHostImageSize() : 0;
            plan.push_back({image_id, image.image_uid, major, bytes < dedicated, bytes});
        }
        return false;
    });
    // Garbage first; within each kind, what gives memory back to the driver at once (memory of its
    // own), and the biggest.
    std::sort(plan.begin(), plan.end(), [](const PlanItem& a, const PlanItem& b) {
        if (a.major != b.major) return a.major < b.major;
        if (a.small != b.small) return !a.small;
        return a.bytes > b.bytes;
    });
}

void TextureCache::ExecutePlan() {
    if (plan_pos >= plan.size() || !VramPolicy::WantBytes() || !VramPolicy::Allowed()) {
        return;
    }
    using VramPolicy::Mode;
    const auto start = std::chrono::steady_clock::now();
    const auto limit = std::chrono::microseconds(VramPolicy::CurrentMode() == Mode::Tight ? 1000 : 2000);
    // Each image the GPU drew is a copy into the game's memory (BeginStore): at most this many a pass.
    static constexpr size_t MaxStoresPerPass = 4;
    size_t stores = 0, frees = 0;
    while (plan_pos < plan.size() && frees < 40 && VramPolicy::WantBytes() &&
           std::chrono::steady_clock::now() - start < limit) {
        const PlanItem item = plan[plan_pos++];
        if (!slot_images.is_allocated(item.image_id)) {
            continue;
        }
        Image& image = slot_images[item.image_id];
        // Gone, replaced, or used since the plan was made: it stays.
        if (image.image_uid != item.image_uid || False(image.flags & ImageFlagBits::Registered) ||
            image.store_pending || image.tick_accessed_last >= plan_sched_tick ||
            (image.lru_touched_tick != ~0ULL && image.lru_touched_tick >= plan_gc_tick)) {
            continue;
        }
        if (item.major == 4) {
            if (stores >= MaxStoresPerPass || gc_tick < image.store_retry_tick) {
                continue;
            }
            if (!BeginStore(item.image_id)) {
                image.store_retry_tick = gc_tick + (u64{60} << std::min<u8>(image.store_failures, 5));
                image.store_failures = static_cast<u8>(std::min(image.store_failures + 1, 255));
                ++gc_tiled_kept;
                continue;
            }
            ++stores;
            continue; // freed once the GPU has copied it out (CompletePendingStores)
        }
        ++frees;
        ++gc_evictions;
        BbStats::gc_freed_images.fetch_add(1, std::memory_order_relaxed);
        NoteCollected(image, gc_second, item.major == 0 ? VramPolicy::Freed::Garbage : VramPolicy::Freed::Texture);
        FreeImage(item.image_id);
    }
    if (stores != 0) {
        // The copies go to the GPU now: a game write that meets one waits for it alone (LandStore).
        scheduler.Flush();
    }
    VramPolicy::NoteWork(std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count());
}

void TextureCache::ClassifyForMinute(u64 second) {
    u64 in_use = 0, cache = 0, garbage = 0, gpu = 0, dead = 0, dead_bytes = 0;
    u64 stale_live = 0, stale_orphan = 0; // 0.4.6 beta 4: garbage by whether a game texture is over it
    // 0.4.6 beta 5 (research): the stale images by the game resource over them, and the largest few.
    using BbEngine::GxResources::Owner;
    std::array<u64, size_t(Owner::kCount)> stale_bytes{}, stale_images{};
    u64 stale_gpu = 0;
    struct Largest {
        u64 bytes;
        ImageId image_id;
        Owner owner;
        bool at_start;
    };
    std::array<Largest, 5> largest{};
    lru_cache.ForEachItemBelow(gc_tick, [&](ImageId image_id) {
        const Image& image = slot_images[image_id];
        const u64 bytes = image.backing ? image.GetHostImageSize() : 0;
        if (UsedSince(image, second, 10)) {
            in_use += bytes;
        } else if (image.SafeToDownload()) {
            gpu += bytes;
        } else if (True(image.flags & (ImageFlagBits::CpuDirty | ImageFlagBits::GpuDirty))) {
            garbage += bytes;
            (BbEngine::GxResources::LiveTextureOverlaps(image.info.guest_address,
                                                        image.info.guest_address + image.info.guest_size)
                 ? stale_live
                 : stale_orphan) += bytes;
            bool at_start = false;
            const Owner owner = BbEngine::GxResources::Classify(
                image.info.guest_address, image.info.guest_address + std::max<u64>(image.info.guest_size, 1),
                &at_start);
            stale_bytes[size_t(owner)] += bytes;
            ++stale_images[size_t(owner)];
            if (True(image.flags & ImageFlagBits::GpuModified)) {
                stale_gpu += bytes;
            }
            if (bytes > largest.back().bytes) {
                largest.back() = {bytes, image_id, owner, at_start};
                std::sort(largest.begin(), largest.end(),
                          [](const Largest& a, const Largest& b) { return a.bytes > b.bytes; });
            }
        } else {
            cache += bytes;
        }
        // The game destroyed its texture and the image has not been used since (research).
        if (image.gx_dead_tick != 0 &&
            !(image.lru_touched_tick != ~0ULL && image.lru_touched_tick > image.gx_dead_tick)) {
            ++dead;
            dead_bytes += bytes;
        }
        return false;
    });
    VramPolicy::PublishTextures(in_use, cache, garbage, gpu);
    BbEngine::GxResources::NoteStale(stale_live, stale_orphan);
    std::string described;
    const u64 now_ns = u64(std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count());
    for (const Largest& l : largest) {
        if (l.bytes == 0) {
            break;
        }
        const Image& image = slot_images[l.image_id];
        char part[256];
        std::snprintf(part, sizeof(part),
                      "%s0x%llx %.0f MiB %ux%u, %u mips, %u layers, format %u, tile %u, %s%s, made %llu s ago%s%s",
                      described.empty() ? "" : "; ", (unsigned long long)image.info.guest_address,
                      double(l.bytes) / double(1ull << 20), image.info.size.width, image.info.size.height,
                      image.info.resources.levels, image.info.resources.layers, u32(image.info.pixel_format),
                      u32(image.info.tile_mode), BbEngine::GxResources::OwnerName(l.owner),
                      l.owner == Owner::None ? "" : (l.at_start ? " at its start" : " inside it"),
                      (unsigned long long)(now_ns > image.registered_ns ? (now_ns - image.registered_ns) / 1'000'000'000ull
                                                                        : 0),
                      True(image.flags & ImageFlagBits::GpuModified) ? ", drawn by the GPU" : "",
                      image.gx_dead_tick != 0 ? ", its texture destroyed" : "");
        described += part;
    }
    BbEngine::GxResources::NoteStaleByOwner(stale_bytes, stale_images, stale_gpu, described.c_str());
    BbEngine::GxResources::NoteDeadInVram(dead, dead_bytes);
}

void TextureCache::GarbageCollectSamplers() {
    total_used_samplers = samplers.size();
    if (total_used_samplers < trigger_gc_samplers) {
        return;
    }
    std::scoped_lock lock{samplers_mutex};
    bool pressured = false;
    bool aggresive = false;
    u64 ticks_to_destroy = 0;
    size_t num_deletions = 0;

    const auto configure = [&](bool allow_aggressive) {
        pressured = total_used_samplers >= pressure_gc_samplers;
        aggresive = allow_aggressive && total_used_samplers >= critical_gc_samplers;
        ticks_to_destroy = aggresive ? 160 : pressured ? 80 : 16;
        ticks_to_destroy = std::min(ticks_to_destroy, gc_tick);
        num_deletions = aggresive ? 40 : pressured ? 20 : 10;
    };
    const auto clean_up = [&](u64 hash) {
        if (num_deletions == 0) {
            return true;
        }
        --num_deletions;
        const size_t lru_id = samplers.at(hash).lru_id;
        samplers.erase(hash);
        sampler_lru_cache.Free(lru_id);
        ++sampler_generation; // remembered handles may be gone (SamplerGeneration)
        return false;
    };

    // Try to remove anything old enough and not high priority.
    configure(false);
    sampler_lru_cache.ForEachItemBelow(gc_tick - ticks_to_destroy, clean_up);

    if (total_used_samplers >= critical_gc_samplers) {
        // If we are still over the critical limit, run an aggressive GC
        configure(true);
        sampler_lru_cache.ForEachItemBelow(gc_tick - ticks_to_destroy, clean_up);
    }
}

void TextureCache::RunGarbageCollector() {
    SCOPE_EXIT {
        ++gc_tick;
    };
    // bbport (GPU hang guard): this frame's submission tick (GcRecentFrames), and the images
    // deleted long enough ago (ImageFreeDelayFrames).
    sched_tick_at_gc[gc_tick % sched_tick_at_gc.size()] = scheduler.CurrentTick();
    ns_at_gc[gc_tick % ns_at_gc.size()] = u64(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                  std::chrono::steady_clock::now().time_since_epoch())
                                                  .count());
    FlushImageGraveyard();

    GarbageCollectImages();
    GarbageCollectSamplers();
}

// 0.4.6 beta 10 (the owner rule, counted only): a draw or bind takes an image of a game's texture that
// went (destroyed, or something newer made over its memory), the first time since. Today it takes it
// back; the owner rule would hand it to the texture living there when that one was made without its data
// and is as large, and otherwise refuse it (a new image, from the memory or empty).
void TextureCache::NoteOwnerTake(Image& image) {
    if (image.gx_dead_tick == 0 || image.owner_noted_tick == image.gx_dead_tick || !image.gx_whole ||
        image.gx_owner == 0 || (image.lru_touched_tick != ~0ULL && image.lru_touched_tick > image.gx_dead_tick) ||
        !BbEngine::GxResources::Following()) {
        return;
    }
    image.owner_noted_tick = image.gx_dead_tick;
    const auto live = BbEngine::GxResources::LiveAt(image.info.guest_address);
    const bool hand_over = live.found && live.without_data && live.bytes >= image.info.guest_size;
    BbEngine::GxResources::NoteDeadImageTaken(!image.dead_by_reuse, hand_over, !hand_over,
                                              image.backing ? image.GetHostImageSize() : 0);
}

void TextureCache::TouchImage(const Image& image) {
    if (image.lru_touched_tick == gc_tick) {
        return;
    }
    image.lru_touched_tick = gc_tick;
    lru_cache.Touch(image.lru_id, gc_tick);
}

void TextureCache::DeleteImage(ImageId image_id) {
    Image& image = slot_images[image_id];
    ASSERT_MSG(!image.IsTracked(), "Image was not untracked");
    ASSERT_MSG(False(image.flags & ImageFlagBits::Registered), "Image was not unregistered");

    // Beta 16: an image built ahead going without a draw: replaced by a draw's while the game's
    // texture lives (a miss), or freed unused (the game destroyed the texture first, or VRAM).
    if (image.ahead != 0) {
        const auto it = ahead_at.find(image.info.guest_address);
        const bool listed = it != ahead_at.end() && it->second.image_id == image_id &&
                            it->second.image_uid == image.image_uid;
        if (image.ahead == 1) {
            if (listed && BbEngine::GxResources::TextureAt(image.info.guest_address).found) {
                BbEngine::GxResources::NoteAheadMiss();
            } else {
                BbEngine::GxResources::NoteAheadUnused(image.backing ? image.GetHostImageSize() : 0);
            }
        }
        if (listed) {
            ahead_at.erase(it);
        }
    }

    // Remove any registered meta areas.
    const auto& meta_info = image.info.meta_info;
    if (meta_info.cmask_addr) {
        surface_metas.erase(meta_info.cmask_addr);
    }
    if (meta_info.fmask_addr) {
        surface_metas.erase(meta_info.fmask_addr);
    }
    if (meta_info.htile_addr) {
        surface_metas.erase(meta_info.htile_addr);
    }

    {
        std::unique_lock lk{download_images_mutex};
        if (download_images.contains(image_id)) {
            download_images.erase(image_id);
        }
    }

    // bbport (GPU hang guard): kept for ImageFreeDelayFrames() frames first (FlushImageGraveyard).
    if (ImageFreeDelayFrames() != 0) {
        std::scoped_lock lk{graveyard_mutex};
        image_graveyard.push_back({gc_tick, image_id});
        return;
    }
    ReclaimImage(image_id);
}

void TextureCache::ReclaimImage(ImageId image_id) {
    // Reclaim image and any image views it references.
    scheduler.DeferOperation([this, image_id] {
        Image& image = slot_images[image_id];
        for (auto& backing : image.backing_images) {
            for (const ImageViewId image_view_id : backing.image_view_ids) {
                slot_image_views.erase(image_view_id);
            }
        }
        slot_images.erase(image_id);
    });
}

void TextureCache::FlushImageGraveyard() {
    const u64 delay = ImageFreeDelayFrames();
    std::scoped_lock lk{graveyard_mutex};
    while (!image_graveyard.empty() && image_graveyard.front().gc_tick + delay <= gc_tick) {
        // Deferred again: freed once the GPU has finished everything recorded until now.
        ReclaimImage(image_graveyard.front().image_id);
        image_graveyard.pop_front();
    }
}

} // namespace VideoCore

// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.5 beta 9: the copy shader's copies held to the end of the render pass
// they come in (BB_COPY_HOLD=0: off, recorded where they come, ending the pass, as before).
//
// Beta 8's trace (performance.md, "The copy shader"): no draw later in the same pass reads what a
// copy-shader dispatch inside it wrote; later passes do. So a copy batch that comes while a pass
// is open is kept here instead of ending the pass, and recorded where the pass ends anyway
// (Scheduler::EndRendering: other targets, a transfer, a dispatch, a barrier, a submission),
// before whatever ended it, in the order the copies came. A binding that touches a held copy (a
// read or a write of what it writes, a write of what it reads) sends the held copies first
// (Runtime::IsBufferAccessed): the pass ends there, as it did before, and the binding's barrier
// check then sees the copies. Each batch is recorded as before (MultiCopy or
// Runtime::CopyBuffer), with the barriers it needs then.
//
// Held only: batches whose source is a snapshot (the stream buffer) or the engine's VRAM copy of
// the game's memory, and whose destination is that VRAM copy, neither reaching memory bound in
// place (the GX region: the game reuses it once a fence says the GPU is done). Recorded by one
// thread at a time (the scheduler's producer).
#pragma once

#include <span>
#include <vector>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_gpu_savings.h"

namespace VideoCore {
struct Buffer;
}

namespace Vulkan::CopyHold {

struct Batch {
    const VideoCore::Buffer* src;
    const VideoCore::Buffer* dst;
    u32 first, count; ///< its ranges in State::copies
    u64 src_min, src_max, dst_min, dst_max;
};

/// Records the held batches (vk_shader_hle.cpp), ending the pass first.
using FlushFn = void (*)(void* context, GpuSavings::PassBreak why);

struct State {
    std::vector<vk::BufferCopy> copies;
    std::vector<Batch> batches;
    bool flushing = false;
    FlushFn flush = nullptr;
    void* context = nullptr;
    /// The scheduler the copies are held for (the presenter has its own: not that one).
    const void* scheduler = nullptr;
};

inline State state;

/// Batches held at most; past it the pass ends as before.
constexpr size_t MaxBatches = 1024;

/// Whether copies are held for `scheduler` and none is being recorded now.
[[nodiscard]] inline bool Pending(const void* scheduler) noexcept {
    return !state.batches.empty() && !state.flushing && state.scheduler == scheduler;
}

/// Whether [offset, offset + size) of `buffer` touches a held copy: what one writes (read or
/// written here), or what one reads (`write`: written here).
[[nodiscard]] inline bool Touches(const VideoCore::Buffer* buffer, u64 offset, u64 size,
                                  bool write) noexcept {
    const u64 end = size > ~u64{0} - offset ? ~u64{0} : offset + size; // (a whole-size range)
    const auto overlaps = [&](u64 begin, u64 stop) { return offset < stop && begin < end; };
    for (const Batch& batch : state.batches) {
        const bool dst = batch.dst == buffer && overlaps(batch.dst_min, batch.dst_max);
        const bool src = write && batch.src == buffer && overlaps(batch.src_min, batch.src_max);
        if (!dst && !src) {
            continue;
        }
        // The batch's bounds overlap: its ranges decide.
        for (u32 i = batch.first; i < batch.first + batch.count; ++i) {
            const auto& copy = state.copies[i];
            if (dst && overlaps(copy.dstOffset, copy.dstOffset + copy.size)) {
                return true;
            }
            if (src && overlaps(copy.srcOffset, copy.srcOffset + copy.size)) {
                return true;
            }
        }
    }
    return false;
}

/// Records the held copies now (the pass ends first, named `why`).
inline void Flush(const void* scheduler, GpuSavings::PassBreak why) {
    if (Pending(scheduler) && state.flush) {
        state.flush(state.context, why);
    }
}

} // namespace Vulkan::CopyHold

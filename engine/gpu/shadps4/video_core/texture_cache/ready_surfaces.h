// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 15 part 3 (BB_READY_SURFACES=0: off; docs/memory-model.md, beta 15 part 3): surfaces'
// memory made during the loading screen. The surfaces made in the 2 s after a load that no dead
// target of their shape could give an image (beta 8's pool) allocate one at their first draw: 11.8
// and 14.3 ms of a frame on NVIDIA. Their shapes (a Vulkan image's creation info) are learned, the
// largest count per shape seen after one load; during the next loading screen, from its 30th frame,
// a background thread makes Vulkan images of those shapes, within BB_READY_SURFACES_MB (256) and
// only while VRAM is comfortable; a new surface with no dead image of its shape takes one of its
// shape (as it takes a dead target's: equal creation info only); those not taken 3 s after the load
// are freed. The load clock (video_core/load_work.cpp) calls Make, EndLearning and FreeUntaken.
#pragma once

#include "common/types.h"
#include "video_core/texture_cache/image.h"

namespace VideoCore::ReadySurfaces {

[[nodiscard]] bool Enabled();
/// The texture cache's device and allocator (its constructor).
void Init(vk::Device device, VmaAllocator allocator);

/// The Image constructor, for a new surface with no dead image of its shape (t_ready_take): a ready
/// image of exactly this creation info, moved into `out`.
bool Take(const vk::ImageCreateInfo& ci, UniqueImage& out);
/// A new surface allocated its image at its draw (TextureCache::FindImage), in `ns`. Counted and
/// learned in the 2 s after a load (`after_load`).
void NoteAllocated(const vk::ImageCreateInfo& ci, u64 bytes, u64 ns, bool after_load);

/// A load's 30th frame: the background thread makes the learned shapes' images.
void Make();
/// 2 s after a load: its surfaces' counts folded into the learned ones (the largest per shape).
void EndLearning();
/// 3 s after a load: the images not taken are freed.
void FreeUntaken();

/// Since start (the line on each load's first frames takes differences).
struct Totals {
    u64 made = 0, made_bytes = 0, taken = 0, freed = 0, allocated = 0, allocated_ns = 0;
};
[[nodiscard]] Totals TotalsNow();

} // namespace VideoCore::ReadySurfaces

namespace VideoCore {
/// Set by TextureCache::FindImage around a new surface's construction: it may take a ready image
/// (t_ready_take); whether it did (t_ready_taken).
inline thread_local bool t_ready_take = false;
inline thread_local bool t_ready_taken = false;
} // namespace VideoCore

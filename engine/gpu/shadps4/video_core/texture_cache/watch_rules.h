// SPDX-License-Identifier: GPL-2.0-or-later
// Which images the texture cache leaves to the game's calls instead of watching their pages
// (TextureCache::TrackImage). A page-protected image learns of CPU writes by faulting; one left to the
// calls learns of them from the calls that make them (GX's create with data, YEBIS taking memory from its
// pool, YEBIS's and Scaleform's unmaps, the engine's own DMA stores: TextureCache::ProcessGxCreated,
// ProcessWritten and NoteEngineStore), and the texture net (beta 16 part 3, Judge) checks that none is
// missed. Plain code (tests/test_watch_rules.cpp).
//
//   0.4.6 beta 8-9: surfaces the game made without data for the GPU, render and depth targets.
//   0.4.6 beta 10: GX textures made with their data.
//   0.4.6 beta 16 part 2: Scaleform's planes, told by Scaleform's unmap.
//   0.4.6 beta 16, YEBIS by its calls (memory-model.md; in place of part 1's rule for YEBIS's registered
//   textures and step 2's for images over no game resource in its heap): every image in YEBIS's pool, the
//   heap all of YEBIS's memory comes from. The CPU never
//   writes a YEBIS texture's memory in place: its data is staged in a block YEBIS has just taken from the
//   pool and copied in by the GPU, and the staging blocks are what the copy passes read; so YEBIS taking
//   memory is the call that tells (a written event for the new block), and the GPU's copies are replayed
//   by the engine. Where the pool is not known (not 1.09's code, or YEBIS's allocations not followed),
//   nothing would tell: YEBIS's textures are watched, as before beta 16.
//   The release (0.4.6): one switch left, safe mode's BB_WATCH_GAME_TEXTURES=1, every image watched as in
//   0.4.5 (BB_WATCH_GAME_TEXTURES=targets, BB_WATCH_SCALEFORM and BB_WATCH_YEBIS, the betas' steps back,
//   went with the betas).
#pragma once

#include "engine/gx_resources.h"

namespace VideoCore::WatchRules {

struct Settings {
    bool watch_all = false; ///< BB_WATCH_GAME_TEXTURES=1 (safe mode): every image watched, as in 0.4.5
};

/// What the texture cache knows of an image's memory at its registration.
struct Image {
    BbEngine::GxResources::Owner owner = BbEngine::GxResources::Owner::None;
    bool whole = false;      ///< a resource of the game's starts where the image does
    bool surface = false;    ///< a surface the game made without data for the GPU to write
    bool with_data = false;  ///< that resource was made with its data
    bool yebis_pool = false; ///< it lies in YEBIS's pool (GxResources::InYebisPool)
};

/// Whether the game's calls tell this image's writes, so its pages are not watched.
inline bool ByCalls(const Image& image, const Settings& s) {
    using BbEngine::GxResources::Owner;
    if (s.watch_all) {
        return false;
    }
    // YEBIS's pool: whatever lies there (its textures, its staging blocks, nothing registered).
    if (image.yebis_pool) {
        return true;
    }
    if (!image.whole) {
        return false;
    }
    if (image.surface || image.owner == Owner::RenderTarget || image.owner == Owner::DepthTarget) {
        return true;
    }
    switch (image.owner) {
    case Owner::TextureData:
    case Owner::Scaleform:
        return true;
    default:
        return false; // a YEBIS texture outside its known pool: nothing would tell
    }
}

/// Whether the texture net checks this image: left to the calls, and filled by the CPU (not a surface,
/// a target or memory the GPU writes).
inline bool NetChecks(const Image& image, const Settings& s) {
    using BbEngine::GxResources::Owner;
    if (!ByCalls(image, s) || image.surface) {
        return false;
    }
    return image.yebis_pool || image.owner == Owner::TextureData || image.owner == Owner::Scaleform;
}

/// The texture net's lane for an image it checks: 0 the middleware's (YEBIS's pool and Scaleform's
/// planes: few, small, left to the calls in beta 16), 1 the GX textures made with their data (since beta
/// 10). Each lane goes round on a budget of its own, so the first is looked at within seconds.
inline unsigned NetLane(const Image& image) {
    using BbEngine::GxResources::Owner;
    return image.yebis_pool || image.owner == Owner::Yebis || image.owner == Owner::Scaleform ? 0 : 1;
}

/// The engine's own store into guest memory (a DMA copy or fill replayed on the CPU): whether this image
/// is marked written by it. Those left to the calls whose contents come from memory (the net's kind: not
/// a surface, not drawn by the GPU); a watched image faults, and a GPU-drawn one keeps its contents, as
/// before.
inline bool StoreTells(const Image& image, const Settings& s, bool calls_watch, bool gpu_modified) {
    return calls_watch && !gpu_modified && NetChecks(image, s);
}
constexpr unsigned kNetLanes = 2;

/// The texture net's look at an image (part 3): whether its memory hashes differently from the last
/// look, whether the cache marked it (possibly) written since (a call told it), and whether the last
/// look already found it changed with no mark. The call telling a write comes after the write (YEBIS's
/// update and unmap, Scaleform's unmap), so a first change with no mark is only a suspect, looked at
/// again a second later; still no mark then, it is a write no call told.
enum class NetLook { Record, Suspect, Untold };
inline NetLook Judge(bool changed, bool marked_since, bool suspect) {
    if (!changed || marked_since) {
        return NetLook::Record;
    }
    return suspect ? NetLook::Untold : NetLook::Suspect;
}

} // namespace VideoCore::WatchRules

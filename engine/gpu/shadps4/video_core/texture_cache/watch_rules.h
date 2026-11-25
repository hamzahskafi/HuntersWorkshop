// SPDX-License-Identifier: GPL-2.0-or-later
// Which images the texture cache leaves to the game's calls instead of watching their pages
// (TextureCache::TrackImage). A page-protected image learns of CPU writes by faulting; one left to the
// calls learns of them from the calls that make them (GX's create with data, YEBIS's update and Unmap,
// Scaleform's unmap: TextureCache::ProcessGxCreated and ProcessWritten), and the texture net (beta 16
// part 3, Judge) checks that none is missed. Plain code (tests/test_watch_rules.cpp).
//
//   0.4.6 beta 8-9: surfaces the game made without data for the GPU, render and depth targets.
//   0.4.6 beta 10: GX textures made with their data (BB_WATCH_GAME_TEXTURES=targets keeps them watched).
//   0.4.6 beta 16 part 1: YEBIS's textures (BB_WATCH_YEBIS_TEXTURES=1 keeps them watched): its utility
//   textures, packed beside its shared vertex buffer, whose quads for every post-processing pass
//   faulted on their pages. As first built only those made with their data; the first run showed the
//   faults going on, on the ones YEBIS makes without data and fills itself (CTextureUtil::InitializeDevice:
//   a create with no data, then a method of its texture object with a buffer; the first minute's YEBIS
//   updates, told since beta 9, fit it), so every YEBIS texture: YEBIS writes them through its own calls
//   or the GPU, and the texture net's first lane looks at them within seconds. Part 2: Scaleform's
//   planes (BB_WATCH_SCALEFORM=1), told by Scaleform's unmap.
//   0.4.6 beta 16, step 2 of the remaining faults: images over no game resource in YEBIS's heap
//   (BB_WATCH_YEBIS_HEAP=1 keeps them watched). The faults still left were all on such images, every
//   write beside the image, never in it: the heap's bookkeeping and YEBIS's other memory between its
//   textures (memory-model.md). The engine registers nothing there (none lay in an allocation of the
//   two YEBIS allocators step 1 hooked; step 2 hooks the third to name them); like YEBIS's registered
//   textures, they are left to YEBIS's calls and the GPU, and the texture net's first lane checks them.
//   The call that tells: YEBIS's allocation of the memory again (GxResources, a written event for the
//   new block: TextureCache::ProcessWritten), what page protection learnt from the heap's header write.
#pragma once

#include "engine/gx_resources.h"

namespace VideoCore::WatchRules {

/// BB_WATCH_GAME_TEXTURES: 1 watches everything (safe mode), "targets" beta 9's set, else the default.
enum class Unwatch { None, Targets, TargetsAndData };

struct Settings {
    Unwatch kinds = Unwatch::TargetsAndData;
    bool watch_yebis = false;      ///< BB_WATCH_YEBIS_TEXTURES=1
    bool watch_scaleform = false;  ///< BB_WATCH_SCALEFORM=1
    bool watch_yebis_heap = false; ///< BB_WATCH_YEBIS_HEAP=1
};

/// What the texture cache knows of an image's memory at its registration.
struct Image {
    BbEngine::GxResources::Owner owner = BbEngine::GxResources::Owner::None;
    bool whole = false;     ///< a resource of the game's starts where the image does
    bool surface = false;   ///< a surface the game made without data for the GPU to write
    bool with_data = false; ///< that resource was made with its data
    bool yebis_heap = false; ///< it lies in YEBIS's heap (GxResources::InYebisHeap)
};

/// Step 2: memory YEBIS took from its heap that holds no resource of the game's.
inline bool YebisHeapOnly(const Image& image) {
    return image.owner == BbEngine::GxResources::Owner::None && image.yebis_heap;
}

/// Whether the game's calls tell this image's writes, so its pages are not watched.
inline bool ByCalls(const Image& image, const Settings& s) {
    using BbEngine::GxResources::Owner;
    if (s.kinds == Unwatch::None) {
        return false;
    }
    if (YebisHeapOnly(image)) {
        return s.kinds == Unwatch::TargetsAndData && !s.watch_yebis_heap;
    }
    if (!image.whole) {
        return false;
    }
    if (image.surface || image.owner == Owner::RenderTarget || image.owner == Owner::DepthTarget) {
        return true;
    }
    if (s.kinds != Unwatch::TargetsAndData) {
        return false;
    }
    switch (image.owner) {
    case Owner::TextureData:
        return true;
    case Owner::Yebis:
        return !s.watch_yebis;
    case Owner::Scaleform:
        return !s.watch_scaleform;
    default:
        return false;
    }
}

/// Whether the texture net checks this image: left to the calls, and filled by the CPU (not a surface,
/// a target or memory the GPU writes).
inline bool NetChecks(const Image& image, const Settings& s) {
    using BbEngine::GxResources::Owner;
    if (!ByCalls(image, s) || image.surface) {
        return false;
    }
    return image.owner == Owner::TextureData || image.owner == Owner::Yebis || image.owner == Owner::Scaleform ||
           YebisHeapOnly(image);
}

/// The texture net's lane for an image it checks: 0 the middleware's (YEBIS's and Scaleform's: few,
/// small, left to the calls in beta 16; step 2: YEBIS's heap's too), 1 the GX textures made with their
/// data (since beta 10). Each lane goes round on a budget of its own, so the first is looked at within
/// seconds.
inline unsigned NetLane(const Image& image) {
    using BbEngine::GxResources::Owner;
    return image.owner == Owner::Yebis || image.owner == Owner::Scaleform || YebisHeapOnly(image) ? 0 : 1;
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

// SPDX-License-Identifier: GPL-2.0-or-later
// Which images the texture cache leaves to the game's calls instead of watching their pages
// (TextureCache::TrackImage). A page-protected image learns of CPU writes by faulting; one left to the
// calls learns of them from the calls that make them (GX's create with data, YEBIS's update and Unmap,
// Scaleform's unmap: TextureCache::ProcessGxCreated and ProcessWritten), and the texture net (beta 16
// part 3, Judge) checks that none is missed. Plain code (tests/test_watch_rules.cpp).
//
//   0.4.6 beta 8-9: surfaces the game made without data for the GPU, render and depth targets.
//   0.4.6 beta 10: GX textures made with their data (BB_WATCH_GAME_TEXTURES=targets keeps them watched).
//   0.4.6 beta 16 part 1: YEBIS's textures made with their data (BB_WATCH_YEBIS_TEXTURES=1 keeps them
//   watched): its utility textures, packed beside its shared vertex buffer, whose quads for every
//   post-processing pass faulted on their pages. Part 2: Scaleform's planes (BB_WATCH_SCALEFORM=1),
//   told by Scaleform's unmap.
#pragma once

#include "engine/gx_resources.h"

namespace VideoCore::WatchRules {

/// BB_WATCH_GAME_TEXTURES: 1 watches everything (safe mode), "targets" beta 9's set, else the default.
enum class Unwatch { None, Targets, TargetsAndData };

struct Settings {
    Unwatch kinds = Unwatch::TargetsAndData;
    bool watch_yebis = false;     ///< BB_WATCH_YEBIS_TEXTURES=1
    bool watch_scaleform = false; ///< BB_WATCH_SCALEFORM=1
};

/// What the texture cache knows of an image's memory at its registration.
struct Image {
    BbEngine::GxResources::Owner owner = BbEngine::GxResources::Owner::None;
    bool whole = false;     ///< a resource of the game's starts where the image does
    bool surface = false;   ///< a surface the game made without data for the GPU to write
    bool with_data = false; ///< that resource was made with its data
};

/// Whether the game's calls tell this image's writes, so its pages are not watched.
inline bool ByCalls(const Image& image, const Settings& s) {
    using BbEngine::GxResources::Owner;
    if (s.kinds == Unwatch::None || !image.whole) {
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
        return image.with_data && !s.watch_yebis;
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
    return image.owner == Owner::TextureData || image.owner == Owner::Yebis || image.owner == Owner::Scaleform;
}

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

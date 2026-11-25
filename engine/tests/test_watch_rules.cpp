// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 16 (shadps4/video_core/texture_cache/watch_rules.h): which images the texture cache leaves to
// the game's calls instead of watching their pages, under each setting, which of those the texture net
// checks and in which lane, which an engine DMA store marks written, and the net's verdict on a look. Every
// owner kind, with and without its data, surface or not, whole or not, in YEBIS's pool or not (YEBIS by its
// calls).
#include "video_core/texture_cache/watch_rules.h"

#include <cstdio>

using namespace VideoCore::WatchRules;
using BbEngine::GxResources::Owner;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

// An image at the start of a resource of the game's, outside YEBIS's pool.
Image Whole(Owner owner, bool with_data = false, bool surface = false) {
    return {owner, true, surface, with_data, false};
}

// An image in YEBIS's pool (`owner` the resource over it: a YEBIS texture, or none for a staging block).
Image InPool(Owner owner = Owner::None, bool whole = false, bool with_data = false, bool surface = false) {
    return {owner, whole, surface, with_data, true};
}

} // namespace

int main() {
    const Settings beta16{};
    Settings watch_yebis{};
    watch_yebis.watch_yebis = true;
    Settings watch_scaleform{};
    watch_scaleform.watch_scaleform = true;
    Settings targets{};
    targets.kinds = Unwatch::Targets;
    Settings safe{};
    safe.kinds = Unwatch::None;

    // Beta 8-10, unchanged: surfaces and targets; GX textures made with their data; buffers and memory
    // the game made nothing at stay watched.
    check(ByCalls(Whole(Owner::RenderTarget), beta16), "a render target is left to the calls");
    check(ByCalls(Whole(Owner::DepthTarget), beta16), "a depth target is left to the calls");
    check(ByCalls(Whole(Owner::TextureNoData, false, true), beta16), "a surface is left to the calls");
    check(ByCalls(Whole(Owner::TextureData, true), beta16), "a GX texture made with its data is left to the calls");
    check(!ByCalls(Whole(Owner::Buffer), beta16), "a buffer's memory stays watched");
    check(!ByCalls(Whole(Owner::None), beta16), "memory no resource starts at stays watched");
    check(!ByCalls({Owner::None, false, false, false, false}, beta16),
          "memory over no resource outside YEBIS's pool stays watched");
    check(!ByCalls(Whole(Owner::TextureNoData), beta16), "a GX texture made without its data, not a surface, stays watched");
    check(!ByCalls({Owner::TextureData, false, false, true, false}, beta16),
          "an image inside a texture (not at its start) stays watched");
    check(!ByCalls({Owner::RenderTarget, false, true, false, false}, beta16), "a target not at its start stays watched");

    // YEBIS by its calls: every image in YEBIS's pool is left to the calls, whatever the registry has there
    // (BB_WATCH_YEBIS=1 watches them as before beta 16).
    check(ByCalls(InPool(Owner::Yebis, true, true), beta16), "a YEBIS texture made with its data is left to the calls");
    check(ByCalls(InPool(Owner::Yebis, true, false), beta16), "a YEBIS texture made without its data is left to the calls");
    check(ByCalls(InPool(Owner::Yebis, true, false, true), beta16), "a YEBIS surface is left to the calls");
    check(ByCalls(InPool(), beta16), "a staging block (no game resource) in the pool is left to the calls");
    check(ByCalls(InPool(Owner::Yebis, false), beta16), "an image inside a YEBIS texture in the pool is left to the calls");
    // Outside the pool (not 1.09's code, or YEBIS's allocations not followed): nothing would tell.
    check(!ByCalls(Whole(Owner::Yebis, true), beta16), "a YEBIS texture outside a known pool stays watched");
    check(!ByCalls(Whole(Owner::Yebis, false), beta16), "a YEBIS texture made without data outside the pool stays watched");
    check(ByCalls(Whole(Owner::Yebis, false, true), beta16), "a YEBIS surface outside the pool is left to the calls (beta 9)");
    // BB_WATCH_YEBIS=1: as before beta 16 (surfaces and targets left to the calls, the rest watched).
    check(!ByCalls(InPool(Owner::Yebis, true, true), watch_yebis), "BB_WATCH_YEBIS=1 watches YEBIS's textures");
    check(!ByCalls(InPool(), watch_yebis), "BB_WATCH_YEBIS=1 watches its staging blocks");
    check(ByCalls(InPool(Owner::Yebis, true, false, true), watch_yebis), "BB_WATCH_YEBIS=1 leaves its surfaces to the calls");
    check(ByCalls(Whole(Owner::Scaleform), watch_yebis), "BB_WATCH_YEBIS=1 leaves Scaleform's planes alone");
    check(ByCalls(Whole(Owner::TextureData, true), watch_yebis), "BB_WATCH_YEBIS=1 leaves GX's textures alone");

    // Beta 16 part 2: Scaleform's planes; BB_WATCH_SCALEFORM=1.
    check(ByCalls(Whole(Owner::Scaleform), beta16), "a Scaleform plane is left to the calls");
    check(!ByCalls(Whole(Owner::Scaleform), watch_scaleform), "BB_WATCH_SCALEFORM=1 watches Scaleform's planes");
    check(ByCalls(InPool(Owner::Yebis, true, true), watch_scaleform), "BB_WATCH_SCALEFORM=1 leaves YEBIS's pool alone");

    // BB_WATCH_GAME_TEXTURES=targets: beta 9's set only; =1 (safe mode): everything watched.
    check(ByCalls(Whole(Owner::RenderTarget), targets), "targets: a render target is left to the calls");
    check(!ByCalls(Whole(Owner::TextureData, true), targets), "targets: a GX texture made with its data is watched");
    check(!ByCalls(InPool(Owner::Yebis, true, true), targets), "targets: a YEBIS texture is watched");
    check(!ByCalls(InPool(), targets), "targets: a staging block is watched");
    check(ByCalls(InPool(Owner::Yebis, true, false, true), targets), "targets: a YEBIS surface is left to the calls");
    check(!ByCalls(Whole(Owner::Scaleform), targets), "targets: a Scaleform plane is watched");
    for (int o = 0; o < int(Owner::kCount); ++o) {
        for (int bits = 0; bits < 16; ++bits) {
            const Image image{Owner(o), (bits & 8) != 0, (bits & 2) != 0, (bits & 1) != 0, (bits & 4) != 0};
            check(!ByCalls(image, safe), "safe mode: every image watched");
            check(!NetChecks(image, safe), "safe mode: the net checks nothing");
            check(!StoreTells(image, safe, true, false), "safe mode: no store marks an image (all watched)");
        }
    }

    // Part 3: the net checks what is left to the calls and filled by the CPU, nothing else.
    check(NetChecks(Whole(Owner::TextureData, true), beta16), "the net checks a GX texture made with its data");
    check(NetChecks(InPool(Owner::Yebis, true, true), beta16), "the net checks a YEBIS texture made with its data");
    check(NetChecks(InPool(Owner::Yebis, true, false), beta16), "the net checks a YEBIS texture made without its data");
    check(NetChecks(InPool(), beta16), "the net checks a staging block");
    check(NetChecks(Whole(Owner::Scaleform), beta16), "the net checks a Scaleform plane");
    check(!NetChecks(InPool(Owner::Yebis, true, true), watch_yebis), "the net leaves a watched YEBIS texture");
    check(!NetChecks(InPool(), watch_yebis), "the net leaves a watched staging block");
    check(!NetChecks(Whole(Owner::Yebis, true), beta16), "the net leaves a YEBIS texture outside the pool (watched)");
    check(!NetChecks(Whole(Owner::Scaleform), watch_scaleform), "the net leaves a watched Scaleform plane");
    check(!NetChecks(Whole(Owner::RenderTarget), beta16), "the net leaves a render target");
    check(!NetChecks(Whole(Owner::DepthTarget), beta16), "the net leaves a depth target");
    check(!NetChecks(Whole(Owner::TextureNoData, false, true), beta16), "the net leaves a surface");
    check(!NetChecks(InPool(Owner::Yebis, true, false, true), beta16), "the net leaves a YEBIS surface");
    check(!NetChecks(Whole(Owner::TextureData, true, true), beta16), "the net leaves a GX surface");
    check(!NetChecks(Whole(Owner::Buffer), beta16), "the net leaves a buffer");
    check(!NetChecks({Owner::None, false, false, false, false}, beta16),
          "the net leaves memory over no resource outside the pool");
    // Whatever the net checks is left to the calls, and every image has a lane.
    const Settings* const all[] = {&beta16, &watch_yebis, &watch_scaleform, &targets};
    for (int o = 0; o < int(Owner::kCount); ++o) {
        for (int bits = 0; bits < 16; ++bits) {
            const Image image{Owner(o), (bits & 4) != 0, (bits & 2) != 0, (bits & 1) != 0, (bits & 8) != 0};
            for (const Settings* s : all) {
                check(!NetChecks(image, *s) || ByCalls(image, *s), "the net checks only images left to the calls");
                // A store marks only what the net would check (left to the calls, filled from memory).
                check(StoreTells(image, *s, true, false) == NetChecks(image, *s),
                      "a store marks what the net checks");
                check(!StoreTells(image, *s, false, false), "a store leaves a watched image to its fault");
                check(!StoreTells(image, *s, true, true), "a store leaves a GPU-drawn image's contents");
            }
            check(NetLane(image) < kNetLanes, "every image in a lane");
        }
    }

    // The net's lanes: the middleware's (few, small: seconds a round) apart from GX's (minutes).
    check(NetLane(InPool(Owner::Yebis, true)) == 0 && NetLane(Whole(Owner::Scaleform)) == 0,
          "YEBIS's pool and Scaleform in the first lane");
    check(NetLane(InPool()) == 0, "a staging block in the first lane");
    check(NetLane(Whole(Owner::TextureData, true)) == 1, "GX's textures in the second lane");

    // YEBIS by its calls, part 2: the engine's own DMA store marks the images left to the calls that the
    // CPU fills: a GX texture made with its data, a Scaleform plane, YEBIS's pool; not a surface, a buffer,
    // memory over no resource outside the pool (watched: it faults).
    check(StoreTells(Whole(Owner::TextureData, true), beta16, true, false), "a store marks a GX texture made with data");
    check(StoreTells(Whole(Owner::Scaleform), beta16, true, false), "a store marks a Scaleform plane");
    check(StoreTells(InPool(Owner::Yebis, true, true), beta16, true, false), "a store marks a YEBIS texture");
    check(StoreTells(InPool(), beta16, true, false), "a store marks a staging block");
    check(!StoreTells(Whole(Owner::TextureNoData, false, true), beta16, true, false), "a store leaves a surface");
    check(!StoreTells(Whole(Owner::RenderTarget), beta16, true, false), "a store leaves a render target");
    check(!StoreTells(Whole(Owner::Buffer), beta16, true, false), "a store leaves a buffer's image");

    // The net's look: a change is a suspect first (the telling call comes after the write), a write no
    // call told only when still unmarked a second later; a mark at either look makes it told.
    check(Judge(false, false, false) == NetLook::Record, "unchanged: recorded");
    check(Judge(false, false, true) == NetLook::Record, "a suspect unchanged again: recorded");
    check(Judge(true, true, false) == NetLook::Record, "changed and marked: told");
    check(Judge(true, false, false) == NetLook::Suspect, "changed with no mark: a suspect first");
    check(Judge(true, true, true) == NetLook::Record, "a suspect marked by the next look: told");
    check(Judge(true, false, true) == NetLook::Untold, "a suspect still unmarked: a write no call told");
    check(Judge(false, true, true) == NetLook::Record, "a suspect marked and unchanged: recorded");

    if (g_fail) {
        std::printf("watch-rules-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("watch-rules-test: ok\n");
    return 0;
}

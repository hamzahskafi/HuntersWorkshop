// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 16 parts 1-3 (shadps4/video_core/texture_cache/watch_rules.h): which images the texture
// cache leaves to the game's calls instead of watching their pages, under each setting, which of those
// the texture net checks, and its verdict on a look. Every owner kind, with and without its data,
// surface or not, whole or not.
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

Image Whole(Owner owner, bool with_data = false, bool surface = false) {
    return {owner, true, surface, with_data};
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
    check(!ByCalls(Whole(Owner::TextureNoData), beta16), "a GX texture made without its data, not a surface, stays watched");
    check(!ByCalls({Owner::TextureData, false, false, true}, beta16),
          "an image inside a texture (not at its start) stays watched");
    check(!ByCalls({Owner::RenderTarget, false, true, false}, beta16), "a target not at its start stays watched");

    // Part 1: YEBIS's textures, made with their data or not (its utility textures made without and filled
    // afterwards faulted on in the first run); BB_WATCH_YEBIS_TEXTURES=1.
    check(ByCalls(Whole(Owner::Yebis, true), beta16), "a YEBIS texture made with its data is left to the calls");
    check(ByCalls(Whole(Owner::Yebis, false), beta16), "a YEBIS texture made without its data is left to the calls");
    check(ByCalls(Whole(Owner::Yebis, false, true), beta16), "a YEBIS surface is left to the calls");
    check(!ByCalls({Owner::Yebis, false, false, true}, beta16), "an image inside a YEBIS texture stays watched");
    check(!ByCalls(Whole(Owner::Yebis, false), watch_yebis), "BB_WATCH_YEBIS_TEXTURES=1 watches those made without");
    check(!ByCalls(Whole(Owner::Yebis, true), watch_yebis), "BB_WATCH_YEBIS_TEXTURES=1 watches YEBIS's textures");
    check(ByCalls(Whole(Owner::Scaleform), watch_yebis), "BB_WATCH_YEBIS_TEXTURES=1 leaves Scaleform's planes alone");

    // Part 2: Scaleform's planes; BB_WATCH_SCALEFORM=1.
    check(ByCalls(Whole(Owner::Scaleform), beta16), "a Scaleform plane is left to the calls");
    check(!ByCalls(Whole(Owner::Scaleform), watch_scaleform), "BB_WATCH_SCALEFORM=1 watches Scaleform's planes");
    check(ByCalls(Whole(Owner::Yebis, true), watch_scaleform), "BB_WATCH_SCALEFORM=1 leaves YEBIS's textures alone");

    // BB_WATCH_GAME_TEXTURES=targets: beta 9's set only; =1 (safe mode): everything watched.
    check(ByCalls(Whole(Owner::RenderTarget), targets), "targets: a render target is left to the calls");
    check(!ByCalls(Whole(Owner::TextureData, true), targets), "targets: a GX texture made with its data is watched");
    check(!ByCalls(Whole(Owner::Yebis, true), targets), "targets: a YEBIS texture is watched");
    check(!ByCalls(Whole(Owner::Scaleform), targets), "targets: a Scaleform plane is watched");
    for (int o = 0; o < int(Owner::kCount); ++o) {
        for (int bits = 0; bits < 4; ++bits) {
            const Image image = Whole(Owner(o), bits & 1, bits & 2);
            check(!ByCalls(image, safe), "safe mode: every image watched");
            check(!NetChecks(image, safe), "safe mode: the net checks nothing");
        }
    }

    // Part 3: the net checks what is left to the calls and filled by the CPU, nothing else.
    check(NetChecks(Whole(Owner::TextureData, true), beta16), "the net checks a GX texture made with its data");
    check(NetChecks(Whole(Owner::Yebis, true), beta16), "the net checks a YEBIS texture made with its data");
    check(NetChecks(Whole(Owner::Yebis, false), beta16), "the net checks a YEBIS texture made without its data");
    check(NetChecks(Whole(Owner::Scaleform), beta16), "the net checks a Scaleform plane");
    check(!NetChecks(Whole(Owner::Yebis, true), watch_yebis), "the net leaves a watched YEBIS texture");
    check(!NetChecks(Whole(Owner::Scaleform), watch_scaleform), "the net leaves a watched Scaleform plane");
    check(!NetChecks(Whole(Owner::RenderTarget), beta16), "the net leaves a render target");
    check(!NetChecks(Whole(Owner::DepthTarget), beta16), "the net leaves a depth target");
    check(!NetChecks(Whole(Owner::TextureNoData, false, true), beta16), "the net leaves a surface");
    check(!NetChecks(Whole(Owner::Yebis, false, true), beta16), "the net leaves a YEBIS surface");
    check(!NetChecks(Whole(Owner::TextureData, true, true), beta16), "the net leaves a GX surface");
    check(!NetChecks(Whole(Owner::Buffer), beta16), "the net leaves a buffer");
    // Whatever the net checks is left to the calls.
    const Settings* const all[] = {&beta16, &watch_yebis, &watch_scaleform, &targets};
    for (int o = 0; o < int(Owner::kCount); ++o) {
        for (int bits = 0; bits < 8; ++bits) {
            const Image image{Owner(o), (bits & 4) != 0, (bits & 2) != 0, (bits & 1) != 0};
            for (const Settings* s : all) {
                check(!NetChecks(image, *s) || ByCalls(image, *s), "the net checks only images left to the calls");
            }
        }
    }

    // The net's lanes: the middleware's (few, small: seconds a round) apart from GX's (minutes).
    check(NetLane(Owner::Yebis) == 0 && NetLane(Owner::Scaleform) == 0, "YEBIS and Scaleform in the first lane");
    check(NetLane(Owner::TextureData) == 1, "GX's textures in the second lane");
    for (int o = 0; o < int(Owner::kCount); ++o) {
        check(NetLane(Owner(o)) < kNetLanes, "every kind in a lane");
    }

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

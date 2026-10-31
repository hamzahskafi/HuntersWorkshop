// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.4 beta 19: ds_append and ds_consume on the GDS translated as one atomic
// for each subgroup, not one for each lane. The GDS is an on-chip counter the PS4 adds a wave's
// active lanes to in one operation; the recompiler stands it in with a storage buffer, and turned
// each append into an atomic add of 1 by every lane. The game's cs a954e79d (one 32-lane group,
// five dispatches a frame) loops over such appends: 35 to 43 us a dispatch in 0.4.4 beta 17's
// profile. The results are the same: the counter moves by the active lanes, and each lane gets
// the value a lane-by-lane order would have given it.
#pragma once

#include <cstdlib>
#include <cstring>

namespace Shader {

/// BB_GDS_APPEND=lane: one atomic for each lane, as before 0.4.4 beta 19.
inline bool GdsAppendPerSubgroup() {
    static const bool on = [] {
        const char* env = std::getenv("BB_GDS_APPEND");
        return !(env && std::strcmp(env, "lane") == 0);
    }();
    return on;
}

} // namespace Shader

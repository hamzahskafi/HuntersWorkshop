// SPDX-License-Identifier: GPL-3.0-or-later
// The game's GX range allocator's region, taken from its init (gx_region.cpp, bbport_gx_ring.h).
#pragma once
#include <cstddef>
#include <cstdint>

namespace BbEngine::GxRegion {
/// Loader start, after the executable is verified. Commits its own hook. BB_GX_RING_STREAM_KB=0
/// leaves it out (the region then stays unknown and is tracked as before).
bool Install(std::uint8_t* image, std::size_t size);
} // namespace BbEngine::GxRegion

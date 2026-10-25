// SPDX-License-Identifier: GPL-3.0-or-later
// Fixes for the game's own bugs, in its code (game_fixes.cpp, job_pool.cpp, fmod_ring.cpp).
#pragma once
#include <cstddef>
#include <cstdint>

namespace BbEngine::GameFixes {
/// Loader start, after the executable is verified. Commits its own hooks. BB_GAME_FIXES=0 leaves
/// them all out, BB_GAME_FIXES_SKIP the ones it names (skip_list.h): job_lock, job_nodes,
/// job_slots, blur_samples, fmod_ring, and deflate (the start program's: scripts/patches.py).
bool Install(std::uint8_t* image, std::size_t size);
} // namespace BbEngine::GameFixes

// SPDX-License-Identifier: GPL-3.0-or-later
// Hunter's Workshop 0.4.5 beta 15: the game's own GX resource calls (research; nothing acts on them).
// Bloodborne's graphics layer creates every texture and buffer through GX creators and destroys them
// through their holders' destructors; bbhost ties its images to these calls (credited in README). Here
// the engine only listens and counts, and the texture cache compares them with what it sees
// (TextureCache: an image registered at a texture's memory, an image still there once the game
// destroyed its texture), so beta 16 can act on them with evidence.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace BbEngine::GxResources {
// At loader startup, beside the other engine hooks (1.09 only: each site's bytes are checked).
bool Install(std::uint8_t* image, std::size_t size);
// Whether the hooks are in place.
bool Active();

// A texture the game destroyed: its memory and size, and when (steady clock, ns).
struct Gone {
    std::uint64_t memory;
    std::uint32_t bytes;
    std::uint64_t ns;
};
// The textures destroyed since the last call (the texture cache's collector, once a second).
void TakeDestroyed(std::vector<Gone>& out);

// At an image's registration: whether a live game texture starts at `memory`, how long ago the game
// created it, and whether it was created during a loading screen.
struct Made {
    bool found;
    std::uint64_t age_ns;
    bool during_loading;
};
Made TextureAt(std::uint64_t memory);

// The texture cache's side of the comparison (counted here, reported with the rest).
void NoteImage(const Made& made);
void NoteDestroyedImage(bool had_image, std::uint64_t image_bytes);
void NoteDeadInVram(std::uint64_t images, std::uint64_t bytes);

// One line a minute (driver.cpp, with the VRAM manager's).
void LogMinute();
} // namespace BbEngine::GxResources

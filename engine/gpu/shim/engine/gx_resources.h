// SPDX-License-Identifier: GPL-3.0-or-later
// Hunter's Workshop 0.4.5 beta 15-16: the game's own GX resource calls, which the texture cache follows.
// Bloodborne's graphics layer creates every texture and buffer through GX creators and destroys them
// through their holders' destructors; bbhost ties its images to these calls (credited in README).
//   Beta 15 listened and counted (on 2026-11-07, 17 minutes with 21 loading screens: 92% of the
//   images the texture cache made sat at a live game texture, 53% of them textures created during a
//   loading screen, built in the first frame after it: that frame's 44-59 ms).
//   Beta 16 follows them (BB_GX_FOLLOW=0: counted only, as beta 15):
//     create   a texture created with its data: the T# GX built for it (its builder runs inside the
//              creator) is queued (TakeAhead), and the texture cache builds, uploads and watches the
//              image during the loading screen (or in play while frames have slack), below the VRAM
//              manager's comfortable line (0.4.6 beta 3, BB_VRAM_MANAGER=0: at any level; an image
//              of a texture destroyed there goes first). A draw then finds it ready.
//     destroy  the game is done with the texture: on the game's thread the texture cache stops
//              watching its pages (DestroyedHook: the next texture made there writes them without a
//              fault each), and the collector frees its image once the GPU is done with it.
//   Research: buffers released against the game's data in VRAM, and the game's shader creators.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace BbEngine::GxResources {
// At loader startup, beside the other engine hooks (1.09 only: each site's bytes are checked).
bool Install(std::uint8_t* image, std::size_t size);
// Whether the hooks are in place.
bool Active();
// Whether the texture cache follows the game's creates and destroys (BB_GX_FOLLOW=0: no).
bool Following();

// A texture the game destroyed, or a buffer it released: its memory and size, and when (ns).
struct Gone {
    std::uint64_t memory;
    std::uint32_t bytes;
    std::uint64_t ns;
};
// The textures destroyed since the last call (the texture cache's collector, once a second).
void TakeDestroyed(std::vector<Gone>& out);
// The buffers released since the last call (the buffer cache, once a second; research).
void TakeReleased(std::vector<Gone>& out);

// A texture created with its data: the T# its GX layer built (the base filled in), its memory, size
// and creation time (ns), and whether a loading screen was up.
struct Ahead {
    std::array<std::uint32_t, 8> tsharp;
    std::uint64_t memory;
    std::uint32_t bytes;
    std::uint64_t ns;
    bool loading;
};
// The textures queued to be built ahead since the last call (the texture cache's collector).
void TakeAhead(std::vector<Ahead>& out);
// Whether the texture created at `memory` at `ns` is still the live one there.
bool StillLive(std::uint64_t memory, std::uint64_t ns);
// Whether any live texture of the game's overlaps [begin, end) (beta 19).
bool LiveTextureOverlaps(std::uint64_t begin, std::uint64_t end);

// Called on the game's thread when it destroys a texture (Following only).
using DestroyedHook = void (*)(std::uint64_t memory, std::uint64_t bytes);
void SetDestroyedHook(DestroyedHook hook);

// At an image's registration: whether a live game texture starts at `memory`, how long ago the game
// created it, and whether it was created during a loading screen.
struct Made {
    bool found;
    std::uint64_t age_ns;
    bool during_loading;
};
Made TextureAt(std::uint64_t memory);

// The caches' side (counted here, reported with the rest).
void NoteImage(const Made& made);
void NoteDestroyedImage(bool had_image, std::uint64_t image_bytes);
void NoteDeadInVram(std::uint64_t images, std::uint64_t bytes);
void NoteDeadFreed(std::uint64_t bytes);
enum class AheadResult { Built, Existed, Gone, TooOld, Invalid };
void NoteAhead(AheadResult result, std::uint64_t bytes);
void NoteAheadHit();
void NoteAheadMiss();
void NoteAheadUnused(std::uint64_t bytes);
void NoteAheadWaiting(std::uint64_t jobs);
void NoteReleasedBuffer(std::uint64_t resident_bytes);

// Two or three lines a minute (driver.cpp, with the VRAM manager's).
void LogMinute();
} // namespace BbEngine::GxResources

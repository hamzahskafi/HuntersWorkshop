// SPDX-License-Identifier: GPL-3.0-or-later
// Hunter's Workshop 0.4.5 beta 15-16: the game's own GX resource calls, which the texture cache follows.
// Bloodborne's graphics layer creates every texture and buffer through GX creators and destroys them
// through their holders' destructors; bbhost ties its images to these calls (credited in README).
//   Beta 15 listened and counted (on 2026-11-07, 17 minutes with 21 loading screens: 92% of the
//   images the texture cache made sat at a live game texture, 53% of them textures created during a
//   loading screen, built in the first frame after it: that frame's 44-59 ms).
//   Beta 16 follows them (BB_GX_FOLLOW=0: counted only, as beta 15):
//     create   a texture created with its data: the T# GX built for it (its builder runs inside the
//              creator) goes to the texture cache, which builds, uploads and watches the image ahead
//              of its first draw, below the VRAM manager's comfortable line (0.4.6 beta 3,
//              the manager off, beta 5's default: at any level; an image of a texture destroyed there goes first).
//              A draw then finds it ready.
//     destroy  the game is done with the texture: on the game's thread the texture cache stops
//              watching its pages (DestroyedHook: the next texture made there writes them without a
//              fault each), and the collector frees its image once the GPU is done with it.
//   0.4.6 beta 4 follows them as the game does them, with no time budget:
//     create   the creating thread (one of the game's loaders) prepares the texture's image (its
//              Vulkan image and memory) inside the game's create call (CreatedHook); the game's main
//              thread hands that to one worker instead. The GPU thread registers what was prepared
//              at its next submission and records the upload.
//     release  the game's data in VRAM leaves with the buffers the game releases (buffer cache).
//   0.4.6 beta 5 manages the game's memory as the PS4 does:
//     create   any resource made (buffers too) is the memory's owner now: images left there from
//              before are retired (TakeCreated, the texture cache's ProcessGxCreated).
//     views    render-target and depth views mark their textures (Classify, the research lines).
//     1D       1D textures and their destructors, keyed by memory.
//     release  frees nothing: the game's data stays in VRAM until the game unmaps the memory.
//   (Beta 5 also followed Map: only buffers are mapped, each into a renamed copy from GX's pool, never
//   a texture; beta 6 leaves it alone.)
//   0.4.6 beta 6 follows the game's textures that are not GX's (as bbhost does): YEBIS's 2D textures
//   (the post-processing middleware's own device: its targets were ~1.1 GB of beta 5's stale images)
//   and Scaleform's texture planes with memory of their own; and the game unmapping memory under
//   resources it keeps (MemoryReleased: they hold none from then on).
//   Research: the game's shader creators.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
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
// The textures destroyed since the last call (the texture cache's collector; 0.4.6 beta 4: every
// pass while DestroyedWaiting, which reads no lock).
void TakeDestroyed(std::vector<Gone>& out);
bool DestroyedWaiting();
// 0.4.6 beta 5: the resources made since the last call (memory, size, when): that memory is theirs
// now, so an image left there from before is no one's (the texture cache retires it).
void TakeCreated(std::vector<Gone>& out);
bool CreatedWaiting();

// 0.4.6 beta 5: what of the game's owns a range of memory (the live resource over it, textures first).
enum class Owner : std::uint8_t {
    None,          // no live resource of the game's
    Buffer,        // a buffer
    TextureData,   // a 2D texture made with its data
    TextureNoData, // a 2D texture made without (filled later, by the CPU or the GPU)
    RenderTarget,  // a texture with a render-target view
    DepthTarget,   // a texture with a depth-stencil view
    Texture3D,
    Texture1D,
    Yebis,         // 0.4.6 beta 6: a YEBIS 2D texture (post-processing)
    Scaleform,     // a Scaleform texture plane (the UI)
    kCount
};
const char* OwnerName(Owner owner);
// `at_start`: whether the resource begins where the range does.
Owner Classify(std::uint64_t begin, std::uint64_t end, bool* at_start = nullptr);
// An image made at a draw (not prepared ahead), by its owner; running totals for the hitch reports.
void NoteDrawMade(Owner owner, std::uint64_t bytes);
std::uint64_t DrawMadeTotal(Owner owner);
// Once a minute: the stale images (written over, unused 10 s) by owner, of them drawn by the GPU, and
// the largest few described.
void NoteStaleByOwner(const std::array<std::uint64_t, std::size_t(Owner::kCount)>& bytes,
                      const std::array<std::uint64_t, std::size_t(Owner::kCount)>& images, std::uint64_t gpu_bytes,
                      const char* largest);
// Images retired because the game made something newer over their memory.
void NoteReuseDead(std::uint64_t images, std::uint64_t bytes);
// 0.4.6 beta 6: an image a draw made over memory no resource of the game's holds (the minute's
// largest are described, with the mapping each lies in).
void NoteDrawMadeNone(std::uint64_t address, std::uint64_t bytes, std::uint32_t width, std::uint32_t height,
                      std::uint32_t levels, std::uint32_t layers, std::uint32_t format, std::uint32_t tile);
// A stale image the GPU drew, freed because the game's memory under it was written since (its bytes),
// and the memory hashed to tell.
void NoteSuperseded(std::uint64_t bytes);
void NoteSupersededHashed(std::uint64_t bytes);
// The game unmapped [address, address + size): the live resources there hold no memory from now on
// (the game releases an area's texture memory under holders it keeps, bbhost found). Any thread.
void MemoryReleased(std::uint64_t address, std::uint64_t size);
// The buffers released since the last call (the buffer cache; 0.4.6 beta 4: every pass while
// ReleasedWaiting, which reads no lock).
void TakeReleased(std::vector<Gone>& out);
bool ReleasedWaiting();

// A texture created with its data: the T# its GX layer built (the base filled in), its memory, size
// and creation time (ns), and whether a loading screen was up.
struct Ahead {
    std::array<std::uint32_t, 8> tsharp;
    std::uint64_t memory;
    std::uint32_t bytes;
    std::uint64_t ns;
    bool loading;
};
// 0.4.6 beta 4: called inside the game's create call, on its thread (or the worker's for the game's
// main thread), for each texture created with its data (Following only).
using CreatedHook = void (*)(const Ahead& job);
void SetCreatedHook(CreatedHook hook);
// Whether the texture created at `memory` at `ns` is still the live one there.
bool StillLive(std::uint64_t memory, std::uint64_t ns);
// Whether any live texture of the game's overlaps [begin, end) (beta 19); one starting at `skip_at`
// (non-zero) is left out (0.4.6 beta 4: the texture being built there).
bool LiveTextureOverlaps(std::uint64_t begin, std::uint64_t end, std::uint64_t skip_at = 0);

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
// 0.4.6 beta 4: Refreshed, the same texture again over its live image (refreshed from memory, no
// new image); NoMemory, VRAM refused its image; NoRoom, over the VRAM manager's comfortable line
// (the manager on); NoRenderer, before the renderer was up. Those three are built at their first draw.
enum class AheadResult { Built, Existed, Gone, TooOld, Invalid, Refreshed, NoMemory, NoRoom, NoRenderer };
void NoteAhead(AheadResult result, std::uint64_t bytes);
// 0.4.6 beta 4: why a texture was left to its draw (Existed): the image there belongs to a texture
// the game has not destroyed, was used since its texture was destroyed, or the GPU drew it under
// another live texture.
enum class Occupant { NotDestroyed, UsedSince, GpuDrawn };
void NoteOccupant(Occupant why);
// 0.4.6 beta 4: one GPU-thread pass registering prepared textures (time and images), for the minute
// line and the hitch reports (BuildPassTotals: running totals).
void NoteBuildPass(std::uint64_t ns, std::uint64_t images);
void BuildPassTotals(std::uint64_t& ns, std::uint64_t& images);
// 0.4.6 beta 4: stale images (written over, unused 10 s) by whether a live game texture is over them,
// and those freed for having none.
void NoteStale(std::uint64_t with_texture_bytes, std::uint64_t without_texture_bytes);
void NoteOrphanFreed(std::uint64_t bytes);
void NoteAheadHit();
void NoteAheadMiss();
void NoteAheadUnused(std::uint64_t bytes);
void NoteAheadWaiting(std::uint64_t jobs);
void NoteReleasedBuffer(std::uint64_t resident_bytes);

// Two or three lines a minute (driver.cpp, with the VRAM manager's).
void LogMinute();
} // namespace BbEngine::GxResources

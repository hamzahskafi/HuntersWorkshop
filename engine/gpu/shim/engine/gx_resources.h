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
//   0.4.6 beta 7: YEBIS's re-created targets keep the images of the ones they replace (same memory and
//   shape; bbhost's), and GX textures made without their data that are sampled only get their image
//   at creation too (its upload is the first draw's).
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
    // 0.4.6 beta 7: a YEBIS target made without its data (`hand_over`, created events): an image of the
    // same shape at the same memory, left by the target it replaces, becomes this one's. A YEBIS
    // texture destroyed (`grace`, destroyed events): its images wait a few seconds for that.
    std::uint32_t width = 0, height = 0;
    std::uint16_t levels = 0, layers = 0;
    bool hand_over = false, grace = false;
    // 0.4.6 beta 10: a texture made with its data (created events): its create call copied that data
    // into the memory, a write the images there are told of (whether their pages are watched or not).
    bool with_data = false;
    // 0.4.6 beta 11: the resource's kind (Owner, created events), and for a GX 2D texture made without
    // its data the shape from its T# (`hand_over` then too: BB_IMAGE_OWNERS, the texture cache).
    std::uint8_t owner = 0;
    // 0.4.6 beta 16, step 2: YEBIS took this memory in its heap (written events, from its allocators):
    // only the images over no game resource that step 2 leaves to the calls there are told (their data
    // is written after the allocation, the heap's header before it: the write page protection caught).
    bool yebis_heap = false;
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
// 0.4.6 beta 16: the live resource over [begin, end) as Classify finds it, with whether the game made it
// with its data (YEBIS's and Scaleform's as GX's), its memory and its bytes.
struct Resource {
    Owner owner = Owner::None;
    bool at_start = false, with_data = false;
    std::uint64_t memory = 0, bytes = 0;
};
Resource Describe(std::uint64_t begin, std::uint64_t end);
// 0.4.6 beta 16, step 2: whether [begin, end) lies in YEBIS's heap (the heap in the game's graphics object
// that the game's allocation callback gives YEBIS its memory from; read from there, 1.09 only), with
// YEBIS's allocations followed (they tell when YEBIS takes memory there again; BB_YEBIS_ALLOCS=0: never).
// Every faulting image over no game resource in beta 16's runs lay in it, the heap's bookkeeping and
// YEBIS's other memory beside it written.
bool InYebisHeap(std::uint64_t begin, std::uint64_t end);
// The images such a written event marked (the texture cache, ProcessWritten), for the minute line.
void NoteYebisHeapRetaken(std::uint64_t images);
// Such an image over no game resource at its registration (the texture cache): counted by who asked for
// the YEBIS allocation under it (the minute line "YEBIS's allocations"), the first 16 named, with whether
// it is left to the calls (watch_rules.h; BB_WATCH_YEBIS_HEAP=1 watches it).
void NoteYebisHeapImage(std::uint64_t memory, std::uint64_t bytes, std::uint32_t width, std::uint32_t height,
                        std::uint32_t format, bool left_to_calls);
// 0.4.6 beta 16 part 4: a write fault on an image's pages (the texture cache, the faulting thread): the
// image's owner kind, whether the write landed inside the image, and whether the image reaches past the
// bytes of the resource starting at it; the first 16 images of the session are named in the log.
struct WatchedImage {
    Owner owner = Owner::None;
    std::uint64_t fault = 0, memory = 0, bytes = 0;
    std::uint32_t width = 0, height = 0, format = 0;
    bool whole = false, with_data = false, surface = false, inside = false, past = false;
};
void NoteImageWatchFault(const WatchedImage& image);
// An image the calls covered, handed to a texture they do not cover and watched again (beta 16).
void NoteHandOverWatched();
// The texture cache's images by owner kind, once a minute: watched by page protection, and left to the
// calls (count and bytes each).
struct WatchCensus {
    std::array<std::uint64_t, std::size_t(Owner::kCount)> watched{}, watched_bytes{}, by_calls{}, by_calls_bytes{};
};
void NoteWatchCensus(const WatchCensus& census);
// Part 3, the texture net: images hashed in its slice of the second, and a change no call told (the
// image marked written).
void NoteTextureNet(std::uint64_t images, std::uint64_t bytes);
// A lane's round (0 the middleware's, 1 GX's): how long it took, over how many images and bytes.
void NoteTextureNetRound(unsigned lane, std::uint64_t seconds, std::uint64_t images, std::uint64_t bytes);
void NoteTextureUntold(Owner owner, std::uint64_t memory, std::uint64_t bytes, std::uint32_t width,
                       std::uint32_t height);
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
// 0.4.6 beta 7: YEBIS's re-created targets that kept the images of the ones they replaced.
void NoteHandedOver(std::uint64_t images, std::uint64_t bytes);
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
    // 0.4.6 beta 7: false for a texture made without its data (sampled only: no render-target,
    // depth or shader-write binding): registered without an upload or a watch, its first draw does both.
    bool upload = true;
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

// 0.4.6 beta 8 (part 1): the live resource that starts at `begin` and covers [begin, end), when the game
// made it without its data for the GPU to write (GX: a render-target, depth or shader-write binding, or
// a render-target or depth view; YEBIS: a render-target, depth or shader-write binding). Such a surface
// never takes the game's memory: it starts from its predecessor's image or empty. `ns` is its creation
// time (StillLive tells later whether it is still the one there). Following only.
struct Surface {
    bool found = false;
    Owner owner = Owner::None;
    std::uint64_t ns = 0;
};
Surface SurfaceAt(std::uint64_t begin, std::uint64_t end);
// How a surface's first use began (the texture cache, RefreshImage).
enum class SurfaceStart : std::uint8_t {
    Empty,       // cleared (colour to zero)
    DepthEmpty,  // a depth target cleared (depth 1, stencil 0), no game clear pending in its HTILE
    DepthCleared, // the same, with the game's clear pending (it lands on top)
    Taken,       // the GPU had written its memory as a buffer since it was made: filled from that
    Gone,        // its resource was no longer the live one there: filled from memory as before
    TakenEarlier,   // 0.4.6 beta 9: the GPU's writes there are older than the resource: filled from them
    EarlierSkipped, // the same left out (BB_TAKE_EARLIER_WRITES=0): started empty
    kCount
};
void NoteSurfaceStart(Owner owner, SurfaceStart how, std::uint64_t bytes);
// An image's first fill from the game's memory (not a surface started empty), by the resource there.
void NoteFirstFill(Owner owner, std::uint64_t bytes);
// A CPU write fault that landed on an image, by the live resource at its memory (part 5's measurement);
// `kept`: a surface whose resource lives, its contents kept (counted, not uploaded). The largest few a
// minute are described.
void NoteCpuWrite(Owner owner, bool kept, std::uint64_t address, std::uint64_t bytes, std::uint32_t width,
                  std::uint32_t height);
// The same for the game's buffers (the buffer cache's write faults on a live GX buffer's memory).
void NoteBufferCpuWrite(std::uint64_t address, std::uint64_t bytes);
// 0.4.6 beta 8 (part 3): a new surface took a dead target's Vulkan image of its shape, or found none.
enum class PoolEvent : std::uint8_t { Taken, NoShape, kCount };
void NotePool(PoolEvent event, std::uint64_t bytes);
// 0.4.6 beta 8 (part 4): images whose pages were left unwatched (the game's calls tell their writes).
void NoteUnwatched(std::uint64_t images, std::uint64_t bytes);
// 0.4.6 beta 9: the textures the CPU wrote through their own calls since the last call: YEBIS's
// UpdateSubresource and Unmap of a 2D texture, Scaleform's unmapTexture of a plane with memory of
// its own (memory and size). The texture cache marks the images over them written (CPU-dirty).
void TakeWritten(std::vector<Gone>& out);
bool WrittenWaiting();
// 0.4.6 beta 9 (research): the fault being handled on this thread (bbgpu_handle_fault sets it, and
// clears it with null), and a CPU write that landed on a texture made with its data: the first few
// are logged with the guest code that wrote (its instruction and callers, image offsets).
void SetFaultContext(void* ucontext);
void NoteDataTextureWrite(std::uint64_t address, std::uint64_t bytes, std::uint32_t width, std::uint32_t height);
// 0.4.6 beta 10: images marked written because a texture was made with its data over them, and live
// surfaces reaching past it left as they are (their CPU writes are kept, beta 8).
void NoteCreationWrites(std::uint64_t images, std::uint64_t bytes, std::uint64_t surfaces_kept);
// 0.4.6 beta 10 (the owner rule, counted only): a draw or bind took an image whose texture is gone
// (destroyed, or something newer made over it). `live` is what lives at its memory now: whether a
// texture of the game's does, made without its data and as large (the owner rule would hand the
// image over), or otherwise (it would refuse the image and make a new one).
struct LiveHere {
    bool found = false;
    bool without_data = false;
    std::uint64_t bytes = 0;
};
LiveHere LiveAt(std::uint64_t memory);
void NoteDeadImageTaken(bool destroyed, bool hand_over, bool refuse, std::uint64_t bytes);
// 0.4.6 beta 12 (BB_WATCH_GAME_BUFFERS): the game's calls about memory, for the buffer cache, on the
// game's thread outside the registry's lock: a GX buffer made (with its data or not) or released, a
// texture of the game's destroyed, and a live GX buffer's memory a new resource was made over.
enum class MemoryEvent : std::uint8_t { BufferMade, BufferMadeWithData, BufferGone, TextureGone, Covered };
using MemoryHook = void (*)(std::uint64_t address, std::uint64_t size, MemoryEvent event);
void SetMemoryHook(MemoryHook hook);
// 0.4.6 beta 15 part 4 (BB_COPY_NOTICE=0: off; docs/memory-model.md, beta 15 part 4): the creators copy
// the caller's data with the game's memcpy (0x261a1b0), CreateBuffer's helper at 0x216d5ea and
// CreateTexture's at 0x2171fea. A cave at each tells the hook the destination and size first, on the
// game's thread, then makes the copy: its pages need not fault one by one.
using CopyHook = void (*)(std::uint64_t address, std::uint64_t size);
void SetCopyHook(CopyHook hook);
// The caves: copy_cave.h.
// A write the engine was told of (an asset or DMA write) over live GX buffers' memory: counted on them
// for the buffer cache's net (SampleBuffers).
void NoteToldWrite(std::uint64_t address, std::uint64_t bytes);
// Up to `max_bytes` of live GX buffers of a page or more, in turn from where the last call stopped.
struct BufferSample {
    std::uint64_t key = 0, memory = 0, ns = 0;
    std::uint32_t bytes = 0, told = 0;
};
void SampleBuffers(std::vector<BufferSample>& out, std::uint64_t max_bytes);
// The buffer cache's side: pages kept writable for the calls (a change), and the net's pass (buffers
// and bytes hashed, CPU writes found that no call told, the first ones described).
void NoteCallsPages(std::int64_t delta);
void NoteNet(std::uint64_t buffers, std::uint64_t bytes);
void NoteUntoldWrite(std::uint64_t address, std::uint64_t bytes, std::uint64_t key);
// 0.4.6 beta 13: a CPU write fault at `address` and what watched its page (bits as
// PageManager::NoteFaultWatch: 1 the GPU's data, 2 the buffer cache's uploaded copy, 4 an image, 8
// none), counted by whose memory it is (none, a live buffer's inside page, a page at a live buffer's
// edge, a page beside a live resource, a live texture by its owner) for the minute line.
void NoteWriteFault(std::uint64_t address, unsigned watched);
// 0.4.6 beta 11 (the owner rule): images handed to a texture of the game's other than YEBIS's made over
// their memory without data, with their shape; images retired that the old guesses kept, and of them
// those a new surface took as its Vulkan image.
void NoteOwnerHandOver(std::uint64_t images, std::uint64_t bytes);
void NoteOwnerRetired(std::uint64_t bytes, bool pooled);
// Where today's rules guess and the owner rule would retire instead: a dead image kept because another
// live texture overlaps it, or because it was used just before its memory's new resource was made; and
// a stale image the collector freed that had an owner (the owner rule retires it when its texture goes).
enum class OwnerGuess : std::uint8_t { KeptOverlap, KeptUsedBefore, StaleOwned, kCount };
void NoteOwnerGuess(OwnerGuess guess, std::uint64_t bytes);
// A copy the copy shader runs through memory (not image to image): whether its source and its
// destination are images of a game's texture, at its start.
void NoteCopyThroughMemory(bool source_owned, bool destination_owned);
// 0.4.6 beta 8 (part 5): a shader translated for a draw, by stage (0 vertex, 1 pixel, 2 compute, 3
// other), its time, and whether its program had been translated before in this session (another
// variant of it).
void NoteShaderTranslated(unsigned stage, std::uint64_t ns, bool program_seen);

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

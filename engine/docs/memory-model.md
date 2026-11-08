# The memory model: 0.4.6

0.4.6 is the memory model release. This document is its design, the research behind it and the
roadmap, beta by beta, with what each beta must show before the next one starts. Written
2026-11-08, after 0.4.6 beta 7's logs. The short form of the roadmap and every other open item are
in [plan.md](plan.md); what each beta changed and measured is in [changelog.md](changelog.md).

Addresses here are **image offsets** of the 1.09 executable (what HW's `Prepare()` calls and the
sampler print). bbhost's and Binary Ninja's patch addresses are these plus 0x400000.

## The goal

The engine moves away from the generic emulator model (the tester, 2026-11-08: "which we are
trying to move away from in order to have a fulfilling experience and optimal engine"). In that
model the game's memory is the source of truth for everything: an image is filled from memory when
first used, hashed to see whether memory changed, and its pages are write-protected to catch the
CPU writing them. The game's own calls already say all of that, as they did to the PS4's driver:

- **What exists:** the game creates and destroys its textures and buffers through GX, YEBIS (the
  post-processing middleware, its own D3D11-shaped device) and Scaleform (the UI).
- **What a resource starts with:** made with data, its texels; made without data, nothing the CPU
  wrote (a render target, a depth target, a texture shaders write).
- **What changes it:** the GPU's draws, copies and shaders; the CPU only through calls (GX's Map,
  YEBIS's Map and update, Scaleform's texture unmap).

So, the rules 0.4.6 ends with:

1. **An image belongs to the game resource it was made for.** Destroying the resource retires the
   image once the GPU is done with it. The image passes to the next resource at that memory only
   when that resource was made without data (on the console the memory carries over).
2. **A GPU-owned surface never reads the game's memory.** It starts from its predecessor's image
   if it has one, otherwise empty.
3. **Writes are learned from the calls.** Pages are write-protected only where no call tells: the
   engine's own data rings and YEBIS's buffers.
4. **Buffers the same way:** GX's create and Map calls say which vertex and index data changed.
5. **Shaders are translated when the game creates them,** for the variants its calls predict, and
   pipelines are built during loading screens, not at a draw in play.

What stays from the generic model, on purpose: detiling textures made with data (their texels are
in the console's tiled layout), translating the game's shaders, and replaying the game's command
stream (see "After 0.4.6").

## Where it stands after beta 7

Already following the game's calls (betas 4 to 7; [plan.md](plan.md)'s history is in
[archive/plan-0.4.5.md](archive/plan-0.4.5.md)):

- **The registry** (`gx_resources.cpp`): GX's creators, destructors, render-target and depth
  views, 1D textures; YEBIS's 2D textures from its creator and destructor; Scaleform's planes with
  memory of their own from Initialize and ReleaseHWTextures; memory the game unmaps.
- **Textures made with their data** are built in the game's own create call; destroys and
  releases freed as soon as the GPU is done; a release frees nothing of the game's data.
- **YEBIS's re-created targets** keep the old image when memory and shape match (beta 7); sampled
  textures made without data get their image at creation (beta 7).
- **The VRAM manager is off by default** (beta 5); the game's own calls decide.

Still the generic model (the gaps the 2026-11-08 research found):

| Gap | Cost seen | Where it goes |
|---|---|---|
| A new render target, depth target or shader-written texture is filled from the game's memory at its first draw | YEBIS's rebuilds: 59-84 ms stalls (hashing 28.8 ms, detiling 13.6 ms, page faults); reading untouched memory also commits its pages | Beta 8, part 1 |
| A GPU-written image's first fill hashes every level: the hashes tell a real CPU write from one elsewhere in the same 4 KiB page (not a bug, as first thought: beta 8's correction) | The 28.8 ms of hashing above | Beta 8, part 2: none for surfaces started empty |
| Every new image's pages are write-protected (`texture_cache.cpp`, `TrackImage` after `InsertImage`), targets included | About 8 ms of protecting a rebuild, faults when memory is reused | Beta 8 part 4 (measured), beta 9 |
| Images found by address and shape; a stack of guesses decides whose an image is (retired when something newer is made over it, dead-by-reuse, the 3 s grace, the same-memory hand-over, hashing 32-37 MiB of memory a minute to tell overwritten drawings) | Rebuilt targets mostly miss the hand-over (8-36 kept a minute against about 280 a rebuild); stale images; CPU time | Beta 10 (counted), beta 11 (built) |
| Every rebuilt target is a new Vulkan image, and the old set waits out the grace | About 4 ms of allocation a rebuild, both sets in VRAM for seconds (YEBIS made 744-775 MiB a minute in beta 7's log) | Beta 8, part 3 |
| A depth target's first use reads memory; its compression metadata (HTILE) is followed by address | Part of the rebuilds' uploads | Beta 8 part 1, beta 13 |
| The game's buffers are write-protected and re-read by page | Write faults and re-uploads | Beta 12 |
| Shaders translated at a draw; pipelines a new area needs built in play | Compile stutter with a cold cache | Beta 14 |

## The research of 2026-11-08

### YEBIS in 1.09

- **Its device** is a D3D11-shaped table at `0x5308810`; slot 5 (CreateTexture2D) is a thunk at
  `0x2a39340` that jumps to the creator `0x11df570`. Its context's table is `0x530a380` (133
  methods; slot 50, ClearRenderTargetView, `0x2a41320`, from Lantern's `GX_NATIVE_PLAN.md`).
- **Its memory comes from the game, not from GX:** the creator's helper `0x11df840` takes memory
  from `0x11e6810` (no initial data) or `0x11e6700`; both call an allocator the game hands to YEBIS
  (the pointer at `0x54b9f38`, set through `0x11e61f0` from YEBIS's context setup `0xbb4b40`, out of
  its parameters). Where a target lands is that allocator's choice.
- **Its texture pool** (`CRenderTexturePool::AllocateTextures`, `0xc119f0`) shares a pooled texture
  only when width, height and format match; otherwise it makes a new one. The pool lives in a
  context.
- **A rebuild is a new context:** the game's YEBIS setup `0x21d2dd0` allocates a new context
  (`0xbb4ac0`) and initializes it (`0xbb4b40`, which calls `IPfxBaseContext::Initialize`
  `0xbb1c20`). It is reached from five virtual methods (`0x21d2da0`, `0x21d9df0`, `0x2272a30`,
  `0x229c200`, `0x22c53e0`); `0x2272a30` builds it only when its owner has none (owner + `0x3160`).
  Every target of a new context is new. That fits the tester's observation (2026-11-08): the stall
  comes crossing from one part of an area into the next, as the earlier part is torn down.
- **Not established yet:** whether the old context is freed before or after the new one is built
  (beta 8's logging), and which owner objects the five methods belong to.
- **Beta 7's log** (`028674f3`, 2026-11-08): YEBIS made 580-599 2D textures a minute (about 530
  render targets, 12 depth) and destroyed 422-606; about two rebuilds a minute while walking.

### The other ports

- **bbhost** (`src/host/render.cpp`, `rt_image`): render targets are images of their own, keyed by
  base address, and a new one is never filled from memory. A target at the same base with the same
  format and size is the same image (contents kept); in another format of the same texel size the
  pixels are copied over (the blood layers move between sRGB and UNORM); with more layers they
  grow, layers copied; a texture a shader stored there first is taken; one re-created after a fill
  its old image took starts with that fill; otherwise it is cleared to zero (`BBHOST_RT_ZERO`, on
  since 2026-10-01: a glare level read past its drawn part and kept values near the format's
  maximum and NaNs). Depth targets are not cleared; the game's clear does it. Images belong to
  resource generations (`docs/rendering.md`); a dead target's image goes to a resource made over
  its memory without data (`gx_successor_gen`, `target_taken_by`, `src/host/textures.cpp`); only
  memory no call tells of is write-protected. bbhost knows YEBIS's textures from its creator,
  destructor, Map, update and unmap (`src/engine/gx_resources.cpp`; "~400 a pass" taken over).
- **bbport** (`gpu/textures/texture_cache.cpp`, `FindRenderTarget`; `texture_cache_upload.cpp`,
  `RefreshImage`) and **Lantern Engine** (`engine/gpu/shadps4/video_core/texture_cache`): a target's
  first use fills it from memory, as HW does. Neither handles YEBIS's rebuilds. Lantern's
  `engine/docs/GX_NATIVE_PLAN.md` and `NATIVE_ENGINE_PLAN.md` plan GX's calls as host work, phase
  by phase (YEBIS and Scaleform last, not started).
- **What the console does:** a new target's memory holds whatever was there; YEBIS draws or clears
  before it reads. Filling from memory gives neither the console's bits (the GPU's drawings never
  go back to memory) nor anything YEBIS uses.

### The first-fill hashes (corrected while building beta 8)

`TextureCache::RegisterImage` sets `skip_hashes_once`; `RefreshImage` honours it only while the
image is not marked GPU-modified (`texture_cache.cpp`, `skip_hashes`). `FindRenderTarget` and
`FindDepthTarget` mark the image GPU-modified before `UpdateImage`, so a target's first fill hashes
every level. The research called that a bug; it is not. Under the generic model the hashes are what
protect a GPU-written image: a CPU write anywhere in one of its 4 KiB pages marks it written, and the
next use compares its memory with the hashes before uploading over what the GPU drew (0.4.5's black
character-creator preview was that upload). What makes them unneeded is the model itself: a surface
started empty takes none, and a CPU write in its pages while its resource lives is counted, its
contents kept (beta 8, part 2).

### Shaders

- **The game creates all of its shaders at startup** and none at loading screens (beta 7's log:
  vertex 10,000 + 410, pixel 2,798, compute 69 in the first minute, 0 afterwards; 13,277 in 0.4.5
  beta 17's count).
- **The pipeline cache builds what it knows at the start** (815 of 822 in 0.4 s in beta 7's log).
  With a cold cache, areas never seen compile in play (`pipeline_compiles.md`).
- **What stopped translating at creation** (0.4.5 beta 17): the variants a draw needs depend on its
  state (vertex formats, target formats), unknown when the shader is created. The game's calls may
  give that state ahead: GX takes the vertex formats in its input-layout creation, as D3D11 does
  (bbhost records shader and input-layout objects at creation), and target formats are known when
  targets are created. Beta 8 measures how much that would predict.

## The roadmap

The betas after 9 were renumbered on 2026-11-08 when beta 10 became the measurement before the owner
rule (the design check below): the owner rule is beta 11, depth buffers 12, the game's buffers 13,
shaders 14. Then, with beta 11 (the tester's call: the only cost left in beta 10's logs is the game's
buffers), the game's buffers became 12 and depth buffers 13. Earlier text that named them by their old
numbers has been brought up to date.

One log must be able to tell each change apart: every part has its own switch in Extra variables
that brings back the old behaviour, named in the changelog when built. A beta's switches go at the
release, with the tester's agreement.

### Beta 8: GPU-owned surfaces never read the game's memory; the measurements

Built 2026-11-08 (engine id 92). Safe mode turns parts 1 and 3 off.

1. **New GPU-owned surfaces start from their predecessor or empty** (on by default;
   `BB_EMPTY_SURFACES=0` fills them from memory as before). An image
   made at a draw for memory that belongs to a game resource made without data (a GX texture
   without initial data whose bind flags say render target, depth or shader writes; a YEBIS
   texture made without data) and with nothing to carry (beta 7's hand-over, an image copied in by
   overlap, a texture a shader stored there first) is not filled from memory:
   - **Colour targets and shader-written textures:** cleared to zero (bbhost's rule).
   - **Depth targets:** not filled either; the first use takes the game's pending clear from its
     HTILE, as now. Where none is pending, the target is cleared and counted (beta 13 uses that
     count).
   - **Still filled from memory:** textures made with data, and memory no known resource holds.
   - Page tracking stays (part 4 decides it), so a CPU write after creation still reaches the image.
2. **No hashes for those surfaces** (with part 1): a surface started empty takes no hashes, and a
   CPU write in its pages while its resource lives is counted, its contents kept (`InvalidateMemory`).
   The hashes stay for every image still filled from memory (see "The first-fill hashes").
3. **Dead targets' images reused** (on by default; `BB_TARGET_POOL=0` off). A new surface takes the
   Vulkan image of a dead image (its texture destroyed, or something newer made over it) of the same
   shape that the GPU is done with, instead of allocating one; part 1 clears it. The game's pending
   destroys and creates are taken first, so beta 7's same-memory hand-over keeps its image. Built
   on the dead list itself rather than a pool kept apart: the old set waits out its 3 s grace on that
   list, so a separate pool would be empty when a rebuild needs it.
4. **Write-watching replaced by the calls, for game textures** (off by default;
   `BB_WATCH_GAME_TEXTURES=0` turns it on). With the switch on, the pages of GX textures (targets, and textures made with data), YEBIS's targets (default
   usage, render-target or depth binding) and their views are not write-protected. YEBIS's
   CPU-written textures and Scaleform's planes with memory of their own stay watched until beta 9
   hooks their write calls.
5. **The logging:**
   - **Write faults that land on live game resources after their creation,** by kind (GX target,
     GX texture with data, YEBIS target, YEBIS other, Scaleform plane, GX buffer), the largest few
     described (address, kind, size, seconds since creation). This is part 4's measurement in the
     default run, and beta 12's for buffers.
   - **Each YEBIS rebuild:** where its new targets landed (a dead target's memory with the same
     shape, with another shape, memory a live target still holds, fresh memory), and whether the
     old set's destroys came before or after the new set's creates.
   - **Part 1:** surfaces started empty, carried, still filled (by owner kind); depth targets
     cleared with no game clear pending.
   - **Part 3:** pool hits and misses, bytes held, freed for VRAM or age.
   - **Shaders:** translations for draws (count, time, by stage) and how many were another variant
     of a program translated before; input layouts the game created (count, elements, during
     loading screens), from GX's input-layout creator `0x21657a0` (bbhost's `0x25657a0`).
   - **The rebuild stalls** in the hitch reports and Stall lines, against beta 7's 59-84 ms.

**Test:**

- **Run A:** a normal session with loading screens and area crossings (the crossing that stalled
  in beta 7's log), switches as shipped.
- **Run B:** the same route with part 4's switch on.
- **Run C** (any time before beta 14): the shader cache cleared, the same route.

**Moves on when:**

- nothing looks wrong in A and B;
- the rebuild stalls are down;
- run A's write count on game textures is zero, or every writer it finds is named.

**Run A's log** (`675ecf16`, 2026-11-08, about 8 minutes, switches as shipped):

- **The rebuild stalls are gone.** In play, every frame was 28.5 ms or less but one of 41.6 ms
  (84 MB of image uploads, below) and one of 43.8 ms (40 pipelines after a load); beta 7's area
  crossings stalled 59-84 ms. One hitch report against beta 7's several.
- **YEBIS destroys its whole set first, then builds the new one:** at all 22 rebuilds no YEBIS
  texture was live when it began, and the previous set's count was destroyed in the 2 s before. The
  sets alternate between 151 and 158 textures (two configurations, crossing between parts of an
  area), 297 at loading screens. The new set lands mostly inside the old set's memory at other
  offsets (73-230 a rebuild), on memory of the same shape 0-37: why beta 7's hand-over rarely held.
- **Part 1:** 160-630 surfaces a minute started empty (nearly all YEBIS's); depth targets cleared
  under the game's pending clear 4-15.
- **Part 3:** 100-230 a minute took a dead target's image; 44-428 found none.
- **Part 5:** CPU writes that landed on textures made with their data: 21 in the first minute
  (startup), none after; on live GX buffers: none at all; on YEBIS textures 10-71 a minute (which of
  YEBIS's textures: its CPU-updated ones are the likely writers, beta 9 hooks their calls); on
  textures made without data (the CPU fills them by design) 1-2. Input layouts: 1,398 at startup,
  then mostly during loading screens (59 in one minute of play). Shaders translated for draws with
  the cache warm: 0-51 a minute, nearly all another variant of a program translated before.
- **Left:** surfaces filled from the GPU's earlier writes to their memory as a buffer ("taken"),
  21-32 a minute and up to 108 MiB (GX render and depth targets, 32 MiB depth targets among them):
  most of the 41.6 ms frame's uploads. The check takes any GPU write ever made there, also one
  made for the memory's previous owner; it should take only those made since the target was
  created (beta 9).

**Run B's log** (`a49794df`, 2026-11-08, about 3 minutes, `BB_WATCH_GAME_TEXTURES=0`): 1,386-7,049
images a minute left unwatched (578-2,804 MiB), 14 YEBIS rebuilds, no hitch report; frames in play
33 ms or less (the 43-57 ms ones at the start and the first load); a clean exit. The same 21 CPU
writes on textures made with their data at startup as in run A (small ones: 32x32, 1024x512), so
not something unwatching changes; which code writes them is still to name. The tester's eyes on the
picture decide part 4 (stale or wrong textures).

### Beta 9: watching only what no call covers; the ownership design

Built 2026-11-08 (engine id 93), from beta 8's runs. Safe mode watches every image as before.

1. **The game's targets unwatched by default** (`BB_WATCH_GAME_TEXTURES=1` watches every image;
   `=0` also leaves textures made with their data unwatched, beta 8's run B). Surfaces made without
   data for the GPU and textures with a render-target or depth view: no CPU write landed on them in
   beta 8's runs. Textures made with their data stay watched: the CPU wrote 21 of them at every
   start.
2. **The GPU's earlier writes, measured** (`BB_TAKE_EARLIER_WRITES=0` leaves them out; on by
   default). A surface whose memory the GPU wrote as a buffer is told whether those writes came
   after the game made it or before (its memory's previous owner's): the GPU's newest write there
   is noted when the creation is taken (`created_gpu_version`). They are taken either way by
   default, as before. The game may copy a depth buffer through memory and then make a texture
   over it, which needs the earlier writes, so leaving them out waits for the counts and the
   tester's eyes.
3. **YEBIS's and Scaleform's write calls hooked** (bbhost's sites, 1.09 prologues checked): YEBIS's
   UpdateSubresource `0x11d8d70` and Unmap `0x11d99c0`, its Map `0x11d92c0` (counted), Scaleform's
   unmapTexture `0x3a3270`. The texture they wrote is marked CPU-written in the registry (no longer a
   surface: filled from memory at its first use) and the images over it are marked written
   (`ProcessWritten`). Beta 8's flicker theory needed this; it turned out older (the register).
4. **The writer of textures made with their data named:** the GPU library's fault entry keeps the
   fault's context (`SetFaultContext`), and the first 24 CPU writes that land on such a texture are
   logged with the guest instruction and its callers (image offsets).
5. **The design note for the owner rule**, below (beta 11 since the design check), to go through
   with the tester before any code.

**The log:** `GX memory, write calls this minute (beta 9)` (YEBIS's updates, Maps and Unmaps of its
2D textures, Scaleform's unmaps, the written events), `a CPU write landed on a texture made with its
data` lines, and in the surfaces line the starts taken from the GPU's earlier writes.

**Moves on when:** no stale or broken texture in the tester's runs (button prompts, the UI, YEBIS's
effects), and the design note is agreed.

**The tester's run** (`2803b47d`, 2026-11-08, about 10 minutes, switches as shipped): a clean exit;
in play every frame 28.2 ms or less but one of 37.9 ms and one of 40.8 ms after a load (4 pipelines,
and 80 MiB of textures the game fills from the CPU; the texture cache's own time 2.8 ms). 319-360
images a minute left unwatched in the busy minutes.

- **YEBIS's write calls:** 58-145 updates a minute, all on its sampled textures (none on a surface);
  no Map or Unmap of a 2D texture (as bbhost found). Scaleform's unmaps 1-82 a minute.
- **The CPU writes on textures made with their data:** the one with a fault context is GX's own
  create call copying the texture's data in (`+0x2166210`, inside CreateTexture `0x2165f90`), over an
  image left from the memory's previous owner. The rest have no fault: they come through the asset
  and DMA path (`Rasterizer::NoteAssetWrite`), which tells the texture cache directly whether its
  pages are watched or not. So textures made with their data can go unwatched too (beta 10).
- **The GPU's earlier writes:** surfaces filled from writes older than the resource 1-58 a minute
  (render targets, up to 83 MiB); from writes since, 2-10 (depth targets among them, 64-110 MiB).

### Beta 10: the hole closed; the owner rule counted before it acts

Built 2026-11-08 (engine id 94), from the design check below (the tester: "go ahead"). Safe mode
watches every image as before.

1. **A texture made with its data tells the images over it** (no switch: it closes beta 9's hole).
   Its create call copies the data into its memory (`+0x2166210`). The created event now says the
   texture came with its data, and `ProcessGxCreated` marks every older image over that memory
   CPU-written, whatever its watching. Each takes the memory at its next use, its hashes telling
   which levels changed, as a watched image did at the fault.
   - **Kept:** a live surface reaching past the new texture (beta 8's rule: the memory outside the
     new texture is older than what the GPU drew there). Counted.
   - **Marked:** a surface wholly inside the new texture. That is beta 9's hole: a draw for the new
     texture took that image with the old target's contents.
   - `FindImage` takes the game's pending destroys and creates before it looks, so a draw after the
     creation finds the mark.
2. **The overlap census.** Every new texture made over a live texture's memory, at its base or
   partly, by the pair of kinds. The first 12 are described (addresses, sizes, seconds between the
   two). bbhost counts the same and reads them as destroys it missed.
3. **The owner rule computed alongside today's rules, not acting.** Only images at a resource's start
   (with an owner) are counted; the rest keep today's rules under the owner rule too.
   - **A dead image a draw takes again** (its texture destroyed, or something newer made over it),
     once per death: today the draw takes it back. The owner rule would hand it to the texture
     living there when that one was made without its data and is as large, and otherwise refuse it.
   - **Dead images today's guesses keep:** because another live texture overlaps, or because it was
     used just before the new resource was made. The owner rule would retire them.
   - **Stale images the collector freed that had an owner** (the owner rule retires them when their
     texture goes).
   - **Copies run through memory:** in all, to an owned image, owned to owned.
4. **Textures made with their data unwatched by default** (`BB_WATCH_GAME_TEXTURES=1` watches every
   image; `=targets` is beta 9's). Their CPU writes are the create call's copy (part 1) and asset
   loads (`Rasterizer::NoteAssetWrite`, which reaches unwatched images). The game's destroy marks
   such an image as it marked a watched one: `ProcessGxDestroyed` does it, since `ReleaseDestroyed`,
   on the game's thread, passes over pages nothing write-protects.

**Not in beta 10:** `registry_generation` stays as it is. Part 1 only marks images written, which the
draw's refresh reads after `FindImage`; the owner changes that need the fast path to forget come with
beta 11.

**The log:** `GX memory, owners this minute (beta 10)` (all of the above in one line) and
`GX memory: ... made over the live ...` (the census's first 12).

**Moves on when:** no stale or broken texture in the tester's runs, and the counts hold: overlaps few
and explained, and refusals rare on images draws take. Then beta 11 turns the rule on.

**Test:** a normal session with loading screens and area crossings, switches as shipped (the two
flicker spots are the game's, parked: unchanged is expected).

**The tester's quick run** (`57912614`, 2026-11-08, about 8 minutes, 3 loading screens, switches as
shipped): a clean exit; 60 FPS, frames in play 30.3 ms or less, 44.1 ms and 45.6 ms at the ends of
loading screens (a load's 425 MB of buffer uploads).

- **Part 1:** 0-424 images a minute marked written by a creation with data (86 MiB at a load); 2
  live surfaces reaching past one kept.
- **Part 4:** 3,239-5,588 images a minute left unwatched in the busy minutes (beta 9: 319-360). Write
  faults unchanged (about 10 a second on average, as in beta 9): they are the game's buffers (beta 12).
- **The census: no new texture made over a live one,** at its base or partly, in 8 minutes. The design
  check misread beta 9's "left to their draw (an image there: N of a texture not destroyed)": that
  counts images whose own texture had no destroy (an image a draw made over memory no texture holds,
  memory the game released), not a live texture under the new one. "One owner per image" holds so far.
- **The owner rule, at a draw:** dead images a draw took again 0-10 a minute; the rule would hand
  29 of them over (a texture made without data and as large lives there) and refuse 2.
- **Kept by a guess:** 76-371 drawn images a minute at loads (81-584 MiB), kept because they were used
  just before the new resource was made over them: YEBIS's old targets under the new set (its rebuild
  lands 124 of 158 inside a destroyed target's memory). They are freed only when a later rebuild
  lands over them again; the owner rule would retire them at the rebuild. The overlap count beside it
  counted the same images a second time (they are on the dead list for the destroy and for the
  create); counted once from engine id 95.
- **Copies through memory:** up to 2,809 a minute, none to an owned image. The design note's item 6
  (copying those image to image) has nothing to do.

**The tester's second run** (`5a20e6b9`, 2026-11-08, about 5 minutes, 4 loading screens, engine id
94): a clean exit, the same picture. No new texture over a live one; the owner rule would hand over
27 dead images draws took and refuse 1; copies through memory up to 3,600 a minute, none to an owned
image; 0-140 images a minute marked written by a creation with data, 4 live surfaces kept. Frames in
play 28.4 ms or less, but one of 37.7 ms with 42 pipelines built on a first visit (beta 14's work).

- **The frame after the 3.8 s load into m34 (both runs: 44.1 and 48.6 ms):** about 400 MB of the game's
  buffers uploaded and about 100,000 pages write-protected in that one frame, 23-25 ms of it in the
  protect calls; no texture work. Not new: 0.4.5 betas 13 and 14 and 0.4.6 beta 3 had the same frame
  (380-400 MB, 24-25 ms of protecting), then with 730-820 images registered beside it (58-69 ms).
  Beta 9's run had no such load. It is the game's buffers watched by page: beta 12's work.

### Beta 11: images belong to their textures

Built 2026-11-08 (engine id 96), from the design note below as beta 10's runs revised it, the tester's
go-ahead. `BB_IMAGE_OWNERS=0` keeps the old rules; safe mode sets it.

1. **An image of a texture of the game's goes with it.** Such an image is one the game's texture
   starts at (`OwnedImage`: the owner found at registration, as beta 8 and 10 counted). When that
   texture is destroyed or something newer is made over its memory, the image is retired once the GPU
   is done with it: beta 8's pool may take its Vulkan image first, then it is freed (`DeadImageState`,
   `FreeDeadImages`). The old guesses ("used just before the new resource", "another live texture
   overlaps") now keep only images of no texture of the game's (`KeptByGuess`).
2. **Unless a draw used it since.** The GPU's timeline is behind the game's: a draw recorded after the
   game destroyed a texture may read it (beta 10's runs: 0-3 a minute), and on the console its memory
   still holds it until something new is made there. Such a draw takes the image back, as before. "Since"
   is now told by the scheduler's tick at the death (`dead_sched_tick`) as well as the collector's pass,
   so a draw in the same pass counts. The copy shader's image copies mark their images used too
   (`MarkFound`), as `FindImage` does.
3. **Any texture of the game's made without data takes the image of its shape it is made over**
   (beta 7's YEBIS hand-over, for every texture): same memory, width and height, levels and layers
   those of the new texture or fewer (an image of a target is one level of it), and the image's texture
   gone or none. A GX texture's shape comes from its T# (`Insert`); the image's owner becomes the new
   texture.
4. **What the design note had that is not built, from the runs:**
   - **Refusing a dead image at a draw:** the late draws above need it back; with something newer
     there, the hand-over (made without data) or beta 10's marking (made with data) already gives the
     right contents.
   - **The 3 s grace:** stays for YEBIS (its old set is destroyed up to 2 s before the new one).
   - **`FindImage`'s fast path:** unchanged; with no refusal it hands out only images `FindImage`
     itself would.
   - **The stale collector and `DrawnOverwritten` for owned images:** unchanged (beta 10: 0 MiB
     hashed, 0-5 stale owned images a minute).
   - **Copies image to image:** none reaches an owned image.

**The log:** `GX memory, the owner rule this minute (beta 11)`: images handed to a texture other than
YEBIS's (YEBIS's stay in the `GX follow` line), and images retired that the old guesses kept, with how
many a new surface took. Beta 10's owners line stays: its "kept because" counts should fall to zero
with the rule on. Beta 8's surfaces line: "new surfaces that took a dead target's Vulkan image" should
rise and "found none" fall (beta 10: 126-255 took, 164-215 found none, a minute at loads).

**Moves on when:** no missing or wrong texture, target or effect in the tester's runs (YEBIS's
effects after loads above all); the kept images gone at loads.

**The design note** (written before beta 10; the reasoning behind the above):

**Today** an image is found by address and shape, and a stack of rules decides whose it is:

- `ProcessGxCreated` retires the images inside a new resource's memory (`dead_by_reuse`, with a
  `reuse_tick` exception for images used since);
- `DeadImageState` keeps a drawn image another live texture overlaps;
- YEBIS's dead images wait 3 s for a same-memory hand-over (beta 7);
- `GarbagePass` hashes the memory under stale drawn images to tell whether it was written over
  (`DrawnOverwritten`).

**The rule instead:** an image belongs to the resource it was made for, and the registry says which
resource lives at an address.

1. **Owner at registration.** An image whose memory a live resource starts at and covers takes that
   resource's identity (its key and creation time; beta 8's `surface_ns` and `gx_owner`, made
   general). An image over memory no resource of the game's holds has no owner and keeps today's
   rules (the engine's own data, and anything the registry does not hook).
2. **At a draw or a bind.** `FindImage` takes an image at the address only while its owner is the
   live resource there (`StillLive`), or while it has no owner and no resource lives there. The game's
   pending creates and destroys are taken first (as beta 8's part 3 does), so the registry is current.
3. **When the owner is gone and another resource lives there:**
   - **The same shape, made without data:** the image passes to the new resource with its contents.
     On the console the memory carries over. This is beta 7's hand-over, made general from YEBIS to
     every resource.
   - **Any other case:** the image is retired once the GPU is done with it, and a new image is made.
     A surface starts empty; a texture made with its data is filled from it.
4. **Destroying a resource** retires its images once the GPU is done, unless rule 3 passed one on
   first (`FreeDeadImages`, unchanged). The 3 s grace goes: the creates are taken before the draws
   that would use them.
5. **No guessing for owned images:**
   - `dead_by_reuse`, `reuse_tick`, the `LiveTextureOverlaps` exception and the grace go.
   - An owned image lives and dies with its owner, so `DrawnOverwritten`'s hashing and the stale
     collector keep only ownerless images.
6. **Copies.** The copy shader already copies image to image where it recognises an image copy (run
   B: 415 a second). Its copies through memory (58 a second) land in the destination's memory, and
   that resource's image takes them (beta 9's "taken"). Beta 10 counts the copies through memory
   whose source and destination are both owned images. If they are many, the copy goes image to
   image (beta 11 or later).
7. **The switch:** `BB_IMAGE_OWNERS=0` keeps today's rules, for beta 11 only.
8. **Textures made with their data unwatched too** (beta 9's run: their CPU writes are the create
   call's own copy and asset loads, both told without faults); `BB_WATCH_GAME_TEXTURES=1` still
   watches everything. Done in beta 10.
9. **The measurements** (counted in beta 10 before the rule acts):
   - images refused because their owner was gone, handed over, and retired;
   - stale images left (near zero for owned ones);
   - copies through memory with an owned destination.

**Revised by beta 10's runs** (proposed to the tester, 2026-11-08):

- **Item 4's grace stays for YEBIS.** Its old set is destroyed up to 2 s before the new set is made
  (the rebuild lines: 113-158 destroyed in the 2 s before, none during or after but at one load). Freed
  sooner, the old images would be gone before the new set's hand-overs and beta 8's pool could take them.
- **What the rule changes most:** the drawn images the "used just before" guess keeps (YEBIS's old
  targets under the new set, up to 696 MiB a minute at loads) become dead images like any other: the
  pool's donors first (beta 8's pool finds none for 45-55% of new surfaces, 164 and 215 a minute), then
  freed. Measured by the pool's misses and the images kept.
- **Item 6 goes:** no copy through memory reaches an owned image (beta 10's runs).
- **At a draw:** the hand-over and refusal decide few images (3-18 a minute); the hand-over keeps
  today's result, and a refused image is one part 1 already marks written.

**What could go wrong, and the guard:**

- **A resource the registry misses.** 3D and 1D textures have hooks, but not every path may be
  covered. Such an image has no owner and keeps today's rules.
- **A draw before the creation is taken.** The pending events are taken first.
- **The game drawing into a resource's memory through a view of another resource.** bbhost's
  "draws that name no view" (YEBIS's, the wrapper's): the registry's live resource there still owns
  the image, so the draw goes into the right one.

### The design note checked against the code (2026-11-08, the tester's request)

- **A hole beta 9 opened (narrow).** A texture the game makes *with* its data copies that data
  into its memory in the create call (beta 9's log: `+0x2166210`, inside CreateTexture). When an
  image of a target sits there, and the target is unwatched since beta 9, that copy no longer
  faults, so nothing tells the image. Usually harmless: the build-ahead path refreshes an exact
  match in place, and a dead one is freed. The case left is "left to their draw (an image there)",
  6-8 a minute in beta 9's run. There, if the draw's `FindImage` resolves to the unwatched target's
  image, it can show the target's old contents. Nothing looked wrong to the tester. **Fix:** a
  creation with data is a write call: the images over the new texture are marked written
  (CPU-dirty) whatever their watching.
- **Live textures do overlap.** The same "left to their draw" counts say that 6-8 times a minute a
  new texture's memory holds an image of a texture the game has not destroyed. So "one owner per
  image" cannot be assumed until it is counted: a census of new resources made over a live one's
  memory, at the same base or partly, by kind (bbhost counts the same and reads them as destroys it
  missed). Today's "used since" and "another live texture overlaps" rules exist for exactly this; the
  0.4.5 beta 17-18 broken surfaces came from ignoring it.
- **Images not at a resource's start** (3.5% of images made in beta 9's run: mips or layers sampled
  on their own, stencil planes, merged images) have no owner and keep today's rules.
- **The fast path:** `FindImage`'s cache would hand out an image whose owner changed. Marking
  images in `ProcessGxCreated`/`ProcessGxDestroyed` must move `registry_generation`.
- **What owners would buy:** correctness by construction (the hole above is one of the cases), and
  dead images freed sooner (beta 9's run: 160 images, 219 MiB of destroyed textures still in VRAM).
  The guesses themselves cost little (32-37 MiB of memory hashed a minute).

**Revised order:** beta 10 measures before it acts (built: "Beta 10" above).
1. The fix above.
2. The overlap census.
3. The owner rule computed alongside today's rules and counted where it would decide differently
   (refuse, hand over, retire), without acting.
4. Textures made with their data unwatched (their creation copy is told by part 1, asset loads by
   their own path).

Beta 11 then turns the owner rule on, if the counts hold. Depth buffers, the game's buffers and the
shaders move one number on (12, 13, 14; the game's buffers then went before depth, 12 and 13).

### Beta 12: the game's buffers by the calls

- GX's buffer creation with data marks the memory it fills.
- Map (types 0-2 in place, 3 a renamed copy kept in GX's rename table at ctx + `0xb6f8`, 4 that copy
  again) tells which data changed. The device-local copies of vertex and index data are updated only
  when told.
- Pages stay protected only for the engine's own data rings and YEBIS's buffers.
- Beta 8's fault count on GX buffers decides how far this goes.
- The frame after the 3.8 s load into m34 (beta 10's runs: about 400 MB of buffers uploaded and about
  100,000 pages write-protected at once, 23-25 ms) is its first measure.

**Moves on when:** no geometry or animation errors in the tester's runs; write faults and buffer
re-uploads down.

### Beta 13: depth buffers

- A depth target's compression metadata (HTILE) and its pending clear follow the owning resource,
  not the address.
- A depth target made without data starts from the game's clear. Beta 8's count says how often there
  was none.

**Moves on when:** no depth artefacts (shadows, the lamp faces, the scene's depth copies) in the
tester's runs.

### Beta 14: shaders built ahead

- GX's input-layout creation hooked; every shader the game creates at startup translated on
  background threads for the variants its vertex formats and the targets predict.
- The pipelines an area needs built during its loading screen.
- The exact design from beta 8's shader numbers and run C.
- `pipeline_compiles.md`'s "Remaining (not done)" items are looked at with it: catalog coverage, the
  warm-up's state rebuilt on the main thread, a profile change emptying the cache, the scene-curve
  cache folder, AMD and Intel catalogs.
- Independent of betas 9 to 13: it moves earlier if beta 8's numbers make compiles the bigger
  stutter.

**Moves on when:** with a cold cache, no compile stutter on run C's route.

### The 0.4.6 release

- **Switches:** each beta's switches, and `BB_COPY_HOLD` and `BB_COPY_MERGE` (kept through 0.4.5
  stable, then due to go), settled with the tester; the old paths agreed on removed.
- **A small-card run:** `BB_VRAM_TEST_BUDGET_MB=3500` with the VRAM manager on. Its rules were
  written for the old model, and Use less VRAM and safe mode turn it on.
- **Docs:** release notes, `engine-map.md`'s memory parts, `gpu-memory.md`'s status, and this
  document's "Where it stands".

## After 0.4.6

**Possibly, the game's GX calls straight to the host GPU,** instead of replaying the console's
command stream (Lantern's native plan, bbhost's YEBIS path). It builds on 0.4.6, not over it:

- **It needs:** resources owned by the game's calls, the shader translator, detiling.
- **It would replace:** reading the command stream and its register state, and the optimisations
  made for that path (the copy-shader detector, render-pass restarts, fence announcements), for the
  draws that move over. Motion vectors would read their per-draw information from the new path.
- **It could go one part at a time,** YEBIS's draws first, with the existing path for the rest.
- **It is decided by measurement:** after 0.4.6, a profile of how much frame time reading the
  command stream takes. If that share is small, it is not done.

## The tester's calls behind this (2026-11-08)

- "do some more research on this behavior, YEBIS, and the other ports": the research above.
- "these findings also enlighten us on certain areas of optimization as well, gaps in our current
  design" and "which we are trying to move away from": the goal.
- "Can't we do more in the next beta?": beta 8 carries five parts, each with its own switch, and the
  measurement for beta 9 rides with its change switched off.
- "0.4.6 is the memory model release, so you need to fold the .7 into it": depth buffers and the
  game's buffers are betas 13 and 12 (11 and 12 before the renumberings).
- "buffers before depth" (2026-11-08, with beta 11): the game's buffers are beta 12, depth beta 13.
- "we haven't made the shader building on game start and in loading": beta 14, measured in beta 8.
- "prepare the full plan/documentation so that we don't lose on or forget any steps": this document
  and plan.md's register.

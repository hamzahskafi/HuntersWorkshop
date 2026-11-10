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
| A depth target's first use reads memory; its compression metadata (HTILE) is followed by address | Part of the rebuilds' uploads | Beta 8 part 1; the rest dropped (the tester, 2026-11-09) |
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
buffers), the game's buffers became 12 and depth buffers 13. On 2026-11-09 the tester dropped the
depth items: beta 13 is beta 12's follow-ups. Earlier text that named them by their old numbers has
been brought up to date.

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

**The tester's run** (`21528c98`, 2026-11-09, about 13 minutes, 14 loading screens, engine id 96;
the tester: "runs like sex", VRAM "much lower and more consistent"): a clean exit, nothing wrong seen.

- **The rule at work:** 35-252 images a minute retired that the old guesses kept (31-400 MiB); 21-44
  GX images a minute (up to 459 MiB) handed to the texture re-created over them. Beta 10's kept-by-guess
  counts fell to 0-2 (the 1-2 "used just before" are images the exact "used since" kept: the label
  predates the rule). No new texture over a live one. Stale images under 3 MiB all session.
- **VRAM (the minute lines):** the texture cache's images 938-1,761 MiB, 1,223 on average (beta 10's
  runs: 1,329 and 1,374; beta 9: 1,279), with more areas visited; idle images the GPU drew 17-184 MiB
  (beta 10: 56-334); memory handed back to the driver 131-530 MiB a minute (beta 10: 0-320), so VRAM
  falls back after loads. The driver's total is about the same (2.4-4.2 GB; beta 10 2.7-3.8 GB),
  lower only in the first minutes (about 350 MB), because the game's buffer data grows with the areas
  (1.6 GB by the end; beta 10 1.3 GB): beta 12's subject. The log samples once a minute, so it cannot
  show the swings between; a run on the same route with `BB_IMAGE_OWNERS=0` would compare directly.
- **Frames:** pacing p99 17.0 ms on average, its worst 26.7 ms (beta 10: 29.1); in play no frame over
  30.1 ms; 40-56 ms only at the start and around loads (one of 40.8 ms with no engine work in it).
  This route had no 3.8 s load into m34, so its buffer frame is not compared.
- **The pool barely gained** (5-186 new surfaces a minute took a dead target's image; 30-469 found
  none): the retired YEBIS targets were drawn up to the frame before the rebuild, so the GPU is still
  on them when the new set is first used, and the pool takes only images the GPU is done with.
- **Write faults** 26.6 a second on average (beta 10: 10.7 and 17.7): the route's buffers (beta 12).

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

Built 2026-11-09 (engine id 97), the tester's go-ahead after the design below was checked against the
code. Safe mode sets `BB_WATCH_GAME_BUFFERS=1`.

**As built** (what differs from the design, found while building):

- **Pages the GPU wrote stay protected:** the writable set is `cpu | (calls & ~gpu)`
  (`RegionManager::WriteTarget`), re-applied whenever the GPU's bits change, so a CPU write on a page
  the GPU wrote still faults and reads the GPU's data back first. Only pages holding no GPU data are
  left writable.
- **The engine's own CPU stores tell the buffer cache:** some GPU commands are executed as CPU stores
  into guest memory (`WRITE_DATA`, a DMA fill or copy not yet on the GPU, the stand-in occlusion
  counts), and the code counted on the page fault to tell the buffer cache ("the VRAM copy follows
  through write tracking"). Each now marks its range to upload (`BufferCache::NoteEngineStore`).
- **End-of-pipe and end-of-shader labels** (the tester's builder, 2026-11-09): written through the
  backing view they never fault and never reached the buffer cache, before beta 12 too (unchanged).
  Where there is no backing view the fallback stores straight into guest memory and counted on the
  fault; it now tells the buffer cache too (`StoreLabelTold`, engine id 98).
- **Where each part runs:** the tracker's regions are made only on the thread that uploads (no lock
  guards their making), so the game's thread does only what is safe anywhere: marking the range to
  upload (`MarkToUploadButGpu`, no region made). The pages' entering and leaving the calls' watch go
  through an ordered queue applied at the per-packet housekeeping (`Maintain`), before the packet's
  bindings: a buffer is created before any packet that uses it is submitted.
- **The net** hashes up to 1 MiB of live GX buffers each second (the first MiB of a larger one), not
  32 MiB once a minute: about 0.1 ms a pass on that thread instead of a few milliseconds at once.
- **The census** covers live buffers too (any new resource over one), in the same minute counts and
  `made over the live` lines.

**The log:** `GX memory, the game's buffers by the calls this minute (beta 12)` (buffers made and
released, textures' memory handed back, buffers made over, pages kept writable now, writes told, the
net's hashed buffers and MiB, CPU writes no call told) and `the CPU wrote a live GX buffer with no call
telling it` lines (the first 12).

**Test:** a normal session with several loading screens, the 3.8 s load into m34 among them, and if
possible a boss, a cutscene, character creation or online play (what beta 9-11's runs did not cover).
Watch for wrong geometry, stretched or exploding meshes, broken animation.

**The tester's run** (`8bebd58c`, 2026-11-09, about 14 minutes, 14 loading screens, engine id 98; the
same route as beta 11's `21528c98`, the loading screens within 0.07 s of each other): a clean exit, no
hitch report (beta 11: three of 25-26 ms), nothing wrong seen.

- **Write faults down 76%:** 5,615 against 22,991; 4.5 a second on average (26.6), at most 75 (376).
  The create calls' copies: textures 2,268 (10,849, -79%), buffers 1,487 (10,050, -85%). Where the
  rest land is not measured: memory not handed back is the guess (pages shared at a resource's edges,
  pages the GPU wrote, memory no GX resource held before), and beta 13 counts each fault by what
  watched its page.
- **The net found no untold write:** about 60 MiB hashed a minute, 27-463 buffers; up to 224,031 pages
  (875 MiB) kept writable for the calls. No new resource was made over a live buffer.
- **Frames:** pacing p99 16.94 ms on average (beta 11: 17.02), 23 frames over 1.5 times the median
  (27); frames over 33 ms only at the start, after the first load, and one with 58 pipelines built
  (40.2 ms: the GPU cache recompiled at the start, below). The m34 load in this route is the first
  entry (3.13 s, from the title); beta 10's 44-49 ms frame came on the second (3.8 s), not run here.
- **VRAM** as beta 11 (beta 12 does not change it): images 1,157 MiB on average, the game's data
  growing to 1.66 GB.

**The run read in full** (2026-11-09, the tester's request; against beta 11's `21528c98`, the same
route; `cmp.py`-style totals over every 5-second window):

- **What the fewer faults bought, in time:** the game's texture creator 1,959 ms over the run against
  2,058 ms (-5%, 53.7 against 56.5 us a texture; 36,517 and 36,406 textures). A fault costs about
  11 us there, so the 8,581 fewer saved about 0.1 s in 14 minutes, most of it during loading
  screens; buffers' creator time is not timed. The larger gains are elsewhere: other threads'
  protection changes 0.263 a frame against 0.915 (-71%), the GPU thread 3.91 us a draw against 4.27
  (-8%). GPU time a frame (16.67 ms), its idle (11.18 ms), the game's sleeps, readbacks and sparse
  binds are as beta 11.
- **Every long frame outside a load is the engine's minute report.** Each minute the presenting
  thread writes the RAM, VRAM, VRAM manager and GX lines (`LogRam` in `videoout/driver.cpp`, on the
  flip path), and that window's worst frame is 25.7-30.1 ms: 14 of 14 minutes here, 13 of 13 in beta
  11. Outside loads and those windows beta 12 has no frame over 25 ms (beta 11: 2). It began with
  0.4.6 beta 5: every minute in every log of betas 5-12 (85 of 85 windows over 22 ms), against 1 of
  36 in betas 3-4 and 5 of 61 in 0.4.5 betas 14-17. It runs in every session, not only with
  diagnostics. Nothing in the report's code reads as 10 ms of work (counters, a few registry copies,
  `GetProcessMemoryInfo`, the VMA budget); which part costs it is not known from the log. Beta 13:
  the report timed part by part, and moved off the presenting thread.
- **The extra shader compiles are the GPU cache recompiled at the start**, not beta 12's memory
  work. The cache's identity (`out/gpu-cache.id`, `build.sh`) hashes the shader recompiler, the
  register layouts and everything in `video_core/amdgpu`, which holds `liverpool.cpp`, the command
  processor; beta 12 changed it (`NoteEngineStore` at WRITE_DATA, the occlusion counts and the label
  fallback), the only file of the set that changed since beta 11. The start recompiled the cache
  (3.0 s): 1,213 shaders kept, 58 left out (changed or without a state), 113 pipeline keys removed
  with them. So 704 pipelines were built at the start (beta 11: 740 of 745) and 229 compiles ran in
  play (140 ms; beta 11: 98, 71 ms), most in the first areas: 58 in the frame after the m34 load
  (39.2 ms), 31 in the next window with 12 draws of 3 pipelines left out while they compiled (scene
  geometry missing for up to a second), then 23, 14, 11. The run kept their states, so the next run
  on beta 12 starts without them. Nothing in `liverpool.cpp` is stored in the cache (it applies
  register packets and runs PM4; a different state only makes different keys); beta 13 changes it
  again (the labels) and leaves it out of the identity, one recompile at its first start.
- **Writes told over live buffers: 0 all run.** The writes the engine hears of (file reads into GPU
  memory, `NoteAssetWrite`) landed only on memory that was not a live GX buffer: the game reads into
  its own memory and the create call copies from there, as the design expected.
- **The net's reach:** about 60 MiB a minute against 213-875 MiB under the calls, so one pass over
  all of them takes 4-15 minutes. Enough for a check that finds nothing; a write it finds is late by
  up to that much.
- **Hitch reports:** beta 11's three (25-26 ms) were the game's main thread in its own code (stacks
  at `+0x2034b08`, `+0x24c5cc1`), just over the 25 ms line; none here. One run each: not put down to
  beta 12.
- **The rest as beta 11:** the owner rule (retired 2,859 MiB, handed over 311, pooled 136; beta 11:
  2,819, 298, 136), YEBIS's rebuilds, images freed at destroys (10.5 GB), textures built at creation
  (7.2 GB), RAM (the game 5.5 GB by the end; beta 11 5.3 GB, one minute shorter), the clean exit. Submissions a frame 14.8 at the median
  against 13.4: more fence submissions when the GPU runs out of work (13.95 against 12.84 a frame),
  with GPU time and idle equal; a matter of timing, nothing beta 12 submits.

**The design** (2026-11-09):

**What the logs say** (beta 5's Map counts, beta 8-11's fault counts and sites, beta 10-11's long
frames):

- **The CPU writes a GX buffer's own memory once: when GX creates it.** Every Map of a buffer in beta
  5's run (up to 115,000 a minute) went into a renamed copy from GX's pool (types 3 and 4), none in
  place (types 0-2), and no texture was ever mapped. No write fault landed on a live GX buffer after
  its creation in about 33 minutes of beta 9-11 runs (beta 8's count). The renamed copies are GX's
  pool, which the engine already reads in place (the GX region, 0.4.4).
- **91% of the write faults left are the game's create calls copying data in:** of 22,991 in beta 11's
  run, 10,849 are CreateTexture's copy (`+0x2166210`) and 10,050 CreateBuffer's (`+0x2165e2e`, its call
  to `0x216d420` with the initial data), landing on memory the buffer cache still watches from an
  earlier resource there. Each fault stops the game's thread (and reads back first any page the GPU
  wrote, data the copy then overwrites). The rest: 901 from the game's code at `+0xc21ef2` (through
  memcpy), 343 YEBIS's, a few hundred others: memory no GX resource holds.
- **The buffer cache protects every page it uploads** (`RegionManager`: a page is writable exactly
  while it is CPU-modified, `writeable == cpu`). The frame after the 3.8 s load into m34 (beta 10:
  44-49 ms) spent 23-25 ms write-protecting about 100,000 pages after uploading about 400 MB.

**Checked against the code before building (2026-11-09, the tester: "are you sure?"):**

- **The count of CPU writes on live GX buffers can see them:** `Covering` finds the buffer holding
  any faulting address, every write fault reaches it, and a page of a live buffer is write-protected
  from its first upload on, so a write after that faults at least once (`ExtendWriteFault` then opens
  the window around it). Zero in about 33 minutes means none after a first use.
- **Map's renamed copies are not a registered buffer:** Map's type 3 (`0x2169c30`) takes its copy from
  an allocator whose retry (`0x26aa860`) is the GX range allocator's own (its init `0x26a9b50`), the
  region the engine reads in place with no write tracking (0.4.4); the logs agree: thousands of
  bindings a second there, 26 write faults a second in all.
- **Textures are written after their creation:** GX's depth view creator (`0x2166610`, through
  `0x217a210`) fills the depth texture's HTILE with its clear pattern (dwords `0x0000000F`) when the
  game makes a depth view, a CPU write into a GX texture's memory that is not its creation. So part 1
  is for GX buffers only; textures stay watched (part 3 still hands their memory back at a destroy).
- **The other writers in the fault sites are not GX memory:** `+0xc21ef2` (901, through memcpy) writes
  around `0x105ef9xxxxx`, YEBIS's memory; `0x216c5d0` (58) fills a block it allocates; YEBIS's 343.
- **Not certain until beta 12 runs:** that the creators' faults land on memory of destroyed GX
  resources (part 3 removes only those); that the pages protected after the m34 load are GX buffers'
  (part 1 removes only those); and that no CPU write into a live GX buffer happens in what beta 9-11's
  runs did not cover (bosses, cutscenes, character creation, online). Part 7 is the net for the last.

**The design** (as checked; implementation points from the tracker's code):

1. **The pages of the game's GX buffers are watched by the calls, not by protection.**
   - A third page state in `RegionManager`, `calls`, beside `cpu` (to upload) and `gpu` (the GPU
     wrote it): protection follows `writeable = cpu | calls`, so an upload protects nothing there.
   - Set for the 4 KiB pages wholly inside a live GX buffer, at the buffer's creation (part 2). A page
     the buffer shares with other memory stays watched as now. GX's textures, YEBIS's and Scaleform's
     memory, the GX region (already untracked) and everything else stay as they are.
   - The tracker keeps its page state in 4 MiB regions made at the memory's first use, all pages
     "to upload" and writable: the buffer's regions are made at its creation, so `calls` is there
     before the first upload (otherwise that upload would protect the pages, as now).
   - The tracker's regions are never freed, so `calls` is cleared wherever the buffer stops owning the
     memory: its release (part 3), the game unmapping the memory (`Rasterizer::UnmapMemory`, which
     already marks it to upload), and a new GX resource made over it (the census, extended to
     buffers: the newer resource's memory goes back to being watched).
2. **A creation is a write.** At GX's buffer creator return (the game's thread, `Insert`, after its
   copy), the buffer's pages are marked to upload (quietly: not counted as the game's write faults)
   and get `calls`; made with its data, the GPU's older data on its inside pages is dropped (the copy
   overwrote it). A page still protected then had its copy fault and handled as now.
3. **A destroy or a release hands the memory back**, for buffers (GX's release, `0x216d690`) and
   textures (the destructors, already followed): its pages leave `calls` and are marked to upload,
   which makes them writable, but for pages the GPU wrote, which keep today's handling (a late read
   of them still gets the GPU's data). Marking a page to upload only costs an upload at its next use,
   so this part is safe on its own. The next resource's creation copy then takes no fault. It runs
   on the game's thread, one protection change per run of pages instead of one fault per page.
4. **What still tells a write:** creations (2), asset and DMA writes (`NoteAssetWrite`, as now), the
   GPU (as now). Map is not hooked again: it never writes a buffer in place.
5. **Measured, once a minute:** write faults (the existing sites, and their total), the pages and MiB
   under `calls`, protection changes on the GPU thread, the census of resources made over a live
   buffer, and in the long-frame breakdown the protect time as now.
6. **The switch:** `BB_WATCH_GAME_BUFFERS=1` watches every page as before; safe mode sets it.
7. **The net,** for writes no call tells: once a minute it hashes up to 32 MiB of live GX buffers, in
   turn, skipping pages the GPU wrote, and keeps each hash with a count of the writes told for that
   buffer (creations and asset writes over it). When a buffer comes round again with the same count
   and a different hash, the CPU wrote it untold: counted, the first ones described (address, size,
   age, which pages), and the buffer marked to upload. Beta 8's count stops seeing such writes once
   the pages are writable; this one keeps looking.

**Expected, and how each is read:** the creators' write faults mostly gone at loads (the fault-site
lines; how many depends on part 3 covering the memory they land on); the frame after the m34 load
without its 23-25 ms of protecting (the long-frame breakdown; only if those pages are GX buffers');
no untold write found by the net (its line).

**Not in beta 12:** uploading buffers during loading screens instead of at their first draw (the
upload's copying runs on the copy threads; whether the frame after a load still waits for it is read
from beta 12's long-frame breakdown first), and freeing the game's dead data from VRAM (part 5's
number first).

**What could go wrong:** a CPU write to a live GX buffer's inside pages that no call tells: its draws
would use old data (geometry, animation) until part 7's pass finds it. None was seen in beta 5-11's
runs. `BB_WATCH_GAME_BUFFERS=1` is the check.

**Moves on when:** no geometry, animation or texture errors in the tester's runs; write faults at
loads mostly gone; the frame after the m34 load without its protect time.

### Beta 13: beta 12's run, followed up

Depth buffers were this beta's subject until 2026-11-09; the tester dropped both depth items, and two of beta 12's follow-ups (below).

- **First, the labels** (from beta 12's build, the tester's builder): an end-of-pipe or end-of-shader
  label written through the backing view never reaches a VRAM copy of its page. Each label write
  checks whether its page holds an uploaded copy not marked to upload, and only then marks it (a
  region lookup and a bit test, about 700 a second); how often is counted, in place of the
  "exact writes/s" counter nothing increments today.
- **From beta 12's run:** each write fault counted by what watched its page (the GPU wrote it, an
  image, a page shared at a buffer's edge, no GX resource), so the create calls' remaining 3,755
  faults are explained by measurement.
- **The loading lines name the area** (from beta 12's run: which m34 entry a load was had to be read
  from the files it opened): each `Loading:` line says the map the game opened during it.

**Dropped (the tester, 2026-11-09: "not needed"):**

- A depth target's compression metadata (HTILE) and its pending clear following the owning
  resource, not the address: no artefact and no count ever pointed at it.
- A depth target made without data starting from the game's clear: beta 8's count says it never
  lacked one (404 depth targets under the game's pending clear in 67 minutes of beta 8-12 runs, none
  without). The count stays in the surfaces line.

- The minute report off the presenting thread and `liverpool.cpp` out of the GPU cache's identity:
  not needed (the tester, 2026-11-09). The minute's 26-30 ms frame and a cache recompile whenever
  `liverpool.cpp` changes stay as they are.

- **Its run closes beta 12** (the plan checked, 2026-11-09): beta 12's last gate is the frame after
  the second entry into m34 (title, m34, a load elsewhere, the 3.8 s load back), which must be
  without beta 10's 23-25 ms of protecting; that frame also decides whether the game's buffers are
  uploaded during loading screens (no frame in beta 12's run waited for buffer uploads, but that one
  was not in it). With it: the long-frame breakdown for frames over 25 ms in the 2 s after a load
  (it starts at 40 ms; beta 11-12's 27-32 ms frames after 3 of 14 loads have none).

**Moves on when:** the label count and the faults' watchers read, the second m34 entry's frame read,
nothing wrong seen.

**As built** (2026-11-09, engine id 99; the tester's go-ahead, the 25 ms breakdown with it):

1. **The labels' check** (`BB_LABEL_CHECK=0`: off, as before; safe mode keeps it, a fix of a gap older
   than beta 12). The end-of-pipe and end-of-shader labels go through `WriteLabel` (`liverpool.cpp`):
   written through the backing view, the buffer cache is told (`NoteBackingStore`); without one, the
   fallback store tells it as beta 12 made it. The occlusion results written through the backing view
   (`vk_occlusion.cpp`) are told the same way. `RegionManager::MarkStoreUploaded` marks to upload
   only the pages holding an uploaded copy (neither to upload nor the GPU's); pages the GPU wrote are
   left (an upload would put older bytes over the GPU's) and counted. No region is made (memory no
   region covers is all to upload already). The log: `GPU data in VRAM:`, in place of the "exact
   writes/s" nothing incremented: stores a second, those over a VRAM copy, those on pages the GPU
   wrote.
2. **Each write fault by what watched its page,** read in `Rasterizer::OnWriteFault` before the
   invalidation clears it: the GPU's data there (`PageWatch` 2: the fault reads it back first), the
   buffer cache's uploaded copy (1), an image (`WatchesPages`), nothing. Per fault site, appended to
   its `Write fault sites` entry (`[watched by ...]`); once a minute, crossed with whose memory it is
   (memory no live resource holds, a live buffer's inside page, a page at a live buffer's edge, a
   page beside a live resource, a live texture by its owner): `GX memory, write faults by what watched
   the page this minute (beta 13)`. A create call's copy faults before its resource is registered,
   so its memory reads as the previous owner's, or no one's.
3. **The loading lines name their maps:** the runtime's file layer notes the map folders the game
   opens (`mNN`, `mNN_NN_NN_NN`; `runtime_file_take_maps`), taken at a load's start and printed at its
   end: `Loading: ... ; maps opened: m24_00_00_00, ...`.
4. **The long-frame breakdown from 25 ms in the 2 s after a load** (40 ms otherwise): the `Stall:`
   lines and the fault handler, copy and protect parts with it (`Loading::EndedWithin`).

Also: a comment beta 12 left above the wrong function in `vk_rasterizer.h` moved back to
`ReleaseDestroyedTexture`. The label check changes `liverpool.cpp`, so the first start recompiles the
GPU cache once (beta 12's run: about 130 pipelines then built in play in the first areas).

**Test:** a normal session through the second entry into m34 (title, m34, a load elsewhere, the 3.8 s
load back into m34), and if possible a boss, a cutscene, character creation or online play.

**The tester's run** (`64b52c0d`, 2026-11-09, about 7 minutes, 10 loading screens, engine id 99, the RTX
3070 Ti): a clean exit, no hitch report, nothing wrong seen; the GPU cache recompiled at the start as
expected (56 shaders, 115 keys; 215 compiles in play).

- **The route, from the new loading lines:** the Hunter's Dream (`m21_00_00_00`, every 1.9 s load),
  `m34_00_00_00` with `m35_00_00_00` and `m36` (3.15 s), `m32_00_00_00` with `m27` (1.73 s),
  `m33_00_00_00` (2.41 s), `m23_00_00_00` with `m24`, `m28` (2.13 s), and `m23_00_00_00` again with
  `m28`, `m24_00_00_00`, `m22`, `m27` (4.31 s). Beta 10's runs show the second m34 entry is the Dream
  then back to `m34` (3.83 s and 3.85 s): this route went from the Dream to `m32` instead, so that
  frame is still not run.
- **But a frame like it was:** after the `m33` load, 45.2 ms, 286.6 MB of buffer uploads; protecting
  took 1.5 ms over 2,007 pages, where beta 10's frame after the second m34 entry took 23.4-24.6 ms
  over 99,186-104,305 pages (425 and 407 MB uploaded). Beta 12's protect cost is gone on a frame of
  that kind. The frame was no shorter: its sparse binds (the game's data put into VRAM) took 23.0 ms
  (beta 10's: 1.0-1.3 ms), the rest is the upload's copying. The frame still waits for the game's
  buffers.
- **The other frames after loads, now broken down from 25 ms:** after the 4.31 s load, 27.1 ms, of
  which 14.3 ms allocating the memory of 11 images (surfaces made at their first draw); after the m34
  load, 36.1 ms: 38 pipelines (26.3 ms, the recompile) and 11.8 ms allocating 21 images; the first
  area after the title, 61.1 ms: 35.9 ms allocating 194 images and 12.3 ms of sparse binds (beta 12's
  same frame: 40.8 ms, 9.5 ms allocating). What is left after a load is the game's buffers going into
  VRAM and the surfaces' memory being allocated, both at the first frame after the load.
- **The labels' check:** 15,000-27,000 stores a second (labels and occlusion results; the design
  guessed 700, counting end-of-pipe labels only). 17 in the run landed on a page holding an uploaded
  copy (one every 25 s or so; up to 7 in a window): a real gap, now closed; none on a page the GPU
  wrote. The GPU thread's time a draw in play: median 3.15 us (beta 12: 3.06, beta 11: 3.18).
- **The write faults by what watched the page:** 2,606 in the run (6.2 a second, as beta 12's 6.7).
  - The create calls' copies, as the plan asked: CreateTexture's (`+0x2166210`) 1,098: the GPU's data
    on the page 599, an uploaded copy 443, an image 64; CreateBuffer's (`+0x2165e2e`) 497: the GPU's
    data 272, an uploaded copy 223. By whose memory: nearly all memory no live resource holds (the
    copy runs before the new resource is registered). So: pages a dead resource's GPU writes still
    hold, which beta 12 keeps watched on purpose (a late read gets the GPU's data), and pages uploaded
    while no resource the calls follow held them. Each of the GPU's-data faults reads the page back
    from VRAM first (readbacks 2.2 a second in the run, about 930), only for the copy to overwrite it.
  - YEBIS's own CPU writes (`+0xc21ef2` 460, `+0x29297ba` 318, others about 100): an image watches the
    page; about 20 of them landed in an image, the rest beside it on a shared page. About 2 a second.
  - Scaleform's plane (`+0x114171`, 49): its 1024x1024 texture, written by the UI.
- **The rest as beta 12:** the net found no untold write (about 60 MiB a minute, up to 744 MiB under
  the calls); VRAM 2.6-3.9 GB of 7.25; the game's data to 1.34 GB; each minute's report still one
  26-30 ms frame (the tester's call).

**Two more runs the same evening** (the shader cache warm):

- `b4244009` (4 minutes): the Dream, `m23`, the Dream, `m34` (3.15 s), the Dream, `m23`, the Dream. The
  m34 entry's worst frame 16.9 ms (the first run's 36.1 ms was the recompile and 11.8 ms of image
  allocation); `m23` again 16.9 ms. The slow frames: the title screen (up to 131 ms, around the
  controller connecting), the first area after the title (45.5 ms, the breakdown under 1 ms), one of
  65.8 ms during the m34 loading screen after the minute report, one of 45.5 ms after the last load
  (nothing in the breakdown), before quitting.
- `e4fc8e33` (4 minutes): the Dream, `m34` (3.13 s), the Dream, `m23`, the Dream, `m34` again (3.11 s),
  the Dream. `m34` twice, with `m23` between: its second load took as long as the first and its worst
  frame was 19.1 ms. Beta 10's slow return was `m34`, the Dream, straight back to `m34` (3.83 and
  3.85 s, about 400 MB uploaded at its first frame); with another area between, the return is a first
  entry again. After the last Dream load one 43.8 ms frame with nothing in the breakdown (as the run
  before: the game's own work, or something the breakdown does not time).
- So beta 12's last gate stands on the `m33` frame of the first run (287 MB uploaded, 1.5 ms of
  protecting); the exact return (`m34`, the Dream, `m34`) would add the 400 MB case.

**The first runs on the RX 9070 XT** (2026-11-10, beta 13, engine id 99; the tester changed cards
before the betas ended):

- `d4be8f82` (under a minute, title to the Hunter's Dream) and `0f407d24` (2.5 minutes: the Dream,
  `m34`, the Dream). AMD's driver 2.0.416, Vulkan 1.4.359, VRAM budget 15,419 MiB. Clean exits, no
  error, no GPU fault; Mailbox not offered, Fifo used (known on AMD).
- **The GPU cache started again**, as expected on a GPU of another kind ("Pipeline cache isn't
  compatible with current system: rebuilding it"); HW ships no catalog, so both runs are cold-cache
  runs on AMD: run C's question, answered for this card. Pipelines cost about 10-26 ms each to build
  (beta 12's run on the 3070 Ti, its driver cache warm: about 0.6 ms). The frames that waited for them:
  2,043 ms during the first load (the loading screen at 26 ms a frame; 86 waits, 2,331 ms), 157-351
  ms frames at the first area, 484 ms after the `m34` load (54 pipelines, 1,392 ms), 140 ms, and 35-76
  ms frames in play; 7 hitch reports in the second run, the first with AMD's compiler busy on the
  pipeline threads, the rest the game's main thread waiting (25-27 ms). Every AMD player's first run,
  and every player's after a GPU change, looks like this until beta 14.
- **The memory model runs on AMD as on NVIDIA:** the GX region imported and read in place through a
  buffer of its own; the net found no untold write; the labels' check 17,900 stores a second.
- **FSR 4.1.2 runs on RDNA 4** (AMD's SDK, native resolution 1920x1080), 0.51 ms a frame (the 3070
  Ti's DLSS: 0.94 ms). The GPU busy 3.5 ms a frame at 60 FPS (the 3070 Ti: 5.6 ms).

### Beta 14: the area's first frames made ready before they are drawn

Designed 2026-11-10 from beta 13's runs (the RTX 3070 Ti's `64b52c0d`, `b4244009` and `e4fc8e33`;
the RX 9070 XT's `d4be8f82` and `0f407d24`) and checked against the code. The roadmap's beta 14 was
shaders built ahead; the tester added the loading-screen work to it (2026-11-10). It is built after
the tester's go-ahead. Each part has its own switch.

**What the frames after a load still wait for.** The loading screen hides its own frames: beta 13's
61.1 ms frame with 194 surfaces was one of them, during the first load.

| After the load | Frame | What it waited for | Part |
|---|---|---|---|
| `m33` (3070 Ti) | 45.2 ms | 287 MB of the game's buffers uploaded, and 23.0 ms of sparse binds reserving their VRAM | 2 |
| `m34`, its first visit (9070 XT) | 141.7 ms, then 483.9 ms | 48, then 54 new pipelines (458 and 1,392 ms of compiling on 4 threads); 147 MB of buffers | 1, 2 |
| `m34` (3070 Ti) | 36.1 ms | 21 surfaces' memory allocated (11.8 ms); 38 pipelines (beta 12's recompile) | 3 |
| the 4.31 s load (3070 Ti) | 27.1 ms | 11 surfaces' memory allocated (14.3 ms: about 1.3 ms each for the ones of 4 MiB or more, which get memory of their own) | 3 |
| during loading screens | (hidden) | the create calls' copies: 1,595 write faults in 7 minutes, about 930 of them reading a page back from VRAM first | 4 |

In beta 13's first minute the game made 2,520 GX buffers with their data (281 MiB), about the
287 MB the `m33` frame uploaded. On AMD a surface's memory costs little (50 in 1.9 ms after
`m34`), so part 3 is for NVIDIA's cost: its counts can be checked on AMD, its time only on NVIDIA.

**Part 1: compile bursts on more threads** (`BB_PIPELINE_BURST=0`: 4 threads, as before)

- **Today:** new pipelines compile on a third of the hardware threads, at most 4 (`BB_PIPELINE_THREADS`,
  Lantern's rule): 4 on the tester's 20-thread i5-14600KF. A new area's first frames bring 40-100
  at once. At AMD's 10-26 ms each, 54 of them held a frame for 484 ms.
- **Burst threads beside the 4,** up to half the hardware threads in all (at most 12; 10 on the
  tester's CPU, 4 on an 8-thread one). They are started with the others but take a job only while
  jobs wait unstarted behind the standing threads, at below-normal priority so the game's own
  threads keep their cores. One new pipeline in play compiles as it does now.
- **Unchanged:** a thread that needs a pipeline nobody has started compiles it itself
  (`BB_PIPELINE_TAKE`); scene geometry is left out for up to a second (`BB_SKIP_UNCOMPILED`).
- **Measured per burst** (from 4 jobs waiting until the queue is empty): pipelines, wall time,
  compile time summed, threads used. The sum divided by the wall time shows whether the driver
  compiles in parallel. NVIDIA's did at the warm-up: 601 pipelines in 13.9 s on 1 thread, 3.1 s on 8.
- **Expected:** a burst's wall time down by up to the thread ratio (2.5 times on the tester's CPU)
  where the driver compiles in parallel.
- **Risk:** CPU time the game's threads want in the same frames, limited by the priority and by
  bursts only.

**Part 2: the game's buffers made during a loading screen, uploaded during it** (`BB_LOAD_UPLOAD=0`:
uploaded at their first draw, as before)

- **The source:** the registry's `BufferMadeWithData` (beta 12's memory event, on the game's thread,
  after the creation's copy) while `Loading::ScreenUp()`: the range is queued. A release removes it.
- **The work:** on the draw recording thread, once a frame, where `BB_PREUPLOAD` runs:
  - each queued range gets its VRAM (`EnsureResident`: the sparse binds) and its upload
    (`SynchronizeMemory`, the same pair as `Preupload`'s mode 2, for these ranges only);
  - up to `BB_LOAD_UPLOAD_MB` (64) a frame while the loading screen is up, then 16 a frame for
    2 s after it, then the rest of the queue is dropped;
  - a range a draw has already used costs a lookup (nothing is left to upload).
- **The room for it:** loading screens draw 104-259 frames of 16.6 ms each. 287 MB takes 5 of
  them at 64 MB a frame.
- **Not touched:** the GX region (read in place, beta 9 of 0.4.4) and textures (built in their
  create call, beta 4).
- **Expected:** the frame after `m33` without its 287 MB and 23 ms of sparse binds. Part 5's line
  says what the first frames still uploaded.
- **Risks:**
  - The VRAM is taken a few seconds earlier, for buffers the area then uses. With the VRAM manager
    on, its comfortable line still decides.
  - A CPU write between a buffer's creation and its first draw that no call tells would be missed
    for longer. Beta 12's net found none in any run, and today's first upload assumes the same
    from then on.

**Part 3: surfaces' memory made during the loading screen** (`BB_READY_SURFACES=0`: allocated at
their first draw, as before)

- **Why surfaces after a load allocate:** they are the ones beta 8's dead-target pool had no image
  for (the register's row: their shape's donor is still in the GPU's hands).
- **Learned shapes:** the shapes allocated without a donor in the 2 s after each load are counted.
  A shape is format, type, size, mips, layers, samples, colour or depth. The count kept per shape is
  the largest seen after one load.
- **Made ahead:** during the next loading screen, from its 30th frame (after YEBIS's rebuild at its
  start), a background thread makes ready images of those shapes. Each is the Vulkan image and its
  memory, made as `PrepareCreated` makes a texture's image on the game's thread. Limits: the count
  per shape, `BB_READY_SURFACES_MB` (256) in all, and only while VRAM is comfortable.
- **Taken:** a new surface with no dead image of its shape takes a ready one of its shape, as beta
  8's pool takes a dead target's (`t_adopt_image`, only when the creation info is equal; otherwise
  it allocates as now). Ready images not taken 3 s after the load are freed.
- **The session's first load** has nothing learned. Its surfaces are made during its own loading
  screen anyway (beta 13: 194 in 35.9 ms, hidden).
- **Why not at the game's create call:** a target's image takes its shape from the draw that writes
  it, which is why beta 7 makes only sampled textures at creation. YEBIS's set after a load is
  created after the screen ends, a frame before its first draw.
- **Expected:** the 11.8 and 14.3 ms gone from those frames on NVIDIA.
- **Risks:** up to 256 MiB more VRAM for a few seconds at loads; a wrong guess costs an
  allocation and its freeing.

**Part 4: the create calls tell the engine before they copy** (`BB_COPY_NOTICE=0`: the copy faults,
as before)

- **The sites (1.09):** both creators copy the caller's data with the game's `memcpy`
  (`0x261a1b0`), each from one call site:
  - CreateBuffer's helper `0x216d420`, at `0x216d5ea`: destination `r14`, source `[rbx]`, size
    `[r12]`;
  - CreateTexture's helper `0x2171d60`, at `0x2171fea`: destination `r13`, source `[r14]`, size
    `[rbp-0xd0]`.
- **A cave at each site** in place of the three argument loads and the call (15 and 18 bytes,
  compared with 1.09's before placing, as the hit cap's cave). It loads the arguments, calls the
  engine with the destination and size, calls the `memcpy`, and jumps back.
- **The engine, on the game's thread:**
  - For the pages wholly inside the copy: the GPU's older data is dropped without reading it back
    (the copy overwrites every byte), and uploaded copies are marked to upload. Both become
    writable. This is what `CallsCreate` does to the inside pages after the creation today.
  - Images watching those pages are told once for the range, which each page's fault does now.
  - The first and last page keep their watch: other bytes share them.
- **Expected:** the create calls' faults (1,098 and 497 in beta 13's 7 minutes) down to their edge
  pages, the about 930 readbacks gone, and the creators' time in loading screens with them.
- **Risk:** small. The cave runs on every creation with data (2,520 buffers a minute at a load):
  one call into the engine each, and a lookup when no page is watched.

**Part 5: what each load's first frames still did** (always on, one line per load, 2 s after it
ends), for example:

    Loading: the 2 s after m33_00_00_00: frames over 25 ms 1 (worst 45.2 ms); the game's buffers made during the screen 287 MB, uploaded during it 287 MB, at their first draw 0 MB; sparse binds during the screen 22.1 ms, after it 0.4 ms; surfaces ready 21 (94 MiB), taken 19, allocated at their draw 2 (1.3 ms), freed untaken 2; pipelines 48 new, compiled 1,392 ms over 0.31 s on up to 10 threads, waits 40 (longest 132 ms); create-call copies told 2,611, pages dropped without a readback 410

**Not in beta 14: pipelines for areas this PC has never drawn.** The roadmap planned to build an
area's pipelines during its loading screen from the game's calls. The research says it cannot:

- **A pipeline** is a vertex shader, a pixel shader and the draw's state: vertex formats, target
  formats, blending, depth (the key, `GraphicsPipelineKey`). The game creates all its shaders at
  startup (13,277) and pairs them only at the draw.
- **What the game gives at a loading screen:** input layouts (646 in the first load). Each is one
  vertex shader with its vertex formats, not its pixel shader, its targets or its blending.
- **Translating the predicted variants at startup** saves little. In the second AMD run's `m34`
  minute, 228 shaders took 411 ms to translate; one frame's compiles took 1,392 ms. A translated
  variant still needs the pairing.
- **What this PC has drawn** is in its GPU cache, built at the start: the second AMD run built 220
  of 224 in 0.2 s. Tagging keys by area would change nothing. A cold cache therefore means areas
  this PC has never drawn: a first install, another kind of GPU, or a profile change.
- **For those, keys can only come from another PC: a catalog.** Two pieces of work use the lab's
  catalog seeding (`pipeline_compiles.md`) and go together after beta 14:
  - whether HW ships catalogs of its own, the tester's parked decision;
  - keeping a cache's states and keys over a profile change.
- **`pipeline_compiles.md`'s "Remaining (not done)",** looked at:
  - catalog coverage, the profile change, the scene-curve folder, and AMD and Intel catalogs are
    all that catalog work;
  - the warm-up's state rebuilt on the main thread costs 0.1 s now: not needed.

**Moves on when** (changed with the tester's agreement, as the old goal needs keys from elsewhere):
compile bursts shorter by the burst threads' factor where the driver compiles in parallel, and the
frames after loads without parts 2 to 4's work.

**The run** (on the 9070 XT):

1. The usual route with loads (the Dream, `m34`, `m33` or `m23`): part 5's line for each load.
2. New ground, or the cache cleared, for the bursts.
3. Part 3's time, on the 3070 Ti if it is at hand (the NVIDIA checks' row).

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
- "Points 1 and 2 aren't needed" (2026-11-09, on beta 13's two depth items): beta 13 is beta 12's
  follow-ups without depth work.
- "we haven't made the shader building on game start and in loading": beta 14, measured in beta 8.
- "prepare the full plan/documentation so that we don't lose on or forget any steps": this document
  and plan.md's register.

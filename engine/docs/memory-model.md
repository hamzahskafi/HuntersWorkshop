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
| Images found by address and shape; a stack of guesses decides whose an image is (retired when something newer is made over it, dead-by-reuse, the 3 s grace, the same-memory hand-over, hashing 32-37 MiB of memory a minute to tell overwritten drawings) | Rebuilt targets mostly miss the hand-over (8-36 kept a minute against about 280 a rebuild); stale images; CPU time | Beta 10 |
| Every rebuilt target is a new Vulkan image, and the old set waits out the grace | About 4 ms of allocation a rebuild, both sets in VRAM for seconds (YEBIS made 744-775 MiB a minute in beta 7's log) | Beta 8, part 3 |
| A depth target's first use reads memory; its compression metadata (HTILE) is followed by address | Part of the rebuilds' uploads | Beta 8 part 1, beta 11 |
| The game's buffers are write-protected and re-read by page | Write faults and re-uploads | Beta 12 |
| Shaders translated at a draw; pipelines a new area needs built in play | Compile stutter with a cold cache | Beta 13 |

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
     HTILE, as now. Where none is pending, the target is cleared and counted (beta 11 uses that
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
- **Run C** (any time before beta 13): the shader cache cleared, the same route.

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

- Part 4 on by default, for the kinds run A showed unwritten (textures made with their data and
  the game's targets, if run B shows nothing wrong).
- A surface takes the GPU's buffer writes to its memory only when they came after its creation
  (run A: 21-32 surfaces a minute, up to 108 MiB, filled from a previous owner's writes).
- Each writer run A found gets its call hooked instead: YEBIS's Map, update and unmap of its 2D
  textures and Scaleform's texture unmap, as bbhost does.
- Pages stay protected only for the engine's own data rings, YEBIS's buffers and memory no call
  covers.
- **The design note for beta 10** is written here and gone through with the tester before any code:
  the lookup at a draw and a bind, the retire and successor rules, copies, the old path's switch.

**Moves on when:** no stale or broken texture in the tester's runs, and the design note is agreed.

### Beta 10: images belong to their textures

- **At a draw or a bind,** the registry names the resource that owns the memory. An image is that
  resource's.
- **Destroying a resource** retires its image once the GPU is done with it. The image passes only
  to a resource made over its memory without data (rule 1).
- **A whole texture copied** from one the GPU wrote is copied image to image, so the copy carries
  the GPU's work.
- **Removed:** retiring an image when something newer is made over it, dead-by-reuse, the grace,
  hashing memory to tell overwritten drawings, address guessing for game resources.
- The old path behind a switch for this beta only.

**Moves on when:** no broken surface in the tester's runs (the wrong tombstone and white streak of
0.4.5 betas 17 and 18, fixed in beta 19, are the kind to watch for), and stale images stay near zero.

### Beta 11: depth buffers

- A depth target's compression metadata (HTILE) and its pending clear follow the owning resource,
  not the address.
- A depth target made without data starts from the game's clear. Beta 8's count says how often there
  was none.

**Moves on when:** no depth artefacts (shadows, the lamp faces, the scene's depth copies) in the
tester's runs.

### Beta 12: the game's buffers by the calls

- GX's buffer creation with data marks the memory it fills.
- Map (types 0-2 in place, 3 a renamed copy kept in GX's rename table at ctx + `0xb6f8`, 4 that copy
  again) tells which data changed. The device-local copies of vertex and index data are updated only
  when told.
- Pages stay protected only for the engine's own data rings and YEBIS's buffers.
- Beta 8's fault count on GX buffers decides how far this goes.

**Moves on when:** no geometry or animation errors in the tester's runs; write faults and buffer
re-uploads down.

### Beta 13: shaders built ahead

- GX's input-layout creation hooked; every shader the game creates at startup translated on
  background threads for the variants its vertex formats and the targets predict.
- The pipelines an area needs built during its loading screen.
- The exact design from beta 8's shader numbers and run C.
- `pipeline_compiles.md`'s "Remaining (not done)" items are looked at with it: catalog coverage, the
  warm-up's state rebuilt on the main thread, a profile change emptying the cache, the scene-curve
  cache folder, AMD and Intel catalogs.
- Independent of betas 9 to 12: it moves earlier if beta 8's numbers make compiles the bigger
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
  game's buffers are betas 11 and 12.
- "we haven't made the shader building on game start and in loading": beta 13, measured in beta 8.
- "prepare the full plan/documentation so that we don't lose on or forget any steps": this document
  and plan.md's register.

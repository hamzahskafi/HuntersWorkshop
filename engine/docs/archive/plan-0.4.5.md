# The plan to 0.4.5 and the start of 0.4.6 (archived)

The "Now: performance" section of [plan.md](../plan.md) as it stood on 2026-11-08, before the 0.4.6
roadmap replaced it: step 1 (the GPU steps, done and released as 0.4.5), the rest of 0.4.5, step 2
(shader code), step 3 (the memory cleanup, which became 0.4.6 betas 1 to 7) and step 4 (the CPU).
Kept word for word. Every item in it still open is carried in plan.md's register; the 0.4.6 work is
in [memory-model.md](../memory-model.md). Links inside point one folder up (`../`).

The plan's "Where it stands" on the same day:

- **Where it stands:** 0.4.5 was released on 2026-11-07 from beta 13 (`a6b34b8`, engine id 84):
  step 1, the GPU steps, +8.4% at the lamp spot against 0.4.4 (release-notes-0.4.5.md). 0.4.6 is in
  beta: beta 1 (engine id 85) is what was tested as 0.4.5 betas 14 to 19, the VRAM and texture work
  (step 3 below; release-notes-0.4.6.md); beta 3 (engine id 87) makes `BB_VRAM_MANAGER=0` a test of
  the game's own creates, destroys and releases alone, at any VRAM level; beta 4 (engine id 88)
  follows them as they happen, with no time budget: textures built in the game's own create call,
  destroys and releases freed as soon as the GPU is done; beta 5 (engine id 89) is the memory design
  below ("The 0.4.6 memory design"), with the logging that tests it, and the VRAM manager off by
  default; beta 6 (engine id 90) follows the textures the game makes outside GX (YEBIS, Scaleform)
  and frees the GPU's stale drawings the game's memory has moved on from; beta 7 (engine id 91) keeps
  YEBIS's target images across its rebuilds and prepares sampled textures made without data at
  creation. 0.4.4 was released on 2026-11-03 (`cbec5a2`, engine id 64).

## Now: performance

The work after 0.4.4, in order: the tester's call (2026-11-05) puts fewer GPU steps first and the
memory cleanup after them. The evidence for each item is in [performance.md](../performance.md).

- **The targets:** time back at the busiest spots without changing a setting or a pixel, then
  smoothness at every frame rate.
- **Each change behind a switch while it is tested;** its gain is the lamp spot's FPS with it on and
  off. Settled switches are removed with the tester's agreement (beta 7: `BB_SKIP_EMPTY_DRAWS`,
  `BB_SHADOW_CLEAR_FOLD`).

**Step 1: fewer GPU steps (done, betas 1–12; beta 13 the cleanup).** Each change gives the same picture.

- **Done:**
  - **The empty full-screen draws, 0.1 ms** (beta 1, `Rasterizer::SkipEmptyDraw`): GCN's in-place
    depth expansions, which change nothing on the PC.
  - **Lamp shadow faces drawn into their blur's scratch image, the copy left out, 0.24 ms** (beta 4,
    `ShadowRedirect`). Its switch `BB_SHADOW_COPY_SKIP` removed in beta 13 (settled).
  - **Their clears folded into the face's render pass, 0.12–0.17 ms** (beta 5; beta 6 keeps the
    fold inside a pass smaller than the face).
  - **Beta 7, the cleanup:** the copy-neighbour tally (beta 2), the shadow trace (beta 3) and the
    empty draws' register log (beta 1) removed once answered, with two settled switches. Kept: the
    counters (`GPU steps per second (0.4.5)`), the profile labels for indirect dispatches and image
    clears, the put-back safety net.
- **Closed:** `a954e79d` (beta 1's one step gained nothing: its 0.21 ms was the indirect dispatch
  after it) and that dispatch, `2da7fe60`: the game's particle update, cleanly translated, real work
  (performance.md).
- **The copy shader, done (betas 8–12, 2026-11-06):** its copies held to their pass's end (beta 9)
  and merged into one dispatch by GPU address, a dispatch's batches together (betas 10–12). Lamp
  spot 6.98 to 6.83 ms a frame; in the tester's three-build scene 0.13 ms before beta 12; barriers
  44 to 23 a frame. What it cost before, and how it was found, below.
- **Left, with the tester's say:**
  - **The copy shader (closed, kept for the record):** 0.35–0.45 ms a frame in every scene of play (60–78 dispatches), and 20–25
    render passes a frame begun again after one of them. Moving a pass's copies ahead of it, in
    fewer dispatches, needs to know that no earlier draw in the pass touches what they copy, and to
    place work ahead of commands already recorded: the riskiest change of step 1. Beta 8 traces one
    frame's copies in their passes (`vk_copy_trace.h`, removed in beta 13); then the decision.
    Estimate 0.15–0.3 ms, everywhere.
    - **Beta 8's trace (2026-11-05, performance.md):** 53 copy dispatches a frame, 25 inside a pass
      and 28 at its end; every copy inside could move to its pass's start, and **no draw later in
      the same pass reads what any copy wrote**. So the copies can wait for the pass's end instead:
      nothing goes ahead of recorded commands.
    - **Built as beta 9 (the tester's go-ahead): copies held to the pass's end.** A copy-shader
      dispatch that comes inside a pass leaves it open; its copies are recorded where the pass ends
      anyway (other targets, an upload, a barrier, a submission), before what ended it. A draw or
      dispatch that binds bytes a held copy writes, or writes bytes it reads, sends the held copies
      first (the pass ends, as today). Copies reading the game's memory in place are not held. A
      switch for the A/B, and counters. Then beta 10: a pass's held copies in one dispatch by buffer
      address (188 copy commands a frame to about 30).
    - **What others do (read 2026-11-05).** **bbhost** (droogie, `bdc5cdf`): copy-backs of renamed
      dynamic buffers end 55% of its draw runs, and it makes them *copy versions*: the bytes go into
      a staging copy at the copy, the draws after it bind that copy, and the copies into place wait
      for the next transfer batch, dispatch or submission, with no pass end or barrier at the copy
      (`gpu_internal.h`, `docs/list-resolve.md`). It also merges copies into runs (`copy_runs.h`)
      and leaves image copies unmade by sending their reads elsewhere (`copy_elide.h`, the general
      form of beta 4's shadow redirect). **bbport 0.7** (deadinside28, `BB_COPY_SKIP_SAME`, on by
      default): of each copy shader call only the copies that change their destination run (the
      last copy into exactly it wrote the same bytes, nothing copied over it since), checked on a
      thread of its own; a call left with none is left out. +1–4% in heavy views. Built on its own
      buffer code (stage A); for HW it would compare the source bytes when the copy is recorded.
      **shadPS4 upstream**: one `vkCmdCopyBuffer` per buffer pair; HW is past it.
    - **What HW measured before:** a render pass begun again with no barrier costs a few µs (0.4.4
      beta 21), so the restarts are not the cost; the copies' own dispatches and barriers are (about
      5 µs a step). Moving copies pays only if they then run in fewer dispatches.
    - **The choice, after the trace:** (a) a pass's movable copies in one dispatch at its start;
      (b) bbhost's copy versions, no pass end at all (larger: the draws after a copy bind its
      staging copy); (c) bbport's copies that change nothing left out, which first needs a count of
      how many change nothing.
  - **Smaller items, moved to "The rest of 0.4.5" below:** `mem-4`; `upscale-2` and `-3` (0.29 ms at
    Quality, nothing at DLAA); the GPU idle between submissions (about 0.1 ms, 3 a frame).
- **The result** (0.4.4 against beta 12 in the same scenes, 2026-11-06): the lamp spot 135.0 to
  146.4 FPS (+8.4%), a second spot 122.3 to 131.0 (+7.1%), the Hunter's Dream (late game, few
  lamps, ~970 draws) 146.3 to 149.8 (+2.4%). The tester saw GPU use 96 to 98% and power 245 to 258 W
  at the second spot: less energy a frame (2.00 to 1.97 J), more of the GPU busy at once.

**The rest of 0.4.5, in order** (the tester's choice, 2026-11-06: 0.4.5 goes on as betas; stable
when the tester says).

1. **Beta 13, the cleanup:** beta 8's copy trace and `BB_SHADOW_COPY_SKIP` removed. `BB_COPY_HOLD`
   and `BB_COPY_MERGE` stay through the stable release (the merged copies' shader has run on the
   tester's NVIDIA card only), then go.
2. **The barriers that remain:** about 23 a frame at the lamp spot. A measurement beta: reads
   tracked exactly instead of by 4 KiB (`BbToggle::CoarseReadTracking`) behind a switch in Extra
   variables, with the barrier counts by what they wait for; the same scene with and without. Built
   into a feature only if the false waits are many and the CPU cost of exact tracking small.
3. **The GPU idle between submissions:** about 0.12 ms a frame in 3.3 gaps (the Hunter's Dream's
   profile): why the GPU runs out of work there, and whether the submissions can overlap.
4. **Step 2, shader code** (below), with the Hunter's Dream as its scene: there DLAA's 0.96 ms, the
   G-buffer pass's 0.74 ms and the lighting passes are most of the frame.
5. **Step 3, the memory cleanup: beta 16** (below). Its test: a normal session of about 30 minutes
   with lamp travel, and one with `BB_VRAM_TEST_BUDGET_MB=3500` in Extra variables (a 4 GB card), a
   long route through several areas. Read: the frames after loading screens (Frame stats' worst
   frame and the Stall lines' images registered), `GX follow` (built, drawn, misses), the VRAM
   manager's minute. **Next, by the numbers:** the misses' descriptions if many; the game's data
   followed the same way (buffers built ahead, freed on release: the `GX research` line says how
   much of it sits in VRAM); shaders translated when the game creates them, if the shader counts and
   the post-load shader work say it is worth it.
6. `mem-4` and `upscale-2`/`-3` when one of the above passes near them (0.1 ms at most; the second
   only at reduced upscaler presets).

**Step 2: shader code.** It acts on the shading, the part of the frame that scales with pixels
(1.5–2 ms at 1080p).

- Rank the game's shaders by GPU time, then check the top ones with `bb-shader-gpu-stats`
  (`native_shader_compiler.md`). Look at registers and occupancy, inputs still interpolated by GCN's
  formula (321 of 1,201), and the legacy multiply.
- Vertex shader contraction with an invariant position (`shader-5`, about 4% of vertex code).
- The native compiler's step 2 (control flow without goto variables): measured in the lab first, and
  built only if the hot shaders gain.

**Step 3: the memory cleanup, for smoothness at every frame rate.**

- **The tester's session with it off** (2026-11-06, performance.md "With the cleanup off"): no long
  frame in play, write faults gone (median 22.5 a second to 0), the game's data level at 1.4 GB,
  VRAM at most 54% of the driver's budget. So on a card with room the cleanup only costs.
- **Beta 14 (the tester's call, 2026-11-06): one VRAM manager** (`vram_policy.h`) for the texture
  cache and the buffer cache, in place of the first step proposed (the age cleanup only above 70%).
  With room in VRAM (below 80% of the driver's budget) nothing leaves for its age, textures (until
  then 20 s on cards up to about 10 GB) or the game's data (180 s). From 80% the longest unused goes
  first, both alike, in steps (5 minutes, 2, 1, 30 s) until 75%; from 90% 10 s then 1 s, and
  textures the GPU drew are stored into the game's memory without the GPU wait. The launcher's two
  VRAM rows are one, "Use less VRAM" (65%). The points below remain for smaller cards' logs.
- **Beta 15 (the tester's call, 2026-11-06, on beta 14's log):** beta 14 overshot (freed memory
  shows in the driver's count only once whole allocations go back) and its single blocks of the
  game's data gave nothing back. So the manager learns where the player is (areas from the files
  the game opens, visits, scores and the likely next areas, nothing hard-coded), keeps three lines
  (comfortable 70, tight 80, full 90), frees at loading screens and only with slack in play, takes
  garbage first and whole 64 MiB chunks, and logs refaults and what went back to the driver. It also
  listens to the game's GX texture and buffer calls (research, logged only).
- **Beta 17 (2026-11-07, on beta 16's logs):** loads after the first: 1 of 10 with a frame over 33
  ms (beta 15: 11 of 20). The session's first load translated about 40 shaders every session (the
  ordinary permutations motion draws asked for only to look at their buffers): asked with motion
  first now. Building ahead paced per 16 ms; the game's data in released buffers freed.
- **Beta 16 (the tester's call, 2026-11-07, on beta 15's logs):** the area tracker removed (it would
  work against the game, which says what it creates and destroys); the texture cache follows the
  game's textures: built during the loading screen that creates them (from the T# its graphics layer
  builds, as bbhost does), freed when it destroys them. The VRAM manager stays for what the game does
  not say and for cards without the room.

- **The 0.4.6 memory design (beta 5, the tester's call on beta 4's logs, 2026-11-07: "all in one
  beta, and the findings will inform us").** The game's memory is managed as the PS4 manages it, the
  engine's caches following the game's own calls rather than guarding against them:
  - **The game's resources are known whole** (`gx_resources.cpp`): creators, destructors, Map (types
    0-2 in place, 3 a renamed copy of a buffer from GX's pool, 4 that copy again, read in 1.09), the
    render-target and depth views, and the 1D textures with their destructors.
  - **Memory belongs to the newest resource in it:** an image wholly inside the memory of a resource
    the game made after it is no one's and goes once the GPU is done (`ProcessGxCreated`,
    `dead_by_reuse`); one the GPU drew and used from the pass before is kept.
  - **A release frees nothing** (beta 4's eviction on release gave 82-201 ms frames at loading
    screens); the game's data stays in VRAM until the game unmaps its memory, and the unbinds then go
    one per run of blocks, not per 64 KiB block; the cache's own marking is no longer counted as the
    game's write faults.
  - **Textures made without their data** get their image at their first Map, registered without an
    upload (the first draw uploads it); every new image's first upload takes no hashes.
  - **VRAM full:** an image goes to system memory (over the budget if it must) instead of stopping.
  - **The VRAM manager is off by default** (the tester's call); `BB_VRAM_MANAGER=1`, Use less VRAM or
    safe mode turn it on, its rules unchanged.
  - **The logging that tests it:** who holds the card when the budget moves (Windows' per-process GPU
    memory counters), images draws still make and stale images by the game resource over them (the
    largest five described), Map and view counts, sparse bind calls and their time (minute line,
    hitch reports, `BB_FRAME_STATS`), images in system memory.
  - **Beta 6 (on beta 5's log, 2026-11-08):** about 1.1 GB of stale images were the GPU's drawings
    over memory no GX resource held (1920x1080 targets, unused for minutes), and two in-play hitches
    were bursts of textures there too (186 and 57). bbhost had found why: YEBIS (the post-processing
    middleware) has its own device, and Scaleform (the UI) its own textures. Beta 6 follows YEBIS's 2D
    textures and Scaleform's planes with memory of their own as the game's resources (1.09 sites
    checked), drops resources whose memory the game unmaps, frees a stale image the GPU drew once the
    memory under it was written over (what its next use would upload over), retires a destroyed
    texture's image as soon as any new resource overlaps it, and stops following Map (only buffers,
    always renamed). The minute log describes the largest images draws still make over no resource,
    with the mapping each lies in.
  - **The two slow areas of beta 5's run** (53-57 FPS for 10-20 s after arriving in m23 and m24): the
    GPU busy (idle 1-3 ms a frame against 10-12), not shaders (none compiled); the second came as
    VRAM reached 91% of the budget. Beta 6's freeing should tell whether VRAM was the cause.
  - **Beta 7 (on beta 6's log, 2026-11-08: the two slow areas at 60 FPS with the GPU idle 10-11 ms a
    frame, stale images under 1 MiB, images 0.9-1.4 GB against 2-2.5, VRAM at most 72% of the budget;
    the budget steps between 7,249 and 5,859 MiB as the game's own use crosses about 3.9 GB, nothing
    else moving):** both hitches left were YEBIS rebuilding its targets (158 and 175 images made at a
    draw, 183 and 261 MiB uploaded; the second came with a controller connecting). YEBIS's re-created
    target now keeps the image of the one it replaces when memory and shape match (a destroyed YEBIS
    texture's images wait 3 s for it), and a GX texture made without its data that is sampled only
    (bind flags +0x3b: 0x8 without 0x20, 0x40 or 0x80) gets its image at creation, registered without
    an upload (after some loads draws made about 1,000 of these a minute).
  - **Next (the logs decide):** the game's area prefetch (it was made for a hard disk: its model can
    drive the loads), the shader building and its threads, bbport's no-sparse memory module (the
    Linux phase).
- **Evict by the VRAM budget, not by age alone.** Done in beta 14 (above).
- **Paced:** in play, at most a set amount a second (beta 15: 12 ms a second and one 64 MiB chunk of
  the game's data, only while frames have slack); loading screens trim down to the comfortable line
  (beta 15).
- **The bind waits only for the work that used the evicted blocks.** This needs a last-use tick per
  block; until then, the unbinds go in batches on loading screens.
- **With it, the counters:** MiB and blocks per cleanup, stage B's time in it, bind waits, and blocks
  coming back within 10 minutes (the churn). Beta 15: freed by kind, returned to the driver at once,
  and what came back within 5 minutes (refaults, which raise the comfortable line).
- **Expected:** 3 of the 4 long-frame minutes of the 217 gone, with the uploads and write faults of
  the churn.
- **Later, if the logs still show stutter after loads:** eFMann's ideas
  ([archive/hitches-2026-10.md](hitches-2026-10.md), "Smoothness leads").

**Step 4: the CPU, for other PCs.**

- Stage B on two threads (`parallel-stageb.md`, approach B), once a 6-core CPU or a frame rate above
  about 230 FPS makes it the limit.
- The small CPU items with it: `idle-1`, `pipe-1`, `asc-2` and `rt-1`.

**Later, if at all:**

- A GPU timer at pass ends that doesn't wait at each label, if an item's gain needs a finer measure
  than FPS.
- Serve the copy shader's ranges from the engine's snapshots, so no copy runs (not the PC memory
  model; see performance.md).
- Draws built from the game's GX objects and barriers taken from its drain calls, as bbhost does: a
  renderer of a different kind, not a step of this plan.

**Not reopened:**

- `seg-1`, `renderer-3`, `renderer-4`, `renderer-6` and the refuted ideas (`engine-map.md` 5).
- The October hitch leads (the game's job threads, the online summon, the PC memory model): fixed,
  removed, or not the cause in 0.4.4's logs.

### Waiting on the tester

- **0.4.6 beta 7's runs** (step 3, "The 0.4.6 memory design"): a normal session with loading screens,
  and the minute lines named there.

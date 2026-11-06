# Performance after 0.4.4: what the logs show

The findings behind the performance work in [plan.md](plan.md) ("Now: performance").

**Where they come from:** the tester's PC, an RTX 3070 Ti (8 GB) and an i5-14600KF, with a 60 Hz
display. The settings are DLAA at 1080p and Model detail Highest, on 0.4.4 (engine id 64) unless
a line says otherwise. Written on 2026-11-04 and 05 from the tester's logs:

- six sessions of play at 60 FPS: 217 minutes, 2026-11-03;
- 94 sessions with the GPU profile on: 2,671 five-second windows, 0.4.4 betas and stable;
- four runs at the lamp spot under the 240 limit, 2026-11-05.

## In short

- **Smoothness at 60 FPS.** Almost every long frame left comes from one mechanism: the VRAM
  cleanup of the game's data unused for 3 minutes. With it off (35 minutes, 2026-11-06), no long
  frame in play and no write faults; see "With the cleanup off".
- **Frame rate above about 130 FPS.** The GPU is the limit, and not because of the pixels it
  draws.
  - **About 240 small steps a frame:** most of the GPU's frame is a chain of small steps, each
    with a fixed cost of about 5 µs whatever its size.
  - **The largest single item** is the dynamic lights' shadows: 1.3–1.5 ms of a 7.4 ms frame.
- **The CPU has room on this PC.** The game's thread waits 5.6 ms of a 7.4 ms frame for the GPU.
- **What engine work can take back, with the settings unchanged:** about 0.5–0.7 ms at the lamp
  spot (135 to about 145 FPS), and the cleanup's long frames. 0.4.5 beta 1 took the first 0.1 ms
  (135.0 to 137.0 FPS).

## Smoothness at 60 FPS

**Six sessions, 217 minutes of play** (`20261103_040727` to `20261103_224104`):

- 5.8 frames over 33 ms an hour.
- Only 4 minutes had a frame of 50 ms or more:
  - three of them (61, 119 and 213 ms) fell in a minute where the VRAM cleanup moved 260–880 MiB
    out;
  - the fourth (85 ms) came right after a load.
- None of the 137 minutes with neither a load nor a cleanup of 100 MiB had one.
- **Write faults:** median 27 a second, 90th percentile 201. The only 3 minutes over 1,000 a
  second (2,103–2,913) all had a large cleanup.
- The log reports the cleanup only at each 256 MiB and at most 20 times, so the minute it is
  charged to is approximate.

**The cleanup churns.** The hour-long session `20261103_220626` (the Hunter's Dream and two areas, game
maps m26 and m32, m32 loaded 8 times; not chalice dungeons, as this read before):

- 3.6 GB of the game's data left VRAM for being unused 180 s, while VRAM in use stayed at
  2.9–4.2 GB of 7.2.
- The game's data in VRAM never passed 1.5 GB, so most of it left more than once. Each return
  was an upload.

## With the cleanup off

**One session, 35 minutes at 60 FPS** (`20261106_032525`, beta 13, `BB_VRAM_CLEANUP_SECONDS=0`; the
DLC's areas and the Hunter's Dream, 7 loads; frame statistics, GPU time and hitch reports on, so
heavier logging than the baseline's standard log). Against the November 3rd hour (`20261103_220626`,
0.4.4, the cleanup on):

| | Cleanup off (35 min) | Cleanup on (58 min) |
|---|---|---|
| Frames over 33 ms | 2 (3.4 an hour) | 9 (9.3 an hour) |
| Minutes with a frame of 50 ms or more | 1 (69 ms, right after a 3.4 s load: 16 pipelines built, 400 MB of uploads) | 3 (119, 213 and 85 ms) |
| Long frames in play, away from loads | none | the cleanup's |
| Write faults a second (median, 90th percentile, most) | 0, 0, 10 | 22.5, 407, 2,913 |
| VRAM in use, most (of 7,249 MiB) | 3,932 MiB | 4,189 MiB |
| The game's data in VRAM, most | 1,408 MiB | 1,472 MiB |

- **The game's data stops growing by itself:** it reached 1.4 GB and stayed, about what the
  baseline peaked at with the cleanup on. Keeping it cost nothing on this card (VRAM 54% of the
  driver's budget at most).
- **The write faults were the cleanup's:** blocks freed and loaded back are watched again, so the
  game's next writes to them fault. With nothing freed there were none.
- **Pacing:** stddev 0.11 ms (median of the 5 s windows), p99 17.0 ms. The other frames over 20 ms
  were the windows after loads, with new areas' shaders compiled (712 compiles, 0.66 s in all,
  the DLC's areas not in the pipeline cache yet).
- **The hitches, all of them shader compiles:** no hitch report was written (the watcher is quiet
  for 20 s at the start, during loading screens and for 10 s after each); both frames over 40 ms
  fell there. All 9 windows with a frame over 20 ms are in the first 6.5 minutes, each with
  pipelines compiled in it (the 69 ms frame: 16 built after a 3.4 s load); none after minute 7.
  The pipeline cache had been empty since beta 11's first session at 01:34 (its files missing:
  "Pipeline cache cleared (0 files)"), and the short runs since rebuilt only about 290 pipelines
  at three spots, so this session built the DLC's: 712 compiles, 438 in the first 3 minutes.
- **Caveats:** 35 minutes, not an hour; other areas than the baseline's; more logging on.

## The VRAM cleanup, in the code

The cleanup is `EvictIdleCopies` (`buffer_cache.cpp`). It runs from `Maintain` on stage B, every
10 s while VRAM has room: level 0 of `UpdateVramLevel`, below 85% of the driver's budget.

- **What leaves:** every block of the game's data unused for 180 s (`BB_VRAM_CLEANUP_SECONDS`).
  Each block is unbound from the arena with a sparse unbind of its own (`EvictVramBlock`), and
  its pages are marked CPU-modified.
- **The bind waits:** the next submission's sparse bind waits for all the work submitted before
  it (`bind_wait_tick`, `SubmitPendingArenaBinds`). So the GPU's queue runs dry in that frame.
- **A block comes back whole:** when it is used again, its whole 2 MiB group is bound and uploaded
  again (`EnsureResident`). Its pages are watched again, so the game's next writes to them fault.
- **Why it exists:** bbport added it for 8 GB laptops whose VRAM grew from 2.6 to 5 GB in 90
  minutes. On this PC, 3 GB of VRAM was free whenever it ran.
- **Not the launcher's "VRAM cleanup":** that row on the Engine page is `BB_IMAGE_WRITEBACK_GC`.
  It frees textures the GPU drew, and only when VRAM is short (`texture_cache.cpp`).
  - This cleanup has no launcher row.
  - `BB_VRAM_CLEANUP_SECONDS=0` in Extra variables leaves the game's data in VRAM until VRAM is
    tight (the default is 180).

## Where the GPU's frame goes

### Resolution is a small part

Two pairs compare the same build at the same scene density, rendering at 1280×720 against
1920×1080, which is 56% fewer pixels:

| Pair | 1080p | 720p | Less |
|---|---|---|---|
| DLSS, engine 43, 1,750–1,850 draws | 8.20 ms | 7.28 ms | 11% |
| FSR 3.1, engine 61 (`34a7f0a1`, `cdaacc45`) | 7.22 ms | 6.36 ms | 12% |

- **Most of the frame does not scale with pixels.** Set the upscaler aside (about 1 ms either
  way): about 1.6 ms of the frame scales with pixels, and about 5.6 ms does not. Some
  post-processing stays at 1080p in both runs, so the scaling part is a little larger.
- **The G-buffer passes alone:** 3.35 against 2.26 ms, a third less for 56% fewer pixels.

### A fixed cost per step

At 134 FPS (`20261102_231754`, the profile's top 30 labels), about 240 GPU steps a frame each
cost about 5 µs, whatever their size:

| Step | A frame | Each |
|---|---|---|
| Passes of 32×32, 64×64, 128×128 and 256×256 | 12 of each | 5.8, 5.2, 5.8 and 8.7 µs |
| `a040875f`, 16 workgroups each | 48 | 5.1 µs |
| The copy shader | 64 | 5.8 µs |
| The empty pass at 32768×32768 | 12.6 | 7.9 µs |
| `a954e79d`, one workgroup on one of the GPU's 48 SMs, with what follows it (see below) | 5 | 37 µs |

- **The profiler doesn't create the floor.** It waits at each label, but it costs only 0.1–0.2 ms
  a frame in all (`engine-map.md` 1.4). So the steps already ran one after the other without it.
- **On the PS4 at 30 FPS** the same fixed costs were under 1% of a 33 ms frame. On this GPU at
  134 FPS, a 32×32 pass's work is next to nothing, and its fixed cost is all that is left.

### The four runs at the lamp spot (2026-11-05)

Under the 240 limit; each run changes one thing from the run before it. The figures are at the
first spot (about 1,345 draws a frame); the tester's second spot (about 1,770 draws) gives the
same steps.

| Run (log) | FPS | GPU ms | Draws a frame | Step |
|---|---|---|---|---|
| 1 DLAA, Model detail Highest, dynamic light shadows on (`20261105_024658`) | 134.9 | 7.41 | 1,345 | |
| 2 dynamic light shadows off (`20261105_025037`) | 168.8 | 5.92 | 712 | −1.49 ms (−1.34 at the second spot) |
| 3 and Model detail at the game's default (`20261105_025406`) | 173.4 | 5.77 | 767 | −0.15 ms (−0.32) |
| 4 and TAA instead of DLAA (`20261105_025720`) | 194.9 | 5.13 | 766 | −0.64 ms (−0.68) |

### The frame by item, at 134 FPS

| Item | ms a frame | Removable without a visible change |
|---|---|---|
| The dynamic lights' shadows | 1.3–1.5 (run 2) | about 0.25 by skipping each face's copy, and its clear's unmeasured cost (see below) |
| DLAA, the upscaler's evaluate and merge | 1.03 (TAA 0.45) | nothing; a setting |
| The copy shader | 0.37 or more, plus 20 pass restarts | most of it, with the copies ahead of the pass in fewer dispatches |
| `a954e79d` and the indirect dispatch `2da7fe60` after it | 0.19–0.23, nearly all `2da7fe60`'s | not as steps; `2da7fe60` is real work (step 2) |
| The empty full-screen draws | 0.10–0.13 | all of it |
| GPU idle between submissions | 0.09–0.10 | part of it |
| G-buffer and other full-size passes | 2.4–3.4 | the shading's share, through shader code |
| Model detail Highest over the game's default | 0.15–0.3 | nothing; a setting |

The removable items together are about 0.5–0.7 ms (first written as 1–1.3 ms, with `a954e79d`'s
row counted in and the shadows read as more than one chain).

## What the items are

### The dynamic lights' shadows

- **What they are (read 2026-11-05, from the tester's compiled shaders):** variance shadow maps.
  - **The faces' pixel shader `1ae03d75`** writes the distance `d` and `d² + ¼·(ddx² + ddy²)`. Those
    are the two moments of a variance shadow map, with the derivative term its authors (Donnelly
    and Lauritzen) add. Unlike a plain depth map, moments can be averaged, so the game blurs them
    ahead of time; that gives soft shadow edges that don't shimmer.
  - **The filter's pixel shader `0f61fd27`** is a 13-tap weighted blur: the sum of 13 bilinear
    samples at explicit level 0. Each sample's offset (scaled by a texel size) and weight come from
    two constant buffers. Its vertex shader `0b0acf50` passes a quad's position and texture
    coordinate through.
- **How the game draws them, from 0.4.5 beta 3's trace** (`20261105_053410`, two frames at the lamp
  spot, the same both times):
  - **8 lights, each a cube map** (R16G16F, 6 layers) at the light's own size: two at 256, two at
    128, two at 64 and two at 32. 48 faces a frame. (Read before the trace as 12 faces at four
    levels each, and before that as a 256→128→64→32 chain: both wrong.)
  - **Each face, in order:** the game's compute clear `8b355b5a` of the layer (the engine runs it
    as an image clear, `IsComputeImageClear`); the toolkit's depth clear (`89cda9e4`) of the size's
    scratch depth image; the shadow casters into the cube's layer with that depth; `a040875f`
    copying the whole layer into the size's scratch image (constants: from 0,0 to 0,0, the face's
    size); the blur from the scratch image back into the layer.
  - **One scratch depth image and one scratch blur image per size**, shared by the 12 faces of
    that size's two lights. So each face's steps wait for the face before it, and no step can move
    past another face's: the merge of a face's four levels, written here after beta 2, assumed
    levels that don't exist and is withdrawn.
  - About 5 steps a face besides the casters: 240 a frame. Run 2 (shadows off) drops them with
    about 630 draws.
- **What can still go, exactly, inside each face's chain:**
  - **The copy (about 0.25 ms: 48 a frame, 5.2 µs each), left out from 0.4.5 beta 4**
    (`ShadowRedirect`, its switch `BB_SHADOW_COPY_SKIP` removed in beta 13; measured 0.24 ms, 136.8 to 141.5 FPS at the lamp spot,
    every face redirected, none put back): draw the face into the scratch image instead of the
    cube's layer, clear included. The scratch image then holds what the copy would
    have put there, the copy is skipped, and the blur writes the layer as before. Nothing reads the
    layer between the casters and the blur in either trace. The engine has to decide at the face's
    clear, before it sees the copy, so it would follow the pattern once seen, and copy the scratch
    image into the layer itself if a face ever went otherwise.
  - **The colour clear, folded into the face's render pass** as its load operation (48 a frame).
    Its cost is not known: the engine's image clears have no profile label of their own, so they
    are counted in the label before them (the previous face's blur).
- **The rest of the shadows' cost** is drawing the objects near a light into its faces. That is
  real work, and it stays while the shadows are on.

### The empty full-screen draws (the 32768×32768 pass)

Found on 2026-11-05 in the 1.09 executable.

- **The shader:** its vertex shader `89cda9e4` is one of the game's built-in shaders, at
  0x56bbde0. That is the toolkit block of Sony's graphics library, beside the copy shader
  (`fefebf9f`) and `a040875f`.
- **What it does:** it is 14 instructions, a full-screen rectangle from the vertex number, with
  no memory writes.
- **Who draws with it:** the game's init (0x2ab2030) loads it into 0x586c530. Five toolkit
  functions draw with it: 0x2ab55a0, 0x2ab5a70, 0x2ab5f50 (eight callers in the game), 0x2ab6810
  and 0x2ab6cb0.
- **These draws have no outputs:** no pixel shader, no colour target, and neither depth nor
  stencil test on. The engine runs them today as passes with no attachments, so they already
  change nothing.
- **What they likely do on the PS4:** work in the depth unit's compression data (HTILE), which
  the GPU does as a side effect. This is how GCN drivers expand a depth buffer before it is
  sampled; radeonsi's depth flush does the same. On the PC there is no such data.
- **Not the shadows:** the rate (12.6 a frame) matched the shadow maps, but with the shadows off
  (run 2) it stays at 12.5.
- **Found in 0.4.5 beta 1's log** (`20261105_044628`): all of them are one kind. Depth control 0
  (depth and stencil off) and `DB_RENDER_CONTROL` 0x60: only `DEPTH_COMPRESS_DISABLE` and
  `STENCIL_COMPRESS_DISABLE`, on a valid depth buffer. That is exactly radeonsi's in-place depth
  and stencil expansion (a rectangle with an all-off depth state and those two bits), so the guess
  holds: the PS4 expands the depth buffer's HTILE data before it is read.
- **Measured:** left out from beta 1 on, the 32768×32768 label (0.10–0.12 ms a frame) is gone;
  lamp spot 135.0 to 137.0 FPS (GPU 7.40 to 7.30 ms), second spot 122.2 to 123.7 FPS (8.18 to
  8.08 ms), against 0.4.4's run 1 with the same settings.
- **0.4.5 beta 1 leaves them out** (`Rasterizer::SkipEmptyDraw`): any draw with no colour or depth
  target bound, no stage writing memory (written buffers, GDS included, storage images, image
  atomics, DMA), no motion vector attachment and no occlusion query measuring it. The state the
  engine keeps from a draw's targets is updated as for a recorded draw.
- **The same shader doing real work:** with depth on (256×256 D32, 784×784 D32S8), or with pixel
  shader `38d65b32` (1920×1080 colour), it is the toolkit's clears and copies.

### The copy shader (`fefebf9f`)

- **What it does:** another toolkit shader. It copies small ranges into the game's data in VRAM:
  about 4,300 ranges a frame.
  - **Sources:** mostly snapshots the engine already makes in its stream buffer, 2.9 KiB on
    average. The rest is the GX region, read in place.
  - **On the PC:** 64 dispatches and about 130 copy batches a frame. 42 dispatches a frame land
    inside a render pass, which ends the pass, and 20 passes a frame begin again after one.
- **First option:** gather the copies ahead of the pass and run them in a few larger dispatches,
  whenever they don't read what the pass draws. Only the order and the number of dispatches
  change. Beta 18 counted which copies qualify: at least those with a snapshot source and no
  barrier needed.
  - **Its gain, looked at again for 0.4.5:** about 0.1–0.2 ms. The copies still run; what goes is
    the 20 passes a frame begun again after one. And the pass has already begun when a copy comes,
    so the engine would record the copy before commands already recorded, with the barrier
    tracking following it: the riskiest change of step 1.
- **Beta 8's trace** (the tester's lamp spot, `20261105_235054`, 143 FPS; the 600th and the
  6,000th frame, the same both times): 53 copy-shader dispatches a frame, about 3.5 batches each
  (188 copy commands a frame: 64 dispatches and 124 `vkCmdCopyBuffer`), 0.38 ms in the profile.
  - None at a pass's start; 25–26 inside a pass; 27–28 at a pass's end (after its last draw,
    before other targets). Inside: the sun's shadow (5, in a pass of 66 steps), the G-buffer (9,
    of 239 steps), the lighting (2), the post passes (about 8), the last 2D pass (2). Each lamp
    shadow pass has one, at its end.
  - **Every copy inside a pass could move to its start:** no earlier draw or dispatch of the pass
    read or wrote what it writes, or wrote what it reads.
  - **And no later draw or dispatch of the same pass reads what any copy wrote:** what they copy
    is read by later passes. So the copies can as well wait until the pass ends, the easier
    direction: nothing goes ahead of commands already recorded. It is bbhost's deferral without
    its staging copies, which bbhost needs because its draws read the copies at once.
  - Uniform buffers the GPU command thread copies into the constant ring are not in the trace;
    it copies none whose bytes the GPU writes or will write (`CollectRingBindings`), so none of
    the copies' destinations. Images are not in it either; what fills an image from memory ends
    the pass.
- **Beta 9 holds them to the pass's end** (`vk_copy_hold.h`, `BB_COPY_HOLD=0`: off): recorded where
  the pass ends anyway, in order; a binding that touches one sends them first. Its counters are in
  `GPU steps per second (0.4.5)`. Its logs: no binding ever touched a held copy; restarts 12
  fewer a frame, about 0.05 ms; the copy shader's own time unchanged (0.365 ms): its cost is its
  ~188 copy commands a frame.
- **Beta 10 merges them** (`buffer_address_copy.comp`, `RecordMerged`, `BB_COPY_MERGE=0`: off): one
  dispatch by GPU address for each pass end and each copy-shader dispatch outside a pass, split
  where one batch needs another's result. Its logs: no gain; 86 dispatches and 48 barriers a frame,
  as it split its groups at every barrier. Beta 11 puts one barrier before each group. In the
  tester's three-build scene: 6.74 ms a frame with neither (beta 7), 6.68 held (beta 9), 6.61 held
  and merged (beta 11). Beta 12 lets the batches of one copy-shader dispatch share a group when its
  sources and destinations are apart in the game's memory.
  - **What the barriers are:** most copies write over constants that draws earlier in the frame
    read (or, at the 4 KiB grain reads are tracked at, bytes beside them), so the copy waits for
    those draws to finish. That wait, not the copy commands, looks like the copy shader's cost.
- **Later, if at all: serve the copied ranges from the snapshots,** so no copy runs. That is not
  the PC memory model, which kept all of the game's memory in system RAM and moved blocks in and
  out of VRAM. But the snapshots are reused within a frame or two, so each redirect would need
  tracking: harder, and riskier.

### `a954e79d`

- **What it does** (decoded in beta 18; read in full from the tester's compiled shader on
  2026-11-05): 32 threads in one workgroup, 5 a frame.
  - It reads an item count `N` and computes `groups = (N + 31) / 32`.
  - It writes three sets of indirect dispatch arguments: `groups` with values from its buffer, or 1.
  - Then its 32 threads loop over `groups` in steps of 32, each pass adding 1 to a GDS counter.
    The atomics' results are never read.
- **So its effect is the counter raised by `groups`, and the three argument sets written.** One
  atomic add of `groups` leaves memory exactly as the loop does: the same integers, no rounding.
- **It is not slow; the profile charged it with the work after it** (found with 0.4.5 beta 1).
  The GPU profile labels a step when it begins and charges it until the next label. Indirect
  dispatches had no label of their own, so the dispatches `a954e79d` prepares, which come right
  after it, were counted as its time. Two changes to its atomics moved nothing: beta 19's one
  atomic per subgroup (about 44 µs either way) and beta 1's single atomic (0.211–0.228 ms a frame
  before, 0.212–0.214 after, 5 a frame). The first reading here, thousands of serialized atomics
  while the GPU waits, was wrong.
- **0.4.5 beta 2 labels indirect dispatches** (`dispatch indirect cs ...`), and its profile
  (`20261105_051112`) splits the time: `dispatch indirect cs 2da7fe60`, 5 a frame, 0.117–0.136 ms a
  frame (23–27 µs each), the dispatch `a954e79d` prepares. `a954e79d` itself is no longer among
  the top 30 labels (under 0.065 ms a frame). `2da7fe60`'s groups come from `a954e79d`'s count, so
  its time is work the game asks for; what it does is for step 2 (`BB_SHADER_DISASM=2da7fe60`).
- **`2da7fe60` is the game's particle update** (its code from the tester's log `20261105_224539`,
  23,876 bytes, 4,969 instructions, 32 threads a group, groups from `a954e79d`'s count):
  - each thread takes one particle from a list kept in GDS (`DS_CONSUME`), loads its 176 bytes of
    state (half floats, 11 typed loads) and reads 25 values from two parameter textures;
  - then two loops over lists the game keeps (likely forces and colliders): about 2,000 and 1,700
    instructions an iteration, branchy (118 and 88 branches), 84 reciprocals and square roots;
  - then appends the particle to one of two lists (`DS_APPEND`) and writes its 176 bytes back.
  - Its cost is particles × list entries, work the game asks for; nothing in it is a step the PC
    could drop.
  - **Its translation, read from the tester's GPU cache** (two modules, `0x000000002da7fe60_0.spv`
    of two builds; 2026-11-05): clean. The same two loops (two `OpLoopMerge`), structured control
    flow with no flag variables, the particle lists taken with one atomic per subgroup (the newer
    module; 0.4.4 beta 19), 59 divisions for the reciprocals. The one systematic cost is
    deliberate: all 2,882 float adds, subtracts and multiplies keep `NoContraction`, so they round
    as GCN's unfused `v_mad_f32` does. Pixel shaders let the driver fuse them since 0.4.4
    (`BB_FS_CONTRACT`, about 10% less code); compute shaders don't. Doing the same here might save
    a tenth of its 0.12 ms, and the particles' state carries from frame to frame, so the last-bit
    differences would add up (invisibly). Not taken: `2da7fe60` is closed as real work, cleanly
    translated.
- **Beta 1's one step, removed in beta 2** (`ExecuteGroupCountHLE`, `gds_group_count.comp`, for the
  record): one thread read the count, wrote `(groups, 1, 1)`, and added `groups` to the counter.
  It was checked against the GCN code (beta 18's dump) and the SPIR-V the engine made of it (the
  tester's shader files), and ran 620 times a second without a decline:
  - the count is one dword, read as raw bits (`BUFFER_LOAD_FORMAT_X` of a 32-bit format; the
    `V_ADD_I32` works on the bits);
  - the three stores are 32-bit dwords at element 0, the last `(groups, 1, 1)` (the 1s are stored as
    the float with bits `0x00000001`);
  - `DS_APPEND` has offset 0, so the counter is at `user_data[2] >> 16` in GDS, as m0 = s2 gives.
  - anything else (another thread count, buffer layout, format, or a counter outside GDS) left the
    dispatch to the game's shader.

## The CPU

- **At 134 FPS:** stage B is 57% busy and stage A idle 28%, and the game's thread waits 5.6 ms a
  frame for the GPU.
- **Where the CPU limits:** on 6-core CPUs, and on this PC above about 230 FPS.

## bbhost, for comparison

- **A hint, not a measurement.** At Old Yharnam on an RTX 4070 uncapped, its meter reads 108 FPS
  with the GPU 46% busy, about 4.3 ms of GPU a frame (its `docs/performance.md`). That is another
  place with other settings.
- **Its renderer works a level higher.** It takes its barriers from the game's own GX drain calls
  and builds draws from GX's objects, instead of from the command buffers (its
  `docs/rendering.md`, "Native or emulated").

## Settings, as the runs show them

- **At 60 FPS everything can stay on:** even the lamp spot runs far above 60.
- **For high frame rates:**
  - Shadows from dynamic lights off gains the most: +25% at the lamp spot. The lamps, torches and
    candles then cast no shadows; the sun's and the moon's are not affected.
  - TAA instead of DLAA: 0.65 ms, for a softer picture.
  - Model detail: 0.15–0.3 ms between Highest and the game's default.

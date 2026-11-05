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
  cleanup of the game's data unused for 3 minutes. A session with it off is still to come.
- **Frame rate above about 130 FPS.** The GPU is the limit, and not because of the pixels it
  draws.
  - **About 240 small steps a frame:** most of the GPU's frame is a chain of small steps, each
    with a fixed cost of about 5 µs whatever its size.
  - **The largest single item** is the dynamic lights' shadows: 1.3–1.5 ms of a 7.4 ms frame.
- **The CPU has room on this PC.** The game's thread waits 5.6 ms of a 7.4 ms frame for the GPU.
- **What engine work can take back, with the settings unchanged:** about 1–1.3 ms at the lamp
  spot (135 to about 155 FPS), and the cleanup's long frames.

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

**The cleanup churns.** The hour of chalice dungeons (`20261103_220626`):

- 3.6 GB of the game's data left VRAM for being unused 180 s, while VRAM in use stayed at
  2.9–4.2 GB of 7.2.
- The game's data in VRAM never passed 1.5 GB, so most of it left more than once. Each return
  was an upload.

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
| `a954e79d`, one workgroup on one of the GPU's 48 SMs | 5 | 37 µs |

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
| The dynamic lights' shadows | 1.3–1.5 (run 2) | about 0.5: the filter chains (0.24) and the 48 copies (0.25) as one job each |
| DLAA, the upscaler's evaluate and merge | 1.03 (TAA 0.45) | nothing; a setting |
| The copy shader | 0.37 or more, plus 20 pass restarts | most of it, with the copies ahead of the pass in fewer dispatches |
| `a954e79d` | 0.19–0.23 | most of it, run wide or beside graphics |
| The empty full-screen draws | 0.10–0.13 | all of it |
| GPU idle between submissions | 0.09–0.10 | part of it |
| G-buffer and other full-size passes | 2.4–3.4 | the shading's share, through shader code |
| Model detail Highest over the game's default | 0.15–0.3 | nothing; a setting |

The removable items together are about 1–1.3 ms.

## What the items are

### The dynamic lights' shadows

- **What the logs show:**
  - two-channel half-float images (R16G16F) of 256×256 with 6 layers, which is a cube map, sampled;
    at least two of them (images 129 and 133);
  - each face drawn with depth (`167daba6`/`1ae03d75`, at 64 to 256 a side);
  - each face filtered down 256→128→64→32 by `0b0acf50`/`0f61fd27`: 12 chains a frame, two lights
    times six faces;
  - `a040875f` copying 32×32 rectangles into a 256×256 storage image, 48 a frame (decoded in 0.4.4
    beta 18: an image load and an image store).
- **Confirmed by run 2:** with dynamic light shadows off, the chains, the copies and about 630
  draws a frame are gone.
- **What they are (read 2026-11-05, from the tester's compiled shaders):** variance shadow maps.
  - **The faces' pixel shader `1ae03d75`** writes the distance `d` and `d² + ¼·(ddx² + ddy²)`. Those
    are the two moments of a variance shadow map, with the derivative term its authors (Donnelly
    and Lauritzen) add. Unlike a plain depth map, moments can be averaged, so the game blurs them
    ahead of time; that gives soft shadow edges that don't shimmer.
  - **The filter's pixel shader `0f61fd27`** is a 13-tap weighted blur: the sum of 13 bilinear
    samples at explicit level 0. Each sample's offset (scaled by a texel size) and weight come from
    two constant buffers. The same shader runs at 256, 128, 64 and 32 for every face; only its
    constants differ. Its vertex shader `0b0acf50` passes a quad's position and texture
    coordinate through.
- **The cap:** in 503 profile windows the game never drew more than 17 faces a frame (about three
  lights), so lamps beyond that add no shadow work.
- **Why four passes:** each level is made from the one before it. One pass per level was the way
  to do that when the game was made; single-pass downsampling (AMD's SPD) came about 2020.
- **Why one job can't be bit for bit:** the passes read each level through the GPU's texture unit
  (bilinear filtering in hardware). One job per face would keep the smaller levels on chip and
  filter them with its own arithmetic, which rounds differently in the last bit.
  - Since 0.4.4 the driver may also fuse this shader's multiply-adds (`BB_FS_CONTRACT`), so its
    last bit already depends on the driver.
  - **An exact alternative:** each level's 12 faces in one dispatch, all read through the texture
    unit: 4 steps instead of 48. It needs the engine to hold the game's filter draws back until
    the faces of a level are all known.
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
- **Still to find:** the depth targets they act on, and the depth unit's register bits
  (`DB_RENDER_CONTROL`). 0.4.5 beta 1 logs them for the first kinds it leaves out.
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
- **Why it is slow here:** the PS4 does one on-chip operation per wave. Here every pass is an atomic
  on a storage buffer standing in for GDS, all on one address. With a large `N` that is thousands
  of serialized atomics: 37 µs, while the rest of the GPU waits. Beta 19's one atomic per subgroup
  did not change it (about 44 µs either way).
- **0.4.5 beta 1 runs it as one step** (`ExecuteGroupCountHLE`, `gds_group_count.comp`): one
  thread reads the count, writes `(groups, 1, 1)`, and adds `groups` to the counter. Checked against
  the GCN code (beta 18's dump) and the SPIR-V the engine made of it (the tester's shader files):
  - the count is one dword, read as raw bits (`BUFFER_LOAD_FORMAT_X` of a 32-bit format; the
    `V_ADD_I32` works on the bits);
  - the three stores are 32-bit dwords at element 0, the last `(groups, 1, 1)` (the 1s are stored as
    the float with bits `0x00000001`);
  - `DS_APPEND` has offset 0, so the counter is at `user_data[2] >> 16` in GDS, as m0 = s2 gives.
  - Anything else (another thread count, buffer layout, format, or a counter outside GDS) leaves the
    dispatch to the game's shader, counted.

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

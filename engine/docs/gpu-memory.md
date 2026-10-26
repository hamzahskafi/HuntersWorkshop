# GPU memory: frames late while memory is moved, created or pinned

Written 2026-10-26 for Hunter's Workshop 0.4.0. **Status: set aside.** The PC memory model is off by
default since 0.4.0 beta 4, and the rest of this work is skipped for now. This file records what
was found and how to fix it, in case it's picked up again. Paths are relative to `engine/`.

## Decision

- **The PC memory model (`BB_GUEST_IN_PLACE`) is off by default.** It's the main source of late
  frames in play on the measured PC (RTX 3070 Ti, i5-14600KF, 60 FPS). The CPU it saves, a few
  tenths of a core, is spare at 60 FPS. The launcher toggle stays as an opt-in.
- **The remaining GPU memory work is skipped.** With the model off, frames late in play are rare
  (table below). What's left shows up mostly at loading screens and while an area streams in.
- **Pick it up again if:**
  - logs with the model off show frames over 30 ms in play, away from loading screens; or
  - the model is wanted for its CPU saving: above 60 FPS, or on slower CPUs and handhelds.

## Evidence

| Build, PC memory model | Play | Frames late in play |
|---|---|---|
| 0.3.0, on (5 sessions) | 35 min | 10 five-second windows with a frame over 33 ms (0.29/min) |
| 0.3.0, off (4 sessions) | 57 min | 6 such windows (0.10/min), 2 of them the online summon (fixed in 0.3.0) |
| 0.4.0 beta 3, off (`20261026_031339`) | ~8.5 min, Yharnam | none over 30 ms |
| 0.4.0 beta 3, on (`20261026_033515`) | ~20 min | 11 hitch reports (frames over 30 ms), each inside a burst of the model's moves |

From `20261026_033515`'s once-a-minute `Guest memory, last 60 s` lines:

- **Rebinds:** 135–1,925 `vkQueueBindSparse` calls a minute, 90–98% of them waiting for the
  GPU; 3,741–32,770 ranges a minute.
- **Time in the call:** 7.5–105 ms a minute in `vkQueueBindSparse`, the longest single call 7.8 ms.
- **Moved into VRAM:** up to 1,193 MiB a minute, in bursts. Before each hitch the log shows
  30–40 `Guest memory: … moved` lines in a row.

Reading hitch reports: the reporter suspends every thread and walks its stack twice, 20 ms apart,
which costs about 200 ms. A report triggered at 30–32 ms shows the next frame 177–250 ms later; the
0.3.0 reports show the same (208–246 ms after a 41–42 ms trigger). The real event is the trigger:
here one missed refresh at 60 Hz.

## Background: where the PC memory model came from

- **bbport (deadinside28):** designed as "how a PC release would work". The GPU uses the game's
  memory where it is, in system memory, and keeps the data it reads often in VRAM, instead of
  keeping VRAM copies of everything and catching every CPU write (page protection, faults,
  re-uploads). Off by default in bbport and in DarkIzuku's fork.
- **Lantern (Awaken):** Windows version from 0.9.16, on for everyone from 0.9.37 beta 4; beta 5
  fixed an FPS sink with it. Hunter's Workshop inherited that default.
- **bbport 0.52 and later:** dropped sparse binding for it; its notes say "NVIDIA's sparse binding
  stalls the GPU for seconds". That replacement (the Vulkan layer's memory module, dma-buf memory)
  is Linux-only and comes with bbport's own renderer, so it doesn't port as it stands. HW's
  Windows version is the earlier sparse-arena design.
- **Measured saving (0.3.0 logs):** ~6,000 pages re-protected a second with the model on against
  105,000–129,000 off, 0.2–0.4 of a core.

## The causes, with the code

### 1. The model's moves (model on only)

The game's memory lives in one large sparse buffer, the arena (`buffer_cache/buffer_cache.cpp`).
Each block of it is bound either to the game's own memory, imported ("in place"), or to a VRAM
copy. The model moves blocks between the two as use changes:

- **`QueuePromotions`:** blocks the GPU reads often go from in place to a VRAM copy (loaded
  assets, GPU-written data, blocks back after idling or an evacuation). There's **no budget**:
  every candidate collected since the last submission moves at once (`promote_candidates` is
  emptied in one go). Each promoted block is then uploaded whole at the next packet
  (`ProcessPromoteFills`).
- **`ProcessDemotions`:** blocks the CPU writes go back to in place, so the writes need no
  tracking. Retries of these into VRAM are capped (`BB_DYNAMIC_RETRY`, `BB_DYNAMIC_RETRY_MB`,
  8 MiB/s); promotions have no such cap.
- **`QueueCopyBacks`:** GPU-written blocks copied back before they go in place.
- **`SubmitPendingArenaBinds`:** all of a submission's moves go out as one `vkQueueBindSparse` right
  before it, from a recording thread (`BB_ASYNC_SUBMIT`). The bind waits on the work semaphore for
  `bind_wait_tick`: the previous submission, because commands already recorded still read the old
  binding. The next submission then waits for the bind. So the GPU finishes one submission,
  rebinds, and only then starts the next: the overlap between submissions is lost, and the bind
  call itself can take milliseconds on NVIDIA.

### 2. Pinning the game's memory (model on only)

`gpu/shim/bbport_guest_memory_win.cpp`, `ImportChunk`: the game's direct memory is imported for the
GPU in 256 MiB chunks the first time a block in a chunk is used, on a thread of its own. Each
import pins every page (65,536 page faults, up to a second). In the 0.3.0 logs, 25 of 26 stalls in
a session's first minutes had an import in the same moment, and two in-play stalls did too. Off
the draw thread it still competes for the process's address space.

### 3. Image memory created and freed with each image (both models)

`texture_cache/image.cpp`, `UniqueImage::Create`: images of `BB_VMA_DEDICATED_MB` (default 4) MiB or
more get memory of their own, created when the image is and returned to the driver when it's
freed. bbport chose this to avoid holes in shared VMA blocks (~0.9 GB on an RX 7800 XT). Every
render target and large texture an area streams in is then a `vkAllocateMemory` and a kernel call.
This is the `allocation` figure in the stall lines' `of which:` breakdown, for example 22.9 ms of a
59 ms stall in `20261026_033515` and 35–60 ms in many 0.3.0 stalls.

### 4. Arena residency chunks (both models)

`BufferCache::AllocateResidency`: VRAM copies come out of 64 MiB chunks. Empty chunks go back to the
driver after 5 s idle (`ProcessIdleBlocks`), so the next area allocates them again. A spare chunk
is already created ahead when half the residency is in use (`spare_residency`,
`TakeSpareResidency`), which covers part of this.

### 5. The full-screen image re-upload (both models)

A 1920×1080 linear image of ~9.4 MB that the game's CPU code rewrites (the `Image write sites`
lines name `+0x2085c0c`, called from `+0x11e6846`). Each rewrite is hashed again and uploaded whole:
41–65 ms in the 0.3.0 stalls that had it, with almost no memory creation.

## The solution, in order

Each step goes in on its own, behind a switch, and is measured before the next.

### Step 1: measure each late frame (no behaviour change)

- **Per-frame figures:** add to the stall line (`core/libraries/videoout/driver.cpp`, `Stall:` and
  its `of which:` line) this frame's:
  - `vkQueueBindSparse` calls, ranges, ms, and how many waited;
  - MiB promoted and filled;
  - dedicated image allocations and frees, count and ms;
  - imports started;
  - the full-screen image's upload ms;
  - submit and present ms on the recording threads.
- **A light hitch report:** a mode of `BB_HITCH_REPORT_MS` that prints those figures without
  suspending threads or walking stacks, so a report doesn't add ~200 ms of its own.
- **Comparison:** then the same route, offline, 10 minutes or more, with the model on and with it
  off.

### Step 2: the model's moves (only if the model is wanted)

1. **Budget promotions.** Promotions are optional: a block left in place is correct, only slower
   for the GPU to read. Cap them per submission (`BB_PROMOTE_MB`, start at 8–16 MiB a frame), keep
   the rest in `promote_candidates` for later frames, and skip candidates that went in place again
   meanwhile.
2. **One rebind a frame.** Gather the moves of a frame and send them before its first submission,
   merging adjacent blocks of the same memory into one range.
3. **Wait only for what used the block.** Keep each block's last-use tick and wait for the latest
   of those among the moved blocks, not the previous submission. This is the riskiest part: a
   wrong tick lets the GPU read freed or rebound memory. Validate it with the GPU fault reports
   (memory binds are tracked through `VK_EXT_device_address_binding_report`) and long sessions
   before it ships.
4. **Long term:** replace the sparse arena on Windows with directly bound imported buffers, as
   bbport 0.52 did on Linux. A redesign of the buffer cache's addressing, not a patch.

### Step 3: memory creation and pinning

1. **Recycle dedicated image memory.** Keep freed dedicated allocations a few seconds, keyed by
   size and memory type, up to a cap (a few hundred MiB), and hand them to new images of the same
   size. Streaming then reuses the last area's render targets' memory. Quick experiment first, no
   code: `BB_VMA_DEDICATED_MB=0` (everything in VMA blocks) and `=64`. Compare the `allocation` ms
   and VRAM held.
2. **Keep residency chunks longer.** Raise the idle wait for returning empty 64 MiB chunks, or keep
   one or two empty ones beyond the spare, so the next area doesn't allocate again.
3. **Pin at load (model on).** Import every chunk of the game's direct memory while a loading
   screen is up, or at start, rather than at first use in play.
4. **The full-screen image.** Upload only the pages its write faults marked, or skip the upload
   when a cheap checksum of the written pages is unchanged. Failing that, spread its hash and
   upload over two frames.

### Step 4: verify, then decide the default

- **What to compare:** the same route as step 1, with the step's switch on and off: frames over
  30 ms in play per minute, the minute summaries' CPU cores, and VRAM.
- **The model's default:** it goes back on only if, with steps 2 and 3, it has no more late
  frames in play than with it off.

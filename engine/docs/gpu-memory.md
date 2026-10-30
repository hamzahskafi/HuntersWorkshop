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
59 ms stall in `20261026_033515` and 35–60 ms in many 0.3.0 stalls. (Until 0.4.4 beta 1 those
figures added up everything since the previous long frame, not the long frame alone: an upper bound.)

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

- *0.4.4 beta 1:* the `of which:` figures are the long frame's own (before, everything since the
  previous long frame), with the images given new memory, given a freed image's, and checked.
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
   *Tried in 0.4.4 betas 1 to 4, removed in beta 5*: freed dedicated memory kept 4 s for the next
   image of its size (VMA `CAN_ALIAS` allocations, `BB_IMAGE_REUSE_MB`). 3 to 4 reuses in 15,000 to
   22,000 new images a session (RTX 3070 Ti): the large textures stay loaded, and small images come
   from VMA blocks anyway. Not worth its code.
2. **Keep residency chunks longer.** Raise the idle wait for returning empty 64 MiB chunks, or keep
   one or two empty ones beyond the spare, so the next area doesn't allocate again.
   *Done in 0.4.4 beta 1* (`FreeEmptyChunks`): with room in VRAM an empty chunk waits 30 s
   (`BB_VRAM_CHUNK_KEEP_SECONDS`; 0: 5 s as before) and the 64 MiB chunk emptied last stays; while
   VRAM is tight, 5 s and none kept. Not with the PC memory model, whose evacuations start on free
   slots.
3. **Pin at load (model on).** Import every chunk of the game's direct memory while a loading
   screen is up, or at start, rather than at first use in play.
4. **The full-screen image.** Upload only the pages its write faults marked, or skip the upload
   when a cheap checksum of the written pages is unchanged. Failing that, spread its hash and
   upload over two frames.

### The GX region as an upload heap (0.4.4 beta 5)

The game's per-frame GPU data lives in one region it makes at start: its GX range allocator
(288 MiB with HW's patch). 0.4.4 beta 4 found it (a hook on its init, `engine/gx_region.cpp`) and
showed that nearly every remaining write fault is inside it, and that the game binds it either in
pieces of 16 KiB or less (already copied) or in spans of about 4 MiB (tracked: walked, the written
pages uploaded, re-protected, faulted again at the game's next write).

`BB_GX_IN_PLACE` (default on, not with the PC memory model, which has its own rules) treats it as a
PC game's upload heap: its whole blocks are imported for the GPU once, as they are
(`BbGuestMemory::ImportRange`: VK_EXT_external_memory_host over the game's memory pool, on a
thread of its own as soon as the region exists), and bound to that memory in one run as soon as
the import is done (one sparse bind, at the game's start, rather than block by block in its first
frames), never moved. What is bound inside it then takes the PC memory model's in-place path (`ObtainBuffer`):
no tracking, uploads or VRAM copy; small read-only bindings are still copied when bound (commands
decoded later may change them on the CPU); the command processor's writes into it (WriteData,
DMA) are recorded on the GPU in stream order (`CommandWriteTarget`, `DmaMayWriteOnCpu`), and GPU
writes not done yet count as GPU-modified (`WriteTicks`). A binding over the region waits for the
import (at most 10 s); a failed import, or the game unmapping the region, leaves it tracked as
before. Unlike the model, nothing moves: no sparse rebinds in play, the cause of its late frames.

Fences (beta 7): read in place, the region is read when the GPU runs the work, not when it is
recorded, so the game must not see a fence for that work before the GPU has done it: it reuses
the range allocator's memory by them (the collector at 0x26aa860 returns a range once its state
reads done). The engine's default writes labels at recording; with the region in place it now
writes them once the GPU has finished, as the PC memory model always has (`BB_HONEST_LABELS`, on
whenever `BbGxRing::InPlaceWanted`). Beta 6 ran without it: lighting and other flicker in
Central Yharnam and a screen-wide purple flash, gone with `BB_HONEST_LABELS=1` on the same build.

When the work goes to the GPU (betas 9 and 10). A fence the game waits for is written once the GPU
has done the work before it, so that work must be submitted. Until beta 8 it went out at the end of
a guest submission when the GPU had finished everything, else once 2 ms had passed since the last
submission (`BB_HONEST_FLUSH_US`), and the GPU command thread submitted everything once it had been
idle for 0.2 ms (`BB_HONEST_IDLE_US`): about 10 submissions a frame at 60 FPS, 6 at 120 with the
GPU as the limit. Beta 9 replaced the 2 ms with the GPU's progress, at the guest submissions' ends
only. Beta 10 has no timing at all:

- At the end of each guest submission, graphics or compute (as the PS4 hands each to its GPU), the
  work goes when the GPU has got to the last of its submissions or finished them all
  (`Rasterizer::SubmitForSignalsNow`). Compute submissions did not decide before: their fences
  waited for the next graphics submission or the 0.2 ms.
- While the GPU still has two or more to finish, the work is held and the GPU announces when it gets
  to its last: a submission queued behind unfinished work, or a decision to hold, sets a watch
  (`ArmWatch`); the signal thread, which waits on the GPU's timeline semaphore, announces it once
  the GPU has finished that submission (`GpuSignalLoop`, `Announce`). It waits on submitted work
  only, and every submission wakes it (the scheduler's submit callback, `Finish` included).
- On the announcement the recording thread submits after its next draw once the work recorded since
  holds at least as many draws and dispatches as the submission the GPU runs (`SubmitForGpu`): the
  CPU then keeps up with the GPU. Fewer would add a submission, and each switch between submissions
  left the GPU idle about 25 us in beta 9's profile, for less work than it already has. With nothing
  more on its way (the GPU command thread waiting for the game), at once: the GPU command thread
  hands the announcement over when it runs out of work, or when the announcement wakes it.
- The frame's end (`sceGnmSubmitDone`) submits, with the frame's retirement in the same
  submission. The game ends a frame right after its last submission, before the GPU command
  thread has decoded it (beta 11's first log: the retirement went alone in every frame). So the
  frame end is done at the frame's boundary, as that submission's decoding ends
  (`EndFrameAtBoundary`): the frame's cache work, its retirement and one submission in one task
  (`FrameEndTask`), and that submission's own end check is left out, as the frame end submits
  right after it (`EndsSignalledFrame`). Before, the retirement went alone there and the frame
  end only once the GPU command thread ran out of work, sometimes after decoding the next frame's
  first submissions: two submissions back to back (betas 9 to 11; beta 10's merge on the
  recording thread found the tasks in the other order). When the game ends the frame after its
  submissions are all decoded, the frame end takes the retirement as well (`TakeDecodedFrame`).
  A later frame end already asked for keeps its own, as before. Frame statistics give the share
  retired with the frame end.
- Fence work with nothing to announce it and the recording thread idle is submitted by the GPU
  command thread at once: a trigger missing somewhere, counted (expected 0).

Beta 9 also held the work while 64 KiB of draws were queued for the recording thread
(`BB_HONEST_IDLE_BACKLOG`), as a submission had cost that thread about 65 us. Beta 9's logs
(`20261030_172924`, `20261030_173438`, 120 FPS, RTX 3070 Ti) give 1 to 2 us now that submissions go
out from the Vulkan recording threads (the stage B tasks: about 0.8 us each without submissions,
1 us with 12 a frame), and the hold fired 5 to 21 times a frame at the cap for nothing: it went.
In those logs, where the GPU was the limit (the game waiting 7.4 to 7.9 ms a frame for the previous
frame, 102 to 108 FPS), there were 4.1 submissions a frame and the GPU was idle between them 0.09 to
0.12 ms a frame, in 4 gaps: the switches themselves, not waits for work. At the cap, 10 to 16 a
frame, pacing stddev 0.02 to 0.05 ms. Frame statistics print `Fence submissions: N/frame at the
game's submission ends (GPU out of work N, on its last N), N/frame when the GPU got to its last,
N/frame by the safety net (expected 0)`. `BB_HONEST_FLUSH_US`, `BB_HONEST_IDLE_US`,
`BB_HONEST_IDLE_BACKLOG` and `BB_HONEST_IDLE_FLUSH` are gone.

Measured (beta 6, `20261030_042617`, about 20 minutes, RTX 3070 Ti, against beta 4's
`20261030_033513`): the import took 70 ms and the region was bound in one run before the title
screen. In play, write faults went from a median of 911 a second to 1, and protection time from
0.21 ms a frame on the GPU command thread plus 0.25 ms on other threads to 0.004 and 0. What
remains are load-time writes outside the region (`0x261af9e` and its neighbours, up to about 2,000
in a load window). About 10,000 bindings a second, of about 3 MiB each, are now read in place, and
7,000 small ones are still copied (about 27 MB/s). Frame rate (60), pacing (stddev 0.04 ms, p99
16.8 ms) and VRAM (2.8 to 3.5 GB by the driver's count) are as in beta 4.

GPU time (`BB_GPU_PROFILE=1`, beta 6, the same route: Hunter's Dream, m32, m33, back; busy = the
profile's total less its idle label; play windows of over 500 draws a frame):

| | GPU busy (median) | per 1,000 draws | write faults/s | protect ms/frame | submissions/frame |
|---|---|---|---|---|---|
| region tracked (`BB_GX_IN_PLACE=0`, `20261030_045501`) | 7.94 ms at 965 draws | 7.87 ms | 912 | 0.405 | 2.0 |
| in place, fences at recording (`20261030_044107`) | 7.26 ms at 1,132 draws | 6.63 ms | 0 | 0.005 | 2.0 |
| in place, honest fences (beta 7's default, `20261030_045019`) | 7.03 ms at 986 draws | 7.15 ms | 1 | 0.003 | 10.1 |

Reading the region over PCIe costs the GPU less than the tracked path's uploads of it did: about
0.9 ms a frame less. A per-frame GPU copy of it into VRAM (an alternative considered) has nothing
to win here. With honest fences the GPU command thread sends about 10 submissions a frame
(`BB_HONEST_FLUSH_US`, 2 ms) for 2; CPU use (3.0 to 3.2 cores), frame rate (60), pacing (stddev
0.05 ms, p99 16.8 ms) and VRAM (3.0 to 3.5 GB, against 3.1 to 3.7 tracked) are unchanged. Long
frames come at loads in all three; in play, honest fences had four windows with one frame of 20 to
30 ms (no shader compiles) in about ten minutes, tracked one: to watch, not acted on.

Its own buffer (beta 9). Betas 5 to 8 bound the region's arena blocks to the import with one sparse
bind. The arena is the engine's one sparse buffer over all of the game's memory, and NVIDIA's lists
only VRAM among the memory types it may be bound to (`0x2`, imports `0x18`): the binding worked, but
outside the Vulkan spec, and its import was made without device addresses though the arena has
them. Now the import is its own buffer (`BbGuestMemory::ImportRange` with the arena's usage,
`AllFlags`, and memory allocated for device addresses), and the buffer cache hands it out for every
binding inside the region (`ObtainBuffer`, `CommandWriteTarget`, `ObtainBufferForImage`, through
`gx_own`), at the binding's offset in it. The page table points the region's blocks at the
buffer's device address (shaders that read memory through it). The arena's blocks there stay
unbound: no sparse binding at all. `IsInPlace` and `IsAnyInPlace` answer for the region from its
blocks, so writes into it from the command processor and DMA still go to the GPU in stream order.

The region's edges (beta 10). The region begins 64 bytes into a block (0x1043400040 in 1.09) and
ends 64 bytes into another. Beta 9 imported its whole blocks only, from the next block on: bindings
starting in the region's first 64 KiB crossed into the buffer and were copied every frame (beta 9's
logs: about one a frame at the title, 160 KiB, 19 MB/s; 30 to 100 a second in play, 30 to 50 KiB).
Now the import begins at the block holding the region's first byte. The 64 bytes in front of the
region are the game's: it gets the region from an allocation call (0x216b21a, 288 MiB, 16-byte
aligned, through an allocator object, so not confirmed statically) that returned a pointer 0x40
past a block boundary, the mark of a heap header. Nothing else lies in that block, so the whole of
it is in place, for bindings and the page table alike: nothing reaches those bytes two ways. A
binding of them is logged once (`NoteBeforeGx`); nothing of the game's GPU work should bind them.
Should that block not be one run of memory with the rest, the import begins at the next block as
before (logged). The block holding the region's last 64 bytes stays out: the rest of it is the
game's next allocation, and the page table cannot point part of a block elsewhere. Bindings across
an edge are counted by edge (`read across its first edge`, expected 0; `read across its last
edge`, only bindings reaching the region's last 64 bytes). Beta 10's first log
(`20261030_182328`): in play none at the first edge (30 to 100 a second in beta 9) and almost
none at the last; at the title one a frame at the last (160 KiB, 19 MB/s), which beta 9's figure
was too (taken then for the first). The first binding across each edge is logged once
(`NoteGxEdge`), with where it ends against the region. Beta 10's log (`20261030_190741`): at the
title, 0x10553c0040 for 262,160 bytes, the region's last 256 KiB and 16 bytes past it (a buffer
descriptor one 16-byte element past its data, into the game's next allocation, likely its heap
header). Beta 11 imports the page holding the region's last bytes as well (to 0x1055401000,
when it is the same run of memory and the driver imports at 4 KiB; logged either way), and reads
read-only bindings reaching into it in place (`InGxTail`) unless the GPU has written there
through the tracked path. Read in place, those bytes are what the CPU wrote, which the tracked
copy would also hold. Writes there, and the page table for that block, stay on the tracked copy,
as the rest of the block is the next allocation's; an unmap of that page's other bytes ends it. Across the edge, part in the buffer and
part outside: read-only bindings up to 8 MiB are copied when bound (the stream path,
`GxEdgeCopied`, `GxTailCopied`); anything else that needs the arena there (a GPU write across the
edge, a shader's page fault there) binds the arena's blocks to the import as before
(`GxArenaBound`, logged once, with aliasing barriers as `BB_CHUNK_BUFFERS` has:
`Runtime::NoteAliasedAccess`). `BB_GX_OWN_BUFFER=0` is beta 8's way, for comparison. The arena's
out-of-spec note in the log is now printed only with the PC memory model.

### Step 4: verify, then decide the default

- **What to compare:** the same route as step 1, with the step's switch on and off: frames over
  30 ms in play per minute, the minute summaries' CPU cores, and VRAM.
- **The model's default:** it goes back on only if, with steps 2 and 3, it has no more late
  frames in play than with it off.

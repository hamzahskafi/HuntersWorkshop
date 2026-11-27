# Beta 19 design: the GPU command thread's cost a draw, and the engine's own waits

Status: approved by the tester ("go ahead with beta 19", 2026-11-27) and built (engine id 134); see
"As built" at the end.

The tester (2026-11-27), after beta 18: keep beta 19's work ("it's still optimization work that should
be completed either way") and do the GPU command thread's optimizations as well. The GPU cache's
identity may change during this development cycle ("we have the shader catalog").

## Why

Beta 18 moved the limit at both of the tester's spots from the game to the engine (log
`20261127_195807`): the game now waits 1.3-2.6 ms a frame for the engine, and the GPU command thread
(stage A) is idle 0.3-3% of the time. Stage A takes about 3.0 us a draw, about 4 ms of a frame at
1,345 draws.

How far that can lift things on the tester's PC (RX 9070 XT, i5-14600KF, FSR 4.1.2 at 1920x1080): the
GPU's own work is about 4.1-4.2 ms a frame at both spots (the 132 diagnostics runs' profile), so about
240 FPS is the ceiling there.
- The lamp spot runs 226-229 FPS without the main thread sampled (log `20261127_201857`): up to about
  +6%.
- The slower spot (200-205 FPS sampled): up to about +15%.
- Views with more draws gain most. The 133 run's 1,780-draw view ran 175-182 FPS with stage A idle
  0.1%: at 2.3 us a draw it would need about 4.1 ms instead of 5.3.
- CPUs slower per thread gain more, since stage A's microseconds a draw are longer there.

## Where stage A's time goes

The tester's profile (`20261127_201857`): `BB_SAMPLE_THREADS=shadPS4:GpuCommandProcessor` at the lamp
spot, 106,711 samples, named by the builder from 133's executable. Shares of stage A's time:

| What | Share | Where |
|---|---|---|
| The statistics' clock read on every packet (`BbNativeCalls::Ticks`, two `rdtsc` a packet) | about 10% | `liverpool.cpp` (ProcessGraphics). Only with `BB_FRAME_STATS`; part of it is the packet loop's own loads landing on the `rdtsc` |
| The pending-read and pending-write tables (`PendingReads::Raise`, `Max`, `Newest`) | about 9.5% | `vk_rasterizer.h`, from `NotePendingRead`, `NotePendingGpuWrite`, `PendingWriteOverlaps` |
| The hand-over of each draw to stage B (`DrawPipe::Commit`: a sequentially consistent store, a full fence, and two sleep checks) | about 4.6% | `vk_draw_pipe.h` |
| Waiting for the submission queue's lock after each submission (`queue.m_access`) | about 4.2% | `liverpool.cpp` (Process), held by the game's submit (`SubmitGfx`) |
| Other waits the sampler can't place (after the fence note, near `PipeSignals`) | about 4.8% | most likely heap allocations (the pending-fence deque's blocks, the decode tasks' frames) |
| Memory copies: constants into the ring (`CopySparseMemory`), the register file's reset (`SetDefaults` at CLEAR_STATE) | about 2% | `bbgpu.cpp`, `regs.cpp` |
| The prepared pipeline's check (`TryPreparedPipeline`'s compare and lookup) | about 1% | `vk_pipeline_cache.cpp` |
| The register checksum (`HashRegisterPacket`) | under 1% | `liverpool.cpp` |
| The rest: the packet loop's and the draw step's own bodies (ProcessGraphics 15 KB, PostDraw 10 KB, most of their callees inlined) | about 40% | needs a finer profile |

Not hot: the register dirty marks, the memory map's lookups (`ClampRangeSize`), `IsRegionGpuModified`,
the draw preparation's hand-over.

## Part 1: stage A

Each item has its own switch this beta (removed before the release candidate), so a run can compare.

1. **Statistics without a clock on every packet.** The per-packet times become per draw for 1 draw in
   16 and per command buffer for the whole: the same lines, a few clock reads a buffer instead of two a
   packet. Normal play has no such cost today; runs with the frame statistics on (every run the tester
   sends) lose up to 10% of stage A to it, so their frame rates read low. This comes first, so every
   later measurement reads play as it is.
2. **The hand-over to stage B without a full fence on every draw.** Today each `Commit` publishes with
   a sequentially consistent store and checks whether stage B (and the lookahead thread) sleep: the
   Dekker handshake that keeps a wake from being lost. Instead: a plain release store on every draw
   (a normal store on x86), and the fence and the sleep checks every 8 draws, at every task (fences,
   signals, flips), at a submission's end, and before stage A itself waits for anything. A wake can then
   come at most 8 draws late (about 25 us of stage A), never be lost; stage B runs 25% busy and is
   never the limit. About 4.5% back.
3. **The pending tables, asked less and filled less.**
   - The read table answers one question: may the constant engine's dump (`DumpConstRam`) overwrite
     bytes a queued draw still reads? The dumps write a small, fixed set of pages (the constant ring,
     about 25 pages). Reads are noted only where they touch a page a dump has written. A dump into a
     page it never wrote before waits once for every queued draw, then the page joins the set. All of
     it runs on stage A, so there is no race.
   - The write table: a short list of the GPU writes stage B hasn't reached yet (fences, DMA,
     storage buffers), pruned by its progress; a constant binding checks a handful of entries, or none
     when every noted write is reached. The hashed table stays as the fallback when that list fills
     (stage B far behind).
   - Counts of notes and lookups a frame go into the statistics, to confirm.
   About 9.5% back, most of it.
4. **The submission queue's lock held only for the push.** The game's submit holds `queue.m_access`
   while it creates the decode task (a coroutine frame from the heap) and notes the buffer for the free
   check. Both move before the lock; the lock keeps numbering, the draw preparation's queue and the
   push in one order, as today. Stage A's pop then rarely waits. About 4% back.
5. **No heap on stage A's per-fence path.** The pending-fence deque becomes a ring reserved once, and
   finished decode tasks' frames are reused. Then a second profile shows whether the 4.8% of
   unplaced waits went with them.
6. **Small ones:**
   - a constant buffer in one mapping is copied directly, without the sparse copy's walk;
   - CLEAR_STATE resets only the register blocks that differ from the defaults (stage A and the draw
     preparation workers);
   - the prepared pipeline's key compared by its hash before the full compare.
   About 2-3% back.

Items 1, 4 and 5 change `liverpool.cpp`, so the GPU cache's identity changes once with this beta
(allowed by the tester); the builder's check expects a new identity instead of the old one.

Together, in normal play: about 20-25% of stage A, about 0.6-0.75 us a draw, so about 3.0 down to
2.3 us a draw.

## Part 2: the engine's own waits (beta 19 as designed)

From [handoffs-and-placement.md](handoffs-and-placement.md):
- Stage A's wait for submissions, the draw pipe's waits, the recording threads' and the host copies'
  waits move onto the parking primitive (`host_park`). Their spins come from one function, by role
  (today's `SpinLimitUs` and `CapSpinUs`). This mostly gives CPU time back to the game and helps CPUs
  with fewer cores; on the tester's PC those threads rarely wait at these spots.
- The `Roles:` line adds the game's main thread and `GXRenderThread` on their own (where they wake,
  their CPU time). Frame threads preferring the fastest cores stays a measured option.

## Later, from runs

- **The packet loop and the draw step themselves** (the profile's other 40%): a profile with
  `BB_SAMPLE_TOP=200` after Part 1, so the hot instructions inside ProcessGraphics and PostDraw can be
  named; then targeted changes (a fast path for runs of register packets, `NextPacket` inlined, the
  checksum in independent lanes). Beta 20.
- **Pool sizes from the `Roles:` lines, the parking barrier and a raised main thread**: after the
  tester's runs 1 (`BB_THREAD_ROLES=0`) and 3 (`BB_TEST_CPUS=8`).

## Measurement

On the beta, frame statistics on, no sampler:
1. The lamp spot and the 1,780-draw view: FPS, stage A's microseconds a draw and idle.
2. The same with Part 1's switches off one at a time (each item's share).
3. A stage A profile (`BB_SAMPLE_THREADS=shadPS4:GpuCommandProcessor`, `BB_SAMPLE_TOP=200`) for beta 20.

## Considered and not chosen

| Alternative | Why not |
|---|---|
| Stage A's register application moved to the draw preparation workers (they already decode the same buffers) | Stage A still has to walk every packet in order for its side effects (fences, DMA, the constant engine); a large change for what Part 1 gets from smaller ones. Kept for after beta 20's profile |
| Publishing to stage B in batches of draws | Saves the same fence as item 2 but delays stage B's start on every batch; item 2 keeps per-draw publication |
| Turning the statistics' clocks off instead of item 1 | The tester's runs need the per-draw numbers |

## As built (engine id 134)

| Item | Where | Switch (this beta only) |
|---|---|---|
| 1. Statistics: about one packet in 16 timed (a countdown reloaded at random from 8 to 24), each counted as 16 | `bbport_native_calls.{h,cpp}`, `liverpool.cpp` | `BB_STATS_EVERY_PACKET=1` |
| 2. The hand-over: a release store a packet, the fence and the sleep checks every 8 packets, after every task, before stage A goes idle and before each of its waits (the draw pipe's, the deferred signals'); stage B's and the lookahead thread's sleeps on the engine's parking word (`bbport_park.h`), looking again every 2 ms (a "late wake" in the draw pipe line, expected 0); a packet handed over while stage B already sleeps is fenced and wakes it at once (engine id 135, from the builder's Wine run: a timeout landing while a fenced publish's wake was on its way counted as late before) | `vk_draw_pipe.h`, `vk_rasterizer.{h,cpp}`, `liverpool.cpp` | `BB_PIPE_HANDOVER=each` |
| 3. The pending tables: reads noted only where the dumps write (`DumpPages`), GPU writes as a list of the unreached ones (`PendingWriteList`) with the hashed table when it fills; counts in a "pending tables/s" line | `vk_pending.h`, `vk_rasterizer.{h,cpp}` | `BB_PENDING_TABLES=old` |
| 4. The submission lock: the draw preparation threads woken and the free check told after it | `liverpool.cpp`, `vk_draw_prep.{h,cpp}` | (with item 5) |
| 5. The decode tasks' frames from a pool (`bbport_frame_pool.h`): no heap under the lock or at each submission's end | `liverpool.{h,cpp}` | `BB_FRAME_POOL=0` |
| Part 2: the recording threads' and stage A's idle waits on their own parking words, no condition variable | `vk_scheduler.{h,cpp}`, `liverpool.{h,cpp}` | `BB_ENGINE_PARK=0` |
| Part 2: the `Roles:` line ends with the game's `main` and `GXRenderThread` on their own (cores, where they woke) | `bbport_roles.cpp` | - |
| The sampler walks a sample inside system code back to its engine caller with the unwind data, instead of the first engine-looking value on the stack (the profile's 4.8% of unplaced waits) | `bbport_sampler.cpp` | - |

Changed from the design while building:
- **Item 5:** the pending-fence queue already reuses its blocks (libc++'s deque in first-in, first-out
  use), so only the decode tasks' frames changed.
- **Item 6 dropped**, each for a reason found in the code:
  - the constant copies are the copying itself;
  - CLEAR_STATE comes a few times a frame (a 208 KB reset each), and resetting only the changed blocks
    would cost a mark on every register packet;
  - the prepared pipeline's compare is the check that keeps a stale pipeline out.
- **Part 2's spins** stay as they are (`CapSpinUs`, `SpinLimitUs`): no run has shown a reason to
  change them yet.

The GPU cache's identity changes with this beta (`liverpool.{h,cpp}`): `cdca3ea71cd46d16`, from
`fed6a8570e2a2710`.

Tests: `draw-pipe-handover-test` (real threads; ThreadSanitizer clean here), `pending-test`.

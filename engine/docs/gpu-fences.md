# Beta 21 research: the GPU writes the game's fences

Status: research (2026-11-27), counted in engine id 139, the plan approved by the tester (2026-11-28),
built in engine id 140 ("As built", the last section).

## Why

The tester's GPU profile of 138 (`20261127_231846`): the GPU between submissions 0.19 ms a frame at the
lamp spot (3.4 switches a frame), 0.35 in the busy view (5.5), 0.49 in the slow area (6.2), about
60-80 us a switch (the profiler adds a little). The slow-area timeline: 88% of submissions reached the
GPU while it was still busy, so the gaps are the switches themselves (the work in flight finishing,
caches, the next submission starting), not the GPU waiting for the CPU.

## What the engine does today

- The game ends work with end-of-pipe fences (EVENT_WRITE_EOP: "write value V at address A once the
  work before this is done"). **About 600 a frame** (the log's `EOP fences:` line: about 140,000 a
  second at 230 FPS).
- With the GX region read in place (0.4.4 beta 7), a fence must not be seen before the GPU has done the
  work before it: the game reuses its range allocator's memory by them (the collector at 0x26aa860; an
  early fence was beta 6's flicker). So each fence goes on the GPU signal thread's list
  (`SignalAfterGpu`), which waits on the GPU's timeline semaphore and **writes the fence on the CPU**
  once the GPU has finished the submission holding it (`GpuSignalLoop`, `SignalEop`, `WriteLabel`).
- A fence is therefore seen only at a submission's end, so the work is submitted at the game's
  submission ends whenever the GPU is down to its last submission (`SubmitForSignalsNow`; the
  policy of 0.4.4 betas 9-12, in `gpu-memory.md`). Feeding the GPU and making fences visible are
  one mechanism, and the switches follow the game's submission pattern: 3.4 to 6.2 a frame.
- The game waits on fences by reading memory: in play it makes no event-queue waits on GPU
  interrupts (the `Guest event waits` lines show only the flip's queue, at loads). It reads no data
  the GPU wrote (`readbacks 0/s`; `0.0 GPU-written` of the bindings in place); the fences tell it
  the GPU is done with memory it then reuses.

## The idea

The GPU writes the fence itself, in its command stream, right after the work it guards, into the
game's memory in place (the GX region import), as the PS4's GPU does with the same packet. Then:
- a fence no longer needs a submission's end to become visible;
- it becomes visible when the GPU passes it, not after the submission ends and the signal thread
  wakes (no CPU round trip);
- the signal thread's 140,000 writes a second go;
- submissions can follow the GPU's need for work only.

## What the research found

**1. The Vulkan command exists and has the right semantics, where the device has it.**
`vkCmdWriteBufferMarker2AMD` (`VK_AMD_buffer_marker` with synchronization2), from the specification's
source (`chapters/copies.adoc` in KhronosGroup/Vulkan-Docs):
- "it defines an execution dependency between prior operations and writing the marker value, as well
  as a memory dependency from earlier buffer marker write commands": the write waits for the work
  before it (at the stage given: `ALL_COMMANDS` for an end-of-pipe fence), and fences stay in order
  among themselves.
- "The second synchronization scope includes only the buffer marker write": **later commands are not
  held up** (the hardware's own pipelined end-of-pipe write on AMD; the PS4's RELEASE_MEM).
- "The first access scope includes only accesses performed by other buffer marker write commands":
  there is **no memory dependency from the earlier work's writes**. A fence that guards data the GPU
  wrote for the CPU would need more; the game reads none (above). The rule: a fence after work that
  wrote the game's memory in place keeps today's path (stage A knows those writes: the pending write
  list).
- 32-bit values only, at offsets that are multiples of 4, into a buffer with `TRANSFER_DST` usage
  bound to one memory object (the GX import is its own buffer, with the arena's usage). A 64-bit
  fence: two markers, low word first (markers are ordered); for a value that only grows, a reader
  between the two sees a smaller value, never a larger one, so never an early "done". The clock and
  counter modes keep today's path.
- A note: "Implementations may only support a limited number of pipelined marker write operations in
  flight at a given time. Thus an excessive number of marker write operations may degrade command
  execution performance." At 600 a frame this must be measured. The PS4 GPU runs the same 600 with its
  own end-of-pipe writes, and AMD's hardware does the same thing today.

**2. Support: both of the tester's GPUs have it.** AMD's Windows driver exposes the extension, and
Mesa's RADV has it. **NVIDIA's Windows driver has it too** (corrected 2026-11-28): the engine's GPU
breadcrumbs are built on it and are on whenever it exists (`vk_breadcrumbs.cpp`), and they ran on the
tester's RTX 3070 Ti (driver 617.42, `engine-map.md` 2.3); 0.4.4 beta 20 measured them there,
`BB_BREADCRUMBS=0` taking 0.10 ms a frame off the GPU's busy time for about 3,600 markers a frame (about
28 ns each, half of it on the copy shader's 62 dispatches). The 2019 vkd3d note this item first relied
on is out of date. Intel's Windows driver: not found. So the engine checks for it at start, and devices
without it keep today's path. That is a capability check, not a vendor rule.

**3. The core-Vulkan fallback proposed first is not viable.** A barrier plus a buffer write stops the
GPU's pipeline at each fence. At 600 fences a frame that is 600 drains. Devices without the marker keep
today's CPU-written fences.

**4. The hand-over to the GPU: a queue, not rules.** The tester (2026-11-27): "this appears to be a
design and queuing problem more so than rules and gates and conditionals". Today there is no queue
between the recording and the GPU. At each of the game's 20-70 submission ends a frame the engine
decides whether the work goes now (the GPU's progress, a watch on its last submission, the
announcement, holds, a safety net: `gpu-memory.md`, 0.4.4 betas 9-12). That is needed because one
mechanism carries two things: the game's fences, visible only at a submission's end, and feeding the
GPU. Each frame then ramps up from small chunks (the GPU is finishing the previous frame when the
game starts one, so the first draws go at once, then a little more, and so on): 3.4-6.2 switches a
frame.

The design separates them:
- The fences are in the GPU's command stream (items 1 and 2); they constrain the hand-over in no way.
- The hand-over is a queue between the recording threads and the GPU, with the game's own submissions
  as its units, as on the PS4. While the GPU is busy, units collect into one batch. When the GPU has
  nothing queued, the next unit goes to it at once. When the game completes a frame (its submit-done),
  that batch is complete and goes. A batch holds at most 64 recorded command buffers, and a full one
  goes.
That is a queue with one consumer: items wait while it is busy, and an idle consumer takes the next at
once. There is nothing to decide per submission and nothing to estimate. What follows from it:
- With the GPU as the limit (the tester's three spots: the game about a frame ahead, waiting 1.6-4 ms
  a frame for the previous one), each frame goes as one batch, queued behind the frame the GPU runs,
  with no gap: about 2 switches a frame (the frame and the presenter's).
- With the CPU as the limit or a frame cap, the GPU is often idle and takes units as they come, as
  today.
- A mid-frame fence wait needs nothing of its own. If the game waits for a fence in its current
  frame, the GPU finishes the previous frame, finds the queue holding that work, takes it, and writes
  the fence.
- Gone: the decisions at each game submission end, the watch and the announcement, the holds, the
  safety net.
(Drafts before this: holding work by an estimate of the GPU time queued, which added waiting, counting
and delay; then three submission rules. The tester asked for the design instead.)

Devices without the marker can keep fences out of the submissions too, with Vulkan events (core,
non-stalling: `vkCmdSetEvent2` at the fence, read by the signal thread with `vkGetEventStatus`, which
then writes the fence on the CPU). That gives the same queue everywhere; its cost (a thread reading
the events while fences are pending) is to be checked.

**5. Passes cut between the recording threads' command buffers: measured afresh, not taken from 0.4.4.**
0.4.4 (betas 14, 20, 21) found a cut pass cost a few microseconds and halving the cuts changed
nothing, on an RTX 3070 Ti at 60-120 FPS with the recording of then. The tester (2026-11-27): a lot has
changed since, so a fresh approach. Today's GPU (RX 9070 XT), frame time (4.3-5.2 ms, where a fixed
cost a frame weighs twice as much) and recording threads differ. The 138 log has about 17 cuts a frame
of a pass ("begun again after a recording cut"), in the main geometry pass most of all (about 20 pieces
a frame in the profile).
- What a cut costs in principle: a store of the attachments at the end of one piece and a load at the
  start of the next. The specification: "Store and resolve operations are only performed at the end of a
  render pass instance that does not specify the VK_RENDERING_SUSPENDING_BIT_KHR flag", and loads only
  at the start of one without VK_RENDERING_RESUMING_BIT_KHR.
- The objection that the suspending flag must be set when a pass begins, before a cut is known, has a
  way around it. Every pass begins suspendable. At a cut it is suspended and resumed as the first
  command of the next command buffer. At a normal end it is closed by an empty resuming instance, which
  does the store. The rules allow it: the suspended and resuming instances are in one submission's
  command buffers, in order, with no action or synchronization commands and no other render pass
  instance between them (VUID-VkSubmitInfo-pCommandBuffers-06014 to 06016). With whole-frame
  submissions every cut is inside one.
- First, a measurement in the counting build: a switch that never cuts inside a pass, only at the
  next pass end (with a size cap). It shows what the cuts cost on this GPU, and what recording the
  long pass on one thread costs the recording threads. Then: if it pays and recording keeps up, cut
  only at pass ends; if it pays and recording suffers, the suspendable passes above; if it does not
  pay, today's numbers recorded.

## Still unknown: a counting build first

Three facts decide how much of the idea applies. None can be read from the logs we have:
1. **Where the fences live:** the share of fence addresses inside the GX region's import, with no VRAM
   copy of their page (the GPU can write only those in place; the rest keep today's path).
2. **What kind:** 32-bit, 64-bit, clock or counter; with or without an interrupt; after work that wrote
   the game's memory in place.
3. **Whether the game blocks on fences mid-frame:** how often the GPU command thread runs out of work
   with fences pending and the game resumes submitting right after one is written; and how often the
   GPU runs empty mid-frame today. With the queue, such a wait is released when the GPU finishes the
   previous frame and takes the waiting work, so how often and how long matters.

Also counted: distinct fence addresses a frame and back-to-back fences (no work between them), for the
marker count's cost. And the switch of item 5 (no cuts inside a pass), for one run with it and one
without, with the GPU profile on.

These are counters only (no behavior change), with one line in the frame statistics. One run at the
three spots answers them.

## The counting build (engine id 139)

Counted only; nothing in play changes but `BB_CUT_IN_PASS=0`, a measurement. With the frame statistics
on, the draw pipe's block has two lines more:
- `fences a frame (beta 21 research)`: by packet (end-of-pipe, end-of-shader, compute's RELEASE_MEM);
  by what they write (no value, 32-bit, 64-bit, a clock or counter); with an interrupt; the share of
  their values in the GX region in place (`BufferCache::GpuWritesInPlace`: its own buffer, nothing
  noted), outside a frame, unaligned; after GPU writes into the game's memory (noted since the fence
  before: `NotePendingGpuWrite`); right after another (nothing in the draw pipe between); distinct
  addresses a frame.
- `the game's pauses mid-frame a frame`: the GPU command thread's idle waits outside the game's wait
  for its previous frame (`GnmDriver::GuestWaitingForFrame`), by length, and those of 50 us or more
  that ended with the GPU out of work (everything submitted done: `Scheduler::IsFree`).
`BB_CUT_IN_PASS=0`: the recording threads cut only where a pass ends (inside one only past 8 times the
segment size); the `render passes ... begun again after a recording cut` figure, the GPU profile's pass
counts and time, and the `Recording:` line show the effect.

The run: the three spots with `BB_GPU_PROFILE=1`, then the same with `BB_GPU_PROFILE=1` and
`BB_CUT_IN_PASS=0`. The GPU cache's identity changes (`liverpool.cpp`): `ce835392748d8a45`.

## The counting build's results (engine id 139)

The tester's two runs: `20261128_000941` (`BB_GPU_PROFILE=1`) and `20261128_001438` (the same with
`BB_CUT_IN_PASS=0`), the three spots each; read with the 138 slow-area timeline (`20261127_231846`).

| | Fences a frame | 64-bit / 32-bit | In the GX region in place | The game's pauses mid-frame |
|---|---|---|---|---|
| Lamp spot | about 603 | 450 / 150 | 0% | about 1.5 a frame, 0.1 ms |
| Busy view | about 627 | 470 / 155 | 0% | about 0.6 a frame, 0.02 ms |
| Slow area | about 410 | 265 / 150 | 0% | about 3.5 a frame, 1.6 ms |

- Almost all are end-of-pipe (about 2 a frame end-of-shader, none from compute). No interrupts, no clock
  or counter values. About a quarter come right after another fence. Every fence a frame writes its
  own address (distinct addresses about equal to fences).
- **None of the fences' values are in the GX region the GPU reads and writes in place.** The GPU cannot
  write them directly through the import as drafted; they live in the game's other memory.
- The "after GPU writes into the game's memory" figure equals the fence count: the fence's own label is
  noted as a pending GPU write (`NotePendingFences`), so the next fence always counts. Not meaningful as
  built.
- **The game does not wait on fences mid-frame at these spots.** In the slow area's long pauses of the
  GPU command thread (200 us or more, about 1.7 a frame, median 0.86 ms, 34% of the time) the game's
  busiest thread (35400) was blocked 3% of the time, so it was working; the frame-ending thread (14160)
  waited on a condition variable 96% (for the game's own threads); the five job workers half. The slow
  area pauses because the game has more CPU work there, not on fences. Fences matter for when the game
  can reuse memory, not for the frame's progress.
- The pass cuts: `BB_CUT_IN_PASS=0` halved the command buffers a submission (lamp 7.2 to 3.7, busy view
  5-6 to 2.5-3.1, slow area 4-5 to 2.0-2.6); FPS +1.2% at the lamp spot (228.3 to 231.0), +2.4% in the
  busy view (191.4 to 195.9), +1.5% in the slow area (221.5 to 224.8); GPU time a frame 4.37 to 4.33, 5.22
  to 5.09, 4.49 to 4.43 ms. But the profiler ends a pass at each of its timestamps (`WriteTimestamp`,
  outside passes) and stamps each pass a cut resumes, so with the profiler on every cut costs a pass end
  and a timestamp more than it does in play (the "begun again after a recording cut" figure reads 0 in
  both runs for that reason). A run without the profiler, with and without the switch, decides it.

## What it changes in the design

1. **The fences are not where the GPU can write them in place.** Writing the game's fences from the GPU
   directly would need that memory imported too. A design that needs no import: the GPU writes one
   marker word of the engine's own, in stream order, the number of the last fence it passed
   (`vkCmdWriteBufferMarker2AMD` after each fence's work: non-stalling, ordered). The signal thread reads
   that word and writes the game's fences up to it, as now on the CPU, but as the GPU passes them rather
   than when the whole submission ends. Fences are then independent of submissions wherever they live.
2. **Fence latency does not gate the game's frame here,** so the word needs no timer and no thread of
   its own: it is read at points the engine passes anyway (the plan below, part 2).
3. **The queue design stands:** units collect while the GPU is busy, an idle GPU takes the next at
   once, a completed frame goes. With fences off the submissions, nothing else asks for a submission.
4. **The pass cuts: cut only where a pass ends.** The unprofiled runs (`20261128_003226` default,
   `20261128_002730` with `BB_CUT_IN_PASS=0`): FPS 229.3 to 231.0 at the lamp spot (+0.7%), 199.8 to 201.0
   in the busy view (+0.6%), 226.1 to 228.0 in the slow area (+0.8%); the same direction at all three, about
   the size of the run-to-run spread for a single run. Cuts a second 3,400-4,900 to 430-940, command
   buffers a submission halved, stage B's busy share and stage A's idle unchanged: the recording keeps up
   with the long passes on one thread. So the simplest form, with no suspend and resume needed: the
   recording threads cut only where a pass ends (and inside one only past 8 segments' size). The
   profiled runs had shown +1.2-2.4%, inflated by the profiler's own pass ends and timestamps at each cut.

## Expected, if the counts are favorable

- Switches: from 3.4-6.2 a frame to about 2 (the frame and the presenter's), on devices with the
  marker. The GPU idle between submissions falls by about two thirds: about 0.1 ms a frame at the
  lamp spot, 0.2 in the busy view, 0.3 in the slow area (profiled figures). The slow area gains most.
- The game sees its fences sooner. The signal thread does a few writes a frame instead of 600.
- Devices without the marker: unchanged.

## The plan for beta 21 (proposed to the tester, 2026-11-28; not built)

**1. The GPU reports how far it has got, in one word.** At start, where the device has
`VK_AMD_buffer_marker` (both of the tester's GPUs), the engine makes one 4-byte word of its own in host
memory (host-visible and coherent, as the breadcrumbs' buffer). The recording numbers the end of each of
the game's submissions, and there writes that number into the word (`writeBufferMarkerAMD`, bottom of
the pipe: after all earlier work, inside or outside a pass, holding up nothing after it). The word then
always reads "the game's submissions up to N are done". One mark a submission, not one a fence
(revised 2026-11-28): the fences are read at the game's submissions (part 2), so a finer mark would
change nothing the game sees, and 20-70 marks a frame instead of about 600 cost about a tenth (the
breadcrumbs already write about 1,800 a frame, one per command; 3,600 cost 0.10 ms a frame on the 3070
Ti, so about 0.002 ms is expected).

**2. Fences written as the GPU passes them.** Each plain fence (a value to write, no interrupt: all of
them in the 139 counts) goes into a ring with its submission's number, address, value and size, in
place of a function on the signal thread's list. The ring is written on the CPU, in order and exactly once per fence, at two
points the engine passes anyway: the end of each of the game's submissions (the GPU command thread reads
the word, one memory read, and writes the fences of the submissions done) and the end of each of the
GPU's submissions (the signal thread, which waits for it as now). No timer, no thread of its own, no
extra wake. A fence is then seen once the GPU has finished the game's submission holding it and the game
next submits: earlier than today (the end of the engine's whole submission, 3-7 a frame, plus the signal
thread's wake), never before the GPU has passed it.
Kept on today's path (written when the submission ends): fences with an interrupt or another action, the
clock and counter values, and a fence after GPU writes into the game's memory in place (those need the
writes visible, which only the submission's end gives). 139 found none of these in play; counted. The
CPU writes the game's 64-bit values whole, so the word's 32 bits limit nothing.

**3. The hand-over: a queue with one consumer.** The units are the game's submissions, recorded in order.
The GPU's queue is the units submitted that it has not started. As in any such queue, work waits while
the consumer has more than one item ahead of it, and the consumer takes all the waiting work when it
starts its last item, so it never waits for work that is ready. The word shows at each unit's end
whether the GPU has started its last unit. The waiting units go as one submission:
- at a unit's end, when the GPU has started its last unit or finished everything;
- when the game completes a frame (after it the game waits for its previous frame, so no unit end would
  look again until the GPU had run out);
- when the GPU finishes everything with units waiting (the signal thread, which wakes at that end now:
  the game paused for longer than the GPU's remaining work, so the GPU has time to spare);
- at the batch limit of 64 command buffers (there now).
With the GPU as the limit (the three spots), the GPU runs the previous frame while the game records the
next; that frame's units wait and go when it completes, queued behind: about 2 switches a frame (the
frame's and the presenter's) against 3.4-6.2. When the GPU catches up with the game (the slow area's
pauses), it takes the work in pieces as large as it can without waiting. Gone: the decision at each
game submission from the GPU's submissions (`SubmitForSignalsNow`), the watch and the announcement
(`ArmWatch`, `Announce`, `SubmitForGpu`), the GPU command thread's idle-time checks and the safety net;
they stay only for devices without the marker.

**4. Passes cut only where they end:** `BB_CUT_IN_PASS=0`'s behaviour by default (+0.6-0.8%, measured);
`BB_CUT_IN_PASS=1` for today's.

**5. The pending write list keeps up to 64 wide writes live** (16 now; the busy view had up to 17).

**Devices without the marker:** today's path whole, so nothing changes on them; the log says which path
runs. `BB_GPU_FENCES=cpu`: today's path on any device, for the comparison.

**Counted** (frame statistics): submissions and switches a frame, the submissions by cause (the GPU on
its last unit, a frame completed, the GPU out of work, the batch limit); fences written at a game
submission against at a submission's end; fences on today's path by reason; marks a frame.

**Checked here:** a unit test of the ring and the queue against a model GPU (order, exactly once, both
writers, the count wrapping round: compared as a difference, 8 hours at this rate before it wraps);
the existing suites; the MinGW build. The risk is a fence seen early (0.4.4 beta 6's flicker): the
marker cannot be written before the work, so the risk is in the numbering, which the test covers.

**The run:** the three spots without the profiler, default and then `BB_GPU_FENCES=cpu`; then once with
`BB_GPU_PROFILE=1` for the switches and the marks' cost. Version 0.4.6-beta.21, engine id 140.

Expected: the profile put the switches at 0.19-0.49 ms of a 4.3-5.2 ms frame; with two thirds of them
gone, a few percent, the slow area most. The game sees its fences sooner, and the signal thread wakes
about twice a frame.

**What stays after it** (read from the 139 profile, `20261128_001438`, cuts only at pass ends):
- The GPU between submissions: lamp 0.18-0.22 ms of 4.33-4.37 (3.2-3.4 submissions a frame), busy view
  0.40-0.54 of 5.19-5.31 (5.5-7.9), slow area 0.48-0.69 of 4.36-4.63 (5.2-7.9): 55-90 us a switch. The
  game waited for its previous frame 3.3-3.5 ms a frame at the lamp spot, 4.1-4.3 in the busy view and
  1.3-2.0 in the slow area: the GPU was the limit at all three, so a whole frame would have been queued
  in time, and the idle is the switches themselves. With the queue about 2 are left a frame: the
  frame's, and the presenter's, which must wait for the swapchain image (taking that image a frame
  earlier could fold it in; a later item). The profiler adds to these: unprofiled the vendor's counter
  read 98/98/94-95% busy.

## The GPU's time that is not the game's work (2026-11-28; to measure)

The usage counter counts all of this as busy. Per frame, from the same profile (lamp / busy view / slow
area):
- **Render passes begun again with the same targets:** about 32 / 40 / 38. By cause: a depth view
  change (depth writes switched, `renderer-3`) 12 / 17 / 17; the copy shader 7 / 9 / 11 (and its image
  copies 1 / 1 / 1); image barriers 7 / 7 / 4 (most moving images to shader read); other layouts or
  clears 3 / 3 / 3; buffer barriers 1 / 2 / 2.
- **Barriers that wait for earlier work:** about 9.5 / 8.4 / 5.2 (attachments 4.0 / 4.1 / 2.0, compute
  1.9 / 1.1 / 0.2, all commands 3.6 / 3.2 / 3.0). Each empties the GPU's pipeline before the next work
  starts.
- **The copy shader** (the game's copies, run as compute): 57-65 dispatches, 0.14-0.17 ms with their
  barriers.
- **Breadcrumbs:** one marker per command, about 1,800; 0.05 ms on the 3070 Ti (0.4.4); not measured on
  the RX 9070 XT. `BB_BREADCRUMBS=0` measures it.
- **The game's shaders as translated:** the bulk (about 3.5-4 ms). Whether they run as fast as they
  could (occupancy, registers) is not visible to the engine.

What each costs is not known: the engine's profiler times only between its labels, cannot see a
barrier's stall or a layout change's hidden work (a decompression), and itself ends passes. 0.4.4's
"a few microseconds a restart" was on the RTX 3070 Ti and is not carried over. The tool that sees all of
it without changing it is AMD's Radeon GPU Profiler (one frame captured at each spot: each barrier's
time, the passes' gaps, layout changes, the waves per draw). Then a design for what costs most.

## As built (engine id 140)

- **The word** (`vk_gpu_progress.{h,cpp}`): 64 bytes of host-visible, coherent host memory, kept for the
  process's life as the breadcrumbs' buffer. `BB_GPU_FENCES=cpu`, or a device without the marker: the
  old path, said in the log at start.
- **Marks** (`Rasterizer::RecordUnitMark`): at the end of each graphics and compute submission of the
  game (`QueueUnitEnd`), at the GPU-idle point and at the frame's end (`FrameEndTask`,
  `FramesRetiredTask`), only with work recorded since the last submission (a mark alone would make a
  submission of nothing). A signal registered takes the next mark's number.
- **Which signals go early** (`SignalAfterGpu`'s `SignalKind`): plain EOP and EOS fences and the GPU
  idle signal. Not: interrupts, clock and counter values, the WRITE_DATA the GPU writes in place, the
  frame retirements, and any signal after a GPU write into the game's memory in place
  (`BbGpuQueue::in_place_writes`: bindings written in place, GPU WRITE_DATA, DMA the GPU does). The
  queue stays in order, so a signal that must wait holds back those after it. The GPU idle signal goes
  early because the GPU command thread runs dry about 20 times a frame: tick-bound, it would have held
  back every fence after it until its whole submission ended.
- **Who writes them** (`GpuQueueLogic::RunReady`, `Rasterizer::RunReadySignals`): one thread at a time
  under `signal_run_mutex`; the GPU command thread at the end of each of the game's submissions
  (`ForwardPassedSignals`, only when the word moved, giving way to the signal thread), the signal thread
  at each submission's end (as before, now also by marks).
- **The hand-over** (`QueueUnitEnd`, `GpuQueueLogic::Decide`): at each submission the callback sets the
  hand-over mark (`HandOverMark`: the mark before the last unit given, or the last mark when the
  submission has none of its own). At a unit's end: past it, or the GPU's last submission done (its
  known tick, no query): submit; else hold and watch the last submission; its end announces it
  (`Announce`, `SubmitForGpu`), and the recording thread submits at its next packet. The frame's end
  submits as before.
- **Counted** (`GPU queue:` line): submissions at the game's submission ends (the GPU on its last
  unit, out of work), when it ran out with work waiting, at a frame's end, by the safety net; marks a
  frame; fences written early (by which thread) and at their submission's end; those kept to the end by
  reason; the GPU's writes into the game's memory in place a frame.
- **Tested** (`tests/test_gpu_queue.cpp`, `gpu-queue-test`): the comparison across the wrap, the
  hand-over decision, two threads at the runner (in order, each once; ThreadSanitizer clean), and a
  model GPU and game in four cases using the same functions. With the GPU as the limit: 1.02
  submissions a frame; with the game the limit: one per game submission (the GPU taking each as it
  comes, as before); in every case no fence before its unit was done, all in order, and the GPU never
  idle while work waited for it.
- **The GPU cache's identity** changes (`liverpool.cpp`): `f5f2f67fe80d3585`.

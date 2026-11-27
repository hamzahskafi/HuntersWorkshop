# Beta 21 research: the GPU writes the game's fences

Status: research, proposed to the tester (2026-11-27); not built. The tester liked the idea and asked
for more research first; this is that research.

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

**2. Support is not universal.** AMD's Windows driver exposes the extension (its extension lists).
Mesa's RADV has it, and Mesa's Qualcomm driver implemented it for crash debugging. NVIDIA had not
implemented it when the D3D12-on-Vulkan translator vkd3d built `WriteBufferImmediate` on it (2019), and
no current source here says otherwise. Intel's Windows driver: not found. So the engine checks for it at
start, and devices without it keep today's path. That is a capability check, not a vendor rule.

**3. The core-Vulkan fallback proposed first is not viable.** A barrier plus a buffer write stops the
GPU's pipeline at each fence. At 600 fences a frame that is 600 drains. Devices without the marker keep
today's CPU-written fences.

**4. Fences alone do not set the submission count; the feeding rule does, and it needs no timing.**
Today's rule ("submit at a game submission's end when the GPU is down to its last submission")
ramps each frame up from small chunks: when the game starts a frame the GPU is finishing the
previous one, so the first draws go at once; the GPU finishes that small chunk quickly, the next is a
little bigger, and so on. That is 3.4-6.2 switches a frame. It is needed only because a fence becomes
visible at a submission's end. With the GPU writing the fences, the rule can be:
- submit at the frame's end (the game has handed over all of it): normally the whole frame in one
  submission;
- submit at once when the GPU has run out of work and something is recorded;
- submit at once when the game stops submitting mid-frame (it may wait on a fence in that work:
  today's idle submission, kept).
There is no timing, no estimate and no holding back, only events the engine already sees. With the
GPU as the limit (the tester's three spots: the game about a frame ahead, waiting 1.6-4 ms a frame
for the previous one), each frame's submission is queued while the GPU still runs the previous frame:
about 2 switches a frame, the frame and the presenter's own, with no gap between frames. Without the
GPU as the limit (a frame cap, a CPU-heavy moment), the second rule feeds it as soon as it runs dry, as
today. The fences reach the game as the GPU passes them whatever the submission size, which is what
makes whole-frame submissions safe. (A first draft held the work by an estimate of the GPU time
queued; the tester asked whether that added waiting, counting and delay. It did, and this rule
needs none.) Devices without the marker keep today's rule, since their fences still need a
submission's end.

**5. Kept from before:** suspending and resuming passes across the recording threads' cuts was
measured in 0.4.4 (betas 14 and 20/21): a cut pass costs a few microseconds, halving the cuts changed
neither GPU time nor FPS. Vulkan also needs the suspending flag when the pass begins, before a cut is
known. Not proposed again.

## Still unknown: a counting build first

Three facts decide how much of the idea applies. None can be read from the logs we have:
1. **Where the fences live:** the share of fence addresses inside the GX region's import, with no VRAM
   copy of their page (the GPU can write only those in place; the rest keep today's path).
2. **What kind:** 32-bit, 64-bit, clock or counter; with or without an interrupt; after work that wrote
   the game's memory in place.
3. **Whether the game blocks on fences mid-frame:** how often the GPU command thread runs out of work
   with fences pending and the game resumes submitting right after one is written. With whole-frame
   submissions such a wait is released by the third rule (the game stopped submitting), so how often
   and how long matters.

Also counted: distinct fence addresses a frame and back-to-back fences (no work between them), for the
marker count's cost.

These are counters only (no behavior change), with one line in the frame statistics. One run at the
three spots answers them.

## Expected, if the counts are favorable

- Switches: from 3.4-6.2 a frame to about 2 (the frame and the presenter's), on devices with the
  marker. The GPU idle between submissions falls by about two thirds: about 0.1 ms a frame at the
  lamp spot, 0.2 in the busy view, 0.3 in the slow area (profiled figures). The slow area gains most.
- The game sees its fences sooner. The signal thread does a few writes a frame instead of 600.
- Devices without the marker: unchanged.

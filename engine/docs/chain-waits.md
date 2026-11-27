# Beta 20 design: the render chain never waits on a lower role, and its cost a draw

Status: approved by the tester ("go ahead with beta 20", 2026-11-27) and built (engine id 136); see
"As built" at the end.

## Why

The tester's profile of both render-chain stages on engine id 135 (log `20261127_214212`:
`BB_SAMPLE_THREADS=shadPS4:GpuCommandProcessor,bb:DrawRec`, `BB_SAMPLE_TOP=200`; the lamp spot, then the
busy view, then the slow area). The builder named the sites from 135's executable. 135's sampler unwinds
a sample inside system code to its real engine caller, so the waits are placed now. Shares are of each
thread's time:

| | Busy view (about 1,790 draws, 195 FPS) | Slow area (1,337 draws, 205-212 FPS) |
|---|---|---|
| Stage A idle (its spin and its park) | 3.7% | 16.8% |
| Stage A waiting on the draw preparation's lock (`Collect`, `BeginSubmission`) | 12.0% | 11.7% |
| Stage A waiting on the submission queue's lock (`queue.m_access`) | 5.0% | 5.3% |
| Stage A in the pending write record (the list's scan, the table when the list is full) | 7.3% | 4.1% |
| Stage A copying constants into the ring (memcpy) | 3.4% | 3.5% |
| Stage A's statistics (`GetThreadTimes` a submission) | 1.7% | 1.5% |
| Stage B waiting for packets (its spin and its park) | about 14% | about 20% |
| Stage B's textures and image state (barriers, the LRU, the set) | about 6.5% | about 6.5% |
| Stage B's hand-over and bookkeeping (the recorder's wake, a sequentially consistent count a packet, a system call to ask whether a thread exists) | about 5.5% | about 5% |
| Stage B replaying registers | 3.3% | 2.6% |
| Stage B's heap allocations a draw | 1.5% | 1.3% |

## The design gap (beta 18)

Beta 18's principle 4 read "priority follows who waits on whom", and it was applied to work handed between
threads only. A thread also waits on whoever holds a lock it needs. The draw preparation's threads (the
Assist role, idle priority) hold the lock stage A (the Chain role, above normal) takes twice a submission,
and they hold it for 208 KB register-file copies and resets, and while they wake each other (a
`notify_all` under the lock). When Windows sets such a thread aside for the game's threads while it holds
the lock, stage A waits until it runs again: Windows' slim locks carry no priority to their holder. The
draw preparation ran at idle priority before beta 18 too; the old sampler could not place these waits
(the "unplaced 4.8%" of the first stage A profile).

The same shape, smaller or waiting to happen:
- the submission queue's lock, taken by the game's submit (the Frame role) and by stage A;
- stage B and stage A waiting for host copies (the copy threads, Frame) and for pipelines being compiled
  (the compile threads, Frame). Today only the burst compile threads are raised while a frame waits.

**Principle 4, corrected:** priority follows who waits on whom, through hand-overs and through locks.
- No lock is shared between roles on the frame's path. Hand-overs between roles are lock-free, or the
  lower role's side is made so that it holds nothing the higher role takes.
- A thread a higher role waits on for its job is raised for that wait (one mechanism in the roles module,
  generalizing the burst threads' raise).
- The check, built in: the role-aware lock counts every contended acquire where the holder's role is below
  the waiter's, per lock name, with the time waited; the `Roles:` line prints them ("waited on lower roles:
  ..."), so any tester's log on any CPU shows a violation, not only a profile. The sampler stays the second
  check: a Chain thread's wait that lands on a lock the type doesn't cover shows with its caller.

## The audit: every lock the chain shares with a lower role

The profile shows the locks that cost something at three spots on one PC. A lower-role holder is only a
problem when the system sets it aside while it holds the lock, which happens more the busier the CPU is: on
a CPU with fewer cores the same locks cost more. So the audit is of the code, every lock a Chain (or
Pacing) thread takes that a lower role also takes:

| Lock | Chain or Pacing side | Lower side | Seen in the profile |
|---|---|---|---|
| The draw preparation's (`vk_draw_prep`) | stage A, twice a submission | the preparation threads (Assist, idle): 208 KB copies and wakes under it | yes, about 12% of stage A |
| The submission queue's (`queue.m_access`) | stage A | the game's submit (Frame) | yes, about 5% |
| The decode tasks' frame pool (`bbport_frame_pool.h`, beta 19) | stage A | the game's submit (Frame) | not yet |
| The runtime's memory map lock (`runtime_memory_clamp`, `_region`) | stage A, every constant binding | the game's threads mapping memory (Frame) | not yet |
| The shader programs' lock (`programs_mutex`) | stage A | the preparation threads (Assist) | not yet |
| The pipeline compile queue's lock | stages A and B | the compile threads (Frame), the burst threads (Background) | not yet |
| The buffer cache's map-hook, load-upload and unmap locks | stage B | the game's threads (Frame) | not yet |
| The texture cache's prepared-textures lock | stage B | the game's create calls, `bb:GxPrepare` (Frame) | not yet |
| The video out port's and flip locks | the flip thread (Pacing) | the game's flip calls (Frame) | not yet |
| Host copies and pipeline compiles waited for | stages A and B | the copy and compile threads (Frame) | partly (stage B's waits) |

Converting each to a lock-free form would be case by case again, and the next lock someone adds would
reopen the gap. So the fix has a structural part and a general part.

## Part 1: no lock shared between roles (the gap, at its source)

1. **The draw preparation off stage A's locks.**
   - The game's submit already builds each submission's preparation record; it travels with the
     submission to stage A (a parameter of its decode task), so `BeginSubmission` finds it without
     searching the shared queue under the lock.
   - Stage A only marks a submission done (an atomic flag). Releasing done submissions (`Collect`) moves
     to the scanner thread, which already holds the lock for its own work.
   - The register-file copies and resets (208 KB each) are made outside the lock: the snapshot's position
     is read under it, the copy after, the position checked again.
   - Wakes after the lock is let go, and one thread woken where one is needed instead of all eight.
   - The rebase (when the scanner lags) is tried without waiting: a busy lock means it waits for the next
     submission.
   - Expected: stage A's 12% here, gone.
2. **The submission queue without a lock on stage A's side.** The game's threads that submit keep a lock
   among themselves (one role: numbering, the draw preparation's queue and the push stay in one order).
   Stage A takes from each queue through a single-consumer, lock-free list. Expected: about 5%.
3. **A lock that carries priority (the general part).** One lock type in the roles module,
   `BbRoles::Mutex` (and a shared variant), for every lock in the table and any added later on the frame
   path. On acquiring, it notes its holder (one plain store). Only when a thread finds it held does it look:
   if the holder's role is below the waiter's, the waiter raises the holder to its own priority (and high QoS,
   any core: the roles' raise) for the rest of the hold, and the holder goes back to its role when it lets
   go. Windows' locks lack this priority inheritance; it costs nothing until there is contention. The
   runtime's memory map lock (C) gets the same through a small hook into the roles module. The locks taken
   on stage A's own per-draw path (items 1 and 2) are removed instead, as even an inherited wait costs a
   wake.
4. **Raise on wait, one mechanism.** `BbRoles` gains a scoped raise for "a Chain thread is about to block on
   this thread's job": the host-copy wait raises the copy threads that hold its copies, a draw waiting for a
   pipeline raises the compile thread compiling it (standing or burst), each only if the wait blocks past its
   spin, and back at the end. The burst threads' raise becomes a user of it.

## Part 2: the chain's cost a draw

Stage A:
5. **The pending write record as a filter.** A counting filter over 64 KB pages of the writes stage B hasn't
   reached (incremented when one is noted, decremented when it's pruned) answers most constant lookups
   with one or two loads; only a hit scans the list. The list grows to 1,024 entries, so the hashed table
   is not needed in play (it filled the 64-entry list 31-34 thousand times a second). Expected: most of
   4-7%.
6. **The statistics' thread times** read once a second instead of every submission (statistics on only).

Stage B:
7. **The packet count** with a release store instead of a sequentially consistent add on every packet. The
   check for a sleeping stage A gets its fence before stage B waits for packets itself, so a wake is at
   most one packet late, never lost (as item 2 of beta 19 did for the other direction).
8. **No system call to ask whether the signal thread exists** (`jthread::joinable()` calls into the
   kernel on Windows): a flag set when it starts. About 1%.
9. **No heap allocation a draw:** the draw record's and the vertex attributes' buffers reused. About 1.5%.
10. **Textures touched once a frame:** the texture cache's LRU and the image's barrier state are updated
   once a frame per image, not once a draw. Part of 4%.
11. **The recorder woken only when it sleeps and work waits for it:** the hand-over's wake system call
   (1.7%) goes to the thread that will record next, and that thread spins a little longer before it parks
   (its spin is set by role, as beta 18 planned).

Each item has a switch this beta, as before (the role-aware lock's inheritance too: off, it only counts).

## Expected

- Stage A: about 25% of its time back in the busy view (waits 17%, the write record most of 7%), so about
  2.4 down to about 1.8-1.9 us a draw.
- Stage B: about 8-10%.
- The busy view (both stages near their limit): about +10%, until stage B is the limit there.
- The slow area: a shorter chain for each of its 10-12 syncs a frame; a smaller gain, harder to predict.
- The lamp spot: none (the GPU's, at 96%).

## Measurement

On the beta, frame statistics on: the three spots as in the profile run, then the same profile run
(both stages sampled) to check that no Chain wait lands on a lock any more.

## As built (engine id 136)

| Item | Where | Switch (this beta only) |
|---|---|---|
| 1. The draw preparation off stage A's locks: the game's submit hands the record over through a lock-free list (the scanner moves it into the queue) and to stage A with its decode task (`ProcessGraphics`'s parameter); stage A only marks a finished submission, the scanner collects it; the scanner sleeps on a parking word, woken by a system call only when it sleeps, and wakes one worker a buffer scanned; a rebase is tried without waiting | `vk_draw_prep.{h,cpp}`, `liverpool.{h,cpp}` | `BB_PREP_HANDOVER=old` |
| 2. Each queue's submissions read and popped by stage A without the queue's lock (`AmdGpu::SubmitQueue`: blocks of 64, an emptied block recycled); the game's submitting threads keep the lock among themselves | `submit_queue.h`, `liverpool.{h,cpp}` | `BB_SUBMIT_QUEUE=locked` |
| 3. The lock that carries priority (`BbRoles::Mutex`, `SharedMutex`): the holder noted at each take (a release store); a waiter behind a lower role counts the wait by the lock's name and raises the holder to its own priority until the holder lets go of its last role lock or the waiter gets the lock; a role without a priority of its own gets back the one it had. The `Roles:` line ends with "waited on lower roles". The runtime's memory map lock through C hooks, try-locks first | `bbport_role_lock.h`, `bbport_roles.cpp`, `src/runtime_memory.c`, `src/host_sync.h` | `BB_LOCK_INHERIT=0` (counts only) |
| 3, applied: the submission counters (`submit_mutex`, its wakes now after it), the decode tasks' frame pool, the draw preparation's, the shader programs', the pipeline compile queue's, the copy pool's, the buffer cache's pre-upload, load-upload and unmap locks, the texture cache's and its prepared textures', the video out port's, flips' and flip requests' | each lock's header | (with item 3) |
| 4. Raise on wait: a chain thread waiting on a pipeline raises the standing compile threads running a job too (slots beside the burst threads'), one waiting on host copies with nothing left to help with raises the copy threads running them; each goes back at its job's end (`BbRoles::EndRaise`) | `pipeline_compile_queue.h`, `vk_graphics_pipeline.cpp`, `bbport_copy.cpp` | `BB_RAISE_ON_WAIT=0` |
| 5. The pending write list: 1,024 entries, a counting filter over 64 KB pages of the live ones; a range written again among the newest 8 moves to the end (the list stays in packet order); the pending tables line adds the lookups the filter answered | `vk_pending.h`, `vk_rasterizer.cpp` | `BB_PENDING_FILTER=0` |
| 6. Stage A's thread times read ten times a second | `liverpool.cpp` | `BB_STATS_USAGE_EVERY=1` |
| 7. Stage B counts packets with a plain store; a stage A about to sleep issues a process-wide fence (`FlushProcessWriteBuffers`, a registered `membarrier` on Linux) | `vk_draw_pipe.h`, `bbport_park.{h,cpp}` | `BB_PIPE_COUNT=fenced` |
| 8. The signal thread's start a flag | `vk_rasterizer.{h,cpp}` | - |
| 9. `GetDrawOffsets` takes the pipeline's own `optional<const FetchShaderData>` | `vk_rasterizer.cpp` | - |

Changed from the design while building:
- **Item 1:** the register-file copies stay under the draw preparation's lock: stage A only tries it now
  (and the game's submit doesn't take it), so they hold up only the preparation's own threads. The
  doc's audit missed the submission counters' lock (`submit_mutex`), which stage A takes once a
  submission and the game's submit held while it woke threads: role-aware now, the wakes after it.
- **Item 7:** the design's per-packet release store with a fence before stage B waits could make a
  wake late by one long packet (a compile, an upload: milliseconds). The process-wide fence on the
  sleeping side keeps every wake exact and costs only when stage A sleeps, which it does after 500 us
  of waiting.
- **Item 9:** the profile's two heap sites (the draw record's own allocation and the vertex
  attributes' vector) were one: `GetDrawOffsets`, inlined into the draw record, took
  `optional<FetchShaderData>` while the pipeline hands out `optional<const FetchShaderData>`, so
  every call built a converted copy with its attribute vector on the heap.
- **Item 10 not built:** the texture cache's LRU is already touched once a frame per image (its tick
  advances once a frame); the profile's cost there is the first read of the image's state at its
  draw, which binding the texture needs anyway. Keeping the barrier state elsewhere to skip that read
  would let a wrong barrier through for about 2%; a finer profile first.
- **Item 11 dropped:** the recorder is woken only when it sleeps; the wakes counted are for the next
  segment's thread, asleep by design. Keeping it spinning would hold a core for each of the three
  recorders.

**Engine id 137 (the builder's review of 136):** Wine's and Proton's `FlushProcessWriteBuffers` is a
stub (Wine 9.0 prints `fixme:virtual:NtFlushProcessWriteBuffers stub`), so under them item 7's
handshake could lose stage A's wake at a drain's last packet, and stage A's sleep had no timeout: a
freeze. Now:
- under Wine (its `ntdll` exports `wine_get_version`) stage B keeps the fenced count;
- stage A's sleep looks again every 2 ms: a packet run with no wake is taken back by stage A itself
  (a missed wake costs that look, never a hang, on any system), counted only when stage B never took
  the waiter ("stage A's missed wakes", expected 0, in the draw pipe line, which also names the count's
  mode);
- a compiler fence between stage B's count and its look at the waiter, so their order is guaranteed,
  not incidental.
A first version counted a timeout landing while stage B was already sending the wake (between taking
the waiter and the wake's system call) as missed: 1-2 in 3,600 sleeps, in either mode. Counting only an
untaken waiter: 0 in 7,200, in either mode.

The GPU cache's identity changes with this beta (`liverpool.{h,cpp}`, `submit_queue.h`,
`vk_pipeline_cache.h`): `31aba0f240c65c6f`, from `cdca3ea71cd46d16` (unchanged by 137).

Tests: `role-lock-test` (real threads; counts everywhere, the raise on Windows or Wine), `submit-queue-test`,
`pending-test` (the filter against a model, with and without), `draw-pipe-handover-test` (now also stage A
sleeping behind slow packets: 300 sleeps, no lost wake), `pipeline-burst-test` (standing raises);
ThreadSanitizer clean here for the threaded ones.

# Beta 18 design: the game's thread handoffs and where its threads run

For the tester's review (2026-11-27). Nothing here is built yet.

## What the measurements say

Four kinds of run went into this: the tester's timelines (engine ids 131 and 132), the GPU profile with
render stage timings, and the thread sampler (`BB_SAMPLE_THREADS`). Logs `20261127_072134`, `_073834`,
`_170032`, `_170807`, `_173510`. All on the RX 9070 XT and i5-14600KF (6 performance cores with 2
threads each, 8 efficiency cores, 20 logical processors), at 1920x1080.

- **The slower spot** (about 1,400 draws, 177-181 FPS, 5.6 ms a frame). Nothing is saturated:
  - the GPU does about 4.1-4.3 ms of work a frame (82-86%, as the tester's overlay shows);
  - stage A is idle 11-13% of the time;
  - the game waits for the engine only 0.15 ms a frame.
  The pace is set by the game's main thread. Its time, from the sampler:

  | Main thread | Share |
  |---|---|
  | The game's own code | 55% |
  | One spin in the game's code (`+0x21b8890`) | 10% |
  | Waiting, in the system | 24-30% |
  | Engine code (much of it the game's own memcpy through the runtime) | 6-8% |

- **The threads the main thread waits on are mostly idle.** GXWorker 1-5 wait 82-83% of the time, and
  GXRenderThread 83-84%. In 132's timeline the other job workers wait 77-96%.
- **The main thread's waits are short and many.** 132's timeline records waits of 10 us or more:

  | Kind | Per frame | Median |
  |---|---|---|
  | Semaphore | 8.5 | 28 us |
  | Condition variable | 3.7 | 55 us |
  | Mutex | 1.7 | 14 us |

  Shorter ones aren't counted.
- **The lamp spot** has the same pattern. The main thread runs 3.75-4.0 ms of a frame, and stage A
  keeps pace with it.

So at the slower spot the frame is a chain of short handoffs: the main thread hands out work, and each
handoff ends in a block and a wake. That is where the spare CPU and GPU time goes.

**The main thread's spin is the game's fork-join barrier.** The builder disassembled 1.09 at `+0x21b8890`:
- The function at `0x21b8710` dispatches a job group.
- The main thread runs its own share, then wakes the group's other N-1 workers with one signal of count
  N-1 (`0x263b0d0`, most likely `sceKernelSignalSema`: the runtime's semaphore).
- It then busy-waits, with no `pause` instruction, until a counter reaches N-1 (every worker done),
  then on each worker's state word.

So the 10% is the main thread waiting for its own workers to wake and finish. With today's semaphore
(below), those workers start one after another.

**The engine's share of the main thread is mostly a shared counter.** The builder symbolized the
sampler's engine sites against 132's executable. `guest_memcpy` and `guest_memset` (`src/runtime.c`)
are sampled right after a locked increment of one global counter (`memory_calls`). Every guest memcpy,
memset, memmove and memcmp of every thread does that increment, and the counter is printed only at
exit. The draw records' counter (`BbNativeCalls::NoteWritten`) is the same pattern, on every draw a
thread writes. That cache line moves between all the game's threads.

## Part 1: the handoffs (the runtime's semaphores and read/write locks)

### What is wrong now

The game's semaphores and read/write locks are the engine's runtime. Its mutexes and condition variables
are per object, over Windows SRW locks and condition variables, and are not part of this.

**Semaphores** (`src/runtime_sema.c`):
1. **One lock for every semaphore of every thread.** Each wait, signal and poll takes the same
   process-wide lock (`lock`), then finds the semaphore by walking a list under it.
2. **The signaller wakes its waiter while still holding that lock.** The waiter sleeps in
   `SleepConditionVariableSRW` on that same lock. Woken, it must take the lock back before it can
   return, so it blocks again until the signaller lets go: two wakes per handoff.
3. **Every other thread's semaphore work queues behind that lock meanwhile.**
4. **Every blocking wait calls `getenv("BB_TRACE_SEMA")`,** which takes the C runtime's environment
   lock: another lock shared by all threads.
5. **Contended game locks end up here too.** The engine replaces the game's `DLAdaptiveMutex::Lock`
   (every heap, GX and queue lock: `gpu/shim/engine/job_pool.cpp`). A contended one spins for its budget,
   then sleeps on its semaphore through this same wait.
6. **The fork-join barrier pays twice.** A signal of count N-1 wakes N-1 waiters, each under the shared
   lock. Each woken worker must then take that lock back in turn, so the workers start one after
   another while the main thread spins (above).

**Read/write locks** (`src/runtime_rwlock.c`): 214,000 operations a second on 8 live locks (the log's
Load line: the game's busiest locks).
- **Each lock** takes the process-wide `registry_lock` twice, walks the registry and allocates a
  holder record.
- **Each unlock** takes it again and frees that record.

That's about 640,000 acquisitions of one lock a second, across every thread that touches a game heap.

**Shared counters in the game's hottest calls:**
- `memory_calls` takes a locked increment in every guest memcpy, memset, memmove and memcmp
  (`src/runtime.c`).
- `written_records` and `written_fallback` take one per draw record (`bbport_native_calls.cpp`).

One cache line is written by every game thread, and the samples land on those increments.

### Design

**Semaphores:**
- **Lookup.** The id names a slot in a fixed table: a generation in the high bits, the slot in the low
  bits. Lookups take no lock.
- **Records are reused, never freed** (type-stable memory). A late call with a deleted id still finds a
  valid record, takes its lock, sees the id changed, and returns the PS4's "no such semaphore"
  (`ESRCH`), as today.
- **One SRW lock per semaphore,** held only to change its count and its waiter queue.
- **Waiters.** Each waiter sleeps on a word of its own with `WaitOnAddress` (a futex on Linux), not on
  a condition variable.
  - The signaller, under the semaphore's lock, unlinks the waiters it satisfies and marks each served.
    It lets the lock go, then wakes them.
  - A woken waiter has nothing to take back: one wake per handoff.
  - Waking a word whose waiter has already returned is harmless: waiters loop until their word says
    served.
- **Order as today.** The queue is FIFO, and a signal serves the first waiters its count satisfies.
- **Timeouts.** The waiter wakes at its deadline, takes the lock, and removes itself if still queued. A
  signal that got there first keeps its result. The timeout precision is as today on Windows
  (milliseconds).
- **Cancel and delete** mark every waiter with today's PS4 error codes and wake them after the lock is
  let go.
- **Diagnostics kept.**
  - `BB_TRACE_SEMA` is read once.
  - The freeze and hitch reports' block slots, `runtime_wait_note` (the timeline) and the freeze
    report's descriptions all stay.
  - The counters become atomic.
- **No spin of our own.** `WaitOnAddress` already spins briefly before it sleeps (Microsoft's
  guidance), and long spins are what the guidance warns against. A spin is added only if the
  after-measurement shows latency left.

**Read/write locks:**
- **The guest's handle points straight at its record:** no registry walk. Records are reused, never
  freed, and carry a state word, so an unknown or destroyed handle answers `EINVAL` as today.
- **First use of a statically initialized lock** installs a record with one compare-and-swap. Two
  threads racing: the loser's record goes back to the pool.
- **Which thread holds what** lives in a small per-thread table: no lock, no allocation. It gives
  today's answers (`EDEADLK` on a write after its own read or write, `EPERM` on an unlock it doesn't
  hold). It also tells unlock which mode to release.
- **`EBUSY` on destroy** comes from two atomic counters per lock (holders, and threads on their way
  in).
- **The same Windows SRW lock underneath.** Timed acquires keep today's try, yield and sleep loop.

**The shared counters:**
- `memory_calls` is dropped. It is an exit-time total nobody reads in play; the sampler measures the
  same thing better.
- The draw record counters become per-thread counts, summed when the statistics line is printed.
- The statistics stay the same.

**The barrier's spin gets a `pause`**, as a game fix beside the existing "job lock spins with pause".
- The loop at `0x21b8890` keeps its logic; it gains a `pause` per pass.
- A spin without one takes a hyperthreaded core's resources from the thread beside it. That thread can
  be one of the workers being waited for, or stage A.
- `pause` costs about 140 cycles a pass on these cores (Intel's figure, about 30-50 ns), nothing next
  to a worker's wake-up.
- Checked against 1.09's code like the other fixes, and left out when the code differs.

**Alternatives considered:**

| Alternative | Why not |
|---|---|
| Spin before blocking (`BB_WAIT_SPIN_US`) | Hides the latency and burns a core while doing it; leaves the shared lock and the double wake |
| Windows kernel semaphores | A system call on every operation, even uncontended |
| Signal after unlock, everything else kept | Removes the double wake but keeps every thread queued on one lock |

### What it should change

- **Per job batch:** the workers start together instead of one after another, and the barrier's spin
  shortens accordingly.
- **Per semaphore handoff:** one wake instead of two, no list walk, no environment lock.
- **Across the whole game:** no thread queued behind another thread's unrelated semaphore or
  read/write lock. About 640,000 shared-lock acquisitions and 214,000 allocations a second go away. So do the locked increments in every memcpy and every draw record.

How much frame time that is can't be computed beforehand. At the slower spot the main thread waits
1.3-1.7 ms a frame and spins 0.55 ms; the run decides.

### Tests

- `tests/test_sema.c` (the PS4 contracts) and `runtime-test`, under Wine and natively.
- New stress tests, many threads at once:
  - semaphores: waits with needs above 1, timeouts, cancel and delete during waits, stale ids;
  - read/write locks: readers and writers, static initialization racing, destroy while busy, the
    error answers.

## Part 2: where the threads run (priorities and cores)

### Now

| Threads | Windows priority | Cores |
|---|---|---|
| The flip thread (vblank watch, pacing) | highest | any |
| Stage B (`bb:DrawRec`), 3 Vulkan recording threads | above normal | any |
| 4 copy threads (on 16+ hardware threads; mostly asleep) | above normal | any |
| **Stage A** (`shadPS4:GpuCommandProcessor`), lookahead, GPU signals, swap thread | **normal** | any |
| **All ~60 game threads, the main thread and GXRenderThread included** | **normal** | any |
| FMOD's threads | normal (above with the launcher's "Sound threads first") | any |
| 7 draw preparation threads, the scanner, 4 compile threads | idle | any |
| 8 burst compile threads | idle | efficiency cores |

The game asks for core affinities for its threads (the PS4's 7 cores). They are recorded and not applied,
which is right: they mean nothing on a PC.

### What Windows does with them

- **Priority is a secondary placement signal.** Intel's hybrid guide: "Threads marked as high priority
  will naturally be assigned to higher performance cores, but this is a secondary consideration." And:
  priority alone can misfire.
- **CPU sets are the documented way** (Microsoft's GDK guide on hybrid CPUs):
  - use CPU sets, not affinity masks;
  - "On machines with three or more performance cores, you could restrict the frame critical threads to
    only those higher performance cores", letting them float across all of them;
  - job threads should float across all cores.
  CPU sets are soft (a preference the scheduler works with); affinity masks are hard and advised
  against.
- **Measured caveats.** Microsoft notes that floating critical threads can sometimes run faster, and
  that locking them improves the 1% lows. Another project found its gain within noise. So the change is
  measured on and off.
- **Wake-up placement.** A woken thread goes to an idle core first, and the efficiency cores are idle
  most. That is the likely reason the main thread wakes on one about half the time (132's timeline).
  It isn't the engine's raised threads: those few are mostly asleep.

### Design

**The frame-critical chain:** the game's main thread, GXRenderThread, stage A, stage B, the 3 recording
threads.
- **Priority above normal.** Stage A rises from normal. Not higher: both guides warn against extreme
  priorities, and the main thread spins.
- **CPU sets on hybrid CPUs with 4 or more performance cores:** all of their logical processors, so
  these threads float across the performance cores and nowhere else.
  - 4 cores is Microsoft's three-or-more rule, plus room for the chain's 2.5-3 cores of load.
  - Fewer performance cores, or one kind of core: priority only.
- **Set once, when each thread starts or gets its name.** Intel warns against changing affinity often.
- **The game's two threads are found by their names** (`main`, `GXRenderThread`). Their names are logged
  once.

**The barrier's workers.** The game's fork-join workers are on the critical path while the main thread waits
for them (Microsoft: a job thread becomes frame-critical while it holds the frame up). Keeping them on
the performance cores as well is measured as a variant, not made the default:
- it would put about 10 threads on 12 performance-core slots;
- Microsoft's guidance is that job threads float.
They are found by name: the GXWorker threads, the likely members of this group. The variant also
confirms which threads the group uses, since their waits and the barrier line up in the timeline.

**Everything else stays as it is.**
- Workers stay normal and float everywhere: the game's job threads (GXWorker, Havok, EzWorkPool,
  character and cloth threads), lookahead, GPU signals, the swap thread, GX prepare. Per Microsoft's
  guidance, job threads float.
- Background work stays idle, the burst threads on the efficiency cores.
- Audio, the copy threads and the flip thread stay as they are.

**Alternatives considered:**

| Alternative | Why not |
|---|---|
| Priority only | A secondary signal; doesn't keep the main thread off the efficiency cores |
| Hard affinity masks | Advised against by both guides; can't adapt |
| Pinning the game's workers to the efficiency cores | Their handoffs are on the critical path; job threads should float |
| MMCSS ("Games" task class) | Near-realtime priorities for threads that spin and wait: risky |
| The power plan's hybrid scheduling policies | System settings, outside the game |

**Linux:** raising priorities needs privileges there, and CPU affinity could follow later. Linux builds are
paused.

## One build, measured three ways

Both parts go behind temporary Extra variables for this beta only, removed again before the release
candidate:
- `BB_RUNTIME_SYNC=0` (the old semaphores and read/write locks);
- `BB_THREAD_PLACEMENT=0` (the old priorities and cores).

**The tester's runs.** The slower spot, a few minutes each, frame statistics and
`BB_SAMPLE_THREADS=main` on:
1. both off;
2. part 1 only (`BB_THREAD_PLACEMENT=0`);
3. both on;
4. both on, with the barrier's workers on the performance cores (`BB_THREAD_PLACEMENT=workers`).

And the lamp spot once with the best of these.

**Compared:**
- FPS;
- the main thread's waiting and spinning shares;
- the frame-ending thread's wait for the engine;
- with `BB_TIMELINE`, optionally: how often the main thread wakes on an efficiency core.

Part 2 is kept only if it helps on its own, or at least helps the 1% lows.

## Resolved: the main thread's spin at `+0x21b8890`

The game's fork-join barrier (above, from the builder's disassembly of 1.09). Part 1 shortens it, with
the workers woken together and the spin given a `pause`.

## Sources

- Microsoft, [Game Performance in Hybrid CPU Environments](https://learn.microsoft.com/en-us/gaming/gdk/docs/gdk-dev/pc-dev/overviews/game-performance-hybrid-cpu-environments?view=gdk-2610)
  (GDK, 2026).
- Intel, [Game Dev Guide for 12th Gen Intel Core Processor Hybrid Architecture](https://www.intel.com/content/www/us/en/developer/articles/guide/12th-gen-intel-core-processor-gamedev-guide.html)
  (2022).
- Microsoft, [WaitOnAddress](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress)
  and [SleepConditionVariableSRW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleepconditionvariablesrw).
- Preshing, [Semaphores are surprisingly versatile](https://preshing.com/20150316/semaphores-are-surprisingly-versatile)
  (lightweight semaphores over atomics).
- The [Lichii pull request](https://github.com/zortos293/Lichii/pull/232) on performance cores for
  time-critical threads (gain within noise).

# Beta 18 design: thread roles and handoffs

For the tester's review (2026-11-27). Nothing here is built yet.

This replaces the first draft, which handled each kind of CPU as a special case. The tester's direction:
the way the engine starts its threads and hands out work should be designed once, so that each thread is
flagged for what it is and the system places it; where scaling matters, the design scales by itself.
That fixes the problem at its source, keeps it from coming back, and covers machines nobody has tested.

## Why: what the measurements say

The evidence comes from:
- the tester's timelines (engine ids 131 and 132), the GPU profile with render stage timings, and the
  thread sampler (`BB_SAMPLE_THREADS`), logs `20261127_072134`, `_073834`, `_170032`, `_170807` and
  `_173510`;
- the builder's symbolization of 132 and disassembly of 1.09.

All on an RX 9070 XT and an i5-14600KF (6 performance cores with 2 threads each, 8 efficiency cores),
at 1920x1080.

**The slower spot** (5.6 ms frames). Nothing is saturated:
- the GPU does 82-86% of a frame's time;
- stage A is idle 11-13% of the time;
- the game waits for the engine 0.15 ms a frame.

The game's main thread sets the pace. Its time:

| Main thread | Share |
|---|---|
| The game's own work | 55% |
| Spinning at the game's fork-join barrier (`+0x21b8890`) | 10% |
| Waiting | 24-30% |
| Engine code | 6-8% |

What these come from:
- **The barrier.** The main thread wakes its job workers with one semaphore signal of count N-1, then
  busy-waits, without `pause`, until they finish. The workers it waits on are idle 77-96% of the time.
- **The waits.** About 14 a frame of 10 us or more: semaphores (median 28 us), condition variables
  (55 us), mutexes (14 us).
- **The engine code.** Mostly locked increments of counters that every game thread shares, in every
  guest memcpy, memset, memmove and memcmp and in every draw record.

**The cause is in the engine's own code.** The runtime gives the game:
- **semaphores** behind one process-wide lock. A signal wakes its waiters while still holding it, so
  each woken thread blocks again until the lock is free. The barrier's workers therefore start one
  after another.
- **read/write locks** that take a process-wide lock twice and allocate on each of 214,000 operations
  a second.

**And thread placement is decided ad hoc.** It's spread over about ten call sites:
- `MakeCritical`, `MakeBackground` and raw `SetThreadPriority`, an above-normal rule for 16 or more
  hardware threads, a spin cap for fewer, CPU sets for the burst threads only;
- stage A, the busiest engine thread, at normal priority with the game's 60 threads.

Nothing states what each thread is for, so each new thread or new machine needs a new rule.

## Principles

1. **Every thread has one role, declared once,** where the engine starts it, or from a table of the
   game's thread names. Nothing else sets priorities, QoS or cores.
2. **A role states the thread's relation to the frame. One module translates it into the system's own
   flags**: priority, quality of service, preferred cores. The system places the thread; no model of
   CPU is named anywhere.
3. **Preferred cores come from one rule over the topology,** not per-CPU branches. A role may prefer
   the fastest or the most efficient class of core, only while that class can hold the role's threads.
   On a CPU of one kind every class is all of it, so the rule does nothing there by itself.
4. **Priority follows who waits on whom.** A thread never sits above a thread it busy-waits on. Threads
   above others never busy-wait on them; they park.
5. **Every wait parks on a word of its own, and is woken after the lock is let go.** Spinning, when
   there is any, is bounded and decided by the waiter's role.
6. **Counters in hot paths are per thread,** summed when they are printed.
7. **Each role shows in the log:** its threads, its CPU time, and the cores it ran on. Any machine's
   log then says whether placement works there.

## 1. Roles

One module, `bbport_roles.{h,cpp}`. A thread gets its role when it is named: the engine's threads by
their name, through one table, in `Common::SetCurrentThreadName` (and the three threads that name
themselves directly); the game's threads by their PS4 priority, when the runtime names them. No thread
start site changes. The old helpers stay as the beta's switch-off path and defer to the roles while
they are on: `MakeCritical` leaves the role's priority, `MakeBackground` puts a Background or Assist
thread back at its role, `SetPriority` (a raise while a frame waits) goes through the role. The burst
threads' CPU-set steering and the runtime's audio priority are the roles' under them.

| Role | What it is | Engine threads | Game threads |
|---|---|---|---|
| **Pacing** | Tiny, deadline-driven, must never be late | flip thread (`shadPS4:PresentThread`), vblank watch | - |
| **Chain** | The engine's frame chain: work nothing else can do, every frame | stage A (`shadPS4:GpuCommandProcessor`), stage B (`bb:DrawRec`), `bb:VkRecorder` x3, `bb:GpuSignals` (fences the game waits on), `bb:Present` (swap) | - |
| **Frame** | The game's frame work, its fork-join workers included, work a frame may wait on, and work-ahead that only helps if it runs now | `bb:Pipeline*` (compiles), `bb:Copy*` (host copies), `bb:DrawAhead`, `bb:GxPrepare`, `bb:ReadySurfaces` | `main`, `GXRenderThread`, `GXWorker:*`, `HavokWorkerThread`, `CSChrThread`, `CSClothThread`, `EzWorkPool_Type`, every game thread not FMOD's |
| **Assist** | Work ahead that the chain uses only if it is done in time | `bb:DrawPrep*`, `bb:DrawScan` (draw preparation) | - |
| **Background** | Work ahead that nobody waits for this frame; a frame that needs it raises it | `bb:PipelineBurst*`, `shadPS4:PipelineCacheIO`, `bb:Prefetch` (the runtime's file read thread) | - |
| **Audio** | Periodic, short, deadline-driven | - | FMOD's threads (the game's PS4 priorities 256-300) |
| **Service** | Everything with its own pace | `bb:window`, `bb:kernel_service`, network, the watch threads, the sampler, video playback threads | - |

The game's threads get their role from their PS4 priority when the runtime names them (at their start
and at `thread_rename`, `thread_set_prio`): FMOD's (256-300) are Audio, every other is Frame, which is
today's treatment. An engine thread whose name is not in the table is left as the system has it and
named once in the log.

### What a role means to the system

| Role | Windows priority | Windows QoS (`ThreadPowerThrottling`) | Preferred cores | Linux |
|---|---|---|---|---|
| Pacing | its own (the flip thread sets time-critical itself) | high | any | as it sets |
| Chain | above normal (normal with `BB_GPU_THREAD_PRIORITY=0`) | high | the fastest class, if it holds the role | normal |
| Frame | normal | high | any | normal |
| Assist | idle | high | any | `SCHED_IDLE` (as before) |
| Audio | normal; highest with the launcher's "Sound threads first" | high | any | normal |
| Service | untouched | untouched | untouched | untouched |
| Background | idle | eco | the most efficient class, if it holds the role | `SCHED_IDLE` (as before) |

Notes on these choices:
- **QoS** is Windows' documented way to say how important a thread is (Microsoft's GDK guide).
  - Eco threads go to efficiency cores and slower clocks on a hybrid CPU, and to slower clocks on any
    other CPU.
  - "High" is stated explicitly, so Windows never decides a game or chain thread is unimportant.
  - With QoS doing the main job, priority only orders threads that compete for the same core.
- **Chain above Frame, Frame equal among themselves.** This follows principle 4:
  - The game's main thread busy-waits for its own workers, so they share a level.
  - The chain never busy-waits on game threads: its idle spins are bounded, then it parks.
  - The chain is above the game today already, except stage A, which now joins it.
  - So no machine can get a raised thread spinning on a lower one, the risk the first draft solved with
    a special case.
- **Background promotion.** A frame that waits on a background thread raises it to the waiting
  thread's priority until its next job (today's `RaiseRunning` for compiles), with high QoS and any core
  together.
- **Assist** (added while building). Draw preparation runs ahead for stage A, which uses its work only
  if it is done in time. Eco clocks or the efficiency cores would make it late more often, so it keeps
  high QoS and any core; idle priority keeps it from taking a core from anything else.
- **Phases** (found on review). Background is background only while the game plays.
  - During a loading screen and while the GPU cache settles at start, the player waits on exactly that
    work: compiles, draw preparation, the file thread.
  - So the mapping takes the phase as well as the role: while loading or settling, Background maps to
    normal priority and high QoS, and to any core.
  - The engine already knows when a loading screen begins and ends (`engine/loading.h`). The roles
    module is told, and re-applies the mapping to Background's threads then. That's a few calls per
    load, as Intel advises against changing affinity often.
  - The start is covered the same way: the engine's own warm-up compiles count as loading.
  - The GPU cache's settle itself (recompile and catalog seed) runs in its own process, the shader
    lab (`RunLab`, `vk_pipeline_serialization.cpp`). It's outside the roles and keeps its normal
    priority, as today.
  - Without this, eco clocks on the efficiency cores would make loads and the first start slower.
- **Linux.** Raising needs privileges there, and a thread lowered to `SCHED_IDLE` cannot be raised
  again without them. So on Linux the threads keep what they set themselves today (draw preparation and
  the burst threads at `SCHED_IDLE`); the roles name and count them. Linux builds are paused.

### The core preference rule

The topology is the one the engine already reads at start (`bbport_cpu_layout`): each logical
processor's efficiency class, core and last-level cache.
- **A role's preferred set** is all logical processors of the fastest class (Chain) or the most
  efficient class (Background).
- **It only applies while the set holds at least as many logical processors as the role has busy
  threads.** Chain has about 6 busy (stage A, stage B, recorders, signals, swap). Otherwise the role
  takes all cores.
- **Results:**

  | CPU | Chain | Background |
  |---|---|---|
  | Hybrid desktop (6 performance cores) | performance cores | efficiency cores |
  | Hybrid laptop with 2 performance cores | all cores (4 logical are too few for 6 threads) | efficiency cores |
  | Ryzen, or Intel before 12th gen | all cores | all cores |

  No CPU is named; the numbers decide.
- **CPU sets are soft:** Windows works with them (Microsoft, Intel). They are set once per thread
  (Intel: never per frame), and the process's affinity, if anyone restricts it, is respected.
- **The same rule can later prefer one last-level cache group for the chain** (Ryzen with two core
  complexes). It is a second criterion in the same rule, not a branch. It waits for a tester with such
  a CPU, as it would change behaviour nobody here can measure.

### Pool sizes

The engine sizes its pools in five places today:
- recording threads: `RecordingThreadCount`;
- copy threads: hardware threads / 4, 1 to 4;
- compile threads: 4;
- burst threads: up to 8, on the efficiency cores;
- draw preparation threads: 7.

They move into the roles module as one function of the topology, with today's values reproduced. The
rule behind them:
- Chain pools scale with what the chain's work needs: it is serial, so a few threads.
- Background pools take the cores the rest leaves idle. Their idle priority means oversizing them
  costs nothing, and undersizing wastes idle cores.

Changing the values comes later, with data. Beta 18 only puts them in one place.

## 2. Handoffs

### One parking primitive

`host_park(word, expected, deadline)` and `host_unpark_one/all(word)`, in `src/host_sync.h`:
- `WaitOnAddress` and `WakeByAddressSingle/All` on Windows (they spin briefly before sleeping:
  Microsoft);
- `futex` on Linux.

A thread parks on a word of its own and returns only when that word says so. A spurious wake or a
wake meant for a thread that has gone is harmless.

### Semaphores (`src/runtime_sema.c`)

- **Lookup.** Ids name slots in a fixed table, with a generation in the high bits. Lookups take no
  lock.
- **Records are reused, never freed.** A stale id still finds a valid record, takes its lock, sees
  the id changed, and answers `ESRCH`, as today.
- **One lock per semaphore,** held only to change its count and its queue.
- **Waiters park on their own word.** A signal unlinks the waiters its count satisfies, marks them,
  lets the lock go, then wakes them. A signal of N-1 (the barrier) wakes N-1 threads at once, each with
  nothing to take back.
- **Kept as today:**
  - FIFO order, with the first waiters the count satisfies served;
  - timeouts: the waiter takes the lock at its deadline and leaves the queue if still in it;
  - cancel and delete marking every waiter with today's PS4 error codes;
  - the freeze and hitch reports' block slots and descriptions, and the timeline's wait notes.
- `BB_TRACE_SEMA` is read once, not by `getenv` on every blocking wait.

### Read/write locks (`src/runtime_rwlock.c`)

- **The guest's handle points straight at its record.** Records are reused and carry a state word,
  so an unknown or destroyed handle answers `EINVAL`.
- **A statically initialized lock is installed on first use** by one compare-and-swap.
- **What each thread holds lives in a small per-thread table:** no lock, no allocation. It gives
  today's `EDEADLK` and `EPERM`, and tells unlock which mode to release.
- **`EBUSY` on destroy** comes from two atomic counters.
- **The same SRW lock underneath.**

### The game's own waiting code

- **The adaptive lock.** The engine replaces the game's `DLAdaptiveMutex::Lock`
  (`gpu/shim/engine/job_pool.cpp`). It already spins with `pause`, then sleeps on the mutex's
  semaphore, which the game's own unlock signals. It stays semaphore-based, and gains through the
  semaphore fix.
- **The fork-join barrier** (`0x21b8710`, the loop at `0x21b8890`) becomes a checked game fix, like
  "job lock spins with pause" (`job_barrier`; `BB_GAME_FIXES_SKIP=job_barrier` leaves it out):
  - `pause` on each pass;
  - after about 50 us, a yield on each pass (`SwitchToThread`), so a worker can take the core when
    cores are scarce;
  - where cores are spare, the yield finds nobody and costs nothing.
  With Frame threads at one level (principle 4), the yield always reaches the workers.
  - As built (`gpu/shim/engine/job_barrier.h`): the dispatcher waits twice, first for the group's
    counter to reach N-1 (`0x21b8890`, or until the byte at `0x56a6c31` says more work arrived), then
    for each worker's slot to read 2 (`0x21b8940`). Each loop becomes a 14-byte jump to a cave that
    calls the engine's wait and leaves by the game's own exits, after the same conditions in the same
    order. The whole dispatcher is checked against 1.09 (FNV-1a over its 0x282 bytes), and the
    builder's scan of its branches found none landing inside either site.
- **A compromise, named as one.** The main thread stays at Frame's level because it busy-waits on its
  workers.
  - On a small CPU, an unraised main thread can also wait its turn behind other game threads. That's
    the same cost that made the engine raise its own chain.
  - The better fix is a barrier that parks instead of spinning: the workers' completion (the game's
    increment of the group's counter) also wakes the main thread. That patches the game's workers as
    well as its dispatcher.
  - Once the barrier parks, the main thread can be raised safely, since it no longer busy-waits.
  - Beta 18 keeps the safe choice and measures it with `BB_TEST_CPUS=8`. If the main thread waits for
    cores there, the parking barrier is a beta 19 item.

### The engine's own waits

These are stage A's wait for submissions, the draw pipe's stage waits, the recorders' waits and the
host-copy waits. They have their own bounded spins and sleeps today.
- They move onto the parking primitive, with their spin decided by role through one function
  (today's `SpinLimitUs` and `CapSpinUs`).
- That is a refactor of measured code, so it is staged after the guest primitives (below).

## 3. Counters

`BbStats::Counter`, one type: a per-thread slot (cache-line sized, indexed by a thread number given at
first use), summed when the statistics are printed.
- **Into it go:**
  - the draw record counters (`BbNativeCalls::NoteWritten`);
  - every statistics counter a hot path touches while the statistics are on (stage A and B's
    `BbStats` atomics).
- **Dropped:** `memory_calls` in `src/runtime.c`, an exit-time total in every guest memcpy, memset,
  memmove and memcmp, never read in play. The sampler measures the same better.

## 4. Seeing it work

A `Roles:` line with the frame statistics, every window, per role:
- threads;
- CPU time in cores (`QueryThreadCycleTime` deltas, one call per thread per window);
- the share of the role's wake-ups on each core class (from the parking primitive, while the
  statistics are on).

For example, `Chain 9 threads, 2.8 cores, 96% on the fastest class; Frame 44, 3.1 cores, 61%;
Background 24, 0.3 cores, 93% on the most efficient class`. Any tester's log then shows whether the
placement holds on their machine.

Coverage in beta 18: the core shares come from the game's threads' wake-ups in the runtime (condition
variables, mutexes, semaphores, sleeps; `runtime_wait_note`), while the statistics are on. The engine's
threads join from beta 19, when their waits move onto the parking primitive, so in beta 18 their roles
show CPU time but no core share. On a CPU of one kind there is no share to show.

The barrier fix adds a `Fork-join waits:` line: the dispatcher's waits per frame, their time (the count
wait and the slot wait), the share that went past 50 us, and the yields per frame. It is the number
beta 19's parking barrier would remove.

## What changes in the code

| Where | Change |
|---|---|
As built:

| Where | Change |
|---|---|
| `gpu/shim/bbport_roles.{h,cpp}` (new) | Roles, the name table, the mapping to the system, the core rule, the phases, raises, pool sizes, the `Roles:` line, `BB_TEST_CPUS` |
| `gpu/shadps4/common/thread.cpp` (`SetCurrentThreadName`), `gx_resources.cpp` | The engine's threads take their role from their name |
| `gpu/shim/bbport_platform.cpp` | `MakeCritical`, `MakeBackground`, `SetPriority` defer to the roles while they are on |
| `vk_scheduler`, `bbport_copy`, `vk_graphics_pipeline`, `vk_draw_prep` | Pool sizes from `BbRoles::PoolSize` (today's values); the burst threads' CPU sets from their role |
| `engine/loading.cpp`, `engine/startup.cpp` | The loading phase: each loading screen, and the start up to the first frame |
| `src/runtime_thread.c`, `src/runtime_file.c`, `src/probe.c` | The game's threads by PS4 priority, the file read thread, the main thread; `BB_TEST_CPUS` at start |
| `src/host_sync.h` | `host_park` / `host_unpark` |
| `src/runtime_sema.c`, `src/runtime_rwlock.c` | Rebuilt (section 2), the old code kept under `BB_RUNTIME_SYNC=0` |
| `src/runtime.c` | `memory_calls` dropped |
| `gpu/shim/engine/job_barrier.{h,cpp}` (new), `game_fixes.cpp` | The barrier fix (`job_barrier`), its `Fork-join waits:` line |
| `gpu/shim/bbport_counter.h` (new), `bbport_native_calls.cpp` | `BbStats::Counter`; the draw record counters on it |
| `tests/test_sync.c`, `tests/test_job_barrier.cpp`, `tests/test_roles.cpp` | The primitives under stress; both barrier caves run for real; the roles on Windows |

### Changed while building

- **Copy threads are Frame, not Chain.** The engine learned in 0.4.4 that raising them above the game
  on small CPUs took the cores the game needed while an area streams in (`MakeCritical`'s `bulk`).
- **Compile threads (`bb:Pipeline*`) are Frame, not Background.** Draws wait on them; their queue
  already runs first in, first out for that. The burst threads stay Background.
- **Draw preparation gets a role of its own, Assist** (above).
- **The flip thread keeps its own priority** (it sets time-critical itself); Pacing adds high QoS only.
- **Service threads are left entirely as the system has them.**
- **No per-site `Assign` calls**: the name table at the one naming function covers every engine thread,
  and a new thread with a new name shows in the log as having no role.
- **`BB_GPU_THREAD_PRIORITY=0`** (the launcher's experiment and safe mode) keeps the chain at normal under
  roles too.
- **The barrier fix covers both of the dispatcher's waits**: the count wait the sampler found, and the
  per-worker slot wait after it (`0x21b8940`), the same loop without `pause` (job_barrier.h).

## Staging

**Beta 18 (this design):**
- the roles module and every thread on it;
- the guest primitives (semaphores, read/write locks);
- the barrier fix;
- the counters;
- the `Roles:` line.

**Beta 19 (after 18's data):**
- the engine's own waits onto the parking primitive;
- if the small-CPU runs show the main thread waiting for cores: the parking barrier, and the main
  thread raised with the chain;
- pool sizes tuned from the `Roles:` lines;
- if a tester with such a CPU turns up, the last-level-cache criterion.

Temporary switches, this beta only, removed before the release candidate:
- `BB_THREAD_ROLES=0`: today's priorities and cores;
- `BB_RUNTIME_SYNC=0`: today's semaphores and read/write locks;
- `BB_TEST_CPUS=<n>`: the engine limits its own process to n logical processors at start. A
  measurement tool, not a case in the design: it checks that the rules scale, on the tester's PC.

## Measurement

The slower spot, a few minutes each, frame statistics and `BB_SAMPLE_THREADS=main` on. The baseline is
132's runs, already in hand.
1. Beta 18 with `BB_THREAD_ROLES=0`: the handoffs alone.
2. Beta 18 as built: handoffs and roles.
3. and 4. Both again with `BB_TEST_CPUS=8` (a 4-core, 8-thread CPU): roles off, then on.
5. The lamp spot, as built.

Compared:
- FPS and the 1% lows;
- the main thread's waiting and spinning shares;
- the frame-ending thread's wait for the engine;
- the `Roles:` lines.

A role setting stays only if it helps on its own, or at least helps the lows.

## Considered and not chosen

| Alternative | Why not |
|---|---|
| Rewriting the game's job dispatch, so small batches run on the main thread with no handoff | Removes the handoffs at their very root, but changes the game's behaviour; patching its scheduler is riskier than everything above put together |
| Workers spinning between batches, to stay awake for the next one | Handoffs come about 350 us apart: far longer than a spin should last (Microsoft: avoid long spins) |
| Roles inferred from each thread's behaviour (how much it runs, how often it waits) | Windows already does this (Thread Director, its scheduler's heuristics); names are exact for 1.09 |
| Per-CPU rules (the first draft) | Each new machine needs a new branch; the roles and one topology rule cover them all |

## Risks

- **Correctness of the primitives every game thread depends on.**
  - `tests/test_sema.c` (the PS4 contracts) and `runtime-test` run under Wine and natively.
  - New stress tests: many threads; needs above 1; timeouts, cancel and delete during waits; stale
    ids; readers and writers; static initialization racing; destroy while busy; every error answer.
- **Eco QoS on compiles.** A compile a frame is waiting on must not run at eco clocks, which is why
  promotion changes QoS with priority. The compile-wait counters show it.
- **Placement can regress for one workload** (Microsoft: floating sometimes wins). That is why roles
  have their own switch and line, and why Frame threads are not restricted. Restricting them to the
  fastest class is a one-line change of their role's preference, kept as a measured option.
- **The barrier fix** touches the game's code. It is checked against 1.09 byte for byte and left out
  when the code differs, like the other game fixes.

## Sources

- Microsoft, [Game Performance in Hybrid CPU Environments](https://learn.microsoft.com/en-us/gaming/gdk/docs/gdk-dev/pc-dev/overviews/game-performance-hybrid-cpu-environments?view=gdk-2610)
  (GDK, 2026):
  - CPU sets over affinity masks;
  - frame-critical threads on the performance cores when there are three or more;
  - job threads floating;
  - QoS through `ThreadPowerThrottling`;
  - avoid long spins;
  - C-state wake costs.
- Intel, [Game Dev Guide for 12th Gen Intel Core Processor Hybrid Architecture](https://www.intel.com/content/www/us/en/developer/articles/guide/12th-gen-intel-core-processor-gamedev-guide.html)
  (2022):
  - priority as a secondary placement signal;
  - the order of strength of placement controls;
  - no per-frame affinity changes;
  - `pause`'s cost.
- Microsoft, [WaitOnAddress](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress)
  and [SleepConditionVariableSRW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleepconditionvariablesrw).
- Microsoft, [Static configuration options for heterogeneous power scheduling](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/static-configuration-options-for-heterogeneous-power-scheduling).
- Preshing, [Semaphores are surprisingly versatile](https://preshing.com/20150316/semaphores-are-surprisingly-versatile).
- The [Lichii pull request](https://github.com/zortos293/Lichii/pull/232) on performance cores for
  time-critical threads (gain within noise).

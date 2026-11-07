# A1–A12: how the first batch of community fixes went in (0.4.0 beta 1, 2026-10-25)

Kept for the record. The fixes, with their sources and credits: [community-fixes.md](../community-fixes.md),
section A; each one's row: [changelog.md](../changelog.md).

## A1–A12: integration plan

Analysis done 2026-10-25, and all twelve went in the same day for 0.4.0 beta 1 (built by the user,
not here). Addresses in this section are image offsets unless marked "patch".

### As built (0.4.0 beta 1)

| # | Commit | Differences from the plan below |
|---|---|---|
| Shared | `ebb9026` | As planned: `Hook::original`, `Replace`, `skip_list.h`. |
| A3 | `c108cd9` | HW has three nested waits, not ayozetr's five: the other two are in code only that fork has. |
| A6 | `f9e74c9` | As planned. |
| A9 | `3569d42` | As planned. |
| A11 | `fe331e3` | As planned; the video and audio reads also return on an empty queue. |
| A12 | `30a7d10` | As planned. |
| A8 | `8329a5d` | Two of the three parts were already in HW (the fetch shader kept per stage, a fresh selection per warm-up record). What went in: a draw without a pixel shader gets its own fragment state, and the key version 7 with the NVIDIA rule. |
| A7 | `af66653` | As planned, with the disk entry removed through the IO queue (`DataBase::RemoveQueued`). |
| A2 | `de3503b` | As planned. |
| A5 | `afd6bda` | As planned; the module `game_fixes.cpp` with it. |
| A4 | `5f74f46` | As planned. |
| A10 | `c0852fd` | As planned. Test: `fmod-ring-test`. |
| A1 | `f0dc6cd` | The lock replaced whole, as planned. **ProcessOne is not replaced**: a hook at its entry puts the cache in by a compare-exchange on the list's allocator slot (no queue lock needed), and the cache keeps a lock of its own, so it does not rely on every caller holding the queue lock (all 14 `Append` callers do). ProcessOne, `Append` and `~CSEzWork` are checked whole all the same, and so are the allocator class's 27 methods (alloc and free where expected). 64 caches. Test: `job-pool-test` (not `game-fixes-test`). |
| Version | `28ab95a` | Launchers and LauncherBuilder 0.4.0-beta.1 (shown as v0.4.0 beta 1); engine id `hunters-workshop-5`. |
| A1, beta 2 | `ff81dc9` | First game run (2026-10-26): `job_lock` in, `job_nodes` left out. The start program's link step rewrites every `mov rax, fs:[0]` to `gs` (17,127 sites), and `Append` has two of them (+0x4c, +0xbd, both on its out-of-memory path), so its fingerprint failed. The rewritten bytes are accepted too (`0x0d88125c856cf28e`, checked again by hand). Lock, ProcessOne and `~CSEzWork` have no `fs` loads, and no red-zone site or red-zone function touches any of the four. 0.4.0-beta.2, engine id `hunters-workshop-6`. |
| A1, beta 3 | (this commit) | Beta 2's game run still left `job_nodes` out. On Windows the loader rewrites each `gs:[0]` read once more (`src/probe.c` `patch_tls_reads`): to the TEB slot's displacement, or, past slot 63 (the user's: 93), to a jump to a stub of its own whose address changes every run. So no fixed hash of Append can match. The fingerprint now reads each listed thread-pointer read as the game's `mov rax, fs:[0]` when it holds one of those forms, and checks every other byte as before (the original hash again). The log says which check failed. Checked on the real Append bytes in all four forms; `job-pool-test` covers the forms on synthetic code. 0.4.0-beta.3, engine id `hunters-workshop-7`. |

Fingerprints of game code are checked against the image the engine actually sees: the eboot
after the start program's patches **and** its link step (`fs`→`gs` loads, red-zone store
redirects, `start/prepare.cpp`) **and** the Windows loader's own rewrites (`src/probe.c`: thread-
pointer reads to the TEB slot), not the patch output alone. A1's first fingerprints missed both.

Planned tests not added: A4's and A5's callbacks, A6's case in `test_runtime.c`, and A7/A8's in the
pipeline tests. They need the full engine build (the runtime and the renderer), which can't be
made where this was written. Each change was checked by hand against the code it touches instead.
The ones added (`engine-hooks-test`, `job-pool-test`, `fmod-ring-test`, `test_patches.py`) pass on
Linux, and `engine-hooks-test` and `job-pool-test` also pass as Windows builds under Wine.

### What each one touches

| # | Layer | Files | Game sites | How it goes in |
|---|---|---|---|---|
| A1 | Game code | new `gpu/shim/engine/job_pool.cpp` | `DLAdaptiveMutex::Lock` 0x20368c0 (whole function, 0xe8 bytes) replaced; `CSEzWork::ProcessOne` 0x2032370 (0x1c2 bytes) hooked at its entry (as built; the plan replaced it); the general allocator's vtable 0x53aefa0 copied, never written | `Replace` (new, below) and `Prepare` |
| A2 | Boot patch | `start/patches.cpp`, `scripts/patches.py` | The recording producer's `lea rdi, [string]` at patch 0x1dc981f: its displacement at patch 0x1dc9822, `5d0ff702` → `2e6af702` | A write in the patch list, checked against the original bytes like the render pools |
| A3 | Renderer | `liverpool.cpp` 920, 1994, 2168 | — | `task.handle.destroy()` after each wait loop |
| A4 | Game code | new `gpu/shim/engine/game_fixes.cpp` | Runner step 0x1067f60 (17 bytes); the two runner resets 0x1067960 and 0x1067a40 (14 bytes each); give-back 0x1056990; pool 0x54b8788 | `Prepare`; the step through `Hook::original` (new, below) |
| A5 | Game code | `game_fixes.cpp` | 0xbbc3e0 (17 bytes) | `Prepare`, raising `edi` in the saved registers |
| A6 | Runtime | `src/runtime_memory.c:340` | — | One condition |
| A7 | Renderer | `vk_pipeline_common.h/.cpp`, `vk_graphics_pipeline.cpp` (~650 job, ~664 direct), `vk_compute_pipeline.cpp` (~142 job, ~158 direct), `vk_rasterizer.cpp` (2085, 2265, 2381, 2550), `vk_pipeline_cache.cpp/.h`, `vk_pipeline_serialization.cpp` | — | A failed state on the pipeline (below) |
| A8 | Renderer | `vk_pipeline_cache.cpp` (`BuildRuntimeInfo` ~330, `RefreshGraphicsStages` 886), `vk_pipeline_serialization.cpp` (31, 182, `LoadGraphicsPipeline` ~305) | — | Three loading/state changes and a key version step |
| A9 | Renderer | `tile_manager.cpp` ~343 | — | One `AccessBuffer` |
| A10 | Game code | new `gpu/shim/engine/fmod_ring.cpp` and `fmod_ring.h` | FMOD `MemPool` free 0xdab3a0 (14 bytes); FMOD's critical-section enter 0xd1acd0 and leave 0xd1acf0 on the pointer at 0x546d260; the four `SoundI` vtables 0x52d7640, 0x52d7840, 0x52d7a30, 0x52d7c30; the shared subsound's free returns to 0xdbe687 | `Prepare` |
| A11 | Runtime (AvPlayer) | `avplayer_common.h:72` | — | Lock before the size check |
| A12 | Renderer | `texture_cache.cpp` ~1223 | — | A format check in `FindDepthTarget` |

Six of the twelve (A3, A6, A9, A11, A12 and A2's single write) touch nothing the others touch. The
game-code four (A1, A4, A5, A10) share one installer. A7 and A8 share two renderer files.

### Shared pieces, added first

1. **`engine_hooks` gets two additions.** Existing hooks are unchanged.
   - `Hook::original`: the address of the displaced instructions plus their jump back, which
     `Prepare` already writes into the stub. A callback can then run the game's function itself
     (`Call(hook.original, ...)`) and return its result through the return slot. A4 needs this:
     HW's hooks only see a function's entry and argument registers, not its exits.
   - `Replace(hook, image, size, offset, expected, fn)`: the entry jumps straight to a host
     function with the game's calling convention (`sysv_abi`): no stub and no register save.
     `Commit` writes both kinds. A1 needs this. `DLAdaptiveMutex::Lock` runs for every heap, GX
     and job-queue lock, thousands of times a frame. There the stub's six pushes and eight xmm
     saves would cost more than the fix saves.
   - Tests in `tests/test_engine_hooks.cpp`: a callback running the original through
     `original`, guest-ABI code calling a replaced function, and wrong bytes refused.
2. **`gpu/shim/engine/skip_list.h`** (header only). The comma-list parser `frame_rate.cpp` uses for
   `BB_FRAME_FIXES_SKIP` moves here, so the game fixes' switch uses the same one. `frame_rate.cpp`
   keeps its behaviour and messages; `test_frame_rate.cpp:279` covers it.
3. **`gpu/shim/engine/game_fixes.cpp/.h`**: one `GameFixes::Install(image, size)`, called from
   `BbEngine::Install` (`change_appearance.cpp`) beside `RenderGuard` and `OnlineFixes`, and
   counted in its "no compatible feature" check.
   - It reads the switches, installs A4 and A5 itself, and calls `job_pool.cpp` (A1) and
     `fmod_ring.cpp` (A10).
   - `BbEngine::Install` runs whenever the executable is the verified 1.09 (`start/main.cpp`
     always sets the identity), so the fixes need no launcher setting.
   - New source files are picked up by `gpu/CMakeLists.txt`'s glob. Only the new test target is
     added there by hand.

### Overlaps and conflicts found, and how each is settled

**No two changes write the same bytes.** Every A-site was checked against:
- every line of all 122 patches in `Bloodborne.xml`, enabled or not;
- the built 60 FPS and uncapped images, which hold the start program's own writes and the
  frame-rate table;
- `frame_rate_sites.inc`, the engine's hooks, `BbGuestHooks`, `probe.c`, and the online
  module's code check (0x1c43ee0).

The relationships that remain:

- **A5 and Kyo's 60 FPS++ entry at patch 0xfbc40f.** That entry (in HW's frame-rate table, at
  every rate) turns the function's first `nBaseSamples >= 3` assert into a jump past it.
  - So in HW a count of 2 doesn't crash: it runs on. bbport's own changelog (2026-10-16) found
    that running on with 2 ends in a loop: the guest stops submitting, a black screen.
  - A5 makes the count valid before the check, so the jump is never needed. The entry stays: it is
    Kyo's list content and the table tests compare it with his list.
  - bbport's clamp raises every count to 3. But the fourth caller (patch 0x1034a4d) passes a mode
    from `[r14+0x21690]`, and mode 2 asserts the count is even and above 3, so 3 fails there.
  - HW's version raises the count to the smallest one each mode accepts: 4 in mode 2, 3 otherwise.
    It leaves anything at or above that alone.
- **A10 and Dasaav's "FMOD Crash Fix"** (off by default). Different bug, different code: channel
  setup at patch 0x1122534, and 0x11f3e4d/0x11f3eab. Both can be on.
- **A7 and A8 share `vk_pipeline_cache.cpp` and `vk_pipeline_serialization.cpp`.** They go in back
  to back, A8 first.
  - HW already has part of A8: since key version 6 it saves the generated clip-discard shader, and
    loading uses it.
  - Still missing:
    - A later shader stage without a fetch shader overwrites the vertex stage's when loading
      (line 182).
    - A failed load leaves its stages behind for the next.
    - A draw without a pixel shader decides clip-distance emulation from the previous draw's
      fragment state (line 886).
  - That last one is part of what NVIDIA saved to disk. So the key version goes 6 → 7, and on
    NVIDIA a version 6 entry without a pixel shader is left out once: it compiles again in play
    and is saved as 7. Every other version 5 and 6 entry keeps loading. This follows the rule HW
    already has for ambiguous version 5 entries. A left-out entry counts as rejected, not
    damaged, so the cache isn't cleared.
- **A7 against HW's background compiles.** Supermedo's change covers only creation on the draw's
  thread; HW also creates pipelines on compile threads.
  - A failed pipeline becomes a third state beside "compiling" and "ready": ready, but with no
    pipeline. Without it, the draw path would treat a failed pipeline as still compiling and bind
    a null handle.
  - The four draw and dispatch sites skip it.
  - The first draw that meets it removes its key from the disk cache (`PipelineCache::Forget`).
    The key was written when the pipeline was made, and by the time a draw sees the failure, that
    write has certainly happened.
  - The start's warm-up already rejects a refused entry and rebuilds the cache; unchanged.
- **A1 against HW's rule that addresses the game can see stay below 1 TiB.** The game packs
  thread handles into 40 bits (`runtime_thread.c`); HW's history has the deadlock that caused.
  - The node caches are objects the game calls through, so they go in `runtime_low_map` memory.
  - The lock compares owners the way the game's own lock does, which HW's low thread handles
    satisfy.
  - Since A1 replaces whole functions, its install checks each function's full bytes, not just
    the entry. A change inside either one would otherwise be dropped silently.
- **A4: bbport hooks the middle of functions** (the shared exit 0x1068092; the stores 0x106798a and
  0x1067a5c). HW's hooks go on function entries, so:
  - the runner step is wrapped: run the original, and on result 1 with `[runner+0x120]` still
    held, give the slot to the pool (0x1057350 returns 0x54b8788's value);
  - the two resets are hooked at entry. Both clear `[rdi+0x120]` in their first instructions, with
    rdi the runner, so this is the same point bbport hooks.
- **A2: bbhost changed the string all five producers share.** Four of them are online: two in
  `FrpgNetMan`, one in the session manager, one in the playlog.
  - The game already holds its own `algorithm=deflate;level=1` at patch 0x4d40254 (and two
    more). Pointing only the recording's `lea` at it changes 4 bytes. It leaves online payloads
    at level 9, and needs no new memory or string.
  - Level 1 is ordinary deflate that every reader inflates. It is only somewhat larger.
  - Whether the recording itself leaves the PC isn't established; the online check in the
    checklist covers it.
- **A1 and the hitch investigation.** A1 changes how long the main thread waits for the character
  workers. Get a hitch-report session before A1 and one after, so the effect is measured, not
  assumed.

### Removed or replaced

- The four `ASSERT_MSG`s on pipeline creation (A7) become the failure path.
- `BuildRuntimeInfo`'s inline clip-distance test becomes a helper, shared with the
  no-pixel-shader case (A8).
- `frame_rate.cpp`'s skip-list parser moves to `skip_list.h`.
- Nothing else is removed and no files are deleted. These all stay as they are: Kyo's 0xfbc40f
  entry, Dasaav's FMOD patch, the warm-up's rejection and rebuild, and bbhost's FMOD detector
  (`BBHOST_FMOD_CHECK`), of which only the guard is ported.

### Switches and logs

| Switch | Effect |
|---|---|
| `BB_GAME_FIXES=0` | A1, A2, A4, A5 and A10 all off |
| `BB_GAME_FIXES_SKIP=<names>` | Leaves out the named ones: `job_lock` and `job_nodes` (A1's two halves), `job_slots` (A4), `blur_samples` (A5), `fmod_ring` (A10), `deflate` (A2) |

- **Logs.** One install line names what is on, and the patch block reports A2. After that each fix
  logs its events: the first five, then at powers of two, as `render_guard.cpp` does. For
  example: a slot given back, a count raised, a sound taken out of the ring, a pipeline the
  driver refused.
- **No switch for A3, A6–A9, A11 and A12.** They correct code with nothing anyone would want back,
  like HW's other renderer fixes.

### Credits

`README.md`:
- **The bbhost row** gains the job-pool lock and node cache, the FMOD sound-ring guard and the
  recording's compression level (A1, A2, A10).
- **"Crash and controller fixes" becomes "Crash, renderer and controller fixes"**, with these
  added:
  - deadinside28 (bbport 0.6: render job slots, blur kernel, pipeline cache state: A4, A5, A8);
  - Supermedo (pipeline failures, A7);
  - DarkIzuku (tiler staging, A9);
  - bmy (bbport-mac, flexible memory, A6);
  - ayozetr and emurphy04 (AvPlayer queue, A11);
  - artemkaVG (stencil linking, from shadPS4#5118, A12);
  - the ports by ayozetr, 0xCydral and Mrsuss60.
- **A3 is upstream shadPS4**, already credited; its commit names `12c5978f`.

Each commit message names its source the way `68c04a1` did ("After ... (author)").

### Commit order

Each is built and tested before the next. Version 0.4.0 beta 1.

1. `engine_hooks`: `Hook::original` and `Replace`, with tests.
2. `skip_list.h`, with `frame_rate.cpp` moved onto it (no change in behaviour).
3. A3, indirect-buffer tasks.
4. A6, flexible-memory split.
5. A9, tiler staging read.
6. A11, AvPlayer queue.
7. A12, stencil linking.
8. A8, pipeline cache state, with key version 7.
9. A7, pipeline failures.
10. A2, the recording's compression: `patches.cpp`, `patches.py`, tests, and the baseline images
    updated by those 4 bytes.
11. `game_fixes.cpp` with A5, blur kernel samples.
12. A4, render job slots.
13. A10, FMOD sound ring.
14. A1, job lock and node cache.
15. This document: the items marked done.

The independent one-file fixes come first, each alone so any of them can be reverted or bisected.
A8 comes before A7 (same files). The game-code fixes come in rising risk, with A1 last: it's the
largest, and the one to measure.

### Tests

Local, no CI. Linux builds of the tests, plus the Windows cross-build under Wine where a test
touches Windows code.

| Item | Test |
|---|---|
| Shared | `engine-hooks-test` (the additions); `frame-rate-test` (the moved parser) |
| A1 | New `game-fixes-test`: our lock against a model of the game's unlock (0x2036a40's transitions) on one word: mutual exclusion and the hand-over to a sleeper, many threads. The node cache's alloc, free and pool end through a stand-in vtable. |
| A2 | `test_patches.py`: written only over the expected `lea`; the verified executable has it; `patches.cpp` and `patches.py` give the same bytes |
| A4 | A synthetic runner through `Hook::original`: result 1 with a slot held gives it back, result 0 doesn't |
| A5 | The callback: mode 0 and 1 → at least 3, mode 2 → at least 4, larger counts untouched |
| A6 | `test_runtime.c`: a flexible mapping split by a partial unmap frees its own pages |
| A7, A8 | `test_pipeline_cache_blob` / `test_pipeline_archive`: a later stage without a fetch shader keeps the first one's; on NVIDIA a version 6 entry without a pixel shader is left out, other version 6 entries and version 7 load |
| A10 | `fmod_ring.h`'s helpers on a synthetic ring: linked, unlink, the refusing shared subsound |
| A3, A9, A11, A12 | Build plus the existing tests |

In-game checklist, for when you test:
- Laurence's cinematic at 60 and 120 (A4).
- Fishing Hamlet to Rom's lake (A10).
- Central Yharnam's lamp with `BB_FRAME_STATS` and `BB_HITCH_REPORT_MS=40`, before and after A1.
- A long session, for the recording every ~50 s (A2).
- An online session with a summon (A2).
- Motion blur on at 60 and 120: the log says if a count was raised (A5).
- Depth-only and shadow passes on NVIDIA (A8).
- No code 23 on AMD (A7).
- A movie played and skipped (A11).

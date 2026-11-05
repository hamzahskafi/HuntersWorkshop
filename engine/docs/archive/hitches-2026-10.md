# The hitch investigation of October 2026 (0.4.0)

Kept for the record: the 40–120 ms hitches investigated on 2026-10-25 and 26 with hitch reports,
the PC memory model measured on and off, and the leads kept for a smoothness phase. What came of
it: the online summon's server lookup moved off the game's thread, and the PC memory model off by
default from 0.4.0 beta 4, then removed in 0.4.4 beta 28 ([gpu-memory.md](../gpu-memory.md)). The
smoothness work after 0.4.4: [performance.md](../performance.md) and [plan.md](../plan.md).

## Earlier investigation: the 40–120 ms hitches (2026-10-25)

Superseded by [performance.md](../performance.md) and [plan.md](../plan.md); kept for the record.

| Log | What it showed |
|---|---|
| `20261025_180526` (60 FPS, ~13 min) | Pacing near perfect (median 16.67 ms, stddev ~0.05 ms). Four long frames, 42–106 ms. In 3 of 4 the hitch came before the nearest save, not during it. |
| `20261025_202235` (`BB_SAMPLE_THREADS=main`) | 17 long frames during play (43–122 ms) plus one area load. The main thread is in the game's condition-variable wait (image offset 0x2083ecd, Dantelion `PthreadCondition.cpp`), so it waits on another thread. Nearly every one has a 9.4 MB texture upload (a 1920×1080 image, 0x900000 bytes) and 1,400–3,700 memory-protection calls (90–235 MB of pages) in the same frame. |

### Hitch reports, six sessions (2026-10-25 evening, ~95 min of play, 7 reports)

Sessions `205240`, `211729`, `214630`, `221249` and `234823` had `BB_HITCH_REPORT_MS=40`;
`20261026_002308` had 30 and included an online co-op summon.

**Fewer long frames in play than this afternoon:** 0.1–0.3 frames a minute over 1.5× the median,
mostly 25–65 ms. That compares with 0.6 (`180526`) and 0.9 (`202235`). Loading screens are
excluded from all of these counts. The areas played differ, so no change is credited for this.

**Each report costs about 200 ms itself**, so "the next frame came 240 ms after" is not the
hitch's length. Leave the switch off for normal play.

What the reports found:

- **The game's job workers were never the cause.** In every report the six `CSChrThread`
  workers and the `EzWorkPool` threads sat idle in their condition-variable wait. A1 stays a
  throughput fix, after bbhost's measurement, but it will not remove these stalls.
- **Online summon: a synchronous server request on the game's network thread.**
  - Both reports in `002308` end with The Hunter's Dream's
    `sceNpSignalingActivateConnection ... (resolved ...)` line.
  - `thd_np.cpp:117` calls `ServerSignalingResolve`, an HTTPS POST to the session server
    (8 s read timeout, 5 s connect), inside the call.
  - The game's network thread (`NexusRevolution`) makes that call while holding a game mutex
    (0xf3ecd3b0). The main thread needs that mutex (`DLLightMutex` 0x207ee90), so the game
    stops until the server answers. The report named the mutex's holder.
  - On PS4 this call returns at once and reports the connection later through the signaling
    callback.
  - HW's matching2 calls already run on a worker (`DispatchAfter`); only this one didn't.
    Inherited from Lantern 0.9.37 beta 7; the unreleased `online-shadps4-0.20` branch has it too.
  - **Fixed:** the lookup now runs on that worker, and the connection is announced when the
    address comes back (see [changelog.md](../changelog.md)).
- **In-play streaming frames** (100–350 MB of uploads in one frame while moving):
  - The main thread sleeps in the game's pacer (0x2034770): the frame is late on the GPU side.
  - The engine's breakdown for those frames, as thread time across the draw-preparation
    workers: 85–133 ms checking uploaded images for changes and 35–58 ms of Vulkan memory
    allocation.
  - The draw-recording threads are inside the NVIDIA driver: kernel calls, and one of the
    driver's locks with `_nvngx.dll` (DLSS) on the stack.
- **The 9.4 MB full-screen image** (1920×1080, 0x900000 bytes) re-uploaded after the game writes
  it from the CPU: 42–65 ms. Seen in play and around the summon.
- **Flip step waiting for the game's render thread.** The main thread is in the flip step
  (0x1d976e0, `SprjFlipper`), which hands work to `GXRenderThread` and waits for it (0x2198fd0).
  Named with the user's `bb-probe.exe` (debug line tables; `scratchpad` tool
  `probe/sym_reports.py`):
  - **The render thread** is in `sceGnmSubmitCommandBuffers` → `WaitPreviousFrame`
    (`gnmdriver.cpp:190`), waiting for the GPU side to retire the previous frame. In one report
    it is in `SubmitGfx` → `Enqueue`'s lock (`liverpool.cpp:2484`). On average that wait is
    0.02–0.14 ms a frame; frames still overlap (no "no longer overlap" message).
  - **The GPU side is late because of the NVIDIA driver:**
    - the presenter in `scheduler.Flush` (`vk_presenter.cpp:1381`, the frame's `vkQueueSubmit`)
      or making kernel calls;
    - the draw recorders waiting on a driver-internal critical section, or in kernel calls
      (`win32u`) of their own.
  - **With the memory model off, one report also has the recorder in `SynchronizeMemory` →
    `UpdatePageWatchersForRegion` → `VirtualProtect`** (`buffer_cache.cpp:3202`,
    `win32_memory.c:201`).
  - **Another report is game work on the main thread itself** (0x103d040 / 0x1067530, the
    task manager and render jobs), 40 ms of it.

### PC memory model (`BB_GUEST_IN_PLACE`), on and off (RTX 3070 Ti, i5-14600KF, 60 FPS)

On in `180526`, `202235`, `205240`, `221249`, `003848`; off in `211729`, `214630`, `234823`,
`002308`.

| | On | Off |
|---|---|---|
| FPS over the session | 60 throughout, no sinking | 60 throughout |
| Pages re-protected a second | ~6,000 | 105,000–129,000 |
| Draw recorder (stage B) busy | 12–16 % | 15–22 % |
| CPU cores in use | 2.8–3.2 | 3.0–3.4 |
| In-play 5-s windows with a frame over 33 ms | 10 in 35 min (0.29/min; `202235` left out, it ran the sampler) | 6 in 57 min (0.10/min), 2 of them the online summon |

- **The sinking from 2026-10-22 is gone.** The in-place share follows the area (0.1–551 MB a
  frame) and doesn't climb.
- **The long-frame difference points to off being smoother at 60 FPS on this PC.** But the counts
  are small and the routes differed, so it isn't settled.
- **Off costs CPU that is spare at 60.** Above 60 that changes, and on would matter more.
- **To settle it:** the same route offline (no summon), 10 minutes each way, hitch report off.
- **Settled for now (2026-10-26):** off by default since 0.4.0 beta 4; see `docs/gpu-memory.md`.

### GPU memory work: set aside, written up in `docs/gpu-memory.md` (2026-10-26)

Late frames in play come mostly from the PC memory model's moves: sparse rebinds that wait for the
GPU, and promotions into VRAM in bursts. A 0.4.0 beta 3 session with the model on had 11 frames
over 30 ms in ~20 minutes, each inside a burst of moves; the session before it, with the model off,
had none in ~8.5 minutes. So the model is **off by default since 0.4.0 beta 4**, and the rest is
skipped for now. `docs/gpu-memory.md` has the evidence, the causes with their code (the model's
moves, pinning, image memory created per image, residency chunks, the full-screen image), and a
step-by-step solution, in case it's picked up again.

## Smoothness leads (for the smoothness phase)

Folded into [plan.md](../plan.md), "Now: performance"; kept for the record.

- The hitch report's findings (above), first.
- A1 (job convoy) and A2 (compression), as frame-time work rather than bug fixes.
- eFMann's notes on stutters after loads (`documents/bb-dlc-judder-investigation.md`, `c57a2b81`):
  64 MiB VMA blocks instead of 256 (less VRAM held unused), 1 GiB of upload memory kept ready,
  write-backs of GPU-drawn images paced (128 MiB/s), the old area's textures freed before an area
  load, and new textures spread over 6 s after a load (64 MiB a frame). Its unsolved DLC judder had
  ~25 more barrier flushes a frame than normal frames. eFMann is upstream-shadPS4 based, so each is
  an idea to port, not a patch.

# Community fixes: the vetted list and plan

Updated 2026-10-25. Hunter's Workshop (HW) started from Lantern Engine 0.9.37 beta 9 (`08173e6`).
This is the analysis of what the other Bloodborne PC projects fixed after that point, checked
against HW's own code, with what has been done since. Nothing here is merged wholesale: each fix
is taken on its own, with its author credited.

Ground rules for this work:

- No merge with Lantern Engine. Its 0.9.37 stable regressed from beta 8 (the fixed-refresh frame
  lock and the particle cap, both off in HW) and it removed community patches and their credits.
- Community patches and their credits stay. A fix taken from another project is credited in
  `README.md` and in its commit.
- The particle cap (`BB_INDIRECT_CLAMP`) and the fixed-refresh frame lock (`BB_VBLANK_PACING`)
  stay off.
- The version stays 0.3.0 until the user says otherwise. Linux waits until Windows is done.

Addresses: **patch** addresses are the ones `Bloodborne.xml`, `frame_rate_sites.inc`, bbhost and
Binary Ninja use (image offset + 0x400000). **Image offsets** are what bbport's `ImageBase + x`,
HW's `Prepare()` calls and the sampler's `+0x...` print. Each address below says which it is.

## Done since the analysis

| What | Commit | Notes |
|---|---|---|
| Menus the engine holds open are checked before a reference is added (the Dream mirror's editor, the Altar's level-up) | `68c04a1` | `menu_step_is` in `engine_state.cpp`: the step must still hold a reference and carry the expected type (+0x28) and name (+0x30), as the step constructor (patch 0x201cce0) stores them. Fixes the Hunter's Dream "can't interact until Back" case. After Lantern 0.9.37 stable (Awaken). |
| Character creation by mouse, at the title and the Dream mirror | `68c04a1` | Drag on the hunter turns them (right stick), click a slider steps toward that side, drag across a row steps its value, the wheel steps by ten (L1/R1). Right-click stays Back. After Lantern 0.9.37 stable (Awaken). |
| Hitch report, `BB_HITCH_REPORT_MS=<ms>` | `b5105f1` | Off by default. For a frame over the threshold: every guest thread's wait, and every thread's stack twice 20 ms apart, the busy ones first. No thread is unwound while suspended. At most 12 per session, 3 s apart, none in the first 20 s or within 10 s of a loading screen. |

Checked and found already in HW (no change):

- **WASD acting as the d-pad after a menu or message.** HW's own fix (0.1.0 and `f9e343b`) matches
  the Lantern stable's code: `offers_choice`, the input-blocked early return, Escape as a closing
  button and the 250 ms settle.
- **Hover updating the item info.** HW's pointer already does what the game's list update does
  when the cursor moves (`SetCursor`, the listener at +0x30 slot 0x10, the cursor sound), through
  the vtable slot and all five direct callers of the list update (patch 0x1ecff30). No difference
  from the Lantern stable was found. If a menu is found where the info doesn't follow, look there.
- **Respawn quick re-entry (no 12-second minimum).** Already in HW (`src/probe.c`, AwesomeObserver
  `149972b`). The first version of this list wrongly said it was missing.

Investigated, not changed:

- **The game's file sync during saves.** HW turns `fsync` into a full disk flush on Windows
  (`src/win32_compat.h:45`: `_commit` → `FlushFileBuffers`). The logs show it isn't the hitch:
  the game saves every few seconds and almost never hitches, and during the hitches its main
  thread waits on another thread rather than writing. Left as it is.

## Current investigation: the 40–120 ms hitches

| Log | What it showed |
|---|---|
| `20261025_180526` (60 FPS, ~13 min) | Pacing near perfect (median 16.67 ms, stddev ~0.05 ms). Four long frames, 42–106 ms. In 3 of 4 the hitch came before the nearest save, not during it. |
| `20261025_202235` (`BB_SAMPLE_THREADS=main`) | 17 long frames during play (43–122 ms) plus one area load. The main thread is in the game's condition-variable wait (image offset 0x2083ecd, Dantelion `PthreadCondition.cpp`), so it waits on another thread. Nearly every one has a 9.4 MB texture upload (a 1920×1080 image, 0x900000 bytes) and 1,400–3,700 memory-protection calls (90–235 MB of pages) in the same frame. |

Next step: a session with `BB_HITCH_REPORT_MS=40` under Extra variables, to name the thread the
main thread waits for and what it is doing. The sampler's 5-second totals are too coarse for this.

Side note: Windows' commit headroom read 0.75–1.4 GB in that log. The page file is
Windows-managed, so Windows grows it when needed; nothing to change.

## A. Bugs HW has, with sound fixes (recommended)

| # | Fix | What it fixes | Where | Source and credit | Risk |
|---|---|---|---|---|---|
| A1 | **Job-pool lock convoy** | The game's job lock spins with `lock cmpxchg` + `rdtsc` and no `pause`, and every job pop frees its list node through the general allocator inside the queue lock. The six character workers spend ~60% of their time waiting; the main loop waits 1.4–3.5 ms a frame at Central Yharnam. Fix: the same lock transitions with a plain-load `pause` spin, and a per-pool cache of list nodes (a stand-in allocator in the list's allocator slot). | `DLAdaptiveMutex::Lock` patch 0x24368c0; `CSEzWork::ProcessOne` patch 0x2432370; allocator vtable patch 0x57aefa0 | droogie, bbhost v0.2.19: `src/decomp/sync/adaptive_mutex.cpp`, `ez_work.cpp` (`58af1c8`, `f764eff`) | Medium: replaces two game functions |
| A2 | **Compression level 9 → 1** | The game compresses a ~300 KB player recording on the main thread every ~50 s at zlib level 9: ~23 ms in one frame (bbhost's measurement). Not visible at 60 FPS in the logs so far; matters more at 120. Surgical form: give only the recording's producer its own `level=1` string. | String at patch 0x4d3a783 (`algorithm=deflate;level=9`; bbhost writes its `=9` at 0x4d3a79a), shared by 5 producers; the recording's is patch 0x1dc97a0 (its `lea` at 0x1dc981f) | droogie, bbhost `patches/deflate-level.toml`; via ayozetr `ebb79d3` | Low |
| A3 | **Indirect-buffer task leak** | Each nested command buffer's coroutine stays suspended when done and is never destroyed: ~600 bytes leaked per indirect buffer, every frame, for the whole session. | `liverpool.cpp`: `ProcessCeUpdate` (~920), the IB case of `ProcessGraphics` (~1994), `ProcessCompute` (~2168): `task.handle.destroy()` once done | upstream shadPS4 `12c5978f`; ayozetr `3f71604` (via Mrsuss60); DarkIzuku `195ab0b` | Trivial |
| A4 | **Render job slot leak** | Laurence's cinematic hangs above 30 FPS: the render job runner's two error exits return with the pool slot still held in `[runner + 0x120]`; after six failures every thread needing a slot sleeps for ever. Fix: give the slot back on those exits, and when the runner is reset with one held. | Image offsets: runner step 0x1067f60, error exit 0x1068092, resets 0x106798a and 0x1067a5c, give back 0x1056990 | deadinside28, bbport 0.6 (`8185b88`, `bbport_guest_hooks.cpp` `JobSlotFix`); ayozetr `087f051` (install after relocation: `7e396e9`) | Low |
| A5 | **Blur kernel clamp** | Crash `0xa0020007`, or a black screen (the guest stops submitting) when game data gives the blur kernel a base sample count of 2. Raise it to 3 at the function's entry. | Image offset 0xbbc3e0 (`GPUTexUtil_GetRecursiveSampleParameters`) | deadinside28, bbport 0.6 (`InstallKernelSampleClamp`) | Low |
| A6 | Flexible-memory split | Splitting a flexible mapping keeps the left piece's offset for the right one; unmapping it frees and zeroes the wrong pages and leaks its own. | `src/runtime_memory.c:340`: `if (right.kind==KIND_DIRECT)` → `!= KIND_RESERVED` | bmy (bbport-mac `9ffc78d`), via ayozetr `efdf7b1` | Trivial |
| A7 | Pipeline creation failure | A pipeline the driver refuses ("ErrorUnknown", some AMD and older drivers) ends the game with code 23. Skip its draws and dispatches instead, and don't save it to the cache. | `vk_graphics_pipeline.cpp` ~651/668, `vk_compute_pipeline.cpp` ~142/158 (`ASSERT_MSG`), `vk_pipeline_cache.cpp` | Supermedo `188c1f6`; ayozetr `49a37b4` | Low |
| A8 | Pipeline cache stale state (NVIDIA) | A draw without a pixel shader takes the previous draw's fragment runtime info for NVIDIA's clip-distance emulation; a geometry-shader pipeline loaded from disk loses its fetch shader. | `vk_pipeline_cache.cpp` ~886 (`bind_stage(Fragment)` with no reset); `vk_pipeline_serialization.cpp` ~182 (`fetch_shader_data` overwritten) | deadinside28, bbport 0.6-pre1, via ayozetr `5ad6bb0` | Low |
| A9 | Tiler staging read hazard | Texture write-back's tiling dispatch reads a staging range that isn't recorded as read, so a later writer can reuse it first. | `tile_manager.cpp` `TileImage`: `AccessBuffer(staging, eComputeShader, eShaderRead)` after the dispatch | DarkIzuku `8e0842c` | Trivial |
| A10 | Rom's lake crash (FMOD) | The game's FMOD Ex frees a shared subsound that is still linked in the master sound group's ring; the next link beside it writes through freed memory. Seen arriving at Rom's lake from the Fishing Hamlet. Fix: at FMOD's pool free, unlink a sound still linked, and keep the shared subsound that refused its release. | Patch addresses: FMOD `MemPool` free 0x11ab3a0, `SoundI::release` 0x11be120, the shared subsound's free 0x11be682 | droogie, bbhost `src/engine/fmod_ring.cpp` (`f1d4fa3`) | Medium |
| A11 | AvPlayer queue race | `Pop()` checks the size outside the lock; a `Clear()` from another thread in between reads an empty queue. | `avplayer_common.h:72` | ayozetr `3d2b8b3` | Trivial |
| A12 | Stencil linking | `FindDepthTarget` links whatever image sits at the stencil address; a colour target there gets reads and writes through an invalid view. Possible NVIDIA GPU hang (Xid 109). | `texture_cache.cpp` ~1225 | shadPS4#5118, via ayozetr `da5561d` | Low |

Suggested first batch: A1–A12, B13 and B15.

## B. Above 30 FPS (0.3.0 territory)

HW already carries 161 of bbhost's 166 frame-rate sites (every redirected load, the physics, AI,
clip-drive and targeting steps, the pacer targets). These are what bbhost found beyond them.

| # | Fix | What it fixes | Where | Source and credit |
|---|---|---|---|---|
| B13 | **Grass and tree wind** | At 30 the game steps the wind 1/60 s a frame (half its pace, with the GX device's +0x100 at 1). HW points that step at the frame's time, so at 60+ the wind runs twice the PS4's pace, and its force, weighted by the same step, bends less and twitches with each frame's length. Fix: the wind's step at half the task step; the force at the authored 1/60. | `sub_2713870`; HW `FRAME_SITE(Grass, 0x2713d8d)`; bbhost's force sites patch 0x27149ea and 0x2714b26 | droogie, bbhost `wind_pace.h`, `frame_rate.cpp` (`b2818b4`, `5cee912`) |
| B14 | **Effects step** | Effects count part of their lives in updates. With the frame's time as their step, lamp and candle flicker runs at the frame rate rather than 30 a second, and some effects never end (Ebrietas' 1251721 in 1 of 6 attacks without the gate). Fix: whole 1/30 s steps with the rest carried, and the frames between holding what effects count. Would also let HW drop the Ebrietas data workaround (Nexus mod 41's trail). Largest item here. | `SprjSfx::Update` patch 0x2377bd0, its step load 0x2377d4a | droogie, bbhost `sfx_schedule.h`, `decomp/sfx/step_gate.cpp` (`6f735f4`, `9c13c5a`, `5986a4b`, `e4fbb44`) |
| B15 | Bloodletter L2 | The transformed L2's self-hit (a 0.4 m sphere live for one 30 FPS frame) misses the hand's ragdoll capsule when it trails the pose by a fraction of a frame, so the frenzy aura never comes above 30. Fix: radius 0.6 at the converted rates. | `AtkParam_Pc` 2900931 `hit0_Radius` 0.4 → 0.6 | droogie, bbhost `6f735f4` |
| B16 | Keyboard sprint turns | Sprint drops when changing direction on the keyboard. Fix: the keyboard stick turns at most ~30° per read, a half turn passes through forward, and a release keeps the direction 60 ms. | `src/runtime_pad.c` | Supermedo `4b930b1`; ayozetr `523c88e` (cassioKenji's measurements); Lantern PR #67 (ikeeasianboy) |
| B17 | DS4 diagonals | Some pads report diagonals past the stick's round gate (up to ~1.41× one axis), so the camera turns 2–3× faster there, worse at high FPS. Fix: bring a stick past the round gate back onto it. | `src/runtime_pad.c` | ayozetr `2b8d9e1` (issue #7) |
| B18 | PS4 float state on game threads | Game threads start with Windows' MXCSR (0x1f80) instead of the PS4's (0x9fc0: flush-to-zero, denormals-are-zero) and x87 control word 0x037f. Needs testing before it ships. | Guest thread start (`src/runtime_thread.c`) | StevenMiller123 and red-prig, shadPS4#3568; via ayozetr `caa9b4b` |

## C. Test first, or optional

- **Laurence's cinematic at 120 FPS.** bbport found the render resource cache (the same allocator
  as HW's GX range allocator: 0x2169c30 → 0x26d0d30 → the range allocator and its collector
  0x26aa860, image offsets) out of room at high frame rates: 20–40 KiB allocations fail, models go
  missing, 320 MiB wasn't enough, 512 MiB of port memory was. HW sets 288 MiB (beta 8, for
  Ebrietas), up to 416 with `BB_GX_REGION_MB`. Test the cinematic at 120 before changing anything.
- **"Bad PM4" at save load.** The game's main and render threads write one command buffer at once
  while an effect initialises (image offset 0xebb960; its three clears at 0xebbc86, 0xebbca4,
  0xebbcc2). bbport does those clears itself and copies command buffers at submit by default. HW
  has the copy (`BB_COPY_GPU_BUFFERS`) off by default, and a zero-dword skip. Source: deadinside28,
  bbport 0.6 (`bbport_fx_init_clear.h`).
- **Third resource post-processor in play** (experimental): patch 0x18281a8 stores 2 instead of 0,
  so the resource system's third loader thread keeps running in play. droogie, bbhost
  `patches/ingame-post-processors.toml`.
- **Uncapped only:** a flip queue that never empties can keep the precise-pacing loop from
  returning for vblank events (`driver.cpp` ~1296, the `wait_until` predicate is true past its
  deadline). DarkIzuku `58ee8f5`.
- **Bugs in community patches, found when Lantern remade them.** HW keeps the authors' patches and
  credits and would fix the bytes:
  - *Restore Debug Camera* (Lance McDonald) writes its code over the game function at patch
    0x1de3330, which 0x1d62765 and 0x1dbe4f2 call.
  - *Enemy Control* (stagvant) drops the controller's null check.
  - *Enable Screen Space Reflections* puts its 2.0 at patch 0x4632f8, inside a clock function
    (0x4632d0) that nothing calls: harmless, but writes into code.
  - *Performance Patch* never applies in HW: about 40 of its writes come from another game version
    and land mid-instruction, so HW's checks refuse it.
- **Lens and camera tuning** (bbhost's "Original" camera, the game's own camera converted from 30
  instead of Kyo's retune): a feature, not a fix.

## D. Not recommended, not applicable, or already in HW

Not recommended:

- ayozetr's indirect-draw guard (`25a8ca7`): a compute pass before each indirect draw, splitting
  render passes. The same family as the particle cap.
- Supermedo's translation of instructions old CPUs lack (`a87b3b4`, Ivy Bridge and older): large,
  and not this hardware.

Not applicable to HW:

- bbport 0.6's fixes to its own new renderer and memory layer: push constants under the occlusion
  pass, the present stage wait, light-shaft tiling, the 4K frame buffer over the bus, FPS aging,
  the write-trap race, the hot-effects SSAO and SSR bugs.
- Supermedo's fixes for regressions from its shadPS4 0.20.1 update (bindings past 40, index buffer
  size, cache versions, the access memo). HW never took that update.
- VRAM fixes built on bbport 0.51's memory model (ayozetr `ddce44b`, `dac2621`, `5a88481`,
  `af2cc9f`, `7bacb0c`): HW's model was redesigned. Revisit only on VRAM-full reports.
- Supermedo's mouse-camera memory queries (`ee2bec4`): HW's mouse camera makes none.
- ayozetr's pad neutral measurement (`746d623`): HW doesn't measure one.
- Polaris end-of-pipe labels (`bb49ffe`): RADV only, Linux later.
- bbhost's renderer items (Forbidden Woods tessellation hang, the black world, its VRAM leak).

Already in HW: thread handles below 40 bits, guest in/out arguments outside the region lock,
write-fault texture skip, the drain race, stereo level, audio priority, the limiter guard and
pacing drift (`PrecisePacing::Cadence`), early Z, 1 MiB render pools, PM4 type-0 skip and resync,
the Intel 12th-gen red zone, occlusion counts in order, the SRT walker's fault handling, stale
vertex inputs, indexed indirect index buffers, the out-of-order flip IRQ, the Windows commit limit
(`BB_POOL_LAZY`), and the online certificate trust and kept-alive retry.

## Smoothness leads (for the smoothness phase)

- The hitch report's findings (above), first.
- A1 (job convoy) and A2 (compression), as frame-time work rather than bug fixes.
- eFMann's notes on stutters after loads (`documents/bb-dlc-judder-investigation.md`, `c57a2b81`):
  64 MiB VMA blocks instead of 256 (less VRAM held unused), 1 GiB of upload memory kept ready,
  write-backs of GPU-drawn images paced (128 MiB/s), the old area's textures freed before an area
  load, and new textures spread over 6 s after a load (64 MiB a frame). Its unsolved DLC judder had
  ~25 more barrier flushes a frame than normal frames. eFMann is upstream-shadPS4 based, so each is
  an idea to port, not a patch.

## Beta 9 audit

Apart from the two switches HW keeps off (the fixed-refresh frame lock, the particle cap), nothing
Beta 9 changed alters Windows defaults in a way that could regress:

- Fixes: the loading screen's pace restored after travel, Ebrietas' trail effect (from the
  player's own files), the startup commit-limit fix (`bb-start` makes the pool section).
- Off unless enabled: chunk buffers (`BB_CHUNK_BUFFERS`), the lazy pool (`BB_POOL_LAZY`).
- Linux only: engine features turned on there.
- Launcher and catalogs: per-vendor shader catalogs, frame generation with HDR, the sidebar,
  updates, the app icon.

## Sources examined

| Project | Up to | Notes |
|---|---|---|
| Lantern Engine (Awaken) | stable `d901fbd`, online branch `918505b`, PRs #66–#78 | Input fixes ported; the online fixes are already in HW |
| bbport (deadinside28) | 0.6, `8185b88` | Its own renderer since 0.6; game-level fixes portable |
| ayozetr (bbport Linux fork) | `23ce633` | Same shadPS4 layout as HW: direct ports |
| Supermedo (bbport Windows fork) | `5adea35` | Its later fixes are mostly for its shadPS4 0.20.1 update |
| bbhost (droogie) | v0.2.19, `56b868e` | Independent engine; its game-level fixes and frame-rate work are portable |
| DarkIzuku | `2f9f1c0` | Lantern's own starting point |
| eFMann (shadPS4 DLSS/FSR fork) | `36e2116a` | Upstream-shadPS4 based; ideas rather than patches |

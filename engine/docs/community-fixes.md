# Community fixes: the vetted list and the reviews

What the other Bloodborne PC projects fixed after the point Hunter's Workshop (HW) started from
(Lantern Engine 0.9.37 beta 9, `08173e6`), checked against HW's own code, and the later reviews of
bbport, Lantern Engine and bbhost. Nothing here is merged wholesale: each fix is taken on its own,
with its author credited in `README.md` and in its commit (the ground rules: [plan.md](plan.md)).
What became of each item is in [changelog.md](changelog.md). How the first batch (A1–A12) went
into 0.4.0 beta 1: [archive/a1-a12-integration-0.4.0.md](archive/a1-a12-integration-0.4.0.md).

Addresses: **patch** addresses are the ones `Bloodborne.xml`, `frame_rate_sites.inc`, bbhost and
Binary Ninja use (image offset + 0x400000). **Image offsets** are what bbport's `ImageBase + x`,
HW's `Prepare()` calls and the sampler's `+0x...` print. Each address says which it is.

## A. Bugs HW has, with sound fixes (recommended)

| # | Fix | What it fixes | Where | Source and credit | Risk |
|---|---|---|---|---|---|
| A1 | **Job-pool lock convoy** | The game's job lock spins with `lock cmpxchg` + `rdtsc` and no `pause`, and every job pop frees its list node through the general allocator inside the queue lock. The six character workers spend ~60% of their time waiting; the main loop waits 1.4–3.5 ms a frame at Central Yharnam. Fix: the same lock transitions with a plain-load `pause` spin, and a per-pool cache of list nodes (a stand-in allocator in the list's allocator slot). | `DLAdaptiveMutex::Lock` patch 0x24368c0; `CSEzWork::ProcessOne` patch 0x2432370; allocator vtable patch 0x57aefa0 | droogie, bbhost v0.2.19: `src/decomp/sync/adaptive_mutex.cpp`, `ez_work.cpp` (`58af1c8`, `f764eff`) | Medium: replaces two game functions |
| A2 | **Compression level 9 → 1** | The game compresses a ~300 KB player recording on the main thread every ~50 s at zlib level 9: ~23 ms in one frame (bbhost's measurement). Not visible at 60 FPS in the logs so far; matters more at 120. Surgical form: give only the recording's producer its own `level=1` string. | String at patch 0x4d3a783 (`algorithm=deflate;level=9`; bbhost writes its `=9` at 0x4d3a79a), shared by 5 producers; the recording's is patch 0x1dc97a0 (its `lea` at 0x1dc981f) | droogie, bbhost `patches/deflate-level.toml`; via ayozetr `ebb79d3` | Low |
| A3 | **Indirect-buffer task leak** | Each nested command buffer's coroutine stays suspended when done and is never destroyed: ~600 bytes leaked per indirect buffer, every frame, for the whole session. | `liverpool.cpp`: `ProcessCeUpdate` (~920), the IB case of `ProcessGraphics` (~1994), `ProcessCompute` (~2168): `task.handle.destroy()` once done | upstream shadPS4 `12c5978f`; ayozetr `3f71604` (via Mrsuss60); DarkIzuku `195ab0b` | Trivial |
| A4 | **Render job slot leak** | Laurence's cinematic hangs above 30 FPS: the render job runner's two error exits return with the pool slot still held in `[runner + 0x120]`; after six failures every thread needing a slot sleeps for ever. Fix: give the slot back on those exits, and when the runner is reset with one held. | Image offsets: runner step 0x1067f60, error exit 0x1068092, resets 0x106798a and 0x1067a5c, give back 0x1056990 | deadinside28, bbport 0.6 (`8185b88`, `bbport_guest_hooks.cpp` `JobSlotFix`); ayozetr `087f051` (install after relocation: `7e396e9`) | Low |
| A5 | **Blur kernel clamp** | Crash `0xa0020007`, or a black screen (the guest stops submitting) when game data gives the blur kernel a base sample count of 2. Raise it to 3 at the function's entry. | Image offset 0xbbc3e0 (`GPUTexUtil_GetRecursiveSampleParameters`) | deadinside28, bbport 0.6 (`InstallKernelSampleClamp`) | Low. Matters more for HW than first thought: see the integration plan |
| A6 | Flexible-memory split | Splitting a flexible mapping keeps the left piece's offset for the right one; unmapping it frees and zeroes the wrong pages and leaks its own. | `src/runtime_memory.c:340`: `if (right.kind==KIND_DIRECT)` → `!= KIND_RESERVED` | bmy (bbport-mac `9ffc78d`), ported by 0xCydral (`efdf7b1` in ayozetr's fork) | Trivial |
| A7 | Pipeline creation failure | A pipeline the driver refuses ("ErrorUnknown", some AMD and older drivers) ends the game with code 23. Skip its draws and dispatches instead, and don't save it to the cache. | `vk_graphics_pipeline.cpp` ~651/668, `vk_compute_pipeline.cpp` ~142/158 (`ASSERT_MSG`), `vk_pipeline_cache.cpp` | Supermedo `188c1f6`; ayozetr `49a37b4` | Low |
| A8 | Pipeline cache stale state (NVIDIA) | A draw without a pixel shader takes the previous draw's fragment runtime info for NVIDIA's clip-distance emulation; a geometry-shader pipeline loaded from disk loses its fetch shader. | `vk_pipeline_cache.cpp` ~886 (`bind_stage(Fragment)` with no reset); `vk_pipeline_serialization.cpp` ~182 (`fetch_shader_data` overwritten) | deadinside28, bbport 0.6-pre1, via ayozetr `5ad6bb0` | Low |
| A9 | Tiler staging read hazard | Texture write-back's tiling dispatch reads a staging range that isn't recorded as read, so a later writer can reuse it first. | `tile_manager.cpp` `TileImage`: `AccessBuffer(staging, eComputeShader, eShaderRead)` after the dispatch | DarkIzuku `8e0842c` | Trivial |
| A10 | Rom's lake crash (FMOD) | The game's FMOD Ex frees a shared subsound that is still linked in the master sound group's ring; the next link beside it writes through freed memory. Seen arriving at Rom's lake from the Fishing Hamlet. Fix: at FMOD's pool free, unlink a sound still linked, and keep the shared subsound that refused its release. | Patch addresses: FMOD `MemPool` free 0x11ab3a0, `SoundI::release` 0x11be120, the shared subsound's free 0x11be682 | droogie, bbhost `src/engine/fmod_ring.cpp` (`f1d4fa3`) | Medium |
| A11 | AvPlayer queue race | `Pop()` checks the size outside the lock; a `Clear()` from another thread in between reads an empty queue. | `avplayer_common.h:72` | ayozetr with emurphy04, `3d2b8b3` | Trivial |
| A12 | Stencil linking | `FindDepthTarget` links whatever image sits at the stencil address; a colour target there gets reads and writes through an invalid view. Possible NVIDIA GPU hang (Xid 109). | `texture_cache.cpp` ~1225 | shadPS4#5118, ported by artemkaVG (`da5561d` in ayozetr's fork) | Low |

Suggested first batch: A1–A12, B13 and B15.

## B. Above 30 FPS (0.3.0 territory)

HW already carries 161 of bbhost's 166 frame-rate sites (every redirected load, the physics, AI,
clip-drive and targeting steps, the pacer targets). These are what bbhost found beyond them.
B13, B15, B16 and B17 went in for 0.4.0 beta 5, and B18 for 0.4.1 beta 1 with a switch for its A/B test ([changelog.md](changelog.md)).

**B14 parked (2026-10-26).** bbhost's effects work is its `decomp/sfx/step_gate.cpp` (1,344 lines) and
the schedule in `engine/frame_rate.cpp` (`kSfxStepLoad` 0x2377d4a, `g_sfx_carry`). Effects move by time
(right at any rate) and count part of their lives in updates (the light module's flicker phase +0x30
and blend +0x110, a node's state edges, emissions, conditions, controllers), which run twice as often
at 60. The options weighed:

- **A, whole steps:** the effects in whole 1/30 s, the frames between running only each node's
  containers (sub_2c4ff30 and the light module sub_131e660 held). Counts right, Ebrietas' 1251721 ends
  without the data workaround, but the effects' own movement then changes 30 times a second. Medium.
- **B, bbhost's smooth steps:** counts at 30 and movement by the frame's time, through about 30
  effect classes converted one by one, the ribbons, GPU particle records and the wind. Several times A.
- **C, the flicker alone:** the light module's update (sub_131e660) with its phase rate (+0x80) as 0
  and its blend mode (+0x38, +0x3c) as keep on the frames between whole 1/30 s, all put back, a
  pre-roll's call left alone: lamps and candles flicker 30 steps a second, as on the PS4; nothing else
  changes. Small. The recommended one, if this is picked up.
- **D, nothing.** Chosen for now: the Ebrietas data fix covers the effect that never ended.

**C tried in 0.4.2 beta 1 and taken back out (2026-10-29):** side by side its flicker could not be told from the game's own at 60, so it did not earn a hook on a game function. D stands, and B14 is closed.

| # | Fix | What it fixes | Where | Source and credit |
|---|---|---|---|---|
| B13 | **Grass and tree wind** | At 30 the game steps the wind 1/60 s a frame (half its pace, with the GX device's +0x100 at 1). HW points that step at the frame's time, so at 60+ the wind runs twice the PS4's pace, and its force, weighted by the same step, bends less and twitches with each frame's length. Fix: the wind's step at half the task step; the force at the authored 1/60. | `sub_2713870`; HW `FRAME_SITE(Grass, 0x2713d8d)`; bbhost's force sites patch 0x27149ea and 0x2714b26 | droogie, bbhost `wind_pace.h`, `frame_rate.cpp` (`b2818b4`, `5cee912`) |
| B14 | **Effects step** | Effects count part of their lives in updates. With the frame's time as their step, lamp and candle flicker runs at the frame rate rather than 30 a second, and some effects never end (Ebrietas' 1251721 in 1 of 6 attacks without the gate). Fix: whole 1/30 s steps with the rest carried, and the frames between holding what effects count. Would also let HW drop the Ebrietas data workaround (Nexus mod 41's trail). Largest item here. | `SprjSfx::Update` patch 0x2377bd0, its step load 0x2377d4a | droogie, bbhost `sfx_schedule.h`, `decomp/sfx/step_gate.cpp` (`6f735f4`, `9c13c5a`, `5986a4b`, `e4fbb44`) |
| B15 | Bloodletter L2 | The transformed L2's self-hit (a 0.4 m sphere live for one 30 FPS frame) misses the hand's ragdoll capsule when it trails the pose by a fraction of a frame, so the frenzy aura never comes above 30. Fix: radius 0.6 at the converted rates. | `AtkParam_Pc` 2900931 `hit0_Radius` 0.4 → 0.6 | droogie, bbhost `6f735f4` |
| B16 | Keyboard sprint turns | Sprint drops when changing direction on the keyboard. Fix: the keyboard stick turns at most ~30° per read, a half turn passes through forward, and a release keeps the direction 60 ms. | `gpu/shim/input/bindings.cpp`, `src/runtime_pad.c` | Supermedo `4b930b1`; ayozetr `523c88e` (cassioKenji's measurements); Lantern PR #67 (ikeeasianboy) |
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
  instead of Kyo's retune): in HW from Lantern 0.9.37, but **incomplete** (found 2026-10-31): Original
  converts only the two eases, not the camera's other per-frame weights; fixed in 0.4.4 beta 24
  (`camera_rate.cpp`: every per-frame weight converted each frame with the frame timer). The launcher's
  Graphics > Follow camera, `camera_tuning=original` (the default) or `fps++` (`start/patches.cpp`,
  `FrameRateFixes`); the lens filter's rate is the frame-rate fixes' Lens group.

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

## bbport 0.7, reviewed (2026-10-29)

deadinside28/bloodborne_pc 0.7 (`0fc2921`, one commit over 0.6, 962 files): its notes' every fix checked against
HW's code and the 1.09 eboot. Taken for 0.4.3 beta 1: the two rows above. Already in HW: play time above 30 (HW's
own clock carries the milliseconds since 0.3), the per-draw `jthread::joinable` (HW's bind helper has bbport's
flag), the occlusion intervals (HW's real counts read results without waits, and are an Experimental option), the
NVIDIA power profile (HW's Reflex boost), FIFO Relaxed (the launcher's), the GX node guard of 0.6's #162 (HW's 1 MiB
render pools), the blur kernel and the job slots (0.4.0). Optional, not taken: the reactive mask's size guard (the
mask is off by default), the HUD icon grid (bbport's UI path; HW's differs; only with a scaled scene), the invalid
character id before a save (its cause unknown to bbport too), the compute copy into a new image (measure first).
Not for HW: bbport's memory model (its heap growth, the vertex explosions at heap pieces, the shrink race, the FPS
drop after a save switch from its layer's marks), its Windows build's own fixes (BC6H storage, the opening movie),
Wayland and the Steam Deck, the ring dedup and the copy shader filter (its buffer code). FSR 3.1 out of protected
memory (RADV handhelds): with the Linux build. Features noted for later: SMAA and FXAA, FSR 1, GT-VBAO, Anti-Lag 2,
the shader recipe pack (to be converted to HW's cache inputs), video skipping, invert mouse X.

## Lantern 0.9.38, reviewed (2026-10-29)

Taken for 0.4.1 beta 2: [changelog.md](changelog.md). Already in HW: the native frame rate (HW's own table since 0.3.0;
every site Lantern's `NATIVE_FRAME_RATE.md` lists is in `frame_rate_sites.inc`, the 90 list's effect step
and the uncapped list's step in the start's patches), and the cutscene freeze fix (`job_slots.cpp`: HW's
A4 hooks the same runner, its destroy and its reset). Not taken for now: FSR 4 from the AMD driver
(RX 9000, Lantern still testing it; port if AMD players ask), the memory retry limit (the PC memory model
only, off by default in HW), the game status and Discord presence, the Extra Messenger Skins patch
(distpade; optional, cosmetic), muting while the window is in the background, the PC menus off switch, the
cheats' online message, the Wayland scale hint (Linux later), the Russian PC menus (GHusr-y5; HW's menu
builder has changed too much to copy) and the Help & FAQ redesign.

## Lantern 0.9.39 and bbhost v0.2.20–v0.2.22, reviewed (2026-10-31)

From their release notes and changes (Lantern 0.9.39 betas 1–26, 29–31 October; bbhost v0.2.20 to
v0.2.22, droogie and Kyoskii), each item checked against HW's code first. bbport has nothing after 0.7.
Where each went is in the plan to 0.4.4 stable, above.

**Lantern 0.9.39.** Mostly seamless co-op (a party kept across bosses, deaths and lamps, on Lantern's own
server: a feature tied to that server) and a rewrite toward its own engine (its own system libraries, a
new renderer and shader compiler, on by default in its beta 24, with no measured gain): neither for HW.

| Item | In HW? | Where it went |
|---|---|---|
| DLSS audit: the history reset on a camera cut | HW resets only on setting changes | Beta 25, credited to NVIDIA's guide §3.13 |
| DLSS audit: the bias mask only for preset F | HW sends it with every preset | Beta 25: not sent at all (§3.15); F itself removed (310.4.0) |
| DLSS audit: the "less wobble while turning" jitter fade off by default | Never in HW (Lantern added it after the fork) | — |
| Left open by the audit: DLSS gets the tone-mapped image at outputs other than 1080p | The same in HW (the startup patch) | The §3.1 decision after the proper DLSS test; the live path at 1080p is right |
| Left open by the audit: some passes not jittered (a likely slight wobble while panning) | To check in HW | After 0.4.4 |
| Left open by the audit: an NVIDIA sample application ID | HW uses the shared 231313132 | Kept: no Project ID (Decisions; the tester's call) |
| Cached pipelines with a motion-vector vertex shader and no motion in the key not built (Lantern's beta 26) | HW checks only the opposite pairing | Beta 25, credited to Lantern |
| Camera timing (Lantern's beta 25): nine of the follow camera's rates scaled by the frame's length | Checked against HW: two of the nine are wrong (the pitch return's kept share treated as a rate; the focus is time-based already), and with FPS++ they would convert a second time | Not taken. HW's own conversion of every weight in beta 24 |
| FSR through AMD's FSR SDK 2.4 | — | Beta 26, from AMD's SDK and credited to it |
| Anti-Lag 2, the 8 GB VRAM tweaks, optional lighting effects (heavy; ghosting listed as a known issue), a lamp-to-lamp patch, bbhost-format patches | — | Features, after 0.4.4 (Anti-Lag 2, lighting, lamp to lamp) or not taken |
| The save repair (for seamless co-op); the PC memory model off on old AMD drivers | Not applicable (HW removes the model) | — |

**bbhost v0.2.20–v0.2.22.** Mostly its own renderer, a new launcher, Quake and Lo-Hit plugins, fixed
rates up to 960, and Uncapped capped to the display (the opposite of HW's choice in beta 22).

| Item | In HW? | Where it went |
|---|---|---|
| Leaving a room called the server on the game's thread: a freeze at every session end (droogie) | Yes: `end_session_locked` calls `ServerLeaveRoom` synchronously with `g.mu` held | Beta 25, credited to droogie |
| A connection that died before its ESTABLISHED/ACTIVE notices announced as live again (droogie) | Yes: `sig_fire` does not check `Conn.dead` | Beta 25, credited to droogie |
| OK in message boxes ignored for the first 0.5 s (Kyoskii) | The game's behaviour | Optional, after 0.4.4 (a feel change) |
| The community SSR patch's tuning writes slower than the game's values, 7.8 ms against 6.1 (Kyoskii) | HW ships another version with its own constants; SSR is off by default | Measured after 0.4.4 |
| Korean menu text cut short (Kyoskii) | Not affected: HW finds the end of UTF-16 text two bytes at a time | — |
| Its decompiled follow camera (`src/decomp/camera/follow_camera.cpp`), every weight converted | HW's Original converts two | A cross-check for beta 24; no code taken |

**AMD and NVIDIA documents.** AMD FSR SDK 2.4 (FidelityFX-SDK `v2.4.0`, `7539ae8`): the FSR versions per
GPU and the licence terms in beta 26's table; Vulkan games ship the DLLs (no driver override), and games
on 4.1.2 or newer can take later driver upgrades; frame generation on Vulkan is FSR 3.1.6 through AMD's
swapchain (4.0.2 needs RX 9000 and Windows 11). NVIDIA DLSS Programming Guide 310.6.0 (31 March 2026,
NVIDIA/DLSS `9981c63`): §3.1 before tone mapping; §3.5 mip bias log2(render/display) − 1 (HW already);
§3.13 reset on a camera cut; §3.15 BiasCurrentColor "should not be used"; presets A–E removed and F
deprecated (310.4.0), K the default for DLAA/Quality/Balanced, M Performance, L Ultra Performance (HW's
Automatic already); §3.21 DLSS through Streamline; §5.1.1 the loader library; §5.2.1 the Project ID.

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
| Lantern Engine (Awaken), later | 0.9.38 (2026-10-29); 0.9.39 betas 1–26 (2026-10-31) | Reviewed in their own sections |
| bbport (deadinside28), later | 0.7, `0fc2921` | Reviewed 2026-10-29 |
| bbhost (droogie, Kyoskii), later | v0.2.22 | Reviewed 2026-10-31 |
| AMD FSR SDK | v2.4.0, `7539ae8` | The FSR plan (beta 27) |
| NVIDIA DLSS | Programming Guide 310.6.0 (NVIDIA/DLSS `9981c63`); Streamline 2.14.16 | The DLSS plan (beta 25; first built as beta 23) |

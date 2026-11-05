# The plan to 0.4.4 stable (2026-10-31 to 11-03; done)

Kept for the record: how 0.4.4 was planned and built beta by beta (betas 23 to 28), the decisions
as they stood then, and the questions settled on the way. 0.4.4 was released on 2026-11-03 from
beta 28 (`cbec5a2`). The decisions that still stand: [plan.md](../plan.md), "Decisions"; every
change with its measurements: [changelog.md](../changelog.md); the reviews behind this plan:
[community-fixes.md](../community-fixes.md).

## Plan to 0.4.4 stable (decided 2026-10-31; done)

Beta 22 is the last build kept. **Beta 23 failed** (the tester's call, 2026-11-01): built and tested,
then withdrawn (its section below). Beta 24 (the camera) is built on beta 22 (`claude/0.4.4-beta-24` from
`claude/0.4.4-beta-22`); beta 23's work is redone as beta 25 on top of it, the launcher's reorganisation is beta 26 (the tester's call,
2026-11-01), AMD's FSR SDK beta 27 and the PC memory model's removal beta 28. Each beta gets its own branch, `claude/0.4.4-beta-N`. The
reviews behind this plan are in [community-fixes.md](../community-fixes.md), "Lantern 0.9.39 and bbhost v0.2.20–v0.2.22, reviewed".

### Decisions

| Topic | Decision |
|---|---|
| FSR | AMD FSR SDK 2.4 (FidelityFX-SDK `v2.4.0`, 28 October 2026) on Windows: AMD's signed `amd_fidelityfx_loader_vk.dll` and `amd_fidelityfx_upscaler_vk.dll` with AMD's licence notice. Every unofficial FSR path is removed, not kept (beta 27). |
| DLSS | The driver's NGX core, as up to beta 22, started with the shared application id 231313132 (`Init_Ext2`). **The shared id stays; no Project ID** (the tester's call, 2026-11-01): it brings HW nothing in practice, and the start code is not touched for it. The DLSS Programming Guide (310.6.0 §5.2.1) asks for one, but the core's entry point that takes it (`NVSDK_NGX_VULKAN_Init_ProjectID_Ext`, behind NVIDIA's `NVSDK_NGX_VULKAN_Init_with_ProjectID`) is declared nowhere public: beta 23 learned its arguments by disassembling NVIDIA's loader libraries, which the NVIDIA RTX SDKs licence (§4a) forbids. **Not Streamline** (§3.21), first chosen and built in beta 23: with its core loaded, NVIDIA's Vulkan driver presents through DXGI and holds V-Sync On (latest frame) to two frames a refresh (beta 23's section). NVIDIA's NGX loader library (`nvsdk_ngx_s.lib`/`_d.lib`, §5.1.1) is not linked: the DLSS SDK licence (§2e) forbids use that puts the SDK under an open-source licence (HW is GPL), and the library is built for MSVC while HW's Windows build is MSYS2 CLANG64. |
| NVIDIA Reflex | Not offered (beta 23's ran through Streamline; beta 26 took out its launcher row, and the in-game item shows only with Streamline loaded for a test). It returns with a design of its own on `VK_NV_low_latency2` directly (the sleep before the frame's work, markers with present IDs), not built on frame generation's code, and measured against the 120 FPS cap first; which beta is open. |
| NVIDIA guide items | In beta 25 (its table; first built in beta 23). No DLSS cost table until a proper test (after 0.4.4). |
| Credits | AMD and NVIDIA items credit AMD's SDK and NVIDIA's guide (ground rules). |
| FSR frame generation | Set aside. The current one (FireBurn's) goes with FireBurn in beta 27; AMD's official one needs AMD's swapchain (its docs call dispatching without it "highly discouraged") and comes after 0.4.4. |
| Frame generation | Not offered (the tester's call in beta 23): from beta 26 `frame_gen` reads as off (`BbSettings::FrameGenOffered`), its menu items and launcher rows left out. **Removed in beta 28** with Streamline and Reflex's code (the tester's call, 2026-11-02): its return, if any, is AMD's on AMD's swapchain and Reflex on `VK_NV_low_latency2`, neither built on this code. |
| OptiScaler | Set aside: the compatibility mode (`vk_optiscaler.h`) stays as it is, untested. The tester uses neither OptiScaler nor frame generation. Re-check after 0.4.4. |
| Original camera | Fixed in HW's engine (beta 24): every per-frame weight of the follow camera, converted each frame with the frame timer. bbhost's weight list is a cross-check only; no code is taken from it or from Lantern. FPS++ becomes that base plus only what Kyo changed (beta 24). |
| When AMD's or NVIDIA's runtime can't start | TAA, with a log line and a note in the game (DLSS falls back to FSR 3.1 until beta 27 removes it). |
| Linux | Set aside until Windows is done; notes kept in [plan.md](../plan.md), "Linux, for later". |
| Test PC | RTX 3070 Ti; Windows HDR is off. HDR-output items are checked in the code, not in the tester's runs. DLSS's input point (§3.1, before tone mapping) concerns the game's internal image, so the runs do cover it. |
| Frame lock / Match display | Pinned (ground rules). |
| PC memory model | Removed in beta 28, the cleanup (the tester's call, 2026-11-02). |
| Live-path proxy trimming (upscale-2/3) | After 0.4.4 (recommended). |

### Beta 23: failed (its record: `claude/0.4.4-beta-23`)

Built and tested 2026-10-31 to 11-01 (engine ids 44 to 52), then withdrawn on the tester's call; its work is
redone as beta 25. Its plan, its rows in [changelog.md](../changelog.md) and its code are on its branch; the
diagnostic build is `claude/0.4.4-beta-23a1` (beta 23a1, engine ids 46 to 50, not kept). What it found:

- **The 120 FPS cap.** With NVIDIA Streamline's core loaded (every release tried, 2.10.3 to 2.14.16),
  NVIDIA's Vulkan driver makes a Direct3D device of its own when the swapchain is created and presents
  through DXGI, whose flip queue holds V-Sync On (latest frame) to two presents a refresh: the 240 preset
  ran 120 on the tester's 60 Hz screen (100 at 50 Hz), each frame waiting 8.2 ms in
  `vkAcquireNextImageKHR`. Not NVIDIA Reflex, DLSS or NGX, PC Latency, the swapchain's image count,
  Streamline's present hook, the engine's submit lock, Reflex's frame limiter or DLSS-G's pacer; a
  Direct3D device opened without Streamline leaves 240, and the driver's "Prefer native" present method,
  globally or per game, changes nothing. The tester posted the findings on NVIDIA's tracker
  (https://github.com/NVIDIA-RTX/Streamline/issues/112). The fix: Streamline not loaded, DLSS through the
  driver's NGX core again (240 at the title screen and the lamp).
- **HW's Project ID** (`e3c54136-7aa1-4a31-9b86-b9f1d103bfc9`) on the NGX core works: NGX started with it
  and DLAA ran (engine id 52). The entry point was read from NVIDIA's loader libraries in the DLSS SDK
  310.9.16 (disassembled, not linked), which the SDK's licence (§4a) forbids: not carried into beta 25
  (Decisions).
- **NVIDIA Reflex through Streamline** went with it. A review found its sleep for frame N+1 ran before
  frame N was queued for presentation; it was built on DLSS-G's frame plumbing. Reflex gets a design of
  its own (Decisions).
- **Frame generation** not offered (the tester's call; it may be deprecated before release).
- **The lamp, about 1.3% slower** (the tester felt beta 22 was faster). DLAA, 240 preset, GPU-bound, both
  orders: a view of about 1,345 draws equal (GPU 7.34 against 7.31 ms); at the lamp (about 1,785 draws)
  8.18 and 8.19 ms against beta 22's 7.90, 8.08 and 8.11; DLSS's own cost equal. Beta 23's windows at the
  lamp had about 3 more render passes begun again after the copy shader and about 2 more images moved to
  shader read a frame, which none of its code explains (it records the scene as beta 22 does), so the
  game's own work in those windows is the likely cause; not settled. Beta 24 against beta 25 at the same
  save settles it (beta 25's test, settled in [changelog.md](../changelog.md): beta 25 runs as beta 24; the gap was run to run).

### Beta 24: the follow camera's weights, per frame, in the engine

**Built (2026-11-01):** the survey found 13 places in the update to convert, 14 after the audit (more than the
table below: the pull-in by the character's sideways move, the wall turn's build-up and cut-off, and lock-on's
play angle are per frame too), and one of the table's weights that must not be converted (+0x1a4, a share of
each frame's move); what each is, and how it is converted, is in the rows of [changelog.md](../changelog.md) and
`camera_rate.cpp`.

**Finding (2026-10-31).** The launcher ("Original: As the game has it, at any frame rate"), the 0.4.0 notes
("the game's own camera at every frame rate"), the comment in `start/patches.cpp` ("the camera of the game
at 30 at every rate") and this doc (section C: "already in HW, from Lantern 0.9.37") say Original is
complete. It is not: Original converts 2 of the follow camera's per-frame weights (the eases at patch
0x4D25E68 and 0x4D25E94, per frame with the frame timer, `frame_rate.cpp` `kEases`). The others stay the
game's 30 FPS values every frame, so at 120 FPS they act four times per PS4 frame: the camera chases,
swings and settles faster than on the PS4. FPS++ converts its weights, but once, at the preset's rate
(`FrameRateFixes`): right only while the game holds that rate (Uncapped converts as at 120,
`kUncappedReference`; at the 144 preset in busy areas the game runs about 121, so the camera is about 16%
slow).

`ChrExFollowCam` (update at patch 0x183ac60), field offsets (from HW's camera tables and bbhost's
decompilation; each use confirmed with HW's own tools in beta 24, which corrected +0x1a4: below):

| Weight | What it does | Original today | FPS++ today |
|---|---|---|---|
| Distance ease (0x4D25E68, 0.1), chase-rate ease (0x4D25E94, 1/30) | The camera's distance; the chase rate moving between normal and wall escape | Per frame (timer) | Per frame (timer) |
| +0x198, easing to +0x19c/+0x1a0 | Following the character | Not converted | Once, at the preset rate |
| +0x1a4 (the game combines it with the follow weight +0x154 first) | The share of the character's chased move this frame the point the camera aims for takes at once (not an ease: the shares of every frame's move add up to the same share of the whole move at any rate, so it needs no conversion; beta 24) | Not converted (right) | Once (wrong: about half Kyo's share at 120) |
| +0x1a8, a kept share (`pow(v, frames)`) | Tilting back after the stick is let go | Not converted | Once |
| +0x1ac/+0x1b8, easing to +0x1b0/+0x1b4 | The chase sideways and vertically | Not converted | Once |
| +0x1c4 | The chase's distance | Not converted | Once |
| +0x1c8/+0x1cc | The chase under lock-on | Not converted | Once |
| +0x23c | The pitch on a fast turn | Not converted | Once |
| +0x240 | Lock-on's and the auto turn's swing | Not converted | Once |
| +0x330 (0.05, the constructor's immediate at patch 0x183A7DE) | Eases the distance, the pitch floor, lock-on's framing, the look-at height and the field of view toward the camera's current LockCamParam row (the camera settings the game selects for where the player is, e.g. a boss arena) | Not converted | Once, from 60 (0.05 is the game's own 30 FPS value; Kyo's retune does not change it) |

+0x130 (the pitch return's weight) and the focus are time-based and need nothing. The game's debug menu
names every camera setting (about 60, registered by patch 0x183fb60): the checklist for the survey. Three
more may be per-frame and are checked too: +0x288 (ロックカメラ調整回転追尾率, the lock-on camera's turn
chase), +0x2b4 (EscapeWallRotDampRatio, the wall turn's damping) and +0x190 (CamSafeMarginRate).

**Kyo's retune, value by value (checked 2026-10-31 against the 1.09 executable).** The constructor (patch
0x183a0e0) writes the defaults twice: a first set (from patch 0x183a4c3), then, after reading the
`Game.IsUseChrFollowCam` setting into +0x342, a second one (from patch 0x183a8c1) that always overwrites it.
The second set is the game's camera. Kyo's list writes both, and stores the same values again in the camera's
debug-menu setup (patch 0x183fb60: each call to patch 0x2dad100 registers one field's slider, with its name,
range, step and display scale), jumping over the rest of it. **Correction:** this doc and `patches.cpp` said
those stores stop the game's per-area camera parameters from loading. They replace debug-menu sliders; no
per-area setting is lost.

| Field (the game's debug-menu name) | Game, 30 FPS | Kyo, 60 FPS | What it is |
|---|---|---|---|
| +0x19c ChrTransChaseRateForNormal | 0.10 | 0.05 | Halved: his 60 FPS conversion |
| +0x1b0 TargetChaseRateXZForNormal | 0.10 | 0.05 | Halved |
| +0x1b8 TargetChaseRateY | 0.03 | 0.015 | Halved |
| +0x1c4 TargetChaseDistRate | 0.40 | 0.20 | Halved |
| +0x1c8/+0x1cc TargetChaseRateAtLockXZ/Y | 0.40/0.30 | 0.20/0.15 | Halved |
| +0x240 LockRotChaseRate | 0.30 | 0.15 | Halved |
| +0x1a0 ChrTransChaseRateForEscapeWall, +0x1b4 TargetChaseRateXZForEscapeWall | 0.25 | 0.05 | **Retune:** no faster chase near walls (as the normal rates) |
| +0x1a4 TargetTransChaseRate | 0.30 | 0.05 | **Retune:** the aimed-for point chases about 3x slower than a conversion (the lag) |
| +0x1a8 TargetRotXChaseRate | 0.30 | 0.05 | **Retune:** a kept share (patch 0x183cef0: `ref + wrap(pitch - ref) * w`), so lower tilts back faster, near at once |
| +0x238 DirectPitchAng | 60° | 40° | **Retune** (an angle) |
| +0x23c DirectPitchRate | 0.06 | 0.04 | **Retune:** a little quicker than a conversion |
| Distance ease (patch 0x4D25E68) | 0.10 | 0.025 | **Retune:** half a conversion (the pull-back while sprinting) |
| Chase-rate ease (patch 0x4D25E94) | 1/30 | 1/30 | Not changed: runs twice the game's speed at 60 (no visible effect in FPS++: its normal and escape rates are equal) |
| +0x330, the settings blend | 0.05 | 0.05 | Not changed: twice the game's speed at 60 |
| The jump at patch 0x183dc96 | to 0x183e2f2 | to 0x183e271 | **Code:** the chase-rate update also runs on the game's other camera path |
| The byte at patch 0x183dcfe | 1 | 0 | **Code:** no wall-escape hold when neither wall probe hits |

No distance setting (CamDist, FulcrumDist...) is changed: the camera sits further back because it chases
and eases its distance more slowly.

**The approach (agreed in principle 2026-10-31; confirmed by the tester 2026-11-01).** One base for both
tunings: the game's values, converted every frame from 30 by the engine with the frame timer (Original).
FPS++ is the base plus Kyo's seven retunes and his two code changes, his values stored as their exact 30 FPS
equivalents (a weight w at 60 is 1 - (1 - w)^2 at 30: 0.05 → 0.0975, 0.04 → 0.0784, the distance ease
0.025 → 0.049375; the kept share 0.05 → 0.0025; +0x1a4's 0.05 as it is, being a share of the frame's move,
which beta 24 found), so the per-frame conversion gives his 60 FPS camera exactly at every rate. His halved values go (the exact conversion replaces them: at 60 up to 14.5% quicker on
the larger ones, which the audit measured); the chase-rate ease and +0x330, which he did not change, are the base's, from 30. His
debug-menu stores are no longer needed: the game's slider setup stays as in Original today.

| Work | Size |
|---|---|
| Every use of these weights in the game's follow-camera update found with HW's own reverse-engineering tools (`tools/re`, as for the frame-rate survey) on the 1.09 executable, each use checked byte for byte as `kEases` are | M |
| Each use converted where the game uses it (the combined +0x1a4 value after the game combines it), with the frame timer's actual frame length, from 30 for both tunings (FPS++ as above). At 30 FPS every value stays the game's to the bit | L |
| FPS++: Kyo's seven retunes as 30 FPS equivalents in the second default set, his two code changes kept, his halved values and debug-menu stores dropped | M |
| `FrameRateFixes`' one-time camera conversions dropped while the timer is on (no double conversion); with `BB_FRAME_TIMER=0` today's behaviour stays as the fallback | S |
| Tests: the game's bits at 30, the maths (weights and kept shares), the checks at each use | M |
| The claims above corrected: the launcher text, this doc, the comments in `patches.cpp` (also the per-area parameters), the frame-rate survey; the 0.4.4 notes say Original was incomplete before | S |

**Asked again at the start of beta 24 and answered** (the tester, 2026-11-01):

1. The approach above: **yes, as described.** So +0x330 under FPS++ runs at the game's speed (90% of a
   change of camera settings in 1.5 s, as on the PS4) instead of today's 0.75 s, since Kyo did not change it.
2. +0x1a8 (TargetRotXChaseRate) under FPS++: **Kyo's value** (FPS++ tilts back as now, near at once; he
   chose to change it), stored as its 30 FPS equivalent like his other retunes.

Test, at 30, 60, 120, 144 and Uncapped, Original and FPS++: lock-on's swing, the tilt back after letting go
of the stick, the chase while sprinting, entering a boss arena: each should match 30 FPS.

### Beta 25: beta 23 redone: fixes, NVIDIA's guide (`claude/0.4.4-beta-25`)

Beta 23's work, on top of beta 24, in two steps (the tester's call, 2026-11-01): **25a**, the fixes,
**built** (2026-11-01; its row in [changelog.md](../changelog.md)), then **25b**, NVIDIA's guide's four
engine items, **built and confirmed** (2026-11-01; its rows there), then **25c**, the texts and translations (to decide).
Each is written afresh from the code as it is, not taken from beta 23's branch.

| Item | Source / credit | Size |
|---|---|---|
| **Fixes (25a, built)** | | |
| Leaving a room and destroying the online context send their server request from the online thread (`THD:np`), not the game's thread with `g.mu` held: no freeze of up to 8 s (NpPost's timeout) at session end. `m2_leave_room`/`m2_destroy_context` → `end_session_locked` → `ServerLeaveRoom` today | bbhost (droogie) | M |
| A connection that dies in the 200–400 ms before the ESTABLISHED/ACTIVE notices (`announce_locked`) is not announced as live: `sig_fire` checks `Conn.dead` | bbhost (droogie) | S |
| Cached graphics pipelines: one whose vertex shader writes motion vectors while its key has none is not built at start (an invalid layout; a crash risk on stricter drivers). `LoadGraphicsPipeline` checks only the opposite pairing | Lantern 0.9.39 beta 26 | S |
| Quit to Desktop, the window's close button and Apply and restart game leave the co-op room before the process ends (at most 2 s, beside the cache save and the GPU drain); the server kept the player until the heartbeat was 15 s old | HW (the tester's call) | S |
| The tests the first review of beta 23 found failing: the GPU library's runtime stubs for every test, Windows' libraries for the renderer tests, `_putenv_s` on Windows, stale expectations in `upscaler-support-test` and `widescreen-test` | — | S |
| **NVIDIA's guide (25b, built)** | | |
| History reset on a camera cut, for every upscaler (DLSS InReset; FSR's reset): more than the vertical field of view turned or more than 15 m moved in one frame (3 m in 25b's first build: it caught the follow camera's snaps); `BB_CAMERA_CUT_RESET=0` off. A cautious threshold: §3.13 warns that misused resets flicker | NVIDIA §3.13 | M |
| DLSS: the bias mask (BiasCurrentColor) no longer sent, and the reactive mask not made while DLSS runs. FSR keeps its reactive mask (AMD's advice) | NVIDIA §3.15, 310.4.0 changelog | S |
| DLSS models: "CNN (preset E)" and "(preset F)" removed from the launcher and the in-game menu (deprecated); a saved `e`/`f` becomes Automatic. Automatic already matches NVIDIA's defaults (K for DLAA/Quality/Balanced, M Performance, L Ultra Performance) | NVIDIA 310.4.0 | S |
| DLSS at least 16 jitter phases (§2.4's minimum; §3.7's 8 x ratio² above it, so only DLAA moves, 8 → 16; the tester's call) | NVIDIA §2.4, §3.7 | S |
| **DLSS** | | |
| ~~HW's own Project ID through the NGX core's `NVSDK_NGX_VULKAN_Init_ProjectID_Ext`~~ **Dropped**: DLSS keeps the shared id 231313132 (Decisions; the tester's call, 2026-11-01) | NVIDIA §5.2.1 | — |
| Streamline loaded only for DLSS-G (not offered) or a test (`BB_STREAMLINE=1`); the start log says why. The launcher no longer asks for Streamline's files for DLSS | — | S |
| **Frame generation and Reflex** | | |
| Frame generation not offered (Decisions); NVIDIA Reflex not offered until its own design (Decisions). **Built in beta 26** (left as they were until 25b was checked) | — | S |
| **Text and docs** | | |
| Live resolution change explained (launcher and in-game): at any output other than 1920x1080 the upscaler takes the game's tone-mapped frame whatever Live resolution says; only 1920x1080 gets it before post-processing (§3.1 asks for before tone mapping; moving it is not planned: the tester's call, 2026-11-01) | NVIDIA §3.1 | S |
| The HDR mode hint (right, checked in beta 23); "not with HDR output" out of the frame-generation texts | — | S |
| Translations: 13 launcher languages, 10 in-game | — | M |
| Docs: `docs/upscaler.md`, release notes (beta 23's entry folded in), this plan; version 0.4.4-beta.25, engine id | — | S |

Not carried: DLSS through Streamline as the main path, the removal of HW's own NGX loading, Reflex
through Streamline, the FSR 3 dispatch order (FireBurn goes in beta 27). To decide when beta 25 starts
(open questions): Streamline's DLSS test path and beta 22's boost hint.

25a's status (the tester, 2026-11-01): built and run normally; the co-op test is dropped (the tester does not play co-op). Frame generation (off) and Reflex (not working) are left as they are until 25b is checked.

Test of 25a (dropped): co-op, Quit to Desktop in a session: the other player sees the hunter leave at once (the
log's `np: left room ... at exit in ... ms`); leaving a room and quitting to the title do not freeze (with a slow or unreachable
server, the log's `np: leave_room for room ... failed after ... ms`, the game going on meanwhile); summons
and invasions still connect; the first start after updating builds a few pipelines again (records left
out), once. The builder runs the renderer tests on Windows (`scene-resolution-test`, `taa-shader-test`,
`camera-motion-test`, `upscaler-support-test`, `widescreen-test`, `bb-shader-lab`).

Test of 25b (none of it needs HDR; frame statistics on): a cutscene with cuts (a boss's entrance): no
ghost of the last shot on the next one's first frames, and the log's `Upscaler: camera cut (turned ...,
moved ...)` at the cuts only; normal play (sprinting, dodging, a fast mouse turn, lock-on switches, walls
behind the hunter, a fall, water with its reflection pass: the sewers, Gascoigne's puddles, Rom's lake;
the lowest frame rate tried too): no camera-cut line, and `Upscaler: history
resets in 600 frames: 0 at camera cuts ...` with the largest turn and move in one frame well under the
field of view and 15 m. DLAA: fine detail (hair, foliage, railings) as before or steadier. The DLSS model
list in the launcher and in game without E and F. The reactive mask greyed out under DLSS in game.
Performance as beta 25a, over a couple of runs each (single runs differ by a percent or two).

### Beta 26: the launcher reorganised (`claude/0.4.4-beta-26`)

The tester's call (2026-11-01): the proposal (`docs/launcher-layout-proposal.md`) with its four
recommendations: the sidebar's headers HUNT, VISUALS (was SIGHT), WORKSHOP, ENGINE; the launcher seeds the
engine's defaults; the in-game menu's labels matched, its tabs as they are. Built as proposed, with the
differences that document lists. Frame generation and Reflex not offered (beta 25's row) built here too.
The WPF launcher compiles on Linux (`dotnet build -p:EnableWindowsTargeting=true`), as do the Avalonia
launcher and the localization checks (which need Windows to run); settings-test passes with g++ and clang.

Test: both layouts (Settings › Layout), and Modern with the sidebar too (Settings › Navigation): every page
and group as the document lists, the greyed rows following their switch (FPS counter, HDR, Picture tuning,
Camera tuning, trophy notifications and sound, Sharpening), the DLSS group with DLSS only; the search finds
a row on each page (texture filtering, extra variables, log level); Window mode's three choices start the
game as the two switches did; Engine switches › Reset all with a feature and a sound setting changed;
an existing config.ini and settings.json load with nothing changed; the in-game menu: no frame generation,
"FPS counter", "HDR source", "Camera tuning" with its note; a config.ini with `frame_gen=dlss` starts
without it and the log says so.

### Beta 26b: Graphics and Look, the folds, the menus in step (`claude/0.4.4-beta-26`)

The tester's calls on beta 26 (2026-11-01): Upscaling and Picture rethought (VISUALS: Display, Graphics,
Look, Interface; `docs/launcher-layout-proposal.md`, "As built (beta 26b)"); Picture tuning, HDR, the FPS
counter and Camera tuning as switches whose settings fold under them; HDR checked on the game's monitor;
save backups while playing every 10 minutes (was 15); and why the game's menus and the launcher
disagreed (Live resolution Off yet the resolution changed in game; Picture tuning off in the launcher yet
on in game). Three causes, each fixed where it starts:

- **A 1920x1080 start ignored Live resolution Off.** With no startup patch at 1080p (`ScaledSizes`
  returns none), `BB_RENDER_RES` is unset, `FixedRenderSession()` false, and an output change took effect
  at once whatever the setting. `BbSettings::OutputChangesLive()` (live only with the setting On) and
  `ResolutionNeedsRestart()` now cover that session; the upscaler keeps the start's output until then, the
  Insert menu's hint and the System menu's help say so. settings-test: a 1080p start with Live 0 needs a
  restart for an output change and not for a preset change; with Live 1 the output is live.
- **The System menu's effect toggles turned Picture Tuning on.** `push_toggles` set `picture` for every
  graphics and effect toggle; only the vignette is a picture setting (the others are effects of their own,
  applied whether Picture Tuning is on or not). The ranges were narrower than the launcher's (fog and
  bloom up to the game's own only, contrast ±5%, hue ±20°), so a launcher value sat at a slider's end;
  they now span the launcher's with the game's own on a position, rounded to 4 places.
- **The launcher never read config.ini again**, and wrote over the game's file while it ran. It now holds
  config.ini writes while the game runs, merges them at its exit, reloads its pages, and reads the file
  again when its window is activated with the file changed (`IniFile.FileTime`, `Workspace.ReloadIni`).

The System menu's PC pages: named Graphics, Effects, Colour, Camera Tuning, Extras (were PC Graphics,
PC Effects, PC Colour, PC Camera, PC Enhancements), Extras last; help texts name the game's own position
and when a restart is needed (`pc_option_messages.tsv`, generator version 10). A parent "PC Settings" row
and a "restart now?" question were not built: both need the game's menu code studied in its executable,
which isn't here. Checks: both launchers and the localization checks compile (the checks need Windows to
run); settings-test (g++, clang) and the menu text test pass. 13 launcher languages (17 strings added,
5 retired), the System menu in English and Chinese.

Test: the System menu: page names and order, a slider at the launcher's value, an effect toggled with
Picture Tuning off (it stays off), Output Resolution changed at 1920x1080 with Live resolution Off (the
game keeps 1080p until restarted) and On (at once). The launcher: Display, Graphics, Look, Interface in
both layouts; each fold (switch off folds, on opens, the chevron by hand, the search opening one); HDR's
switch on the HDR-less monitor (greyed, "This display shows no HDR"); a setting changed while the game
runs (status "game settings apply at its next start", applied after the game exits) and one changed in
game (the launcher shows it after the game exits); the mirror and rebirth rows; 1280 × 800.

### Beta 27: AMD FSR SDK 2.4 and the upscaling settings

| Item | Size |
|---|---|
| AMD's signed DLLs and licence notice (binary redistribution with the notice; no reverse engineering, decompilation or disassembly); a download script from AMD's release | M |
| One "FSR" choice; AMD's SDK picks (`ffxQueryDescGetVersions`): FSR 4.1.2 FP8 on RX 9000; INT8 on RX 7000, RX 6000 and Ryzen AI Max 300/400; lightweight INT8 on Ryzen AI 300/400 and Z2 A/Z2 Extreme; FSR 3.1.6 elsewhere (RX 5000 and older, NVIDIA, Intel). The log names the version. Saved `fsr4`, `fsr411` and `fsr3` become `fsr` | M |
| Removed: FSR 4.1.1 (`fsr411/`, `tools/fsr4cap`, the `tools/fsr4*` scripts, the `fsr4-bench` tool: it replays AMD's DLL with shaders and weights taken from it, which AMD's licence forbids), the built-in FSR 4 (`vk_fsr4.cpp`, `fsr4_shaders`, `fetch_fsr4_assets.sh`: the source-v07 model), fsr4vk (`vk_fsr4vk.cpp`, `fetch_fsr4vk.py`), FireBurn FSR-Vulkan (`third_party/fsr-vulkan`, 35 MB) with FSR 3.1 upscaling and frame generation; their menu entries, switches and docs. Credits for the removed code (FireBurn, 0xCydral, dvj5411) move to an "earlier versions used" line | M |
| The SDK can't start: TAA with a note | S |
| The upscaling settings cleanup (discussed with the tester before any code): one Upscaling section with each choice described; the render resolution with its real size; one sharpening slider; options per upscaler; no "preset" wording; the in-game menu's names to match | L |
| Translations: 13 launcher languages, 10 in-game | M |

To settle in the settings discussion: upscaler "Off"; merging the mip-bias settings. An "FSR version:
Automatic / 3.1" choice (`ffxOverrideVersion`) is in: no AMD testers (the tester, 2026-10-31), so FSR 4.1.2
on AMD cards ships untested and AMD players need a way back to 3.1 without waiting for a fix; the release
notes say it is untested. Test: the tester alone, FSR 3.1.6 through the SDK on the RTX 3070 Ti against
FireBurn's FSR 3.1 (0.64 ms at the lamp spot, Quality); the TAA fallback (the SDK's DLLs taken out).

**Built first (27a, 2026-11-01), on `claude/0.4.4-beta-27` from 26b:** the first four rows. AMD FSR in
`vk_amd_fsr.cpp` on AMD's MIT headers (`third_party/amd-fsr-sdk`, v2.4.0 `7539ae8`); AMD's DLLs from
`tools/fetch_amd_fsr.sh` (SHA-256 checked, with AMD's licence) in `engine/amd-fsr`, also in
`licenses/AMD-FSR-SDK.md`. The version list logged at start; Automatic (no override) or FSR 3.1 (AMD's
override by name, a shipped version before the driver's); the context made on a worker thread, the scene
at full size meanwhile; frames in flight kept under AMD's ring of four; the reactive mask and the
`fsr3_*` tuning given to AMD's FSR (3.1). Device features AMD's Vulkan sample asks for enabled when its
DLLs are present. Fallbacks: DLSS to AMD FSR to TAA (before the first frame and in play); bb-start and
`patches.py` size the start for TAA when AMD's files are missing. Removed as listed (fsr4vk's fetch script
was already gone); frame generation keeps DLSS-G's code (not offered). The launcher: one AMD FSR choice,
its FSR version row, Best settings picking AMD FSR (TAA without its files), the FSR 4-only rows out
(the Avalonia launcher keeps its pages until Linux; it builds). The in-game menu: the upscaler list, FSR
version, the running version's name; its new texts in English and Russian only until the settings
cleanup's translation pass. Not yet: the settings cleanup (row 5; discussion first) and the in-game
translations. Checks: the engine files' syntax (g++; the AMD provider's Windows branch with MinGW),
settings-test and upscaler-support-test (g++, clang), `test_patches.py`, both launchers and the
localization checks build. Untested in a run.

**Then (27b, 2026-11-01), on the same branch:** row 5 and the translations, with the tester's calls on
26b (Look goes, picture tuning under HDR, an anti-aliasing choice greyed with an upscaler, children under
their parents, the detail settings together, character motion vectors in Advanced, the presets reworked,
the title's duplicate Quit to Desktop). The launcher's Graphics: Upscaler AMD FSR, DLSS or None (FSR
version or DLSS model under it); Render resolution (no "preset" wording) with each entry's real size
(`Choices.RenderSize`, the engine's rounding: `ForScale` live, `ScaledSizes` patched at start);
Anti-aliasing TAA, the game's own or Off for None; sharpening with its strength under it; Advanced with
character motion vectors; Detail; Effects (from Look). Display gains Picture tuning below HDR. The engine:
the game's own anti-aliasing only with `upscaler=off` (`NoUpscaler` in bb-start and `patches.py`); the
System menu's Quit to Desktop in the world only (`world_player_block`), kept at the title when the title's
own Quit Game could not be added. The Insert menu in the same order, with `BbSettings::PresetRenderSize`
for the sizes and the game's own anti-aliasing moved from the game effects to Anti-aliasing. Settled:
"Off" as an upscaler is gone from the menus (None plus Anti-aliasing say what it was); the two mip-bias
settings stay apart (DLSS's is NVIDIA's documented extra level, the other a player offset). Translations:
the launcher's 13 languages, the in-game menu's 8 (27a's texts included). Version stays
0.4.4-beta.27; engine id 61.

**Then (27c, 2026-11-02), on the same branch:** the tester's review of 27b (their first AMD FSR runs: FSR
3.1.6 through AMD's SDK on the RTX 3070 Ti, 0.58 ms at Quality and 0.80 ms at Native AA a frame; Native
AA 7.2 ms against Quality's 6.4 ms at the view, 139 against 157 FPS). Decided with them before any code:
Anti-aliasing first; plain quality names, the numbers on a render resolution slider that only Custom
moves (locked, not greyed); an Auto quality, the default, by the output's height (Native AA up to 1200
lines, Quality up to 1600, Performance above: never much more than native 1080p's work); every FSR
version AMD offers on the GPU, FSR 2.3.4 included, detected for the launcher by `bb-gpu-capabilities
--fsr-versions` (Automatic only when it cannot tell); DLSS's Auto model the default; the overlay's DLSS
section merged into the same layout; the running FSR and DLSS versions in the overlay and the FPS
counter; one short line per launcher row (`LauncherPage.Brief`, 64 characters) and the System menu's
help lines within about 65 (Output resolution's, 146, was cut by the window). The tester's catch: the
shipped `config.default.ini` still said `fsr411` at Quality: it now matches the launcher's and the
engine's defaults, with tests for both. Engine id 62.

**Then (27d, 2026-11-02), on the same branch:** the tester's review of 27c's Graphics page, decided with
them: Anti-aliasing greyed with "Upscaler (DLSS)" in its box (it was empty, and its row did not look
greyed: no launcher row did, only its control; a disabled row now dims on every page); the render
resolution greyed unless Custom, one indent (beside Quality); Sharpening kept as it was, RCAS in its name,
with a line that says where it runs for what runs and a tooltip; the upscaling rows a tooltip with advice
(27c's short lines had left none; the other pages keep their full text on hover); Advanced down to
character motion vectors and texture mip bias. Two folds from the cleanup audit, done here: DLSS's
"sharper textures" switch into the bias (log2(render/output) - 1 for AMD FSR too: AMD's FSR docs ask for
the same), and the two auto exposure switches into one rule, no setting (AMD FSR always, as AMD advises;
DLSS with the HDR scene). The in-game menu's FSR checks (auto exposure, jitter sign) and sub-pixel jitter
left the menu (config.ini keeps `fsr4_invert_jitter` and `jitter` for tests). The tester's report: the
System menu's Anti-Aliasing showed On with DLAA (the saved choice, not what runs): it shows Off while an
upscaler or TAA runs, locked, with a help line that says why (117009). Engine id 63.

### Beta 28: the cleanup (decided 2026-11-02)

From the audit of 27c (four read-only audits, re-checked) and the tester's calls. Its own branch,
`claude/0.4.4-beta-28`, one commit per part so a part can be undone alone:

- **Fixes and dead code** (no behaviour change): `test_run_settings.py` (fails since 27a: `fsr3`); page
  subtitles the launcher never shows (Online's three steps); launcher strings that lost their translations
  to a rewording; `BB_FSR4_PROFILE`/`BB_FSR4_MODEL` set for nothing; licence notices the package lacks
  (ImGui, LibAtrac9, sirit, SPIRV-Headers, half, DejaVu, Roboto); `amd-fsr/` in `.gitignore`. Dead code:
  FSR 3.1 frame interpolation's remains, the 8-bit upscaler output path, the bbhost debug-menu key,
  functions without callers, vendored shadPS4 files nothing compiles, `--live-resolution`, 27c's unread
  `fsr_version_serial`, unused launcher members and translation keys (with a check for unused and missing
  keys), the never-run workflow under `engine/.github`, `.gitmodules`, `tools/release-assets.ps1`, the
  drifted Dockerfile, `tools/bridge_helper`, the duplicate hash list, unused third-party files (ImGui's
  examples and prebuilt libraries, SPIRV-Headers' extras, `gcn`, half's tests, LibAtrac9's C# port), docs
  that describe removed features moved to `docs/archive/` or corrected.
- **Removed:** the PC memory model (`BB_GUEST_IN_PLACE` and the switches under it, `bbport_guest_memory*`,
  its probe tools and doc); frame generation, Streamline and Reflex's code (`vk_streamline`,
  `vk_frame_generation`, its shaders and settings, the Streamline headers and fetcher; the
  `VK_NV_low_latency2` clock boost stays); compatibility with Lantern 0.9.x installs (`run.bat` and
  `scripts/run_windows.py`, the launcher's Python start, the 0.9.x settings migrations, `LayoutCleanup`, the
  mouse-bind fix, the one-time preset question); the old Linux pieces (the Python GTK launcher, the
  Nix/AppImage build, `shell.nix`, `test_packaged_vulkan.py`; "Linux, for later" in [plan.md](../plan.md)) and the bbport-era setup
  (`setup.bat`, `tools/setup/BbportSetup.cs`).
- **Engine switches settled by measurement:** removed, gone through with the tester (54 found; the A/B
  switches still compared stay).
- **Folds:** env aliases of config.ini keys (config.ini kept, `BB_UPSCALER=none` for A/B); `BB_FRAME_TIMER`
  and `BB_CLOTH_STEP` into the frame fixes' skip list, the game fixes' pair likewise; the launcher's
  experiments and Safe mode as one engine switch each instead of sets of env vars; the nine sync fallbacks
  as one; the spin-wait knobs as one; names that pair (`BB_GPU_LOG`/`BB_LOG_LEVEL`, the thread samplers,
  env vars and toggle bits for the same thing); `fsr4_*`/`fsr3_*` keys renamed `fsr_*` (the old ones read).
- **Defaults that disagree:** the launcher's values become the engine's (`BB_OCCLUSION`, `BB_NATIVE_CALLS`,
  `BB_FAST_LOADING`, `BB_HOT_PAGES`, `BB_AUDIO_THREAD_PRIORITY`, `BB_PRECISE_PACING`); `run.sh`'s exports;
  the Uncapped limit (240 or 0 by reader); `BB_INTEL_TONEMAP_FIX` (patches.py and bb-start differ); the
  sharpness sliders' 0.5 fallback.
- **Still to ask:** the crash diagnostics for the fixed 0x263b8e7 heap corruption (`bbport_free_check`,
  `heap_sites`, `write_log`, `windows_experiments`; recommended: remove); OptiScaler (one key); the old
  shadNet online path and bbnet's NP modules the game never imports (checked first; done: the path in
  part 7, then all of shadPS4's sceNet, NetCtl and NP libraries and the shadNet client, once the
  game's import list and an online run, 2026-11-02, showed nothing called them); the GPU-path tracers
  that overlap; `docs/archive/`.
- **The launcher's texts, last before the version step** (the tester, 2026-11-02): once every part above
  and every open task is done. In the port's own voice (the tester: not a production game's): today's
  texts stay where they are right, with their translations; a line under the title (a complete phrase of
  60 characters or fewer) where Classic cuts today's description mid-sentence or it says too little; a
  tooltip changed only where it is wrong or missing; "Default: …" added where the text doesn't say it;
  Off or None first in every list; no Lantern version references. Row by row:
  [launcher-text-review.md](../launcher-text-review.md),
  with its style guide, every row's text today and the proposal beside it; made from the code by
  `launcher/windows/tests/text_review` (extract.py, render.py, the proposals in proposals.py), so the step
  makes it again from the launcher as it is then, checks every fact against the engine, and the tester
  goes through it before the texts change. Its fifth draft (2026-11-02) takes in an outside review of the
  fourth (five overpromising texts, two old menu names, the frame-rate notes against V-Sync) and the
  tester's decisions, and lists the changes outside the texts that come with it: suggested settings read the
  game's monitor, Changed switches lists what Extra variables override, Unified frame time joins Frame rate
  corrections, Fixed-refresh frame lock greys without Precise frame pacing, settings sharing takes everything
  but what belongs to the PC, the player or one session, and Graphics' Game effects and Enhancements groups.
  With it: the 13 launcher languages for every changed text (a strict check for them), both launchers built,
  the translation key check clean. **Applied** (2026-11-02, after the tester's last call: both Intel effect
  fixes stay separate): the row code (the line apart from the tooltip, no final full stop, no tooltip that
  repeats the line, "Default: …" from the setting, checked in English through `Localization.Source`), every
  text, the changes outside the texts, the in-game overlay menu's Enhancements section and names (its 8
  tables), the Also found fixes, and the 165 new texts in all 13 languages (machine-made). Not done here: the
  on-screen check at 100% and 150% in English, German and Japanese (needs Windows). The ~206 older texts
  the 12 other catalogs lacked (zh-CN had them) were translated after it (2026-11-03): 199 of them; the
  other 7 are looked up only by the Avalonia launcher (frame generation, the PC memory model and other
  rows since removed) and go with the Linux work.
- **The Log page** (the tester, 2026-11-02): one Log options fold again, the log level first in it and the
  diagnostics after it (part 12 had put the log level above a Diagnostics fold).
- **Then the version step:** 0.4.4-beta.28, engine id 64, release notes (their Defaults section: a new
  install starts without an upscaler, on the game's own anti-aliasing), this document, every check.
  **Done** (2026-11-02): the three launcher projects at 0.4.4-beta.28, `Updater.EngineId`
  hunters-workshop-64; the release notes have beta 28's defaults, the launcher's changes and the
  cleanup (switches settled, folds, `BB_DIAG`, the shadNet path, Lantern 0.9.x installs).
- **After the launcher: Streamline's slowdown** (the tester, 2026-11-02). Not the 120 FPS cap (beta 23,
  above), which stays: the lower frame rate with Streamline loaded was probably the game's own
  anti-aliasing still running under an upscaler, which 27b fixed (`NoUpscaler`). To be checked again
  once the launcher is done.

### Release candidate, then 0.4.4

An independent review (a sub-agent; the tester is told before it starts); version 0.4.4, engine id,
release notes, docs, `README.md` credits. The tester: a long session (heap growth, `gnm-1`), safe mode,
broad play, online.

### After 0.4.4

AMD's official FSR frame generation (AMD's swapchain); OptiScaler compatibility re-checked, then kept or
dropped; the proper DLSS test, then its cost table; live-path proxy trimming (upscale-2/3); the SSR patch's
tuning writes measured (bbhost: 7.8 ms against the game's 6.1); smoothness and load hitches; less detail
when the startup patch renders below 1080; the passes left unjittered (Lantern's DLSS audit; a likely slight
wobble while panning) checked in HW; the message boxes' confirm delay (bbhost, Kyoskii; optional,
a feel change); features: Anti-Lag 2, lighting effects, lamp to lamp, SMAA/FXAA, FSR 1; the frame lock
with Match display (pinned); the AMD/Intel shader-3 and CPU-bound items.

### Settled since (2026-10-31)

- The beta order: the camera (24) before AMD (now 27, after the launcher's 26).
- No AMD testers: the tester tests FSR alone (beta 27's note above).
- Beta 23 built and tested, then marked failed (2026-11-01, the tester's call): beta 24 from beta 22,
  beta 23 redone as 25, AMD 26, the PC memory model 27.
- Beta 24's two questions (2026-11-01): the approach as described; +0x1a8 under FPS++ is Kyo's value.
- DLSS's Project ID (2026-11-01): dropped; the shared id stays and the start code is not touched for it.
- Live resolution change (2026-11-01, the tester's call): Off or On only, Off recommended and the default; the "Auto"
  choice goes (25c).
- The launcher (2026-11-01, the tester's call): its sections reorganised, "SIGHT" becoming "Visuals" with a clearer
  structure, as a beta of its own once the new structure is agreed (proposal first). The classic layout is the one
  reworked; the Modern layout is kept and follows the same pages and groups (its tabs map to them); the Avalonia
  launcher is left until Linux, last. The proposal, as built: `docs/launcher-layout-proposal.md`; its recommendations
  taken (2026-11-01, the tester's call) and built as beta 26.
- Beta 26b (2026-11-01, the tester's calls): Display, Graphics, Look and Interface; Picture tuning kept as a
  switch with its settings folded under it, as are HDR, the FPS counter and Camera tuning; the System
  menu's PC pages renamed and reordered (a parent entry if the game's menu code allows, later); save
  backups while playing every 10 minutes; the Steam Deck page kept.
- The upscaler's place at outputs other than 1080p (2026-11-01, the tester's call): it stays on the game's finished
  frame. Taking it before tone mapping there (NVIDIA §3.1) would run the game's post-processing at the output
  size, a renderer split the game was not built for, for a modest gain and a real cost; not planned.
- Co-op: leaving the room on quit, in 25a (2026-11-01).

### Open questions

1. At the start of beta 25: Streamline's DLSS path (`StreamlineDlss`, kept in beta 23 only for the
   `BB_STREAMLINE=1` test) carried or left on the beta 23 branch (recommended: left, nothing offered uses
   it); beta 22's boost hint (`VK_NV_low_latency2`, boost only, `reflex` in config.ini), which beta 23
   removed with its Reflex (it did not hold the clocks in the tester's runs): removed again or kept until
   Reflex's own design.
2. Where NVIDIA Reflex's own design goes (a beta before the release candidate, or after 0.4.4).
3. Frame generation: deprecated before release, or kept as code that is not offered.
4. In beta 27's settings discussion: upscaler "Off"; merging the mip-bias settings.

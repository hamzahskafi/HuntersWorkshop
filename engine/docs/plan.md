# Hunter's Workshop: the plan

What comes next, and the rules and decisions it works under. Updated 2026-11-05.

- **Where it stands:** 0.4.4 was released on 2026-11-03 (`cbec5a2`, engine id 64). 0.4.5 beta 1
  (engine id 65) left out the empty draws: +2 FPS at the lamp spot (GPU 7.40 to 7.30 ms). Beta 2
  (engine id 66) removes beta 1's `a954e79d` step, which gained nothing, and brings the profile and
  log lines step 1 needs next. Beta 3 (engine id 67) traces the shadow steps.
- **Where the work goes:** the branch `claude/awesome-gates-cjba5e`, until a beta has its own
  (`claude/0.4.5-beta-N`).

The other documents:

- [performance.md](performance.md): the findings behind the performance work below.
- [changelog.md](changelog.md): every change since Hunter's Workshop (HW) began, with its
  measurements and credits.
- [community-fixes.md](community-fixes.md): what the other Bloodborne PC projects fixed, vetted
  against HW.
- [engine-map.md](engine-map.md): how the engine works.
- [archive/](archive/README.md): finished plans and closed investigations. The plan that led to
  0.4.4 is [archive/plan-0.4.4.md](archive/plan-0.4.4.md).

## Ground rules

- **No merge with Lantern Engine.** Its 0.9.37 stable regressed from beta 8 (the fixed-refresh
  frame lock and the particle cap, both off in HW), and it removed community patches and their
  credits.
- **Community patches and their credits stay.** A fix taken from another project is credited in
  `README.md` and in its commit.
- **AMD's and NVIDIA's own documents** (the FSR SDK, the DLSS Programming Guide) are credited as
  such, not to a project that applied them first. The tester, 2026-10-31: "this is AMD spec not
  theirs", "also nVidia spec and not theirs".
- **AMD's and NVIDIA's libraries are used as shipped,** never disassembled (the NVIDIA RTX SDKs
  licence, §4a).
- **The particle cap and the fixed-refresh frame lock stay off** (`BB_INDIRECT_CLAMP`,
  `BB_VBLANK_PACING`).
  - The frame lock with a "Match display" frame rate is pinned for a later version: the
    display's own rate, with its flips on the display's vblanks.
  - The tester found Lantern's lock not smooth; until then, there is no native-rate option.
- **Root causes, for every vendor.** A fix finds the cause and works on every GPU vendor; a
  workaround for one card is the last resort.
- **The launcher's pages and settings:** changes are proposed to the tester before they are
  built.
- **One beta per step.** Each beta has its own branch and engine id, and is measured on the
  tester's PC against the one before.
- **Linux waits until Windows is done.**

Addresses in these documents: **patch** addresses are the ones `Bloodborne.xml`,
`frame_rate_sites.inc`, bbhost and Binary Ninja use (image offset + 0x400000). **Image offsets**
are what bbport's `ImageBase + x`, HW's `Prepare()` calls and the sampler's `+0x...` print. Each
address says which it is.

## Decisions

The decisions that still stand. How each was reached is in [archive/plan-0.4.4.md](archive/plan-0.4.4.md).

| Topic | Decision |
|---|---|
| FSR | AMD FSR SDK 2.4 (FidelityFX-SDK `v2.4.0`, 28 October 2026) on Windows: AMD's signed `amd_fidelityfx_loader_vk.dll` and `amd_fidelityfx_upscaler_vk.dll` with AMD's licence notice. Every unofficial FSR path was removed (0.4.4 beta 27). |
| DLSS | The driver's NGX core, started with the shared application id 231313132 (`Init_Ext2`). No Project ID (the tester's call, 2026-11-01): the core's entry point that takes one is declared nowhere public, and learning it means disassembling NVIDIA's libraries, which their licence forbids (§4a). Not Streamline: with its core loaded, NVIDIA's Vulkan driver presents through DXGI and holds V-Sync On (latest frame) to two frames a refresh. NVIDIA's NGX loader library is not linked: the DLSS SDK licence (§2e) forbids use that puts the SDK under an open-source licence (HW is GPL). |
| When AMD's or NVIDIA's runtime can't start | TAA, with a log line and a note in the game. |
| Frame generation | Not offered, and its code removed in 0.4.4 beta 28 with Streamline's and Reflex's (the tester's calls, 2026-11-01 and 02). If it returns, it is AMD's on AMD's swapchain, not built on that code. |
| NVIDIA Reflex | Not offered. If it returns, it is a design of its own on `VK_NV_low_latency2` directly (the sleep before the frame's work, markers with present IDs), measured against the 120 FPS cap first. |
| OptiScaler | Set aside: the compatibility mode (`vk_optiscaler.h`) stays as it is, untested. Re-checked after the performance work. |
| The upscaler at outputs other than 1080p | It stays on the game's finished frame (the tester's call, 2026-11-01). Taking it before tone mapping there would run the game's post-processing at the output size, a renderer split the game was not built for. |
| Live resolution change | Off or On only; Off is recommended and the default (the tester's call, 2026-11-01). |
| The follow camera | Fixed in HW's engine (0.4.4 beta 24): every per-frame weight converted each frame with the frame timer. FPS++ is that base plus only what Kyo changed. bbhost's weight list is a cross-check only. |
| PC memory model | Removed (0.4.4 beta 28). The game's data lives in VRAM; the GX region is read in place. |
| Online connections | The engine reports a connection to the game once the server gives the player's address, and a failure when the server says the player left or timed out. That is how bbhost and Lantern 0.9.39 do it; nothing to change (checked 2026-11-03). |
| Test PC | RTX 3070 Ti, i5-14600KF, 60 Hz display; Windows HDR is off. HDR-output items are checked in the code, not in the tester's runs. |

## Now: performance

The work after 0.4.4, in order: the tester's call (2026-11-05) puts fewer GPU steps first and the
memory cleanup after them. The evidence for each item is in [performance.md](performance.md).

- **The targets:** about 0.6–0.8 ms back of a 7.4 ms frame at the lamp spot without changing a
  setting or a pixel (135 to about 145 FPS; 0.1 ms of it done in beta 1), then smoothness at every
  frame rate. First written as 1–1.3 ms: `a954e79d`'s 0.2 ms turned out to be another shader's
  real work, and the shadows' merge is smaller than thought (see below).
- **No measurement beta first.** The tester's logs already hold what the first items need (the copy
  shader's movable copies counted since beta 18, `a954e79d` and `a040875f` decoded in beta 18). Each
  change comes behind a switch, and its gain is the lamp spot's FPS with it on and off.
- **What the logs didn't hold comes with the betas:** a counter per item that shows it acting
  (`GPU steps per second (0.4.5)`) and the first kinds of empty draw with their depth registers
  (beta 1); a GPU profile label for indirect dispatches and what is recorded around the `a040875f`
  copies (beta 2). `BB_SHADER_DISASM` still dumps compute shaders only; vertex and pixel shaders come
  when step 2 needs them.

**Step 1: fewer GPU steps.** Each item gives the same picture, bit for bit where a shader's output is
involved.

- **Done:**
  - **The empty full-screen draws, 0.1 ms (beta 1, measured).** A draw is left out when nothing it
    could touch is bound: no colour or depth target (as `PrepareRenderState` chose them), no stage
    writing memory, no motion vector attachment, no occlusion query measuring it
    (`Rasterizer::SkipEmptyDraw`, `BB_SKIP_EMPTY_DRAWS=0` to record them). Lamp spot: 135.0 to
    137.0 FPS, GPU 7.40 to 7.30 ms; second spot 122.2 to 123.7 FPS. All of them were one kind:
    depth and stencil off, `DB_RENDER_CONTROL` 0x60 (the compress-disable bits), the in-place depth
    expansion GCN drivers do.
- **Dropped:** **`a954e79d` as one step** (beta 1, removed in beta 2). It was exact (one atomic add
  of `groups` instead of the loop), but its profile label stayed at 0.21 ms a frame: the time was the
  work after it, indirect dispatches with no label of their own. Beta 19's atomic change, which also
  moved nothing, had pointed the same way. Beta 2's labels split it: the indirect dispatch
  `2da7fe60` (0.12–0.14 ms a frame, 5 of them) is the cost, and `a954e79d` is under 0.065 ms. Real
  work, so it goes to step 2.
- **Next, with the tester's say:**
  - **The copy shader: about 0.1–0.2 ms, not the 0.3–0.5 first written.** Its copies would move
    ahead of a render pass that has already begun, so the engine would have to record work before
    commands it has already recorded, and its barrier tracking would have to follow. The gain is
    the 20 passes a frame begun again, not the copies themselves, which still run.
  - **The shadows' `a040875f` copies:** part of the shadow item below (each sits between a
    level's draw and its filter).
- **The shadows: about 0.35 ms, if each face's four levels can be reordered.** Beta 2's log
  showed the order: each level of each face is drawn, copied (`a040875f`) and filtered
  (`0f61fd27`) on its own, 144 steps a frame, not the 256→128→64→32 chain both earlier options
  assumed (withdrawn; performance.md, "The dynamic lights' shadows").
  - **The one merge left:** within a face, draw its four levels first, then one copy and one filter
    for all four (12 steps to 6). The engine would hold back the game's copy and filter until the
    face's last level, and run a compute filter for the four sizes that reads through the texture
    unit like the game's, so it can be exact.
  - **First, one frame's trace** of these steps with their images (beta 3): whether each size has
    its own targets, and whether a later level reads an earlier level's copy or filter. If either
    fails, the shadows stay as they are.
- **Smaller items:** `mem-4`; `upscale-2` and `-3` (0.29 ms at Quality, nothing at DLAA); the GPU
  idle between submissions (0.1 ms).

**Step 2: shader code.** It acts on the shading, the part of the frame that scales with pixels
(1.5–2 ms at 1080p).

- Rank the game's shaders by GPU time, then check the top ones with `bb-shader-gpu-stats`
  (`native_shader_compiler.md`). Look at registers and occupancy, inputs still interpolated by GCN's
  formula (321 of 1,201), and the legacy multiply.
- Vertex shader contraction with an invariant position (`shader-5`, about 4% of vertex code).
- The native compiler's step 2 (control flow without goto variables): measured in the lab first, and
  built only if the hot shaders gain.

**Step 3: the memory cleanup, for smoothness at every frame rate.**

- **Evict by the VRAM budget, not by age alone.** While VRAM is below a share of the driver's budget
  (`VK_EXT_memory_budget`, which `UpdateVramLevel` already reads), nothing leaves; above that share,
  the oldest leaves first. The 8 GB laptops the cleanup was added for stay covered, since their budget
  fills.
- **Paced:** in play, at most a set amount a second; larger batches during loading screens.
- **The bind waits only for the work that used the evicted blocks.** This needs a last-use tick per
  block; until then, the unbinds go in batches on loading screens.
- **With it, the counters:** MiB and blocks per cleanup, stage B's time in it, bind waits, and blocks
  coming back within 10 minutes (the churn).
- **Expected:** 3 of the 4 long-frame minutes of the 217 gone, with the uploads and write faults of
  the churn.
- **Later, if the logs still show stutter after loads:** eFMann's ideas
  ([archive/hitches-2026-10.md](archive/hitches-2026-10.md), "Smoothness leads").

**Step 4: the CPU, for other PCs.**

- Stage B on two threads (`parallel-stageb.md`, approach B), once a 6-core CPU or a frame rate above
  about 230 FPS makes it the limit.
- The small CPU items with it: `idle-1`, `pipe-1`, `asc-2` and `rt-1`.

**Later, if at all:**

- A GPU timer at pass ends that doesn't wait at each label, if an item's gain needs a finer measure
  than FPS.
- Serve the copy shader's ranges from the engine's snapshots, so no copy runs (not the PC memory
  model; see performance.md).
- Draws built from the game's GX objects and barriers taken from its drain calls, as bbhost does: a
  renderer of a different kind, not a step of this plan.

**Not reopened:**

- `seg-1`, `renderer-3`, `renderer-4`, `renderer-6` and the refuted ideas (`engine-map.md` 5).
- The October hitch leads (the game's job threads, the online summon, the PC memory model): fixed,
  removed, or not the cause in 0.4.4's logs.

### Waiting on the tester

- **0.4.5 beta 3 at the lamp spot** for about a minute, with frame statistics and GPU time on: its
  log holds two traces of one frame's shadow steps (the tester chose the trace, 2026-11-05).
- **Optional, on beta 2 as it is:** `BB_SHADER_DISASM=2da7fe60` in Extra variables puts the code of
  the indirect dispatch that costs 0.12–0.14 ms into the log, for step 2.
- **Whether to do the copy shader's move** (about 0.1–0.2 ms, the riskiest of step 1).
- **The cleanup off, one session, before step 3 (whenever it suits):** an hour at 60 FPS, ideally a
  chalice dungeon, with `BB_VRAM_CLEANUP_SECONDS=0` in Extra variables. The launcher's "VRAM cleanup"
  row is another cleanup, of textures, and stays on.

## Next, after the performance work

- AMD FSR frame generation through the SDK, on AMD's swapchain.
- OptiScaler checked again, then kept or dropped.
- A proper DLSS test, then its cost table.
- The SSR patch's tuning writes measured (bbhost: 7.8 ms against the game's 6.1).
- Less detail when the startup resolution patch renders below 1080.
- The passes left unjittered (Lantern's DLSS audit; a likely slight wobble while panning),
  checked in HW.
- The message boxes' confirm delay (bbhost, Kyoskii; optional, a change of feel).
- Features: Anti-Lag 2, lighting effects, lamp to lamp, SMAA/FXAA, FSR 1.
- The frame lock with Match display (pinned).
- The AMD and Intel shader item (`shader-3`).
- Small: the online probe's log line says how long the probing took, as bbhost's does
  (`after N ms`), since log lines carry no time.

## Checks not made before 0.4.4

- **The launcher's texts on screen:** at 100% and 150% scaling, in English, German and Japanese,
  in both Classic and Modern layouts, plus the Insert menu's Enhancements section.
- **Launcher behaviour:**
  - Suggested settings with two monitors;
  - a Share code exported and imported on another PC;
  - Changed switches with Extra variables set;
  - the fixed-refresh frame lock greying out.
- **Online: joining another world** (summoned with the Small Resonant Bell, or invading). Hosting
  is checked: two invasions on 2026-11-03.
- **Untested for want of hardware:** AMD FSR 4.1 on Radeon cards, FSR 2.3.4, HDR output.

## Open decisions

- NVIDIA Reflex on `VK_NV_low_latency2` without Streamline: whether, and when.
- The GPU-path tracers that overlap.
- The System menu's PC pages under one parent entry, if the game's menu code allows it.

## Closed since 0.4.4

- **Lamp and candle flicker (B14):** looked at again at 240 FPS and barely told from 30. Option C
  (the light module held to 30 steps a second) stays written down for a high-refresh report.
- **Online (two invasions, 2026-11-03):** the room, the join, the hole punch, traffic both ways
  and both leaves worked at 60 FPS. A travel started as an invader arrived held its loading
  screen 26.7 s until the game ended the session, which is the game's own handling. Four of five
  players never answered the probes; the game also connects to players whose signs and ghosts it
  shows, and the relay is the server's choice.

## Linux, for later

| Topic | Note |
|---|---|
| The launcher | The Avalonia launcher rebuilt on the Windows launcher's pages, with its stale texts, the dead DLSS model row and the 7 texts only it looks up. |
| FSR | AMD's SDK is Windows only. Linux has no FSR until AMD ships for Linux; the Linux build keeps compiling, and FSR is not offered there. |
| DLSS | NVIDIA's Linux loader library (`libnvsdk_ngx.a`) has the same licence problem as Windows': Linux keeps HW's own path (`libnvidia-ngx.so.1`), the driver's core, with the shared id. |
| Old Linux pieces | Removed in 0.4.4 beta 28: the Python GTK launcher, the Nix/AppImage build, `shell.nix` and `test_packaged_vulkan.py`. Linux keeps `packaging/linux/` (Ubuntu setup, `bb-start`, `play.sh`), `run.sh` with the Python start scripts, and the Avalonia launcher. A Nix or AppImage build, if wanted, is made again from those. |

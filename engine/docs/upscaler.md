# Temporal upscaling and frame generation — frame analysis and plan

## Today (0.4.4)

The sections after this one are the record of the work from the first frame analysis (bbport,
2026-10-01) on, dated; what runs now:

- **Upscalers** (`upscaler` in `config.ini`, `BB_UPSCALER`; the launcher's Graphics page and the
  in-game menu): `fsr` (AMD FSR through AMD's FSR SDK 2.4, `vk_amd_fsr.cpp`; from 0.4.4 beta 27),
  `dlss` (DLSS Super Resolution, NVIDIA RTX), `taa` (below) and `off`. A `config.ini` without the key
  gets `fsr`; the older `fsr3`, `fsr4` and `fsr411` read as `fsr`. `BB_UPSCALER=none` leaves the
  whole path out. From 0.4.4 beta 27b the menus offer AMD FSR, DLSS or None as the upscaler and, for
  None, Anti-aliasing: TAA (`taa`), the game's own (`off` with `effect_game_aa=1`) or Off (`off`).
- **The game's own anti-aliasing** (`effect_game_aa`, a patch at start, off by default) applies only
  with `upscaler=off` from 0.4.4 beta 27b: under AMD FSR, DLSS or TAA bb-start and `patches.py` apply
  "Disable AA" whatever the key says (`NoUpscaler`; `BB_UPSCALER` decides when set). It ran after the
  tonemap, on the frame an upscaler at other outputs takes, softening it.
- **AMD FSR** (0.4.4 beta 27): AMD's signed `amd_fidelityfx_loader_vk.dll` and
  `amd_fidelityfx_upscaler_vk.dll` in the package's `engine/amd-fsr` (`tools/fetch_amd_fsr.sh`, with
  AMD's licence; `BB_AMD_FSR_DIR` overrides the folder, `BB_AMD_FSR=0` hides it), loaded at run time
  and driven through the AMD FSR API (`third_party/amd-fsr-sdk`, MIT headers). AMD's SDK lists the
  versions for the GPU (the log names them) and picks one: FSR 4.1.2 on RX 9000 (FP8), RX 7000, RX 6000
  and Ryzen AI Max (INT8), Ryzen AI 300/400 and Z2 (lightweight INT8); FSR 3.1.6 elsewhere (NVIDIA,
  Intel, older Radeon). `fsr_version` (0.4.4 beta 27c) is `auto` or the name of a version AMD lists
  for the GPU (`3.1.6`, `2.3.4`, `4.1.2`; 27a's `3.1` the newest 3.1; `BbSettings::FsrVersionMatch`,
  a shipped version before the driver's), asked for by AMD's version override; live, the context made
  again; one AMD does not list runs AMD's choice, logged. The menus list what AMD offers: the engine's
  probe (`FsrVersionsOffered`), the launcher's `bb-gpu-capabilities --fsr-versions` (AMD's same query
  on a device of the game's GPU). The menu, the FPS counter and the log name the version that runs,
  marked when AMD's driver provides it (`fsr_running_driver`); DLSS's file version too (`dlss_version`). The context is made on a worker thread (it builds the upscaler's shaders): the
  scene renders at full size without the upscaler meanwhile. Inputs as before: standard depth (AMD
  supports it), render-size motion vectors without jitter, dynamic resolution within the output size;
  HDR at a 1920x1080 output, the game's frame decoded to linear light elsewhere. Windows only: AMD's
  SDK has no Linux build. FireBurn's FSR 3.1 and FSR 4, the built-in FSR 4 model, FSR 4.1.1's replay and
  fsr4vk were removed with it (AMD's licence forbids reverse engineering its DLLs, which the 4.1.1
  replay was built from).
- **Where:** at a 1920x1080 output on the HDR scene colour right before the post-processing
  combine (compute shader `9a9cf8a9`), written back so that the game's own post-processing,
  tonemap and UI run on it; at other outputs on the game's tonemapped frame before its UI, which
  is then drawn at the output size. Inputs: the scene depth, camera motion vectors, object motion
  vectors (`object_motion`, on by default) and sub-pixel jitter (`jitter`, on).
- **Presets** (`preset`; the menus' Quality from 0.4.4 beta 27c): Auto (`auto`, the default from 27c:
  `BbSettings::AutoPreset` by the output's height, Native AA up to 1200 lines, Quality up to 1600,
  Performance above; bb-start's and `patches.py`'s start sizes resolve it the same), Native AA (DLAA
  with DLSS), Quality x1.5, Balanced x1.7, Performance x2, Ultra Performance x3 and Custom
  (`render_scale`, 1.0-3.0; the menus' Render resolution slider, in percent of the output per side,
  only Custom moves). TAA always renders at the output. The menus show each one's scene size for the output
  (`BbSettings::PresetRenderSize`, the launcher's `Choices.RenderSize`): live, `ForScale` of the
  output (multiples of 4); patched at start, `ScaledSizes` (at least 640x360, never exactly 1920x1080:
  1916x1076).
- **Render size:** outputs other than 1920x1080 (TAA aside) get the game's render size patched at
  the start (`live_resolution=0`, the default; a preset or output change then needs a restart), as
  in "Startup patch is the default again" below; `live_resolution=1` takes the live path. Off is the
  default and the recommended choice; `auto` (a GPU check) is no longer offered from 0.4.4 beta 25c
  and reads as off.
- **Fallbacks** (0.4.4 beta 27): before the first frame, `dlss` without an RTX GPU, the driver's NGX
  or `nvngx_dlss` becomes AMD FSR, and AMD FSR without AMD's files (or on Linux, or with no version
  for the GPU) TAA; bb-start (and `scripts/patches.py`) choose the start's render size for TAA then,
  so TAA runs at the output size. A fatal failure in play falls back the same way (DLSS to AMD FSR,
  AMD FSR to TAA); in a session started at a reduced render size TAA cannot run, and the picture is
  not upscaled until the game starts again. The menu shows the upscaler in use and why.
- **DLSS's model** (`dlss_preset`): Auto from 0.4.4 beta 27c (NVIDIA's pairing: K for DLAA, Quality and
  Balanced, M for Performance, L for Ultra Performance), was K; J, K, L, M or the DLL's default.
- **Sharpening:** `sharpen` (on) and `sharpness` (0-2, default 0.20). AMD FSR applies its own
  RCAS up to 1 and an extra RCAS pass above it; DLSS, which has no sharpening of its own, gets the
  whole range from that pass; TAA has its own pass (docs/MODS.md, "TAA sharpening"). The launcher's
  and the menu's lines say which, for the upscaler chosen (0.4.4 beta 27d).
- **Texture mip bias** (`mip_bias`, -2..+1, live): the scene's texture LOD bias while AMD FSR or DLSS
  runs is log2(render / output) - 1, as both NVIDIA's DLSS guide and AMD's FSR docs ask
  (`mipBias = log2(renderResolution/displayResolution) - 1.0`), plus `mip_bias`. From 0.4.4 beta 27d
  for both (DLSS's -1 was the `dlss_mip_bias` switch, on by default; AMD FSR had the ratio alone). TAA
  renders at the output: no bias.
- **Exposure:** the game gives the upscalers no exposure value. AMD FSR runs with its auto exposure
  (AMD recommends it unless there is a reason not to); DLSS with its own for the HDR scene (a
  1920x1080 output), none for the game's tonemapped frame. 0.4.4 beta 27d: no settings for them
  (`fsr4_auto_exposure`, on by default, and `dlss_auto_exposure`, off, are ignored if a file has them).
- **Reactive mask** (`reactive`, off by default): AMD FSR's reactive mask (FSR 3.1 uses it; FSR 4
  may; up to 0.4.4 beta 26 FSR 4 got it blended over its output, `fsr4_reactive.comp`, removed); TAA
  and DLSS do not use it, and it is not made for them. FSR 3.1's tuning (`fsr3_*`) goes to AMD's FSR
  3.1 through its key-value configuration. Up to 0.4.4 beta
  25a DLSS was given it as its bias-current-colour mask, which NVIDIA's guide (310.4.0, §3.15) says
  should not be used.
- **History reset on a camera cut** (0.4.4 beta 25b, every upscaler): the first frame after the
  camera turned more than the vertical field of view or moved more than 15 m starts the history again
  (`camera_cut.h`; NVIDIA §3.13). Not less: the follow camera snaps up to 4 m in one frame towards the
  hunter when a wall comes between them, which the motion vectors handle. Frames after a loading screen started again already (a frame
  without the upscaler resets the next). The log names each cut (`Upscaler: camera cut ...`); with
  frame statistics, the resets by cause and the largest turn and move of the other frames.
  `BB_CAMERA_CUT_RESET=0`: logged and counted, the history kept.
- **Jitter:** a Halton(2,3) pattern of 8 x (output / render)^2 phases (AMD's and NVIDIA's formula);
  DLSS at least 16 (NVIDIA's checklist; from 0.4.4 beta 25b), so DLAA has 16 instead of 8.
- **Frame generation, DLSS's options and the mip bias:**
  [archive/dlss_frame_generation.md](archive/dlss_frame_generation.md).

## Bloodborne's frame (1920x1080, Hunter's Dream / Nightmare)

| Passes | What |
|---|---|
| first pass of a frame | copy of the previous UI target into the display buffer (sRGB) |
| shadow | 4096x4096 D32 depth |
| G-buffer | 6 targets 1920x1080 (RGBA8 x3, sRGB albedo, B10G11R11, RGBA16F) + D32S8 depth |
| lighting | light volumes into two B10G11R11 targets |
| scene color | RGBA16F 1920x1080, also forward/transparent draws and effects |
| volumetric fog | compute, reads linear depth (R32F 1920x1080) and composites into scene color |
| half-res effects | RGBA8 960x540 with half-res depth |
| post | combine/bloom pyramid in RGBA16F / B10G11R11 |
| tonemap | into RGBA8 1920x1080 (the UI target) |
| game AA | ping-pong RGBA8 1920x1080 after tonemap (to be skipped when upscaling) |
| UI | stencil-masked draws over the same RGBA8 target |

There is no velocity buffer, also with the camera moving: motion vectors are computed.

## Scene constants (864 bytes, bound by most passes)

Signature: `[0]=3000 (far) [1]=1/3000 [4]=1920 [5]=1080 [6]=1/1920 [7]=1/1080`.

| Floats | Meaning |
|---|---|
| 8–19 | view matrix, 3x4 rows (rotation + translation); the only block that changes with the camera |
| 36–51 | inverse projection (0.700285 = 1/1.42799, 0.39391 = 1/2.53865) |
| 52, 57, 62, 63 | projection (rows 52–67), D3D depth 0..1: x 1.42799, y 2.53865, z 1.00002 / -0.0500679 (near 0.05, far 3000) |
| 112–175 | shadow cascade matrices |
| 180–191 | inverse view (camera to world), 3x4 (176–179: 2, 8, 15, 0) |

The previous frame's matrices are not there; the port keeps them itself.

The dated notes that were here (TAA's first version, the live and startup resolution paths of
October 2026, the FSR 3.1, FSR 4 and FSR 4.1.1 work that beta 27 replaced with AMD's FSR SDK) are in
[archive/upscaler-2026-10.md](archive/upscaler-2026-10.md).

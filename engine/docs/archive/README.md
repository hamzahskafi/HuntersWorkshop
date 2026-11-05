# Archive: documents kept for the record

Nothing here describes the engine as it is. How it works now: [engine-map.md](../engine-map.md);
every change and measurement since Hunter's Workshop began: [changelog.md](../changelog.md); what
comes next: [plan.md](../plan.md). Links inside these files may point at files
one folder up (`../`). (Called `docs/history/` until 0.4.4 beta 28.)

## From bbport

These files are from bbport's development (deadinside28's native Linux port of Bloodborne, which
DarkIzuku's fork, Lantern Engine and then Hunter's Workshop are built on). They came into this
repository with the engine's source in Lantern 0.9.37 beta 7 (`c494fca`, 2026-10-22). Most are in
Russian, and all describe bbport on Linux (an RX 7800 XT with RADV, unless they say otherwise), not
Hunter's Workshop as it is. They are kept as written; links inside them may point at files beside
them that are one folder up (`../`).

Oldest first:

- [DEVELOPMENT_LOG.ru.md](DEVELOPMENT_LOG.ru.md): the log of the first native launch experiment (the
  game's code run directly, FPS patches, the runtime's memory, imports up to `sceAjmInitialize`),
  2026-09-25 and before; superseded by the port itself.
- [object_motion_notes_2026-09-26.md](object_motion_notes_2026-09-26.md): notes on the first build of
  per-object motion vectors, before it was tried in the game, 2026-09-26; superseded by
  [motion_vectors.md](../motion_vectors.md).
- [ROADMAP_2026-09.md](ROADMAP_2026-09.md) (English): what was done and what came next, as of
  2026-09-26 with an update of 2026-09-30; superseded, by its own note, by
  [EMULATION_REMOVAL_PLAN.md](EMULATION_REMOVAL_PLAN.md) and [COMPATIBILITY.md](../COMPATIBILITY.md).
- [CHANGELOG_2026-09-27.md](CHANGELOG_2026-09-27.md): the day's changes (the reactive mask setting
  saved, upscaler presets switched without a restart on cards that cannot blit D32S8, `jitter=1`),
  2026-09-27; the upscaler's later state: [upscaler.md](../upscaler.md).
- [FIXES_2026-09-27.md](FIXES_2026-09-27.md): the same day's analysis of three reported problems (the
  reactive mask, trails on weapons, settings that needed a restart), 2026-09-27; superseded by
  [motion_vectors.md](../motion_vectors.md) and [upscaler.md](../upscaler.md).
- [CURRENT_STATUS_2026-09-29.md](CURRENT_STATUS_2026-09-29.md): a snapshot of the port and its
  upscaler (what is in the code, what ran, what was checked on screen), 2026-09-29; superseded by
  later work, today [engine-map.md](../engine-map.md).
- [SESSION_2026-09-30.md](SESSION_2026-09-30.md): the report of the 2026-09-30 to 10-01 session (the
  two-stage GPU command pipeline, the upscaler, a crash fixed, the launcher, AppImage; commits
  `37c8eac` to `9d0cf7f`); its English details are in [parallel_gpu.md](../parallel_gpu.md).
- [DEPTH_ADAPTIVE_TAA_2026-10-01.md](DEPTH_ADAPTIVE_TAA_2026-10-01.md) (English): an experiment with
  depth-dependent TAA thresholds, 2026-10-01; replaced, by its own note, by a fix of the temporal
  reprojection ([TEMPORAL_REVIEW_2026-10-01.md](TEMPORAL_REVIEW_2026-10-01.md)).
- [TEMPORAL_REVIEW_2026-10-01.md](TEMPORAL_REVIEW_2026-10-01.md): a review of TAA and the temporal
  upscalers' inputs against a video, with causes and checks, 2026-10-01; later state:
  [upscaler.md](../upscaler.md), [motion_vectors.md](../motion_vectors.md).
- [CHANGES_2026-10-02.md](CHANGES_2026-10-02.md): unpublished changes of 2026-10-02 (the startup
  resolution patch the default again for outputs other than 1080p, uneven frame times on NVIDIA,
  the shader cache, CPU, a crash, the picture, mods, memory, test tools); the resolution item is in
  [upscaler.md](../upscaler.md).
- [CHANGES_2026-10-03.md](CHANGES_2026-10-03.md): unpublished changes of 2026-10-03 (live resolution
  changes off by default, the Steam Deck's start, thin far objects flickering under TAA, CPU
  measurements).
- [CHANGES_2026-10-06.md](CHANGES_2026-10-06.md): bbport 0.3, 2026-10-04 to 10-06 (its PC memory
  model, through dma-buf on Linux, experimental and off; VRAM no longer growing from area to area; a
  GPU hang located; launcher, patches, FSR 4.1.1 assets).
- [CHANGES_0.4.md](CHANGES_0.4.md): bbport 0.4, from 2026-10-06 (sections to 2026-10-07): its PC
  memory model the default again and for NVIDIA, the game's libGnm as the API level, weak CPUs,
  depth of field, the shader cache, motion vectors, FSR 4.1.1; the libGnm work continues in
  [EMULATION_REMOVAL_PLAN.md](EMULATION_REMOVAL_PLAN.md), the memory model in Hunter's Workshop
  in [gpu-memory.md](../gpu-memory.md).
- [bbport-README.md](bbport-README.md) (English): bbport's README (deadinside28), undated, from after
  bbport 0.3 (its new memory model still an option for AMD GPUs only); Hunter's Workshop's own:
  [README.md](../../README.md).

## From Lantern Engine and Hunter's Workshop

- [linux-radv-investigation.md](linux-radv-investigation.md) (English): one Linux/RADV report
  investigated in Lantern 0.9.37 beta 9 (online play's start on Linux, the indirect dispatch cap);
  both fixes are in the engine.
- [WINDOWS_0.4_INTEGRATION.md](WINDOWS_0.4_INTEGRATION.md) (English): merging bbport 0.4 into a
  Windows build and the bbhost ports after it, before Hunter's Workshop began; moved 2026-10-31.
- [online.md](online.md) (English): setting up a custom shadNet server in the launcher this project
  started from; Hunter's Workshop plays online through a Hunter's Dream account.
- [memory_model_fps_sink.md](memory_model_fps_sink.md) (English): the PC memory model's FPS sinking
  over a session and its fix, 2026-10-22; the model was removed in 0.4.4 beta 28.
- [dlss_frame_generation.md](dlss_frame_generation.md) (English): DLSS Frame Generation through
  NVIDIA Streamline and the upscaler options, 2026-10-22; not offered from 0.4.4 beta 26, removed in
  beta 28.
- [upscaler-2026-10.md](upscaler-2026-10.md) (English): upscaler.md's dated notes of October 2026
  (TAA's first version, the live and startup resolution paths, FireBurn's FSR 3.1, FSR 4 and FSR
  4.1.1); moved in 0.4.4 beta 28.
- [launcher-layout-proposal.md](launcher-layout-proposal.md) (English): the launcher's reorganisation
  as proposed and built in 0.4.4 betas 26 to 27d.
- [a1-a12-integration-0.4.0.md](a1-a12-integration-0.4.0.md): how the first batch of community fixes
  (A1–A12) went into 0.4.0 beta 1, 2026-10-25; the fixes themselves are in
  [community-fixes.md](../community-fixes.md). Moved out of the plan on 2026-11-05.
- [hitches-2026-10.md](hitches-2026-10.md): the 40–120 ms hitches investigated with hitch reports, the PC
  memory model measured on and off, and the smoothness leads, 2026-10-25 and 26; the smoothness work
  after 0.4.4 is in [performance.md](../performance.md). Moved out of the plan on 2026-11-05.
- [plan-0.4.4.md](plan-0.4.4.md): the plan to 0.4.4 stable, beta by beta (23 to 28), with the decisions
  as they stood and the questions settled, 2026-10-31 to 11-03; the decisions that stand are in
  [plan.md](../plan.md). Moved out of the plan on 2026-11-05.

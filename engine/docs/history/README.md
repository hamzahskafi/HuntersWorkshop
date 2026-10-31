# History: documents kept for the record

These files are from bbport's development (deadinside28's native Linux port of Bloodborne, which
DarkIzuku's fork, Lantern Engine and then Hunter's Workshop are built on). They came into this
repository with the engine's source in Lantern 0.9.37 beta 7 (`c494fca`, 2026-10-22). Most are in
Russian, and all describe bbport on Linux (an RX 7800 XT with RADV, unless they say otherwise), not
Hunter's Workshop as it is. They are kept as written; links inside them may point at files beside
them that are one folder up (`../`). How the engine works now: [engine-map.md](../engine-map.md);
every change and measurement since Hunter's Workshop began:
[community-fixes-plan.md](../community-fixes-plan.md).

Oldest first:

- [DEVELOPMENT_LOG.ru.md](DEVELOPMENT_LOG.ru.md): the log of the first native launch experiment (the
  game's code run directly, FPS patches, the runtime's memory, imports up to `sceAjmInitialize`),
  2026-09-25 and before; superseded by the port itself.
- [object_motion_notes_2026-09-26.md](object_motion_notes_2026-09-26.md): notes on the first build of
  per-object motion vectors, before it was tried in the game, 2026-09-26; superseded by
  [motion_vectors.md](../motion_vectors.md).
- [ROADMAP_2026-09.md](ROADMAP_2026-09.md) (English): what was done and what came next, as of
  2026-09-26 with an update of 2026-09-30; superseded, by its own note, by
  [EMULATION_REMOVAL_PLAN.md](../EMULATION_REMOVAL_PLAN.md) and [COMPATIBILITY.md](../COMPATIBILITY.md).
- [CHANGELOG_2026-09-27.md](CHANGELOG_2026-09-27.md): the day's changes (the reactive mask setting
  saved, upscaler presets switched without a restart on cards that cannot blit D32S8, `jitter=1`),
  2026-09-27; the upscaler's later state: [upscaler.md](../upscaler.md).
- [FIXES_2026-09-27.md](FIXES_2026-09-27.md): the same day's analysis of three reported problems (the
  reactive mask, trails on weapons, settings that needed a restart), 2026-09-27; superseded by
  [motion_vectors.md](../motion_vectors.md) and [upscaler.md](../upscaler.md).
- [FIXES_SUMMARY_2026-09-27.md](FIXES_SUMMARY_2026-09-27.md): a short summary of the same fixes with
  what to test, 2026-09-27; as above.
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
  [EMULATION_REMOVAL_PLAN.md](../EMULATION_REMOVAL_PLAN.md), the memory model in Hunter's Workshop
  in [gpu-memory.md](../gpu-memory.md).
- [bbport-README.md](bbport-README.md) (English): bbport's README (deadinside28), undated, from after
  bbport 0.3 (its new memory model still an option for AMD GPUs only); Hunter's Workshop's own:
  [README.md](../../README.md).

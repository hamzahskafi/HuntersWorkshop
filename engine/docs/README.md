# Engine documents

What each document here is for. Checked against the code at 0.4.4 (2026-11-03); a document written
for an older version says so at its top. Finished plans, closed investigations and removed
features are in [archive/](archive/README.md) (was `history/`).

## For players and testers

- [release-notes-0.4.4.md](release-notes-0.4.4.md), [0.4.3](release-notes-0.4.3.md),
  [0.4.2](release-notes-0.4.2.md), [0.4.0](release-notes-0.4.0.md): what changed in each release.
- [MODS.md](MODS.md): file mods, where they go and how the engine merges them.
- [COMPATIBILITY.md](COMPATIBILITY.md): graphics cards and drivers, and what the engine does on each.
- [hdr.md](hdr.md): HDR output and its settings.

## The plan, the log and the map

- [plan.md](plan.md): what comes next (the performance work after 0.4.4, then the rest), the ground
  rules, the decisions that stand, the checks still to make and what waits on the tester.
- [performance.md](performance.md): what the 0.4.4 logs show about smoothness and frame rate: the
  VRAM cleanup's long frames, where the GPU's frame goes, and what each costly item is.
- [changelog.md](changelog.md): every change since Hunter's Workshop began, with its measurements and
  credits, newest at the bottom.
- [community-fixes.md](community-fixes.md): what the other Bloodborne PC projects fixed, vetted against
  Hunter's Workshop, and the reviews of bbport 0.7, Lantern 0.9.38 and 0.9.39, and bbhost.
- [engine-map.md](engine-map.md): how the engine works (threads, a frame from the game's commands to
  the screen, where the GPU and CPU time goes) and the reviewed optimization ideas, with the status of
  each after 0.4.4 beta 21 at its top; the open ones are ordered in plan.md.
- [launcher-text-review.md](launcher-text-review.md): every launcher row's title, line and tooltip, with
  the texts proposed for 0.4.4's text pass (applied).

## How parts of the engine work

- [parallel_gpu.md](parallel_gpu.md): the GPU command processing threads (the two-stage draw
  pipeline, recording threads, copy threads) and how the game's fences are written.
- [parallel-stageb.md](parallel-stageb.md): the draw recording thread and its lookahead thread.
- [pipeline_compiles.md](pipeline_compiles.md): shader and pipeline compiles, the cache and the waits
  behind them.
- [native_shader_compiler.md](native_shader_compiler.md): the shader translator's native paths
  (constant buffers, resource tables).
- [motion_vectors.md](motion_vectors.md): motion vectors for the upscalers (camera and object motion).
- [upscaler.md](upscaler.md): the upscalers (TAA, DLSS, AMD FSR through AMD's FSR SDK), where they run
  and their inputs.
- [hdr-scene-path.md](hdr-scene-path.md): the plan for HDR from the scene (the next HDR mode).

## Frame rate

- [frame-rate-survey.md](frame-rate-survey.md): Kyo's 60 FPS++ entry by entry, and what the engine's
  frame-rate corrections do with each.
- [high-fps-audit.md](high-fps-audit.md): what in the game still assumed 30 FPS, and what was done.

## GPU memory

- [gpu-memory.md](gpu-memory.md): why frames were late while memory was moved, and what 0.4.4 did
  about it (the GX region read in place, fences written when the GPU is done).

# Hunter's Workshop 0.4.4

What changed since 0.4.3, for the release page. The engine work behind each line is in
`docs/community-fixes-plan.md` and `docs/gpu-memory.md`.

## Defaults

- **The project's tested settings are the defaults:** depth of field on, the intro skipped,
  sharpness 0.20, the FPS counter off, picture tuning off, and the unified frame time on (More ›
  Engine). The follow camera stays Original and model detail As in the game.
  Settings files from earlier versions get the unified frame time turned on once; everything else in
  them stays as it was.

## Performance

- **The game's per-frame GPU data is read where the game writes it.** The memory the game rewrites
  for the GPU every frame (per-draw parameters, CPU-animated vertices, its upload area; 288 MiB) is
  used by the GPU in place, as a PC game's upload heap: no write protection, write faults, uploads
  or VRAM copy for it. The engine takes it over once, when the game makes it at start, and the GPU
  reads it through a buffer of its own, within the Vulkan spec on every graphics card. Small
  read-only pieces of it are still copied when used. The game is told the GPU has finished its
  work only once it has, so it never reuses memory the GPU has yet to read. In play (RTX 3070 Ti):
  write faults went from about 1,400 a second at 120 FPS to almost none, and the time spent re-protecting memory from
  about 0.4 ms a frame to almost none. Frame rate and VRAM are as in 0.4.3, also where the GPU is
  the limit (the busiest spot tested, 120 FPS: 109 FPS in both). Experimental engine
  features › Per-frame GPU data in place turns it off (`BB_GX_IN_PLACE=0`), should something look
  wrong.
- **The GPU gets its work as it reaches the end of what it has, with no timers.** The game's
  graphics and compute work goes to the GPU at the end of each of the game's submissions when the
  GPU is on the last of what it was given, or, when the game has nothing more to send, the moment
  the GPU gets there. No submission goes out empty, and a frame's end and its completion signal
  share one. Where the GPU is the limit (RTX
  3070 Ti, 120 FPS) it never waits for work the engine has ready: 3 submissions a frame, and about
  0.1 ms a frame idle between them, the switches themselves.
- **Game-data VRAM kept rather than freed and allocated again.** With room in VRAM, emptied memory
  for the game's data is kept 30 seconds instead of 5, and the last 64 MiB emptied stays.

## Fixes

- **Uncapped frame rate:** the vblank is signalled on time even when frames keep coming (with
  precise pacing off).

## For testers

- Long-frame reports (frame statistics on) now give the long frame's own figures, with how many
  images were made or had their data checked.
- The frame capture lists every draw of each pass.
- Frame statistics count how the work fences wait for went to the GPU (`Fence submissions:`): at
  the game's submission ends, when the GPU got to its last submission, by a safety net that should
  never be needed (expected 0), the share of frames retired with the frame end, and the empty
  submissions not made. The GX region's bindings across its edges are counted per edge.
- The log and the GPU profile name the upscaler that runs (they said FSR for DLSS and TAA as
  well).
- Settings › Log › GPU time in the log (`BB_GPU_PROFILE`): the GPU's time a frame by render pass,
  dispatch and upscaler, every 5 seconds.
- Settings › Log › Render stage timings stays on until it is turned off, like the other log
  options (it used to turn itself off after one game).
- Windows logs give VRAM once a minute: the game's use against the driver's budget, and the
  engine's part of it.
- The log names the game's per-frame GPU memory (its GX range allocator) as the game makes it,
  says when it is in place, marks write faults inside it, and counts how the game's bindings of it
  are served (with frame statistics).

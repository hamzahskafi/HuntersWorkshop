# Hunter's Workshop 0.4.4

What changed since 0.4.3, for the release page. The engine work behind each line is in
`docs/community-fixes-plan.md` and `docs/gpu-memory.md`. (In progress: beta 12. Lines marked
*to be measured* wait for a test log before the release says what they bring.)

## Defaults

- **The project's tested settings are the defaults:** depth of field on, the intro skipped,
  sharpness 0.20, the FPS counter off, picture tuning off, and the unified frame time on (More ›
  Engine). The follow camera stays Original and model detail As in the game.
  Settings files from earlier versions get the unified frame time turned on once; everything else in
  them stays as it was.

## Performance

- **The game's per-frame GPU data read where the game writes it.** The memory the game rewrites for
  the GPU every frame (per-draw parameters, CPU-animated vertices, its upload area; 288 MiB) is now
  used by the GPU in place, as a PC game's upload heap: no write protection, write faults, uploads
  or VRAM copy for it. The game makes it once at start; the engine takes it from there, once, and
  never moves it. Small read-only pieces of it are still copied when used. As with the PC memory
  model, the game is told the GPU has finished a frame's work only once it has, so it cannot reuse
  memory the GPU has yet to read. The GPU reaches that memory through a buffer of its own, within
  the Vulkan spec on every graphics card (the betas before used a binding NVIDIA's driver only
  tolerates), and that buffer starts where the region does: the bindings at its start are no longer
  copied every frame. In play (RTX 3070 Ti, 60 FPS, the same route with it on and off): write faults
  went from about 900 a second to about 1, the time spent re-protecting memory from about 0.4 ms a
  frame to almost none, and the GPU's time from 7.9 to 7.0 ms a frame (its uploads of the region are
  gone; reading it over PCIe costs less than they did). Frame rate, pacing and VRAM are as before.
  Experimental engine features › Per-frame GPU data in place turns it off (`BB_GX_IN_PLACE=0`),
  should something look wrong.
- **The GPU gets its work by its progress, without timers.** With the per-frame data in place, the
  work the game's fences wait for goes to the GPU at the end of each of the game's submissions
  when the GPU is on the last of what it was given, or when the GPU gets there: no timer decides
  it, and the game's compute work is sent the same way (it used to wait for the next graphics
  work or a short timer). No submission goes out without work in it, and a frame's end and its
  completion signal share one. In play (RTX 3070 Ti): where the GPU is the limit, 3 submissions a
  frame instead of 4 to 6, the GPU idle between them about 0.1 ms a frame; at the 120 FPS cap 8 to
  16 instead of 10 to 25, at 60 FPS 8 to 20 instead of 14 to 32. Frame rate and pacing as before.
- **Game-data memory** (*to be measured*). Memory for the game's data in VRAM is kept longer when it
  empties while VRAM has room. In beta 1 a tester saw about 1 GB less VRAM in dense areas, and
  smoother play there, than with 0.4.3; which change did it is not known yet.

## Fixes

- **Uncapped frame rate:** the vblank is signalled on time even when frames keep coming (with
  precise pacing off).

## For testers

- Long-frame reports (frame statistics on) now give the long frame's own figures, with how many
  images were made or had their data checked.
- The frame capture lists every draw of each pass.
- Frame statistics count how the work fences wait for went to the GPU (`Fence submissions:`): at
  the game's submission ends, when the GPU got to its last submission, and by a safety net that
  should never be needed (expected 0). The GX region's bindings across its edges are counted per
  edge.
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

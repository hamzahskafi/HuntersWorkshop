# Hunter's Workshop 0.4.4

What changed since 0.4.3, for the release page. The engine work behind each line is in
`docs/community-fixes-plan.md` and `docs/gpu-memory.md`. (In progress: beta 7. Lines marked
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
  or VRAM copy for it. The game makes it once at start; the engine takes it from there and binds it
  once, never moving it. Small read-only pieces of it are still copied when used. As with the PC
  memory model, the game is told the GPU has finished a frame's work only once it has, so it cannot
  reuse memory the GPU has yet to read. In play (RTX 3070 Ti, 60 FPS, the same route with it on
  and off): write faults went from about 900 a second to about 1, the time spent re-protecting
  memory from about 0.4 ms a frame to almost none, and the GPU's time from 7.9 to 7.0 ms a frame
  (its uploads of the region are gone; reading it over PCIe costs less than they did). Frame rate,
  pacing and VRAM are as before. More › Engine › Performance › Per-frame GPU data in place turns it
  off (`BB_GX_IN_PLACE=0`), should something look wrong.
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
- Settings › Log › GPU time in the log (`BB_GPU_PROFILE`): the GPU's time a frame by render pass,
  dispatch and upscaler, every 5 seconds.
- Windows logs give VRAM once a minute: the game's use against the driver's budget, and the
  engine's part of it.
- The log names the game's per-frame GPU memory (its GX range allocator) as the game makes it,
  says when it is in place, marks write faults inside it, and counts how the game's bindings of it
  are served (with frame statistics).

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

- **Where the graphics card is the limit, about a third more frames.** At the busiest spot tested
  (the dense area facing its lamps, about 1,770 draws a frame; RTX 3070 Ti, DLAA, 120 FPS preset),
  0.4.3 ran about 91 FPS (10.9 ms frames), its slowest frames about 80, and 0.4.4 holds 120, the
  preset's cap, with the GPU busy about 8.1 ms of each 8.33 ms frame. Frame times are steadier too:
  0.5 ms of spread around the average in 0.4.3, 0.03 ms now, and the slowest 1% of frames at
  8.5 ms instead of 12.5. Where 0.4.3 ran 106 to 108 FPS (about 1,300 draws), 0.4.4 holds 120. The
  lines below are where it came from.
- **The game's per-frame GPU data is read where the game writes it.** The memory the game rewrites
  for the GPU every frame (per-draw parameters, CPU-animated vertices, its upload area; 288 MiB) is
  used by the GPU in place, as a PC game's upload heap: no write protection, write faults, uploads
  or VRAM copy for it. The engine takes it over once, when the game makes it at start, and the GPU
  reads it through a buffer of its own, within the Vulkan spec on every graphics card. Small
  read-only pieces of it are still copied when used. The game is told the GPU has finished its
  work only once it has, so it never reuses memory the GPU has yet to read. In play (RTX 3070 Ti):
  write faults went from about 1,400 a second at 120 FPS to almost none, and the time spent re-protecting memory from
  about 0.4 ms a frame to almost none. That freed the CPU; on its own the GPU's frame rate stayed
  as in 0.4.3 (109 FPS at a dense spot). Experimental engine features › Per-frame GPU data in place
  turns it off (`BB_GX_IN_PLACE=0`), should something look wrong.
- **The GPU gets its work as it reaches the end of what it has, with no timers.** The game's
  graphics and compute work goes to the GPU at the end of each of the game's submissions when the
  GPU is on the last of what it was given, or, when the game has nothing more to send, the moment
  the GPU gets there. No submission goes out empty, and a frame's end and its completion signal
  share one. Where the GPU is the limit (RTX
  3070 Ti, 120 FPS) it never waits for work the engine has ready: 3 submissions a frame, and about
  0.1 ms a frame idle between them, the switches themselves.
- **The game's copies of its render targets done as image copies.** The game copies some of its
  render targets with a compute shader over their memory; most of those copies went through memory
  (the image written out, then read back in). They are now image copies on the GPU, including the
  bytes past the image and a depth image's stencil (from bbport 0.7, deadinside28). At about 1,750
  draws a frame: 95 to 107 FPS, the GPU's frame 10.35 to about 9.1 ms. With an upscaler below native
  resolution (DLSS and FSR quality modes), copies between the reduced-size targets stay at that size
  (after bbport 0.7).
- **Motion vectors for the upscalers, cheaper for the same vectors.** The per-vertex history is
  written without reading it back, and the motion target holds 8 bytes a pixel instead of 16 (after
  bbport 0.7, deadinside28). Small skeletons the GPU animates (the lanterns hanging from their stands)
  are no longer stored every frame when they did not move. Facing the lamps: 108 to 113 FPS.
- **The game's copy of its depth for the next frame done by a shader**, read where it lies instead
  of through a transfer: 0.36 ms of GPU time a frame to 0.03. Facing the lamps: 113 to 118 FPS. All
  copies between depth and colour images now go this way.
- **The extra sharpening done inside the upscaler's final step** instead of as a pass of its own,
  with the same result: 0.17 ms a frame less at native resolution (DLAA, and DLSS or FSR's native
  modes). This took the lamps to 120.
- **GPU hang markers: one per draw instead of two**, the same hang report; they took 0.10 ms a frame.
- **Game-data VRAM kept rather than freed and allocated again.** With room in VRAM, emptied memory
  for the game's data is kept 30 seconds instead of 5, and the last 64 MiB emptied stays.
- **Less VRAM for the upscaler:** images for features that are off (the reactive mask, FSR 4 with
  another upscaler chosen) are made only when first needed: about 35 MB at 1080p with DLSS.

## Frame rate and V-Sync

- **The frame rates in order, with 240:** 30 (as on PS4), 60, 90, 120, 144, 240 and Uncapped, each
  saying what it runs: every rate above 30 is Kyo's 60 FPS++ with the engine's values for the rate
  (Frame rate corrections). 240 is a fixed rate like 120 and 144; it needs Frame rate corrections
  (on by default) and runs at 144 without them, as in safe mode.
- **Uncapped is uncapped:** it was held to the display's refresh rate (60 FPS on a 60 Hz screen)
  unless the presentation mode was Immediate. Now it runs as fast as the PC can, up to 480 FPS. The
  launcher says that above 240 FPS the game is untested (the old community uncapped patch froze in
  boss fights at such rates) and recommends 240 for a high frame rate.
- **V-Sync beside the frame rate:** the presentation mode, under Screen › Advanced before, is now
  V-Sync in the Frame rate section, with plain names: On (latest frame) (Mailbox, the default), On
  (every frame) (FIFO), Adaptive (FIFO Relaxed) and Off (Immediate). Settings files keep their
  choice.
- **V-Sync follows Uncapped:** choosing Uncapped turns V-Sync Off, and going back to a fixed rate
  turns it On (latest frame), unless another choice was made in between. Nothing is blocked: a
  note under V-Sync says when the pair holds the game to the display (On (every frame) or Adaptive
  with 120 FPS on a 60 Hz screen runs at 60).

## Fixes

- **Uncapped frame rate:** the vblank is signalled on time even when frames keep coming (with
  precise pacing off).
- **144 FPS is a fixed rate like the others:** it runs the console's vblank at 144 Hz, as 120 runs
  120, and renders 144 frames on a slower display as 120 does. Before, it was handled as Uncapped and
  held to the display's refresh rate (60 FPS on a 60 Hz screen). On Linux, 120 and 144 ran at 60.
- **The screen's last row and column keep the game's own alpha** with DLAA and TAA at native
  resolution: the fix for the shimmering line at the right and bottom edges (GitHub issue 43) copied
  the neighbouring pixels whole, alpha included; it now copies their colour only.

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
- GPU time in the log labels the upscaler's own steps (edge clamp, motion vectors, the upscaler,
  the merge). Frame statistics count render passes begun again and what ended them, the copy
  shader's dispatches that interrupt a pass, depth and colour copies by kind, and the GPU command
  thread's work per draw without its idle spin.
- The log says when the frame rate is above the display's and what that costs (`VideoOut:` the
  share of frames drawn and never shown with Mailbox), or that FIFO holds the game to the display.
- `BB_SHADER_DISASM=<hash>` writes a compute shader's game code into the log, decoded, at its first
  dispatch. `BB_BREADCRUMBS=both` puts back the hang marker before each draw.
- Experiments removed: `BB_ASYNC_FENCES`, `BB_FENCE_DELAY_TEST`, `BB_SUBMIT_LOCK=ahead`,
  `BB_SHADOWS` and `BB_HLE_COPY_MERGE` (measured slower, unused, or never safe to turn on).

## Good to know

- **The first start after updating rebuilds part of the GPU cache.** The motion vectors' shaders
  and a few compute shaders changed, so they compile again the first time something draws them:
  some pop-in on first visits, once each.

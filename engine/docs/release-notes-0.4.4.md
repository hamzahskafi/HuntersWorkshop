# Hunter's Workshop 0.4.4

What changed since 0.4.3, for the release page. The engine work behind each line is in
`docs/community-fixes-plan.md` and `docs/gpu-memory.md`. (In progress: beta 2. Lines marked
*to be measured* wait for a test log before the release says what they bring.)

## Defaults

- **The project's tested settings are the defaults:** depth of field on, the intro skipped,
  sharpness 0.20, the FPS counter off, picture tuning off, and the unified frame time on (More ›
  Engine). The follow camera stays Original and model detail As in the game.
  Settings files from earlier versions get the unified frame time turned on once; everything else in
  them stays as it was.

## Performance

- **Less CPU work every frame** (*to be measured*: on from beta 2; beta 1 had it switched off by a
  bug). The memory the game rewrites for the GPU every frame (per-draw parameters, CPU-animated
  vertices, its upload area) is copied where it is used instead of being write-protected and
  watched page by page.
- **Texture and game-data memory** (*to be measured*). A freed large texture's memory is kept a few
  seconds for the next texture of the same size (beta 1: rarely used, 3 of 19,305 new images), and
  memory for the game's data in VRAM is kept longer when it empties while VRAM has room. In beta 1 a
  tester saw about 1 GB less VRAM in dense areas, and smoother play there, than with 0.4.3; which
  change did it is not known yet.

## Fixes

- **Uncapped frame rate:** the vblank is signalled on time even when frames keep coming (with
  precise pacing off).

## For testers

- Long-frame reports (frame statistics on) now give the long frame's own figures, with how many
  images got new memory, reused memory or had their data checked.
- The frame capture lists every draw of each pass.
- Windows logs give VRAM once a minute: the game's use against the driver's budget, and the
  engine's part of it.

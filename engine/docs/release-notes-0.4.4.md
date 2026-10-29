# Hunter's Workshop 0.4.4

What changed since 0.4.3, for the release page. The engine work behind each line is in
`docs/community-fixes-plan.md` and `docs/gpu-memory.md`. (In progress: beta 1.)

## Defaults

- **The project's tested settings are the defaults:** depth of field on, the intro skipped,
  sharpness 0.20, the FPS counter off, and the unified frame time on (More › Engine). The follow
  camera stays Original.
  Settings files from earlier versions get the unified frame time turned on once; everything else in
  them stays as it was.

## Performance

- **Less CPU work every frame.** The memory the game rewrites for the GPU every frame (per-draw
  parameters, CPU-animated vertices, its upload area) is copied where it is used instead of being
  write-protected and watched page by page.
- **Fewer hitches when an area loads.** A freed texture's memory is kept a few seconds for the next
  texture of the same size, instead of being given back to the driver and asked for again; the
  memory for the game's data in VRAM is kept longer when it empties while VRAM has room.

## Fixes

- **Uncapped frame rate:** the vblank is signalled on time even when frames keep coming (with
  precise pacing off).

## For testers

- Long-frame reports (frame statistics on) now give the long frame's own figures, with how many
  images got new memory, reused memory or had their data checked.
- The frame capture lists every draw of each pass.

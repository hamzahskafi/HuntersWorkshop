# Hunter's Workshop 0.4.6 (in beta)

What changed since 0.4.5, for the release page. 0.4.6 beta 1 holds what was tested as 0.4.5 betas 14
to 19 (the VRAM and texture work); 0.4.5 itself shipped from beta 13. The engine work behind each line
is in `docs/changelog.md` (under those 0.4.5 beta names), the findings in `docs/performance.md` and
`docs/gpu-memory.md`, and what comes next in `docs/plan.md`.

## VRAM: one manager, nothing freed while there is room

Textures and the game's data each had their own rules for leaving VRAM, and both freed things while
VRAM was half empty: textures 20 seconds after their last use (on every card up to about 10 GB),
the game's data after 3 minutes. The tester's session with the game's data kept (2026-11-06, 60
FPS, RTX 3070 Ti) had frames over 33 ms 3.4 times an hour against 9.3, and no write faults in play
(performance.md). One manager decides for both (tested as 0.4.5 beta 14):

- **With room in VRAM** (below 80% of what the driver gives the game, which already allows for other
  programs): nothing leaves for its age.
- **From 80%:** what has gone unused longest leaves first, textures and the game's data alike,
  until use is back under 75%. **From 90%** (or after VRAM ran out once) also what was used 10
  seconds ago, and textures the GPU drew.
- **Textures the GPU drew** are copied into the game's memory without the CPU waiting for the GPU,
  which it did for each one before; a game write that meets one while it is copied waits for that
  copy alone.
- **One launcher row in place of two:** Engine switches › Performance › **Use less VRAM** (off)
  starts freeing at 65% instead of 80%, only for games that run out of video memory. VRAM cleanup
  and Use more VRAM for textures are gone, as are their engine switches; the crash report points to
  the new row.
- **The log** says once a minute what was freed for room, and when VRAM gets tight or full.
- For testing: `BB_VRAM_TEST_BUDGET_MB=N` in Extra variables makes a card behave as one with N MiB.

The engine follows the game's own textures (tested as 0.4.5 betas 16 to 19):

- **What the game creates during a loading screen is ready when it ends.** The game makes every
  texture through its graphics layer, with the texture's description and data. The engine now builds
  and uploads those textures while the loading screen is still up, instead of in the first frame
  after it, which is where nearly every stutter after a load came from (40-73 ms frames building
  300-800 textures at once). In play it does the same only while frames are on time, and only while
  VRAM is below the comfortable line.
- **What the game destroys is freed.** When the game is done with a texture, the engine stops
  watching its memory at once (the next texture there no longer faults page by page) and frees it
  once the GPU is done with it, whatever VRAM's level.
- **Three lines** of what the driver gives the game still decide the rest: comfortable 70%, tight
  80%, full 90% (Use less VRAM: 55, 65, 75). Loading screens trim to the comfortable line; while
  tight, idle data goes only when frames have time to spare; when full, at once. The game's data
  goes in whole 64 MiB chunks, so its memory goes back to the driver.
- **The log** says once a minute how many textures were built ahead, how many of them were drawn,
  and how many images were freed when the game destroyed their textures.
- `BB_GX_FOLLOW=0` in Extra variables turns the following off (counted only); `BB_GX_RESOURCES=0`
  stops listening to the game altogether.
- `BB_VRAM_MANAGER=0` in Extra variables (beta 2, for testing on cards with room) turns the VRAM
  manager's own freeing off and leaves only the following: textures built ahead and freed when the
  game destroys them.
- **The game's released data:** the game's data in buffers the game releases leaves VRAM too, when VRAM is above
  the comfortable line (0.4.5 beta 17 freed it all at once, which stalled loading screens); building ahead is
  paced per frame (6 ms in 16 at loading screens, 2 ms in play when frames are on time), so neither
  a loading screen frame nor the game's bursts of new textures in play take long.
- **Only what is the game's alone:** following the game does not touch images other things still use: a texture is
  built ahead only where no image is yet, an image the GPU drew or another texture still covers
  is left alone when the game destroys a texture, and a resource whose memory the game moved is
  not followed. 0.4.5 betas 17 and 18 could show a broken surface or a light streak because of these (fixed in beta 19).
- **The first load of each session no longer translates about 40 shaders** (about 36 ms): draws that
  may get object motion looked at an ordinary version of their shaders first, which the shader cache
  never kept. They now use the motion version they already have.

## Diagnostics

- **The "Building shaders" note** shows when a frame was late while shaders were being
  prepared, for 2 seconds, rather than once 30 ms of that work had added up: it missed the short
  stutters on a first visit and showed for longer work that made no frame late.

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
  manager's own freeing off and leaves only the game's own calls to decide, whatever VRAM's level
  (beta 3): textures built ahead when the game creates them, freed when it destroys them, and its
  data freed when it releases its buffers.
- **Following the game as it goes (beta 4), no time budget:**
  - A texture the game creates with its data gets its image (and the memory for it) inside the
    game's own create call, on the game's loading thread, as the PS4 pays for it there; the game's
    main thread hands that to one helper thread instead, so its frames don't wait. At the next
    submission the GPU thread only registers what was made, protects the memory of textures side
    by side in one go, and records the upload: no hashing of the new texture's data (it is taken
    only if the GPU later draws into it). Beta 3's time slices (6 ms in 16 at loading screens, 2 ms
    in play) are gone.
  - What the game destroys is freed as soon as the GPU is done with it (checked at every
    submission, not once a second), including smaller images inside a destroyed texture, which
    nothing freed before (beta 3's 20-minute test ended with about 2 GB of such stale images).
    A stale image with no game texture over it at all is freed too.
  - Where the game made the same texture again over its live image, that image is refreshed
    instead of the texture waiting for its first draw.
  - With `BB_VRAM_MANAGER=0`, the game's data in released buffers leaves VRAM as soon as the GPU
    has done the last work with it (no 2-second wait, no pacing), unless the memory is in use
    again.
  - The log says which game threads create textures and how long that takes, why a texture was
    left to its first draw, where the GPU thread spent its time registering, how much of VRAM is
    stale with and without a game texture over it, and, in hitch reports, what the texture cache
    did between the last frame and the late one.
- **Following, tightened (beta 3):** the game's destroys are taken before textures are built ahead,
  so a texture the game makes where it just destroyed one replaces it during the same loading
  screen (before, it waited for its first draw); an image of a destroyed texture with nothing drawn
  into it goes once the GPU is done with it, even where the game already made a new texture; and
  memory of released buffers stops being watched in one call per stretch of memory instead of one
  per 64 KiB.
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

## The game's memory, managed as the PS4 does (beta 5)

The tester's call on beta 4's logs: one beta with the whole design and the logging that tests it.

- **The VRAM manager is off by default.** Only the game's own calls decide, as on the PS4.
  `BB_VRAM_MANAGER=1` in Extra variables, or Engine switches › Performance › Use less VRAM, turns it
  back on (rules unchanged); so does safe mode, the run after a crash. `BB_VRAM_MANAGER=0` is no
  longer needed.
- **A released buffer frees nothing.** Beta 4 unbound the game's data as the game released its
  buffers, which made 82-201 ms frames at loading screens. Now the data stays in VRAM, reused in
  place by whatever the game puts there next, and leaves when the game unmaps the memory. Those
  unbinds go in one call per stretch of memory, not one per 64 KiB, and the engine no longer counts
  its own bookkeeping as the game's write faults.
- **Memory belongs to the newest thing in it.** When the game makes a texture or buffer in memory
  that still holds an older image, that image goes once the GPU is done with it. An image the GPU
  drew and was still using is kept.
- **More of the game's calls are followed:** Map (when the game is about to write a resource), the
  render-target and depth views, and 1D textures. A texture made without its data gets its image the
  first time the game writes it; its first draw uploads it.
- **No hashing on any new image's first upload**, not only for textures built ahead.
- **When VRAM is full**, an image goes to system memory instead of stopping the game.
- **The log** (research for the next step):
  - Which programs hold the graphics card's memory when the game's budget moves by 256 MiB or more,
    and once a minute (Windows' counters, as Task Manager shows them).
  - Images draws still make, and stale images, by what the game has at their memory, with the
    largest five described.
  - Maps and views, the time spent binding the game's data in and out of VRAM (also in hitch
    reports), and images in system memory.

## The game's other textures (beta 6)

- **Textures the game makes outside its graphics layer are followed too.** YEBIS, the game's
  post-processing middleware, makes its own render targets, and Scaleform, its UI, makes some of its
  own textures. The engine didn't know them: in beta 5's log about 1.1 GB of video memory was
  post-processing targets drawn minutes earlier and never freed, and two in-play stutters were bursts
  of textures there. Their creation and destruction are now followed like the game's own textures
  (as bbhost found them; checked against 1.09).
- **A drawing the game has written over is freed.** An image the GPU drew is freed once the game's
  memory under it has been rewritten everywhere it covers, which is when its next use would replace
  it anyway. Checking that is capped at 32 MB of memory a second.
- **Memory the game unmaps** no longer counts as holding the textures it held, so their images are
  judged by what is really there.
- **An image of a destroyed texture goes as soon as anything new is made over part of it** (beta 5
  waited until something covered it whole: a 64 MB shadow map stayed).
- **Map is no longer followed:** beta 5 showed the game maps only buffers, every time into a fresh
  copy, never a texture.
- **The log** describes the largest textures draws still make over memory nothing of the game's
  holds, with where that memory is, and counts YEBIS's and Scaleform's textures.

## Diagnostics

- **The "Building shaders" note** shows when a frame was late while shaders were being
  prepared, for 2 seconds, rather than once 30 ms of that work had added up: it missed the short
  stutters on a first visit and showed for longer work that made no frame late.

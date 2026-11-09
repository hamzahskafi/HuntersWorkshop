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

## Fewer images made at the first frame (beta 7)

- **Post-processing rebuilds keep their images.** YEBIS throws away and rebuilds hundreds of its
  render targets at every loading screen (and when a controller connects). Each rebuilt target now
  keeps the image of the one it replaces when it is the same size at the same memory, instead of the
  first frame making and uploading them all again (beta 6's two remaining hitches: 158 and 175 of
  them, 183 and 261 MB uploaded).
- **Textures the game makes empty and fills later** (only sampled, never drawn into) get their image
  when the game creates them, as textures made with their data do; their first draw uploads what the
  game has put there. After some loads the first frames made about 1,000 of these a minute.
- **The log** counts the textures made without their data by what they are bound as, and the
  rebuilt targets that kept their images.

## Post-processing rebuilds without the stall (beta 8)

0.4.6 is the memory model release: the engine follows the game's own calls, not its memory
(`docs/memory-model.md`). Beta 8 is its first step.

- **New render targets start empty.** A target the game makes without data for the GPU to draw
  into is a new Vulkan image. This covers YEBIS's (the post-processing middleware) and the game's
  own targets, depth targets and textures shaders write. Such an image is no longer filled from the
  game's memory, which never holds what the GPU drew there anyway. It starts cleared, as bbhost
  starts its new targets. Only if the GPU wrote that memory as a buffer before is it filled, from
  that. YEBIS rebuilds about 280 targets at loading screens and when you cross from one part of an
  area into the next. In beta 7's log that stalled 59-84 ms, most of it hashing (29 ms), detiling
  (14 ms) and uploading that memory. `BB_EMPTY_SURFACES=0` in Extra variables brings back the fill
  from memory.
- **No hashing for those targets.** The hashes of a target's first fill were there to tell a real
  CPU write from another write in the same 4 KiB page. For a target the game made without data, a
  write in its pages while the game's texture lives is counted and its contents kept. The game's
  calls are what change it.
- **Rebuilt targets reuse dead ones' images.** A new target takes the Vulkan image of a target the
  game destroyed, when it has the same shape and the GPU is done with it, instead of allocating
  another (about 4 ms a rebuild). VRAM no longer holds the old set beside the new one for 3 seconds.
  A target re-made over the same memory still keeps its old image first (beta 7).
  `BB_TARGET_POOL=0` turns this off.
- **Safe mode turns both off.**
- **For the next step (off by default):** `BB_WATCH_GAME_TEXTURES=0` stops write-protecting the
  pages of the game's textures. That covers textures made with their data, targets, and YEBIS's
  targets: the game's calls tell what writes them. Pages stay watched for everything else.
- **The log** has two new lines a minute and one per YEBIS rebuild:
  - **Surfaces:** first uses by how they began, first fills from memory by owner, and CPU writes
    that landed on the game's textures and buffers, with the largest described. Also targets that
    reused a dead one's image, and images left unwatched.
  - **Shaders:** input layouts the game created, and shaders translated for draws by stage, with
    their time.
  - **Each YEBIS rebuild:** where its targets landed, and the destroys before, during and after it.

## The game's own calls tell more (beta 9)

- **The game's render targets are no longer write-protected.** They are written only by the GPU
  (beta 8's sessions: no CPU write on any), so watching their pages for CPU writes only cost time
  at every crossing. Textures the game loads with their data stay watched. `BB_WATCH_GAME_TEXTURES=1`
  in Extra variables watches everything as before; safe mode does too.
- **YEBIS's and Scaleform's texture updates are followed through their own calls** (YEBIS's update
  and unmap, Scaleform's unmap), as bbhost does: a texture they write from the CPU is filled from
  what they wrote, never started empty.
- **The log** counts those calls, names the game code behind the few CPU writes to textures loaded
  with their data, and tells apart the GPU's writes to a new target's memory from before the game
  made it (`BB_TAKE_EARLIER_WRITES=0` leaves those out, for testing).

## Textures follow the game's create calls (beta 10)

- **A texture the game makes with its data tells the images over its memory.** The game copies the
  data in inside its create call. Since beta 9 a render target's pages are not write-protected, so
  that copy no longer reached a target's image left in the same memory, and a draw could show the
  target's old contents there. Every image over a new texture's memory is now marked written when
  the game creates it, watched or not, and takes the new data at its next use. A render target that
  only partly overlaps the new texture and is still in use keeps what the GPU drew (beta 8's rule).
- **Textures the game loads with their data are no longer write-protected either.** Their CPU
  writes are that create call's copy and the game's file loads, both told without page faults.
  `BB_WATCH_GAME_TEXTURES=1` in Extra variables watches everything as before (safe mode does);
  `BB_WATCH_GAME_TEXTURES=targets` is beta 9's choice.
- **Measured before the next step acts:** how often the game makes a texture over one still alive,
  and where the next beta's rule (an image belongs to its texture) would decide differently from
  today's rules, in one log line a minute. Nothing changes in what is drawn from these counts.

## Images belong to the game's textures (beta 11)

- **An image of one of the game's textures now goes with that texture.** When the game destroys the
  texture, or makes something new in its memory, the image is freed once the GPU is done with it,
  and a new render target of the same shape may take it over first. Before, two old rules of thumb
  kept many of them: at each post-processing rebuild after a load, up to about 700 MB of old render
  targets stayed in VRAM until the next rebuild landed on them.
- **A draw the game recorded before it destroyed the texture still gets it,** as on the console,
  where the memory still holds the texture until something new is made there.
- **Any texture the game makes without data takes the image of the same shape it is made over,** as
  YEBIS's did since beta 7: on the console the memory carries over.
- `BB_IMAGE_OWNERS=0` in Extra variables brings back the old rules; safe mode does too.

## The game's buffers follow its calls (beta 12)

- **The game's buffers are no longer write-protected.** The game writes a buffer's memory only when
  it creates it (every later write goes into a fresh copy GX hands out), so the engine now takes the
  creation as the write and stops watching those pages for the CPU. The frame after some loading
  screens spent about 24 ms protecting about 100,000 pages it had just uploaded: that should be gone.
- **Memory the game frees is handed back at once.** When the game destroys a buffer or a texture, its
  pages stop being watched, so the next thing the game creates there no longer stops on a fault per
  page (nine in ten of the write faults left were those).
- **Pages the GPU wrote are still watched,** and a sampling check looks each second for any buffer
  the CPU changed without the game saying so; it fixes the buffer and writes it to the log.
- `BB_WATCH_GAME_BUFFERS=1` in Extra variables watches the game's buffers as before; safe mode does
  too.

## Diagnostics

- **The "Building shaders" note** shows when a frame was late while shaders were being
  prepared, for 2 seconds, rather than once 30 ms of that work had added up: it missed the short
  stutters on a first visit and showed for longer work that made no frame late.

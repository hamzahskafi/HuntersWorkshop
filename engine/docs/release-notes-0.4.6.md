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

## Beta 12's run, followed up (beta 13)

- **Fence labels reach the GPU's copy of their memory.** A label the engine writes for the game (the
  GPU saying it finished some work) went into the game's memory only; if the GPU kept a copy of that
  memory as a buffer, the copy kept the old value. Such a page is now uploaded again before the GPU
  next reads it, and the log counts how often that happens. `BB_LABEL_CHECK=0` in Extra variables
  turns it off.
- **The log says more about loads and write faults:** each `Loading:` line names the maps the game
  opened during it; each write fault is counted by what was watching its page; frames over 25 ms in
  the 2 seconds after a loading screen get the long-frame breakdown that frames over 40 ms always had.

## Online: The Hunter's Dream's update (beta 14)

The Hunter's Dream changed its server on 2026-11-10: co-op, invasions and PvP now need the
connection handshake bbhost 0.2.31 brought in. Without it, an updated player's game gave up on a
Hunter's Workshop player after 30 seconds. This beta speaks it, ported from bbhost 0.2.35:

- **Games greet each other before playing together**, as the console's games did through the PSN
  servers: each side confirms the other is there and answering before the game is told the
  connection is up, and a player who never answers is dropped after 30 seconds as the game expects.
- **The room's links come up from that greeting**, a second after it, for each member on its own;
  a member leaving or dropping ends that member's link alone.
- **Large game messages arrive whole.** The game's busy moments (a big fight, a warp) send
  messages of several KB, and those were cut at 2 KB. They are now received up to the largest size
  the network carries, and sent in pieces that each fit one network frame, so routers that drop
  oversized messages no longer lose them.
- **Character names show to other players** with Player Name Display set to Name: they went out
  blank.
- **The same player is no longer taken for two**, which could leave the game keeping a second
  connection to someone already there.

- **An expired sign-in no longer breaks online play silently.** When The Hunter's Dream no longer
  accepts the sign-in saved on the launcher's Online page (it expired, or this PC was signed out
  elsewhere), the game is told it is signed out: it shows its own "not signed in" message and plays
  offline, instead of going online and being refused all session. The engine checks the sign-in once
  at the start, in the background, without delaying the game. Sign in again on the launcher's Online
  page to play online.
- **News from the server (invites, players joining or leaving) can no longer land in the wrong
  session** when online play stops and starts again within seconds: only one request waits for it
  at a time, and one left over from the old session gives its news back instead of acting on it.
- **Logs no longer show internet addresses in full**, only their first half (`203.0.x.x`), so they
  can be shared safely.
- **One line in the log per session** says how each connection went: how many came up and how fast,
  which timed out and why (the other player never answered, or answered but the network did not let
  the connection finish), and which were closed by whom.

Nothing changes offline or for messages, bloodstains and ghosts. `BB_ONLINE_PAUSED=1` in Extra
variables still keeps the game offline.

## Work moved into the loading screen (beta 15)

The first frames after a load still waited for work the loading screen could have hidden. This beta
moves it there:

- **New shaders build faster entering an area, without taking time from the game.** When shaders
  pile up (a first visit), extra build threads join in. They run only on processor time nothing
  else wants: Windows hands it out and takes it back at once, so the game's own threads always come
  first. On processors with performance and efficiency cores (Intel 12th to 14th generation and
  later) they are kept on the efficiency cores, leaving the fast cores to the game. While a frame
  waits on a shader, the threads building them get full priority until they finish. The log's
  `CPU:` line at the start shows the processor and how many it allows. The cached shaders built at
  the start use up to 16 threads (was 8). Under Linux (Proton) the extra threads stay off: Wine
  passes thread priorities on only where Linux is set up to allow it, and without that they would
  compete with the game. `BB_PIPELINE_BURST=0` turns them off; `BB_PIPELINE_BURST=N` sets how many.
- **The game's data made during a loading screen is uploaded during it**, not at the first frame
  after (up to 287 MB in one frame before). `BB_LOAD_UPLOAD=0` turns this off.
- **Surfaces the first frames make are ready in advance** (it matters most where making them is
  slow: 11-14 ms of a frame on the tester's RTX 3070 Ti, about 2 ms on the RX 9070 XT). The engine learns which
  surfaces each load's first frames make and prepares their memory during the next loading screen,
  up to 256 MiB and only while there is room in VRAM; what is not used is freed 3 seconds after.
  `BB_READY_SURFACES=0` turns this off.
- **Fewer slow memory faults while loading.** The game's create calls now tell the engine before they
  copy their data, so the memory is prepared once for the whole copy instead of faulting page by page
  (about 1,600 faults and 930 reads back from VRAM per 7 minutes of loads). `BB_COPY_NOTICE=0` turns
  this off.
- **One line per load** in the log, 2 seconds after it ends, says what the first frames still did:
  slow frames, uploads, surfaces, shaders and the create calls' copies.

Safe mode turns all four off.

## Textures follow the game's calls (beta 16)

The last of the game's memory still watched by page protection was some of its textures: each write
near one stopped the game's thread for a moment while the engine checked it. Now:

- **YEBIS's post-processing no longer stops the game 8-12 times a second, nor after area loads.**
  For every post-processing pass YEBIS writes a few vertices into a buffer that sits beside its small
  built-in textures (dither, film grain, colour spectrum, fade). And when it fills or changes a
  texture, it writes the data into a scratch block from its own memory pool, which the GPU then
  copies in; the pool's bookkeeping beside those blocks was caught dozens of times in the minute after
  each area load. Because those textures and blocks were watched, every one of those writes was caught
  and checked. Nothing in YEBIS's memory pool is watched now: YEBIS only ever writes memory it has just
  taken from the pool, and taking it tells the engine. `BB_WATCH_YEBIS=1` in Extra variables watches
  them as before.
- **The UI's texture planes** (Scaleform's) are no longer watched either: Scaleform tells the engine
  when it has finished writing one. `BB_WATCH_SCALEFORM=1` watches them as before.
- **The engine's own copies tell the textures they land in.** When the engine carries out one of the
  game's GPU copies or fills by writing memory directly, it now tells the textures it no longer
  watches that their contents changed, as it already told its buffers. Before, such a texture kept
  its old contents until the safety net below noticed, seconds later. `BB_DMA_TELLS_TEXTURES=0` in
  Extra variables turns it off.
- **A safety net:** once a second the engine checks a slice of the textures it no longer watches
  (about 2 MB a second for YEBIS's and the UI's, which takes seconds to go round them, and as much
  again for the game's own). If one changed without the game's calls saying so, and still nothing has
  said so a second later, the engine updates it and notes it in the log. It should find nothing. `BB_TEXTURE_NET=0` turns it off.
- **One line a minute in the log** says which textures are still watched, which write faults remain
  and on what, and what the net found.

Safe mode watches every texture, as before.

## A much smaller Windows download (beta 16)

The engine played the game's movies through MSYS2's complete FFmpeg, which came with about 100
DLLs for video encoders, image formats, network and text libraries the game never uses. It now
has its own FFmpeg 9.1 built in, with only what the movies need (MP4 files with H.264 or HEVC
video and AAC sound): the engine folder drops from 110 DLLs (137 MB) to about 9 (14 MB). The intro
and cutscenes play as before. On Linux, the same build replaces FFmpeg 7.1.1, which had been built
without thread support although the movie player uses it from three threads.

## Shaders: new ones translated ahead (beta 17)

- **New shaders are translated ahead, several at once.** The first time the game needs a shader (a
  new area, an empty cache), the engine translated it on the thread that feeds the graphics card, and
  every frame behind it waited. Now the threads that prepare draws ahead notice it first and have it
  translated on the shader-building threads, side by side with others; the frame waits only for what
  is not ready yet. `BB_TRANSLATE_AHEAD=0` in Extra variables turns it off; safe mode turns it off.
- **A splash screen while the game starts.** From the moment you press Play until the game shows its
  first picture, a small window in the middle of your screen shows the game's own background art
  (read from your game folder) and what is happening: preparing the game, starting the engine,
  building shaders with a count and a bar, and the one-time steps after an update or a graphics card
  change. It works however you start the game: the launcher, the Steam shortcut or Linux.
  `BB_SPLASH=0` in Extra variables turns it off.
- **Shaders built before you play, on any graphics card.** The engine now ships a shader catalog
  (1,416 pipelines of 608 shader programs, from Lantern's community catalogs for AMD and NVIDIA and
  the tester's own caches of a month's play on both): at the first start it is
  prepared for your graphics card and its pipelines are built before the game starts, so places you
  have never visited no longer stutter the first time, whatever card you have. Changing graphics card
  or driver no longer throws your shader cache away: it is prepared again for the new card instead,
  in seconds. `BB_SHADER_CATALOG=0` in Extra variables leaves the catalog out.
- **"Building shaders" in the bottom right corner**, with a small spinner, where most games show such
  a note.
- **The launcher's Online page lists its steps one per line**, in every language.
- **No more of the same few pipelines built again every session.** A handful of cached pipelines (6
  in the tester's cache) were left out at every start and built again in play, in places visited
  just before: a short wait and, for a moment, missing scene geometry. Their vertex shader uses no
  resources, which play already knew to allow; the start's check now allows it too.

## Threads: each with a role, and quicker handoffs (beta 18)

- **Every thread says what it is for, once.** The engine's threads and the game's each get a role:
  the render chain, the game's frame work, work ahead, background work, sound, or left to Windows. One
  place turns a role into what Windows understands: priority, how important the thread's speed is
  (Windows' power throttling, which decides between fast and efficient cores and clocks), and on a CPU
  with two kinds of core, which kind it prefers. Background work (shader backlogs, file read-ahead)
  runs on the efficient cores while you play, and at full speed on any core during a loading screen
  and at the start, when you are waiting for exactly that work.
- **Quicker handoffs between the game's threads.** The game hands work between its threads tens of
  thousands of times a second through semaphores and read/write locks. Each of those used to go
  through one lock shared by all of them, and a woken thread often had to wait again right away. Each
  now has its own state, and a woken thread runs at once.
- **The game's main thread waits more politely for its workers.** When it hands out a job list it
  waited in a tight loop. It now pauses between looks, and after 50 microseconds lets a worker that is
  waiting for a core have this one.
- New log lines with the frame statistics: `Roles:` (each role's threads, CPU time and, on a CPU with
  two kinds of core, where they woke) and `Fork-join waits:`.
- For this beta's measurements only, `BB_THREAD_ROLES=0`, `BB_RUNTIME_SYNC=0` and
  `BB_GAME_FIXES_SKIP=job_barrier` turn each part off, and `BB_TEST_CPUS=8` runs the game as on a CPU
  with 8 hardware threads.

## The GPU command thread, lighter on every draw (beta 19)

The thread that turns the game's commands into graphics card work became the limit after beta 18
at the tested spots. A profile of it showed where its time went, and beta 19 cuts the largest parts:
- **The frame statistics cost less.** They read the clock for about one command in 16 instead of
  every one, so runs with statistics on behave like normal play.
- **Handing a draw to the next thread costs less.** The expensive memory fence that guarantees the
  next thread is woken now comes every 8 draws and whenever this thread stops, not on every draw.
- **Less bookkeeping per draw.** The engine kept a record of every piece of memory a queued draw
  would read or write. It now records only what can actually be overwritten while a draw waits.
- **The game's submissions no longer hold up this thread.** The work the game's submit did while
  holding a lock this thread needs moved outside it.
- **The engine's own threads sleep and wake more cheaply**, on a word of their own, as the game's
  threads do since beta 18.
- The `Roles:` log line now also shows the game's main thread and render thread on their own.
- The shader cache is rebuilt once on the first start of this beta (its identity changed).
- For this beta's measurements only, `BB_STATS_EVERY_PACKET=1`, `BB_PIPE_HANDOVER=each`,
  `BB_PENDING_TABLES=old`, `BB_FRAME_POOL=0` and `BB_ENGINE_PARK=0` each turn one change off.

## The render chain no longer waits on slower threads (beta 20)

A profile of both render threads on beta 19 showed the GPU command thread spending about a sixth of
its time waiting for locks held by threads Windows runs at a lower priority (the draw preparation's
helpers, the game's own submit), which Windows had set aside. Windows' locks don't pass a waiter's
priority on to the thread holding the lock.
- **The GPU command thread takes none of those locks any more.** The draw preparation's data comes
  with each submission, finished work is cleaned up by the helpers themselves, and the game's
  submissions reach this thread without a lock.
- **A lock that passes priority on.** Every lock the render threads share with slower ones now
  notes who holds it. A render thread that finds it held by a slower thread raises that thread to its
  own priority until it lets go. Each such wait is counted, so the `Roles:` log line shows any that are
  left ("waited on lower roles"), on any PC.
- **The threads the render chain waits for are raised while it waits**: the shader compile threads
  and the copy threads, as the backlog compile threads already were.
- **Less work on every draw:** a quicker check of the memory the queued draws write, no memory
  fence on the drawing thread for every draw, no system call to check a thread exists, and no
  memory allocation for each draw.
- On Linux through Wine or Proton the drawing thread keeps its fence on every draw (their process-wide
  fence does nothing), and a waiting thread looks again every 2 ms, so a missed wake-up can never freeze
  the game (engine id 137).
- The shader cache is rebuilt once on the first start of this beta (its identity changed).
- For this beta's measurements only, `BB_PREP_HANDOVER=old`, `BB_SUBMIT_QUEUE=locked`,
  `BB_LOCK_INHERIT=0`, `BB_RAISE_ON_WAIT=0`, `BB_PENDING_FILTER=0`, `BB_STATS_USAGE_EVERY=1` and
  `BB_PIPE_COUNT=fenced` each turn one change off.

## Sharpening: one meaning, off by default (beta 16)

The sharpening slider meant different things depending on the upscaler, so the same value could be
five times stronger under one than another, and on the 1080p picture it barely touched bright
highlights. Now:

- **One sharpening for every upscaler and TAA**: RCAS, on a picture encoded as the eye sees it,
  which is what RCAS is designed for. Highlights are sharpened as well, and kept from flaring. It
  costs no measurable frame rate: it runs inside steps the engine does anyway.
- **The strength is a percentage**: 0% is none, 100% the strongest, and twice the percentage is
  about twice the sharpening. A strength you set before is kept as the same strength.
- **Off by default.** If your settings still had the old default (on, at 0.20), it is switched off
  once; any other choice you made is kept.

The reactive mask's line in the in-game menu now says whether the FSR version running reads the
mask, by FSR's own answer (FSR 4.1.2 does not), instead of "FSR 4 may".

## Diagnostics

- **`bb-shader-lab --check-cache <cache folder>`** (beta 17): prints again when run from a terminal on
  Windows (it was built as a windowed program, so a terminal showed nothing). It says how many cached
  pipelines the start would build, and why it would leave out the rest.

- **AMD FSR's own view** (beta 16): "Show AMD FSR's own view (debug)" in the in-game menu, under Motion
  vectors, replaces the picture with AMD's diagnostic view: how FSR reads the motion vectors, where it
  finds newly uncovered areas and how much it trusts its history. AMD FSR only; turning it on or off
  restarts FSR in the background for a moment; not saved. `BB_AMD_FSR_DEBUG=1` in Extra variables turns
  on AMD's own checks of what the engine gives FSR (any finding in the log).

- **Sharpening for comparison** (beta 16): `BB_SHARPEN_FSR_OWN=1` in Extra variables has FSR sharpen
  with its own sharpening instead, at the same percentage, for side-by-side comparisons.

- **For the sharpening** (beta 16): a line says how much of the 1080p picture is brighter than
  white, and a line says how strong the sharpening is and where it runs, once a change has held for a
  second.

- **The "Building shaders" note** shows when a frame was late while shaders were being
  prepared, for 2 seconds, rather than once 30 ms of that work had added up: it missed the short
  stutters on a first visit and showed for longer work that made no frame late.

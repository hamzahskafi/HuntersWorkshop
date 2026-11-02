# Hunter's Workshop 0.4.4

What changed since 0.4.3, for the release page. The engine work behind each line is in
`docs/community-fixes-plan.md` and `docs/gpu-memory.md`.

## Defaults

- **The project's tested settings are the defaults:** depth of field on, the intro skipped,
  sharpness 0.20, the FPS counter off, picture tuning off, and Frame rate corrections on (Engine
  switches), the unified frame time with them: it is part of the corrections now, no longer a switch
  of its own (it works only with them, and every log since 0.3.0 ran with both). The follow camera
  stays Original and model detail As in the game. A settings file that had the unified frame time off
  gets it back with the corrections; everything else in it stays as it was.
- **A new install starts without an upscaler, on the game's own anti-aliasing** (the tester's call;
  the launcher used to start it on FSR 4 at Ultra Performance, then AMD FSR). With an upscaler chosen,
  **Auto quality** goes by the output's height: Native AA up to 1200 lines (1080p),
  Quality up to 1600 (1440p), Performance above (4K renders 1920 × 1080), so by default the game never
  renders much more than native 1080p, and it follows the output when you change it. **DLSS's Auto
  model** is NVIDIA's pairing: K for DLAA, Quality and Balanced, M for Performance, L for Ultra
  Performance. The first start's Suggested settings for this PC still offers DLSS on an RTX card and
  a lower quality for weaker cards. A settings file that has its own choices keeps them.
- **The shipped settings file matches:** a new install's settings came from a file that still said
  FSR 4.1.1 at Quality after the defaults changed. It now holds the same defaults as the launcher and
  the engine, and two tests keep the three in step.

## The launcher

The Windows launcher's pages, reorganised (the Classic layout; the Modern layout has the same pages
behind its tabs, Visuals holding Display to Interface; the Linux launchers are unchanged for now):

- **The sidebar:** HUNT (Play, Gameplay, Controls, Trophies, Online), VISUALS (Display, Graphics,
  Interface; was SIGHT: Display and Graphics), WORKSHOP (Mods & patches, Cheats) and ENGINE
  (Engine switches, Log), with Settings once, at the bottom.
- **Display:** what the screen gets: the monitor, the window, the output resolution, the frame rate
  and V-Sync, HDR, and picture tuning below it (with a Game's own picture button, as in game).
  **Window mode** (Windowed, Borderless fullscreen, Fullscreen) replaces the two fullscreen
  switches; it keeps the same settings. **HDR** is checked on the monitor the game opens on: its
  switch greys out when that monitor shows no HDR, and says so when Windows' Use HDR is off. The
  **Steam Deck** output is its screen's 1280 × 800 (the list had 1280 × 720 under its name).
- **Graphics:** everything about how the game is drawn, in one place. **Suggested settings for this
  PC** at the top (was Best settings: a rule of thumb from the hardware, not a measured best; it reads
  the monitor the game opens on, not the primary one), then **Upscaling and anti-aliasing:**
  - **Anti-aliasing:** Off, the game's own or TAA, for Upscaler None. With an upscaler it is greyed
    and its box says so ("Upscaler (DLSS)").
  - **Upscaler:** None, AMD FSR or DLSS, with what belongs to it under it:
    - **Upscaling quality** (was Quality preset): Auto (the default; it says what it picks, e.g. "Auto
      (Quality)"), Native AA (DLAA with DLSS), Quality, Balanced, Performance, Ultra Performance,
      Custom.
    - **Render resolution**, beside it: a slider showing the share of the output the game renders at
      and its size, as the engine works it out ("67% — 1280 × 720"; on the Steam Deck the engine never
      goes below 640 × 360, so Ultra Performance shows 50% — 640 × 400). Only Custom moves it; it is
      greyed otherwise.
    - **FSR version** (AMD FSR): Automatic, with the version it runs, and every version AMD offers on
      your graphics card, found by asking AMD's FSR SDK as the game does (in the background, once a
      launcher session). Versions AMD's driver provides are marked.
    - **DLSS model** (DLSS): Auto by default, and what Auto runs at the chosen quality.
  - **Sharpening (RCAS)**, its strength under it. Its line says where it runs for the upscaler
    chosen: after DLSS (which has none of its own), FSR's own, after TAA.
  - **Advanced:** character motion vectors and texture mip bias. DLSS's sharper textures and auto
    exposure are no longer settings: both upscalers get the texture detail and the exposure handling
    NVIDIA and AMD advise.

  The upscaling rows have tooltips with advice (hover a row's text, or the ⓘ in the Modern layout).

  Then **Game effects**, what the game has and a switch leaves out (depth of field, motion blur with
  its switch for upscalers under it, chromatic aberration, ambient occlusion, shadows from dynamic
  lights), and **Enhancements**, what goes past the game or below it for weaker GPUs (texture
  filtering, model detail, screen-space reflections). The in-game menu has the same two sections.
- **Interface:** the in-game menu's shortcut and language, Sharp UI, the shader note and the FPS
  counter.
- **Gameplay:** the camera (Follow camera, Camera tuning) and the extras (Skip intro videos, Free
  camera, Debug menu, and the Hunter's Dream mirror and rebirth at the altar, which were only in the
  game's System menu).
- **Picture tuning, HDR, the FPS counter and Camera tuning** are switches whose settings fold under
  them: turning one off folds them away, turning it on opens them, and the arrow beside the switch
  opens or folds them by hand. The search still finds a row inside and opens its fold.
- **Engine switches** also holds the performance and sound settings that were under Settings, and
  Extra variables; **Reset all** puts back every setting on the page. The Engine preset row went:
  each engine feature has its own switch. **Changed switches** also lists every switch or feature
  that Extra variables sets, at its default or not. **Fixed-refresh frame lock** greys out while
  Precise frame pacing is off (it needs it). **Log** (was Game output) has one Log options fold: the
  log level first, then the diagnostics, among them the new heap corruption checks.
- **Settings** is the launcher's own: its look, the engine folder and how the game starts, files,
  shared settings and updates. The Background rows show in the Modern layout only, where they do
  something.
- A setting's own switch greys out what depends on it (the FPS counter, HDR, picture tuning, the
  camera, trophy notifications and their sound, sharpening strength, peak brightness), rows for a
  choice not made are hidden (the DLSS settings, the HDR UI settings with Expanded), and a setting
  that belongs to another sits under it, indented (sharpening strength, the render scale, the FSR
  version, the DLSS model, peak brightness, the HDR UI settings). A greyed row looks greyed on every
  page (only its switch or box did).
- Every setting is in one place (the in-game menu's language was on two pages), labels match the
  in-game menu's (Character motion vectors, FPS counter, HDR source, Blood Echo multiplier, Camera
  tuning, Render resolution, Anti-aliasing), and every hint names the page as both layouts call it.
  Settings files and shared codes from earlier versions work as before. **Share your settings**
  shares everything except what belongs to this PC (folders, monitor, output resolution, window mode,
  HDR, graphics card, sound device, preferred controller, and fixes or tuning for its hardware), to
  you (online account, languages, profiles, the launcher's look, trophy notifications) or to one
  session (log options, Extra variables); an imported code still sets no command, folder or variable.
- **The game's own anti-aliasing applies only without an upscaler** (Upscaler None, Anti-aliasing
  The game's own). Under AMD FSR, DLSS or TAA it only softened the picture they work from; a
  settings file that has it on keeps it for when the upscaler is None. The game's System menu shows
  its Anti-Aliasing as Off then (it showed On with DLAA), and its help line says why. Its pass also
  no longer runs under DLSS or AMD FSR when it is the choice kept for Upscaler None (119 FPS at the
  lamps against 122 without it, in the tester's report).
- **The launcher and the game no longer overwrite each other's settings.** The game reads its
  settings when it starts and writes its own when you change them in game, so a change made in the
  launcher while the game ran was lost. The launcher now keeps those changes until the game exits
  (the status line says "game settings apply at its next start"), then writes them over what the
  game saved, and its pages show what was changed in game.
- **Save backups while playing every 10 minutes** (was 15), when the saves changed.
- **Frame generation and NVIDIA Reflex are not offered**, and their code went with Streamline's
  (a saved choice loads with no effect). NVIDIA's clock boost stays, on unless `BB_NV_BOOST=0`.
- **The in-game menu (Insert or L3 + R3) matches Graphics:** Anti-aliasing, then the Upscaler with its
  Upscaling quality and locked render resolution slider, the FSR version (every version AMD offers here) or
  DLSS's model under it, Sharpening with its strength (Sharpening strength) under it, and Advanced (character motion
  vectors, texture mip bias). With an upscaler, Anti-aliasing names it. The FSR and DLSS checks
  (auto exposure, the jitter sign, sub-pixel jitter) are no longer in the menu. The DLSS section went:
  its rows are in those places. It shows what runs: "Running: FSR 3.1.6" (marked when AMD's driver
  provides it) and "Running: DLSS 310.9.16, model K". The FPS counter's upscaler item shows the same
  ("FSR 3.1.6", "DLSS K"). The game's own anti-aliasing moved from the game effects to
  Anti-aliasing; FSR 3.1's tuning presets are called Tuning.
- **Texts:** every launcher row shows a short line under its name that wraps instead of being cut,
  the full text when you hover it (or behind the ⓘ in the Modern layout) only where it adds to the line,
  and the setting's default ("Default: …") unless the text says it. 124 lines and 36 tooltips were
  rewritten or corrected (`docs/launcher-text-review.md`), in all 13 languages (machine-made: players'
  corrections are welcome). Every list has Off or None first. The game's System menu help lines fit
  the window (Output resolution's was cut off).
- **Every language complete:** 199 older texts that showed in English in 12 of the 13 languages
  (since 0.9.37 beta 5, only Simplified Chinese had them) are translated: the setup guide above all,
  and the DLSS model names, the log level and diagnostics rows, hitch reports, shader catalogs and the
  Engine page's older switches (machine-made, like the rest). German addresses the player as "Sie"
  throughout (11 texts used "du").

## AMD FSR

- **FSR is AMD's own now:** AMD's FSR SDK 2.4, with AMD's signed files, picks the version your
  graphics card runs: **FSR 4.1** on Radeon RX 9000, RX 7000 and RX 6000 (and Ryzen AI Max, Ryzen AI
  300/400 and the Z2 handhelds), **FSR 3.1** on other cards (NVIDIA, Intel, older Radeon). One "AMD
  FSR" choice replaces FSR 3, FSR 4 and FSR 4.1.1; settings files that chose any of them get AMD FSR.
- **FSR version:** Automatic (the newest AMD offers on your graphics card) or any version AMD offers
  there: FSR 3.1.6 and FSR 2.3.4 on NVIDIA and Intel cards and older Radeons, FSR 4.1 too on Radeon RX
  6000 and newer. In the launcher (Graphics, under the upscaler) and the in-game menu, where the menu
  also names the version that runs. FSR 4.1 on AMD cards and FSR 2.3.4 have not been tested by the
  project yet (no AMD testers): FSR 3.1 is the way back should FSR 4.1 look wrong.
- **Sharper textures with AMD FSR:** one mip level more texture detail than the render size alone
  gives, as AMD's FSR documentation advises (DLSS had it already, as NVIDIA's guide asks; it was a
  switch, now always). Texture mip bias still adds your own offset on top. AMD FSR's auto exposure
  is always on, as AMD advises (it was an in-game checkbox, on by default).
- When AMD FSR starts or its settings change, it builds its shaders for a few seconds; the game shows
  the picture at full size without it meanwhile.
- **Without AMD's files** (they come with the package, in `engine\amd-fsr`), the game runs TAA
  instead, and says so. DLSS on a card without it runs AMD FSR.
- **Removed:** FireBurn's FSR 3.1 and FSR 4, the built-in FSR 4 model, FSR 4.1.1 (fsr4vk and the
  model built from AMD's DLL, which AMD's licence does not allow), and FSR 3.1 frame generation
  (frame generation is not offered). The FSR 3.1 tuning sliders in game now tune AMD's FSR 3.1.

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
  as in 0.4.3 (109 FPS at a dense spot). Engine switches › Experimental engine features › Per-frame GPU data in place
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

- **The follow camera at every frame rate.** Original was meant to be the game's camera at any frame
  rate, but only its two distance eases followed the frame's length: its other per-frame weights stayed
  the game's 30 FPS values every frame, so at 120 FPS the camera chased, turned and settled up to four
  times as fast as on the PS4 (lock-on's swing, the tilt back after letting go of the stick, the chase
  while running), and above 60 it no longer steered around walls at all (each frame's first step of that
  turn fell under the game's 1 degree cut-off). Every per-frame value in the camera's update is now
  converted each frame from the game's 30 FPS frame: 14 places, found in the game's code, among them the
  chase of the character, the camera point's chase, lock-on's and the auto turn's swing and where lock-on's
  swing eases off, the pitch's return, the fast turn's pitch, the camera settings' blend (a boss arena's
  distance and field of view), the turn around walls, and a pull-in by the character's sideways move each
  frame (which shrank above 30). At 30 FPS every value is the game's to the bit. The FPS++ retune is now
  the game's camera with only what Kyo changed: his retuned values stored as a 30 FPS frame's and converted
  the same way, so they act at every rate as his did at 60 (before, the camera was converted once for the
  chosen rate, about 16% slow at 144 when the game ran near 121). Not exactly his 60 FPS camera: the values
  he only halved for 60 now come from the game, converted (at 60 up to 14.5% quicker than his halving),
  and the settings' blend and the chase rates' change, which he left as the game's, move at the game's
  speed (the blend over 1.5 s instead of 0.75); the aimed-for point keeps his share of each frame's move
  unconverted, as that share is the same at any rate. Without the frame timer (`BB_FRAME_FIXES_SKIP=timer,camera`) the
  camera is as in earlier versions; should the engine fail to place its conversion, it converts the camera
  once for the chosen rate instead, as before.
- **The upscalers start fresh at a camera cut.** DLSS, FSR and TAA build each frame on the frames
  before it; after a cut (a cutscene's next shot) that history showed the last shot for a few frames
  (ghosting). The first frame after the camera turns more than the field of view or moves more than
  15 m now starts over, as NVIDIA's DLSS guide asks (§3.13). Frames after a loading screen (lamp
  travel, death) already started over. The limits sit far above anything play does (the follow
  camera snapping up to 4 m towards the hunter when a wall comes between them included), as the
  guide warns that needless resets flicker; the log names each cut.
- **DLAA: 16 jitter positions instead of 8.** The image is shifted by a fraction of a pixel each
  frame so that DLSS sees a pixel's detail from several positions; at native resolution HW used 8,
  under the 16 NVIDIA's guide asks for as a minimum. DLSS at a lower render size already had more
  (18 to 72). FSR and TAA keep AMD's 8.
- **DLSS: no bias mask.** With the reactive mask on, DLSS was given it as its "bias current colour"
  mask, which NVIDIA's guide says should not be used (310.4.0). DLSS no longer gets it, and the mask
  is not made while DLSS runs; in the in-game menu the option is greyed out under DLSS, as under TAA
  ("DLSS does not use it"). FSR 3.1 and FSR 4 use it as before. The mask is off by default.
- **DLSS presets E and F removed** from the launcher and the in-game menu: NVIDIA deprecated these
  older CNN models (DLSS 310.6.0; F "should not be used"). A saved E or F runs Auto, NVIDIA's model
  for each quality mode (K for DLAA, Quality and Balanced, M for Performance, L for Ultra
  Performance).
- **Live resolution change: Off or On, Off recommended.** The "Auto (by GPU)" choice is gone from the
  launcher and the in-game menu: it turned the live path on for strong GPUs, which then ran the
  game's post-processing at 1080p at every preset, and only helps someone who changes resolution in
  the game. Off (the default) renders everything at the preset's size; a saved Auto runs Off and the
  log says so.
- **Live resolution change Off now holds at a 1920 × 1080 output too.** Started at 1920 × 1080,
  the game took an output resolution changed in its menus at once, as if the setting were On. It now
  applies at the next start, as at every other output (the menus say so); with the setting On it
  applies at once, as before.
- **The PC pages of the game's System menu,** reworked:
  - Named Graphics, Effects, Colour, Camera Tuning and Extras (were PC Graphics, PC Effects,
    PC Colour, PC Camera and PC Enhancements), in that order, then PC Controls and Key Bindings,
    with Extras (the mirror, rebirth and Camera Tuning's switch) last instead of first.
  - Turning an effect on or off (anti-aliasing, ambient occlusion, motion blur, depth of field,
    chromatic aberration) no longer turns Picture Tuning on: those apply whether it is on or not.
    Only the picture settings it applies do (ambient occlusion strength, shadow distance, fog,
    vignette, bloom and the Colour page).
  - The sliders cover what the launcher's do, with the game's own value on the slider (fog and
    bloom went only up to the game's own, contrast ±5%, hue ±20°): a value set in the launcher shows
    at its place instead of at an end. Each help text names the game's own position.
- **Co-op: no freeze when a session ends.** Leaving a room (or the game closing its online session)
  asked the server to take the player out on the game's own thread and waited for the answer, up to
  8 seconds on a slow or unreachable connection, with the game stopped meanwhile. The request now
  goes from the online thread, as every other request to the server does, and still reaches the
  server before anything the game asks for next (found by droogie in bbhost).
- **Co-op: quitting leaves the room.** Quit to Desktop, the window's close button and Apply and
  restart game ended the game without telling The Hunter's Dream's server, which kept the player in
  the room until their heartbeat was 15 seconds old: the other players saw them leave late. The
  game now leaves the room as it closes (at most 2 seconds, while the window is already hidden).
- **Co-op: a connection that drops in its first moments stays dropped.** A connection is reported to
  the game as made and then as active 0.2 and 0.4 seconds after it starts; one that failed in between
  (its player gone, the room over) was reported as live after the game had been told it was gone. Those
  reports now go only to a connection that is still there (found by droogie in bbhost).
- **Saved GPU pipelines checked against their motion setting.** The GPU cache could pair a shader
  that writes motion vectors with a pipeline that has none, or the reverse (each run numbers its shader
  variants in the order it first meets them). Such a pipeline was built at start: invalid Vulkan, a
  crash risk on stricter drivers. These are now left out and compiled as the game draws them (one case
  found by Lantern 0.9.39 beta 26; HW checks every pairing).
- **Uncapped frame rate:** the vblank is signalled on time even when frames keep coming (with
  precise pacing off).
- **144 FPS is a fixed rate like the others:** it runs the console's vblank at 144 Hz, as 120 runs
  120, and renders 144 frames on a slower display as 120 does. Before, it was handled as Uncapped and
  held to the display's refresh rate (60 FPS on a 60 Hz screen). On Linux, 120 and 144 ran at 60.
- **The screen's last row and column keep the game's own alpha** with DLAA and TAA at native
  resolution: the fix for the shimmering line at the right and bottom edges (GitHub issue 43) copied
  the neighbouring pixels whole, alpha included; it now copies their colour only.

- **The System menu opened from the title screen no longer has Quit to Desktop:** it repeated the
  title menu's own Quit Game. It stays in the System menu opened in game.

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
- Log › Log options › GPU time in the log (`BB_GPU_PROFILE`): the GPU's time a frame by render pass,
  dispatch and upscaler, every 5 seconds.
- Log › Log options › Render stage timings stays on until it is turned off, like the other log
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
- The log says the follow camera is converted every frame (`Frame rate fixes: the follow camera's
  weights every frame, 14 sites`), and `camera` among the corrections in `Frame rate fixes: on`.
  `BB_FRAME_FIXES_SKIP=camera` leaves it out (the camera as before).
- The log names each camera cut that starts the upscaler's history over (`Upscaler: camera cut
  (turned ... degrees, moved ... m in one frame, ... m of it along the view ...)`, the first 100). With frame statistics, every
  600 upscaled frames: the history resets by cause and the largest turn and move in one frame of the
  other frames (`Upscaler: history resets in 600 frames: ...`). `BB_CAMERA_CUT_RESET=0` in Extra
  variables: cuts logged and counted, the history kept.
- The log says when leaving a co-op room fails at the server (`np: leave_room for room ... failed
  after ... ms`); the server then drops the player when their connection's heartbeat stops. On
  closing the game in a room: `np: left room ... at exit in ... ms`.
- The renderer's test programs build on Windows (`scene-resolution-test`, `taa-shader-test`,
  `camera-motion-test`), and `upscaler-support-test` and `widescreen-test` run there and match the
  current settings.
- `BB_SHADER_DISASM=<hash>` writes a compute shader's game code into the log, decoded, at its first
  dispatch. `BB_BREADCRUMBS=both` puts back the hang marker before each draw.
- Experiments removed: `BB_ASYNC_FENCES`, `BB_FENCE_DELAY_TEST`, `BB_SUBMIT_LOCK=ahead`,
  `BB_SHADOWS` and `BB_HLE_COPY_MERGE` (measured slower, unused, or never safe to turn on).
- **Beta 28, the cleanup:** the switches settled by measurement went, each keeping the path that was
  the default (`BB_DEFER_PROTECT`, `BB_UFFD`, `BB_TEXTURE_HELPER`, `BB_UBO_EXACT`, `BB_CAMERA_FIX`,
  `BB_CAMERA_INVERSE`, TAA's experiment bits, and 16 Lantern 0.9.x "as before" switches among them),
  with the PC memory model (`BB_GUEST_IN_PLACE`). config.ini decides the settings: their environment
  aliases (`BB_HDR*`, `BB_DLSS_PRESET`, ...) went; `BB_UPSCALER` stays for A/B runs.
  `BB_FRAME_FIXES_SKIP=timer,camera` is the frame timer off (was `BB_FRAME_TIMER=0`) and `=clothstep`
  the cloth's last substep (was `BB_CLOTH_STEP`); `BB_GPU_LOG` follows `BB_LOG_LEVEL`.
- The diagnostics are one list, `BB_DIAG=frame,hitch=40,gpu,sections,load,audio,pad,heap,shaders`
  (the Log options fold passes it; bb-start and run.sh expand it, and a switch set on its own wins).
- Occlusion culling and hot pages default as the launcher always set them, in the engine too, so
  bb-start, run.sh and test runs behave as the launcher.

## Good to know

- **Online play is The Hunter's Dream's.** The shadNet path (a server and password typed in by hand)
  went; such settings now start the game offline. With it went shadPS4's sceNet, NetCtl and NP
  libraries and the shadNet client, which nothing called any more: The Hunter's Dream's own
  functions answer every network import the game has (its import list, and an online run with none
  left unanswered), and offline the engine's own answers serve the game. shadPS4's HTTP and SSL
  stay. Building the engine no longer needs protobuf (with Abseil) or miniupnpc.
- **A Lantern 0.9.x folder updated in place is no longer supported** (`run.bat`, the Python start and
  the 0.9.x settings migrations went): install the Hunter's Workshop package instead.

- **The first start after updating rebuilds part of the GPU cache.** The motion vectors' shaders
  and a few compute shaders changed, so they compile again the first time something draws them:
  some pop-in on first visits, once each. Saved pipelines whose shaders disagree with their motion
  setting are compiled again the same way, once.

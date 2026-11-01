# Hunter's Workshop 0.4.4

What changed since 0.4.3, for the release page. The engine work behind each line is in
`docs/community-fixes-plan.md` and `docs/gpu-memory.md`.

## Defaults

- **The project's tested settings are the defaults:** depth of field on, the intro skipped,
  sharpness 0.20, the FPS counter off, picture tuning off, and the unified frame time on (Engine
  switches). The follow camera stays Original and model detail As in the game.
  Settings files from earlier versions get the unified frame time turned on once; everything else in
  them stays as it was.
- **A new install starts on FSR 3.1 at Native AA**, the engine's own defaults, which run on any
  graphics card (the launcher used to start it on FSR 4 at Ultra Performance). The first start's
  Best settings for this PC still offers DLSS on an RTX card, FSR 4 on an RX 9000 and a quality
  preset for the card. A settings file that has an upscaler and preset keeps them.

## The launcher

The Windows launcher's pages, reorganised (the Classic layout; the Modern layout has the same pages
behind its tabs, Visuals holding Display to Interface; the Linux launchers are unchanged for now):

- **The sidebar:** HUNT (Play, Gameplay, Controls, Trophies, Online), VISUALS (Display, Graphics,
  Look, Interface; was SIGHT: Display and Graphics), WORKSHOP (Mods & patches, Cheats) and ENGINE
  (Engine switches, Log), with Settings once, at the bottom.
- **Display:** what the screen gets: the monitor, the window, the output resolution, the frame rate
  and V-Sync, and HDR. **Window mode** (Windowed, Borderless fullscreen, Fullscreen) replaces the two
  fullscreen switches; it keeps the same settings. **HDR** is checked on the monitor the game opens
  on: its switch greys out when that monitor shows no HDR, and says so when Windows' Use HDR is off.
  The **Steam Deck** output is its screen's 1280 × 800 (the list had 1280 × 720 under its name).
- **Graphics:** Best settings for this PC at the top, then **Upscaling and anti-aliasing** (the
  upscaler; the DLSS model with DLSS only; quality, render scale and sharpening; the game's own
  anti-aliasing; character motion vectors; texture mip bias and DLSS's two finer settings under
  Advanced) and **Detail** (model detail, texture filtering, shadows from dynamic lights, ambient
  occlusion, screen-space reflections): what costs frames, in one place.
- **Look:** the game's lens effects (depth of field, motion blur with its switch for upscalers,
  chromatic aberration) and picture tuning, with a Game's own picture button, as in game.
- **Interface:** the in-game menu's shortcut and language, Sharp UI, the shader note and the FPS
  counter.
- **Gameplay:** the camera (Follow camera, Camera tuning) and the extras (Skip intro videos, Free
  camera, Debug menu, and the Hunter's Dream mirror and rebirth at the altar, which were only in the
  game's System menu).
- **Picture tuning, HDR, the FPS counter and Camera tuning** are switches whose settings fold under
  them: turning one off folds them away, turning it on opens them, and the arrow beside the switch
  opens or folds them by hand. The search still finds a row inside and opens its fold.
- **Engine switches** also holds the performance and sound settings that were under Settings, and
  Extra variables; **Reset all** puts back every setting on the page (it used to skip the engine
  preset and the experimental features). **Log** (was Game output) has the log options.
- **Settings** is the launcher's own: its look, the engine folder and how the game starts, files,
  shared settings and updates. The Background rows show in the Modern layout only, where they do
  something.
- A setting's own switch greys out what depends on it (the FPS counter, HDR, picture tuning, the
  camera, trophy notifications and their sound, sharpening strength, peak brightness), and rows for a
  choice not made are hidden (the DLSS settings, the HDR UI settings with Expanded).
- Every setting is in one place (the in-game menu's language was on two pages), labels match the
  in-game menu's (Character motion vectors, FPS counter, HDR source, Blood Echo multiplier, Camera
  tuning), and every hint names the page as both layouts call it. Nothing about the settings
  themselves changed: settings files and shared codes from earlier versions work as before.
- **The launcher and the game no longer overwrite each other's settings.** The game reads its
  settings when it starts and writes its own when you change them in game, so a change made in the
  launcher while the game ran was lost. The launcher now keeps those changes until the game exits
  (the status line says "game settings apply at its next start"), then writes them over what the
  game saved, and its pages show what was changed in game.
- **Save backups while playing every 10 minutes** (was 15), when the saves changed.
- **Frame generation and NVIDIA Reflex are not offered** (the launcher's rows and the in-game items
  are gone; a saved frame generation choice runs off and the log says so). The code stays.

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
  unconverted, as that share is the same at any rate. Without the frame timer (`BB_FRAME_TIMER=0`) the
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
  `BB_FRAME_FIXES_SKIP=camera` leaves it out (the camera as before). `BB_CAMERA_FIX` applies only
  without the frame timer.
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

## Good to know

- **The first start after updating rebuilds part of the GPU cache.** The motion vectors' shaders
  and a few compute shaders changed, so they compile again the first time something draws them:
  some pop-in on first visits, once each. Saved pipelines whose shaders disagree with their motion
  setting are compiled again the same way, once.

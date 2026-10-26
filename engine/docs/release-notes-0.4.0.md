# Hunter's Workshop 0.4.0

What changed since 0.3.0, for the release page. The engine work behind each line is in
`docs/community-fixes-plan.md`; every fix taken from another project is credited below and in
`README.md`.

## Fixes

- **Laurence's cinematic no longer hangs above 30 FPS.** A failed render job kept its slot; after six
  of them every thread that needed one waited for ever. (deadinside28, bbport 0.6; ayozetr)
- **Rom's lake no longer crashes when you arrive from the Fishing Hamlet.** The game's sound library
  freed a sound still linked to its neighbours. (droogie, bbhost)
- **No more crash or black screen from a blur effect** whose data gave its kernel too few samples.
  (deadinside28, bbport 0.6)
- **A graphics pipeline the driver refuses no longer ends the game** (some AMD and older drivers): its
  draws are skipped instead. (Supermedo; ayozetr)
- **NVIDIA:** draws without a pixel shader no longer take the previous draw's state, and a saved
  geometry-shader pipeline keeps its vertex fetch. Pipelines saved by 0.3.0 that this affects are
  rebuilt once as you play. (deadinside28, bbport 0.6, via ayozetr)
- **A possible NVIDIA GPU hang** from a colour image linked as a depth target's stencil.
  (shadPS4 #5118, ported by artemkaVG)
- **Skipping a movie can no longer crash** in the movie player's frame queue. (ayozetr with emurphy04)
- Smaller fixes: a memory leak of about 600 bytes per nested command buffer, every frame (upstream
  shadPS4; Mrsuss60 via ayozetr; DarkIzuku); a split memory mapping that freed the wrong pages (bmy,
  ported by 0xCydral); a texture write-back read that another writer could overtake (DarkIzuku).

## Smoother play

- **The PC memory model is off by default.** It moved the game's memory in and out of VRAM, which
  stalled the GPU now and then: a late frame every minute or two on NVIDIA. Existing settings are
  switched off once; the launcher's toggle for it stays, for those who want it.
- **The game's job workers wait less:** their lock spins politely and each job queue keeps its own
  list nodes, instead of every worker fighting over one lock and the general memory allocator.
  (droogie, bbhost)
- **A 23 ms main-thread job every ~50 seconds is gone:** the game's play recording is compressed at
  level 1 instead of 9. (droogie, bbhost; via ayozetr)

## Above 30 FPS

- **Grass, flowers and trees sway at the PS4's pace.** At 60 FPS and above the wind ran twice as fast
  and bent half as far, twitching with each frame's length; most visible in the Hunter's Dream.
  (droogie, bbhost)
- **The Bloodletter's transformed L2 gives its frenzy aura again.** Its self-hit missed the hand
  above 30 FPS. (droogie, bbhost)
- **Keyboard: sprinting survives direction changes.** The movement keys now turn the stick
  smoothly instead of jumping 45 degrees, which ended the sprint. (Supermedo, ayozetr; Lantern PR #67
  by ikeeasianboy; measured by cassioKenji)
- **Controllers: no faster camera on diagonals.** A stick that reports past its round gate (some
  DualShock 4s, and DualSense through DS4Windows) is brought back onto it. `pad_round_gate=0` in
  config.ini turns this off. (ayozetr)

## Launcher

- **Settings › Log, capture and extra variables** has three new options:
  - **Log level:** Standard (the default, a much leaner log), Detailed (every save, thread and draw
    count, for something odd) or Debug (everything; a large log and a slower game, only to chase a
    specific bug).
  - **Hitch reports:** a frame slower than the threshold writes every thread's state into the log;
    pick about twice your frame time (40 ms at 60 FPS, 20 ms at 120).
  - **Controller details in the log:** connection, rumble support, disconnects and vibration, for
    controller problems.
- Players coming from a beta get the frame statistics and the sharp UI switched off once, as on any
  stable release; both stay available.

## Good to know

- **The first start after updating rebuilds the GPU cache.** A few hundred pipelines then compile
  again the first time something draws them: some pop-in on first visits, once each.
- The follow camera has two tunings (Graphics › Follow camera): Original, the game's own camera at
  every frame rate (the default), and Kyo's FPS++ retune.

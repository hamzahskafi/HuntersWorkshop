# Hunter's Workshop 0.4.5 (in beta)

What changed since 0.4.4, for the release page. The engine work behind each line is in
`docs/changelog.md`; the findings it rests on are in `docs/performance.md`, and what comes next is in
`docs/plan.md`.

## Fewer GPU steps

The GPU is the limit at the busiest spots, and not because of pixels: the frame has about 240 steps,
and each costs about 5 µs however little it does (performance.md). 0.4.5 takes out steps that do
nothing on the PC or that the PC can do another way. The picture is the same.

**Measured** (RTX 3070 Ti, DLAA, Model detail Highest; 0.4.4 and beta 12 in the same scenes, one
after the other, 2026-11-06): the lamp spot **135.0 to 146.4 FPS** (GPU 7.41 to 6.83 ms a frame,
+8.4%), a second spot **122.3 to 131.0 FPS** (8.17 to 7.63 ms, +7.1%), a third with few lamps
**146.3 to 149.8 FPS** (6.83 to 6.67 ms, +2.4%). Worst frames unchanged (11–15 ms). The gain grows
with the lamps casting shadows; one lamp-lit scene the tester compared went from 140–145 FPS in
0.4.2 to 210.

- **Draws that change nothing are left out** (beta 1). The game draws about 12 a frame with the
  toolkit's full-screen rectangle, no pixel shader, no colour target, depth and stencil off, and only
  the depth unit's two "compress disable" bits set. That is how AMD's drivers expand a depth buffer's
  compression data in place before it is read; the PC has no such data. Here each one was a render
  pass of its own, and it ended the pass before it. A draw is left out only when nothing it could
  touch is bound: no colour or depth target, no shader writing memory, no motion vectors, no
  occlusion query measuring it. About 0.1 ms.
- **Lamp shadow faces drawn where the game copies them** (beta 4). Each lamp's shadow is a cube map
  of six faces, 48 faces a frame at the lamp spot; the game clears each face, draws it, copies it
  whole into a scratch image and blurs it from there back into the face. The engine draws the face
  straight into the scratch image, so the copy has nothing to do and is left out; the blur writes the
  face as before. About 0.24 ms.
  - The engine learns each lamp's scratch image from the game's own copies first (4 of them). Should
    anything else touch a face or its scratch image in between, it copies the face back where the
    game drew it (counted as `faces put back`; none in any test).
  - After 64 faces put back in a session the engine stops redirecting for that session. Its
    switch, `BB_SHADOW_COPY_SKIP`, was removed in beta 13 (settled with the tester).
- **The lamp shadow faces' clears done as each face's drawing begins** (beta 5). Each face was
  cleared as a step of its own; it is now cleared as the first command of the face's render pass,
  with the same value. A face with nothing to draw is cleared where its copy is left out, and a face
  whose pass is smaller than itself (a smaller depth image; beta 6, from the tester's build review;
  not seen in Bloodborne) before its pass. About 0.12–0.17 ms.
- **The copy shader's copies done where their render pass ends** (beta 9). The game's
  copy shader (about 53 dispatches a frame) ended the render pass it came in, about 20 times a
  frame in passes that then began again. Beta 8's trace showed that nothing later in the same pass
  reads what it copies, so its copies now wait for the pass's end and run there, in their order.
  - A draw that does touch them first gets them first, the pass ending there as before. Copies
    reading the game's own memory in place are not held.
  - `BB_COPY_HOLD=0` in Extra variables does them where they come, as before (kept through the
    stable release, for graphics cards the tester has not tried).
  - Beta 9's logs: about 95 copy batches a frame held, 12 fewer passes begun again a frame, no draw
    ever touching them; about 0.05 ms.
- **The copy shader's copies merged** (betas 10–12). Its copies were about 188 GPU commands
  a frame. They now run as one command for each place they are done (a pass's end, or the end of a
  copy-shader dispatch outside a pass), split only where one copy needs another's result: one
  shader that reaches every buffer by its GPU address. `BB_COPY_MERGE=0` in Extra variables does
  them one command each, as before (kept through the stable release, as `BB_COPY_HOLD`).
  - Beta 10 gained nothing: it split its groups at every barrier (48 a frame). Beta 11 puts one
    barrier before each group instead.
  - The tester's three builds in one scene: 6.74 ms a frame without either change, 6.68 with the
    copies held, 6.61 held and merged (about 148 to 151 FPS). Beta 12 lets one dispatch's copies
    share a command (the game's shader runs them all at once): at the lamp spot 57 commands and
    23 barriers a frame (beta 11: 78 and 44), the lamp spot 6.83 ms a frame against 6.98 before
    holding (about 143 to 146 FPS).

## VRAM: one manager, nothing freed while there is room

Textures and the game's data each had their own rules for leaving VRAM, and both freed things while
VRAM was half empty: textures 20 seconds after their last use (on every card up to about 10 GB),
the game's data after 3 minutes. The tester's session with the game's data kept (2026-11-06, 60
FPS, RTX 3070 Ti) had frames over 33 ms 3.4 times an hour against 9.3, and no write faults in play
(performance.md). From beta 14 one manager decides for both:

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

From beta 15 the manager knows where the player is and when work is free:

- **Areas, learned:** the engine sees which area's files the game opens, so it knows the area the
  player is in and, during a loading screen, the one they are going to. Each area gains a score per
  visit that halves every 10 minutes, and the engine learns where the player tends to go next from
  each area. Nothing is hard-coded: the Hunter's Dream rises by itself because the player keeps
  coming back. The current areas, the destination and the likely next areas are protected.
- **Three lines** of what the driver gives the game: comfortable 70%, tight 80%, full 90% (Use less
  VRAM: 55, 65, 75).
- **Freed when it does not show:** at loading screens, down to the comfortable line; in play while
  tight, only when frames are on time at the frame cap, a little each second; when full, at once.
  Textures whose memory the game has written over are freed whenever frames have time to spare.
- **What goes first:** what the game wrote over, then what belongs to the areas the player is least
  likely to return to, and what gives memory back to the driver straight away (the game's data in
  whole 64 MiB chunks, where beta 14's single blocks gave nothing back).
- **It corrects itself:** when freed data keeps being needed again within 5 minutes, it keeps more.
- **The log** says once a minute what VRAM holds by kind, what was freed, what went back to the
  driver and what came back; each loading screen's trim; and the areas visited.
- **Research:** the engine also listens to the game creating and destroying its textures and buffers
  (found with bbhost's addresses) and logs how that lines up with what the texture cache sees. Nothing
  acts on it yet; beta 16 will, with the numbers from the logs. Off with `BB_GX_RESOURCES=0`.

## Diagnostics

- **The GPU profile labels indirect dispatches** (`dispatch indirect cs ...`) and **image clears**
  (`image clear WxH`); their time used to go to the label before them. This is how `a954e79d`'s
  0.21 ms turned out to be the game's particle update after it.
- **The "Building shaders" note** (beta 14) shows when a frame was late while shaders were being
  prepared, for 2 seconds, rather than once 30 ms of that work had added up: it missed the short
  stutters on a first visit and showed for longer work that made no frame late.
- **The frame statistics count** the steps taken out (`GPU steps per second (0.4.5)`): the draws left
  out, the rectangle copies and those left out, the lamp shadow faces drawn into their scratch images,
  their clears, and any faces put back; from beta 9 the copy shader's batches held to their pass's
  end, the times they were done, how many of those early, and those not held; from beta 10 the
  batches merged and their dispatches.

## Tried and not kept

- Beta 1 ran the game's shader `a954e79d` as one step instead of its loop of atomics. It was exact,
  but the time was the particle update after it; beta 2 removed it.
- Betas 1 to 3 logged the empty draws' depth registers, what is recorded around the shadow copies,
  and one frame's shadow steps. Beta 7 removed them once they had answered their questions, with the
  switches `BB_SKIP_EMPTY_DRAWS` and `BB_SHADOW_CLEAR_FOLD` (settled; the tester, 2026-11-05).
- Beta 8 traced one frame's copy-shader dispatches in their render passes (where each came, and
  whether anything in the pass read what it copied). It showed that nothing did, which led to betas
  9–12; beta 13 removed it, with `BB_SHADOW_COPY_SKIP`.

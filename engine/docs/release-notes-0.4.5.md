# Hunter's Workshop 0.4.5 (in beta)

What changed since 0.4.4, for the release page. The engine work behind each line is in
`docs/changelog.md`; the findings it rests on are in `docs/performance.md`, and what comes next is in
`docs/plan.md`.

## Fewer GPU steps

The GPU is the limit at the busiest spots, and not because of pixels: the frame has about 240 steps,
and each costs about 5 µs however little it does (performance.md). 0.4.5 takes out steps that do
nothing on the PC or that the PC can do another way. The picture is the same.

**Measured** (RTX 3070 Ti, DLAA, Model detail Highest, the tester's lamp spot and a second spot,
against 0.4.4): **135.0 to 143.5 FPS** (GPU 7.40 to 6.96 ms a frame) and **122.2 to 130.3 FPS**
(8.18 to 7.67 ms). The gain grows with the lamps casting shadows; one lamp-lit scene the tester
compared went from 140–145 FPS in 0.4.2 to 210.

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
  - `BB_SHADOW_COPY_SKIP=0` in Extra variables draws, clears and copies as the game does. It stays
    until 0.4.5 stable, in case a rarer scene shows a shadow problem.
- **The lamp shadow faces' clears done as each face's drawing begins** (beta 5). Each face was
  cleared as a step of its own; it is now cleared as the first command of the face's render pass,
  with the same value. A face with nothing to draw is cleared where its copy is left out, and a face
  whose pass is smaller than itself (a smaller depth image; beta 6, from the tester's build review;
  not seen in Bloodborne) before its pass. About 0.12–0.17 ms.
- **The copy shader's copies done where their render pass ends** (beta 9, in test). The game's
  copy shader (about 53 dispatches a frame) ended the render pass it came in, about 20 times a
  frame in passes that then began again. Beta 8's trace showed that nothing later in the same pass
  reads what it copies, so its copies now wait for the pass's end and run there, in their order.
  - A draw that does touch them first gets them first, the pass ending there as before. Copies
    reading the game's own memory in place are not held.
  - `BB_COPY_HOLD=0` in Extra variables does them where they come, as before.
  - Beta 9's logs: about 95 copy batches a frame held, 12 fewer passes begun again a frame, no draw
    ever touching them; about 0.05 ms.
- **The copy shader's copies merged** (beta 10, in test). Its copies were about 188 GPU commands
  a frame. They now run as one command for each place they are done (a pass's end, or the end of a
  copy-shader dispatch outside a pass), split only where one copy needs another's result: one
  shader that reaches every buffer by its GPU address. `BB_COPY_MERGE=0` in Extra variables does
  them one command each, as before.
  - Beta 10 gained nothing: it split its groups at every barrier (48 a frame). Beta 11 puts one
    barrier before each group instead.
  - The tester's three builds in one scene: 6.74 ms a frame without either change, 6.68 with the
    copies held, 6.61 held and merged (about 148 to 151 FPS). Beta 12 lets one dispatch's copies
    share a command (the game's shader runs them all at once): about 69 commands a frame to 35.

## Diagnostics

- **The GPU profile labels indirect dispatches** (`dispatch indirect cs ...`) and **image clears**
  (`image clear WxH`); their time used to go to the label before them. This is how `a954e79d`'s
  0.21 ms turned out to be the game's particle update after it.
- **The frame statistics count** the steps taken out (`GPU steps per second (0.4.5)`): the draws left
  out, the rectangle copies and those left out, the lamp shadow faces drawn into their scratch images,
  their clears, and any faces put back; from beta 9 the copy shader's batches held to their pass's
  end, the times they were done, how many of those early, and those not held; from beta 10 the
  batches merged and their dispatches.
- **A trace of the copy shader** (beta 8), with frame statistics on: twice a session (the 600th and
  the 6,000th frame of play), one frame's copy-shader dispatches in their passes: for each pass, how
  many copies came at its start, inside it, and at its end, and of those inside how many could move
  to its start (no earlier draw or dispatch of the pass touching what they copy), with the reason
  for the first that couldn't (`Copy trace 1 ...` to `Copy trace 1: end of the frame`).

## Tried and not kept

- Beta 1 ran the game's shader `a954e79d` as one step instead of its loop of atomics. It was exact,
  but the time was the particle update after it; beta 2 removed it.
- Betas 1 to 3 logged the empty draws' depth registers, what is recorded around the shadow copies,
  and one frame's shadow steps. Beta 7 removed them once they had answered their questions, with the
  switches `BB_SKIP_EMPTY_DRAWS` and `BB_SHADOW_CLEAR_FOLD` (settled; the tester, 2026-11-05).

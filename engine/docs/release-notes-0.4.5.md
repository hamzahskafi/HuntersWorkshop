# Hunter's Workshop 0.4.5 (in beta)

What changed since 0.4.4, for the release page. The engine work behind each line is in
`docs/changelog.md`; the findings it rests on are in `docs/performance.md`, and what comes next is in
`docs/plan.md`.

## Fewer GPU steps

The GPU is the limit at the busiest spots, and not because of pixels: the frame has about 240 steps
and each costs about 5 µs however little it does (performance.md). The picture is the same.

- **Draws that change nothing are left out** (beta 1). The game draws about 12 a frame with the
  toolkit's full-screen rectangle, no pixel shader, no colour target, depth and stencil off, and only
  the depth unit's two "compress disable" bits set. That is how AMD's drivers expand a depth buffer's
  compression data in place before it is read; the PC has no such data. Here each one was a render
  pass of its own, and it ended the pass before it. A draw is left out only when nothing it could
  touch is bound: no colour or depth target, no shader writing memory, no motion vectors, no
  occlusion query measuring it.
  - **Measured** (RTX 3070 Ti, DLAA, Model detail Highest, the tester's lamp spot, against 0.4.4):
    135.0 to 137.0 FPS (GPU 7.40 to 7.30 ms a frame), and 122.2 to 123.7 FPS at the second spot
    (8.18 to 8.08 ms).
  - `BB_SKIP_EMPTY_DRAWS=0` in Extra variables draws them as before.
- **Lamp shadow faces drawn where the game copies them** (beta 4). Each lamp's shadow is a cube map
  of six faces; the game draws each face, copies it whole into a scratch image and blurs it from
  there back into the face (48 faces a frame at the tester's lamp spot). The engine now draws the face
  straight into the scratch image, so the copy has nothing to do and is left out; the blur writes the
  face as before.
  - **Measured** (same PC and settings, `20261105_223017` on and `20261105_223222` off): at the lamp
    spot 136.8 to 141.5 FPS (GPU 7.31 to 7.07 ms a frame), at the second spot 124.8 to 127.9 FPS (8.03
    to 7.82 ms). All 48 faces a frame drawn into their scratch images, no face put back. Since 0.4.4:
    135.0 to 141.5 FPS at the lamp spot.
  - The engine learns each lamp's scratch image from the game's own copies first (4 of them), and
    should anything else touch a face or its scratch image in between, it copies the face back where
    the game drew it.
  - `BB_SHADOW_COPY_SKIP=0` in Extra variables draws and copies as the game does.
- **The lamp shadow faces' clears done as each face's drawing begins** (beta 5). Each face was cleared
  as a step of its own before its drawing; it is now cleared as the first command of the face's
  render pass, with the same value. A face with nothing to draw is cleared where its copy is left
  out, so the blur never reads an uncleared image. `BB_SHADOW_CLEAR_FOLD=0` in Extra variables clears
  them on their own as in beta 4.
- **Not kept:** beta 1 also ran the game's shader `a954e79d` as one step instead of its loop of
  atomics. It was exact, but the GPU time didn't move: the 0.21 ms a frame the profile showed for it
  was the work after it, which had no label of its own. Beta 2 removes it (and
  `BB_GROUP_COUNT_HLE`).

## Diagnostics

- **The GPU profile labels indirect dispatches** (`dispatch indirect cs ...`, beta 2). Their time used
  to go to the label before them.
- **What comes around the shadows' rectangle copies** (`a040875f`, about 43 a frame), with frame
  statistics on (beta 2): every 30,000 copies, the log names the draws and dispatches most often
  recorded right before and right after them (`GPU steps: around the rectangle copies ...`). Beta 1
  found none right after another; this tells whether they can merge with the shadow filter's passes.
  Beta 2's log answered it: each level of each shadow face is drawn, copied and filtered on its own.
- **A trace of the shadow steps** (beta 3), with frame statistics on: twice a session (the 600th and
  the 6,000th frame with lamp shadows), one whole frame's draws into targets up to 256 wide, grouped
  while their targets stay the same, with their shaders, targets and textures; every dispatch, the
  shadow copies with their images and constants; and how many other draws came between
  (`Shadow trace 1 ...` to `Shadow trace 1: end of the frame`). It tells whether a face's four
  levels can be merged.
- **Image clears have their own lines in the GPU profile** (beta 5: `image clear WxH`). Their time
  used to go to the line before them.
- **The frame statistics count** the draws left out and the rectangle copies (`GPU steps per second
  (0.4.5)`), and the log names the first kinds of draw left out with their depth registers. From
  beta 4 also the lamp shadow faces drawn into the scratch image, the copies left out, and the faces
  put back (the first 8 named, with why).

**To test beta 5:** the lamp spot as before (frame statistics and GPU time on): the FPS, then the
same with `BB_SHADOW_CLEAR_FOLD=0` in Extra variables. With it off, the GPU profile shows what the
clears cost (`image clear 256x256, one layer` and the other sizes); with it on, those lines are gone.
The lamp shadows should look as in beta 4.

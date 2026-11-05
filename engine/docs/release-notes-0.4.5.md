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
- **The frame statistics count** the draws left out and the rectangle copies (`GPU steps per second
  (0.4.5)`), and the log names the first kinds of draw left out with their depth registers.

**To test beta 3:** the lamp spot, with frame statistics and GPU time on (Log page), for about a
minute in all: the first trace comes about 5 seconds after lamp shadows appear, the second about 40
seconds later. The FPS should match beta 2's.

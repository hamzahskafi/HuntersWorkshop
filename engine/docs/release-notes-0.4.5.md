# Hunter's Workshop 0.4.5 (in beta)

What changed since 0.4.4, for the release page. The engine work behind each line is in
`docs/changelog.md`; the findings it rests on are in `docs/performance.md`, and what comes next is in
`docs/plan.md`.

## Beta 1: fewer GPU steps

The GPU is the limit at the busiest spots, and not because of pixels: the frame has about 240 steps
and each costs about 5 µs however little it does (performance.md). This beta takes out two kinds of
step that do nothing on the PC. The picture is the same.

- **Draws that change nothing are left out.** The game draws about 12 a frame with the toolkit's
  full-screen rectangle and no pixel shader, no colour target, and depth and stencil off. On the PS4
  they likely work on the depth unit's compression data, which the PC doesn't have. Here each one
  was a render pass of its own, and it ended the pass before it. A draw is left out only when nothing
  it could touch is bound: no colour or depth target, no shader writing memory, no motion vectors,
  no occlusion query measuring it. Expected: about 0.1 ms a frame.
  - The log names the first kinds left out, with the depth registers that would tell what the PS4
    does with them (`GPU steps: a draw left out: ...`).
  - `BB_SKIP_EMPTY_DRAWS=0` in Extra variables draws them as before.
- **The game's `a954e79d` runs as one step.** Five times a frame, this shader writes indirect
  dispatch arguments, then raises a counter one by one in a loop of thousands of atomics on one
  address, while the rest of the GPU waits (about 37 µs each). The engine now writes the same
  arguments and raises the counter by the same amount in one atomic. Memory ends up holding the same
  integers. Expected: about 0.17 ms a frame.
  - If a check fails (an unexpected layout of its buffers), the game's own shader runs, and the
    frame statistics count it.
  - `BB_GROUP_COUNT_HLE=0` in Extra variables runs the game's shader always.
- **Counted with frame statistics on** (`GPU steps per second (0.4.5)`): the draws left out, the
  `a954e79d` runs taken over and those left to the game's shader, and the shadows' rectangle copies
  (`a040875f`) with how many come right after another. The last decides whether merging those copies
  is worth it.

**To test:** the lamp spot, as for 0.4.4's four runs (frame statistics and GPU time on in the Log
page): the FPS as it is, then with `BB_SKIP_EMPTY_DRAWS=0 BB_GROUP_COUNT_HLE=0` in Extra variables.
Anything drawn differently is a bug: the two switches each turn one change off to tell which.

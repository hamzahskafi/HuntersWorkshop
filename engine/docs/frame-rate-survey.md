# Kyo's "60 FPS++", entry by entry

Our frame-rate base (`start/frame_rate_base.inc`) is Kyo's "60 FPS++" list for 1.09: 244 entries,
written at every rate. The list was made at 60 and fixes most constants for 60. This note goes
through every entry: what it is, and what the frame-rate corrections (`gpu/shim/engine/frame_rate.cpp`,
`frame_rate_sites.inc`) do with it now that the game can run at any rate. A value should follow the
frame time only if the game uses it per frame. A duration in seconds, a frame count from data turned
into seconds, or a limit in m/s is the game's at every rate.

How each entry was read (scratchpad tooling, then `tools/re/frame_rate_steps.py` for the rewrites):
the instruction or the constant's readers in the 1.09 executable, the function around it (bounds
from `.eh_frame_hdr`), its callers and vtable slots, strings and singleton names nearby, and Kyo's
other lists (30, 90, uncapped) for what he took the entry to be.

## The step-info: what the game's updates are handed

Most of the game's fixed steps are an FD4Time built on the stack before a call: the vtable 0x56efd40
(`lea R, [0x56efd30]; add R, 0x10`) and the seconds at +8. An FD4Time is seconds. Countdowns subtract
the one they are handed, and wrappers pass it on unchanged (0x1feee20, 0x23a9400). Every 1/30
immediate in the code is in Kyo's list (63 of 63), so his list found all the hard-coded steps. The
1/30 constants in data that he left out are frame counts from data turned into seconds (`vcvtsi2ss`
then `vmulss` by 1/30: 0x131e607, 0x131f7b7, 0x1320214, 0x1337a64, 0x16ab70c, 0x1bb55ed, 0x2323009
and others). They are right at every rate.

## The categories

| Entries | What | Now |
| --- | --- | --- |
| 3 | the frame's step cave (0x2418e3d) | kept; the frame timer replaces it when on |
| 33 | the frame pacer (sub_2434770) | kept; its four targets set for the rate (FRAME_TARGET) |
| 45 | the follow camera's retune | converted above 60 (patches.cpp); its eases per frame with the timer |
| 77 | a 1/30 s step made 1/60 | see below: 57 follow the frame or the rate, 8 the game's, 12 kept |
| 27 | 30 a second made 60 | see below: 9 follow the rate, 17 the game's, 1 kept |
| 28 | a value for a 60 FPS frame | see below: 24 follow the frame or the rate, 4 the game's |
| 3 | frame counts (10 frames, a default of 5, usleep 33000) | set for the rate (FRAME_COUNT) |
| 5 | the sound loop's clock reads and its 16 ms cap | the game's call put back, its update time one frame |
| 8 | "table bytes" at 0x3abc208..0x3abcb20 | kept: the same in Kyo's 30, 60 and 90 lists, in a zero run inside an embedded shader package (PS_BokehBlur) that nothing references |
| 1 | a constant 0.25 at 0x47df584 | kept: nothing reads it, Kyo's code included |
| 1 | the motion blur's sample check | kept: not a frame quantity |
| 7 | 0x21bc181: a fixed step times the frame's | kept: it already follows the frame (status effects' second step) |
| 2 | the frame modes' names | kept: a debug display |
| 1 | status effects' first step | kept: reads the frame's step |
| 2 | the cloth's damping exponent and substep count | kept; above 60 the cloth caves add the stiffness scale |
| 1 | 0x575a908's first value | kept |

## A 1/30 s step made 1/60 (77)

- **The step-infos: 48 sites, new (group `steps`).** The list's 1/60 is right only at 60. Each now
  reads the frame's step (0x575a908, which the cave or the frame timer writes every frame).
  - 15 in a character action family: its +0x260 request run through its own update (vtable +0xd0).
  - 19 in the same through a sub-object at +0x108. Its request at +0x368 is the sub-object's +0x260.
  - 11 in functors ticked until done (vtable +0x10). The Chalice dungeon sequences (SprjHolygrail)
    build some of them.
  - 3 more: an event entity's first update, a list update (sub_1373330), and the item list's screen.

  42 of them are rewritten in place: `mov R32, [0x575a908]; mov [rbp-B], R32; lea R, [0x56efd40];
  mov [rbp-A], R`, the same bytes long, with R still the vtable after. In six (0x1ec3f8c..0x1ec41c8)
  r12 or r13 already holds the vtable and there is no room. There
  `mov rax, [rax+0xd0] ... call rax` becomes `call [rax+0xd0]`, which ends where the call did (the same
  return address), and ecx carries the step. The tool checks that no branch lands inside a rewrite and
  that no flag the dropped `add` set is read. A run of each site's game bytes and our bytes in unicorn,
  on the same state with the step at 1/30, leaves the same registers and memory. The one difference is
  rax at a folded call: it holds the vtable, not the function. No vtable's +0xd0 function reads `al` at
  entry.
- **The Havok AI world step (0x222bcd7):** already in `physics` (bbhost's).
- **The pacer's first target (0x243460c, `mov [r13+0x18]` where the pacer is made):** now the rate's,
  like the four in the pacer (FRAME_TARGET, which now takes the store's bytes).
- **Steps an object keeps: 3, new (`steps`, the rate's step).** 0xa3fc3f (+0x70, sub_a3fad0),
  0x12b5458 (+0x80, a first value until it measures one), and 0x1daa65e (a timeline that advances by
  +0xc8 and counts its length in it).
- **Back to the game's 1/30: 3 new, 5 before (`durations`).** The shortest-duration floors at
  0x4cad65c, 0x4cad660 and 0x4cad664 (`vmaxss`, the siblings of 0x4cadc08 and 0x4cadd28), and
  0x4c88688, 0x4d28418 and 0x4d27720.
- **Already the frame's:** 0x1bf9b74 (the Havok world, `physics`), 0x1c0ca9a (the ragdoll controller),
  and the menus' 0x4d26c9c and 0x4d270e4.
- **Kept:** 8 constants whose readers the list points at 0x575a908 (0x4befdcc, 0x4ccc3f4, 0x4d27554,
  0x4d275f4, 0x4d27c4c, 0x4d28e98, 0x4d29b80, 0x4d29e74); the debug sliders' 0x4d265fc and 0x4d26618;
  0x2418f38 (the shutdown loop, after the game); and 0x12b55b6 (dead).

## 30 a second made 60 (27)

Kyo took each 30.0 for a rate. Read, most are seconds.

- **Back to the game's 30: 5 new, 12 before (`durations`).**
  - 0x13ef51c and 0x13ef6ec: Havok rigid bodies' maximum linear speed, 30 m/s. It is an hkUFloat8
    beside the maximum angular speed (4 pi rad/s) and the damping (0.2, 0.1). Kyo's 60 doubled it.
  - 0x203acb7: an online sequence's 30 s timeout. It is an FD4Time counted down by the step it is
    handed (FrpgNetMan).
  - 0x23dede6: the install-progress poll every 30 s (scePlayGoGetLocus), a static initializer's
    global.
  - 0x1ab67ef: the title screen's longest wait (最大待機時間). Only its debug display reads it.
  - The 12 already there: network timers, summon signs, position records, remote objects' lifetimes,
    session protection, and the Havok AI's braking limit.
- **Rated per frame: 1 new (`lens`), 8 before (`hitbox`).** 0x4c9f358 is 30 x a / b and its inverse
  in the camera lens filter's shader constants (vtable 0x56e7bc0, whose parameters are FilmSize(mm),
  Lens, CoC, NearBokeh...). That is a frame's worth at 30, so it follows the rate. The hitbox's two
  4-lane vectors were rated already.
- **Kept:** 0x23dd026, FD4PerfManager's 30 (a profiling target).

## A value for a 60 FPS frame (28)

Nothing new; each was already right.

- 11 loads that Kyo points at the frame's step (0x1c0f647, 0x1bf9d26, 0x1444943, 0x1cbc50d, 0x2377d4a,
  0x267784b, 0x2677892, 0x2713db7, 0x2715d71, 0x2713dad, 0xeb9f0d) read 0x575a908 at every rate.
- Already corrected: the clip drive's path B (0x1915fab, plus paths A and C, which the list left at
  1/30), grass, the jump momentum (0.95 a frame, 4 lanes), the ghost fades, and the 0.1-a-frame timers.
- 8 entries where the list writes the game's own 1/30:
  - 4 are steps, which the table sets for the rate (0x1972a80, 0x4d2577c, 0x4d29170, 0xf383ca).
  - 4 are frame counts turned into seconds, right as they are: 0x4caf1c0 (0x131e64e), 0x4d26364 (the
    clip drive's path A seconds), and 0x4d27284 and 0x4d2728c (SprjSessionManager's random waits,
    min + (max - min) x r, in frames from data).

## Not visible to this survey

These are only the list's entries and the 1/30 and 30.0 constants around them. A wait counted in
frames with no float constant (docs/high-fps-audit.md) still ends sooner above 30.

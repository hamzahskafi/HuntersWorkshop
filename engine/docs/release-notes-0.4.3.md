# Hunter's Workshop 0.4.3

What changed since 0.4.2, for the release page. The engine work behind each line is in
`docs/community-fixes-plan.md`; every fix taken from another project is credited below and in
`README.md`.

## Fixes

- **Lamp shadows without wedges of light.** A lamp's shadow is six faces, and the game clears one at
  a time; the engine only recognised a whole clear, so the other faces could keep older shadows. Each
  face is now cleared on its own. (bbport 0.7, deadinside28)
- **Less ghosting with DLSS and FSR.** Bushes and flowers drawn in batches are no longer matched to
  another plant's previous position when the camera turns, and an object taking the place of one that
  went out of view no longer inherits its motion. (bbport 0.7, deadinside28)
- **The right camera for the upscaler when the game draws a reflection.** Where a water reflection
  is drawn after the scene (the sewers), DLSS and FSR were given the reflection's camera and depth;
  the scene's are used now, whichever comes first. (after bbport 0.7)
- **Cloth as on the PS4 (test).** At 60 FPS some of the cloth's constraints never acted, and others
  stayed at half strength: the 60 FPS patch runs one cloth solve a frame and Havok was told it was
  always the first of two. They now act on the frames the PS4's second solve would. The cloth's
  damping had the same cause: it was worked out for half a frame and applied for a whole one, so
  cloth settled half as fast as on the PS4 (beta 2). More › Engine › Game fixes › Cloth as on the
  PS4 turns both off, for a side-by-side.
- **Two crash guards** for long sessions: a physics hit list that a stuck query can fill until memory
  runs out is capped (SrShadowy; via bbport 0.7), and a GX occlusion query whose creation ran out of
  room is made again instead of crashing the render thread (bbport #336, whp199). Since beta 2 the cap
  is checked only when the list grows, not for every hit.
- **Motion vectors in busy scenes.** On graphics cards with 6 GB or more, the motion vectors keep
  room for twice as many vertices a frame: in Yharnam's streets many objects ran out of room and got
  no motion of their own, which DLSS and FSR show as ghosting. When room still runs short, characters,
  foliage and objects seen moving come first; the props that give way are mostly static, and their
  motion is the camera's anyway. With frame statistics on, the log says what took the room and the
  busiest frame's need. With Precise GPU readbacks, the motion code no longer reads memory the GPU is
  still writing, which could hang the game.
- **Character creator:** the model turns at the PS4's speed at 60 FPS (twice as fast before).
  (bbport 0.7, deadinside28)
- **Frame limiter:** after a late frame the next one waits its full time instead of coming early.
  (after bbport 0.7)

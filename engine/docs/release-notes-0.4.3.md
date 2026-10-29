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
  always the first of two. They now act on the frames the PS4's second solve would. More › Engine ›
  Game fixes › Cloth as on the PS4 turns it off, for a side-by-side.
- **Two crash guards** for long sessions: a physics hit list that a stuck query can fill until memory
  runs out is capped (SrShadowy; via bbport 0.7), and a GX occlusion query whose creation ran out of
  room is made again instead of crashing the render thread (bbport #336, whp199).
- **Character creator:** the model turns at the PS4's speed at 60 FPS (twice as fast before).
  (bbport 0.7, deadinside28)
- **Frame limiter:** after a late frame the next one waits its full time instead of coming early.
  (after bbport 0.7)

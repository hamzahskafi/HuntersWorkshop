# Hunter's Workshop 0.4.2

What changed since 0.4.1, for the release page. The engine work behind each line is in
`docs/community-fixes-plan.md`.

## Fixes

- **Lamps and candles flicker at the PS4's pace** above 30 FPS: 30 flicker steps a second, where at
  60 FPS they flickered twice as fast. Nothing else about the effects changes. (droogie, bbhost)
- **Free camera** (Lance McDonald's Restore Debug Camera, Graphics › Free camera): its code no
  longer sits over a game function the game can still call, which could crash with the free camera
  on. The camera works as before.
- **Screen Space Reflections** (Kyo's patch): its value no longer lives inside the game's code; it
  reads the 2.0 the game already has.

## Removed

- **Performance Patch:** it never applied on this version of the game (part of it was written for
  another version), so it is no longer offered.

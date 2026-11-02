# Above 30 FPS: what still assumes the game's 30

For the 60 FPS++ list's own entries (our frame-rate base at every rate) see frame-rate-survey.md:
each one read, and what the frame-rate corrections do with it. This note is the wider search, from
before (2026-10-21, brought up to date 2026-10-31).

Bloodborne runs its logic at 30 frames a second. Above 30, with the frame-rate corrections on (the
default; `BB_FRAME_FIXES=0` or the launcher's Frame rate corrections off turns them off), every rate
runs our frame-rate base (Kyo's 60 FPS++ findings: `start/frame_rate_base.inc`; `scripts/patches.py`
applies the patch file's list) and the engine's corrections (`gpu/shim/engine/frame_rate.cpp`,
`frame_rate_sites.inc`), which give it the real frame time and correct what the list gets wrong;
`frame_rate_fixes` (`scripts/patches.py`, `FrameRateFixes` in `start/patches.cpp`) adjusts the
list's writes (the effects' rate, the follow camera). With the corrections off, the older presets
apply (`FPS_PRESETS`, `SCALED_FPS`: the 90 FPS++ list scaled for 120 and 144; 240, on the base
only, runs at 144 without them). This note tracks what is left.

## Tools

- `tools/re/fps_constants.py out/eboot.elf [patched.txt]`: every instruction that reads a float
  constant of a 30 fps frame (1/30, 30.0, as float or double) or stores one as an immediate,
  grouped by function, `*` where a preset already writes it.
- `tools/re/fps_patched.py out/eboot.elf <uncap|60|90|120|144>`: every executable byte that
  preset's writes touch, the second argument above. It builds the presets as `scripts/patches.py`
  does with the corrections off (`FPS_PRESETS`, `SCALED_FPS`, then `frame_rate_fixes`); the
  engine's own sites (`frame_rate_sites.inc`) are not in it.

## State (measured 2026-10-21, 1.09 eboot, the 144 preset of the time)

211 sites in 151 functions; 80 functions have their sites written by the preset, 71 do not. The
144 preset was then the 90 FPS++ list scaled for 144, today's path with the corrections off.

Not every 30 is a 30 fps assumption. Three kinds:

1. **Data frame counts to seconds** (`count * 1/30`, `seconds * 30`): the data is authored in
   30 fps frames, so these are right at any frame rate and must stay. Checked: `0xf1e5e0`,
   `0xf1f790`, `0xf37a40` (teardown functions converting a stored frame count, `+0xac`/`+0x90`/
   `+0x4c`, into seconds); their patched neighbour `0xf1e630` keeps 1/30 too.
2. **A step per frame** (a timer advanced by 1/30 every frame, an ease weight tuned for a 30 fps
   frame): wrong above 30; these get the frame time (`0x575A908`) or a converted weight, as the
   presets do for the camera and status effects.
3. **Not time at all** (30 degrees, 30 units, a 30-entry table): leave alone.

All of them are classified now (frame-rate-survey.md, "The game's other 30s"). With the 0.3.0 table
(the base and the corrections marked as patched) the scan leaves 91 sites in 69 functions; none
needs converting.

## Not visible to the scan

A counter that goes up by one each frame and is compared with a count from data (a wait of N
frames) uses no float constant. Above 30 it ends sooner: N frames at 120 FPS is a quarter of the
time the data meant. Finding these needs the game's state compared between a 30 and a high frame
rate run (the same save, the same input), not a constant search. The integer counts in Kyo's list
itself (a 10-frame countdown, a default of 5, a usleep of 33,000 us) are set for the rate
(`FRAME_COUNT` in `frame_rate_sites.inc`).


## Changes outside the texts

What the launcher does changes in these, not only what it says. Each comes with the check that shows it works.

1. **Suggested settings read the game's monitor.** Today `Hardware.Probe` reads the primary display's mode
   (`EnumDisplaySettings(null, …)`), once per launcher run, and both the suggestion (`Hardware.Recommend`:
   output resolution and frame rate) and the V-Sync note on Display ("The game waits for your 60 Hz display")
   use it, whatever monitor Display › Monitor names. Change: read the mode of the monitor in `display_device`,
   resolved as the game's start resolves it (`WindowsMonitors.Resolve`: the chosen one, else the primary),
   each time it is needed; the summary names that monitor. Check: two monitors with different resolutions and
   refresh rates; pick each in turn, then Suggest… and the V-Sync note follow it; with the chosen monitor
   unplugged, the primary is used and named.
2. **Changed switches lists what Extra variables override.** Today `EnginePage.ShowChanged` names an Extra
   variable only when it sets a switch that is already changed: a switch at its default, or an experimental
   feature (`BB_NATIVE_CALLS=0` with Native draw calls on), goes unmentioned, and the row can still say "None:
   the engine runs with its own defaults". Change: every Extra variable that sets a switch or a feature's
   variable on this page is listed, at its default or not; "None…" only when nothing is changed or set there.
   Reset all keeps leaving Extra variables alone, as today. Check: `BB_NATIVE_CALLS=0` with Native draw calls
   on is listed; an unrelated variable is not.
3. **Unified frame time joins Frame rate corrections.** Its row and `exp_frame_timer` go
   (`EnginePage`, `GameEnvironment`, `LauncherSettings`); the frame timer follows Frame rate corrections. A
   player who had turned it off gets it back on. The engine is unchanged: `BB_FRAME_FIXES_SKIP=timer,camera` in
   Extra variables still runs the corrections on the old smoothed, one-frame-late copy for a test.
4. **Fixed-refresh frame lock greys while Precise frame pacing is off,** as Graphics' child rows do; its
   value is kept and comes back with Precise frame pacing.
5. **Share your settings shares everything except a list.** Today `SharedSettings` lists what goes in, so
   every new setting is left out until someone adds it (DLSS model, texture mip bias, texture filtering, the
   FPS counter's look and most engine switches are out; frame rate, V-Sync, widescreen and live resolution
   are in, though the tooltip says display settings are out). Change: everything in config.ini and the
   launcher's settings goes in except what belongs to this PC (folders and files, monitor, output resolution,
   window mode, HDR, graphics card, sound device, preferred controller, the Intel CPU fixes and the effect
   trail fix, and what is tuned to this PC's monitor, disk or processor: fixed-refresh frame lock, area
   prefetch, shader compile threads, sound threads first), to the player (online account, languages,
   profiles, the launcher's look, trophy notifications) or to one session (log options, Extra variables,
   setup flags). Older codes still import; the import preview lists every change before it applies. Check: a
   code from one PC imported on another changes no folder, monitor or language, and brings DLSS model and
   texture filtering.
6. **Graphics' groups:** `GraphicsPage`'s two effect sets become Game effects and Enhancements, with the
   captions above; the in-game overlay menu's "Game effects (restart required)" section loses model detail
   and screen-space reflections to a new "Enhancements (restart required)" section (its 8 language tables
   gain the name).
7. **In-game overlay menu names:** Quality becomes Upscaling quality and Sharpness becomes Sharpening
   strength (8 language tables).
8. **The row code:** the default line comes from each setting's own default, not from reading its text
   (rows whose tooltip already says "On by default" carry a flag that leaves it out); a line's last full
   stop is dropped; a tooltip that would only repeat its line is left out. Check: V-Sync's hover shows
   "Default: On (latest frame)" alone; Reset trophy progress's line has no full stop.
9. **Proposed, not yet decided: the two Intel effect fixes together.** The effect trail crash fix moves
   from Experimental engine features to Game fixes, under the Intel effect crash fix, and greys while that
   one is on; it is then left out of the start, as the Intel fix's tooltip already asks ("Turn off the
   Effect trail crash fix with it"). Today both can be on at once. Check: with both on in the settings,
   only the Intel fix is applied, and the start log says why.

## Outside review

An outside review of the fourth draft (2026-11-02, at commit `434d381`) checked it against the code.

**Taken:**
- Five texts that promised too much: the respawn fix (it removes a 12-second minimum, loading still takes
  what it takes), No shader freezes (only scene geometry is left out, for at most a second;
  `engine/docs/pipeline_compiles.md`), the shader catalog (a catalog covers only the shaders it holds), Share
  your settings (its included and excluded lists were both wrong), Test the connection (it checks that the
  server and relay answer, not a summon).
- Three old menu names the fourth draft kept unchanged: Live resolution change ("the game menu"), Picture
  tuning ("the Insert menu") and Add echoes or insight ("the in-game menu").
- Frame rate's notes said extra frames above the display's rate are never shown, which V-Sync On (every
  frame) and Adaptive contradict; the sentence moves to the tooltip, where it depends on V-Sync.
- Output resolution said the upscaler always renders below it; Native AA renders at it.
- The Enhancements caption covers lower detail too (Model detail's Lower and Lowest).
- The Suggested settings rename; the two bugs and the dependency handling in "Changes outside the texts".
- 60 characters as an English target, with translated lines wrapping; Off or None first only where it is a
  choice; the default line from the setting, not from its text.
- A strict check that every text this step changes exists in all 13 launcher languages (the key check today
  requires zh-CN only).

**Not taken, and why:**
- A settings framework (one record per setting with timing, applicability and a resolved
  selected/next-launch/running state, and a running-state inspector): beyond a cleanup beta. The status bar
  already says game settings apply at the game's next start; a running state needs the engine to report it.
- Per-row "applies when" badges and symptom-based diagnostic presets: the status bar and the Log options fold
  cover them for now.
- A detected DLC state, a reported HDR peak readout and a list of connected controllers: useful, but not text
  fixes; later.
- Frame rate's line "The frame-rate limit the game aims to run at": the tester's line stays.
- Cheats "Amount" under Blood Echoes: not a launcher fault. The launcher shows one Amount under each item, for
  Custom amount only; the review's table had placed it wrong (fixed here).
- A 14-case acceptance matrix and an accessibility audit: each change above carries its own check; keyboard
  access to tooltips can be looked at with the Linux launcher work.

## Also found

- **The old start in Settings' texts.** The engine folder's hint ("out\bb-probe.exe, its scripts and
  Python"; "with bin\bb-probe.exe and run.py"), its folder picker ("with run.py and bin\bb-probe.exe"), its
  warning ("no run.py with Bloodborne.exe or bin\bb-probe.exe") and the Command row ("instead of
  Bloodborne.exe --run") describe Lantern's start. The launcher starts `engine\out\bb-start.exe`, which
  starts bb-probe.exe (`Paths.IsPortDir`, `Paths.Start`). Proposed: "The engine that runs your game:
  engine\out\bb-start.exe. Your game files stay where they are"; "Not found. It is normally the folder this
  launcher is in"; the picker and the warning as the start's own check already says it ("That folder has no
  engine\out\bb-start.exe.", already translated). `GameRunner`'s comment likewise.
- **Capture shaders** (Log): its text is not translated (no `L.T`).
- **A dead launcher setting:** `dlss_preset` in `LauncherSettings` (default "k"). The DLSS model row writes
  config.ini's `dlss_preset` (default Auto); nothing reads the launcher's.
- **Stale code comments:** `Choices.DlssModels` names `BB_DLSS_PRESET` (now config.ini's `dlss_preset`);
  `Choices.Effects` says the game's own anti-aliasing starts off (on since beta 28).
- **A setting changed in both places during one session:** the launcher holds its config.ini changes while
  the game runs and writes them over the file when the game exits (`Workspace.SaveNow`,
  `IniFile.Save`), so for the same setting the launcher's value wins over the in-game overlay menu's. No
  change proposed; noted so it is not mistaken for a lost setting.
- **Other names that differ in the in-game overlay menu,** left as they are: Camera FOV/distance/height
  scale (launcher: Field of view, Distance, Height, under Camera tuning), Background colour (FPS counter
  background), Live resolution changes (Live resolution change). The surrounding section gives each its
  context.

## What applying it involves

- 124 new lines and 36 new or corrected tooltips, translated into the 13 launcher languages; every other
  tooltip keeps its text and translations. The translations are machine-made in the text step and nobody
  fluent reads them before the release; players' corrections are welcome.
- The row code: the line apart from the tooltip (no more cutting at 64 characters), the default line.
- Eight lists reordered or reworded, four renames, one row removed (Unified frame time).
- The changes outside the texts above, the Also found fixes, and the in-game overlay menu's names and
  sections in its 8 language tables.
- **On screen:** nobody has seen the new lines in the launcher yet. The step checks Classic's wrapping
  and Modern's (i) at 100% and 150% scaling, in English, German and Japanese, before the version step.
- **The Avalonia launcher** (for Linux, later) keeps its own pages until the Linux work; it gets only the
  strings it shares with this one.
- Both launchers build, the localization checks clean, the strict check for this step's texts passing.

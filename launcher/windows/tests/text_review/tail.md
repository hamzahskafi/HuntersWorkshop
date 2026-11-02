
## Also found

- **The old start in Settings' texts:** the engine folder's hint ("out\bb-probe.exe, its scripts and
  Python"; "with bin\bb-probe.exe and run.py"), its folder picker ("with run.py and bin\bb-probe.exe"), its
  warning ("no run.py with Bloodborne.exe or bin\bb-probe.exe") and the Command row ("instead of
  Bloodborne.exe --run") describe Lantern's start. The launcher starts `engine\out\bb-start.exe`, which
  starts bb-probe.exe. Proposed: "The engine that runs your game: engine\out\bb-start.exe. Your game files
  stay where they are"; "Not found. It is normally the folder this launcher is in"; the picker and the
  warning as the start's own check already says it ("That folder has no engine\out\bb-start.exe.", already
  translated). `GameRunner`'s comment likewise.
- **Capture shaders** (Log): its text is not translated (no `L.T`).
- **A dead launcher setting:** `dlss_preset` in `LauncherSettings` (default "k"). The DLSS model row writes
  config.ini's `dlss_preset` (default Auto); nothing reads the launcher's.
- **Stale comments:** `Choices.DlssModels` names `BB_DLSS_PRESET` (now config.ini's `dlss_preset`);
  `Choices.Effects` says the game's own anti-aliasing starts off (on since this beta).
- **Other names that differ in the in-game menu** (left as they are unless you want them matched): Camera
  FOV/distance/height scale (launcher: Field of view, Distance, Height, under Camera tuning), Background
  colour (FPS counter background), Live resolution changes (Live resolution change).

## What the step involves

- 122 new lines and 27 new or corrected tooltips, translated into the 13 languages; every other tooltip
  keeps its text and translations.
- The row code: the line apart from the tooltip (no more cutting at 64 characters), and the default line.
- Eight lists reordered or reworded: V-Sync, Area prefetch, Texture filtering, Model detail, Background,
  Background blur, In-game menu shortcut, and Frame rate's notes.
- Two renames, and Quality and Sharpness renamed in the in-game menu (its 8 tables).
- If agreed: Graphics' Game effects and Enhancements groups, in the launcher and the in-game overlay menu.
- The fixes above. Both launchers built, the translation key check clean.

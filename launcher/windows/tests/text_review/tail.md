
## Also found

- **Capture shaders** (Log): its text is not translated (no `L.T`).
- **A dead launcher setting:** `dlss_preset` in `LauncherSettings` (default "k"). The DLSS model row writes
  config.ini's `dlss_preset` (default Auto); nothing reads the launcher's.
- **Stale comments:** `Choices.DlssModels` names `BB_DLSS_PRESET` (now config.ini's `dlss_preset`);
  `Choices.Effects` says the game's own anti-aliasing starts off (on since this beta).

## What the step involves

- New lines for about 170 rows and new tooltips for about 150, in all 13 languages. Today 110 descriptions
  are translated in all 13; they are rewritten as complete sentences, so they are translated again.
- About 20 renamed titles, and the matching names in the in-game menu (its 8 tables).
- The row code: the line and the tooltip as separate fields (no more cutting at 64 characters), the
  default line, "(recommended)" in lists.
- Six list orders: V-Sync, Area prefetch, Texture filtering, Model detail, Background, Background blur.
- If agreed: the in-game icon and the performance labels (with the measurements behind them).
- Both launchers built, the translation key check clean.

# Launcher text review

Windows launcher, every row on every page (207), as of 0.4.4 beta 28 (2026-11-02). Fourth draft, for the
tester's review. Nothing has been applied. Each row shows **everything it says today**, taken from the code
(title, the line Classic shows under the title, the full tooltip, list choices with their notes, the
default), next to the **proposal**. Beta 28's text step makes it again from the launcher as it is then.

## How it is written

Hunter's Workshop is a port, and its texts read like the port's own notes: plain, short, specific. This
draft keeps that voice and fixes what is wrong or cut, rather than restyling everything.

- **Today's texts stay** wherever they are right, with their translations (110 descriptions are translated
  in all 13 languages). Tooltips change in 27 rows: 7 corrected (wrong or out of date), 20 added where
  today says nothing useful (mostly rows with no tooltip at all, such as the picture sliders).
- **The line under the title** is new where Classic cuts today's text mid-sentence or it says too little
  (a variable's name alone, "Relaxed is the default"). A complete phrase of 60 characters or fewer, saying
  what the setting is or does ("Frees VRAM held by textures the GPU drew"). No instructions ("Choose…",
  "Adjust…"), no full stop. Folders, files and the FPS counter's items keep no line.
- **Specifics stay:** VRAM, FSR and DLSS names, `BB_` names on Engine switches and Log, costs ("about 10%
  FPS"), advice ("Recommended: on", "turn it off if…"), and "Applied when the game starts" where it is said
  today.
- **The default:** the row code adds "Default: …" at the end of a tooltip that doesn't say it already.
- **Lists:** Off or None first. Labels and notes as today.
- **Words:** the in-game overlay menu (Insert or L3 + R3), not "the port's settings menu".
- **Renames** only where the name misleads: Quality becomes Upscaling quality; Skip the game files check
  becomes Allow other game versions. In the in-game menu, Quality and Sharpness take the launcher's names.

## Reading the tables

**Today** is the launcher word for word ("—": nothing there). **Same** keeps today's text. In the tooltip
column, *Default: …* is the line the row code adds.

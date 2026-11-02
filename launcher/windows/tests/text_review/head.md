# Launcher text review

Windows launcher, every row on every page (207), as of 0.4.4 beta 28 (2026-11-02). Third draft, for the
tester's review. Nothing has been applied. Each row shows **everything it says today**, taken from the code
(title, the line Classic shows under the title, the full tooltip, list choices with their notes, the
default), next to the **proposed** text. Beta 28's text step checks it once more against the launcher as it
is then, before any text changes.

## Style guide

The launcher is a Fluent (Windows 11 style) app, so its texts follow Windows 11 Settings. Every row is
written to these rules; a change later is checked against them too.

### The line under the title

One clause, sentence case, no full stop, 60 characters or fewer, no semicolons or colons. It starts with an
instruction verb chosen by the kind of row:

| Row | Pattern | Example |
|---|---|---|
| Switch | What turning it on does | Darken the edges of the screen |
| List | Choose … | Choose how finished frames reach the screen |
| Slider | Adjust … | Adjust the glow around bright lights |
| Button or card | What the action does | Add the game to your Steam library |
| Folder, file, status | No line; the row shows the path or the state | |

### The tooltip

Complete sentences, in this order: what it does, when to change it, what it costs. Advice is never dropped
("Recommended", "turn it off if…", "only if…"). The row code adds the last line itself: **Default: …**, with
**(recommended)** where the default is the advice for everyone. Nothing says when a change applies: the
launcher's status bar already says "game settings apply at its next start" while the game runs, and every
launcher setting takes effect at the next start (the engine reads its settings once, when the game starts).

### Lists

Off or None first, then less to more. The recommended choice carries **(recommended)** in the list itself;
"(default)" in labels goes. A choice's note is one short sentence.

### Words

One word for each thing, everywhere:

| Use | Not |
|---|---|
| graphics card (player pages); GPU on Engine switches and Log | GPU / graphics card mixed |
| video memory | VRAM |
| controller | gamepad, pad |
| the in-game menu (Hunter's Workshop's, Insert or L3 + R3) | the port's settings menu, the Insert menu |
| the game's menus (Bloodborne's own) | the game menu |
| the engine (Hunter's Workshop's code), the game (Bloodborne) | the port |

### One name per setting

A setting the in-game menu also has gets the same name in both places (today: "Sharpness" and "Sharpening
strength", "Camera FOV scale" and "Field of view", "Background colour" and "FPS counter background", "Live
resolution changes" and "Live resolution change", "Quality"). Where the in-game name changes, its 8 menu
tables are translated again.

### Marks (interface changes, for your decision)

- **In-game menu:** a small icon on every setting the in-game menu also has (about 70), instead of
  "Also in game: Insert or L3 + R3" written into a few texts.
- **Performance:** on Graphics' rows, Low / Medium / High. Only where measured or stated by the patch's
  author today; the rest are marked "to measure" and get measured before the labels ship (frame statistics
  and GPU time in the log, at a fixed spot).

## Reading the tables

- **Now** is the launcher today, word for word. "—" means it shows nothing there.
- **Proposed**: "Same" keeps today's. *[Default: …]* is the line the row code adds.
- A heading "New title (now: Old title)" is a rename.


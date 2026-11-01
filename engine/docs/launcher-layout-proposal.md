# Launcher layout: proposal

**Status:** built in 0.4.4 beta 26 (2026-11-01). The tester took the four recommendations at the end, and
the layout below is the one built, with the differences listed in "As built". The text below is the
proposal as it was reviewed.

## As built (beta 26)

What differs from the proposal below, and why:

- **Extra variables** sits at the top of Engine switches, under Changed switches, not at the bottom: it
  is the other half of "what reaches the game", and a single row at the bottom would have been a group
  of its own. Reset all leaves it alone (it is typed text).
- **Gameplay:** Follow camera comes first in the Camera group, so Camera tuning's "the three settings
  below" stays true; the second group is called **Extras** (Play already has a group called Game).
- **The `fullscreen` default in settings.json stays:** the Linux launcher (`bbport_launcher.py`) reads it.
- **Shared code:** the Avalonia launcher compiles the WPF launcher's `Core` folder and uses the same
  translation files. Nothing it uses was removed (`EngineSwitches` keeps its groups; the WPF pages choose
  which switches they show), so it builds and behaves as before, apart from the new config.ini defaults
  and the hints that name the new places.
- **In game:** the HDR source is "HDR 源" in Chinese (the menu font has no glyphs for 来 or 号), and the
  camera section's note names the launcher's Gameplay page.


- **Classic:** the WPF launcher's Classic layout is the one designed here.
- **Modern:** it follows the same pages (its tab mapping is near the end).
- **Avalonia:** that launcher waits until Linux.
- **Source:** the current layout was inventoried from the working tree on 2026-11-01: every row, its key, default and conditions.

## What's wrong today

- **Display mixes five topics:** the screen, frame rate, the FPS counter (14 rows), HDR (8 rows) and languages.
- **Graphics has 43 rows:** upscaler, DLSS, frame generation, picture tuning, camera and game effects.
  - "Game effects" holds four rows that aren't effects: Skip intro videos, Free camera, Debug menu and Follow camera.
  - The camera already has its own group on the same page.
- **Engine settings are split across two pages.** Settings › Performance and Settings › Diagnostics are engine options, and the Engine page has a second group also called Performance.
- **Duplicates:**
  - In-game menu language is on both Play and Display.
  - Settings is listed twice in the Classic sidebar.
  - Settings has an "Engine switches" button that only opens that page.
- **Master switches don't grey out their rows.** This affects the FPS counter, HDR, Picture tuning, Camera tuning, trophy notifications and trophy sound, Sharpening, and Peak brightness from the display. The DLSS options also show whatever the upscaler. The in-game menu gets all of these right.
- **Hints point to the wrong place:**
  - Some name places that exist in one layout only ("More › Engine › …").
  - Some use old names ("Display › Borderless fullscreen", "Enable PC Camera in the launcher").
- **The launcher's seeded defaults differ from the engine's:**

  | Setting | Launcher | Engine |
  |---|---|---|
  | Upscaler | FSR 4 | FSR 3.1 |
  | Quality preset | Ultra Performance | Native AA |
  | Vignette (when unset) | off | on |

- **Frame generation and NVIDIA Reflex are still shown** (5 rows), to everyone on any GPU. The plan's decisions say neither is offered from beta 25.
- **Smaller issues:**
  - A Server drop-down has one option and does nothing.
  - Five groups have a single row: Engine › Input, Engine › Sound, Settings › Engine, Online › Play and Online › account.
  - The Background rows show in Classic, where they do nothing.
  - "Reset all" on the Engine page skips the preset and the 21 experimental features, though its hint says it resets every switch.

## Rules for every page

1. **One topic per page.** Groups come in the order people use them.
2. **No duplicates.** Every setting appears once.
3. **Greyed or hidden:**
   - A row is **greyed** when its own switch is off.
   - A row is **hidden** when it belongs to a choice that isn't made: DLSS rows unless DLSS is chosen, Render scale unless the preset is Custom, the HDR UI rows unless the source is Native.
4. **One name per page.** A page has the same name in both layouts, in hints and in the search.
5. **Same defaults.** The launcher seeds the engine's defaults.
6. **Folds.** "Advanced" folds hold only what most people should leave alone.
7. **No setting's key or meaning changes.**
   - config.ini, settings.json, shared-settings codes and the in-game menu work as before.
   - What changes: where rows sit, their labels, greying and the seeded defaults.

## The sidebar

```
HUNT
  Play              the game, DLC and saves
  Gameplay    new   camera, game options
  Controls
  Trophies
  Online
VISUALS             was SIGHT
  Display           monitor, window, resolution, frame rate
  Upscaling   new   upscaler, quality, sharpening, DLSS
  Picture     new   HDR, picture tuning, game effects
  Interface   new   in-game menu, FPS counter
WORKSHOP
  Mods & patches
  Cheats
ENGINE
  Engine switches   now also performance, sound, extra variables
  Log               was Game output; now also the log options
──────────
  Settings          once, at the bottom: the launcher itself
```

Display and Graphics (78 rows together) become four pages:

| Page | Rows |
|---|---|
| Display | 9 |
| Upscaling | 11 |
| Picture | 29 |
| Interface | 17 |

## Page by page

"←" marks where a moved row comes from.

### Play

- **Above the groups:** the summary pane, Play button and status line, unchanged.
- **Game:** Game folder · The Old Hunters (DLC) · DLC folder · Game language ← Display · Add to Steam.
- **Saves:** Player profile · Saves folder · Save backups · Import saves.
- **Removed:** In-game menu language. It was a duplicate; it stays on Interface.

### Gameplay (new; matches the in-game menu's Gameplay tab)

- **Camera:**
  - Camera tuning.
  - Field of view · Distance · Height, all greyed while Camera tuning is off.
  - Follow camera ← Game effects.
- **Game** (applied when the game starts): Skip intro videos · Free camera · Debug menu, all ← Game effects.

### Controls

- **Keyboard & mouse:**
  - PC controls.
  - Mouse camera · Mouse in menus · Move and look in menus · Mouse sensitivity · Invert horizontal · Invert vertical · Camera auto-rotation. These are greyed while PC controls is off, as now.
  - Hide mouse cursor.
- **Gamepad:** Controller · Swap sticks · Invert camera left/right · Invert camera up/down · Stick dead zone · Preferred gamepad ← Engine › Input.
- **Button icons:**
  - Button prompts ← Keyboard & mouse. Turning PC controls off no longer greys it, since it's about icons, not the mouse.
  - Controller icons ← Gamepad.
- **Profiles** and the **Actions** table: unchanged.

### Trophies

- **Notification:**
  - Show in game · Position (greyed while notifications are off).
  - Play a sound · Volume (greyed while the sound is off).
  - Sound file · Try it.
- **Trophy data:** Trophy log · Reset trophy progress.
- **Hint fix:** the hint names "Display › Window mode" instead of "Display › Borderless fullscreen".

### Online

- **The Hunter's Dream:** Play online · Account · Website.
- **Removed:** the Server drop-down (one option; it does nothing).
- **Connection:** unchanged.

### Display

- **Best settings for this PC** (Recommend…) moves to the top. It sets the resolution, frame rate and upscaler, so it sits on the first Visuals page, and its text says so.
- **Screen:** Monitor · Window mode · Output resolution · Custom resolution (only with Custom) · Widescreen.
- **Frame rate:** Frame rate · V-Sync.
- **Advanced:** Live resolution change (Off (recommended) / On).
- **Window mode** replaces the Fullscreen and Borderless fullscreen toggles. Borderless means nothing unless fullscreen is on.
  - Its options: Windowed · Fullscreen · Borderless fullscreen.
  - It writes the same two keys as today, so the engine sees no change.
  - Today's default, Windowed, stays.
- **Moved out:**
  - To Interface: the FPS counter, Sharp UI, the in-game menu language and shortcut.
  - To Play: Game language.
  - To Picture: HDR.

### Upscaling

- **Upscaler:** Upscaler · Quality preset · Render scale (only with Custom) · Sharpening · Sharpening strength (greyed while Sharpening is off) · Texture mip bias · Character motion vectors.
- **DLSS** (shown only with DLSS, as in game): DLSS model · DLSS auto exposure · DLSS sharper textures · the DLSS status line.
- **Removed: Frame generation and NVIDIA Reflex.** This follows the plan's earlier decision (not offered from beta 25), and the code is kept.
  - The in-game menu's frame-generation items go as well.
  - A saved `frame_gen` reads as off.
- **What these rows offer** (which upscalers, "Off", the two mip-bias settings) is for the AMD beta's settings discussion. This beta only moves them.

### Picture

- **HDR:**
  - HDR output, the switch for the rest of the group. Everything below is greyed while it's off.
  - HDR source · Paper white.
  - Peak brightness from the display · Peak brightness (greyed while taken from the display).
  - Highlight boost.
  - UI brightness · UI colour (only with the Native source, as in game).
  - The Advanced fold goes.
- **Picture tuning:**
  - Picture tuning, the switch for the rest of the group.
  - Brightness · Contrast · Saturation · Hue · Fog · Bloom · Vignette · Ambient occlusion strength · Shadow distance. This is the in-game order, and all of them are greyed while Picture tuning is off.
  - **Game's own picture:** a reset button, as in game.
- **Game effects** (applied when the game starts):
  - Model detail.
  - Texture filtering ← Engine › Graphics.
  - Chromatic aberration · Depth of field · Motion blur.
  - No motion blur smear with upscalers ← Engine › Graphics. It sits under Motion blur, whose note already points to it.
  - Ambient occlusion (SSAO) · Game's own anti-aliasing · Shadows from dynamic lights · Screen-space reflections.

### Interface

- **Menus and HUD:** In-game menu shortcut · In-game menu language · Sharp UI · Note while shaders build.
- **FPS counter:**
  - FPS counter (on/off).
  - What it shows: Frame rate · Frame time · Upscaler · CPU usage · GPU usage.
  - A **Look** fold: Position · Size · Text colour · Text opacity · Background · Background opacity · Border.
  - Everything in the group is greyed while the counter is off.

### Mods & patches, Cheats

Unchanged.

### Engine switches

- **Changed switches** with **Reset all.** Reset all now resets every row on the page. Today it skips the preset and the experimental features, though its hint says every switch.
- **Engine features:** Engine preset · Experimental engine features (fold, unchanged).
- **Performance:**
  - Graphics card ← Engine › Graphics (the last row left there).
  - Two-stage GPU pipeline · CPU reads of GPU data, both ← Settings › Performance.
  - Shader compile threads · Area prefetch · Frame rate in the background.
- **Sound:** Sound output · Sound threads first, both ← Settings › Performance · Run without sound.
- **Game fixes:** unchanged.
- **Extra variables** ← Settings › Diagnostics, at the bottom: for anything this page doesn't list. Its path becomes Engine switches › Extra variables. (Built at the top: see "As built".)

### Log (was Game output)

- **Unchanged:** the toolbar, the live log, and "Show this page when the game starts".
- **Log options** (fold) ← Settings › Diagnostics: Log level · Frame statistics · Hitch reports · Controller details · FSR 4 profile · GPU time · Audio statistics · Loading profile · Render stage timings · Capture shaders.

### Settings (the launcher itself)

- **Appearance:**
  - Launcher language · Layout · Navigation (Modern only, as now) · Theme · Theme colour (only with Custom).
  - Background · Picture · Background blur. These three show only in Modern, where they do something.
  - App icon · Icon picture (only with "Your picture") · Window buttons · Window buttons side.
- **Engine and launch:** Engine folder · Start the game with · Command · Arguments (only with Custom command). This merges two groups of 1 and 3 rows.
- **Files**, **Share settings**, **Updates** and **Build from source:** unchanged.
- **Moved out:** Performance (to Engine switches) and Diagnostics (to Engine switches and Log).
- **Removed:** the Engine switches button.

## The Modern layout

- **Tabs:** Play · Visuals · Gameplay · Controls · Trophies · Online · Settings · More. That's eight, as now.
- **Visuals** works as More does today, with sub-tabs: Display · Upscaling · Picture · Interface.
- **More:** Mods & patches · Cheats · Engine switches · Log. These are the sidebar's names; today More says "Mods", "Engine" and "Log".
- **Modern with sidebar navigation** uses the sidebar above.
- **Most of the work is shared.** The pages are shared, so every move above reaches Modern on its own. The only new parts are the tab row and the Visuals container, which is a copy of More's.

## Labels matched with the in-game menu

| Launcher today | In game today | Both |
|---|---|---|
| Object motion vectors | Character motion vectors | Character motion vectors |
| Show FPS | FPS counter in corner | FPS counter |
| HDR source | HDR mode | HDR source |
| Echoes from kills | Blood Echo multiplier | Blood Echo multiplier |
| Camera tuning | PC Camera ("Enable PC Camera in the launcher") | Camera tuning; the in-game text names Gameplay › Camera tuning |

The in-game menu's tabs stay as they are (decision 4).

## Defaults

| Setting | Launcher seeds | Engine | Proposed |
|---|---|---|---|
| Upscaler | FSR 4 | FSR 3.1 | FSR 3.1 |
| Quality preset | Ultra Performance | Native AA | Native AA |
| Vignette | off when unset | on | on |

- **Nobody's existing settings change.** Seeds only fill keys a config.ini lacks.
- **The first start** still runs "Best settings for this PC". It picks DLSS on RTX, FSR 4 on RDNA 4 and FSR 3.1 otherwise, with a preset for the GPU.
- **Why FSR 3.1 at Native AA:** it runs on any GPU, so it's the safer seed for someone who declines that offer.
- **The launcher's own fallbacks** for a missing preset or upscaler (three places, each different today) use the same values.
- **Removed:** the unused `fullscreen` default in settings.json.

## Work

- **WPF launcher:**
  - The pages above: Graphics splits into Upscaling and Picture; Gameplay and Interface are new.
  - MainWindow: the sidebar, the tabs and the Visuals container.
  - Deep links, the search and the hints.
- **Translations:**
  - The new names and changed hints, in the 13 launcher languages.
  - The in-game label changes, in its 10 languages.
- **In-game menu:** the labels above. Frame generation's items are removed, per the plan's decision.
- **Tests:**
  - Every page in both layouts and both navigations.
  - The search finds every row.
  - An old config.ini and settings.json load with nothing changed.
  - A shared-settings code from 0.4.3 imports.
- **Beta numbering:** beta 26 on `claude/0.4.4-beta-26`. AMD's FSR SDK becomes beta 27, and the PC memory model's removal beta 28.

## Decisions for the tester

1. **Structure:** as above, or with changes.
2. **Sidebar headers:**
   - HUNT · VISUALS · WORKSHOP · ENGINE (recommended: only SIGHT changes).
   - Or plain words throughout: GAME · VISUALS · MODS · ENGINE.
3. **Defaults:**
   - The launcher seeds the engine's defaults (recommended).
   - Or the engine takes the launcher's.
4. **In-game menu:**
   - Labels only (recommended).
   - Or HDR also moves from its Display tab to Picture, so both menus match. That would leave the in-game Display tab with the output resolution alone.

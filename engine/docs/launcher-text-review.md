# Launcher text review

Every row of the Windows launcher (208 rows on 13 pages), its text today beside the proposed text, as of
Hunter's Workshop 0.4.4 beta 28 (2026-11-02). Fifth draft: the fourth draft, corrected after an outside review
and with the tester's decisions on it. Nothing here has been applied yet: beta 28's text step applies it,
after it is made again from the launcher as it is then (`launcher/windows/tests/text_review`).

## How to review

- Each page has one table. **Today** is the launcher word for word ("Line" is what the Classic layout shows
  under a title, cut at 64 characters today; "Tooltip" is the full text on hover and behind Modern's (i)).
- The proposed columns show the full text. *(unchanged)* marks today's text, kept with its translations. In
  the tooltip column, *Default: …* is the line the launcher adds from the setting's own default.
- Edit a proposed cell or comment on it. Rows left alone are taken as approved.
- Decisions already taken are listed below with their reasons; changes to how settings behave (not only what
  they say) are in "Changes outside the texts", after the pages.

## How it is written

Hunter's Workshop is a port, and its texts read like the port's own notes: plain, short and specific. This
draft keeps that voice and fixes what is wrong, cut or out of date, rather than restyling everything.

- **Today's texts stay** wherever they are right, with their translations (110 descriptions are translated
  in all 13 languages). Tooltips change in 36 rows: 16 corrected (wrong, overpromising or out of date), 20
  added where today says nothing useful, mostly rows with no tooltip at all, such as the picture sliders.
- **The line under the title** is new in 124 rows, where Classic cuts today's text mid-sentence, it says
  too little (a variable's name alone, "Relaxed is the default") or it describes one choice of two. It
  says what the setting is or does ("Frees VRAM held by textures the GPU drew"), or gives a short
  instruction where that reads more clearly ("Use the keyboard and mouse layout below"). It ends without
  a full stop: the row code drops a kept line's last one. New lines keep to 60 characters; kept lines stay
  as today, up to 64. A translated line wraps instead of being cut. Folders, files and the FPS counter's
  items keep no line.
- **No tooltip that only repeats the line.** In 28 rows today's tooltip is the line again; the row code
  leaves it out, and hovering shows only the default line, where there is one. The two stick invert rows
  drop theirs, as the mouse's do: their new line says more.
- **Specifics stay:** VRAM, FSR and DLSS names, `BB_` names on Engine switches and Log, costs ("about 10%
  FPS", measured), advice ("Recommended: on", "turn it off if…"), and "Applied when the game starts" where
  it is said today.
- **No promise past what the code does.** Each claim was checked against the code (the outside review
  found five that went too far; they are corrected).
- **Defaults:** the launcher adds "Default: …" from the setting itself, except where the tooltip already
  says it ("On by default").
- **Lists:** Off or None first where Off or None is a choice; Automatic and numeric lists keep their order.
  Only the order and labels change, never the value a choice stores.
- **Words:** "the in-game overlay menu (Insert or L3 + R3)" for Hunter's Workshop's own menu, not "the
  port's settings menu", "the Insert menu" or "the game menu"; "the game's menus" for Bloodborne's own.

## Decisions taken

| Decision | Why | Where |
| --- | --- | --- |
| Off or None first in every list where it is a choice | The usual order; the tester's call | V-Sync, Area prefetch, Texture filtering, Model detail, Background, Background blur, In-game menu shortcut |
| Anti-aliasing: Off, The game's own, TAA; "The game's own" keeps its name | The tester's call | Graphics (applied) |
| XeSS is not offered | Not implemented | Graphics (applied) |
| One Log options fold with the log level first | The tester's call | Log (applied) |
| The port's own voice: today's texts stay where right | The tester: the third draft read like a production game's | Every page |
| Engine update available says why it shows | It shows when the launcher updated itself and the engine package is older | Play |
| Frame rate's line names the PS4's 30 and the engine's corrections | The tester's call | Display |
| Renames: Upscaling quality (was Quality), Allow other game versions (was Skip the game files check), Suggested settings for this PC (was Best settings for this PC), Reduce shader stalls (was No shader freezes) | Each old name misleads: overall quality; a check you skip; a measured best; a promise of no freezes | Graphics, Engine switches; Quality and Sharpness also in the in-game overlay menu |
| Graphics' Detail and Effects become Game effects and Enhancements | Five switches leave out what the game has; reflections, texture filtering and model detail go past it | Graphics and the in-game overlay menu |
| Unified frame time joins Frame rate corrections | It works only with the corrections on, and every log since 0.3.0 ran with both | Engine switches |
| Fixed-refresh frame lock greys while Precise frame pacing is off | It needs it; its own value is kept | Engine switches |
| Share your settings shares everything except what belongs to this PC, to the player or to one session | The tester's call | Settings |
| Suggested settings read the game's monitor; Changed switches lists what Extra variables override | Two bugs the outside review found | Graphics, Engine switches |
| The two Intel effect fixes stay separate switches, as today | The tester's call | Engine switches |


## What changes

166 of the 208 rows change; the other 42 keep everything as today. Each is shown in full in its page's table below.

| Page | Setting | What changes |
| --- | --- | --- |
| Play | Engine update available | new line |
| Play | The Old Hunters (DLC) | new line |
| Play | Add to Steam | new line |
| Play | Game language | tooltip dropped (repeated the line) |
| Play | Player profile | new line |
| Play | Save backups | new line |
| Play | Import saves | new line |
| Display | Output resolution | new line, tooltip corrected |
| Display | Custom resolution | new line |
| Display | Frame rate | new line, tooltip corrected, list notes reworded |
| Display | V-Sync | tooltip dropped (repeated the line), list reordered |
| Display | Monitor | new line |
| Display | Window mode | tooltip dropped (repeated the line) |
| Display | Widescreen | new line |
| Display | Live resolution change | new line, tooltip corrected |
| Display | HDR output | new line |
| Display | HDR source | new line |
| Display | UI brightness | new line |
| Display | UI colour | new line |
| Display | Paper white | tooltip dropped (repeated the line) |
| Display | Peak brightness from the display | new line |
| Display | Peak brightness | new line |
| Display | Highlight boost | new line |
| Display | Picture tuning | new line, tooltip corrected |
| Display | Brightness | tooltip added |
| Display | Contrast | tooltip added |
| Display | Saturation | tooltip added |
| Display | Hue | tooltip added |
| Display | Fog | new line, tooltip added |
| Display | Bloom | new line, tooltip added |
| Display | Vignette | new line, tooltip added |
| Display | Ambient occlusion strength | new line, tooltip added |
| Display | Shadow distance | new line, tooltip added |
| Display | Game's own picture | tooltip dropped (repeated the line) |
| Graphics | Suggested settings for this PC | renamed, new line, tooltip corrected |
| Graphics | Upscaling quality | renamed, new line |
| Graphics | Model detail | new line, tooltip added, list reordered |
| Graphics | Texture filtering | new line, list reordered |
| Graphics | Ambient occlusion (SSAO) | new line |
| Graphics | Shadows from dynamic lights | new line, tooltip added |
| Graphics | Screen-space reflections | new line, tooltip added |
| Graphics | Chromatic aberration | new line, tooltip added |
| Graphics | Depth of field | new line |
| Graphics | Motion blur | new line, tooltip added |
| Graphics | No motion blur smear with upscalers | new line |
| Interface | In-game menu shortcut | new line, tooltip corrected, list reordered |
| Interface | In-game menu language | new line, tooltip dropped |
| Interface | Sharp UI | new line |
| Interface | Note while shaders build | new line |
| Interface | FPS counter | new line |
| Interface | Frame rate | tooltip dropped |
| Interface | Frame time | tooltip dropped |
| Interface | Upscaler | tooltip dropped |
| Interface | CPU usage | tooltip dropped |
| Interface | GPU usage | tooltip dropped |
| Interface | FPS counter text colour | tooltip dropped (repeated the line) |
| Gameplay | Follow camera | new line |
| Gameplay | Camera tuning | new line |
| Gameplay | Field of view | tooltip added |
| Gameplay | Distance | tooltip added |
| Gameplay | Height | tooltip added |
| Gameplay | Free camera | tooltip dropped (repeated the line) |
| Gameplay | Debug menu | new line |
| Gameplay | Hunter's Dream mirror | new line |
| Gameplay | Rebirth at the altar | new line |
| Controls | PC controls | new line |
| Controls | Mouse camera | new line |
| Controls | Mouse in menus | new line |
| Controls | Move and look in menus | new line |
| Controls | Mouse sensitivity | new line |
| Controls | Invert horizontal | new line |
| Controls | Invert vertical | new line |
| Controls | Camera auto-rotation | tooltip dropped (repeated the line) |
| Controls | Hide mouse cursor | new line |
| Controls | Swap sticks | tooltip dropped (repeated the line) |
| Controls | Invert camera left / right | new line, tooltip dropped |
| Controls | Invert camera up / down | new line, tooltip dropped |
| Controls | Stick dead zone | new line |
| Controls | Preferred gamepad | new line |
| Controls | Button prompts | new line |
| Controls | Controller icons | new line |
| Controls | Save and share | new line |
| Engine switches | Changed switches | tooltip dropped (repeated the line) |
| Engine switches | Extra variables | new line |
| Engine switches | Legacy shader compiler | new line, tooltip corrected |
| Engine switches | VRAM cleanup | new line |
| Engine switches | Precise frame pacing | new line |
| Engine switches | Particle work safety cap | new line, tooltip corrected |
| Engine switches | Use more VRAM for textures | new line |
| Engine switches | Graphics threads first | new line |
| Engine switches | Shorter sleep spin | new line |
| Engine switches | Background shader compiling | new line |
| Engine switches | Occlusion culling | new line |
| Engine switches | Faster draw thread | new line |
| Engine switches | Reduce shader stalls | renamed, new line, tooltip corrected |
| Engine switches | Fast loading | new line |
| Engine switches | Frame rate corrections | new line, tooltip corrected |
| Engine switches | Unified frame time | row removed (merged into frame rate corrections) |
| Engine switches | Fewer memory write checks | new line |
| Engine switches | Per-frame GPU data in place | new line |
| Engine switches | Native draw calls | new line |
| Engine switches | Effect trail crash fix | new line |
| Engine switches | Graphics card | new line |
| Engine switches | Two-stage GPU pipeline | tooltip dropped (repeated the line) |
| Engine switches | CPU reads of GPU data | new line, tooltip added |
| Engine switches | Shader compile threads | new line |
| Engine switches | Area prefetch (hard disks) | new line, list reordered |
| Engine switches | Frame rate in the background | new line |
| Engine switches | Sound threads first | new line |
| Engine switches | No minimum loading time after death | new line |
| Engine switches | Weapon sound save fix | new line |
| Engine switches | OptiScaler compatibility | new line |
| Engine switches | Ebrietas effect fix | new line, tooltip corrected |
| Engine switches | PS4 floating-point mode | new line |
| Engine switches | Intel effect crash fix (test) | new line |
| Engine switches | Intel black tonemap fix | new line |
| Engine switches | Allow other game versions | renamed, new line |
| Log | Log level | new line |
| Log | Frame statistics in the log | new line, tooltip added |
| Log | Hitch reports | new line |
| Log | GPU time in the log | new line |
| Log | Render stage timings in the log | new line |
| Log | Loading profile in the log | new line |
| Log | Audio statistics in the log | new line, tooltip added |
| Log | Controller details in the log | new line |
| Log | Heap corruption checks | new line |
| Log | Capture shaders | new line, tooltip corrected |
| Settings | Launcher language | tooltip dropped (repeated the line) |
| Settings | Layout | new line |
| Settings | Navigation | tooltip dropped (repeated the line) |
| Settings | Theme | new line |
| Settings | Theme colour | tooltip dropped (repeated the line) |
| Settings | Background | tooltip dropped (repeated the line), list reordered |
| Settings | Picture | tooltip dropped (repeated the line) |
| Settings | Background blur | list reordered |
| Settings | Icon picture | tooltip dropped (repeated the line) |
| Settings | Window buttons | tooltip dropped (repeated the line) |
| Settings | Command | new line, tooltip corrected |
| Settings | Launcher settings | tooltip dropped (repeated the line) |
| Settings | Game settings (config.ini) | tooltip dropped (repeated the line) |
| Settings | Shader cache | new line |
| Settings | Shader catalog | new line, tooltip corrected |
| Settings | Share your settings | new line, tooltip corrected |
| Settings | Update channel | new line |
| Settings | Check for updates at start | tooltip dropped (repeated the line) |
| Online | Play online | new line |
| Online | Website | tooltip dropped (repeated the line) |
| Online | Local P2P port | new line |
| Online | Summon sign timeout | new line |
| Online | Test the connection | new line |
| Mods & patches | Load mods | new line |
| Cheats | Blood Vials | new line |
| Cheats | Amount | tooltip dropped (repeated the line) |
| Cheats | Quicksilver Bullets | new line |
| Cheats | Amount | tooltip dropped (repeated the line) |
| Cheats | Infinite consumables | new line |
| Cheats | Infinite stamina | tooltip dropped (repeated the line) |
| Cheats | Stealth | tooltip dropped (repeated the line) |
| Cheats | No weapon wear | tooltip dropped (repeated the line) |
| Cheats | Firearm range | new line |
| Cheats | Keep Blood Echoes on death | new line |
| Cheats | Blood Echo multiplier | new line |
| Cheats | Add echoes or insight | new line, tooltip corrected |
| Trophies | Show in game | new line |
| Trophies | Sound file | new line |
| Trophies | Try it | tooltip dropped (repeated the line) |

## Play

| Setting | Today | Proposed line | Proposed tooltip | Proposed choices | Notes |
| --- | --- | --- | --- | --- | --- |
| **Engine update available** | Line: This package has an older engine than this launcher… · Tooltip: This package has an older engine than this launcher: the crash fixes since then are missing. Updating downloads the latest package and keeps your settings, saves and mods. | The launcher was updated, but the engine wasn't | This package has an older engine than this launcher: the crash fixes since then are missing. Updating downloads the latest package and keeps your settings, saves and mods. *(unchanged)* | — | Shown when the launcher updated itself and the engine package is older. |
| **Game folder** · Game | — | — | — | — |  |
| **The Old Hunters (DLC)** · Game | Line: Off: the game starts without its DLC. · Tooltip: Off: the game starts without its DLC. The Game of the Year edition (CUSA03173) always has it · Default: On | Starts the game with The Old Hunters expansion | Off: the game starts without its DLC. The Game of the Year edition (CUSA03173) always has it *(unchanged)* + *Default: On* | — |  |
| **DLC folder** · Game | — | — | — | — |  |
| **Add to Steam** · Game | Line: Bloodborne as a non-Steam game in your Steam library… · Tooltip: Bloodborne as a non-Steam game in your Steam library: Steam Input, the overlay and Big Picture work with it | Adds Bloodborne to Steam as a non-Steam game | Bloodborne as a non-Steam game in your Steam library: Steam Input, the overlay and Big Picture work with it *(unchanged)* | — |  |
| **Game language** · Game | Line: The game's own text and voices · Tooltip: The game's own text and voices · Choices: English; Russian; Japanese; French; Spanish; German; Italian · Default: English | The game's own text and voices *(unchanged)* | None (it would repeat the line) + *Default: English* | English; Russian; Japanese; French; Spanish; German; Italian *(unchanged)* |  |
| **Player profile** · Saves | Line: Each profile has its own save files and trophies, so several… · Tooltip: Each profile has its own save files and trophies, so several people can play their own runs on this PC | Each player gets their own saves and trophies | Each profile has its own save files and trophies, so several people can play their own runs on this PC *(unchanged)* | — |  |
| **Saves folder** · Saves | — | — | — | — |  |
| **Save backups** · Saves | Line: Before every start and every… · Tooltip: Before every start and every 10 minutes while playing, when this profile's saves changed. The newest 10 from before starting and 5 from playing are kept | This profile's saves, backed up automatically | Before every start and every 10 minutes while playing, when this profile's saves changed. The newest 10 from before starting and 5 from playing are kept *(unchanged)* | — |  |
| **Import saves** · Saves | Line: From shadPS4, another Bloodborne port, or a PS4 save exported… · Tooltip: From shadPS4, another Bloodborne port, or a PS4 save exported decrypted (Apollo Save Tool, Save Wizard). This profile's saves are backed up first | From shadPS4, another port or a decrypted PS4 save | From shadPS4, another Bloodborne port, or a PS4 save exported decrypted (Apollo Save Tool, Save Wizard). This profile's saves are backed up first *(unchanged)* | — |  |

## Display

| Setting | Today | Proposed line | Proposed tooltip | Proposed choices | Notes |
| --- | --- | --- | --- | --- | --- |
| **Output resolution** | Line: The upscaler renders below this and fills it in · Tooltip: The upscaler renders below this and fills it in · Choices: 1280 × 720; 1600 × 900; 1920 × 1080; 2560 × 1440; 3840 × 2160; 2560 × 1080 (21:9); 3440 × 1440 (21:9); 3840 × 1600 (24:10); 5120 × 2160 (21:9); 3840 × 1080 (32:9); 5120 × 1440 (32:9); 7680 × 2160 (32:9); 1280 × 1024 (5:4); 1280 × 800 (Steam Deck); 1680 × 1050 (16:10); 1920 × 1200 (16:10); 2560 × 1600 (16:10); Display (auto) (Your display's own resolution, whatever its shape: the picture fills the screen); Custom (Any width and height: set them below) · Default: 1920 × 1080 | The resolution the game is shown at | The resolution the game is shown at. An upscaler renders at it (Native AA) or below it, and fills in the rest + *Default: 1920 × 1080* | 1280 × 720; 1600 × 900; 1920 × 1080; 2560 × 1440; 3840 × 2160; 2560 × 1080 (21:9); 3440 × 1440 (21:9); 3840 × 1600 (24:10); 5120 × 2160 (21:9); 3840 × 1080 (32:9); 5120 × 1440 (32:9); 7680 × 2160 (32:9); 1280 × 1024 (5:4); 1280 × 800 (Steam Deck); 1680 × 1050 (16:10); 1920 × 1200 (16:10); 2560 × 1600 (16:10); Display (auto) (Your display's own resolution, whatever its shape: the picture fills the screen); Custom (Any width and height: set them below) *(unchanged)* | Tooltip corrected. In-game overlay menu: Output resolution. Today's text says the upscaler always renders below the output; Native AA renders at it. |
| **Custom resolution** | Line: Your own output size, any shape (5:4, 16:10, 32:9...)… · Tooltip: Your own output size, any shape (5:4, 16:10, 32:9...): the camera and UI follow it with Widescreen on | Any width and height, in any shape | Your own output size, any shape (5:4, 16:10, 32:9...): the camera and UI follow it with Widescreen on *(unchanged)* | — |  |
| **Frame rate** · Frame rate | Line: Every rate above 30 runs on Kyo's 60 FPS++, the engine setting… · Tooltip: Every rate above 30 runs on Kyo's 60 FPS++, the engine setting the game's values for the rate (Engine switches › Frame rate corrections). 30 is the game as on the PS4 · Choices: 30 FPS (as on PS4); 60 FPS (Kyo's 60 FPS++ with the engine's values for 60 FPS (Frame rate corrections). On a slower display the extra frames are drawn but never shown); 90 FPS (Kyo's 60 FPS++ with the engine's values for 90 FPS (Frame rate corrections). On a slower display the extra frames are drawn but never shown); 120 FPS (Kyo's 60 FPS++ with the engine's values for 120 FPS (Frame rate corrections). On a slower display the extra frames are drawn but never shown); 144 FPS (Kyo's 60 FPS++ with the engine's values for 144 FPS (Frame rate corrections). On a slower display the extra frames are drawn but never shown); 240 FPS (Kyo's 60 FPS++ with the engine's values for 240 FPS. Needs Frame rate corrections (Engine switches): without them the game runs at 144. On a slower display the extra frames are drawn but never shown); Uncapped (No limit: as fast as your PC can go, up to 480 FPS. Above 240 FPS the game is untested, and the old community uncapped patch froze in boss fights at such rates. For a high frame rate we recommend 240) · Default: 60 FPS | 30 is the PS4's; higher rates use the engine's corrections | Every rate above 30 runs on the engine's frame rate corrections, built on Kyo's 60 FPS++ and other community findings. With the corrections off (Engine switches › Frame rate corrections), the community FPS++ patches run instead, and 240 runs at 144. Above the display's refresh rate, V-Sync On (latest frame) and Off draw frames the display never shows; On (every frame) and Adaptive hold the game to the display's rate. 30 is the game as on the PS4 + *Default: 60 FPS* | 30 FPS (as on PS4); 60 FPS (With the engine's frame rate corrections for 60 FPS); 90 FPS (With the engine's frame rate corrections for 90 FPS); 120 FPS (With the engine's frame rate corrections for 120 FPS); 144 FPS (With the engine's frame rate corrections for 144 FPS); 240 FPS (With the engine's frame rate corrections for 240 FPS. Needs Frame rate corrections (Engine switches): without them the game runs at 144); Uncapped (No limit: as fast as your PC can go, up to 480 FPS. Above 240 FPS the game is untested, and the old community uncapped patch froze in boss fights at such rates. For a high frame rate we recommend 240) | Tooltip corrected. Today's texts credit Kyo's patch for what the engine's corrections do. The notes' "drawn but never shown" is wrong with V-Sync On (every frame) or Adaptive, which hold the game to the display (the note under V-Sync already says so). |
| **V-Sync** · Frame rate | Line: How finished frames reach the screen · Tooltip: How finished frames reach the screen · Choices: On (latest frame) (No tearing, low latency: each refresh shows the newest finished frame. Default (Mailbox)); On (every frame) (Classic V-Sync: every frame shown in order, no tearing, a little more latency. The game can't run faster than the screen (FIFO)); Adaptive (Like On (every frame), but tears instead of stuttering when a frame is late (FIFO Relaxed)); Off (Frames go out the moment they're ready: lowest latency, may tear (Immediate)) · Default: On (latest frame) | How finished frames reach the screen *(unchanged)* | None (it would repeat the line) + *Default: On (latest frame)* | Off (Frames go out the moment they're ready: lowest latency, may tear (Immediate)); On (latest frame) (No tearing, low latency: each refresh shows the newest finished frame. Default (Mailbox)); On (every frame) (Classic V-Sync: every frame shown in order, no tearing, a little more latency. The game can't run faster than the screen (FIFO)); Adaptive (Like On (every frame), but tears instead of stuttering when a frame is late (FIFO Relaxed)) | Its note ("The game waits for your 60 Hz display") will read the game's monitor (Changes outside the texts). |
| **Monitor** · Screen | Line: The game opens here from the start, including fullscreen and… · Tooltip: The game opens here from the start, including fullscreen and HDR. If this monitor is disconnected, the Windows primary monitor is used. Applied on the next launch. | The monitor the game opens on | The game opens here from the start, including fullscreen and HDR. If this monitor is disconnected, the Windows primary monitor is used. Applied on the next launch. *(unchanged)* | — |  |
| **Window mode** · Screen | Line: How the game's window fills the screen · Tooltip: How the game's window fills the screen · Choices: Windowed; Borderless fullscreen (Trophy notifications and other windows show over the game); Fullscreen (The driver may give the game the screen to itself (exclusive fullscreen): the lowest latency and G-SYNC's fullscreen-only mode, but trophy notifications and other windows don't show over it) | How the game's window fills the screen *(unchanged)* | None: it would repeat the line | Windowed; Borderless fullscreen (Trophy notifications and other windows show over the game); Fullscreen (The driver may give the game the screen to itself (exclusive fullscreen): the lowest latency and G-SYNC's fullscreen-only mode, but trophy notifications and other windows don't show over it) *(unchanged)* |  |
| **Widescreen** · Screen | Line: Outputs that are not 16:9 (21:9, 32:9, 16:10, 5:4) with the… · Tooltip: Outputs that are not 16:9 (21:9, 32:9, 16:10, 5:4) with the right field of view and the UI kept in shape · Default: On | Right field of view and UI on screens that aren't 16:9 | Outputs that are not 16:9 (21:9, 32:9, 16:10, 5:4) with the right field of view and the UI kept in shape *(unchanged)* + *Default: On* | — |  |
| **Live resolution change** · Advanced | Line: Change the resolution in the game menu without restarting. · Tooltip: Change the resolution in the game menu without restarting. Off is faster; turn on only if you switch resolutions in game · Choices: Off (recommended); On · Default: Off (recommended) | Change the resolution in game, without restarting | Lets the in-game overlay menu change the resolution without restarting. Off is faster; turn it on only if you switch resolutions in game + *Default: Off* | Off (recommended); On *(unchanged)* | Tooltip corrected. In-game overlay menu: Live resolution changes. "The game menu" today is the in-game overlay menu: the game's own menus don't change the resolution. |
| **HDR output** · HDR | Line: The game's picture on an HDR screen, with brighter highlights. · Tooltip: The game's picture on an HDR screen, with brighter highlights. Applied when the game starts | The game in HDR, with brighter highlights | The game's picture on an HDR screen, with brighter highlights. Applied when the game starts *(unchanged)* | — | In-game overlay menu: HDR output (restart required). |
| **HDR source** · HDR | Line: Native: highlights as bright as they are in the game's scene. · Tooltip: Native: highlights as bright as they are in the game's scene. Expanded: the SDR picture's bright parts made brighter · Choices: Native (default); Expanded · Default: Native (default) | The scene's own highlights, or the SDR picture expanded | Native: highlights as bright as they are in the game's scene. Expanded: the SDR picture's bright parts made brighter *(unchanged)* | Native (default); Expanded *(unchanged)* | In-game overlay menu: HDR source. |
| **UI brightness** · HDR | Line: Native source only: how bright the HUD, menus and subtitles are… · Tooltip: Native source only: how bright the HUD, menus and subtitles are, against paper white · Choices: Dimmer; Paper white (default); Brighter; Much brighter · Default: Paper white (default) | How bright the HUD, menus and subtitles are | Native source only: how bright the HUD, menus and subtitles are, against paper white *(unchanged)* | Dimmer; Paper white (default); Brighter; Much brighter *(unchanged)* | In-game overlay menu: UI brightness. |
| **UI colour** · HDR | Line: Native source only: Vivid shows the HUD and menus with the… · Tooltip: Native source only: Vivid shows the HUD and menus with the colours a wide-gamut monitor gives SDR; Exact keeps sRGB's · Choices: Exact (sRGB); In between; Vivid (default) · Default: Vivid (default) | Vivid or exact (sRGB) colours for the HUD and menus | Native source only: Vivid shows the HUD and menus with the colours a wide-gamut monitor gives SDR; Exact keeps sRGB's *(unchanged)* | Exact (sRGB); In between; Vivid (default) *(unchanged)* | In-game overlay menu: UI colour. |
| **Paper white** · HDR | Line: How bright the game's white is, in nits. Range: 10–500 · Tooltip: How bright the game's white is, in nits. Range: 10–500 · Default: 200 | How bright the game's white is, in nits. Range: 10–500 *(unchanged)* | None (it would repeat the line) + *Default: 200* | — | In-game overlay menu: Paper white (nits). |
| **Peak brightness from the display** · HDR | Line: The peak Windows reports for the display the game is on (the… · Tooltip: The peak Windows reports for the display the game is on (the Windows HDR Calibration app's, once it has run). Off: the value below · Default: On | Takes the peak brightness Windows reports | The peak Windows reports for the display the game is on (the Windows HDR Calibration app's, once it has run). Off: the value below *(unchanged)* + *Default: On* | — | In-game overlay menu: Peak brightness from the display. |
| **Peak brightness** · HDR | Line: Your display's peak brightness, in nits, when it is not taken… · Tooltip: Your display's peak brightness, in nits, when it is not taken from the display. Range: 10–4000 · Default: 1000 | Your display's peak brightness, in nits | Your display's peak brightness, in nits, when it is not taken from the display. Range: 10–4000 *(unchanged)* + *Default: 1000* | — | In-game overlay menu: Peak brightness (nits). Greyed while Peak brightness from the display is on (as today). |
| **Highlight boost** · HDR | Line: Off keeps the game's own look… · Tooltip: Off keeps the game's own look; higher makes lamps, fire and the sky brighter · Choices: Off; Low (default); Medium; High · Default: Low (default) | How much brighter lamps, fire and the sky get | Off keeps the game's own look; higher makes lamps, fire and the sky brighter *(unchanged)* | Off; Low (default); Medium; High *(unchanged)* | In-game overlay menu: Highlight boost. |
| **Picture tuning** · Picture | Line: Applies the settings below, live… · Tooltip: Applies the settings below, live; off shows the game's own picture and keeps your values. Also in the game's System menu (Graphics, Effects, Colour) and the Insert menu | Your own brightness, colour, fog and bloom, live | Applies the settings below, live; off shows the game's own picture and keeps your values. Also in the game's System menu (Graphics, Effects, Colour) and the in-game overlay menu | — | Tooltip corrected. In-game overlay menu: Picture tuning. "The Insert menu" becomes the in-game overlay menu. |
| **Brightness** · Picture | — | — | 1.00 is the game's own | — | In-game overlay menu: Brightness. |
| **Contrast** · Picture | — | — | 1.00 is the game's own | — | In-game overlay menu: Contrast. |
| **Saturation** · Picture | — | — | 1.00 is the game's own | — | In-game overlay menu: Saturation. |
| **Hue** · Picture | — | — | 0 is the game's own | — | In-game overlay menu: Hue. |
| **Fog** · Picture | — | How thick the fog is | 1.00 is the game's own | — | In-game overlay menu: Fog. |
| **Bloom** · Picture | — | How strong the glow around bright lights is | 1.00 is the game's own | — | In-game overlay menu: Bloom. |
| **Vignette** · Picture | Default: On | Darkens the edges of the screen, as the game does | Off: the picture stays even to the edges + *Default: On* | — | In-game overlay menu: Vignette. |
| **Ambient occlusion strength** · Picture | — | How dark the shading in corners is | Needs Ambient occlusion on (Graphics › Game effects). 1.00 is the game's own | — | In-game overlay menu: Ambient occlusion strength. |
| **Shadow distance** · Picture | — | How far away shadows are drawn | 1.00 is the game's own | — | In-game overlay menu: Shadow distance. |
| **Game's own picture** · Picture | Line: Puts the settings above back to the game's own values · Tooltip: Puts the settings above back to the game's own values | Puts the settings above back to the game's own values *(unchanged)* | None: it would repeat the line | — | In-game overlay menu: Game's own picture. |

## Graphics

Decided: the Detail and Effects groups become two groups by where each setting comes from, in the launcher and the in-game overlay menu. **Game effects**: what the game has and a switch leaves out (depth of field, motion blur with its upscaler fix under it, chromatic aberration, ambient occlusion, shadows from dynamic lights); caption "The game's own effects. Off leaves one out; applied when the game starts". **Enhancements**: what goes past the game, or below it for weaker GPUs (texture filtering, model detail, screen-space reflections); caption "Model detail and texture filtering above or below the game's, and reflections it doesn't have. Applied when the game starts". Today both groups say "Patches to the game, applied when it starts". Two effects are on in the original game and off here by default (chromatic aberration, motion blur); their tooltips now say so.

| Setting | Today | Proposed line | Proposed tooltip | Proposed choices | Notes |
| --- | --- | --- | --- | --- | --- |
| **Suggested settings for this PC** (was Best settings for this PC) | Line: Output resolution, frame rate, upscaler and quality for this… · Tooltip: Output resolution, frame rate, upscaler and quality for this PC, then this PC's hardware summary | Resolution, frame rate and upscaler from this PC's hardware | Output resolution, frame rate, upscaler and quality worked out from this PC's graphics card, processor and the game's monitor: a starting point, not a measurement. Every change is listed before it applies | — | Tooltip corrected. Renamed (the tester's call): a rule of thumb from the hardware, not a measured best. Its button "Recommend…" becomes "Suggest…". "The game's monitor" holds once the suggestion reads the chosen monitor (Changes outside the texts). |
| **Anti-aliasing** · Upscaling and anti-aliasing | Line: Smooths edges when no upscaler runs · Tooltip: Smooths edges when no upscaler runs. Off: the sharpest, with jagged edges. The game's own: its PS4 pass, after a restart. TAA: smooth edges, live. · Choices: Off; The game's own (Applied when the game starts); TAA · Status: With an upscaler the box shows "Upscaler (DLSS)" (or AMD FSR), greyed | Smooths edges when no upscaler runs *(unchanged)* | Smooths edges when no upscaler runs. Off: the sharpest, with jagged edges. The game's own: its PS4 pass, after a restart. TAA: smooth edges, live. *(unchanged)* | Off; The game's own (Applied when the game starts); TAA *(unchanged)* | In-game overlay menu: Anti-aliasing. |
| **Upscaler** · Upscaling and anti-aliasing | Line: Renders below the output and fills it in · Tooltip: Renders below the output and fills it in. DLSS needs an NVIDIA RTX card; AMD FSR runs on any GPU. Both anti-alias as they upscale. · Choices: None; AMD FSR; DLSS (NVIDIA RTX) · Default: None · Status: Status line when an upscaler is unavailable on this PC | Renders below the output and fills it in *(unchanged)* | Renders below the output and fills it in. DLSS needs an NVIDIA RTX card; AMD FSR runs on any GPU. Both anti-alias as they upscale. *(unchanged)* + *Default: None* | None; AMD FSR; DLSS (NVIDIA RTX) *(unchanged)* | In-game overlay menu: Upscaler. |
| **Upscaling quality** (was Quality) · Upscaling and anti-aliasing | Line: How far below the output the game renders… · Tooltip: How far below the output the game renders: lower is faster and softer. Auto: Native AA up to 1200 lines, Quality up to 1600, Performance above | How far below the output the game renders | How far below the output the game renders: lower is faster and softer. Auto: Native AA up to 1200 lines, Quality up to 1600, Performance above *(unchanged)* | — | In-game overlay menu: Quality (rename there too). Shown only with an upscaler; "Quality" alone reads as overall graphics quality. The tester's call. |
| **Render resolution** · Upscaling and anti-aliasing | Line: The share of the output's width and height the game renders at. · Tooltip: The share of the output's width and height the game renders at. DLSS runs the nearest quality's model | The share of the output's width and height the game renders at *(unchanged)* | The share of the output's width and height the game renders at. DLSS runs the nearest quality's model *(unchanged)* | — | In-game overlay menu: Render resolution. |
| **FSR version** · Upscaling and anti-aliasing | Line: The versions AMD offers on this GPU; Automatic runs the newest · Tooltip: The versions AMD offers on this GPU; Automatic runs the newest. Newer is usually better. (driver) marks one AMD's graphics driver provides. | The versions AMD offers on this GPU; Automatic runs the newest *(unchanged)* | The versions AMD offers on this GPU; Automatic runs the newest. Newer is usually better. (driver) marks one AMD's graphics driver provides. *(unchanged)* | — | In-game overlay menu: FSR version. |
| **DLSS model** · Upscaling and anti-aliasing | Line: Which NVIDIA model DLSS runs; also in game (Insert or L3 + R3) · Tooltip: Which NVIDIA model DLSS runs; also in game (Insert or L3 + R3). Auto: K from DLAA to Balanced, M at Performance, L at Ultra Performance. · Choices: Auto (per quality) (NVIDIA's DLSS 4.5 pairing: K for DLAA, Quality and Balanced, M for Performance, L for Ultra Performance); Transformer (preset K) (The newer model: steadier thin and moving objects (a weapon on the back, railings)); Transformer 2 (preset M) (DLSS 4.5's model for Performance: sharper, a little heavier); Transformer 2 (preset L) (DLSS 4.5's model for Ultra Performance); Transformer (preset J) (Like K, slightly less ghosting and a little more flicker); The DLL's default (Whatever the included DLSS version picks for each quality mode) · Default: Auto (per quality) · Status: Status line with Auto: "Auto: model K at this quality" (the model Auto picks) | Which NVIDIA model DLSS runs; also in game (Insert or L3 + R3) *(unchanged)* | Which NVIDIA model DLSS runs; also in game (Insert or L3 + R3). Auto: K from DLAA to Balanced, M at Performance, L at Ultra Performance. *(unchanged)* | Auto (per quality) (NVIDIA's DLSS 4.5 pairing: K for DLAA, Quality and Balanced, M for Performance, L for Ultra Performance); Transformer (preset K) (The newer model: steadier thin and moving objects (a weapon on the back, railings)); Transformer 2 (preset M) (DLSS 4.5's model for Performance: sharper, a little heavier); Transformer 2 (preset L) (DLSS 4.5's model for Ultra Performance); Transformer (preset J) (Like K, slightly less ghosting and a little more flicker); The DLL's default (Whatever the included DLSS version picks for each quality mode) *(unchanged)* | In-game overlay menu: DLSS model. |
| **Sharpening (RCAS)** · Upscaling and anti-aliasing | Tooltip: AMD's RCAS. Brings back detail that upscaling softens. · Default: On · Status: Line set by the upscaler: DLSS "After DLSS (it has none of its own)", AMD FSR "FSR's own sharpening", TAA "After TAA", none "Needs an upscaler or TAA" | Line set by the upscaler: DLSS "After DLSS (it has none of its own)", AMD FSR "FSR's own sharpening", TAA "After TAA", none "Needs an upscaler or TAA" *(unchanged)* | AMD's RCAS. Brings back detail that upscaling softens. *(unchanged)* + *Default: On* | — | In-game overlay menu: Sharpening (RCAS). Its line already changes with the upscaler. |
| **Sharpening strength** · Upscaling and anti-aliasing | Tooltip: 0.20 is light. High values can halo edges and show grain. · Default: 0.20 · Status: Line set by the upscaler: AMD FSR "Above 1 adds a second pass", otherwise "Higher is sharper" | Line set by the upscaler: AMD FSR "Above 1 adds a second pass", otherwise "Higher is sharper" *(unchanged)* | 0.20 is light. High values can halo edges and show grain. *(unchanged)* + *Default: 0.20* | — | In-game overlay menu: Sharpness (rename there to Sharpening strength). |
| **Character motion vectors** · Advanced | Line: Less ghosting on characters; costs about 10% FPS · Tooltip: Less ghosting on characters; costs about 10% FPS. The game gives no motion for moving characters; this works it out. Off: faster, but moving enemies can trail. After a restart. · Default: On | Less ghosting on characters; costs about 10% FPS *(unchanged)* | Less ghosting on characters; costs about 10% FPS. The game gives no motion for moving characters; this works it out. Off: faster, but moving enemies can trail. After a restart. *(unchanged)* + *Default: On* | — | In-game overlay menu: Character motion vectors. |
| **Texture mip bias** · Advanced | Line: Below 0 sharper textures, above 0 softer · Tooltip: Below 0 sharper textures, above 0 softer. 0 is what NVIDIA and AMD advise with an upscaler. Below 0 can shimmer. · Default: 0 | Below 0 sharper textures, above 0 softer *(unchanged)* | Below 0 sharper textures, above 0 softer. 0 is what NVIDIA and AMD advise with an upscaler. Below 0 can shimmer. *(unchanged)* + *Default: 0* | — | In-game overlay menu: Texture mip bias. |
| **Model detail** · Detail | Choices: As in the game; Highest (-2); Lower (1); Lowest (2) · Default: As in the game | How far away models keep their full detail | Highest costs GPU time; Lower and Lowest help weaker GPUs + *Default: As in the game* | Lowest (2); Lower (1); As in the game; Highest (-2) | In-game overlay menu: Model detail (in-game: to an Enhancements section). Group: Enhancements. |
| **Texture filtering** · Detail | Line: Anisotropic filtering forced on the game's textures… · Tooltip: Anisotropic filtering forced on the game's textures: ground and walls seen at an angle stay sharp. Lower values cost a little less GPU time; Game's own leaves the game's filtering as it is. BB_ANISO, default: 16x. Applied when the game starts · Choices: 16x; 8x; 4x; 2x; Game's own · Default: 16x | Sharper ground and walls seen at an angle | Anisotropic filtering forced on the game's textures: ground and walls seen at an angle stay sharp. Lower values cost a little less GPU time; Game's own leaves the game's filtering as it is. BB_ANISO, default: 16x. Applied when the game starts *(unchanged)* | Game's own; 2x; 4x; 8x; 16x | Group: Enhancements. |
| **Ambient occlusion (SSAO)** · Detail | Default: On | Soft shading in corners and creases | *Default: On* | — | In-game overlay menu: Ambient occlusion (SSAO). Group: Game effects. |
| **Shadows from dynamic lights** · Detail | Default: On | Shadows cast by lamps and other point lights; heavy | One of the heaviest effects: off gives a large FPS gain + *Default: On* | — | In-game overlay menu: Shadows from dynamic lights. Group: Game effects. |
| **Screen-space reflections** · Detail | Line: Not in the original game · Tooltip: Not in the original game · Default: Off | Adds reflections on wet and shiny surfaces | Not in the original game: Kyo's patch turns on reflections the game has only in its debug functions. Costs GPU time + *Default: Off* | — | In-game overlay menu: Screen-space reflections (in-game: to an Enhancements section). Group: Enhancements. |
| **Chromatic aberration** · Effects | Default: Off | Colour fringes toward the screen's edges | On in the original game; off here by default, for a clearer picture | — | In-game overlay menu: Chromatic aberration. Group: Game effects. |
| **Depth of field** · Effects | Default: On | Blur on the parts of the picture out of focus | *Default: On* | — | In-game overlay menu: Depth of field (DoF). Group: Game effects. |
| **Motion blur** · Effects | Default: Off | Blur when the camera turns fast | On in the original game; off here by default, for a clearer picture. Costs GPU time | — | In-game overlay menu: Motion blur. Group: Game effects. |
| **No motion blur smear with upscalers** · Effects | Line: With an upscaler at an output other than 1920x1080 the game's… · Tooltip: With an upscaler at an output other than 1920x1080 the game's motion blur is left out: the upscaler would smear it over the whole picture. Off (default): the blur stays. The Motion blur setting is not changed. BB_UPSCALER_NO_BLUR, default: off. Applied when the game starts · Default: off | Leaves out motion blur an upscaler would smear | With an upscaler at an output other than 1920x1080 the game's motion blur is left out: the upscaler would smear it over the whole picture. Off (default): the blur stays. The Motion blur setting is not changed. BB_UPSCALER_NO_BLUR, default: off. Applied when the game starts *(unchanged)* | — | Group: Game effects, under Motion blur as today (a fix of Hunter's Workshop's for upscalers). |

## Interface

| Setting | Today | Proposed line | Proposed tooltip | Proposed choices | Notes |
| --- | --- | --- | --- | --- | --- |
| **In-game menu shortcut** · Menus and HUD | Line: What opens the port's settings menu while playing. · Tooltip: What opens the port's settings menu while playing. Applied on the next launch · Choices: Off (The menu never opens; settings stay in the launcher and the game's PC menus); Insert only (Pressing both sticks in a fight no longer opens it); Insert or L3 + R3 (The keyboard's Insert key, or both sticks pressed on a controller) · Default: Insert or L3 + R3 | What opens the in-game overlay menu | What opens the in-game overlay menu while playing. Applied on the next launch + *Default: Insert or L3 + R3* | Off (The menu never opens; settings stay in the launcher and the game's PC menus); Insert only (Pressing both sticks in a fight no longer opens it); Insert or L3 + R3 (The keyboard's Insert key, or both sticks pressed on a controller) | Tooltip corrected. "The port's settings menu" becomes "the in-game overlay menu" (the tester's term). |
| **In-game menu language** · Menus and HUD | Line: The port's settings menu (Insert or L3 + R3) · Tooltip: The port's settings menu (Insert or L3 + R3) · Choices: 简体中文; English; Español; Português (Brasil); Français; Deutsch; Italiano; Polski; Türkçe; Русский · Default: English | The in-game overlay menu's language | None: the line says it all + *Default: English* | 简体中文; English; Español; Português (Brasil); Français; Deutsch; Italiano; Polski; Türkçe; Русский *(unchanged)* |  |
| **Sharp UI** · Menus and HUD | Line: Menus, HUD and text drawn at the output resolution instead of… · Tooltip: Menus, HUD and text drawn at the output resolution instead of 1080p stretched up: much sharper at 1440p and 4K. New: on by default in betas while it is tested. Applied on the next launch · Default: On in betas, off in stable releases | Menus, HUD and text drawn at the output resolution | Menus, HUD and text drawn at the output resolution instead of 1080p stretched up: much sharper at 1440p and 4K. New: on by default in betas while it is tested. Applied on the next launch *(unchanged)* | — |  |
| **Note while shaders build** · Menus and HUD | Line: "Building shaders" in a corner while the game builds shaders for… · Tooltip: "Building shaders" in a corner while the game builds shaders for a place it shows for the first time. Also in game: Insert or L3 + R3 · Default: On | Shows "Building shaders" while new shaders compile | "Building shaders" in a corner while the game builds shaders for a place it shows for the first time. Also in game: Insert or L3 + R3 *(unchanged)* + *Default: On* | — | In-game overlay menu: Note while shaders build. |
| **FPS counter** · FPS counter | Line: Also in game: Insert or L3 + R3 · Tooltip: Also in game: Insert or L3 + R3 · Default: Off | Frame rate and timings in a corner of the game | Also in game: Insert or L3 + R3 *(unchanged)* + *Default: Off* | — | In-game overlay menu: FPS counter. |
| **Frame rate** · FPS counter | Line: Shown in the FPS counter · Tooltip: Shown in the FPS counter · Default: On | — | *Default: On* | — | In-game overlay menu: Frame rate. "Shown in the FPS counter" goes from the five items: they sit in its fold. |
| **Frame time** · FPS counter | Line: Shown in the FPS counter · Tooltip: Shown in the FPS counter · Default: On | — | *Default: On* | — | In-game overlay menu: Frame time. |
| **Upscaler** · FPS counter | Line: Shown in the FPS counter · Tooltip: Shown in the FPS counter · Default: On | — | *Default: On* | — | In-game overlay menu: Upscaler. |
| **CPU usage** · FPS counter | Line: Shown in the FPS counter · Tooltip: Shown in the FPS counter · Default: Off | — | *Default: Off* | — | In-game overlay menu: CPU usage. |
| **GPU usage** · FPS counter | Line: Shown in the FPS counter · Tooltip: Shown in the FPS counter · Default: Off | — | *Default: Off* | — | In-game overlay menu: GPU usage. |
| **FPS counter position** · FPS counter | Choices: Top right; Top left; Bottom right; Bottom left; Top centre · Default: Top right | — | *Default: Top right* | Top right; Top left; Bottom right; Bottom left; Top centre *(unchanged)* | In-game overlay menu: Position. |
| **FPS counter size** · FPS counter | Default: 1.00 | — | *Default: 1.00* | — | In-game overlay menu: Size. |
| **FPS counter text colour** · FPS counter | Line: Any colour in game: Insert or L3 + R3 › Other · Tooltip: Any colour in game: Insert or L3 + R3 › Other · Choices: White; Yellow; Green; Cyan; Red; Orange; Pink; Black · Default: White | Any colour in game: Insert or L3 + R3 › Other *(unchanged)* | None (it would repeat the line) + *Default: White* | White; Yellow; Green; Cyan; Red; Orange; Pink; Black *(unchanged)* | In-game overlay menu: Text colour. |
| **FPS counter text opacity** · FPS counter | Default: 1.00 | — | *Default: 1.00* | — | In-game overlay menu: Text opacity. |
| **FPS counter background** · FPS counter | Choices: Dark grey; Black; Dark red; Dark blue; White · Default: Dark grey | — | *Default: Dark grey* | Dark grey; Black; Dark red; Dark blue; White *(unchanged)* | In-game overlay menu: Background colour. |
| **FPS counter background opacity** · FPS counter | Default: 0.50 | — | *Default: 0.50* | — | In-game overlay menu: Background opacity. |
| **FPS counter border** · FPS counter | Default: On | — | *Default: On* | — | In-game overlay menu: Border. |

## Gameplay

| Setting | Today | Proposed line | Proposed tooltip | Proposed choices | Notes |
| --- | --- | --- | --- | --- | --- |
| **Follow camera** · Camera | Line: Original keeps the game's camera distance, angle and follow… · Tooltip: Original keeps the game's camera distance, angle and follow speed at 60 FPS and above · Choices: Original (the game's camera) (As the game has it, at any frame rate); FPS++ retune (Kyo's 60 FPS++ camera: further back and slower to follow) · Default: Original (the game's camera) | How the camera follows you at 60 FPS and above | Original keeps the game's camera distance, angle and follow speed at 60 FPS and above *(unchanged)* + *Default: Original (the game's camera)* | Original (the game's camera) (As the game has it, at any frame rate); FPS++ retune (Kyo's 60 FPS++ camera: further back and slower to follow) *(unchanged)* |  |
| **Camera tuning** · Camera | Line: Applies the three settings below. · Tooltip: Applies the three settings below. Also in the game's System menu (Camera Tuning) | Your own field of view, distance and height | Applies the three settings below. Also in the game's System menu (Camera Tuning) *(unchanged)* | — | In-game overlay menu: Camera tuning. |
| **Field of view** · Camera | — | — | 1.00 is the game's own | — | In-game overlay menu: Camera FOV scale. |
| **Distance** · Camera | — | — | 1.00 is the game's own | — | In-game overlay menu: Camera distance scale. |
| **Height** · Camera | — | — | 1.00 is the game's own | — | In-game overlay menu: Camera height scale. |
| **Skip intro videos** · Extras | Default: On | — | *Default: On* | — |  |
| **Free camera** · Extras | Line: Toggle with Cross + L3 / Space + Z · Tooltip: Toggle with Cross + L3 / Space + Z · Default: Off | Toggle with Cross + L3 / Space + Z *(unchanged)* | None (it would repeat the line) + *Default: Off* | — |  |
| **Debug menu** · Extras | Line: Left touchpad / Tab. Needs the adhoc folder from Nexus mod #253… · Tooltip: Left touchpad / Tab. Needs the adhoc folder from Nexus mod #253 (adhoc/font) in the game's dvdroot_ps4 or as a mod; without it the patch is not applied · Default: Off | The game's developer menu (Left touchpad / Tab) | Left touchpad / Tab. Needs the adhoc folder from Nexus mod #253 (adhoc/font) in the game's dvdroot_ps4 or as a mod; without it the patch is not applied *(unchanged)* + *Default: Off* | — |  |
| **Hunter's Dream mirror** · Extras | Line: The Dream's mirror opens the appearance editor (the game's… · Tooltip: The Dream's mirror opens the appearance editor (the game's unused "Put on Disguise"). Applied when the game starts | The Dream's mirror opens the appearance editor | The Dream's mirror opens the appearance editor (the game's unused "Put on Disguise"). Applied when the game starts *(unchanged)* | — |  |
| **Rebirth at the altar** · Extras | Line: With the Yharnam Stone, the Altar of Despair resets your level… · Tooltip: With the Yharnam Stone, the Altar of Despair resets your level and attributes. Applied when the game starts | Reset your level and attributes at the Altar of Despair | With the Yharnam Stone, the Altar of Despair resets your level and attributes. Applied when the game starts *(unchanged)* | — | The tester's line. |

## Controls

| Setting | Today | Proposed line | Proposed tooltip | Proposed choices | Notes |
| --- | --- | --- | --- | --- | --- |
| **PC controls** · Keyboard & mouse | Line: The keyboard and mouse layout below… · Tooltip: The keyboard and mouse layout below; off keeps the gamepad-style keys · Default: On | Use the keyboard and mouse layout below | The keyboard and mouse layout below; off keeps the gamepad-style keys *(unchanged)* + *Default: On* | — | The tester's line. |
| **Mouse camera** · Keyboard & mouse | Line: The mouse turns the game's own camera by raw counts, at any… · Tooltip: The mouse turns the game's own camera by raw counts, at any frame rate · Default: On | The mouse turns the game's camera by raw counts | The mouse turns the game's own camera by raw counts, at any frame rate *(unchanged)* + *Default: On* | — |  |
| **Mouse in menus** · Keyboard & mouse | Line: The mouse becomes a pointer in the game's menus… · Tooltip: The mouse becomes a pointer in the game's menus: hover and click items · Default: On | The mouse becomes a pointer in the game's menus | The mouse becomes a pointer in the game's menus: hover and click items *(unchanged)* + *Default: On* | — |  |
| **Move and look in menus** · Keyboard & mouse | Line: With a menu open in the world, the movement and look keys stay… · Tooltip: With a menu open in the world, the movement and look keys stay your character's and the arrow keys move through the menu. The mouse turns the camera while Mouse in menus is off. Whether the character can walk then is up to the game. Applied at the next start · Default: Off | Keep moving and looking with a menu open | With a menu open in the world, the movement and look keys stay your character's and the arrow keys move through the menu. The mouse turns the camera while Mouse in menus is off. Whether the character can walk then is up to the game. Applied at the next start *(unchanged)* + *Default: Off* | — |  |
| **Mouse sensitivity** · Keyboard & mouse | Line: 5 is the default; below 0 for high-DPI mice (each step 20%… · Tooltip: 5 is the default; below 0 for high-DPI mice (each step 20% slower) · Default: 5 | 5 is the default; below 0 for high-DPI mice | 5 is the default; below 0 for high-DPI mice (each step 20% slower) *(unchanged)* | — |  |
| **Invert horizontal** · Keyboard & mouse | — | Moving the mouse right turns the camera left | None: the line says it all | — |  |
| **Invert vertical** · Keyboard & mouse | — | Moving the mouse up looks down | None: the line says it all | — |  |
| **Camera auto-rotation** · Keyboard & mouse | Line: Let the camera turn by itself with movement, as with a gamepad · Tooltip: Let the camera turn by itself with movement, as with a gamepad | Let the camera turn by itself with movement, as with a gamepad *(unchanged)* | None: it would repeat the line | — |  |
| **Hide mouse cursor** · Keyboard & mouse | Line: Hidden 2 seconds after the mouse stops moving… · Tooltip: Hidden 2 seconds after the mouse stops moving; moving it brings it back · Default: On | Hidden 2 seconds after the mouse stops moving | Hidden 2 seconds after the mouse stops moving; moving it brings it back *(unchanged)* + *Default: On* | — | In-game overlay menu: Hide mouse cursor. |
| **Controller** · Gamepad | — | — | — | — |  |
| **Swap sticks** · Gamepad | Line: Move with the right stick and look with the left · Tooltip: Move with the right stick and look with the left | Move with the right stick and look with the left *(unchanged)* | None: it would repeat the line | — |  |
| **Invert camera left / right** · Gamepad | Line: The camera stick's horizontal direction · Tooltip: The camera stick's horizontal direction | Pushing the stick right turns the camera left | None: the line says it all | — | Today's line reads either way; the line now says it all, as the mouse's invert rows. |
| **Invert camera up / down** · Gamepad | Line: The camera stick's vertical direction · Tooltip: The camera stick's vertical direction | Pushing the stick up looks down | None: the line says it all | — |  |
| **Stick dead zone** · Gamepad | Line: Ignores small stick movement, for sticks that drift. · Tooltip: Ignores small stick movement, for sticks that drift. 0 = off (the game's own) · Default: 0 | Ignores small stick movement, for sticks that drift | Ignores small stick movement, for sticks that drift. 0 = off (the game's own) *(unchanged)* + *Default: 0* | — |  |
| **Preferred gamepad** · Gamepad | Line: Part of a controller's name (DualSense, Xbox, 8BitDo...) or its… · Tooltip: Part of a controller's name (DualSense, Xbox, 8BitDo...) or its SDL GUID: that one is used when several are connected. Empty: the pad you use takes over. BB_GAMEPAD, default: not set. Applied when the game starts · Default: not set | The controller to use when several are connected | Part of a controller's name (DualSense, Xbox, 8BitDo...) or its SDL GUID: that one is used when several are connected. Empty: the pad you use takes over. BB_GAMEPAD, default: not set. Applied when the game starts *(unchanged)* | — |  |
| **Button prompts** · Button icons | Line: The button icons in the game's menus and prompts. · Tooltip: The button icons in the game's menus and prompts. Automatic shows your keys while the keyboard or mouse is in use, and the controller's buttons with a controller; also in-game under System › PC Controls · Choices: Automatic (default); Keyboard; Controller · Default: Automatic (default) | Keys or controller buttons in the game's prompts | The button icons in the game's menus and prompts. Automatic shows your keys while the keyboard or mouse is in use, and the controller's buttons with a controller; also in-game under System › PC Controls *(unchanged)* | Automatic (default); Keyboard; Controller *(unchanged)* | Today the two button rows read almost the same. |
| **Controller icons** · Button icons | Line: The controller's buttons in the game's menus and prompts. · Tooltip: The controller's buttons in the game's menus and prompts. Automatic shows PlayStation buttons with a PlayStation controller and Xbox buttons (A, B, X, Y, LB, RT…) with any other; rebound buttons show as bound. Also in-game under System › PC Controls · Choices: Automatic (default); PlayStation; Xbox · Default: Automatic (default) | PlayStation or Xbox buttons in the game's prompts | The controller's buttons in the game's menus and prompts. Automatic shows PlayStation buttons with a PlayStation controller and Xbox buttons (A, B, X, Y, LB, RT…) with any other; rebound buttons show as bound. Also in-game under System › PC Controls *(unchanged)* | Automatic (default); PlayStation; Xbox *(unchanged)* |  |
| **Profile** · Profiles | — | — | — | — |  |
| **Save and share** · Profiles | Line: Profiles are small JSON files. · Tooltip: Profiles are small JSON files. Copy one and paste it on any paste site (Pastebin, GitHub Gist, Rentry, hastebin…) to share it; others import it from the link. | Share a profile as a link or a file | Profiles are small JSON files. Copy one and paste it on any paste site (Pastebin, GitHub Gist, Rentry, hastebin…) to share it; others import it from the link. *(unchanged)* | — |  |

## Engine switches

Rows with a BB_ name already end their tooltip with "BB_X, default: …. Applied when the game starts"; that stays. Decided here: Unified frame time joins Frame rate corrections (it only works with them on), Fixed-refresh frame lock greys while Precise frame pacing is off, No shader freezes becomes Reduce shader stalls, and Changed switches also lists what Extra variables override (Changes outside the texts).

| Setting | Today | Proposed line | Proposed tooltip | Proposed choices | Notes |
| --- | --- | --- | --- | --- | --- |
| **Changed switches** | Line: Every switch on this page that is not at its default · Tooltip: Every switch on this page that is not at its default | Every switch on this page that is not at its default *(unchanged)* | None: it would repeat the line | — | Will also list what Extra variables override, at a default or not (Changes outside the texts). |
| **Extra variables** | Line: NAME=value pairs separated by spaces, passed to the game. · Tooltip: NAME=value pairs separated by spaces, passed to the game. For anything this page doesn't list; a variable set here wins over its switch | NAME=value pairs passed to the game | NAME=value pairs separated by spaces, passed to the game. For anything this page doesn't list; a variable set here wins over its switch *(unchanged)* | — |  |
| **Legacy shader compiler** · Experimental engine features | Line: Off by default: the native compiler reads constant data through… · Tooltip: Off by default: the native compiler reads constant data through the GPU's constant cache. On compiles the game's shaders the way 0.9.33 did: try it if something draws wrong. Switching recompiles every shader once · Default: Off | Compiles shaders the old way, if something draws wrong | Off by default: the native compiler reads constant data through the GPU's constant cache. On compiles the game's shaders the way earlier versions did: try it if something draws wrong. Switching recompiles every shader once | — | Tooltip corrected. "The way 0.9.33 did" becomes "the way earlier versions did". |
| **VRAM cleanup** · Experimental engine features | Line: Frees video memory held by textures the GPU drew, so long… · Tooltip: Frees video memory held by textures the GPU drew, so long sessions don't fill up VRAM and slow down. Recommended: on · Default: On | Frees VRAM held by textures the GPU drew | Frees video memory held by textures the GPU drew, so long sessions don't fill up VRAM and slow down. Recommended: on *(unchanged)* + *Default: On* | — | "Recommended: on" stays. |
| **Precise frame pacing** · Experimental engine features | Line: A high-resolution timer and even spacing between frames… · Tooltip: A high-resolution timer and even spacing between frames: steadier frame times and less VRR flicker. Uses a little CPU. On by default · Default: On | Steadier frame times and less VRR flicker | A high-resolution timer and even spacing between frames: steadier frame times and less VRR flicker. Uses a little CPU. On by default *(unchanged)* | — |  |
| **Fixed-refresh frame lock** · Experimental engine features | Line: Only for monitors without G-Sync or FreeSync (VRR). · Tooltip: Only for monitors without G-Sync or FreeSync (VRR). Each frame is shown right after one of the monitor's refreshes when the FPS limit divides its refresh rate (60 FPS on 60, 120 or 240 Hz). Needs Precise frame pacing. With G-Sync or FreeSync it stutters: leave it off. Off by default · Default: Off | Only for monitors without G-Sync or FreeSync (VRR) *(unchanged)* | Only for monitors without G-Sync or FreeSync (VRR). Each frame is shown right after one of the monitor's refreshes when the FPS limit divides its refresh rate (60 FPS on 60, 120 or 240 Hz). Needs Precise frame pacing. With G-Sync or FreeSync it stutters: leave it off. Off by default *(unchanged)* | — | Greyed while Precise frame pacing is off (the tester's call); its own value is kept. |
| **Particle work safety cap** · Experimental engine features | Line: Checks the size of the game's GPU particle work before the… · Tooltip: Checks the size of the game's GPU particle work before the graphics card runs it, and caps it to what the card supports. Only for games that crash with "device lost" in effect-heavy scenes. It costs smoothness in busy scenes. Off by default · Default: Off | For "device lost" crashes in effect-heavy scenes | Checks the size of the game's GPU particle work before the graphics card runs it, and caps it to what the card supports. Only if the game crashes with "device lost" in effect-heavy scenes. It costs smoothness in busy scenes. Off by default | — | Tooltip corrected. "Only for games that crash" becomes "Only if the game crashes". |
| **Use more VRAM for textures** · Experimental engine features | Line: Keeps textures in video memory up to 80% of the card instead of… · Tooltip: Keeps textures in video memory up to 80% of the card instead of about 40%, so they aren't thrown out and loaded again (the cause of most stutters on 8 GB cards) · Default: On | Up to 80% of VRAM for textures, for less stutter | Keeps textures in video memory up to 80% of the card instead of about 40%, so they aren't thrown out and loaded again (the cause of most stutters on 8 GB cards) *(unchanged)* + *Default: On* | — |  |
| **Graphics threads first** · Experimental engine features | Line: Runs the engine's graphics threads at a higher CPU priority than… · Tooltip: Runs the engine's graphics threads at a higher CPU priority than the game's other threads, so a frame isn't held up behind them. On by default · Default: On | Graphics threads at a higher CPU priority | Runs the engine's graphics threads at a higher CPU priority than the game's other threads, so a frame isn't held up behind them. On by default *(unchanged)* | — |  |
| **Shorter sleep spin** · Experimental engine features | Line: The game's many short sleeps busy-wait only as long as the timer… · Tooltip: The game's many short sleeps busy-wait only as long as the timer needs (about 0.2–0.4 ms instead of 0.6 ms), leaving that CPU time to the graphics threads. On by default · Default: On | Leaves more CPU time to the graphics threads | The game's many short sleeps busy-wait only as long as the timer needs (about 0.2–0.4 ms instead of 0.6 ms), leaving that CPU time to the graphics threads. On by default *(unchanged)* | — |  |
| **Background shader compiling** · Experimental engine features | Line: New shaders compile on helper threads instead of freezing the… · Tooltip: New shaders compile on helper threads instead of freezing the frame: much shorter hitches when you reach a new area. On by default · Default: On | Much shorter hitches when you reach a new area | New shaders compile on helper threads instead of freezing the frame: much shorter hitches when you reach a new area. On by default *(unchanged)* | — |  |
| **Occlusion culling** · Experimental engine features | Line: Off by default. The game skips objects hidden behind walls… · Tooltip: Off by default. The game skips objects hidden behind walls: higher FPS in dense areas, but objects and floors can appear late or flicker · Default: Off | Skips objects hidden behind walls: more FPS, some pop-in | Off by default. The game skips objects hidden behind walls: higher FPS in dense areas, but objects and floors can appear late or flicker *(unchanged)* | — |  |
| **Faster draw thread** · Experimental engine features | Line: Remembers texture and sampler lookups and hands the end of each… · Tooltip: Remembers texture and sampler lookups and hands the end of each frame to the draw thread: higher FPS in busy areas. New: turn it off if the game hangs or crashes · Default: On | Higher FPS in busy areas | Remembers texture and sampler lookups and hands the end of each frame to the draw thread: higher FPS in busy areas. New: turn it off if the game hangs or crashes *(unchanged)* + *Default: On* | — |  |
| **Reduce shader stalls** (was No shader freezes) · Experimental engine features | Line: While a new shader compiles, the objects that need it appear a… · Tooltip: While a new shader compiles, the objects that need it appear a moment later instead of the game freezing. New: turn it off if something stays missing · Default: On | Some objects appear late while new shaders compile | While a new shader compiles, the scene objects that need it are left out for up to a second instead of the game waiting, and the draw thread goes on with the rest of the frame. Shadow maps, half-resolution effects, post-processing and the UI still wait for it, so some waits remain. New: turn it off if something stays missing + *Default: On* | — | Tooltip corrected. "No shader freezes" promised more than it does (engine/docs/pipeline_compiles.md: only scene geometry is left out, for at most 1 s). Same setting and stored key. |
| **Fast loading** · Experimental engine features | Line: Above 60 FPS, loading screens run at 60 so more of the CPU goes… · Tooltip: Above 60 FPS, loading screens run at 60 so more of the CPU goes to the loading itself. Does nothing at 30 or 60 FPS, or before a cutscene. On by default · Default: On | Faster loading screens above 60 FPS | Above 60 FPS, loading screens run at 60 so more of the CPU goes to the loading itself. Does nothing at 30 or 60 FPS, or before a cutscene. On by default *(unchanged)* | — |  |
| **Frame rate corrections** · Experimental engine features | Line: At 60 FPS and above, ragdolls, physics, cloth, jumps, slopes… · Tooltip: At 60 FPS and above, ragdolls, physics, cloth, jumps, slopes, sprinting, attack sounds, online movement and timers follow the real frame time, and the timings the FPS++ patches halved or doubled (menu repeat, hit lifetimes, online timers) are the game's own again. Off: the older FPS++ patches above 60. On by default · Default: On | Physics, cloth, timers and camera correct at 60 FPS and up | At 60 FPS and above, ragdolls, physics, cloth, jumps, slopes, sprinting, attack sounds, online movement, timers and the follow camera follow the real frame time: the frame's own, the same for the game and every correction, so after a stutter they catch up at once. The timings the FPS++ patches halved or doubled (menu repeat, hit lifetimes, online timers) are the game's own again. Off: the older FPS++ patches above 60. On by default | — | Tooltip corrected. Takes in Unified frame time (the tester's call): that half only works with the corrections on, and every log since 0.3.0 ran with it. |
| **Unified frame time** · Experimental engine features | Line: Needs Frame rate corrections. · Tooltip: Needs Frame rate corrections. The game and every correction use one frame time, the frame's own, in the same frame, instead of the corrections following a smoothed copy one frame late: after a stutter, ragdolls, physics, jumps, timers and the camera's easing catch up at once rather than over 10 to 15 frames. On by default · Default: On | Row removed: Merged into Frame rate corrections | — | — | Its value goes (a player who turned it off gets it back on). For a test, BB_FRAME_FIXES_SKIP=timer,camera in Extra variables still runs the corrections on the old smoothed, one-frame-late copy. |
| **Fewer memory write checks** · Experimental engine features | Line: Memory the game rewrites before almost every use is no longer… · Tooltip: Memory the game rewrites before almost every use is no longer watched for changes, saving up to about 2 ms a frame in busy areas. On by default · Default: On | Up to about 2 ms less a frame in busy areas | Memory the game rewrites before almost every use is no longer watched for changes, saving up to about 2 ms a frame in busy areas. On by default *(unchanged)* | — |  |
| **Per-frame GPU data in place** · Experimental engine features | Line: The memory the game rewrites for the GPU every frame (288 MiB)… · Tooltip: The memory the game rewrites for the GPU every frame (288 MiB) is read by the GPU where the game writes it, as a PC game's upload heap: no write faults or uploads for it, and less CPU and GPU time a frame. On by default. Off: watched and copied as before 0.4.4, should something look wrong · Default: On | Less CPU and GPU time a frame | The memory the game rewrites for the GPU every frame (288 MiB) is read by the GPU where the game writes it, as a PC game's upload heap: no write faults or uploads for it, and less CPU and GPU time a frame. On by default. Off: watched and copied as before 0.4.4, should something look wrong *(unchanged)* | — |  |
| **Native draw calls** · Experimental engine features | Line: The game's draw calls reach the engine directly instead of as… · Tooltip: The game's draw calls reach the engine directly instead of as console GPU commands it decodes again. On by default; turn it off if something stops drawing · Default: On | Draw calls reach the engine directly | The game's draw calls reach the engine directly instead of as console GPU commands it decodes again. On by default; turn it off if something stops drawing *(unchanged)* | — |  |
| **Effect trail crash fix** · Experimental engine features | Line: For crashes while drawing effect trails on newer Intel CPUs… · Tooltip: For crashes while drawing effect trails on newer Intel CPUs (Witch of Hemwick, for example): stops the crash, and that kind of trail is not drawn. Off by default · Default: Off | For effect trail crashes on newer Intel CPUs | For crashes while drawing effect trails on newer Intel CPUs (Witch of Hemwick, for example): stops the crash, and that kind of trail is not drawn. Off by default *(unchanged)* | — | Stays where it is, separate from the Intel effect crash fix (the tester's call). |
| **Graphics card** · Performance | Line: For laptops and PCs with two graphics cards where the game picks… · Tooltip: For laptops and PCs with two graphics cards where the game picks the wrong one. Auto takes the faster one. BB_GPU_ID, default: not set. Applied when the game starts · Choices: Auto; First; Second · Default: not set | For PCs with two graphics cards | For laptops and PCs with two graphics cards where the game picks the wrong one. Auto takes the faster one. BB_GPU_ID, default: not set. Applied when the game starts *(unchanged)* | Auto; First; Second *(unchanged)* |  |
| **Two-stage GPU pipeline** · Performance | Line: 20–30% faster; turn off if the game is unstable · Tooltip: 20–30% faster; turn off if the game is unstable · Choices: Off (More stable, but slower); Auto (On with 8 or more CPU threads); On · Default: Auto | 20–30% faster; turn off if the game is unstable *(unchanged)* | None (it would repeat the line) + *Default: Auto* | Off (More stable, but slower); Auto (On with 8 or more CPU threads); On *(unchanged)* |  |
| **CPU reads of GPU data** · Performance | Line: Relaxed is the default · Tooltip: Relaxed is the default · Choices: Off; Relaxed (Default); Precise · Default: Relaxed | When the game gets back data the GPU wrote | Off: never. Relaxed (default): before the game changes that memory. Precise: before it reads it too; slower | Off; Relaxed (Default); Precise *(unchanged)* | Today it says only "Relaxed is the default". Checked in the engine. |
| **Shader compile threads** · Performance | Line: How many threads compile new shaders in the background. · Tooltip: How many threads compile new shaders in the background. More finish a new area's shaders sooner but take CPU time from the game while they work. BB_PIPELINE_THREADS, default: not set. Applied when the game starts · Choices: Auto (A third of the CPU threads, at most 4); 1; 2; 3; 4; 6; 8 · Default: not set | How many threads compile new shaders | How many threads compile new shaders in the background. More finish a new area's shaders sooner but take CPU time from the game while they work. BB_PIPELINE_THREADS, default: not set. Applied when the game starts *(unchanged)* | Auto (A third of the CPU threads, at most 4); 1; 2; 3; 4; 6; 8 *(unchanged)* |  |
| **Area prefetch (hard disks)** · Performance | Line: When the game starts loading a map area, a background thread… · Tooltip: When the game starts loading a map area, a background thread reads that area's model files into the Windows file cache in big sequential reads, so models and textures stop appearing late in front of you. Auto: on when the game is installed on a hard disk (an SSD does not need it). BB_FILE_PREFETCH, default: not set. Applied when the game starts · Choices: Auto; On; Off · Default: not set | Reads an area's files ahead on hard disks | When the game starts loading a map area, a background thread reads that area's model files into the Windows file cache in big sequential reads, so models and textures stop appearing late in front of you. Auto: on when the game is installed on a hard disk (an SSD does not need it). BB_FILE_PREFETCH, default: not set. Applied when the game starts *(unchanged)* | Off; Auto; On |  |
| **Frame rate in the background** · Performance | Line: The frame-rate limit while the game window is in the background… · Tooltip: The frame-rate limit while the game window is in the background or minimized, to save power and heat. Online play is never limited, so co-op and invasions keep their pace. BB_BACKGROUND_FPS, default: 30 FPS. Applied when the game starts · Choices: 10 FPS; 15 FPS; 30 FPS; 60 FPS; No limit · Default: 30 FPS | The FPS limit while the game is in the background | The frame-rate limit while the game window is in the background or minimized, to save power and heat. Online play is never limited, so co-op and invasions keep their pace. BB_BACKGROUND_FPS, default: 30 FPS. Applied when the game starts *(unchanged)* | 10 FPS; 15 FPS; 30 FPS; 60 FPS; No limit *(unchanged)* |  |
| **Sound output** · Sound | Line: If the sound jitters or crackles, try DirectSound. · Tooltip: If the sound jitters or crackles, try DirectSound. The larger buffer is steadier but the sound comes a little later · Choices: WASAPI (Default); DirectSound; DirectSound, larger buffer · Default: WASAPI | If the sound jitters or crackles, try DirectSound *(unchanged)* | If the sound jitters or crackles, try DirectSound. The larger buffer is steadier but the sound comes a little later *(unchanged)* + *Default: WASAPI* | WASAPI (Default); DirectSound; DirectSound, larger buffer *(unchanged)* |  |
| **Sound threads first** · Sound | Line: For CPUs with many slower cores (older Xeons and similar)… · Tooltip: For CPUs with many slower cores (older Xeons and similar): the game's sound runs before its other threads, against late or stuttering sound. Off by default · Default: Off | For late or stuttering sound on many-core CPUs | For CPUs with many slower cores (older Xeons and similar): the game's sound runs before its other threads, against late or stuttering sound. Off by default *(unchanged)* | — |  |
| **Run without sound** · Sound | Line: Starts without any sound device (silent). · Tooltip: Starts without any sound device (silent). Only for finding out whether a sound driver is what keeps the game from starting or hangs it. BB_AUDIO, default: off. Applied when the game starts · Default: off | Starts without any sound device (silent) *(unchanged)* | Starts without any sound device (silent). Only for finding out whether a sound driver is what keeps the game from starting or hangs it. BB_AUDIO, default: off. Applied when the game starts *(unchanged)* | — |  |
| **No minimum loading time after death** · Game fixes | Line: Skips the game's 12-second minimum loading screen after dying. · Tooltip: Skips the game's 12-second minimum loading screen after dying. Off: the original delay. BB_RESPAWN_DELAY_FIX, default: on. Applied when the game starts · Default: on | Removes the 12-second minimum wait after dying | Skips the game's 12-second minimum loading screen after dying. Off: the original delay. BB_RESPAWN_DELAY_FIX, default: on. Applied when the game starts *(unchanged)* | — | Loading still takes what it takes: only the minimum goes (engine/src/respawn_patch.h). |
| **Weapon sound save fix** · Game fixes | Line: Sets the flag in the game's save that some weapon and player… · Tooltip: Sets the flag in the game's save that some weapon and player sounds need (without it they never play), before the game reads the save. Off leaves saves untouched. BB_SOUND_HACK, default: on. Applied when the game starts · Default: on | Fixes weapon sounds that never play | Sets the flag in the game's save that some weapon and player sounds need (without it they never play), before the game reads the save. Off leaves saves untouched. BB_SOUND_HACK, default: on. Applied when the game starts *(unchanged)* | — |  |
| **OptiScaler compatibility** · Game fixes | Line: Only when OptiScaler is installed… · Tooltip: Only when OptiScaler is installed: DLSS calls wait for the GPU queue, and one thread sends the GPU work while its frame generation is on, so OptiScaler's hooks don't crash. Costs a little FPS with its frame generation. Off: the engine runs as without OptiScaler. BB_OPTISCALER_COMPAT, default: on. Applied when the game starts · Default: on | Keeps OptiScaler's hooks from crashing the game | Only when OptiScaler is installed: DLSS calls wait for the GPU queue, and one thread sends the GPU work while its frame generation is on, so OptiScaler's hooks don't crash. Costs a little FPS with its frame generation. Off: the engine runs as without OptiScaler. BB_OPTISCALER_COMPAT, default: on. Applied when the game starts *(unchanged)* | — |  |
| **Ebrietas effect fix** · Game fixes | Line: Replaces Ebrietas' trail effect with the one Nexus mod 41 uses… · Tooltip: Replaces Ebrietas' trail effect with the one Nexus mod 41 uses, made from your own game files: her rock attacks no longer pile up thousands of rocks (FPS falling from 90 to 40, crashes before beta 8). Off: the game's own effect. A mod that changes these effect files wins. BB_EFFECT_FIXES, default: on. Applied when the game starts · Default: on | Stops her rock attacks from piling up rocks | Replaces Ebrietas' trail effect with the one Nexus mod 41 uses, made from your own game files: her rock attacks no longer pile up thousands of rocks (FPS falling from 90 to 40). Off: the game's own effect. A mod that changes these effect files wins. BB_EFFECT_FIXES, default: on. Applied when the game starts | — | Tooltip corrected. "Crashes before beta 8" goes. |
| **PS4 floating-point mode** · Game fixes | Line: The game's threads handle very small numbers as a PS4 does… · Tooltip: The game's threads handle very small numbers as a PS4 does, rounding them to zero, instead of as Windows does. Physics and cloth then behave as on the console: with Windows' way, some of Cainhurst's ghosts lose the lower half of their dresses. Off: Windows' way, as before 0.4.2. BB_PS4_FPU, default: on. Applied when the game starts · Default: on | Physics and cloth as on the PS4 | The game's threads handle very small numbers as a PS4 does, rounding them to zero, instead of as Windows does. Physics and cloth then behave as on the console: with Windows' way, some of Cainhurst's ghosts lose the lower half of their dresses. Off: Windows' way, as before 0.4.2. BB_PS4_FPU, default: on. Applied when the game starts *(unchanged)* | — |  |
| **Intel effect crash fix (test)** · Game fixes | Line: For crashes in effects on Intel 12th-14th gen CPUs (Ebrietas… · Tooltip: For crashes in effects on Intel 12th-14th gen CPUs (Ebrietas, Witch of Hemwick, One Reborn, the Cainhurst cutscene): the two effect ribbon writers keep their data where Windows can't overwrite it. Every effect stays drawn. Turn off the Effect trail crash fix with it, and post your log either way. BB_RED_ZONE_FUNCTIONS, default: off. Applied when the game starts · Default: off | For effect crashes on Intel 12th-14th gen CPUs | For crashes in effects on Intel 12th-14th gen CPUs (Ebrietas, Witch of Hemwick, One Reborn, the Cainhurst cutscene): the two effect ribbon writers keep their data where Windows can't overwrite it. Every effect stays drawn. Turn off the Effect trail crash fix with it, and post your log either way. BB_RED_ZONE_FUNCTIONS, default: off. Applied when the game starts *(unchanged)* | — | Both Intel effect fixes stay separate switches; its tooltip keeps asking to turn the Effect trail crash fix off with it (the tester's call). |
| **Intel black tonemap fix** · Game fixes | Line: Adds the Intel Black Tonemap Fix patch… · Tooltip: Adds the Intel Black Tonemap Fix patch: for Intel CPUs where the picture turns black or far too dark (the DLC areas most). BB_INTEL_TONEMAP_FIX, default: off. Applied when the game starts · Default: off | For a black or very dark picture on Intel CPUs | Adds the Intel Black Tonemap Fix patch: for Intel CPUs where the picture turns black or far too dark (the DLC areas most). BB_INTEL_TONEMAP_FIX, default: off. Applied when the game starts *(unchanged)* | — |  |
| **Allow other game versions** (was Skip the game files check) · Game fixes | Line: Starts with game files that are not the supported version (base… · Tooltip: Starts with game files that are not the supported version (base game plus the 1.09 update). Other editions may crash, and the engine's patches and PC menus may not apply. BB_SKIP_GAME_CHECK, default: off. Applied when the game starts · Default: off | Starts game versions other than 1.09; 1.09 works best | Starts with game files that are not the supported version (base game plus the 1.09 update). Other editions may crash, and the engine's patches and PC menus may not apply. BB_SKIP_GAME_CHECK, default: off. Applied when the game starts *(unchanged)* | — | The tester asked for a clearer name that says 1.09 works best. |

## Log

One Log options fold holds the log level and every diagnostic again (the tester's call, already applied). The BB_ names stay first in each tooltip, as today; these texts are translated in zh-CN only.

| Setting | Today | Proposed line | Proposed tooltip | Proposed choices | Notes |
| --- | --- | --- | --- | --- | --- |
| **Show this page when the game starts** | Default: On | — | *Default: On* | — |  |
| **Log level** · Log options | Line: How much the game writes into its log… · Tooltip: How much the game writes into its log: Standard for everyday play, Detailed when asked for a log of something odd, Debug only to chase a specific bug · Choices: Standard (What a problem report needs: the settings at the start, loading times, a summary every minute, warnings and errors); Detailed (Also every save, thread and semaphore the game makes, the draw counts every 5 seconds and the GPU side's notes: a larger log, for something odd); Debug (Warning: everything the GPU side can say. The log grows by many megabytes and the game runs slower. Only to chase a specific bug, then set it back) · Default: Standard | How much the game writes into its log | How much the game writes into its log: Standard for everyday play, Detailed when asked for a log of something odd, Debug only to chase a specific bug *(unchanged)* + *Default: Standard* | Standard (What a problem report needs: the settings at the start, loading times, a summary every minute, warnings and errors); Detailed (Also every save, thread and semaphore the game makes, the draw counts every 5 seconds and the GPU side's notes: a larger log, for something odd); Debug (Warning: everything the GPU side can say. The log grows by many megabytes and the game runs slower. Only to chase a specific bug, then set it back) *(unchanged)* |  |
| **Frame statistics in the log** · Log options | Line: BB_FRAME_STATS · Tooltip: BB_FRAME_STATS · Default: On in betas, off in stable releases | Frame times and engine counters every 5 seconds | BB_FRAME_STATS: frame times and engine counters every 5 seconds. On in betas, so crash logs carry them + *Default: On in betas, off in stable releases* | — | Today it says only the variable's name. |
| **Hitch reports** · Log options | Line: A frame slower than this puts every thread's wait and stack in… · Tooltip: A frame slower than this puts every thread's wait and stack in the log, for stutter reports. Pick about twice your frame time: 50 ms at 30 FPS, 40 at 60, 25 at 90, 20 at 120, 16 at 144 or uncapped. Each report pauses the game about 0.2 s; at most 12 a session, none in the first 20 s or just after a loading screen · Choices: Off; 16 ms (For 144 FPS or uncapped); 20 ms (For 120 FPS); 25 ms (For 90 FPS); 30 ms (60 FPS, every missed refresh: many reports); 40 ms (For 60 FPS); 50 ms (For 30 FPS); 100 ms (Long stalls only, at any frame rate) · Default: Off | Puts what held up slow frames in the log | A frame slower than this puts every thread's wait and stack in the log, for stutter reports. Pick about twice your frame time: 50 ms at 30 FPS, 40 at 60, 25 at 90, 20 at 120, 16 at 144 or uncapped. Each report pauses the game about 0.2 s; at most 12 a session, none in the first 20 s or just after a loading screen *(unchanged)* + *Default: Off* | Off; 16 ms (For 144 FPS or uncapped); 20 ms (For 120 FPS); 25 ms (For 90 FPS); 30 ms (60 FPS, every missed refresh: many reports); 40 ms (For 60 FPS); 50 ms (For 30 FPS); 100 ms (Long stalls only, at any frame rate) *(unchanged)* |  |
| **GPU time in the log** · Log options | Line: BB_GPU_PROFILE: the GPU's time a frame by render pass, dispatch… · Tooltip: BB_GPU_PROFILE: the GPU's time a frame by render pass, dispatch and upscaler, every 5 seconds. Costs a little GPU time · Default: Off | The GPU's time a frame by pass, every 5 seconds | BB_GPU_PROFILE: the GPU's time a frame by render pass, dispatch and upscaler, every 5 seconds. Costs a little GPU time *(unchanged)* + *Default: Off* | — |  |
| **Render stage timings in the log** · Log options | Line: BB_SECTIONS: where each frame's CPU time goes (with frame… · Tooltip: BB_SECTIONS: where each frame's CPU time goes (with frame statistics). Costs a little speed: a few tenths of a millisecond a frame on the draw recording thread · Default: Off | Where each frame's CPU time goes | BB_SECTIONS: where each frame's CPU time goes (with frame statistics). Costs a little speed: a few tenths of a millisecond a frame on the draw recording thread *(unchanged)* + *Default: Off* | — |  |
| **Loading profile in the log** · Log options | Line: BB_SAMPLE_THREADS: where the game's main, render and file… · Tooltip: BB_SAMPLE_THREADS: where the game's main, render and file threads spend their time, every 5 seconds. For slow loading; costs a little speed · Default: Off | Where loading time goes, every 5 seconds | BB_SAMPLE_THREADS: where the game's main, render and file threads spend their time, every 5 seconds. For slow loading; costs a little speed *(unchanged)* + *Default: Off* | — |  |
| **Audio statistics in the log** · Log options | Line: BB_AUDIO_STATS · Tooltip: BB_AUDIO_STATS | Sound buffers and dropouts every 5 seconds | BB_AUDIO_STATS: sound buffers and dropouts every 5 seconds, for crackling or late sound | — | Today it says only the variable's name. |
| **Controller details in the log** · Log options | Line: BB_PAD_LOG: each controller's USB ids, connection and rumble… · Tooltip: BB_PAD_LOG: each controller's USB ids, connection and rumble support, disconnects, the game's first vibration request and sticks past their round gate. For controller problems · Default: Off | Controller ids, connections and rumble | BB_PAD_LOG: each controller's USB ids, connection and rumble support, disconnects, the game's first vibration request and sticks past their round gate. For controller problems *(unchanged)* + *Default: Off* | — |  |
| **Heap corruption checks** · Log options | Line: BB_WRITE_LOG: the GPU side's last writes into the game's memory… · Tooltip: BB_WRITE_LOG: the GPU side's last writes into the game's memory, put in the log if the game crashes on damaged memory. For crash reports; changes the timing of those writes a little · Default: Off | The GPU's last writes into game memory, for crashes | BB_WRITE_LOG: the GPU side's last writes into the game's memory, put in the log if the game crashes on damaged memory. For crash reports; changes the timing of those writes a little *(unchanged)* + *Default: Off* | — |  |
| **Capture shaders** · Log options | Line: BB_SHADER_CAPTURE: every shader the game uses is saved once to… · Tooltip: BB_SHADER_CAPTURE: every shader the game uses is saved once to logs\shaders (a few MB), for testing the new native shader compiler offline. Costs a little time the first time each shader is seen · Default: Off | Saves every shader the game uses to logs\shaders | BB_SHADER_CAPTURE: every shader the game uses is saved once to logs\shaders (a few MB), for testing the native shader compiler offline. Costs a little time the first time each shader is seen + *Default: Off* | — | Tooltip corrected. "New" goes from "the new native shader compiler". Bug today: this text isn't translated. |

## Settings

| Setting | Today | Proposed line | Proposed tooltip | Proposed choices | Notes |
| --- | --- | --- | --- | --- | --- |
| **Launcher language** · Appearance | Line: Restart the launcher to apply the language. · Tooltip: Restart the launcher to apply the language. · Choices: English; Deutsch; Español; Français; Italiano; Polski; Português (Brasil); Türkçe; Русский; Українська; 日本語; 한국어; 简体中文; 繁體中文 · Default: English | Restart the launcher to apply the language *(unchanged)* | None (it would repeat the line) + *Default: English* | English; Deutsch; Español; Français; Italiano; Polski; Português (Brasil); Türkçe; Русский; Українська; 日本語; 한국어; 简体中文; 繁體中文 *(unchanged)* |  |
| **Layout** · Appearance | Line: Classic: the sidebar and one card per setting. · Tooltip: Classic: the sidebar and one card per setting. Modern: tabs and compact rows · Choices: Classic; Modern · Default: Classic | Classic: sidebar and cards; Modern: tabs and compact rows | Classic: the sidebar and one card per setting. Modern: tabs and compact rows *(unchanged)* + *Default: Classic* | Classic; Modern *(unchanged)* | Today's line describes Classic only. |
| **Navigation** · Appearance | Line: Tabs across the top, or a sidebar with the pages in groups · Tooltip: Tabs across the top, or a sidebar with the pages in groups · Choices: Tabs; Sidebar · Default: Tabs | Tabs across the top, or a sidebar with the pages in groups *(unchanged)* | None (it would repeat the line) + *Default: Tabs* | Tabs; Sidebar *(unchanged)* |  |
| **Theme** · Appearance | Line: The accent colour, and a faint tint of it in the launcher's… · Tooltip: The accent colour, and a faint tint of it in the launcher's surfaces · Choices: Default; Crimson (The blood moon: deep red); Moonlight (Pale blue, like the Moonlight Greatsword); Amber (Lamplight gold); Nightmare (Arcane violet); Verdant (Forbidden Woods green); Custom (Your own colour, below) · Default: Default | The accent colour, and a faint tint of it | The accent colour, and a faint tint of it in the launcher's surfaces *(unchanged)* + *Default: Default* | Default; Crimson (The blood moon: deep red); Moonlight (Pale blue, like the Moonlight Greatsword); Amber (Lamplight gold); Nightmare (Arcane violet); Verdant (Forbidden Woods green); Custom (Your own colour, below) *(unchanged)* |  |
| **Theme colour** · Appearance | Line: Pick a dot or type a colour as #RRGGBB · Tooltip: Pick a dot or type a colour as #RRGGBB | Pick a dot or type a colour as #RRGGBB *(unchanged)* | None: it would repeat the line | — |  |
| **Background** · Appearance | Line: The picture behind the glass panes · Tooltip: The picture behind the glass panes · Choices: Game artwork; Your picture; None · Default: Game artwork | The picture behind the glass panes *(unchanged)* | None (it would repeat the line) + *Default: Game artwork* | None; Game artwork; Your picture |  |
| **Picture** · Appearance | Line: A PNG, JPG or BMP; it fills the window · Tooltip: A PNG, JPG or BMP; it fills the window | A PNG, JPG or BMP; it fills the window *(unchanged)* | None: it would repeat the line | — |  |
| **Background blur** · Appearance | Choices: Strong; Light; Off · Default: Strong | — | *Default: Strong* | Off; Light; Strong |  |
| **App icon** · Appearance | Line: The launcher's and the game window's icon. · Tooltip: The launcher's and the game window's icon. The game window takes it at its next start · Choices: Auto (Red lantern in betas, blue in stable releases); Red lantern; Blue lantern; Lantern by j3loo (Fan art by j3loo); Your picture · Default: Auto | The launcher's and the game window's icon *(unchanged)* | The launcher's and the game window's icon. The game window takes it at its next start *(unchanged)* + *Default: Auto* | Auto (Red lantern in betas, blue in stable releases); Red lantern; Blue lantern; Lantern by j3loo (Fan art by j3loo); Your picture *(unchanged)* |  |
| **Icon picture** · Appearance | Line: A PNG, JPG, BMP or ICO; square pictures look best · Tooltip: A PNG, JPG, BMP or ICO; square pictures look best | A PNG, JPG, BMP or ICO; square pictures look best *(unchanged)* | None: it would repeat the line | — |  |
| **Window buttons** · Appearance | Line: Windows: – □ ✕. macOS: the red, yellow and green dots · Tooltip: Windows: – □ ✕. macOS: the red, yellow and green dots · Choices: Windows; macOS · Default: Windows | Windows: – □ ✕. macOS: the red, yellow and green dots *(unchanged)* | None (it would repeat the line) + *Default: Windows* | Windows; macOS *(unchanged)* |  |
| **Window buttons side** · Appearance | Choices: Usual (macOS on the left, Windows on the right); Left; Right · Default: Usual | — | *Default: Usual* | Usual (macOS on the left, Windows on the right); Left; Right *(unchanged)* |  |
| **Engine folder** · Engine and launch | — | — | — | — | Its texts still describe the old start (run.py, Python, bin\bb-probe.exe): see Also found. |
| **Start the game with** · Engine and launch | Choices: Hunter's Workshop (Prepares mods and patches, then starts the engine (out\bb-probe.exe)); Custom command (Runs a program of your choice with the settings as environment variables) · Default: Hunter's Workshop | — | *Default: Hunter's Workshop* | Hunter's Workshop (Prepares mods and patches, then starts the engine (out\bb-probe.exe)); Custom command (Runs a program of your choice with the settings as environment variables) *(unchanged)* |  |
| **Command** · Engine and launch | Line: A program to run instead of Bloodborne.exe --run, with the… · Tooltip: A program to run instead of Bloodborne.exe --run, with the settings as environment variables | A program to run instead of bb-start.exe | A program to run instead of bb-start.exe, with the settings as environment variables | — | Tooltip corrected. Today it says "instead of Bloodborne.exe --run", the old start. |
| **Arguments** · Engine and launch | — | — | — | — |  |
| **Launcher settings** · Files | Line: the file's path · Tooltip: the file's path | the file's path *(unchanged)* | None: it would repeat the line | — | The line is the file's path. |
| **Game settings (config.ini)** · Files | Line: the file's path · Tooltip: the file's path | the file's path *(unchanged)* | None: it would repeat the line | — | The line is the file's path. |
| **Shader cache** · Files | Line: The game's compiled shaders, kept between starts (a new engine… · Tooltip: The game's compiled shaders, kept between starts (a new engine version clears them itself). Clear them if something draws wrong in every save, such as missing skin or hair; the next start compiles them again and stutters for a while | The game's compiled shaders, kept between starts | The game's compiled shaders, kept between starts (a new engine version clears them itself). Clear them if something draws wrong in every save, such as missing skin or hair; the next start compiles them again and stutters for a while *(unchanged)* | — |  |
| **Shader catalog** · Files | Line: Save your shaders as a catalog file, or add someone else's… · Tooltip: Save your shaders as a catalog file, or add someone else's: areas they have played don't pop in or stutter for you. It works with the same kind of graphics card; a catalog adds to your shaders, it never replaces them | Shares shader catalogs to reduce first-visit stutter | Save your shaders as a catalog file, or add someone else's: areas they have played need fewer new shaders the first time, so less pop-in and stutter. It works with the same kind of graphics card; a catalog adds to your shaders, it never replaces them | — | Tooltip corrected. Today's "areas they have played don't pop in or stutter for you" promises too much: a catalog covers only the shaders it holds. |
| **Share your settings** · Share settings | Line: Graphics, effects, camera, controls, cheats and engine options… · Tooltip: Graphics, effects, camera, controls, cheats and engine options, as a code to paste in Discord or a file. Not included: display and HDR, the online account, folders and languages | Your settings as a code for Discord, or a file | Everything that sets how the game looks, runs and plays, as a code to paste in Discord or a file. Not included: what belongs to this PC (folders, monitor, output resolution, window mode, HDR, graphics card, sound device, preferred controller, and fixes or tuning for its hardware), to you (the online account, languages, profiles, the launcher's look, trophy notifications) or to one session (log options, Extra variables) | — | Tooltip corrected. Today's text is wrong both ways: it says display settings stay out, but frame rate, V-Sync, widescreen and live resolution go in; DLSS model, texture mip bias, texture filtering and most engine switches stay out. With the sharing change (the tester's call: everything but the list). |
| **Import settings** · Share settings | Line: Someone's code or file. · Tooltip: Someone's code or file. You see what changes before anything is applied | Someone's code or file *(unchanged)* | Someone's code or file. You see what changes before anything is applied *(unchanged)* | — |  |
| **Launcher updates** · Updates | — | — | — | — |  |
| **Update channel** · Updates | Line: Stable: releases that have been tested. · Tooltip: Stable: releases that have been tested. Beta: every change as soon as it is done, before it is tested · Choices: Stable; Beta | Tested releases (Stable), or every change (Beta) | Stable: releases that have been tested. Beta: every change as soon as it is done, before it is tested *(unchanged)* | Stable; Beta *(unchanged)* | Today's line describes Stable only. |
| **Check for updates at start** · Updates | Line: Looks at github.com/hamzahskafi/HuntersWorkshop/releases · Tooltip: Looks at github.com/hamzahskafi/HuntersWorkshop/releases · Default: On | Looks at github.com/hamzahskafi/HuntersWorkshop/releases *(unchanged)* | None (it would repeat the line) + *Default: On* | — |  |
| **Version** · Updates | Line: Every published release, stable and beta, newest first. · Tooltip: Every published release, stable and beta, newest first. Install any of them, older ones too, to go back if a version gives you trouble. Your settings, saves, mods and profiles stay | Every published release, stable and beta, newest first *(unchanged)* | Every published release, stable and beta, newest first. Install any of them, older ones too, to go back if a version gives you trouble. Your settings, saves, mods and profiles stay *(unchanged)* | — |  |
| **Build** · Updates | — | — | — | — |  |

## Online

| Setting | Today | Proposed line | Proposed tooltip | Proposed choices | Notes |
| --- | --- | --- | --- | --- | --- |
| **Play online** · The Hunter's Dream | Line: Off: the game stays offline · Tooltip: Off: the game stays offline | Co-op, messages and bloodstains on The Hunter's Dream | Off: the game stays offline *(unchanged)* | — |  |
| **Hunter's Dream account** · The Hunter's Dream | — | — | — | — |  |
| **Website** · The Hunter's Dream | Line: Players online, the map and your hunter's profile · Tooltip: Players online, the map and your hunter's profile | Players online, the map and your hunter's profile *(unchanged)* | None: it would repeat the line | — |  |
| **Local P2P port** · Connection | Line: Empty: 3658, the PS4's · Tooltip: Empty: 3658, the PS4's | The port co-op connects through | Empty: 3658, the PS4's *(unchanged)* | — |  |
| **Summon sign timeout** · Connection | Line: How long a sign you touched waits for its host before giving up · Tooltip: How long a sign you touched waits for its host before giving up · Choices: 30 seconds (default); 60 seconds; The game's own (180 seconds) · Default: 30 seconds (default) | How long a touched sign waits for its host | How long a sign you touched waits for its host before giving up *(unchanged)* | 30 seconds (default); 60 seconds; The game's own (180 seconds) *(unchanged)* |  |
| **Test the connection** · Connection | Line: Checks The Hunter's Dream… · Tooltip: Checks The Hunter's Dream: game server, co-op sessions and the relay summons use | Checks that the game server and summon relay answer | Checks The Hunter's Dream: game server, co-op sessions and the relay summons use *(unchanged)* | — | A check that they answer, not a test summon. |

## Mods & patches

| Setting | Today | Proposed line | Proposed tooltip | Proposed choices | Notes |
| --- | --- | --- | --- | --- | --- |
| **Load mods** · Mods | Line: When two mods replace the same file, the one lower in the list… · Tooltip: When two mods replace the same file, the one lower in the list wins · Default: On | Loads the mods in the list below | When two mods replace the same file, the one lower in the list wins *(unchanged)* + *Default: On* | — |  |
| **Mods folder** · Mods | — | — | — | — |  |
| **Patches folder** · Third-party patches | — | — | — | — |  |

## Cheats

| Setting | Today | Proposed line | Proposed tooltip | Proposed choices | Notes |
| --- | --- | --- | --- | --- | --- |
| **Blood Vials** · Items | Choices: Off; Infinite (Kept full at the normal carry limit, even if you have none. Items put into storage while it is on come back); Custom amount (Refilled to the amount below whenever it drops) | Infinite, or kept at a set amount | — | Off; Infinite (Kept full at the normal carry limit, even if you have none. Items put into storage while it is on come back); Custom amount (Refilled to the amount below whenever it drops) *(unchanged)* | In-game overlay menu: Blood Vials. |
| **Amount** · Items | Line: How many Blood Vials to keep · Tooltip: How many Blood Vials to keep | How many Blood Vials to keep *(unchanged)* | None: it would repeat the line | — | Under its item, shown only for Custom amount. |
| **Quicksilver Bullets** · Items | Choices: Off; Infinite (Kept full at the normal carry limit, even if you have none. Items put into storage while it is on come back); Custom amount (Refilled to the amount below whenever it drops) | Infinite, or kept at a set amount | — | Off; Infinite (Kept full at the normal carry limit, even if you have none. Items put into storage while it is on come back); Custom amount (Refilled to the amount below whenever it drops) *(unchanged)* | In-game overlay menu: Quicksilver Bullets. |
| **Amount** · Items | Line: How many Quicksilver Bullets to keep · Tooltip: How many Quicksilver Bullets to keep | How many Quicksilver Bullets to keep *(unchanged)* | None: it would repeat the line | — | Under its item, shown only for Custom amount. |
| **Infinite consumables** · Items | Line: Molotovs, throwing knives, pebbles, antidotes, papers and the… · Tooltip: Molotovs, throwing knives, pebbles, antidotes, papers and the like are not used up (not Blood Vials or Bullets, which are above, and not insight items) | Molotovs, knives, pebbles and the like aren't used up | Molotovs, throwing knives, pebbles, antidotes, papers and the like are not used up (not Blood Vials or Bullets, which are above, and not insight items) *(unchanged)* | — | In-game overlay menu: Infinite consumables. |
| **Infinite stamina** · Character | Line: Your stamina stays full · Tooltip: Your stamina stays full | Your stamina stays full *(unchanged)* | None: it would repeat the line | — | In-game overlay menu: Infinite stamina. |
| **Stealth** · Character | Line: Enemies don't notice you and your steps make no noise · Tooltip: Enemies don't notice you and your steps make no noise | Enemies don't notice you and your steps make no noise *(unchanged)* | None: it would repeat the line | — | In-game overlay menu: Stealth. |
| **No weapon wear** · Character | Line: Equipped weapons are kept at full durability, so no repairs · Tooltip: Equipped weapons are kept at full durability, so no repairs | Equipped weapons are kept at full durability, so no repairs *(unchanged)* | None: it would repeat the line | — | In-game overlay menu: No weapon wear. |
| **Firearm range** · Character | Line: How far firearm bullets travel (pistols, rifles, blunderbuss… · Tooltip: How far firearm bullets travel (pistols, rifles, blunderbuss, cannons) · Choices: Normal; 150%; 200%; 300% · Default: Normal | How far firearm bullets travel | How far firearm bullets travel (pistols, rifles, blunderbuss, cannons) *(unchanged)* + *Default: Normal* | Normal; 150%; 200%; 300% *(unchanged)* | In-game overlay menu: Firearm range. |
| **Keep Blood Echoes on death** · Blood Echoes | Line: Dying never drops your Blood Echoes, so there is nothing to pick… · Tooltip: Dying never drops your Blood Echoes, so there is nothing to pick back up | Dying never drops your Blood Echoes | Dying never drops your Blood Echoes, so there is nothing to pick back up *(unchanged)* | — | In-game overlay menu: Keep Blood Echoes on death. |
| **Blood Echo multiplier** · Blood Echoes | Line: Enemies already loaded give the new amount after you rest at a… · Tooltip: Enemies already loaded give the new amount after you rest at a lamp or reload · Choices: Normal; x2; x5; x10 · Default: Normal | More Blood Echoes from each enemy | Enemies already loaded give the new amount after you rest at a lamp or reload *(unchanged)* + *Default: Normal* | Normal; x2; x5; x10 *(unchanged)* | In-game overlay menu: Blood Echo multiplier. |
| **Add echoes or insight** · Blood Echoes | Line: In the in-game menu (Insert or L3 + R3)… · Tooltip: In the in-game menu (Insert or L3 + R3): +1,000, +10,000, +100,000 echoes and +1 insight | From the in-game overlay menu (Insert or L3 + R3) | In the in-game overlay menu (Insert or L3 + R3): +1,000, +10,000, +100,000 echoes and +1 insight | — | Tooltip corrected. "The in-game menu" becomes the in-game overlay menu. |

## Trophies

| Setting | Today | Proposed line | Proposed tooltip | Proposed choices | Notes |
| --- | --- | --- | --- | --- | --- |
| **Show in game** · Notification | Line: A small banner in a corner of the game's screen, without taking… · Tooltip: A small banner in a corner of the game's screen, without taking focus. In fullscreen it needs Display › Window mode › Borderless fullscreen · Default: On | A banner in a corner when you earn a trophy | A small banner in a corner of the game's screen, without taking focus. In fullscreen it needs Display › Window mode › Borderless fullscreen *(unchanged)* + *Default: On* | — |  |
| **Position** · Notification | Choices: Top right; Top left; Bottom right; Bottom left · Default: Top right | — | *Default: Top right* | Top right; Top left; Bottom right; Bottom left *(unchanged)* |  |
| **Play a sound** · Notification | Default: On | — | *Default: On* | — |  |
| **Volume** · Notification | Default: 70 | — | *Default: 70* | — |  |
| **Sound file** · Notification | Line: A sound of your own (WAV, MP3, MP4/M4A)… · Tooltip: A sound of your own (WAV, MP3, MP4/M4A); empty uses the built-in chime | A sound of your own; empty uses the built-in chime | A sound of your own (WAV, MP3, MP4/M4A); empty uses the built-in chime *(unchanged)* | — |  |
| **Try it** · Notification | Line: Shows a sample notification and plays the sound · Tooltip: Shows a sample notification and plays the sound | Shows a sample notification and plays the sound *(unchanged)* | None: it would repeat the line | — |  |
| **Trophy log** · Trophy data | — | — | — | — |  |
| **Reset trophy progress** · Trophy data | Line: Starts trophies and completion over, for a fresh run. · Tooltip: Starts trophies and completion over, for a fresh run. The old log is kept as a backup | Starts trophies and completion over, for a fresh run *(unchanged)* | Starts trophies and completion over, for a fresh run. The old log is kept as a backup *(unchanged)* | — |  |

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

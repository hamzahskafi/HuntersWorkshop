# Launcher settings review: titles, lines and tooltips

Windows launcher, every page, as of 0.4.4 beta 28 commit 67e6040 (2026-11-02). This is the first
draft for beta 28's last step before the version (community-fixes-plan.md › Beta 28). The step makes
it again from the launcher as it is then; the tester goes through that version, then the texts are
changed as agreed. Nothing here has been applied yet.

## How a row shows its text today

Each row has a title and one description. The description does two jobs:

- **Classic layout (the default):** the description's first sentence goes under the title, cut at 64 characters with "…". Hovering shows the rest plus any extra advice.
- **Modern layout:** nothing goes under the title. The (i) mark shows the whole description plus the advice.

## The main problem

About 185 rows. In Classic:

- **70 lines are cut off mid-sentence**, for example "Before every start and every…", "Checks The Hunter's Dream…" and "Off keeps the game's own look…".
- **About 20 more show only a first sentence that doesn't say what the setting does**, for example "Needs Frame rate corrections.", "Off: the game stays offline" and "Relaxed is the default".
- **About 50 rows have no text at all.** Most of these are fine (folders, sliders whose name says it all). A few need one (listed per page).

**Proposal: every row gets two texts.**

1. **A line under the title:** one complete phrase of 60 characters or fewer, saying what the setting does. No defaults, no conditions, no variable names.
2. **A tooltip:** the details, when to change it, its cost, its default, and when it applies.

Classic shows the line and the tooltip on hover. Modern shows the line in the (i) above the tooltip. Rows that need no tooltip don't get one.

## Rules for every row

| Rule | Today |
|---|---|
| The line says what it does, not "Off: …" or "Needs …" | 20 rows start with a condition |
| The default, once, at the end of the tooltip, in one form: "Default: on." | 8 rows say "On by default", 3 "Off by default" (some at the start), 1 "Recommended: on", others nothing |
| When it applies, one phrase: "Applies at the next game start." | 6 wordings today: "Applied when the game starts" (7), "on the next launch" (3), "at the next start", "After a restart", "after a restart", "at its next start" (2) |
| Variable names (BB_…) at the end of the tooltip, never first | The Log page starts 7 lines with them ("BB_GPU_PROFILE: the GPU's…") |
| Plain words in the line; technical terms only in the tooltip | "port", "raw counts", "288 MiB", "upload heap", "constant cache" |
| No Lantern or old version references | "the way 0.9.33 did", "crashes before beta 8" |
| Off or None first, then less to more | Done for 5 lists. Two left: **Texture filtering** (16x … 2x, Game's own) becomes Game's own, 2x, 4x, 8x, 16x. **Model detail** (As in the game, Highest, Lower, Lowest) becomes Lowest, Lower, As in the game, Highest |

## Page by page

"—" means no text today. Rows not listed are fine as they are.

### Play

| Row | Under the title now | Proposed line | Tooltip |
|---|---|---|---|
| Engine update available | This package has an older engine than this launcher… | This package's engine is older than the launcher | Keep the rest |
| The Old Hunters (DLC) | Off: the game starts without its DLC. | The DLC's areas, bosses and items | "Off: the game starts without it. The Game of the Year edition (CUSA03173) always has it." |
| Add to Steam | Bloodborne as a non-Steam game in your Steam library… | Adds the game to your Steam library | "As a non-Steam game: Steam Input, the overlay and Big Picture work with it." |
| Player profile | Each profile has its own save files and trophies, so several… | Separate saves and trophies for each player | Keep the rest |
| Save backups | Before every start and every… | Automatic backups of this profile's saves | Keep the timing details |
| Import saves | From shadPS4, another Bloodborne port, or a PS4 save exported… | From shadPS4, another port or a decrypted PS4 save | Tools named; "This profile's saves are backed up first." |

Fine: Game folder, DLC folder, Game language, Saves folder.

### Display

| Row | Under the title now | Proposed line | Tooltip |
|---|---|---|---|
| Custom resolution | Your own output size, any shape (5:4, 16:10, 32:9...)… | Any output size and shape | "5:4, 16:10, 32:9…: the camera and UI follow it with Widescreen on." |
| Frame rate | Every rate above 30 runs on Kyo's 60 FPS++, the engine setting… | 30 is the PS4's; higher rates use Kyo's 60 FPS++ | Keep the rest |
| Monitor | The game opens here from the start, including fullscreen and… | The monitor the game opens on | "Fullscreen and HDR too. If it's disconnected, the Windows primary monitor is used." |
| Widescreen | Outputs that are not 16:9 (21:9, 32:9, 16:10, 5:4) with the… | Correct view and UI on screens that aren't 16:9 | "21:9, 32:9, 16:10, 5:4: a wider field of view, the UI kept in shape." |
| HDR source | Native: highlights as bright as they are in the game's scene. | Where HDR's bright highlights come from | Both choices explained (today's text) |
| UI brightness | Native source only: how bright the HUD, menus and subtitles are… | How bright the HUD, menus and subtitles are | "Against paper white. Native source only." (The row already greys out otherwise.) |
| UI colour | Native source only: Vivid shows the HUD and menus with the… | Vivid or exact (sRGB) colours for the HUD and menus | Today's text |
| Paper white | How bright the game's white is, in nits. Range: 10–500 | How bright white is, in nits | — (the slider shows the range) |
| Peak brightness from the display | The peak Windows reports for the display the game is on (the… | Uses the peak brightness Windows reports | "Set by the Windows HDR Calibration app once it has run. Off: the value below." |
| Peak brightness | Your display's peak brightness, in nits, when it is not taken… | Your display's peak brightness, in nits | "Used when it isn't taken from the display." |
| Highlight boost | Off keeps the game's own look… | Brighter lamps, fire and sky | "Off keeps the game's own look." |
| Picture tuning (switch) | Applies the settings below, live… | Your own brightness, colour, fog and bloom, live | "Off shows the game's own picture and keeps your values. Also in the game's System menu and the Insert menu." |
| Fog | — | How thick the fog is | "1 is the game's own." |
| Bloom | — | The glow around bright lights | "1 is the game's own." |
| Vignette | — | The darkened screen edges | — |
| Ambient occlusion strength | — | How dark the shading in corners is | "1 is the game's own." |
| Shadow distance | — | How far away shadows are drawn | "1 is the game's own; higher costs GPU time." |

Fine: Output resolution, V-Sync, Window mode, Live resolution change, HDR output, Game's own picture. Brightness, Contrast, Saturation and Hue need no line.

### Graphics

| Row | Under the title now | Proposed line | Tooltip |
|---|---|---|---|
| Best settings for this PC | Output resolution, frame rate, upscaler and quality for this… | Recommended settings for this PC | Keep the hardware summary |
| Quality | How far below the output the game renders… | Lower is faster and softer | Keep the Auto rule |
| Model detail | — | How far away models keep their full detail | "Highest costs GPU time; Lower and Lowest are for weaker GPUs." Reorder the list |
| Texture filtering | Anisotropic filtering forced on the game's textures… | Sharper ground and walls seen at an angle | Keep the rest. Reorder the list |
| Shadows from dynamic lights | — | Shadows from lamps, torches and fire | "Off is faster." |
| Ambient occlusion (SSAO) | — | Soft shading in corners and creases | — |
| Screen-space reflections | Not in the original game | Reflections on wet and shiny surfaces | "Not in the original game." |
| Depth of field | — | Blurs the distance and cutscene backgrounds | — |
| Motion blur | — (a status line appears) | Blur when the camera turns fast | — |
| Chromatic aberration | — | Colour fringes toward the screen's edges | — |
| No motion blur smear with upscalers | With an upscaler at an output other than 1920x1080 the game's… | Rename: **Skip motion blur with upscalers**. Line: "Upscalers smear it outside 1080p" | Keep the rest |

The effect lines (shadows, reflections, depth of field and so on) describe how each effect looks. They are worth a quick check in game before they ship.

Fine: Anti-aliasing, Upscaler, Render resolution, FSR version, DLSS model, Sharpening (RCAS), Sharpening strength, Character motion vectors, Texture mip bias.

### Interface

| Row | Under the title now | Proposed line | Tooltip |
|---|---|---|---|
| In-game menu shortcut | What opens the port's settings menu while playing. | The keys that open the in-game menu | Applies-at-start phrase |
| In-game menu language | The port's settings menu (Insert or L3 + R3) | The in-game menu's language | — |
| Sharp UI | Menus, HUD and text drawn at the output resolution instead of… | Menus and HUD at the full output resolution | "Much sharper at 1440p and 4K. On by default in betas while it's tested." |
| Note while shaders build | "Building shaders" in a corner while the game builds shaders… | Shows "Building shaders" in a corner | "When the game builds shaders for a place it shows for the first time. Also in game." |
| FPS counter (switch) | Also in game: Insert or L3 + R3 | Frame rate and timings in a corner | "Also in game: Insert or L3 + R3." |
| Frame rate, Frame time, Upscaler, CPU usage, GPU usage | "Shown in the FPS counter" five times | No line (they're inside the FPS counter fold) | — |
| FPS counter position, size, text colour, … (7 rows) | — | Drop the "FPS counter" prefix inside its fold: **Position, Size, Text colour, Text opacity, Background, Background opacity, Border** | — |

### Gameplay

| Row | Under the title now | Proposed line | Tooltip |
|---|---|---|---|
| Camera tuning (switch) | Applies the three settings below… | Your own field of view, distance and height | "Also in the game's System menu (Camera Tuning)." |
| Follow camera | Original keeps the game's camera distance, angle and follow… | How the camera follows you at 60 FPS and above | The two choices' notes cover it |
| Debug menu | (long) | The game's developer menu (Left touchpad / Tab) | "Needs the adhoc folder from Nexus mod #253…" |
| Hunter's Dream mirror | The Dream's mirror opens the appearance editor (the game's… | The Dream's mirror opens the appearance editor | "The game's unused Put on Disguise." |
| Rebirth at the altar | With the Yharnam Stone, the Altar of Despair resets your level… | Reset your level at the Altar of Despair | "With the Yharnam Stone." |

Fine: Field of view, Distance, Height, Skip intro videos, Free camera.

### Controls

| Row | Under the title now | Proposed line | Tooltip |
|---|---|---|---|
| PC controls | The keyboard and mouse layout below… | The keyboard and mouse layout below | "Off: the gamepad-style keys." |
| Mouse camera | The mouse turns the game's own camera by raw counts, at any… | The mouse turns the camera directly | "The same at any frame rate." |
| Mouse in menus | The mouse becomes a pointer in the game's menus… | A mouse pointer in the game's menus | "Hover and click items." |
| Move and look in menus | With a menu open in the world, the movement and look keys stay… | Keep moving and looking with a menu open | Keep the rest |
| Mouse sensitivity | 5 is the default; below 0 for high-DPI mice (each step 20%… | How fast the mouse turns the camera | "Default: 5. Each step is 20%; below 0 for high-DPI mice." |
| Invert horizontal / Invert vertical (mouse) | — | Same names as the gamepad's: **Invert left / right**, **Invert up / down** | — |
| Invert camera left / right, up / down (gamepad) | The camera stick's horizontal direction | Same: **Invert left / right**, **Invert up / down** | — |
| Hide mouse cursor | Hidden 2 seconds after the mouse stops moving… | After 2 seconds without moving | "Moving the mouse brings it back." |
| Preferred gamepad | (long, cut) | The controller to use when several are connected | Today's text |
| Button prompts | The button icons in the game's menus and prompts. | Keyboard keys or controller buttons in prompts | Today's text |
| Controller icons | The controller's buttons in the game's menus and prompts. | PlayStation or Xbox buttons | Today's text |
| Save and share | Profiles are small JSON files. | Share a profile as a link or a file | Today's text |

The two button rows read almost the same today; the proposed lines tell them apart.

Fine: Camera auto-rotation, Swap sticks, Stick dead zone, Controller, Profile.

### Engine switches

**Experimental engine features.** All 17 lines are cut. Several start with "Off by default:", which takes up the whole line.

| Row | Proposed line | Tooltip changes |
|---|---|---|
| Legacy shader compiler | Compiles shaders the old way, if something draws wrong | "The way 0.9.33 did" becomes "the way earlier versions did" |
| VRAM cleanup | Frees video memory from old GPU-drawn textures | "Default: on" instead of "Recommended: on" |
| Precise frame pacing | Steadier frame times and less VRR flicker | |
| Fixed-refresh frame lock | Smoother on monitors without G-Sync or FreeSync | |
| Particle work safety cap | Against "device lost" crashes in heavy effects | "Only for games that crash" becomes "Only if the game crashes" |
| Use more VRAM for textures | Textures kept in up to 80% of VRAM, not 40% | Add its default |
| Graphics threads first | Graphics threads at a higher CPU priority | |
| Shorter sleep spin | More CPU time for the graphics threads | |
| Background shader compiling | Shorter hitches when new shaders compile | |
| Occlusion culling | Skips hidden objects: more FPS, some pop-in | Default moved to the end |
| Faster draw thread | Higher FPS in busy areas | |
| No shader freezes | Objects appear late instead of the game freezing | |
| Fast loading | Faster loading screens above 60 FPS | |
| Frame rate corrections | Physics, cloth and timers right at any frame rate | Shorter |
| Unified frame time | Catches up at once after a stutter | "Needs Frame rate corrections" moves here |
| Fewer memory write checks | Up to 2 ms less a frame in busy areas | |
| Per-frame GPU data in place | Less CPU and GPU time a frame | "288 MiB", "upload heap" go |
| Native draw calls | Draw calls reach the engine directly | |
| Effect trail crash fix | For effect trail crashes on newer Intel CPUs | |

Add "Default: on/off." automatically to every row on this page, as the switch rows already get it.

**Other rows on this page**

| Row | Under the title now | Proposed line | Tooltip |
|---|---|---|---|
| CPU reads of GPU data | Relaxed is the default | When the game sees data the GPU computed | "Relaxed (default): at sync points. Precise: as soon as it's read; slower. Off: never; fastest, some effects may break." (Wording to confirm against the engine.) |
| Sound output | If the sound jitters or crackles, try DirectSound. | WASAPI, or DirectSound if sound crackles | Today's text |
| Sound threads first | For CPUs with many slower cores (older Xeons and similar)… | Against sound stutter on many-core CPUs | Today's text |
| Shader compile threads | (cut) | Threads that compile new shaders in the background | |
| Area prefetch (hard disks) | (cut) | Reads an area's files ahead on hard disks | |
| Frame rate in the background | (cut) | The FPS limit while the game is in the background | |
| Run without sound | (cut) | Starts silent, to test a sound driver problem | |
| No minimum loading time after death | (cut) | Skips the 12-second loading screen after dying | |
| Weapon sound save fix | (cut) | Fixes weapon sounds that never play | |
| OptiScaler compatibility | (cut) | Keeps OptiScaler from crashing the game | |
| Ebrietas effect fix | (cut) | Stops Ebrietas' rocks from piling up | Drop "crashes before beta 8" |
| PS4 floating-point mode | (cut) | Physics and cloth as on the PS4 | |
| Intel effect crash fix (test) | (cut) | Stops the crash and keeps the effects drawn | |
| Intel black tonemap fix | (cut) | For a black or very dark picture on Intel CPUs | |
| Skip the game files check | (cut) | Starts an unsupported game version | |

The two Intel crash fixes (Effect trail crash fix under Experimental, Intel effect crash fix under Game fixes) fight the same crashes. Their names don't say how they differ, and they sit in different groups. Proposal: put them next to each other, with the lines above showing the difference.

Fine: Graphics card, Two-stage GPU pipeline, Changed switches, Extra variables.

### Log

| Row | Under the title now | Proposed line | Tooltip |
|---|---|---|---|
| Log level | How much the game writes into its log… | How much the game writes in its log | Today's text |
| Frame statistics in the log | BB_FRAME_STATS (nothing else) | Frame times and engine counters every 5 seconds | Variable name at the end |
| Hitch reports | A frame slower than this puts every thread's wait and stack in… | What held up slow frames, in the log | Today's text |
| GPU time in the log | BB_GPU_PROFILE: the GPU's time a frame by render pass, dispatch… | GPU time a frame by pass, every 5 seconds | "Costs a little GPU time. BB_GPU_PROFILE." |
| Render stage timings in the log | BB_SECTIONS: where each frame's CPU time goes (with frame… | Where each frame's CPU time goes | Variable at the end |
| Loading profile in the log | BB_SAMPLE_THREADS: where the game's main, render and file… | Where loading time goes, every 5 seconds | Variable at the end |
| Audio statistics in the log | BB_AUDIO_STATS (nothing else) | Sound buffers, gaps and dropouts every 5 seconds | Variable at the end |
| Controller details in the log | BB_PAD_LOG: each controller's USB ids, connection and rumble… | Controller ids, connections and rumble | Variable at the end |
| Heap corruption checks | BB_WRITE_LOG: the GPU side's last writes into the game's memory… | The GPU's last writes into game memory, for crashes | Variable at the end |
| Capture shaders | BB_SHADER_CAPTURE: every shader the game uses is saved once to… | Saves every shader the game uses to logs\shaders | **Bug: this text isn't translated (it's missing L.T).** |

Fine: Show this page when the game starts.

### Settings

| Row | Under the title now | Proposed line | Tooltip |
|---|---|---|---|
| Theme | The accent colour, and a faint tint of it in the launcher's… | The accent colour | "And a faint tint of it in the launcher's surfaces." |
| Engine folder | — | The Hunter's Workshop folder | "The folder with engine\out\bb-start.exe." |
| Shader cache | The game's compiled shaders, kept between starts (a new engine… | The game's compiled shaders | "Clear them if something draws wrong in every save…" |
| Shader catalog | Save your shaders as a catalog file, or add someone else's… | Share shaders so areas don't stutter the first time | Today's text |
| Share your settings | Graphics, effects, camera, controls, cheats and engine options… | Your settings as a code or a file | What's included and what isn't |
| Update channel | Stable: releases that have been tested. | Stable or beta releases | Today's text |
| Version | Every published release, stable and beta, newest first. | Install any release, older ones too | Today's text |

Fine: Launcher language, Layout, Navigation, Theme colour, Background, Picture, Background blur, App icon, Icon picture, Window buttons, Window buttons side, Start the game with, the two settings files, Import settings, Check for updates at start, Build.

### Online

| Row | Under the title now | Proposed line | Tooltip |
|---|---|---|---|
| Play online | Off: the game stays offline | Co-op, invasions and messages | "Through The Hunter's Dream. Off: the game stays offline." |
| Test the connection | Checks The Hunter's Dream… | Checks the server, co-op and the summon relay | — |

Fine: Hunter's Dream account, Website, Local P2P port, Summon sign timeout.

### Mods

| Row | Under the title now | Proposed line | Tooltip |
|---|---|---|---|
| Load mods | When two mods replace the same file, the one lower in the list… | Loads the mods in the list below | "When two mods change the same file, the lower one wins." |

Fine: Mods folder, Patches folder, each mod and patch.

### Cheats

| Row | Under the title now | Proposed line | Tooltip |
|---|---|---|---|
| Blood Vials, Quicksilver Bullets | — | Never run out, or keep a set number | — |
| Infinite consumables | Molotovs, throwing knives, pebbles, antidotes, papers and the… | Molotovs, knives, pebbles and the like last forever | "Not Blood Vials or Bullets (above), nor insight items." |
| Firearm range | How far firearm bullets travel (pistols, rifles, blunderbuss… | How far bullets travel | The weapon list |
| Keep Blood Echoes on death | Dying never drops your Blood Echoes, so there is nothing to pick… | Dying never drops your Blood Echoes | — |
| Blood Echo multiplier | Enemies already loaded give the new amount after you rest at a… | More Blood Echoes from each kill | "Enemies already loaded change after you rest at a lamp or reload." |
| Add echoes or insight | In the in-game menu (Insert or L3 + R3)… | Echoes or insight from the in-game menu | Today's text |

Fine: Infinite stamina, Stealth, No weapon wear, Amount.

### Trophies

| Row | Under the title now | Proposed line | Tooltip |
|---|---|---|---|
| Show in game | A small banner in a corner of the game's screen, without taking… | A banner in a corner when you earn a trophy | "In fullscreen it needs Borderless fullscreen." |
| Sound file | A sound of your own (WAV, MP3, MP4/M4A)… | Your own sound; empty plays the built-in chime | "WAV, MP3 or MP4/M4A." |

Fine: Position, Play a sound, Volume, Try it, Trophy log, Reset trophy progress.

## Also found

- **Capture shaders** (Log page): its description is not translated (no `L.T`).
- **Stale code comment:** `Choices.Effects` says the game's own anti-aliasing "starts off"; it's on by default since this beta. Code only, not visible.

## What changing this involves

- One pass over about 110 texts. Each becomes a short line plus a tooltip; texts that are already right just split in two.
- The row code takes the two texts as separate fields instead of cutting one at 64 characters.
- Translations: zh-CN for every changed text (the other 12 tables translate only part of the launcher today). The key check confirms nothing is missing.
- Both layouts benefit: Classic gets whole lines, Modern's (i) gets a clear first line.

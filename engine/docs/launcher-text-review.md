# Launcher text review

Windows launcher, every page, as of 0.4.4 beta 28 (2026-11-02, after commit 21971be). Second draft,
for the tester's review. Nothing here has been applied yet. Beta 28's text step (community-fixes-plan.md
› Beta 28) checks it again against the launcher as it is then, before any text changes.

## What changes

Today each row has one description. Classic (the default layout) shows its first sentence under the
title, cut at 64 characters: 70 lines end mid-sentence ("Before every start and every…") and about
20 more show a first sentence that doesn't say what the setting does ("Relaxed is the default").

Each row gets two texts instead:

- **A line under the title** (60 characters or fewer). It answers the player's two questions: what
  is this for, and what should I pick? When there is a best choice, the line says so.
- **A tooltip** with the details. Where today's description is right, it stays the tooltip word for
  word: 110 of the 147 descriptions are translated in all 13 languages, and a reworded text loses its
  translations. A description is rewritten only where it is wrong (marked **Rewrite**).

Classic shows the line, and the tooltip on hover. Modern's (i) shows both.

## How the texts were written

1. Every claim is checked in the code, not copied from today's text. (Frame rate: above 30 the game
   runs on the engine's own corrections, not on Kyo's patch as today's text says.)
2. When the title itself is unclear, the title changes (marked **Rename**).
3. A line names the choices only when choosing is the point (HDR source).
4. "Default: …" is added to the tooltip by the row code from each setting's default, one translated
   phrase for all rows. Where the default is the advice for everyone it reads "Default: on
   (recommended)": a default says what is set, a recommendation what to choose (most of Engine
   switches' experimental features). Advice that depends on the PC stays in the row's own text ("only
   for monitors without G-Sync or FreeSync"). "Applies at the next game start." is one phrase too
   (today: 5 wordings).
5. Off or None first in every list, then less to more.

## Reading the tables

- **Line:** the proposed line. "Today's" means today's description already fits and stays as it is.
  "—" means no line (the title says it all).
- **Tooltip:** "Today's" keeps today's description. Otherwise the new text.

## Play

| Row | Line | Tooltip | Notes |
|---|---|---|---|
| Engine update available | This package's engine is older than the launcher | Today's | Shown only then |
| Game folder | — | — | Shows the path |
| The Old Hunters (DLC) | The DLC's areas, bosses, weapons and attire | Today's | |
| DLC folder | — | — | Shows the path |
| Add to Steam | Adds the game to your Steam library | Today's | |
| Game language | Today's: "The game's own text and voices" | — | |
| Player profile | Separate saves and trophies for each player | Today's | |
| Saves folder | — | — | Shows the path |
| Save backups | Automatic backups of this profile's saves | Today's | |
| Import saves | From shadPS4, another port or a decrypted PS4 save | Today's | |

## Display

| Row | Line | Tooltip | Notes |
|---|---|---|---|
| Output resolution | The resolution the game is shown at | Today's | Today's line spoke of the upscaler, which is off by default now |
| Custom resolution | Any output size and shape | Today's | |
| Frame rate | 30 is the PS4's; higher rates get the engine's corrections | **Rewrite:** "30: the game as on the PS4. Above 30 the engine runs the game at that rate with its own frame-rate corrections, built on Kyo's 60 FPS++ and other community findings (Engine switches › Frame rate corrections). With the corrections off, the community FPS++ patches run instead." | The per-rate notes in the list ("Kyo's 60 FPS++ with the engine's values for 60 FPS…") get the same correction |
| V-Sync | Today's: "How finished frames reach the screen" | — | **Order:** Off, On (latest frame), On (every frame), Adaptive |
| Monitor | The monitor the game opens on | Today's | |
| Window mode | Today's: "How the game's window fills the screen" | — | |
| Widescreen | Correct view and UI on screens that aren't 16:9 | Today's | |
| Live resolution change | Change the resolution in game without restarting | Today's | |
| HDR output | Brighter highlights on an HDR screen | Today's | |
| HDR source | Where highlights come from: the scene, or SDR expanded | Today's | |
| UI brightness | How bright the HUD, menus and subtitles are | Today's | Shown only with the Native source |
| UI colour | Vivid or exact (sRGB) colours for the HUD and menus | Today's | Shown only with the Native source |
| Paper white | How bright the game's white is, in nits | Today's | |
| Peak brightness from the display | Uses the peak brightness Windows reports | Today's | |
| Peak brightness | Your display's peak brightness, in nits | Today's | |
| Highlight boost | Brighter lamps, fire and sky | Today's | |
| Picture tuning | Your own brightness, colour, fog and bloom, live | Today's | |
| Brightness, Contrast, Saturation | — | "1.00 is the game's own." | One shared phrase |
| Hue | — | "0 is the game's own." | |
| Fog | How thick the fog is | "1.00 is the game's own." | |
| Bloom | The glow around bright lights | "1.00 is the game's own." | |
| Vignette | Darkens the picture toward its edges, as the game does | "Off: the picture stays even to the edges." | Off sets the game's vignette strength to 0 |
| Ambient occlusion strength | Strength of the shading in corners | "Needs Ambient occlusion (Graphics › Detail). 1.00 is the game's own." | It scales the SSAO pass, which runs only with that effect on |
| Shadow distance | How far away shadows are drawn | "1.00 is the game's own." | |
| Game's own picture | Today's: "Puts the settings above back to the game's own values" | — | |

## Graphics

| Row | Line | Tooltip | Notes |
|---|---|---|---|
| Best settings for this PC | Recommended settings for this PC | Today's, with the hardware summary | |
| Anti-aliasing | Today's: "Smooths edges when no upscaler runs" | Today's advice | |
| Upscaler | Renders below the output and fills it in: faster | Today's advice ("DLSS needs an NVIDIA RTX card; AMD FSR runs on any GPU…") | |
| Quality | Higher looks sharper; lower runs faster | Today's | **Rename: Upscaling quality.** Shown only with FSR or DLSS: how far below the output the game renders |
| Render resolution | The share of the output the game renders at | Today's | |
| FSR version | Automatic runs the newest AMD offers here | Today's advice | |
| DLSS model | Which NVIDIA model DLSS runs | Today's, with its advice | |
| Sharpening (RCAS) | Says where it runs for the chosen upscaler (as today) | Today's advice | |
| Sharpening strength | As today, by upscaler | Today's advice | |
| Character motion vectors | Today's: "Less ghosting on characters; costs about 10% FPS" | Today's advice | |
| Texture mip bias | Today's: "Below 0 sharper textures, above 0 softer" | Today's advice | |
| Model detail | How far away models keep their full detail | "Highest costs GPU time; Lower and Lowest help weaker GPUs." | **Order:** Lowest, Lower, As in the game, Highest |
| Texture filtering | Sharper ground and walls seen at an angle | Today's | **Order:** Game's own, 2x, 4x, 8x, 16x |
| Shadows from dynamic lights | Shadows from lamps, torches and fire | "One of the heaviest effects: off gives a large FPS gain." | The patch's note: "a ton of heavy draw calls" |
| Ambient occlusion (SSAO) | Soft shading in corners and creases | — | |
| Screen-space reflections | Reflections on wet and shiny surfaces | "Not in the original game. Costs GPU time." | The patch's note: lowers performance |
| Depth of field | Blurs what is out of focus | — | |
| Motion blur | Blur when the camera turns fast | "Costs GPU time." | Its status line stays |
| Skip motion blur with upscalers | Upscalers smear it outside 1080p | Today's | **Rename** (was "No motion blur smear with upscalers") |
| Chromatic aberration | Colour fringes toward the screen's edges | — | |

The effect lines describe how each looks; worth a quick look in game before they ship.

## Interface

| Row | Line | Tooltip | Notes |
|---|---|---|---|
| In-game menu shortcut | The keys that open the in-game menu | Today's | |
| In-game menu language | The in-game menu's language | — | |
| Sharp UI | Menus and HUD at the full output resolution | Today's | |
| Note while shaders build | Shows "Building shaders" in a corner | Today's | |
| FPS counter | Frame rate and timings in a corner | Today's ("Also in game…") | |
| Frame rate, Frame time, Upscaler, CPU usage, GPU usage | — | — | "Shown in the FPS counter" five times goes: they sit in its fold |
| FPS counter position, size, text colour, text opacity, background, background opacity, border | — | Today's where there is one | **Rename:** Position, Size, Text colour, Text opacity, Background, Background opacity, Border (inside the FPS counter fold; search shows "Interface › FPS counter") |

## Gameplay

| Row | Line | Tooltip | Notes |
|---|---|---|---|
| Camera tuning | Your own field of view, distance and height | Today's | |
| Follow camera | How the camera follows you at 60 FPS and above | — | Its two choices' notes stay |
| Field of view, Distance, Height | — | — | |
| Skip intro videos | — | — | |
| Free camera | Fly the camera freely: Cross + L3 / Space + Z | — | |
| Debug menu | The game's developer menu (Left touchpad / Tab) | Today's | |
| Hunter's Dream mirror | The Dream's mirror opens the appearance editor | Today's | |
| Rebirth at the altar | Reset your level and attributes at the Altar of Despair | Today's | |

## Controls

| Row | Line | Tooltip | Notes |
|---|---|---|---|
| PC controls | Use the keyboard and mouse layout below | Today's | |
| Mouse camera | The mouse turns the camera directly | Today's | |
| Mouse in menus | A mouse pointer in the game's menus | Today's | |
| Move and look in menus | Keep moving and looking with a menu open | Today's | |
| Mouse sensitivity | How fast the mouse turns the camera | Today's | |
| Invert horizontal, Invert vertical (mouse) | — | — | **Rename:** Invert left / right, Invert up / down |
| Camera auto-rotation | The camera turns by itself as you move | Today's | |
| Hide mouse cursor | After 2 seconds without moving | Today's | |
| Controller | — | — | Shows the controller |
| Preferred gamepad | The controller to use when several are connected | Today's | |
| Swap sticks | Today's: "Move with the right stick and look with the left" | — | |
| Invert camera left / right, up / down (gamepad) | Today's ("The camera stick's horizontal direction", …) | — | **Rename:** Invert left / right, Invert up / down, as the mouse's (search shows "Controls › Gamepad") |
| Stick dead zone | Ignores small stick movement, for drifting sticks | Today's | |
| Button prompts | Keyboard keys or controller buttons in prompts | Today's | |
| Controller icons | PlayStation or Xbox buttons | Today's | Today the two button rows read almost the same |
| Profile | — | — | |
| Save and share | Share a profile as a link or a file | Today's | |

## Engine switches

| Row | Line | Tooltip | Notes |
|---|---|---|---|
| Changed switches | Today's: "Every switch on this page that is not at its default" | — | |
| Extra variables | Engine variables this page doesn't list | Today's | |
| Legacy shader compiler | The old shader compiler, if something draws wrong | **Rewrite:** "the way 0.9.33 did" becomes "the way earlier versions did" | |
| VRAM cleanup | Frees video memory from old GPU-drawn textures | Today's, with its "Recommended: on" | |
| Precise frame pacing | Steadier frame times and less VRR flicker | Today's | |
| Fixed-refresh frame lock | Smoother on monitors without G-Sync or FreeSync | Today's | |
| Particle work safety cap | Against "device lost" crashes in heavy effects | **Rewrite:** "Only for games that crash" becomes "Only if the game crashes" | |
| Use more VRAM for textures | Fewer texture reloads: less stutter on 8 GB cards | Today's | |
| Graphics threads first | Graphics threads at a higher CPU priority | Today's | |
| Shorter sleep spin | More CPU time for the graphics threads | Today's | |
| Background shader compiling | Shorter hitches when new shaders compile | Today's | |
| Occlusion culling | Skips hidden objects: more FPS, some pop-in | Today's | |
| Faster draw thread | Higher FPS in busy areas | Today's | |
| No shader freezes | Objects appear late instead of the game freezing | Today's | |
| Fast loading | Faster loading screens above 60 FPS | Today's | |
| Frame rate corrections | Physics, cloth and timers right at 60 FPS and above | Today's | |
| Unified frame time | Catches up at once after a stutter | Today's | |
| Fewer memory write checks | Up to 2 ms less a frame in busy areas | Today's | |
| Per-frame GPU data in place | Less CPU and GPU time a frame | Today's | |
| Native draw calls | Draw calls reach the engine directly | Today's | |
| Effect trail crash fix | For effect trail crashes on newer Intel CPUs | Today's | Optional **Rename:** Intel crash fix: skip effect trails |
| Two-stage GPU pipeline | Today's: "20–30% faster; turn off if the game is unstable" | — | |
| CPU reads of GPU data | When the game gets back data the GPU wrote | New (checked in the engine): "Off: never. Relaxed: before the game changes that memory. Precise: before it reads it too; slower." | Today's line was only "Relaxed is the default" |
| Sound output | WASAPI, or DirectSound if sound crackles | Today's | |
| Sound threads first | Against sound stutter on many-core CPUs | Today's | |
| Graphics card | Which GPU to use on PCs with two | Today's | |
| Shader compile threads | Threads that compile new shaders in the background | Today's | |
| Area prefetch (hard disks) | Reads an area's files ahead on hard disks | Today's | **Order:** Off, Auto, On |
| Frame rate in the background | The FPS limit while the game is in the background | Today's | |
| Run without sound | Starts silent, to test a sound driver problem | Today's | |
| No minimum loading time after death | Skips the 12-second loading screen after dying | Today's | |
| Weapon sound save fix | Fixes weapon sounds that never play | Today's | |
| OptiScaler compatibility | Keeps OptiScaler from crashing the game | Today's | |
| Ebrietas effect fix | Stops Ebrietas' rocks from piling up | **Rewrite:** without "(crashes before beta 8)" | |
| PS4 floating-point mode | Physics and cloth as on the PS4 | Today's | |
| Intel effect crash fix (test) | Stops the crash and keeps the effects drawn | Today's | Optional **Rename:** Intel crash fix: keep effect trails (test) |
| Intel black tonemap fix | For a black or very dark picture on Intel CPUs | Today's | |
| Skip the game files check | Lets other game versions start; 1.09 works best | Today's | **Rename:** Allow other game versions |

The two Intel crash fixes fight the same crashes from two groups (Experimental features, Game fixes).
Proposal: put them next to each other. Their lines (and the optional renames) say how they differ.

## Log

These texts are translated in zh-CN only, so the tooltips can be reworked: the variable name moves to
the end.

| Row | Line | Tooltip | Notes |
|---|---|---|---|
| Show this page when the game starts | — | — | |
| Log level | How much the game writes in its log | Today's | |
| Frame statistics in the log | Frame times and engine counters every 5 seconds | "On in betas, so crash logs carry them. BB_FRAME_STATS." | Today it says only the variable's name |
| Hitch reports | What held up slow frames, in the log | Today's | |
| GPU time in the log | GPU time a frame by pass, every 5 seconds | "Costs a little GPU time. BB_GPU_PROFILE." | |
| Render stage timings in the log | Where each frame's CPU time goes | Today's, the variable last | |
| Loading profile in the log | Where loading time goes, every 5 seconds | Today's, the variable last | |
| Audio statistics in the log | Sound buffers, gaps and dropouts every 5 seconds | "For crackling or late sound. BB_AUDIO_STATS." | Today it says only the variable's name |
| Controller details in the log | Controller ids, connections and rumble | Today's, the variable last | |
| Heap corruption checks | The GPU's last writes into game memory, for crashes | Today's, the variable last | |
| Capture shaders | Saves every shader the game uses to logs\shaders | Today's, the variable last | **Bug:** today's text is not translated (no L.T) |

## Settings

| Row | Line | Tooltip | Notes |
|---|---|---|---|
| Launcher language | Today's: "Restart the launcher to apply the language." | — | |
| Layout | Classic cards or compact Modern rows | Today's | |
| Navigation | Today's: "Tabs across the top, or a sidebar with the pages in groups" | — | |
| Theme | The accent colour | Today's | |
| Theme colour, Background, Picture, Icon picture, Window buttons | Today's | — | Already short |
| Background blur, Window buttons side | — | — | |
| App icon | The launcher's and the game window's icon | Today's | |
| Engine folder | The Hunter's Workshop folder | "The folder with engine\out\bb-start.exe." | |
| Start the game with | — | — | Its choices' notes stay |
| Launcher settings, Game settings (config.ini) | — | — | Show the paths |
| Shader cache | The game's compiled shaders | Today's | |
| Shader catalog | Share shaders so areas don't stutter the first time | Today's | |
| Share your settings | Your settings as a code or a file | Today's | |
| Import settings | Today's: "Someone's code or file." | Today's | |
| Launcher updates, Build | — | — | |
| Update channel | Stable or beta releases | Today's | |
| Check for updates at start | Today's | — | |
| Version | Install any release, older ones too | Today's | |

## Online

| Row | Line | Tooltip | Notes |
|---|---|---|---|
| Play online | Co-op, messages and bloodstains | Today's | |
| Hunter's Dream account | — | — | |
| Website | Today's: "Players online, the map and your hunter's profile" | — | |
| Local P2P port | The port co-op connects through | Today's ("Empty: 3658, the PS4's") | |
| Summon sign timeout | How long a touched sign waits for its host | Today's | |
| Test the connection | Checks the server, co-op and the summon relay | — | |

## Mods

| Row | Line | Tooltip | Notes |
|---|---|---|---|
| Load mods | Loads the mods in the list below | Today's ("When two mods replace the same file, the one lower in the list wins") | |
| Mods folder, Patches folder | — | — | |
| Each mod | Today's: "Load order N" | — | |

## Cheats

| Row | Line | Tooltip | Notes |
|---|---|---|---|
| Blood Vials, Quicksilver Bullets | Never run out, or keep a set number | — | Their choices' notes stay |
| Infinite consumables | Molotovs, knives, pebbles and the like last forever | Today's | |
| Infinite stamina, Stealth, No weapon wear | Today's | — | Already short |
| Firearm range | How far bullets travel | Today's | |
| Keep Blood Echoes on death | Dying never drops your Blood Echoes | Today's | |
| Blood Echo multiplier | More Blood Echoes from each kill | Today's | |
| Add echoes or insight | Echoes or insight from the in-game menu | Today's | |
| Amount | Today's: "How many … to keep" | — | |

## Trophies

| Row | Line | Tooltip | Notes |
|---|---|---|---|
| Show in game | A banner in a corner when you earn a trophy | Today's | |
| Position, Play a sound, Volume | — | — | |
| Sound file | Your own sound; empty plays the built-in chime | Today's | |
| Try it | Today's: "Shows a sample notification and plays the sound" | — | |
| Trophy log | — | — | |
| Reset trophy progress | Today's: "Starts trophies and completion over, for a fresh run." | Today's | |

## Also found

- **Capture shaders** (Log): its text is not translated (no `L.T`).
- **A dead launcher setting:** `dlss_preset` in `LauncherSettings` (default "k"). The DLSS model row
  writes config.ini's `dlss_preset` (default Auto); nothing reads the launcher's.
- **Stale comments:** `Choices.DlssModels` names `BB_DLSS_PRESET` (now config.ini's `dlss_preset`);
  `Choices.Effects` says the game's own anti-aliasing starts off (on since this beta).

## What the step involves

- About 120 new lines and 15 new or rewritten tooltips, each in all 13 languages; today's descriptions
  stay as tooltips elsewhere, so every page stays as translated as it is now.
- About 15 renamed titles (Upscaling quality, the FPS counter rows, the invert rows, Allow other game
  versions, Skip motion blur with upscalers; the two Intel fixes if wanted).
- The row code: the line and the tooltip as separate fields (no more cutting at 64 characters), and
  "Default: …" from each setting's default.
- Four list orders (V-Sync, Area prefetch, Texture filtering, Model detail).
- Both launchers built, the translation key check clean.

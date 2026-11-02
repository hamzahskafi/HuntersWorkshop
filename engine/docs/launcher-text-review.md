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


## Play

#### Engine update available
| | Now | Proposed |
|---|---|---|
| Title | Engine update available | Same |
| Line under the title (Classic) | This package has an older engine than this launcher… | Update to get the latest fixes and keep your settings |
| Tooltip (hover; Modern's (i)) | This package has an older engine than this launcher: the crash fixes since then are missing. Updating downloads the latest package and keeps your settings, saves and mods. | This package's engine is older than the launcher and lacks the latest crash fixes. Updating downloads the latest package and keeps your settings, saves and mods.<br> |


### Game

#### Game folder
| | Now | Proposed |
|---|---|---|
| Title | Game folder | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | — |
| Note | | Shows the folder. |

#### The Old Hunters (DLC)
| | Now | Proposed |
|---|---|---|
| Title | The Old Hunters (DLC) | Same |
| Line under the title (Classic) | Off: the game starts without its DLC. | Load The Old Hunters expansion |
| Tooltip (hover; Modern's (i)) | Off: the game starts without its DLC. The Game of the Year edition (CUSA03173) always has it | Turn it off to start the game without the expansion. The Game of the Year edition (CUSA03173) always includes it.<br>*[Default: On]* |
| Default | On | Same |

#### DLC folder
| | Now | Proposed |
|---|---|---|
| Title | DLC folder | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | — |
| Note | | Shows the folder. |

#### Add to Steam
| | Now | Proposed |
|---|---|---|
| Title | Add to Steam | Same |
| Line under the title (Classic) | Bloodborne as a non-Steam game in your Steam library… | Add the game to your Steam library |
| Tooltip (hover; Modern's (i)) | Bloodborne as a non-Steam game in your Steam library: Steam Input, the overlay and Big Picture work with it | Adds Bloodborne as a non-Steam game, so Steam Input, the Steam overlay and Big Picture work with it.<br> |

#### Game language
| | Now | Proposed |
|---|---|---|
| Title | Game language | Same |
| Line under the title (Classic) | The game's own text and voices | Choose the language of the game's text and voices |
| Tooltip (hover; Modern's (i)) | The game's own text and voices | *[Default: English]* |
| Choices | English<br>Russian<br>Japanese<br>French<br>Spanish<br>German<br>Italian | Same |
| Default | English | Same |


### Saves

#### Player profile
| | Now | Proposed |
|---|---|---|
| Title | Player profile | Same |
| Line under the title (Classic) | Each profile has its own save files and trophies, so several… | Choose whose saves and trophies to use |
| Tooltip (hover; Modern's (i)) | Each profile has its own save files and trophies, so several people can play their own runs on this PC | Each profile has its own saves and trophies, so several people can play their own runs on this PC.<br> |

#### Saves folder
| | Now | Proposed |
|---|---|---|
| Title | Saves folder | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | — |
| Note | | Shows the folder. |

#### Save backups
| | Now | Proposed |
|---|---|---|
| Title | Save backups | Same |
| Line under the title (Classic) | Before every start and every… | Restore one of this profile's automatic backups |
| Tooltip (hover; Modern's (i)) | Before every start and every {SaveBackups.InGameEvery.TotalMinutes:0} minutes while playing, when this profile's saves changed. The newest {SaveBackups.Keep} from before starting and {SaveBackups.InGameKeep} from playing are kept | A backup is made before every start and every {N} minutes while you play, whenever this profile's saves changed. The newest {K} from before starting and {M} from playing are kept.<br> |

#### Import saves
| | Now | Proposed |
|---|---|---|
| Title | Import saves | Same |
| Line under the title (Classic) | From shadPS4, another Bloodborne port, or a PS4 save exported… | Import saves from shadPS4, another port or a PS4 |
| Tooltip (hover; Modern's (i)) | From shadPS4, another Bloodborne port, or a PS4 save exported decrypted (Apollo Save Tool, Save Wizard). This profile's saves are backed up first | Takes saves from shadPS4, another Bloodborne port, or a PS4 save exported decrypted (Apollo Save Tool, Save Wizard). This profile's saves are backed up first.<br> |


## Display

#### Output resolution
| | Now | Proposed |
|---|---|---|
| Title | Output resolution | Same |
| Line under the title (Classic) | The upscaler renders below this and fills it in | Choose the resolution the game is shown at |
| Tooltip (hover; Modern's (i)) | The upscaler renders below this and fills it in | With an upscaler, the game renders below this and the upscaler fills in the rest.<br>*[Default: 1920 × 1080]* |
| Choices | 1280 × 720<br>1600 × 900<br>1920 × 1080<br>2560 × 1440<br>3840 × 2160<br>2560 × 1080 (21:9)<br>3440 × 1440 (21:9)<br>3840 × 1600 (24:10)<br>5120 × 2160 (21:9)<br>3840 × 1080 (32:9)<br>5120 × 1440 (32:9)<br>7680 × 2160 (32:9)<br>1280 × 1024 (5:4)<br>1280 × 800 (Steam Deck)<br>1680 × 1050 (16:10)<br>1920 × 1200 (16:10)<br>2560 × 1600 (16:10)<br>Display (auto) · Your display's own resolution, whatever its shape: the picture fills the screen<br>Custom · Any width and height: set them below | Same |
| Default | 1920 × 1080 | Same |
| In-game menu | Output resolution | Marked with the in-game icon |

#### Custom resolution
| | Now | Proposed |
|---|---|---|
| Title | Custom resolution | Same |
| Line under the title (Classic) | Your own output size, any shape (5:4, 16:10, 32:9...)… | Enter any output size, in any shape |
| Tooltip (hover; Modern's (i)) | Your own output size, any shape (5:4, 16:10, 32:9...): the camera and UI follow it with Widescreen on | Use any size, such as 5:4, 16:10 or 32:9. With Widescreen on, the camera and the UI follow its shape.<br> |


### Frame rate

#### Frame rate limit (now: Frame rate)
| | Now | Proposed |
|---|---|---|
| Title | Frame rate | Frame rate limit |
| Line under the title (Classic) | Every rate above 30 runs on Kyo's 60 FPS++, the engine setting… | Choose the highest frame rate the game runs at |
| Tooltip (hover; Modern's (i)) | Every rate above 30 runs on Kyo's 60 FPS++, the engine setting the game's values for the rate (Engine switches › Frame rate corrections). 30 is the game as on the PS4 | 30 FPS is the game as on the PS4. Above 30, the engine runs the game at the chosen rate with its own frame-rate corrections, built on Kyo's 60 FPS++ and other community findings (Engine switches › Frame rate corrections). With the corrections off, the community FPS++ patches run instead.<br>*[Default: 60 FPS]* |
| Choices | 30 FPS (as on PS4)<br>60 FPS · Kyo's 60 FPS++ with the engine's values for 60 FPS (Frame rate corrections). On a slower display the extra frames are drawn but never shown<br>90 FPS · Kyo's 60 FPS++ with the engine's values for 90 FPS (Frame rate corrections). On a slower display the extra frames are drawn but never shown<br>120 FPS · Kyo's 60 FPS++ with the engine's values for 120 FPS (Frame rate corrections). On a slower display the extra frames are drawn but never shown<br>144 FPS · Kyo's 60 FPS++ with the engine's values for 144 FPS (Frame rate corrections). On a slower display the extra frames are drawn but never shown<br>240 FPS · Kyo's 60 FPS++ with the engine's values for 240 FPS. Needs Frame rate corrections (Engine switches): without them the game runs at 144. On a slower display the extra frames are drawn but never shown<br>Uncapped · No limit: as fast as your PC can go, up to 480 FPS. Above 240 FPS the game is untested, and the old community uncapped patch froze in boss fights at such rates. For a high frame rate we recommend 240 | 30 FPS (PS4) · As on the PS4<br>60 FPS · Needs a 60 Hz display or faster to show every frame<br>90 FPS · Needs a 90 Hz display or faster to show every frame<br>120 FPS · Needs a 120 Hz display or faster to show every frame<br>144 FPS · Needs a 144 Hz display or faster to show every frame<br>240 FPS · Needs Frame rate corrections and a 240 Hz display<br>Uncapped · Up to 480 FPS. Untested above 240, so 240 is recommended for high frame rates |
| Default | 60 FPS | Same |
| Note | | Renamed: it is a limit (Uncapped is a choice), and the FPS counter has an item called Frame rate. The per-rate notes credited Kyo's patch for what the engine's corrections do. |

#### V-Sync
| | Now | Proposed |
|---|---|---|
| Title | V-Sync | Same |
| Line under the title (Classic) | How finished frames reach the screen | Choose how finished frames reach the screen |
| Tooltip (hover; Modern's (i)) | How finished frames reach the screen | *[Default: Mailbox]* |
| Choices | On (latest frame) · No tearing, low latency: each refresh shows the newest finished frame. Default (Mailbox)<br>On (every frame) · Classic V-Sync: every frame shown in order, no tearing, a little more latency. The game can't run faster than the screen (FIFO)<br>Adaptive · Like On (every frame), but tears instead of stuttering when a frame is late (FIFO Relaxed)<br>Off · Frames go out the moment they're ready: lowest latency, may tear (Immediate) | Off · Lowest latency, with tearing (Immediate)<br>On (latest frame) (recommended) · No tearing and low latency (Mailbox)<br>On (every frame) · Classic V-Sync with a little more latency (FIFO)<br>Adaptive · Tears instead of stuttering when a frame is late (FIFO Relaxed) |
| Default | Mailbox | Same |


### Screen

#### Monitor
| | Now | Proposed |
|---|---|---|
| Title | Monitor | Same |
| Line under the title (Classic) | The game opens here from the start, including fullscreen and… | Choose the monitor the game opens on |
| Tooltip (hover; Modern's (i)) | The game opens here from the start, including fullscreen and HDR. If this monitor is disconnected, the Windows primary monitor is used. Applied on the next launch. | The game opens there, in fullscreen and HDR too. If it is disconnected, the Windows primary monitor is used.<br> |

#### Window mode
| | Now | Proposed |
|---|---|---|
| Title | Window mode | Same |
| Line under the title (Classic) | How the game's window fills the screen | Choose how the game's window fills the screen |
| Tooltip (hover; Modern's (i)) | How the game's window fills the screen | — |
| Choices | Windowed<br>Borderless fullscreen · Trophy notifications and other windows show over the game<br>Fullscreen · The driver may give the game the screen to itself (exclusive fullscreen): the lowest latency and G-SYNC's fullscreen-only mode, but trophy notifications and other windows don't show over it | Windowed<br>Borderless fullscreen · Trophy notifications and other windows can show over the game<br>Fullscreen · Lowest latency and G-SYNC's fullscreen mode, but nothing shows over the game |

#### Widescreen
| | Now | Proposed |
|---|---|---|
| Title | Widescreen | Same |
| Line under the title (Classic) | Outputs that are not 16:9 (21:9, 32:9, 16:10, 5:4) with the… | Fit the view and the UI to screens that aren't 16:9 |
| Tooltip (hover; Modern's (i)) | Outputs that are not 16:9 (21:9, 32:9, 16:10, 5:4) with the right field of view and the UI kept in shape | Widens the field of view on 21:9 and 32:9 screens and keeps the UI in shape on 16:10 and 5:4.<br>*[Default: On (recommended)]* |
| Default | On | Same |


### Advanced (folded)

#### Live resolution change
| | Now | Proposed |
|---|---|---|
| Title | Live resolution change | Same |
| Line under the title (Classic) | Change the resolution in the game menu without restarting. | Change the resolution in game without restarting |
| Tooltip (hover; Modern's (i)) | Change the resolution in the game menu without restarting. Off is faster; turn on only if you switch resolutions in game | Turn it on only if you switch resolutions in the game's menu. Off is faster.<br>*[Default: Off (recommended)]* |
| Choices | Off (recommended)<br>On | Off (recommended)<br>On |
| Default | Off (recommended) | Same |
| In-game menu | Live resolution changes → align to Live resolution change | Marked with the in-game icon |


### HDR

#### HDR output
| | Now | Proposed |
|---|---|---|
| Title | HDR output | Same |
| Line under the title (Classic) | ‹HdrHint› | Show the game in HDR on an HDR display |
| Tooltip (hover; Modern's (i)) | ‹HdrHint› | Brighter highlights and richer colours.<br> |
| In-game menu | HDR output (restart required) | Marked with the in-game icon |

#### HDR source
| | Now | Proposed |
|---|---|---|
| Title | HDR source | Same |
| Line under the title (Classic) | Native: highlights as bright as they are in the game's scene. | Choose where HDR highlights come from |
| Tooltip (hover; Modern's (i)) | Native: highlights as bright as they are in the game's scene. Expanded: the SDR picture's bright parts made brighter | *[Default: Native (default)]* |
| Choices | Native (default)<br>Expanded | Native (recommended) · Highlights as bright as in the game's own scene<br>Expanded · The SDR picture's bright parts made brighter |
| Default | Native (default) | Same |
| In-game menu | HDR source | Marked with the in-game icon |

#### UI brightness
| | Now | Proposed |
|---|---|---|
| Title | UI brightness | Same |
| Line under the title (Classic) | Native source only: how bright the HUD, menus and subtitles are… | Choose how bright the HUD, menus and subtitles are |
| Tooltip (hover; Modern's (i)) | Native source only: how bright the HUD, menus and subtitles are, against paper white | Relative to paper white.<br>*[Default: Paper white (default)]* |
| Choices | Dimmer<br>Paper white (default)<br>Brighter<br>Much brighter | Dimmer<br>Paper white (recommended)<br>Brighter<br>Much brighter |
| Default | Paper white (default) | Same |
| In-game menu | UI brightness | Marked with the in-game icon |

#### UI colour
| | Now | Proposed |
|---|---|---|
| Title | UI colour | Same |
| Line under the title (Classic) | Native source only: Vivid shows the HUD and menus with the… | Choose the colour range of the HUD and menus |
| Tooltip (hover; Modern's (i)) | Native source only: Vivid shows the HUD and menus with the colours a wide-gamut monitor gives SDR; Exact keeps sRGB's | Vivid uses the wider colours an HDR display gives SDR content. Exact keeps sRGB colours.<br>*[Default: Vivid (default)]* |
| Choices | Exact (sRGB)<br>In between<br>Vivid (default) | Exact (sRGB)<br>In between<br>Vivid (recommended) |
| Default | Vivid (default) | Same |
| In-game menu | UI colour | Marked with the in-game icon |

#### Paper white
| | Now | Proposed |
|---|---|---|
| Title | Paper white | Same |
| Line under the title (Classic) | How bright the game's white is, in nits. Range: 10–500 | Adjust how bright white is, in nits |
| Tooltip (hover; Modern's (i)) | How bright the game's white is, in nits. Range: 10–500 | Match it to the SDR content brightness you use in Windows. Range 10 to 500.<br>*[Default: 200]* |
| Default | 200 | Same |
| In-game menu | Paper white (nits) | Marked with the in-game icon |

#### Peak brightness from the display
| | Now | Proposed |
|---|---|---|
| Title | Peak brightness from the display | Same |
| Line under the title (Classic) | The peak Windows reports for the display the game is on (the… | Use the peak brightness Windows reports |
| Tooltip (hover; Modern's (i)) | The peak Windows reports for the display the game is on (the Windows HDR Calibration app's, once it has run). Off: the value below | Windows reports it once the Windows HDR Calibration app has run. Turn it off to set it yourself below.<br>*[Default: On]* |
| Default | On | Same |
| In-game menu | Peak brightness from the display | Marked with the in-game icon |

#### Peak brightness
| | Now | Proposed |
|---|---|---|
| Title | Peak brightness | Same |
| Line under the title (Classic) | Your display's peak brightness, in nits, when it is not taken… | Adjust your display's peak brightness, in nits |
| Tooltip (hover; Modern's (i)) | Your display's peak brightness, in nits, when it is not taken from the display. Range: 10–4000 | Used when the peak is not taken from Windows. Range 10 to 4000.<br>*[Default: 1000]* |
| Default | 1000 | Same |
| In-game menu | Peak brightness (nits) | Marked with the in-game icon |

#### Highlight boost
| | Now | Proposed |
|---|---|---|
| Title | Highlight boost | Same |
| Line under the title (Classic) | Off keeps the game's own look… | Choose how much brighter lamps, fire and the sky get |
| Tooltip (hover; Modern's (i)) | Off keeps the game's own look; higher makes lamps, fire and the sky brighter | Off keeps the game's own look.<br>*[Default: Low (default)]* |
| Choices | Off<br>Low (default)<br>Medium<br>High | Off<br>Low (recommended)<br>Medium<br>High |
| Default | Low (default) | Same |
| In-game menu | Highlight boost | Marked with the in-game icon |


### Picture

#### Picture tuning
| | Now | Proposed |
|---|---|---|
| Title | Picture tuning | Same |
| Line under the title (Classic) | Applies the settings below, live… | Apply your own brightness, colour, fog and bloom |
| Tooltip (hover; Modern's (i)) | Applies the settings below, live; off shows the game's own picture and keeps your values. Also in the game's System menu (Graphics, Effects, Colour) and the Insert menu | Changes apply live. Turn it off to see the game's own picture; your values are kept. Also in the game's System menu.<br> |
| In-game menu | Picture tuning | Marked with the in-game icon |

#### Brightness
| | Now | Proposed |
|---|---|---|
| Title | Brightness | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | 1.00 is the game's own.<br> |
| In-game menu | Brightness | Marked with the in-game icon |

#### Contrast
| | Now | Proposed |
|---|---|---|
| Title | Contrast | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | 1.00 is the game's own.<br> |
| In-game menu | Contrast | Marked with the in-game icon |

#### Saturation
| | Now | Proposed |
|---|---|---|
| Title | Saturation | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | 1.00 is the game's own.<br> |
| In-game menu | Saturation | Marked with the in-game icon |

#### Hue
| | Now | Proposed |
|---|---|---|
| Title | Hue | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | 0 is the game's own.<br> |
| In-game menu | Hue | Marked with the in-game icon |

#### Fog
| | Now | Proposed |
|---|---|---|
| Title | Fog | Same |
| Line under the title (Classic) | — | Adjust how thick the fog is |
| Tooltip (hover; Modern's (i)) | — | 1.00 is the game's own.<br> |
| In-game menu | Fog | Marked with the in-game icon |

#### Bloom
| | Now | Proposed |
|---|---|---|
| Title | Bloom | Same |
| Line under the title (Classic) | — | Adjust the glow around bright lights |
| Tooltip (hover; Modern's (i)) | — | 1.00 is the game's own.<br> |
| In-game menu | Bloom | Marked with the in-game icon |

#### Vignette
| | Now | Proposed |
|---|---|---|
| Title | Vignette | Same |
| Line under the title (Classic) | — | Darken the edges of the screen |
| Tooltip (hover; Modern's (i)) | — | The game's own look. Turn it off for an evenly lit picture.<br>*[Default: On]* |
| Default | On | Same |
| In-game menu | Vignette | Marked with the in-game icon |

#### Ambient occlusion strength
| | Now | Proposed |
|---|---|---|
| Title | Ambient occlusion strength | Same |
| Line under the title (Classic) | — | Adjust the shading in corners and creases |
| Tooltip (hover; Modern's (i)) | — | Needs Ambient occlusion on (Graphics › Detail). 1.00 is the game's own.<br> |
| In-game menu | Ambient occlusion strength | Marked with the in-game icon |
| Note | | It scales the SSAO pass, which runs only with that effect on. |

#### Shadow distance
| | Now | Proposed |
|---|---|---|
| Title | Shadow distance | Same |
| Line under the title (Classic) | — | Adjust how far away shadows are drawn |
| Tooltip (hover; Modern's (i)) | — | 1.00 is the game's own.<br> |
| In-game menu | Shadow distance | Marked with the in-game icon |

#### Game's own picture
| | Now | Proposed |
|---|---|---|
| Title | Game's own picture | Same |
| Line under the title (Classic) | Puts the settings above back to the game's own values | Reset the settings above to the game's own |
| Tooltip (hover; Modern's (i)) | Puts the settings above back to the game's own values | — |
| In-game menu | Game's own picture | Marked with the in-game icon |


## Graphics

#### Best settings for this PC
| | Now | Proposed |
|---|---|---|
| Title | Best settings for this PC | Same |
| Line under the title (Classic) | Output resolution, frame rate, upscaler and quality for this… | Get recommended settings for this PC |
| Tooltip (hover; Modern's (i)) | Output resolution, frame rate, upscaler and quality for this PC<br><br> | Suggests the output resolution, frame rate, upscaler and quality for your hardware. You see every change before it applies.<br> |


### Upscaling and anti-aliasing

#### Anti-aliasing
| | Now | Proposed |
|---|---|---|
| Title | Anti-aliasing | Same |
| Line under the title (Classic) | Smooths edges when no upscaler runs | Choose how edges are smoothed without an upscaler |
| Tooltip (hover; Modern's (i)) | Smooths edges when no upscaler runs<br><br>Off: the sharpest, with jagged edges. The game's own: its PS4 pass, after a restart. TAA: smooth edges, live. | The game's own is its PS4 anti-aliasing. TAA smooths edges more and works with sharpening. Off is the sharpest, with jagged edges.<br> |
| Status line | With an upscaler the box shows "Upscaler (DLSS)" (or AMD FSR), greyed | Same |
| Choices | Off<br>The game's own · Applied when the game starts<br>TAA | Off<br>The game's own (recommended)<br>TAA |
| In-game menu | Anti-aliasing | Marked with the in-game icon |
| Note | | The note "Applied when the game starts" under The game's own goes: everything in the launcher applies at the next start. |

#### Upscaler
| | Now | Proposed |
|---|---|---|
| Title | Upscaler | Same |
| Line under the title (Classic) | Renders below the output and fills it in | Render at a lower resolution and upscale for more FPS |
| Tooltip (hover; Modern's (i)) | Renders below the output and fills it in<br><br>DLSS needs an NVIDIA RTX card; AMD FSR runs on any GPU. Both anti-alias as they upscale. | DLSS needs an NVIDIA RTX graphics card. AMD FSR works on any graphics card. Both also smooth edges.<br>*[Default: off]* |
| Status line | Status line when an upscaler is unavailable on this PC | Same |
| Choices | None<br>AMD FSR<br>DLSS (NVIDIA RTX) | None<br>AMD FSR<br>DLSS (NVIDIA RTX) |
| Default | off | Same |
| In-game menu | Upscaler | Marked with the in-game icon |

#### Upscaling quality (now: Quality)
| | Now | Proposed |
|---|---|---|
| Title | Quality | Upscaling quality |
| Line under the title (Classic) | How far below the output the game renders… | Choose between sharpness and frame rate |
| Tooltip (hover; Modern's (i)) | How far below the output the game renders: lower is faster and softer. Auto: Native AA up to 1200 lines, Quality up to 1600, Performance above | Sets how far below the output resolution the game renders before upscaling. Auto picks Native AA up to 1200 lines, Quality up to 1600 and Performance above.<br> |
| Choices | — | Auto (recommended)<br>Native AA<br>Quality<br>Balanced<br>Performance<br>Ultra Performance<br>Custom |
| In-game menu | Quality → rename there too | Marked with the in-game icon |
| Note | | Shown only with an upscaler. "Quality" alone reads as overall graphics quality. |

#### Render resolution
| | Now | Proposed |
|---|---|---|
| Title | Render resolution | Same |
| Line under the title (Classic) | The share of the output's width and height the game renders at. | Adjust the share of the output the game renders at |
| Tooltip (hover; Modern's (i)) | The share of the output's width and height the game renders at. DLSS runs the nearest quality's model | Used with Custom quality. DLSS uses the model of the nearest quality.<br> |
| In-game menu | Render resolution | Marked with the in-game icon |

#### FSR version
| | Now | Proposed |
|---|---|---|
| Title | FSR version | Same |
| Line under the title (Classic) | The versions AMD offers on this GPU; Automatic runs the newest | Choose the AMD FSR version |
| Tooltip (hover; Modern's (i)) | The versions AMD offers on this GPU; Automatic runs the newest<br><br>Newer is usually better. (driver) marks one AMD's graphics driver provides. | Lists the versions AMD offers for this graphics card. Automatic runs the newest. (driver) marks one that comes with AMD's graphics driver.<br> |
| Choices | — | Automatic (recommended)<br>…the versions AMD offers |
| In-game menu | FSR version | Marked with the in-game icon |

#### DLSS model
| | Now | Proposed |
|---|---|---|
| Title | DLSS model | Same |
| Line under the title (Classic) | Which NVIDIA model DLSS runs; also in game (Insert or L3 + R3) | Choose the NVIDIA model DLSS uses |
| Tooltip (hover; Modern's (i)) | Which NVIDIA model DLSS runs; also in game (Insert or L3 + R3)<br><br>Auto: K from DLAA to Balanced, M at Performance, L at Ultra Performance. | Auto uses model K up to Balanced, M at Performance and L at Ultra Performance.<br>*[Default: Auto (per quality)]* |
| Status line | Status line with Auto: "Auto: model K at this quality" (the model Auto picks) | Same |
| Choices | Auto (per quality) · NVIDIA's DLSS 4.5 pairing: K for DLAA, Quality and Balanced, M for Performance, L for Ultra Performance<br>Transformer (preset K) · The newer model: steadier thin and moving objects (a weapon on the back, railings)<br>Transformer 2 (preset M) · DLSS 4.5's model for Performance: sharper, a little heavier<br>Transformer 2 (preset L) · DLSS 4.5's model for Ultra Performance<br>Transformer (preset J) · Like K, slightly less ghosting and a little more flicker<br>The DLL's default · Whatever the included DLSS version picks for each quality mode | Auto (recommended) · NVIDIA's pairing for each quality<br>Transformer (preset K) · Steadier thin and moving objects<br>Transformer 2 (preset M) · Sharper at Performance, a little heavier<br>Transformer 2 (preset L) · For Ultra Performance<br>Transformer (preset J) · Like K, with less ghosting and more flicker<br>The DLL's default · Whatever the included DLSS version picks |
| Default | Auto (per quality) | Same |
| In-game menu | DLSS model | Marked with the in-game icon |

#### Sharpening (now: Sharpening (RCAS))
| | Now | Proposed |
|---|---|---|
| Title | Sharpening (RCAS) | Sharpening |
| Line under the title (Classic) | — | Restore detail that upscaling softens |
| Tooltip (hover; Modern's (i)) | AMD's RCAS. Brings back detail that upscaling softens. | Uses AMD's RCAS. With AMD FSR it is FSR's own sharpening; with DLSS and TAA it runs after them. Needs an upscaler or TAA.<br>*[Default: On]* |
| Status line | Line set by the upscaler: DLSS "After DLSS (it has none of its own)", AMD FSR "FSR's own sharpening", TAA "After TAA", none "Needs an upscaler or TAA" | Shown only when it explains why the setting is unavailable |
| Default | On | Same |
| In-game menu | Sharpening (RCAS) → Sharpening | Marked with the in-game icon |
| Note | | Its status line ("Needs an upscaler or TAA") shows only when it can't run. |

#### Sharpening strength
| | Now | Proposed |
|---|---|---|
| Title | Sharpening strength | Same |
| Line under the title (Classic) | — | Adjust how strong the sharpening is |
| Tooltip (hover; Modern's (i)) | 0.20 is light. High values can halo edges and show grain. | 0.20 is light. High values can add halos and grain. With AMD FSR, values above 1 add a second pass.<br>*[Default: 0.20]* |
| Status line | Line set by the upscaler: AMD FSR "Above 1 adds a second pass", otherwise "Higher is sharper" | Shown only when it explains why the setting is unavailable |
| Default | 0.20 | Same |
| In-game menu | Sharpness → Sharpening strength | Marked with the in-game icon |


### Advanced (folded)

#### Character motion vectors
| | Now | Proposed |
|---|---|---|
| Title | Character motion vectors | Same |
| Line under the title (Classic) | Less ghosting on characters; costs about 10% FPS | Reduce ghosting on moving characters |
| Tooltip (hover; Modern's (i)) | Less ghosting on characters; costs about 10% FPS<br><br>The game gives no motion for moving characters; this works it out. Off: faster, but moving enemies can trail. After a restart. | The game gives no motion for moving characters, so the engine works it out for the upscaler. Turn it off for more FPS if you accept trails behind moving enemies.<br>*[Default: On (recommended)]* |
| Default | On | Same |
| In-game menu | Character motion vectors | Marked with the in-game icon |
| Performance | — | Medium (about 10% FPS, measured) |

#### Texture mip bias
| | Now | Proposed |
|---|---|---|
| Title | Texture mip bias | Same |
| Line under the title (Classic) | Below 0 sharper textures, above 0 softer | Adjust how sharp textures are |
| Tooltip (hover; Modern's (i)) | Below 0 sharper textures, above 0 softer<br><br>0 is what NVIDIA and AMD advise with an upscaler. Below 0 can shimmer. | Below 0 is sharper but can shimmer; above 0 is softer. 0 is what NVIDIA and AMD advise with an upscaler.<br>*[Default: 0]* |
| Default | 0 | Same |
| In-game menu | Texture mip bias | Marked with the in-game icon |


### Detail

#### Model detail
| | Now | Proposed |
|---|---|---|
| Title | Model detail | Same |
| Line under the title (Classic) | — | Choose how far away models keep their full detail |
| Tooltip (hover; Modern's (i)) | — | Highest costs graphics card time. Lower and Lowest help weaker graphics cards.<br>*[Default: As in the game (recommended)]* |
| Choices | As in the game<br>Highest (-2)<br>Lower (1)<br>Lowest (2) | Lowest<br>Lower<br>As in the game (recommended)<br>Highest |
| Default | As in the game | Same |
| In-game menu | Model detail | Marked with the in-game icon |
| Performance | — | To measure (Highest) |

#### Texture filtering
| | Now | Proposed |
|---|---|---|
| Title | Texture filtering | Same |
| Line under the title (Classic) | Anisotropic filtering forced on the game's textures… | Keep surfaces sharp at steep angles |
| Tooltip (hover; Modern's (i)) | Anisotropic filtering forced on the game's textures: ground and walls seen at an angle stay sharp. Lower values cost a little less GPU time; Game's own leaves the game's filtering as it is<br>BB_ANISO, default: 16x. Applied when the game starts | Forces anisotropic filtering on the game's textures, so ground and walls seen at an angle stay sharp. Lower values cost a little less.<br>*[Default: 16x (recommended)]* |
| Choices | 16x<br>8x<br>4x<br>2x<br>Game's own | Game's own<br>2x<br>4x<br>8x<br>16x (recommended) |
| Default | 16x | Same |
| Performance | — | Low |

#### Ambient occlusion (SSAO)
| | Now | Proposed |
|---|---|---|
| Title | Ambient occlusion (SSAO) | Same |
| Line under the title (Classic) | — | Add soft shading in corners and creases |
| Tooltip (hover; Modern's (i)) | — | *[Default: On]* |
| Default | On | Same |
| In-game menu | Ambient occlusion (SSAO) | Marked with the in-game icon |
| Performance | — | To measure |

#### Shadows from dynamic lights
| | Now | Proposed |
|---|---|---|
| Title | Shadows from dynamic lights | Same |
| Line under the title (Classic) | — | Let lamps and torches cast shadows |
| Tooltip (hover; Modern's (i)) | — | One of the heaviest effects. Turn it off for a large FPS gain.<br>*[Default: On]* |
| Default | On | Same |
| In-game menu | Shadows from dynamic lights | Marked with the in-game icon |
| Performance | — | High (the patch's note) |

#### Screen-space reflections
| | Now | Proposed |
|---|---|---|
| Title | Screen-space reflections | Same |
| Line under the title (Classic) | Not in the original game | Add reflections on wet and shiny surfaces |
| Tooltip (hover; Modern's (i)) | Not in the original game | Not in the original game. Costs performance.<br>*[Default: Off]* |
| Default | Off | Same |
| In-game menu | Screen-space reflections | Marked with the in-game icon |
| Performance | — | Medium (the patch's note) |


### Effects

#### Chromatic aberration
| | Now | Proposed |
|---|---|---|
| Title | Chromatic aberration | Same |
| Line under the title (Classic) | — | Add colour fringes toward the screen's edges |
| Tooltip (hover; Modern's (i)) | — | *[Default: Off]* |
| Default | Off | Same |
| In-game menu | Chromatic aberration | Marked with the in-game icon |
| Performance | — | Low (to measure) |

#### Depth of field
| | Now | Proposed |
|---|---|---|
| Title | Depth of field | Same |
| Line under the title (Classic) | — | Blur what is out of focus |
| Tooltip (hover; Modern's (i)) | — | *[Default: On]* |
| Default | On | Same |
| In-game menu | Depth of field (DoF) | Marked with the in-game icon |
| Performance | — | To measure |

#### Motion blur
| | Now | Proposed |
|---|---|---|
| Title | Motion blur | Same |
| Line under the title (Classic) | — | Blur the picture when the camera turns fast |
| Tooltip (hover; Modern's (i)) | — | Costs performance.<br>*[Default: Off]* |
| Default | Off | Same |
| In-game menu | Motion blur | Marked with the in-game icon |
| Performance | — | High (the in-game menu's note) |
| Note | | Its status line stays. |

#### Skip motion blur with upscalers (now: No motion blur smear with upscalers)
| | Now | Proposed |
|---|---|---|
| Title | No motion blur smear with upscalers | Skip motion blur with upscalers |
| Line under the title (Classic) | With an upscaler at an output other than 1920x1080 the game's… | Leave motion blur out while an upscaler runs |
| Tooltip (hover; Modern's (i)) | With an upscaler at an output other than 1920x1080 the game's motion blur is left out: the upscaler would smear it over the whole picture. Off (default): the blur stays. The Motion blur setting is not changed<br>BB_UPSCALER_NO_BLUR, default: off. Applied when the game starts | At outputs other than 1920 x 1080, upscalers smear the game's motion blur across the picture. The Motion blur setting itself is not changed.<br>*[Default: off]* |
| Default | off | Same |


## Interface


### Menus and HUD

#### In-game menu shortcut
| | Now | Proposed |
|---|---|---|
| Title | In-game menu shortcut | Same |
| Line under the title (Classic) | What opens the port's settings menu while playing. | Choose what opens the in-game menu |
| Tooltip (hover; Modern's (i)) | What opens the port's settings menu while playing. Applied on the next launch | *[Default: Insert or L3 + R3 (recommended)]* |
| Choices | Off · The menu never opens; settings stay in the launcher and the game's PC menus<br>Insert only · Pressing both sticks in a fight no longer opens it<br>Insert or L3 + R3 · The keyboard's Insert key, or both sticks pressed on a controller | Off · The menu never opens. Settings stay in the launcher and the game's PC menus<br>Insert only · Pressing both sticks in a fight no longer opens it<br>Insert or L3 + R3 (recommended) · The Insert key, or both sticks pressed on a controller |
| Default | Insert or L3 + R3 | Same |

#### In-game menu language
| | Now | Proposed |
|---|---|---|
| Title | In-game menu language | Same |
| Line under the title (Classic) | The port's settings menu (Insert or L3 + R3) | Choose the in-game menu's language |
| Tooltip (hover; Modern's (i)) | The port's settings menu (Insert or L3 + R3) | *[Default: English]* |
| Choices | 简体中文<br>English<br>Español<br>Português (Brasil)<br>Français<br>Deutsch<br>Italiano<br>Polski<br>Türkçe<br>Русский | Same |
| Default | English | Same |

#### Sharp UI
| | Now | Proposed |
|---|---|---|
| Title | Sharp UI | Same |
| Line under the title (Classic) | Menus, HUD and text drawn at the output resolution instead of… | Draw menus and HUD at the full output resolution |
| Tooltip (hover; Modern's (i)) | Menus, HUD and text drawn at the output resolution instead of 1080p stretched up: much sharper at 1440p and 4K. New: on by default in betas while it is tested. Applied on the next launch | Much sharper at 1440p and 4K than the game's 1080p UI scaled up. On by default in betas while it is tested.<br>*[Default: On in betas, off in stable releases]* |
| Default | On in betas, off in stable releases | Same |

#### Note while shaders build
| | Now | Proposed |
|---|---|---|
| Title | Note while shaders build | Same |
| Line under the title (Classic) | "Building shaders" in a corner while the game builds shaders for… | Show "Building shaders" while new shaders compile |
| Tooltip (hover; Modern's (i)) | "Building shaders" in a corner while the game builds shaders for a place it shows for the first time. Also in game: Insert or L3 + R3 | Appears in a corner when the game shows a place for the first time.<br>*[Default: On]* |
| Default | On | Same |
| In-game menu | Note while shaders build | Marked with the in-game icon |


### FPS counter

#### FPS counter
| | Now | Proposed |
|---|---|---|
| Title | FPS counter | Same |
| Line under the title (Classic) | Also in game: Insert or L3 + R3 | Show the frame rate and timings in a corner |
| Tooltip (hover; Modern's (i)) | Also in game: Insert or L3 + R3 | *[Default: Off]* |
| Default | Off | Same |
| In-game menu | FPS counter | Marked with the in-game icon |
| Note | | The in-game mark replaces "Also in game: Insert or L3 + R3". |

#### Frame rate
| | Now | Proposed |
|---|---|---|
| Title | Frame rate | Same |
| Line under the title (Classic) | Shown in the FPS counter | — |
| Tooltip (hover; Modern's (i)) | Shown in the FPS counter | *[Default: On]* |
| Default | On | Same |
| In-game menu | Frame rate | Marked with the in-game icon |
| Note | | "Shown in the FPS counter" goes: the item sits in its fold. |

#### Frame time
| | Now | Proposed |
|---|---|---|
| Title | Frame time | Same |
| Line under the title (Classic) | Shown in the FPS counter | — |
| Tooltip (hover; Modern's (i)) | Shown in the FPS counter | *[Default: On]* |
| Default | On | Same |
| In-game menu | Frame time | Marked with the in-game icon |

#### Upscaler
| | Now | Proposed |
|---|---|---|
| Title | Upscaler | Same |
| Line under the title (Classic) | Shown in the FPS counter | — |
| Tooltip (hover; Modern's (i)) | Shown in the FPS counter | *[Default: On]* |
| Default | On | Same |
| In-game menu | Upscaler | Marked with the in-game icon |

#### CPU usage
| | Now | Proposed |
|---|---|---|
| Title | CPU usage | Same |
| Line under the title (Classic) | Shown in the FPS counter | — |
| Tooltip (hover; Modern's (i)) | Shown in the FPS counter | *[Default: Off]* |
| Default | Off | Same |
| In-game menu | CPU usage | Marked with the in-game icon |

#### GPU usage
| | Now | Proposed |
|---|---|---|
| Title | GPU usage | Same |
| Line under the title (Classic) | Shown in the FPS counter | — |
| Tooltip (hover; Modern's (i)) | Shown in the FPS counter | *[Default: Off]* |
| Default | Off | Same |
| In-game menu | GPU usage | Marked with the in-game icon |

#### Position (now: FPS counter position)
| | Now | Proposed |
|---|---|---|
| Title | FPS counter position | Position |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | *[Default: Top right]* |
| Choices | Top right<br>Top left<br>Bottom right<br>Bottom left<br>Top centre | Same |
| Default | Top right | Same |
| In-game menu | Position | Marked with the in-game icon |
| Note | | These seven take the in-game menu's names; search shows "Interface › FPS counter" with them. |

#### Size (now: FPS counter size)
| | Now | Proposed |
|---|---|---|
| Title | FPS counter size | Size |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | *[Default: 1.00]* |
| Default | 1.00 | Same |
| In-game menu | Size | Marked with the in-game icon |

#### Text colour (now: FPS counter text colour)
| | Now | Proposed |
|---|---|---|
| Title | FPS counter text colour | Text colour |
| Line under the title (Classic) | Any colour in game: Insert or L3 + R3 › Other | — |
| Tooltip (hover; Modern's (i)) | Any colour in game: Insert or L3 + R3 › Other | More colours in the in-game menu.<br>*[Default: White]* |
| Choices | White<br>Yellow<br>Green<br>Cyan<br>Red<br>Orange<br>Pink<br>Black | Same |
| Default | White | Same |
| In-game menu | Text colour | Marked with the in-game icon |

#### Text opacity (now: FPS counter text opacity)
| | Now | Proposed |
|---|---|---|
| Title | FPS counter text opacity | Text opacity |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | *[Default: 1.00]* |
| Default | 1.00 | Same |
| In-game menu | Text opacity | Marked with the in-game icon |

#### Background colour (now: FPS counter background)
| | Now | Proposed |
|---|---|---|
| Title | FPS counter background | Background colour |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | *[Default: Dark grey]* |
| Choices | Dark grey<br>Black<br>Dark red<br>Dark blue<br>White | Same |
| Default | Dark grey | Same |
| In-game menu | Background colour | Marked with the in-game icon |

#### Background opacity (now: FPS counter background opacity)
| | Now | Proposed |
|---|---|---|
| Title | FPS counter background opacity | Background opacity |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | *[Default: 0.50]* |
| Default | 0.50 | Same |
| In-game menu | Background opacity | Marked with the in-game icon |

#### Border (now: FPS counter border)
| | Now | Proposed |
|---|---|---|
| Title | FPS counter border | Border |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | *[Default: On]* |
| Default | On | Same |
| In-game menu | Border | Marked with the in-game icon |


## Gameplay


### Camera

#### Follow camera
| | Now | Proposed |
|---|---|---|
| Title | Follow camera | Same |
| Line under the title (Classic) | Original keeps the game's camera distance, angle and follow… | Choose how the camera follows you at high frame rates |
| Tooltip (hover; Modern's (i)) | Original keeps the game's camera distance, angle and follow speed at 60 FPS and above | *[Default: Original (the game's camera)]* |
| Choices | Original (the game's camera) · As the game has it, at any frame rate<br>FPS++ retune · Kyo's 60 FPS++ camera: further back and slower to follow | Original (recommended) · The game's own camera, at any frame rate<br>FPS++ retune · Kyo's 60 FPS++ camera, further back and slower to follow |
| Default | Original (the game's camera) | Same |

#### Camera tuning
| | Now | Proposed |
|---|---|---|
| Title | Camera tuning | Same |
| Line under the title (Classic) | Applies the three settings below. | Apply your own field of view, distance and height |
| Tooltip (hover; Modern's (i)) | Applies the three settings below. Also in the game's System menu (Camera Tuning) | Also in the game's System menu (Camera Tuning).<br> |
| In-game menu | Camera tuning | Marked with the in-game icon |

#### Field of view
| | Now | Proposed |
|---|---|---|
| Title | Field of view | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | 1.00 is the game's own.<br> |
| In-game menu | Camera FOV scale → Field of view | Marked with the in-game icon |

#### Distance
| | Now | Proposed |
|---|---|---|
| Title | Distance | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | 1.00 is the game's own.<br> |
| In-game menu | Camera distance scale → Distance | Marked with the in-game icon |

#### Height
| | Now | Proposed |
|---|---|---|
| Title | Height | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | 1.00 is the game's own.<br> |
| In-game menu | Camera height scale → Height | Marked with the in-game icon |


### Extras

#### Skip intro videos
| | Now | Proposed |
|---|---|---|
| Title | Skip intro videos | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | *[Default: On]* |
| Default | On | Same |

#### Free camera
| | Now | Proposed |
|---|---|---|
| Title | Free camera | Same |
| Line under the title (Classic) | Toggle with Cross + L3 / Space + Z | Fly the camera freely |
| Tooltip (hover; Modern's (i)) | Toggle with Cross + L3 / Space + Z | Toggle it with Cross + L3, or Space + Z on the keyboard.<br>*[Default: Off]* |
| Default | Off | Same |

#### Debug menu
| | Now | Proposed |
|---|---|---|
| Title | Debug menu | Same |
| Line under the title (Classic) | Left touchpad / Tab. Needs the adhoc folder from Nexus mod #253… | Restore the game's developer menu |
| Tooltip (hover; Modern's (i)) | Left touchpad / Tab. Needs the adhoc folder from Nexus mod #253 (adhoc/font) in the game's dvdroot_ps4 or as a mod; without it the patch is not applied | Open it with the left touchpad or Tab. Needs the adhoc font folder from Nexus mod #253 in the game's dvdroot_ps4 or as a mod; without it the patch is not applied.<br>*[Default: Off]* |
| Default | Off | Same |

#### Hunter's Dream mirror
| | Now | Proposed |
|---|---|---|
| Title | Hunter's Dream mirror | Same |
| Line under the title (Classic) | The Dream's mirror opens the appearance editor (the game's… | Use the Dream's mirror to change your appearance |
| Tooltip (hover; Modern's (i)) | The Dream's mirror opens the appearance editor (the game's unused "Put on Disguise"). Applied when the game starts | Opens the appearance editor, the game's unused Put on Disguise.<br> |

#### Rebirth at the altar
| | Now | Proposed |
|---|---|---|
| Title | Rebirth at the altar | Same |
| Line under the title (Classic) | With the Yharnam Stone, the Altar of Despair resets your level… | Reset your level and attributes at the Altar of Despair |
| Tooltip (hover; Modern's (i)) | With the Yharnam Stone, the Altar of Despair resets your level and attributes. Applied when the game starts | Needs the Yharnam Stone.<br> |
| Note | | As the tester approved. |


## Controls


### Keyboard & mouse

#### PC controls
| | Now | Proposed |
|---|---|---|
| Title | PC controls | Same |
| Line under the title (Classic) | The keyboard and mouse layout below… | Use the keyboard and mouse layout below |
| Tooltip (hover; Modern's (i)) | The keyboard and mouse layout below; off keeps the gamepad-style keys | Turn it off to keep the controller-style keys.<br>*[Default: On]* |
| Default | On | Same |
| Note | | As the tester approved. |

#### Mouse camera
| | Now | Proposed |
|---|---|---|
| Title | Mouse camera | Same |
| Line under the title (Classic) | The mouse turns the game's own camera by raw counts, at any… | Turn the camera directly with the mouse |
| Tooltip (hover; Modern's (i)) | The mouse turns the game's own camera by raw counts, at any frame rate | Uses raw mouse movement, the same at any frame rate.<br>*[Default: On]* |
| Default | On | Same |

#### Mouse in menus
| | Now | Proposed |
|---|---|---|
| Title | Mouse in menus | Same |
| Line under the title (Classic) | The mouse becomes a pointer in the game's menus… | Use the mouse as a pointer in the game's menus |
| Tooltip (hover; Modern's (i)) | The mouse becomes a pointer in the game's menus: hover and click items | Hover over and click items.<br>*[Default: On]* |
| Default | On | Same |

#### Move and look in menus
| | Now | Proposed |
|---|---|---|
| Title | Move and look in menus | Same |
| Line under the title (Classic) | With a menu open in the world, the movement and look keys stay… | Keep moving and looking with a menu open |
| Tooltip (hover; Modern's (i)) | With a menu open in the world, the movement and look keys stay your character's and the arrow keys move through the menu. The mouse turns the camera while Mouse in menus is off. Whether the character can walk then is up to the game. Applied at the next start | With a menu open in the world, the movement and look keys stay your character's and the arrow keys move through the menu. While Mouse in menus is off, the mouse turns the camera. Whether your character can walk then is up to the game.<br>*[Default: Off]* |
| Default | Off | Same |

#### Mouse sensitivity
| | Now | Proposed |
|---|---|---|
| Title | Mouse sensitivity | Same |
| Line under the title (Classic) | 5 is the default; below 0 for high-DPI mice (each step 20%… | Adjust how fast the mouse turns the camera |
| Tooltip (hover; Modern's (i)) | 5 is the default; below 0 for high-DPI mice (each step 20% slower) | Below 0 is for high-DPI mice; each step is 20% slower.<br>*[Default: 5]* |
| Default | 5 | Same |

#### Invert left / right (now: Invert horizontal)
| | Now | Proposed |
|---|---|---|
| Title | Invert horizontal | Invert left / right |
| Line under the title (Classic) | — | Swap the mouse's left and right |
| Tooltip (hover; Modern's (i)) | — | — |

#### Invert up / down (now: Invert vertical)
| | Now | Proposed |
|---|---|---|
| Title | Invert vertical | Invert up / down |
| Line under the title (Classic) | — | Swap the mouse's up and down |
| Tooltip (hover; Modern's (i)) | — | — |

#### Camera auto-rotation
| | Now | Proposed |
|---|---|---|
| Title | Camera auto-rotation | Same |
| Line under the title (Classic) | Let the camera turn by itself with movement, as with a gamepad | Let the camera turn by itself as you move |
| Tooltip (hover; Modern's (i)) | Let the camera turn by itself with movement, as with a gamepad | As it does with a controller.<br> |

#### Hide mouse cursor
| | Now | Proposed |
|---|---|---|
| Title | Hide mouse cursor | Same |
| Line under the title (Classic) | Hidden 2 seconds after the mouse stops moving… | Hide the cursor while the mouse is still |
| Tooltip (hover; Modern's (i)) | Hidden 2 seconds after the mouse stops moving; moving it brings it back | It hides after 2 seconds and comes back when you move the mouse.<br>*[Default: On]* |
| Default | On | Same |
| In-game menu | Hide mouse cursor | Marked with the in-game icon |


### Gamepad

#### Controller
| | Now | Proposed |
|---|---|---|
| Title | Controller | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | — |
| Note | | Shows the controller in use. The group "Gamepad" becomes "Controller" (one word for it everywhere). |

#### Swap sticks
| | Now | Proposed |
|---|---|---|
| Title | Swap sticks | Same |
| Line under the title (Classic) | Move with the right stick and look with the left | Move with the right stick and look with the left |
| Tooltip (hover; Modern's (i)) | Move with the right stick and look with the left | — |

#### Invert left / right (now: Invert camera left / right)
| | Now | Proposed |
|---|---|---|
| Title | Invert camera left / right | Invert left / right |
| Line under the title (Classic) | The camera stick's horizontal direction | Swap the camera stick's left and right |
| Tooltip (hover; Modern's (i)) | The camera stick's horizontal direction | — |

#### Invert up / down (now: Invert camera up / down)
| | Now | Proposed |
|---|---|---|
| Title | Invert camera up / down | Invert up / down |
| Line under the title (Classic) | The camera stick's vertical direction | Swap the camera stick's up and down |
| Tooltip (hover; Modern's (i)) | The camera stick's vertical direction | — |

#### Stick dead zone
| | Now | Proposed |
|---|---|---|
| Title | Stick dead zone | Same |
| Line under the title (Classic) | Ignores small stick movement, for sticks that drift. | Ignore small movements from drifting sticks |
| Tooltip (hover; Modern's (i)) | Ignores small stick movement, for sticks that drift. 0 = off (the game's own) | 0 is off, the game's own.<br>*[Default: 0]* |
| Default | 0 | Same |

#### Preferred controller (now: Preferred gamepad)
| | Now | Proposed |
|---|---|---|
| Title | Preferred gamepad | Preferred controller |
| Line under the title (Classic) | Part of a controller's name (DualSense, Xbox, 8BitDo...) or its… | Choose the controller to use when several are connected |
| Tooltip (hover; Modern's (i)) | Part of a controller's name (DualSense, Xbox, 8BitDo...) or its SDL GUID: that one is used when several are connected. Empty: the pad you use takes over<br>BB_GAMEPAD, default: not set. Applied when the game starts | Enter part of its name (DualSense, Xbox, 8BitDo…) or its SDL GUID. Leave it empty to use whichever you press.<br> |
| Default | not set | Same |


### Button icons

#### Button prompts
| | Now | Proposed |
|---|---|---|
| Title | Button prompts | Same |
| Line under the title (Classic) | The button icons in the game's menus and prompts. | Choose which button icons the game's prompts show |
| Tooltip (hover; Modern's (i)) | The button icons in the game's menus and prompts. Automatic shows your keys while the keyboard or mouse is in use, and the controller's buttons with a controller; also in-game under System › PC Controls | Automatic shows your keys while you use the keyboard or mouse, and the controller's buttons while you use a controller. Also in the game's System › PC Controls.<br>*[Default: Automatic (default)]* |
| Choices | Automatic (default)<br>Keyboard<br>Controller | Automatic (recommended)<br>Keyboard<br>Controller |
| Default | Automatic (default) | Same |

#### Controller icons
| | Now | Proposed |
|---|---|---|
| Title | Controller icons | Same |
| Line under the title (Classic) | The controller's buttons in the game's menus and prompts. | Choose PlayStation or Xbox button icons |
| Tooltip (hover; Modern's (i)) | The controller's buttons in the game's menus and prompts. Automatic shows PlayStation buttons with a PlayStation controller and Xbox buttons (A, B, X, Y, LB, RT…) with any other; rebound buttons show as bound. Also in-game under System › PC Controls | Automatic shows PlayStation buttons with a PlayStation controller and Xbox buttons with any other. Rebound buttons show as bound. Also in the game's System › PC Controls.<br>*[Default: Automatic (default)]* |
| Choices | Automatic (default)<br>PlayStation<br>Xbox | Automatic (recommended)<br>PlayStation<br>Xbox |
| Default | Automatic (default) | Same |


### Profiles

#### Profile
| | Now | Proposed |
|---|---|---|
| Title | Profile | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | — |

#### Save and share
| | Now | Proposed |
|---|---|---|
| Title | Save and share | Same |
| Line under the title (Classic) | Profiles are small JSON files. | Share a profile as a link or a file |
| Tooltip (hover; Modern's (i)) | Profiles are small JSON files. Copy one and paste it on any paste site (Pastebin, GitHub Gist, Rentry, hastebin…) to share it; others import it from the link. | Profiles are small JSON files. Paste one on any paste site (Pastebin, GitHub Gist, Rentry…) to share it; others import it from the link.<br> |


## Engine switches

#### Changed switches
| | Now | Proposed |
|---|---|---|
| Title | Changed switches | Same |
| Line under the title (Classic) | Every switch on this page that is not at its default | See and reset the switches you changed |
| Tooltip (hover; Modern's (i)) | Every switch on this page that is not at its default | — |

#### Extra variables
| | Now | Proposed |
|---|---|---|
| Title | Extra variables | Same |
| Line under the title (Classic) | NAME=value pairs separated by spaces, passed to the game. | Pass engine variables this page doesn't list |
| Tooltip (hover; Modern's (i)) | NAME=value pairs separated by spaces, passed to the game. For anything this page doesn't list; a variable set here wins over its switch | NAME=value pairs separated by spaces. A variable set here wins over its switch.<br> |


### Experimental engine features (folded)

#### Legacy shader compiler
| | Now | Proposed |
|---|---|---|
| Title | Legacy shader compiler | Same |
| Line under the title (Classic) | Off by default: the native compiler reads constant data through… | Compile shaders the old way if something draws wrong |
| Tooltip (hover; Modern's (i)) | Off by default: the native compiler reads constant data through the GPU's constant cache. On compiles the game's shaders the way 0.9.33 did: try it if something draws wrong. Switching recompiles every shader once | The native compiler reads constant data through the graphics card's constant cache. The legacy compiler works the way earlier versions did. Switching recompiles every shader once.<br>*[Default: Off]* |
| Default | Off | Same |

#### Video memory cleanup (now: VRAM cleanup)
| | Now | Proposed |
|---|---|---|
| Title | VRAM cleanup | Video memory cleanup |
| Line under the title (Classic) | Frees video memory held by textures the GPU drew, so long… | Free video memory held by old textures |
| Tooltip (hover; Modern's (i)) | Frees video memory held by textures the GPU drew, so long sessions don't fill up VRAM and slow down. Recommended: on | Frees textures the graphics card drew, so long sessions don't fill video memory and slow down.<br>*[Default: On (recommended)]* |
| Default | On | Same |

#### Precise frame pacing
| | Now | Proposed |
|---|---|---|
| Title | Precise frame pacing | Same |
| Line under the title (Classic) | A high-resolution timer and even spacing between frames… | Steady frame times and reduce VRR flicker |
| Tooltip (hover; Modern's (i)) | A high-resolution timer and even spacing between frames: steadier frame times and less VRR flicker. Uses a little CPU. On by default | Uses a high-resolution timer and spaces frames evenly. Uses a little CPU time.<br>*[Default: On (recommended)]* |
| Default | On | Same |

#### Fixed-refresh frame lock
| | Now | Proposed |
|---|---|---|
| Title | Fixed-refresh frame lock | Same |
| Line under the title (Classic) | Only for monitors without G-Sync or FreeSync (VRR). | Smooth pacing on displays without G-Sync or FreeSync |
| Tooltip (hover; Modern's (i)) | Only for monitors without G-Sync or FreeSync (VRR). Each frame is shown right after one of the monitor's refreshes when the FPS limit divides its refresh rate (60 FPS on 60, 120 or 240 Hz). Needs Precise frame pacing. With G-Sync or FreeSync it stutters: leave it off. Off by default | Shows each frame right after a display refresh when the frame rate limit divides the refresh rate (60 FPS on 60, 120 or 240 Hz). Needs Precise frame pacing. Leave it off with G-Sync or FreeSync, where it stutters.<br>*[Default: Off]* |
| Default | Off | Same |

#### Particle work safety cap
| | Now | Proposed |
|---|---|---|
| Title | Particle work safety cap | Same |
| Line under the title (Classic) | Checks the size of the game's GPU particle work before the… | Prevent "device lost" crashes in heavy effects |
| Tooltip (hover; Modern's (i)) | Checks the size of the game's GPU particle work before the graphics card runs it, and caps it to what the card supports. Only for games that crash with "device lost" in effect-heavy scenes. It costs smoothness in busy scenes. Off by default | Checks the size of the game's particle work before the graphics card runs it and caps it to what the card supports. Use it only if the game crashes with "device lost" in effect-heavy scenes; it costs smoothness there.<br>*[Default: Off]* |
| Default | Off | Same |

#### More video memory for textures (now: Use more VRAM for textures)
| | Now | Proposed |
|---|---|---|
| Title | Use more VRAM for textures | More video memory for textures |
| Line under the title (Classic) | Keeps textures in video memory up to 80% of the card instead of… | Keep more textures in video memory to reduce stutter |
| Tooltip (hover; Modern's (i)) | Keeps textures in video memory up to 80% of the card instead of about 40%, so they aren't thrown out and loaded again (the cause of most stutters on 8 GB cards) | Keeps textures in up to 80% of video memory instead of about 40%, so they are not thrown out and loaded again, the cause of most stutters on 8 GB cards.<br>*[Default: On (recommended)]* |
| Default | On | Same |

#### Graphics threads first
| | Now | Proposed |
|---|---|---|
| Title | Graphics threads first | Same |
| Line under the title (Classic) | Runs the engine's graphics threads at a higher CPU priority than… | Run the graphics threads at a higher priority |
| Tooltip (hover; Modern's (i)) | Runs the engine's graphics threads at a higher CPU priority than the game's other threads, so a frame isn't held up behind them. On by default | So a frame isn't held up behind the game's other threads.<br>*[Default: On (recommended)]* |
| Default | On | Same |

#### Shorter sleep spin
| | Now | Proposed |
|---|---|---|
| Title | Shorter sleep spin | Same |
| Line under the title (Classic) | The game's many short sleeps busy-wait only as long as the timer… | Give more CPU time to the graphics threads |
| Tooltip (hover; Modern's (i)) | The game's many short sleeps busy-wait only as long as the timer needs (about 0.2–0.4 ms instead of 0.6 ms), leaving that CPU time to the graphics threads. On by default | The game's short sleeps busy-wait only as long as the timer needs (about 0.2 to 0.4 ms instead of 0.6 ms).<br>*[Default: On (recommended)]* |
| Default | On | Same |

#### Background shader compiling
| | Now | Proposed |
|---|---|---|
| Title | Background shader compiling | Same |
| Line under the title (Classic) | New shaders compile on helper threads instead of freezing the… | Compile new shaders without freezing the game |
| Tooltip (hover; Modern's (i)) | New shaders compile on helper threads instead of freezing the frame: much shorter hitches when you reach a new area. On by default | New shaders compile on helper threads, so hitches in new areas are much shorter.<br>*[Default: On (recommended)]* |
| Default | On | Same |

#### Occlusion culling
| | Now | Proposed |
|---|---|---|
| Title | Occlusion culling | Same |
| Line under the title (Classic) | Off by default. The game skips objects hidden behind walls… | Skip objects hidden behind walls for more FPS |
| Tooltip (hover; Modern's (i)) | Off by default. The game skips objects hidden behind walls: higher FPS in dense areas, but objects and floors can appear late or flicker | Raises FPS in dense areas, but objects and floors can appear late or flicker.<br>*[Default: Off]* |
| Default | Off | Same |

#### Faster draw thread
| | Now | Proposed |
|---|---|---|
| Title | Faster draw thread | Same |
| Line under the title (Classic) | Remembers texture and sampler lookups and hands the end of each… | Raise FPS in busy areas |
| Tooltip (hover; Modern's (i)) | Remembers texture and sampler lookups and hands the end of each frame to the draw thread: higher FPS in busy areas. New: turn it off if the game hangs or crashes | Remembers texture and sampler lookups and hands the end of each frame to the draw thread. Turn it off if the game hangs or crashes.<br>*[Default: On (recommended)]* |
| Default | On | Same |

#### No shader freezes
| | Now | Proposed |
|---|---|---|
| Title | No shader freezes | Same |
| Line under the title (Classic) | While a new shader compiles, the objects that need it appear a… | Draw new objects late instead of freezing the game |
| Tooltip (hover; Modern's (i)) | While a new shader compiles, the objects that need it appear a moment later instead of the game freezing. New: turn it off if something stays missing | While a new shader compiles, the objects that need it appear a moment later. Turn it off if something stays missing.<br>*[Default: On (recommended)]* |
| Default | On | Same |

#### Fast loading
| | Now | Proposed |
|---|---|---|
| Title | Fast loading | Same |
| Line under the title (Classic) | Above 60 FPS, loading screens run at 60 so more of the CPU goes… | Speed up loading screens above 60 FPS |
| Tooltip (hover; Modern's (i)) | Above 60 FPS, loading screens run at 60 so more of the CPU goes to the loading itself. Does nothing at 30 or 60 FPS, or before a cutscene. On by default | Loading screens run at 60 FPS so more CPU time goes to loading. No effect at 30 or 60 FPS or before a cutscene.<br>*[Default: On (recommended)]* |
| Default | On | Same |

#### Frame rate corrections
| | Now | Proposed |
|---|---|---|
| Title | Frame rate corrections | Same |
| Line under the title (Classic) | At 60 FPS and above, ragdolls, physics, cloth, jumps, slopes… | Keep physics, cloth and timers right above 30 FPS |
| Tooltip (hover; Modern's (i)) | At 60 FPS and above, ragdolls, physics, cloth, jumps, slopes, sprinting, attack sounds, online movement and timers follow the real frame time, and the timings the FPS++ patches halved or doubled (menu repeat, hit lifetimes, online timers) are the game's own again. Off: the older FPS++ patches above 60. On by default | At 60 FPS and above, ragdolls, physics, cloth, jumps, slopes, sprinting, attack sounds, online movement and timers follow the real frame time, and the timings the FPS++ patches halved or doubled are the game's own again. Turn it off to use the community FPS++ patches instead.<br>*[Default: On (recommended)]* |
| Default | On | Same |

#### Unified frame time
| | Now | Proposed |
|---|---|---|
| Title | Unified frame time | Same |
| Line under the title (Classic) | Needs Frame rate corrections. | Recover at once after a stutter |
| Tooltip (hover; Modern's (i)) | Needs Frame rate corrections. The game and every correction use one frame time, the frame's own, in the same frame, instead of the corrections following a smoothed copy one frame late: after a stutter, ragdolls, physics, jumps, timers and the camera's easing catch up at once rather than over 10 to 15 frames. On by default | The game and every correction use one frame time, the frame's own, so after a stutter ragdolls, physics, jumps, timers and the camera catch up at once instead of over 10 to 15 frames. Needs Frame rate corrections.<br>*[Default: On (recommended)]* |
| Default | On | Same |

#### Fewer memory write checks
| | Now | Proposed |
|---|---|---|
| Title | Fewer memory write checks | Same |
| Line under the title (Classic) | Memory the game rewrites before almost every use is no longer… | Save up to 2 ms a frame in busy areas |
| Tooltip (hover; Modern's (i)) | Memory the game rewrites before almost every use is no longer watched for changes, saving up to about 2 ms a frame in busy areas. On by default | Memory the game rewrites before almost every use is no longer watched for changes.<br>*[Default: On (recommended)]* |
| Default | On | Same |

#### Per-frame GPU data in place
| | Now | Proposed |
|---|---|---|
| Title | Per-frame GPU data in place | Same |
| Line under the title (Classic) | The memory the game rewrites for the GPU every frame (288 MiB)… | Save CPU and graphics card time every frame |
| Tooltip (hover; Modern's (i)) | The memory the game rewrites for the GPU every frame (288 MiB) is read by the GPU where the game writes it, as a PC game's upload heap: no write faults or uploads for it, and less CPU and GPU time a frame. On by default. Off: watched and copied as before 0.4.4, should something look wrong | The graphics card reads the memory the game rewrites every frame (288 MiB) where the game writes it, with no copies. Turn it off if something looks wrong.<br>*[Default: On (recommended)]* |
| Default | On | Same |

#### Native draw calls
| | Now | Proposed |
|---|---|---|
| Title | Native draw calls | Same |
| Line under the title (Classic) | The game's draw calls reach the engine directly instead of as… | Pass the game's draw calls straight to the engine |
| Tooltip (hover; Modern's (i)) | The game's draw calls reach the engine directly instead of as console GPU commands it decodes again. On by default; turn it off if something stops drawing | They no longer go through console GPU commands the engine decodes again. Turn it off if something stops drawing.<br>*[Default: On (recommended)]* |
| Default | On | Same |

#### Intel crash fix: skip effect trails (optional) (now: Effect trail crash fix)
| | Now | Proposed |
|---|---|---|
| Title | Effect trail crash fix | Intel crash fix: skip effect trails (optional) |
| Line under the title (Classic) | For crashes while drawing effect trails on newer Intel CPUs… | Stop effect trail crashes on newer Intel CPUs |
| Tooltip (hover; Modern's (i)) | For crashes while drawing effect trails on newer Intel CPUs (Witch of Hemwick, for example): stops the crash, and that kind of trail is not drawn. Off by default | Stops crashes while drawing effect trails on newer Intel CPUs (Witch of Hemwick, for example). That kind of trail is not drawn.<br>*[Default: Off]* |
| Default | Off | Same |


### Performance

#### Graphics card
| | Now | Proposed |
|---|---|---|
| Title | Graphics card | Same |
| Line under the title (Classic) | For laptops and PCs with two graphics cards where the game picks… | Choose the graphics card on PCs with two |
| Tooltip (hover; Modern's (i)) | For laptops and PCs with two graphics cards where the game picks the wrong one. Auto takes the faster one<br>BB_GPU_ID, default: not set. Applied when the game starts | For laptops and PCs where the game picks the wrong one. Auto picks the faster one.<br> |
| Choices | Auto<br>First<br>Second | Auto (recommended)<br>First<br>Second |
| Default | not set | Same |

#### Two-stage GPU pipeline
| | Now | Proposed |
|---|---|---|
| Title | Two-stage GPU pipeline | Same |
| Line under the title (Classic) | 20–30% faster; turn off if the game is unstable | Choose whether GPU work runs on two threads |
| Tooltip (hover; Modern's (i)) | 20–30% faster; turn off if the game is unstable | 20 to 30% faster. Turn it off if the game is unstable.<br>*[Default: Auto (recommended)]* |
| Choices | Off · More stable, but slower<br>Auto · On with 8 or more CPU threads<br>On | Off · More stable, but slower<br>Auto (recommended) · On with 8 or more CPU threads<br>On |
| Default | Auto | Same |

#### GPU data sync (now: CPU reads of GPU data)
| | Now | Proposed |
|---|---|---|
| Title | CPU reads of GPU data | GPU data sync |
| Line under the title (Classic) | Relaxed is the default | Choose when GPU results reach the game |
| Tooltip (hover; Modern's (i)) | Relaxed is the default | Off: never. Relaxed: before the game changes that memory. Precise: before the game reads it too, which is slower.<br>*[Default: Relaxed (recommended)]* |
| Choices | Off<br>Relaxed · Default<br>Precise | Off<br>Relaxed (recommended)<br>Precise |
| Default | Relaxed | Same |

#### Shader compile threads
| | Now | Proposed |
|---|---|---|
| Title | Shader compile threads | Same |
| Line under the title (Classic) | How many threads compile new shaders in the background. | Choose how many threads compile new shaders |
| Tooltip (hover; Modern's (i)) | How many threads compile new shaders in the background. More finish a new area's shaders sooner but take CPU time from the game while they work<br>BB_PIPELINE_THREADS, default: not set. Applied when the game starts | More threads finish a new area's shaders sooner but take CPU time from the game.<br> |
| Choices | Auto · A third of the CPU threads, at most 4<br>1<br>2<br>3<br>4<br>6<br>8 | Auto (recommended) · A third of the CPU threads, at most 4<br>1<br>2<br>3<br>4<br>6<br>8 |
| Default | not set | Same |

#### Area prefetch (hard disks)
| | Now | Proposed |
|---|---|---|
| Title | Area prefetch (hard disks) | Same |
| Line under the title (Classic) | When the game starts loading a map area, a background thread… | Read each area's files ahead on a hard disk |
| Tooltip (hover; Modern's (i)) | When the game starts loading a map area, a background thread reads that area's model files into the Windows file cache in big sequential reads, so models and textures stop appearing late in front of you. Auto: on when the game is installed on a hard disk (an SSD does not need it)<br>BB_FILE_PREFETCH, default: not set. Applied when the game starts | When an area starts loading, a background thread reads its model files into the Windows file cache, so models and textures stop appearing late. An SSD doesn't need it.<br> |
| Choices | Auto<br>On<br>Off | Off<br>Auto (recommended) · On when the game is on a hard disk<br>On |
| Default | not set | Same |

#### Frame rate in the background
| | Now | Proposed |
|---|---|---|
| Title | Frame rate in the background | Same |
| Line under the title (Classic) | The frame-rate limit while the game window is in the background… | Choose the FPS limit while the game is in the background |
| Tooltip (hover; Modern's (i)) | The frame-rate limit while the game window is in the background or minimized, to save power and heat. Online play is never limited, so co-op and invasions keep their pace<br>BB_BACKGROUND_FPS, default: 30 FPS. Applied when the game starts | Saves power and heat while the window is in the background or minimized. Online play is never limited.<br>*[Default: 30 FPS (recommended)]* |
| Choices | 10 FPS<br>15 FPS<br>30 FPS<br>60 FPS<br>No limit | 10 FPS<br>15 FPS<br>30 FPS (recommended)<br>60 FPS<br>No limit |
| Default | 30 FPS | Same |


### Sound

#### Sound output
| | Now | Proposed |
|---|---|---|
| Title | Sound output | Same |
| Line under the title (Classic) | If the sound jitters or crackles, try DirectSound. | Choose how the game's sound reaches Windows |
| Tooltip (hover; Modern's (i)) | If the sound jitters or crackles, try DirectSound. The larger buffer is steadier but the sound comes a little later | If the sound jitters or crackles, try DirectSound. The larger buffer is steadier but the sound comes a little later.<br>*[Default: WASAPI (recommended)]* |
| Choices | WASAPI · Default<br>DirectSound<br>DirectSound, larger buffer | WASAPI (recommended)<br>DirectSound<br>DirectSound, larger buffer |
| Default | WASAPI | Same |

#### Sound threads first
| | Now | Proposed |
|---|---|---|
| Title | Sound threads first | Same |
| Line under the title (Classic) | For CPUs with many slower cores (older Xeons and similar)… | Run the game's sound before its other threads |
| Tooltip (hover; Modern's (i)) | For CPUs with many slower cores (older Xeons and similar): the game's sound runs before its other threads, against late or stuttering sound. Off by default | For CPUs with many slower cores, such as older Xeons, where sound comes late or stutters.<br>*[Default: Off]* |
| Default | Off | Same |

#### Run without sound
| | Now | Proposed |
|---|---|---|
| Title | Run without sound | Same |
| Line under the title (Classic) | Starts without any sound device (silent). | Start with no sound device |
| Tooltip (hover; Modern's (i)) | Starts without any sound device (silent). Only for finding out whether a sound driver is what keeps the game from starting or hangs it<br>BB_AUDIO, default: off. Applied when the game starts | Only to find out whether a sound driver keeps the game from starting or hangs it.<br>*[Default: off]* |
| Default | off | Same |


### Game fixes

#### No minimum loading time after death
| | Now | Proposed |
|---|---|---|
| Title | No minimum loading time after death | Same |
| Line under the title (Classic) | Skips the game's 12-second minimum loading screen after dying. | Skip the 12-second loading screen after dying |
| Tooltip (hover; Modern's (i)) | Skips the game's 12-second minimum loading screen after dying. Off: the original delay<br>BB_RESPAWN_DELAY_FIX, default: on. Applied when the game starts | Turn it off for the original delay.<br>*[Default: on (recommended)]* |
| Default | on | Same |

#### Weapon sound save fix
| | Now | Proposed |
|---|---|---|
| Title | Weapon sound save fix | Same |
| Line under the title (Classic) | Sets the flag in the game's save that some weapon and player… | Fix weapon sounds that never play |
| Tooltip (hover; Modern's (i)) | Sets the flag in the game's save that some weapon and player sounds need (without it they never play), before the game reads the save. Off leaves saves untouched<br>BB_SOUND_HACK, default: on. Applied when the game starts | Sets the flag in your save that some weapon and player sounds need, before the game reads the save. Turn it off to leave saves untouched.<br>*[Default: on (recommended)]* |
| Default | on | Same |

#### OptiScaler compatibility
| | Now | Proposed |
|---|---|---|
| Title | OptiScaler compatibility | Same |
| Line under the title (Classic) | Only when OptiScaler is installed… | Keep OptiScaler from crashing the game |
| Tooltip (hover; Modern's (i)) | Only when OptiScaler is installed: DLSS calls wait for the GPU queue, and one thread sends the GPU work while its frame generation is on, so OptiScaler's hooks don't crash. Costs a little FPS with its frame generation. Off: the engine runs as without OptiScaler<br>BB_OPTISCALER_COMPAT, default: on. Applied when the game starts | Only matters when OptiScaler is installed: DLSS calls wait for the GPU queue, and one thread sends GPU work while its frame generation is on. Costs a little FPS with its frame generation.<br>*[Default: on]* |
| Default | on | Same |

#### Ebrietas effect fix
| | Now | Proposed |
|---|---|---|
| Title | Ebrietas effect fix | Same |
| Line under the title (Classic) | Replaces Ebrietas' trail effect with the one Nexus mod 41 uses… | Stop Ebrietas' rocks from piling up |
| Tooltip (hover; Modern's (i)) | Replaces Ebrietas' trail effect with the one Nexus mod 41 uses, made from your own game files: her rock attacks no longer pile up thousands of rocks (FPS falling from 90 to 40, crashes before beta 8). Off: the game's own effect. A mod that changes these effect files wins<br>BB_EFFECT_FIXES, default: on. Applied when the game starts | Uses the trail effect from Nexus mod 41, built from your own game files, so her rock attacks no longer pile up thousands of rocks and drop FPS. A mod that changes these effect files wins.<br>*[Default: on (recommended)]* |
| Default | on | Same |

#### PS4 floating-point mode
| | Now | Proposed |
|---|---|---|
| Title | PS4 floating-point mode | Same |
| Line under the title (Classic) | The game's threads handle very small numbers as a PS4 does… | Run physics and cloth as on the PS4 |
| Tooltip (hover; Modern's (i)) | The game's threads handle very small numbers as a PS4 does, rounding them to zero, instead of as Windows does. Physics and cloth then behave as on the console: with Windows' way, some of Cainhurst's ghosts lose the lower half of their dresses. Off: Windows' way, as before 0.4.2<br>BB_PS4_FPU, default: on. Applied when the game starts | The game's threads round very small numbers to zero, as the PS4 does. With Windows' handling, some of Cainhurst's ghosts lose the lower half of their dresses.<br>*[Default: on (recommended)]* |
| Default | on | Same |

#### Intel crash fix: keep effect trails (test) (optional) (now: Intel effect crash fix (test))
| | Now | Proposed |
|---|---|---|
| Title | Intel effect crash fix (test) | Intel crash fix: keep effect trails (test) (optional) |
| Line under the title (Classic) | For crashes in effects on Intel 12th-14th gen CPUs (Ebrietas… | Stop Intel effect crashes and keep effects drawn |
| Tooltip (hover; Modern's (i)) | For crashes in effects on Intel 12th-14th gen CPUs (Ebrietas, Witch of Hemwick, One Reborn, the Cainhurst cutscene): the two effect ribbon writers keep their data where Windows can't overwrite it. Every effect stays drawn. Turn off the Effect trail crash fix with it, and post your log either way<br>BB_RED_ZONE_FUNCTIONS, default: off. Applied when the game starts | For effect crashes on Intel 12th to 14th gen CPUs (Ebrietas, Witch of Hemwick, One Reborn, the Cainhurst cutscene). Turn off the Effect trail crash fix with it, and post your log either way.<br>*[Default: off]* |
| Default | off | Same |
| Note | | The two Intel fixes fight the same crashes from two groups; put them next to each other. |

#### Intel black tonemap fix
| | Now | Proposed |
|---|---|---|
| Title | Intel black tonemap fix | Same |
| Line under the title (Classic) | Adds the Intel Black Tonemap Fix patch… | Fix a black or very dark picture on Intel CPUs |
| Tooltip (hover; Modern's (i)) | Adds the Intel Black Tonemap Fix patch: for Intel CPUs where the picture turns black or far too dark (the DLC areas most)<br>BB_INTEL_TONEMAP_FIX, default: off. Applied when the game starts | Adds the Intel Black Tonemap Fix patch. The DLC areas are affected most.<br>*[Default: off]* |
| Default | off | Same |

#### Allow other game versions (now: Skip the game files check)
| | Now | Proposed |
|---|---|---|
| Title | Skip the game files check | Allow other game versions |
| Line under the title (Classic) | Starts with game files that are not the supported version (base… | Start game versions other than 1.09 |
| Tooltip (hover; Modern's (i)) | Starts with game files that are not the supported version (base game plus the 1.09 update). Other editions may crash, and the engine's patches and PC menus may not apply<br>BB_SKIP_GAME_CHECK, default: off. Applied when the game starts | Only version 1.09 (the base game plus the 1.09 update) is supported. Others may crash, and the engine's patches and PC menus may not apply.<br>*[Default: off]* |
| Default | off | Same |


## Log

#### Show this page when the game starts
| | Now | Proposed |
|---|---|---|
| Title | Show this page when the game starts | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | *[Default: On]* |
| Default | On | Same |

#### Log level
| | Now | Proposed |
|---|---|---|
| Title | Log level | Same |
| Line under the title (Classic) | How much the game writes into its log… | Choose how much the game writes in its log |
| Tooltip (hover; Modern's (i)) | How much the game writes into its log: Standard for everyday play, Detailed when asked for a log of something odd, Debug only to chase a specific bug | *[Default: Standard (recommended)]* |
| Choices | Standard · What a problem report needs: the settings at the start, loading times, a summary every minute, warnings and errors<br>Detailed · Also every save, thread and semaphore the game makes, the draw counts every 5 seconds and the GPU side's notes: a larger log, for something odd<br>Debug · Warning: everything the GPU side can say. The log grows by many megabytes and the game runs slower. Only to chase a specific bug, then set it back | Standard (recommended) · keep today's note<br>Detailed · keep today's note<br>Debug · keep today's note |
| Default | Standard | Same |


### Diagnostics (folded)

#### Frame statistics in the log
| | Now | Proposed |
|---|---|---|
| Title | Frame statistics in the log | Same |
| Line under the title (Classic) | BB_FRAME_STATS | Log frame times and engine counters every 5 seconds |
| Tooltip (hover; Modern's (i)) | BB_FRAME_STATS | On in betas, so crash logs include them. (BB_DIAG frame)<br>*[Default: On in betas, off in stable releases]* |
| Default | On in betas, off in stable releases | Same |

#### Hitch reports
| | Now | Proposed |
|---|---|---|
| Title | Hitch reports | Same |
| Line under the title (Classic) | A frame slower than this puts every thread's wait and stack in… | Log what held up slow frames |
| Tooltip (hover; Modern's (i)) | A frame slower than this puts every thread's wait and stack in the log, for stutter reports. Pick about twice your frame time: 50 ms at 30 FPS, 40 at 60, 25 at 90, 20 at 120, 16 at 144 or uncapped. Each report pauses the game about 0.2 s; at most 12 a session, none in the first 20 s or just after a loading screen | Every frame slower than this puts each thread's wait and stack in the log, for stutter reports. Pick about twice your frame time: 50 ms at 30 FPS, 40 at 60, 25 at 90, 20 at 120 and 16 at 144 or above. Each report pauses the game for about 0.2 s, at most 12 times a session. (BB_DIAG hitch)<br>*[Default: Off (recommended)]* |
| Choices | Off<br>16 ms · For 144 FPS or uncapped<br>20 ms · For 120 FPS<br>25 ms · For 90 FPS<br>30 ms · 60 FPS, every missed refresh: many reports<br>40 ms · For 60 FPS<br>50 ms · For 30 FPS<br>100 ms · Long stalls only, at any frame rate | Off (recommended)<br>16 ms · For 144 FPS or uncapped<br>20 ms · For 120 FPS<br>25 ms · For 90 FPS<br>30 ms · Every missed refresh at 60 FPS: many reports<br>40 ms · For 60 FPS<br>50 ms · For 30 FPS<br>100 ms · Long stalls only, at any frame rate |
| Default | Off | Same |

#### GPU time in the log
| | Now | Proposed |
|---|---|---|
| Title | GPU time in the log | Same |
| Line under the title (Classic) | BB_GPU_PROFILE: the GPU's time a frame by render pass, dispatch… | Log GPU time per frame by pass every 5 seconds |
| Tooltip (hover; Modern's (i)) | BB_GPU_PROFILE: the GPU's time a frame by render pass, dispatch and upscaler, every 5 seconds. Costs a little GPU time | Costs a little GPU time. (BB_DIAG gpu)<br>*[Default: Off]* |
| Default | Off | Same |

#### Render stage timings in the log
| | Now | Proposed |
|---|---|---|
| Title | Render stage timings in the log | Same |
| Line under the title (Classic) | BB_SECTIONS: where each frame's CPU time goes (with frame… | Log where each frame's CPU time goes |
| Tooltip (hover; Modern's (i)) | BB_SECTIONS: where each frame's CPU time goes (with frame statistics). Costs a little speed: a few tenths of a millisecond a frame on the draw recording thread | Needs Frame statistics. Costs a few tenths of a millisecond a frame. (BB_DIAG sections)<br>*[Default: Off]* |
| Default | Off | Same |

#### Loading profile in the log
| | Now | Proposed |
|---|---|---|
| Title | Loading profile in the log | Same |
| Line under the title (Classic) | BB_SAMPLE_THREADS: where the game's main, render and file… | Log where loading time goes every 5 seconds |
| Tooltip (hover; Modern's (i)) | BB_SAMPLE_THREADS: where the game's main, render and file threads spend their time, every 5 seconds. For slow loading; costs a little speed | Samples the game's main, render and file threads. For slow loading; costs a little speed. (BB_DIAG load)<br>*[Default: Off]* |
| Default | Off | Same |

#### Audio statistics in the log
| | Now | Proposed |
|---|---|---|
| Title | Audio statistics in the log | Same |
| Line under the title (Classic) | BB_AUDIO_STATS | Log sound buffers and dropouts every 5 seconds |
| Tooltip (hover; Modern's (i)) | BB_AUDIO_STATS | For crackling or late sound. (BB_DIAG audio)<br> |

#### Controller details in the log
| | Now | Proposed |
|---|---|---|
| Title | Controller details in the log | Same |
| Line under the title (Classic) | BB_PAD_LOG: each controller's USB ids, connection and rumble… | Log controller connections, IDs and rumble |
| Tooltip (hover; Modern's (i)) | BB_PAD_LOG: each controller's USB ids, connection and rumble support, disconnects, the game's first vibration request and sticks past their round gate. For controller problems | Each controller's USB IDs, connection and rumble support, disconnects, the game's first vibration request and sticks past their round gate. For controller problems. (BB_DIAG pad)<br>*[Default: Off]* |
| Default | Off | Same |

#### Heap corruption checks
| | Now | Proposed |
|---|---|---|
| Title | Heap corruption checks | Same |
| Line under the title (Classic) | BB_WRITE_LOG: the GPU side's last writes into the game's memory… | Log the GPU's last writes into game memory |
| Tooltip (hover; Modern's (i)) | BB_WRITE_LOG: the GPU side's last writes into the game's memory, put in the log if the game crashes on damaged memory. For crash reports; changes the timing of those writes a little | For crash reports: if the game crashes on damaged memory, the log shows what wrote there last. Changes the timing of those writes a little. (BB_DIAG heap)<br>*[Default: Off]* |
| Default | Off | Same |

#### Capture shaders
| | Now | Proposed |
|---|---|---|
| Title | Capture shaders | Same |
| Line under the title (Classic) | BB_SHADER_CAPTURE: every shader the game uses is saved once to… | Save every shader the game uses to logs\shaders |
| Tooltip (hover; Modern's (i)) | BB_SHADER_CAPTURE: every shader the game uses is saved once to logs\shaders (a few MB), for testing the new native shader compiler offline. Costs a little time the first time each shader is seen | A few MB, for testing the native shader compiler offline. Costs a little time the first time each shader is seen. (BB_DIAG shaders)<br>*[Default: Off]* |
| Default | Off | Same |
| Note | | Bug today: this text is not translated (no L.T). |


## Settings


### Appearance

#### Launcher language
| | Now | Proposed |
|---|---|---|
| Title | Launcher language | Same |
| Line under the title (Classic) | Restart the launcher to apply the language. | Choose the launcher's language |
| Tooltip (hover; Modern's (i)) | Restart the launcher to apply the language. | Restart the launcher to apply it.<br>*[Default: en]* |
| Choices | l.Name | Same |
| Default | en | Same |

#### Layout
| | Now | Proposed |
|---|---|---|
| Title | Layout | Same |
| Line under the title (Classic) | Classic: the sidebar and one card per setting. | Choose Classic cards or compact Modern rows |
| Tooltip (hover; Modern's (i)) | Classic: the sidebar and one card per setting. Modern: tabs and compact rows | *[Default: Classic (recommended)]* |
| Choices | Classic<br>Modern | Classic (recommended)<br>Modern |
| Default | Classic | Same |

#### Navigation
| | Now | Proposed |
|---|---|---|
| Title | Navigation | Same |
| Line under the title (Classic) | Tabs across the top, or a sidebar with the pages in groups | Choose tabs across the top or a sidebar |
| Tooltip (hover; Modern's (i)) | Tabs across the top, or a sidebar with the pages in groups | *[Default: Tabs]* |
| Choices | Tabs<br>Sidebar | Same |
| Default | Tabs | Same |

#### Theme
| | Now | Proposed |
|---|---|---|
| Title | Theme | Same |
| Line under the title (Classic) | The accent colour, and a faint tint of it in the launcher's… | Choose the accent colour |
| Tooltip (hover; Modern's (i)) | The accent colour, and a faint tint of it in the launcher's surfaces | It also tints the launcher's surfaces slightly.<br>*[Default: default]* |
| Default | default | Same |

#### Theme colour
| | Now | Proposed |
|---|---|---|
| Title | Theme colour | Same |
| Line under the title (Classic) | Pick a dot or type a colour as #RRGGBB | Pick a colour or enter one as #RRGGBB |
| Tooltip (hover; Modern's (i)) | Pick a dot or type a colour as #RRGGBB | — |

#### Background
| | Now | Proposed |
|---|---|---|
| Title | Background | Same |
| Line under the title (Classic) | The picture behind the glass panes | Choose the picture behind the glass panes |
| Tooltip (hover; Modern's (i)) | The picture behind the glass panes | *[Default: Game artwork (recommended)]* |
| Choices | Game artwork<br>Your picture<br>None | None<br>Game artwork (recommended)<br>Your picture |
| Default | Game artwork | Same |

#### Picture
| | Now | Proposed |
|---|---|---|
| Title | Picture | Same |
| Line under the title (Classic) | A PNG, JPG or BMP; it fills the window | Choose a PNG, JPG or BMP file |
| Tooltip (hover; Modern's (i)) | A PNG, JPG or BMP; it fills the window | It fills the window.<br> |

#### Background blur
| | Now | Proposed |
|---|---|---|
| Title | Background blur | Same |
| Line under the title (Classic) | — | Choose how much the background is blurred |
| Tooltip (hover; Modern's (i)) | — | *[Default: Strong (recommended)]* |
| Choices | Strong<br>Light<br>Off | Off<br>Light<br>Strong (recommended) |
| Default | Strong | Same |

#### App icon
| | Now | Proposed |
|---|---|---|
| Title | App icon | Same |
| Line under the title (Classic) | The launcher's and the game window's icon. | Choose the launcher's and the game window's icon |
| Tooltip (hover; Modern's (i)) | The launcher's and the game window's icon. The game window takes it at its next start | The game window takes it at its next start.<br>*[Default: auto]* |
| Default | auto | Same |

#### Icon picture
| | Now | Proposed |
|---|---|---|
| Title | Icon picture | Same |
| Line under the title (Classic) | A PNG, JPG, BMP or ICO; square pictures look best | Choose a PNG, JPG, BMP or ICO file |
| Tooltip (hover; Modern's (i)) | A PNG, JPG, BMP or ICO; square pictures look best | Square pictures look best.<br> |

#### Window buttons
| | Now | Proposed |
|---|---|---|
| Title | Window buttons | Same |
| Line under the title (Classic) | Windows: – □ ✕. macOS: the red, yellow and green dots | Choose Windows or macOS window buttons |
| Tooltip (hover; Modern's (i)) | Windows: – □ ✕. macOS: the red, yellow and green dots | Windows: – □ ✕. macOS: the red, yellow and green dots.<br>*[Default: Windows]* |
| Choices | Windows<br>macOS | Same |
| Default | Windows | Same |

#### Window buttons side
| | Now | Proposed |
|---|---|---|
| Title | Window buttons side | Same |
| Line under the title (Classic) | — | Choose which side the window buttons are on |
| Tooltip (hover; Modern's (i)) | — | *[Default: Usual]* |
| Choices | Usual · macOS on the left, Windows on the right<br>Left<br>Right | Same |
| Default | Usual | Same |


### Engine and launch

#### Engine folder
| | Now | Proposed |
|---|---|---|
| Title | Engine folder | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | The Hunter's Workshop folder, with engine\out\bb-start.exe.<br> |

#### Start the game with
| | Now | Proposed |
|---|---|---|
| Title | Start the game with | Same |
| Line under the title (Classic) | — | Choose how the launcher starts the game |
| Tooltip (hover; Modern's (i)) | — | *[Default: Hunter's Workshop (recommended)]* |
| Choices | Hunter's Workshop · Prepares mods and patches, then starts the engine (out\bb-probe.exe)<br>Custom command · Runs a program of your choice with the settings as environment variables | Hunter's Workshop (recommended) · keep today's note<br>Custom command · keep today's note |
| Default | Hunter's Workshop | Same |

#### Command
| | Now | Proposed |
|---|---|---|
| Title | Command | Same |
| Line under the title (Classic) | A program to run instead of Bloodborne.exe --run, with the… | A program to run instead of Bloodborne.exe --run, with the… |
| Tooltip (hover; Modern's (i)) | A program to run instead of Bloodborne.exe --run, with the settings as environment variables | — |

#### Arguments
| | Now | Proposed |
|---|---|---|
| Title | Arguments | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | — |


### Files

#### Launcher settings
| | Now | Proposed |
|---|---|---|
| Title | Launcher settings | Same |
| Line under the title (Classic) | ‹Paths.ConfigFile› | — |
| Tooltip (hover; Modern's (i)) | ‹Paths.ConfigFile› | — |
| Note | | Shows the file. |

#### Game settings (config.ini)
| | Now | Proposed |
|---|---|---|
| Title | Game settings (config.ini) | Same |
| Line under the title (Classic) | ‹Paths.IniPath› | — |
| Tooltip (hover; Modern's (i)) | ‹Paths.IniPath› | — |
| Note | | Shows the file. |

#### Shader cache
| | Now | Proposed |
|---|---|---|
| Title | Shader cache | Same |
| Line under the title (Classic) | The game's compiled shaders, kept between starts (a new engine… | Store compiled shaders between game starts |
| Tooltip (hover; Modern's (i)) | The game's compiled shaders, kept between starts (a new engine version clears them itself). Clear them if something draws wrong in every save, such as missing skin or hair; the next start compiles them again and stutters for a while | A new engine version clears them itself. Clear them if something looks wrong in every save, such as missing skin or hair; the next start compiles them again and stutters for a while.<br> |

#### Shader catalog
| | Now | Proposed |
|---|---|---|
| Title | Shader catalog | Same |
| Line under the title (Classic) | Save your shaders as a catalog file, or add someone else's… | Share shaders so new areas don't stutter |
| Tooltip (hover; Modern's (i)) | Save your shaders as a catalog file, or add someone else's: areas they have played don't pop in or stutter for you. It works with the same kind of graphics card; a catalog adds to your shaders, it never replaces them | Save your shaders as a catalog file, or add someone else's. It works with the same kind of graphics card, and a catalog only adds to your shaders.<br> |


### Share settings

#### Share your settings
| | Now | Proposed |
|---|---|---|
| Title | Share your settings | Same |
| Line under the title (Classic) | Graphics, effects, camera, controls, cheats and engine options… | Share your settings as a code or a file |
| Tooltip (hover; Modern's (i)) | Graphics, effects, camera, controls, cheats and engine options, as a code to paste in Discord or a file. Not included: display and HDR, the online account, folders and languages | Includes graphics, effects, camera, controls, cheats and engine options. Leaves out display and HDR, the online account, folders and languages.<br> |

#### Import settings
| | Now | Proposed |
|---|---|---|
| Title | Import settings | Same |
| Line under the title (Classic) | Someone's code or file. | Import someone's settings code or file |
| Tooltip (hover; Modern's (i)) | Someone's code or file. You see what changes before anything is applied | You see what changes before anything is applied.<br> |


### Updates

#### Launcher updates
| | Now | Proposed |
|---|---|---|
| Title | Launcher updates | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | — |

#### Update channel
| | Now | Proposed |
|---|---|---|
| Title | Update channel | Same |
| Line under the title (Classic) | Stable: releases that have been tested. | Choose stable or beta releases |
| Tooltip (hover; Modern's (i)) | Stable: releases that have been tested. Beta: every change as soon as it is done, before it is tested | — |
| Choices | Stable<br>Beta | Stable (recommended) · Releases that have been tested<br>Beta · Every change as soon as it is done, before testing |

#### Check for updates at start
| | Now | Proposed |
|---|---|---|
| Title | Check for updates at start | Same |
| Line under the title (Classic) | Looks at github.com/{Updater.Repository}/releases | Check for new releases when the launcher starts |
| Tooltip (hover; Modern's (i)) | Looks at github.com/{Updater.Repository}/releases | Looks at github.com/{repository}/releases.<br>*[Default: On]* |
| Default | On | Same |

#### Version
| | Now | Proposed |
|---|---|---|
| Title | Version | Same |
| Line under the title (Classic) | Every published release, stable and beta, newest first. | Install any release, including older ones |
| Tooltip (hover; Modern's (i)) | Every published release, stable and beta, newest first. Install any of them, older ones too, to go back if a version gives you trouble. Your settings, saves, mods and profiles stay | Every published release, stable and beta, newest first. Go back to an older one if a version gives you trouble; your settings, saves, mods and profiles stay.<br> |

#### Build
| | Now | Proposed |
|---|---|---|
| Title | Build | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | — |


## Online


### The Hunter's Dream

#### Play online
| | Now | Proposed |
|---|---|---|
| Title | Play online | Same |
| Line under the title (Classic) | Off: the game stays offline | Connect to The Hunter's Dream for co-op and messages |
| Tooltip (hover; Modern's (i)) | Off: the game stays offline | Turn it off to keep the game offline.<br> |

#### Hunter's Dream account
| | Now | Proposed |
|---|---|---|
| Title | Hunter's Dream account | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | — |

#### Website
| | Now | Proposed |
|---|---|---|
| Title | Website | Same |
| Line under the title (Classic) | Players online, the map and your hunter's profile | See players online, the map and your hunter's profile |
| Tooltip (hover; Modern's (i)) | Players online, the map and your hunter's profile | — |


### Connection

#### Local P2P port
| | Now | Proposed |
|---|---|---|
| Title | Local P2P port | Same |
| Line under the title (Classic) | Empty: 3658, the PS4's | Choose the port co-op connects through |
| Tooltip (hover; Modern's (i)) | Empty: 3658, the PS4's | Leave it empty to use 3658, the PS4's.<br> |

#### Summon sign timeout
| | Now | Proposed |
|---|---|---|
| Title | Summon sign timeout | Same |
| Line under the title (Classic) | How long a sign you touched waits for its host before giving up | Choose how long a touched sign waits for its host |
| Tooltip (hover; Modern's (i)) | How long a sign you touched waits for its host before giving up | *[Default: 30 seconds (default)]* |
| Choices | 30 seconds (default)<br>60 seconds<br>The game's own (180 seconds) | 30 seconds (recommended)<br>60 seconds<br>The game's own (180 seconds) |
| Default | 30 seconds (default) | Same |

#### Test the connection
| | Now | Proposed |
|---|---|---|
| Title | Test the connection | Same |
| Line under the title (Classic) | Checks The Hunter's Dream… | Check the game server, co-op and the summon relay |
| Tooltip (hover; Modern's (i)) | Checks The Hunter's Dream: game server, co-op sessions and the relay summons use | — |


## Mods


### Mods

#### Load mods
| | Now | Proposed |
|---|---|---|
| Title | Load mods | Same |
| Line under the title (Classic) | When two mods replace the same file, the one lower in the list… | Load the mods in the list below |
| Tooltip (hover; Modern's (i)) | When two mods replace the same file, the one lower in the list wins | When two mods change the same file, the lower one in the list wins.<br>*[Default: On]* |
| Default | On | Same |

#### Mods folder
| | Now | Proposed |
|---|---|---|
| Title | Mods folder | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | — |


### Third-party patches

#### Patches folder
| | Now | Proposed |
|---|---|---|
| Title | Patches folder | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | — |


## Cheats


### Items

#### Blood Vials
| | Now | Proposed |
|---|---|---|
| Title | Blood Vials | Same |
| Line under the title (Classic) | — | Keep a set amount or never run out |
| Tooltip (hover; Modern's (i)) | — | — |
| Choices | Modes | Off<br>Infinite · keep today's note<br>Custom amount · keep today's note |
| In-game menu | Blood Vials | Marked with the in-game icon |

#### Quicksilver Bullets
| | Now | Proposed |
|---|---|---|
| Title | Quicksilver Bullets | Same |
| Line under the title (Classic) | — | Keep a set amount or never run out |
| Tooltip (hover; Modern's (i)) | — | — |
| In-game menu | Quicksilver Bullets | Marked with the in-game icon |

#### Infinite consumables
| | Now | Proposed |
|---|---|---|
| Title | Infinite consumables | Same |
| Line under the title (Classic) | Molotovs, throwing knives, pebbles, antidotes, papers and the… | Never use up Molotovs, knives, pebbles and the like |
| Tooltip (hover; Modern's (i)) | Molotovs, throwing knives, pebbles, antidotes, papers and the like are not used up (not Blood Vials or Bullets, which are above, and not insight items) | Not Blood Vials or Quicksilver Bullets, which have their own settings above, and not insight items.<br> |
| In-game menu | Infinite consumables | Marked with the in-game icon |


### Character

#### Infinite stamina
| | Now | Proposed |
|---|---|---|
| Title | Infinite stamina | Same |
| Line under the title (Classic) | Your stamina stays full | Keep your stamina full |
| Tooltip (hover; Modern's (i)) | Your stamina stays full | — |
| In-game menu | Infinite stamina | Marked with the in-game icon |

#### Stealth
| | Now | Proposed |
|---|---|---|
| Title | Stealth | Same |
| Line under the title (Classic) | Enemies don't notice you and your steps make no noise | Keep enemies from noticing you |
| Tooltip (hover; Modern's (i)) | Enemies don't notice you and your steps make no noise | Your steps make no noise either.<br> |
| In-game menu | Stealth | Marked with the in-game icon |

#### No weapon wear
| | Now | Proposed |
|---|---|---|
| Title | No weapon wear | Same |
| Line under the title (Classic) | Equipped weapons are kept at full durability, so no repairs | Keep equipped weapons at full durability |
| Tooltip (hover; Modern's (i)) | Equipped weapons are kept at full durability, so no repairs | No repairs needed.<br> |
| In-game menu | No weapon wear | Marked with the in-game icon |

#### Firearm range
| | Now | Proposed |
|---|---|---|
| Title | Firearm range | Same |
| Line under the title (Classic) | How far firearm bullets travel (pistols, rifles, blunderbuss… | Choose how far bullets travel |
| Tooltip (hover; Modern's (i)) | How far firearm bullets travel (pistols, rifles, blunderbuss, cannons) | Pistols, rifles, blunderbusses and cannons.<br>*[Default: Normal]* |
| Choices | Normal<br>150%<br>200%<br>300% | Same |
| Default | Normal | Same |
| In-game menu | Firearm range | Marked with the in-game icon |


### Blood Echoes

#### Keep Blood Echoes on death
| | Now | Proposed |
|---|---|---|
| Title | Keep Blood Echoes on death | Same |
| Line under the title (Classic) | Dying never drops your Blood Echoes, so there is nothing to pick… | Keep your Blood Echoes when you die |
| Tooltip (hover; Modern's (i)) | Dying never drops your Blood Echoes, so there is nothing to pick back up | — |
| In-game menu | Keep Blood Echoes on death | Marked with the in-game icon |

#### Blood Echo multiplier
| | Now | Proposed |
|---|---|---|
| Title | Blood Echo multiplier | Same |
| Line under the title (Classic) | Enemies already loaded give the new amount after you rest at a… | Choose how many Blood Echoes each kill gives |
| Tooltip (hover; Modern's (i)) | Enemies already loaded give the new amount after you rest at a lamp or reload | Enemies already loaded give the new amount after you rest at a lamp or reload.<br>*[Default: Normal]* |
| Choices | Normal<br>x2<br>x5<br>x10 | Same |
| Default | Normal | Same |
| In-game menu | Blood Echo multiplier | Marked with the in-game icon |

#### Add echoes or insight
| | Now | Proposed |
|---|---|---|
| Title | Add echoes or insight | Same |
| Line under the title (Classic) | In the in-game menu (Insert or L3 + R3)… | Add echoes or insight from the in-game menu |
| Tooltip (hover; Modern's (i)) | In the in-game menu (Insert or L3 + R3): +1,000, +10,000, +100,000 echoes and +1 insight | +1,000, +10,000 or +100,000 echoes, and +1 insight.<br> |

#### Amount
| | Now | Proposed |
|---|---|---|
| Title | Amount | Same |
| Line under the title (Classic) | How many {title} to keep | Set how many to keep |
| Tooltip (hover; Modern's (i)) | How many {title} to keep | — |


## Trophies


### Notification

#### Show in game
| | Now | Proposed |
|---|---|---|
| Title | Show in game | Same |
| Line under the title (Classic) | A small banner in a corner of the game's screen, without taking… | Show a banner when you earn a trophy |
| Tooltip (hover; Modern's (i)) | A small banner in a corner of the game's screen, without taking focus. In fullscreen it needs Display › Window mode › Borderless fullscreen | It appears in a corner without taking focus. In fullscreen it needs Display › Window mode › Borderless fullscreen.<br>*[Default: On]* |
| Default | On | Same |

#### Position
| | Now | Proposed |
|---|---|---|
| Title | Position | Same |
| Line under the title (Classic) | — | Choose the banner's corner |
| Tooltip (hover; Modern's (i)) | — | *[Default: Top right]* |
| Choices | Top right<br>Top left<br>Bottom right<br>Bottom left | Same |
| Default | Top right | Same |

#### Play a sound
| | Now | Proposed |
|---|---|---|
| Title | Play a sound | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | *[Default: On]* |
| Default | On | Same |

#### Volume
| | Now | Proposed |
|---|---|---|
| Title | Volume | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | *[Default: 70]* |
| Default | 70 | Same |

#### Sound file
| | Now | Proposed |
|---|---|---|
| Title | Sound file | Same |
| Line under the title (Classic) | A sound of your own (WAV, MP3, MP4/M4A)… | Choose your own sound |
| Tooltip (hover; Modern's (i)) | A sound of your own (WAV, MP3, MP4/M4A); empty uses the built-in chime | WAV, MP3, MP4 or M4A. Leave it empty for the built-in chime.<br> |

#### Try it
| | Now | Proposed |
|---|---|---|
| Title | Try it | Same |
| Line under the title (Classic) | Shows a sample notification and plays the sound | Show a sample banner and play the sound |
| Tooltip (hover; Modern's (i)) | Shows a sample notification and plays the sound | — |


### Trophy data

#### Trophy log
| | Now | Proposed |
|---|---|---|
| Title | Trophy log | Same |
| Line under the title (Classic) | — | — |
| Tooltip (hover; Modern's (i)) | — | — |

#### Reset trophy progress
| | Now | Proposed |
|---|---|---|
| Title | Reset trophy progress | Same |
| Line under the title (Classic) | Starts trophies and completion over, for a fresh run. | Start trophies and completion over |
| Tooltip (hover; Modern's (i)) | Starts trophies and completion over, for a fresh run. The old log is kept as a backup | For a fresh run. The old log is kept as a backup.<br> |

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

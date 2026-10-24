# Hunter's Workshop

Hunter's Workshop runs your own copy of *Bloodborne* on a Windows PC. It is a launcher and an engine
in one package: the game's own code runs natively on your CPU, and its graphics are translated to
Vulkan.

> **No game files are included.** You need your own copy of Bloodborne (CUSA03173) with update
> 1.09, dumped from your own PS4. This project is not affiliated with Sony Interactive Entertainment
> or FromSoftware.

## Install

1. Download `HuntersWorkshop-v<version>-win64.zip` from
   [Releases](https://github.com/hamzahskafi/HuntersWorkshop/releases) and extract it to a folder of
   its own.
2. Run `Hunters Workshop.exe` and pick your game folder (the one that contains `eboot.bin`).
3. Press **Play**.

You need 64-bit Windows 10 or 11 and a graphics card with an up-to-date Vulkan driver. Your
settings, saves, mods and logs stay in the package folder, so an update or a reinstall into the
same folder keeps them.

## Help and bug reports

Open an issue on [GitHub](https://github.com/hamzahskafi/HuntersWorkshop/issues). Describe what
happened and attach the newest log from the `logs` folder in your package; it names your graphics
card, driver and settings, which is usually what's needed to track a problem down.

## Based on

Hunter's Workshop started from [Lantern Engine](https://github.com/Awakenchan/LanternEngine)
0.9.37 beta 9 (commit `08173e6`) by Awaken. Lantern Engine started from
[DarkIzuku/bloodborne_pc](https://github.com/DarkIzuku/bloodborne_pc), which is built on
[bbport](https://github.com/deadinside28/bloodborne_pc) and
[shadPS4](https://github.com/shadps4-emu/shadPS4).

## Credits

| | |
|---|---|
| Launcher and engine this project started from | [Lantern Engine](https://github.com/Awakenchan/LanternEngine) by Awaken |
| Engine starting point | [DarkIzuku/bloodborne_pc](https://github.com/DarkIzuku/bloodborne_pc), built on [bbport](https://github.com/deadinside28/bloodborne_pc) and [shadPS4](https://github.com/shadps4-emu/shadPS4) |
| VRAM cleanup and frame pacing | adapted from [IFreemz/shadPS4-Bloodborne-DLSS-FSR](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR) |
| Crash and controller fixes | adapted from [AwesomeObserver](https://github.com/AwesomeObserver/bloodborne_pc), [Supermedo](https://github.com/Supermedo/bloodborne_pc) and [deadinside28](https://github.com/deadinside28/bloodborne_pc) |
| PC controls, camera, menus, cheats, online sign-in, occlusion culling, trophy research and the frame-rate corrections (with findings from Kyo's later FPS++ lists and yogesh1239) | [droogie/bbhost](https://github.com/droogie/bbhost) |
| Linux online start and the bound on runaway particle work | [Lena Różalska](https://github.com/DecayingEagle) |
| Online servers | [The Hunter's Dream](https://thehuntersdream.com) |
| FSR 3.1 and FSR 4 on Vulkan | [FireBurn/FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan) and the AMD FidelityFX SDK |
| FSR 4.1.1 | [fsr4vk](https://github.com/dvj5411/fsr4vk), with the AMD driver fix from [0xCydral](https://github.com/0xCydral/bloodborne_pc) |
| DLSS | NVIDIA DLSS SDK and Streamline |
| Sound | [LibAtrac9](https://github.com/Thealexbarney/LibAtrac9) |
| In-game menu | [Dear ImGui](https://github.com/ocornut/imgui) |
| Controller mappings | [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) |
| Trophy list | [pyL1nx/bloodborne_pc](https://github.com/pyL1nx/bloodborne_pc) |
| Launcher UI library | [WPF-UI](https://github.com/lepoco/wpfui) |

### Game patches

The built-in patches are the Bloodborne modding community's work, shipped as their authors wrote
them:

| Author | Patches |
|---|---|
| **Kyo** | 30, 60 and 90 FPS++, 60 FPS without delta time, Uncap FPS++ (with Lance McDonald), the Performance Patch, the resolution patches and light grids, the graphics options (motion blur, depth of field, SSAO, anti-aliasing, dynamic light shadows, screen space reflections), model detail levels (with auser1337), camera distance and auto rotation (with Imedved), Rally, physics, text scale, analog input, the Intel black tonemap fix, and the Player No Dead, Stealth and Silent options |
| **Lance McDonald** (manfightdragon) | Uncap FPS++ (with Kyo), Restore Debug Camera, Unlock Game Region (with Kyo) |
| **auser1337** | Increased Graphics Heap Sizes, model detail levels (with Kyo), Restore Debug Menu (with Whitehawkx) |
| **illusion** | Skip Intro, Disable Chromatic Aberration |
| **Whitehawkx** | Restore Debug Menu (with auser1337) |
| **emoose** | Intel 12th Gen+ SFX workaround |
| **Dasaav** | FMOD Crash Fix |
| **bloo** | Disable HTTP Requests |
| **Imedved** | Disable Camera Auto Rotation via Movement (with Kyo) |
| **Foxy Hooligans** | Bookmark and Capture outputs |
| **stagvant** | Enemy Control |
| **yogesh1239** | Sprint Fix (High FPS) |

## License

Hunter's Workshop, the launcher and the engine, is licensed under the GNU General Public License,
version 3 or later (see [LICENSE](LICENSE)). Much of its code comes from projects licensed under
version 2 or later (shadPS4, bbport, bloodborne_pc and Lantern Engine), which allows it to be
distributed under version 3; the parts ported from bbhost are version 3 or later. Copyright in each
part stays with its authors; the third-party licenses are in [`engine/licenses`](engine/licenses).

## Source

| Folder | What |
|---|---|
| [`engine/`](engine) | the engine: start program, runtime and renderer (see its [README](engine/README.md) for building) |
| [`launcher/windows/`](launcher/windows) | the launcher |
| [`tools/`](tools) | release tooling |

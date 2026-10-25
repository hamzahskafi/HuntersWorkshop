# Hunter's Workshop engine

The engine of Hunter's Workshop: it runs your own copy of *Bloodborne* (CUSA03173, update 1.09) on a
PC. The game's own x86-64 code runs directly on the CPU, a runtime written for this one game stands
in for the PS4's system libraries, and the game's graphics are translated to Vulkan. The launcher
that starts it is in [`../launcher/windows`](../launcher/windows).

> **No game files are included.** You need your own decrypted dump. This project is not affiliated
> with Sony Interactive Entertainment, FromSoftware, AMD or NVIDIA.

## Where it comes from

This engine is Lantern Engine's as of 0.9.37 beta 9 (commit `08173e6`), by Awaken. Lantern Engine
started from [DarkIzuku/bloodborne_pc](https://github.com/DarkIzuku/bloodborne_pc) (tag
`v0.4.0-darkizuku-preview.1`), itself built on [bbport](https://github.com/deadinside28/bloodborne_pc)
and [shadPS4](https://github.com/shadps4-emu/shadPS4), and added:

- **its own start and runtime**: `start/` (bb-start: the game's executable prepared once and reused,
  mods, patches checked against the game's code, the GPU cache kept across updates) and `src/` (the
  runtime, saves, files read in place, pads with menu and keyboard bindings);
- **a direct path away from emulation** ([docs/EMULATION_REMOVAL_PLAN.md](docs/EMULATION_REMOVAL_PLAN.md)):
  draw calls taken from the game's graphics library as records, the PC memory model (the game's
  memory used by the GPU in place), and a native compiler for the game's shaders
  ([docs/native_shader_compiler.md](docs/native_shader_compiler.md));
- **renderer performance work**: draw preparation on worker threads, cached render state, pipeline
  cache warm-up with a crash guard, occlusion culling and frame pacing;
- **the PC version**: native PC menus and keyboard and mouse controls, high frame rates, widescreen,
  HDR, DLSS, FSR 3.1, 4 and 4.1.1, frame generation, trophies, and online play through The Hunter's
  Dream.

The renderer and shader recompiler are still shadPS4's at their core (`gpu/shadps4`, where changes
are marked `bbport:`).

## Before changing it

Read [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md): who plays (GPUs, drivers, CPUs), what each GPU
family lacks, and the checks a change goes through. A change is not done when it works on one PC.

## Build

### Windows

The engine builds in [MSYS2](https://www.msys2.org)'s CLANG64 environment.

1. Install MSYS2 and open the **MSYS2 CLANG64** terminal.
2. Install the toolchain and libraries:

   ```sh
   pacman -S --needed git mingw-w64-clang-x86_64-{clang,lld,libc++,cmake,ninja,pkgconf,python,sdl3,boost,fmt,glslang,spirv-cross,spirv-headers,vulkan-headers,vulkan-loader,vulkan-memory-allocator,xxhash,zydis,robin-map,ffmpeg,protobuf,nlohmann-json,openssl,miniupnpc,zlib,expat}
   ```

3. In this folder, run `bash build.sh`. The programs land in `out/`: `bb-start.exe` (prepares the
   game and starts it), `bb-probe.exe` (the engine itself), and the tools `bb-engine-assets.exe`,
   `bb-shader-lab.exe` and `bb-gpu-capabilities.exe`. `bb-probe.exe` needs the CLANG64 DLLs it links
   to (SDL3, FFmpeg, the Vulkan loader, libc++ and their dependencies) next to it.

The launcher builds with the .NET 8 SDK, from the repository's root:

```sh
dotnet publish launcher/windows/BloodborneLauncher/BloodborneLauncher.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true
```

### Linux

`build.sh` here builds the engine with clang, CMake, Ninja and the Vulkan and SDL3 headers. On
Ubuntu 24.04, `packaging/linux/setup-ubuntu.sh` builds the libraries it needs once, and
`packaging/linux/build.sh` makes a Linux package with the cross-platform launcher
(`../launcher/windows/AvaloniaLauncher`). To run from a checkout:
`BB_GAME_DIR=/path/to/CUSA03173 ./run.sh`.

### What a package holds

A Windows package is the launcher at the top, `package.json` (its version and engine build), and an
`engine` folder with `out/` (the programs and their DLLs), `patches/`, `data/`, `licenses/`, the
default settings, the shader catalogs (`shader-catalog/`: pipelines compiled ahead, so fewer
shaders compile during play) and the upscalers' files (`fsr4_shaders/`, `fsr4vk/`). The shader
catalogs and the upscalers' files are not kept in this repository; `tools/fetch_dlss.sh`,
`tools/fetch_fsr4_assets.sh` and `tools/fetch_streamline.sh` download the NVIDIA and AMD parts.

## Tests and checks

- `build.sh --test`, and the GPU library's tests (`ninja -C out/gpu pc-input-test frame-rate-test ...`).
- `tools/re/frame_rate_base.py eboot.elf [--check]`: our frame-rate table (start/frame_rate_base.inc) from the 60 FPS++
  list and a 1.09 executable.
- `tools/re/frame_rate_check.py [eboot.elf]`: the frame-rate corrections' table against the 60 FPS++ list
  and, given one, a 1.09 executable.
- `tools/re/frame_rate_steps.py eboot.elf [--check]`: the table's `steps` (the game's 1/30 s step-infos read
  from the frame's step) from a 1.09 executable; needs capstone. docs/frame-rate-survey.md has every entry of
  the 60 FPS++ list and what the corrections do with it.
- `tools/validate_pipelines.sh`: the engine on Mesa's software Vulkan driver with the validation
  layer, as each GPU family.
- `bb-shader-lab --check-cache <cache folder>`: what the start's warm-up would build from a player's
  cache, and whether each pipeline's layout matches its shaders.
- `tools/hardware_census.sh <logs>`: the GPUs, drivers and CPUs in players' logs.

## Layout

| Folder | What |
|---|---|
| `start/` | bb-start: prepares and starts the game (Windows) |
| `src/` | the runtime: loader, memory, threads, files, saves, pads, audio |
| `gpu/` | the renderer library: shadPS4's video core and shader recompiler with this engine's changes (`gpu/shadps4`), the PC menus, input and settings (`gpu/shim`), online (`gpu/bbnet`) |
| `scripts/` | patches, mods, assets and GPU cache preparation (Python, also used on Linux) |
| `patches/` | the community's game patches and their checks |
| `tests/` | tests and the shader lab |
| `tools/` | developer tools |
| `packaging/` | the Linux package (`packaging/linux`); `default.nix` and `launcher/` are bbport's AppImage and GTK launcher, kept for NixOS |
| `docs/` | design notes; `docs/history` keeps the notes of the projects it started from |

## Credits and licenses

Like the rest of Hunter's Workshop, the engine is licensed under the **GNU GPL version 3 or later**
([LICENSE](LICENSE)). It combines GPL-2.0-or-later code (shadPS4, bbport, bloodborne_pc, Lantern
Engine), which allows distribution under version 3, with GPL-3.0-or-later ports from
[droogie/bbhost](https://github.com/droogie/bbhost)
(`licenses/BBHOST-PORTS.md`: the PC controls, camera, native option pages, rebirth, trophy research
and widescreen plates). Copyright in each part stays with its authors.

Also in the engine, under their own licenses: [shadPS4](https://github.com/shadps4-emu/shadPS4)'s
video core and shader recompiler (GPL-2.0-or-later), shadNet's client (`licenses/SHADNET-PORT.md`),
[sirit](https://github.com/shadps4-emu/sirit), [half](https://half.sourceforge.net/),
[FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan) and the AMD FidelityFX SDK (MIT),
[LibAtrac9](https://github.com/Thealexbarney/LibAtrac9) (MIT),
[Dear ImGui](https://github.com/ocornut/imgui) (MIT), NVIDIA Streamline (headers; DLSS and Reflex),
cpp-httplib (MIT), wepoll (BSD), and the DejaVu and Lantern Menu SC fonts (the latter a Noto Sans SC
subset, SIL OFL 1.1). VRAM cleanup and frame pacing were adapted from IFreemz's shadPS4 fork, and
crash and controller fixes from AwesomeObserver, Supermedo and deadinside28.

The built-in game patches (`patches/Bloodborne.xml`) are the Bloodborne modding community's: Kyo,
Lance McDonald, auser1337, illusion, Whitehawkx, emoose, Dasaav, bloo, Imedved, Foxy Hooligans,
stagvant and yogesh1239, each credited on their patches. AMD's FSR 4 DLLs and NVIDIA's DLSS files
are not distributed in this repository.

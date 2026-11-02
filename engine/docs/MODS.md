# File mods

The launcher has a **Mods** group: the folder choice, a master switch, a switch for each mod and
arrows for the load order. Changes apply at the next start. The lowest enabled mod has the highest
priority; when files collide, the log says which mod replaced the one before it.

The default is `mods/` in the data folder (the package folder); in bbport's AppImage it was
`~/.local/share/bbport/mods/`. Unpack a downloaded mod like this:

```text
mods/
  Mod name/
    dvdroot_ps4/
      chr/...
      param/...
      menu/...
```

Also accepted:

- `Mod name/app0/dvdroot_ps4/...` and `Mod name/CUSA03173/dvdroot_ps4/...` (the other
  editions' serials, such as CUSA00207, too);
- an extra wrapper folder, as after unpacking an archive into a folder of its name:
  `Mod name/Mod name v1.2/dvdroot_ps4/...` (readmes and pictures may lie next to it);
- a mod without `dvdroot_ps4` that holds the game's folders directly: `Mod name/chr/...`,
  `Mod name/parts/...` (`chr`, `parts`, `map`, `menu`, `msg`, `param`, `sfx`, `sound`, `font` and
  the other `dvdroot_ps4` folders).

Name case inside a mod does not matter: `DVDROOT_PS4/Chr/C0000.chrbnd.dcx` from a mod made on
Windows replaces the game's `dvdroot_ps4/chr/c0000.chrbnd.dcx`. Such a file used to be added next
to it under its own name, and the game did not see it. The start's log shows how many game files a
mod replaced and how many it added: `Mods: 12 game files replaced, 0 added`. If a mod should replace
files but says `0 game files replaced`, check the paths inside the mod. ZIP/7z archives must be
unpacked first. Ready replacements of `dvdroot_ps4` files are supported: textures, models,
parameters, interface, sound and fonts. The manager does not merge the contents of two identical
`.dcx`/`.bnd` files: the last replacement wins whole.

For a folder already prepared for shadPS4, a sibling folder `CUSA03173-mods/dvdroot_ps4/...` next
to `CUSA03173/` works. It is added automatically before the mods from the list, and the master
switch turns it off too. A folder that directly contains `dvdroot_ps4` can also be chosen as the
mods folder; such a folder is added whole, without a list of separate mods. The `-mods` convention
was checked against
[shadPS4's source](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/file_sys/fs.cpp).

The game's originals are not overwritten. For the run, a separate merged tree of links is made in
the data folder's `out/mod-game-*` (on Windows, when the data folder is on another drive than the
game, in `.bbport-overlays` beside the game folder: hard links work within one drive only); reads,
stat and directory listings see the chosen replacements and the remaining original files. The
game's `/app0` and `/hostapp` are read-only. Saves stay in their usual folder and are writable. On
a normal exit or a stop from the launcher the temporary tree is removed. After SIGKILL or a power
loss it may remain; the next start removes such folders.

A dumped game update beside the game folder, named as shadPS4 keeps it (`CUSA03173-UPDATE` or
`CUSA03173-patch` for a folder `CUSA03173`, any case) and holding an `eboot.bin`, is laid over the
game first, the mods over it; the log says `Game update: ...`. It is left out when the game folder
already has the update's version.

Mod files that other players of a co-op session read too (parameters, map layouts, events, AI and
talk scripts, actions, character animations) are counted in the log: co-op partners need the same
files, or enemies and items desync between them.

On drives without links (exFAT, FAT32; Windows junctions need NTFS) the start writes the list of
replaced files to `out/mod-files.txt` instead (`BB_MOD_FILES`), and the runtime reads the mod files
in place.

Mods that replace `eboot.bin`, `sce_module` or `sce_sys`, DLL injectors and script plugins are not
supported by this loader. Patches of executable code stay in the existing `patches/` /
`BB_PATCHES` system. Links inside a mod (symbolic links, junctions) are skipped, never followed,
with a line in the log. Whether a mod works with CUSA03173 1.09 is up to its author.

From a terminal:

```sh
BB_GAME_DIR=/path/to/CUSA03173 BB_MODS_DIR=/path/to/mods bash run.sh
BB_GAME_DIR=/path/to/CUSA03173 BB_MODS_ENABLED=0 bash run.sh
```

The order and the disabled mods are saved in the data folder's `mods.json`:

```json
{"order": ["First", "Second"], "disabled": ["First"]}
```

New folders are enabled automatically and added at the end in alphabetical order.
`BB_MODS_CONFIG` changes the profile's path. `BB_MOD_TRACE=1` with `BB_MODS_DIR` prints the first
reads of the files actually used, for diagnosis.

# Third-party patches (shadPS4 XML)

The `patches/` folder in the data folder (in bbport's AppImage `~/.local/share/bbport/patches/`;
in the launcher the **Third-party patches** group, where another folder can be chosen too). Patch
XML files in the shadPS4/GoldHEN format fit: the `Metadata` for `AppVer="01.09"` and `eboot.bin`
is taken, files with other games' `TitleID` are skipped. A patch is on by default when its file
says `isEnabled`; the choice made in the launcher is saved in the data folder's `patches.json`:

```json
{"enabled": ["file.xml/Patch name"], "disabled": ["file.xml/Another patch"]}
```

Lines of type `bytes`, `bytes16/32/64`, `float32/64`, `utf8` and `utf16` are supported. A patch
with `mask` lines (pattern search) or with an address outside the eboot is skipped whole, with a
message in the log. Third-party patches are applied after the built-in ones
(`patches/Bloodborne.xml`): where they overlap, the third-party one wins. Two kinds are left out
unless the launcher's question at Play is answered "keep": a patch that changes the game's region
while online is on (`BB_REGION_PATCH_ONLINE`), and a resolution patch while the output is not
1920x1080 (`BB_RESOLUTION_PATCH`); the log says why. From a terminal:
`BB_PATCHES_DIR=/path BB_PATCHES_CONFIG=/path/patches.json bash run.sh`.

Patches that replace pointers in tables (such as `60 FPS++` and `90 FPS++`) are moved by the loader
to the image's real address. Such patches used to stop the start with the error
`patch overlaps a relocation`.

# TAA sharpening

The `sharpen` and `sharpness` settings also work for TAA, in the launcher and the in-game menu. The
range is **0…2** (above 1 the RCAS change is extrapolated, stronger than AMD's maximum); at 0, or
with the switch off, there is no extra pass. RCAS is applied to the finished scene before the HUD.
The TAA history keeps the colour before sharpening, so that sharpening does not build up over
repeated accumulation. Flat areas keep their original colour, HDR included; no extra buffer is
needed.

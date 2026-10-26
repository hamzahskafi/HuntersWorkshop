# bbhost engine ports

Source: https://github.com/droogie/bbhost/tree/7c790536c2c27ad7bb5115e3b1a12d7ffd7a972e

`gpu/shim/engine/engine_hooks.cpp` adapts `src/core/thunk.cpp` (the prologue emitter).
`gpu/shim/engine/change_appearance.cpp` adapts `src/engine/change_appearance.cpp`,
`menu_steps.cpp`, `frame_rate.cpp` (only the main-thread hook site) and `world_chr.cpp`
(only the player block lookup). `scripts/engine_assets.py` adapts
`dream_mirror_layout.cpp` and `gcn/container.cpp` (DCX).
`gpu/shim/engine/camera.cpp` adapts `engine/camera.cpp` and the typed repository
lookup in `engine/params.cpp`. Its sliders and the WPF launcher share the existing
`config.ini`; no bbhost renderer or second configuration store is introduced.
`gpu/shim/engine/option_menu.cpp` adapts the native row, caption, section opener and
System builder from `engine/option_menu.cpp`. `menu_memory.cpp` adapts bbhost's
Scaleform heap sizes. `tools/engine/menu_assets.cpp` adapts the independent GFX,
BND4 and FMG serializers from `engine/menu_assets.cpp`, with bounded inflation;
its PC strings come from `tools/engine/pc_option_messages.tsv`. All edited game movies
and language bundles are prepared locally, never packaged or committed.
`engine_state.cpp` adapts typed lookups from `params.cpp`, `player_data.cpp`,
`event_flags.cpp`, `world_chr.cpp` and the shared `menu_steps.cpp` watches.
`event_flag_layout.h` retains the traversal from `decomp/sprj_event_flag_man.h`.
`rebirth.cpp` retains the native apply/heal, refund, snapshot/undo and menu
lifecycle from `engine/rebirth.cpp`. The origin pricing in `rebirth_refund.h`
adds rejection of invalid attributes, non-finite curves and overflowing refunds.
`tools/engine/esd.*`, `rebirth_script.*` and `sha256.*` adapt the corresponding
bbhost serializers and digest helper. Both known altar script identities are
checked before and after preparation.
`graphics.cpp` adapts the render-view flags and freshly blended CPU parameters
from `engine/graphics_patch.cpp`. Fog, renderer replacement and shader patches
are not included; motion blur, DoF and CA keep bloodborne_pc's startup patches.

These portions are GPL-3.0-or-later. Copyright remains with the bbhost contributors.
The surrounding bloodborne_pc code remains GPL-2.0-or-later under its existing notices.
Combined builds containing these ports are distributed under GPL-3.0-or-later; see
`GPL-3.0.txt`. Third-party notices remain unchanged. Generated game assets are local
cache entries from the player's own dump and are not distributed in the repository.

Keyboard/mouse and widescreen source:
https://github.com/droogie/bbhost/tree/db5457cbd447f4ef129b58df0a78e0a8819c7033

The input/bindings, mouse_camera and menu_pointer modules adapt the matching
bbhost host and engine modules. They retain angle-per-count camera movement,
controller priority, target flick, walking, region-aware confirmation, Scaleform
geometry hit tests and capture semantics. input.cpp bridges the existing SDL
event pump, scePad ABI, checked loader image and guest-call bridge.
Native PC Controls / Key Bindings reuse the row builders, checked widget identity
and Defaults lifecycle from option_menu.cpp. Both frontends use bbport.ini.

engine/widescreen.cpp adapts the camera blend hook, UI stage pins and projected
world-plate bounds from live_resolution.cpp and graphics_patch.cpp. It checks
original bytes or the exact existing native-stage resolution patch, preserves
vertical FOV and uses this port's output size. Vulkan UI composition adds uniform
centered fitting, including stencil masks and window-space vertices. The existing
presenter already letterboxes. The existing renderer, temporal resources,
synchronization, shader and motion-vector paths remain in use.

Online and picture sources:
https://github.com/droogie/bbhost/tree/566184a74f071166299bb656c4af479e5bbe2771

engine/online_fixes.cpp adapts src/engine/chara_id.cpp (the login's character step clears the
empty-list character id) and src/engine/sos_signs.cpp (the summon sign timeout). The fog control in
engine/graphics.cpp adapts the fog table upload hook of src/engine/graphics_patch.cpp, redirected
through the function's padding; the play-log redirect in scripts/online.py follows src/hle/http.cpp. The boss defeat flags in
engine/trophy_sync.cpp are from the boss rush table of plugins/boss_rush/boss_rush.cpp.

Frame-rate source (the same commit):
engine/frame_rate.cpp and frame_rate_sites.inc adapt from src/engine/frame_rate.cpp the steps made
to read the frame's time (kFrameTimeSites: the ragdoll/clip drive, the Havok physics and AI worlds,
grass, the target bank), the per-frame loads (frame_rate_dt_loads.inc, made by tools/dt_loads.py;
here they read slots past the end of the game's data segment, in its last page), the values bbhost sets differently from Kyo's
"60 FPS++" at 60, the durations tools/fps_sites.py keeps from that list, and the play time's count;
above 60, as bbhost runs every rate on that list, the frame pacer's target, the values that follow
the rate (kRated) and the counts (kCounts: the sound thread's MainThreadUpdateTime among them, in
place of the list's sleep cap), the cloth's six stiffness caves from "90 FPS++" (ninety_fps_sites.inc)
with their (60 dt)^2 scale read from slots, and uncapped's reference rate (120). They are applied over
Kyo's list as patches/Bloodborne.xml ships it, with the frame's step from the list's own cave, instead
of bbhost's SprjFlipper::Update; a constant the list changed is converted from the game's value. The
patch builders (start/patches.cpp, scripts/patches.py) leave out the list's timed-wait return, as
fps_sites.py does, and convert the FPS++ camera's weights from a 60 fps frame above 60, after
bbhost's camera_tuned_fps. bbhost credits the findings it collects there to Kyo's 1.3 lists and
yogesh1239's high-FPS fixes.

Since 0.3.0 the Windows start program writes Kyo's "60 FPS++" from our own table,
start/frame_rate_base.inc (made by tools/re/frame_rate_base.py from the list and a 1.09 eboot, its
comments after what src/engine/frame_rate.cpp says each entry is), instead of the patch file's list. The
frame timer (BB_FRAME_TIMER=1) follows bbhost's kStepLoad: the cave loads a step of ours into the task
step, here from the game's own pacer measurement through a hook in the cave rather than bbhost's
SprjFlipper::Update, and the follow camera's two eases are read per frame, as bbhost's each_frame
kRated entries. The rest of the list's fixed steps, read entry by entry for 0.3.0
(docs/frame-rate-survey.md), are ours, not bbhost's: the step-infos made to read the frame's step
(frame_rate_sites.inc's steps, tools/re/frame_rate_steps.py), the steps objects keep, the pacer's first
target, the camera lens filter's rate, and the durations after the Havok AI's braking limit.

Game fixes source (0.4.0):
https://github.com/droogie/bbhost/tree/56b868e25b9bb91074db0ab283a83880b4b20d87 (v0.2.19)

gpu/shim/engine/job_pool.cpp adapts src/decomp/sync/adaptive_mutex.cpp (f764eff: DLAdaptiveMutex::Lock
spinning on a plain load with pause) and the node cache of src/decomp/sync/ez_work.cpp (58af1c8). Here
the game's ProcessOne stays: the cache goes in at its entry by a compare-exchange on the job list's
allocator slot and keeps a lock of its own, and the lock compares the owner in its 40 bits.
gpu/shim/engine/fmod_ring.h and fmod_ring.cpp adapt src/engine/fmod_ring.h and fmod_ring.cpp (f1d4fa3):
the guard, not its detector. The play recording's compression level (start/patches.cpp,
scripts/patches.py) follows patches/deflate-level.toml, but points only the recording's producer at the
game's own level=1 string instead of changing the level=9 string all five producers share.
gpu/shim/engine/frame_rate_sites.inc (the Grass group) follows src/engine/wind_pace.h and frame_rate.cpp's
wind sites (b2818b4, 5cee912): the wind's step half the frame's, as a per-frame load of the table, and its
force weighed by the game's 1/60. gpu/shim/engine/frame_rate.cpp's SelfHit follows
frame_rate_atk_params_loaded (6f735f4): the Bloodletter's L2 self-hit 0.4 -> 0.6 m, found again every 120
frames through the param lookup above rather than at bbhost's table-load event.

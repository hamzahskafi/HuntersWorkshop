# Community fixes: the vetted list and plan

Updated 2026-10-26 (A1–A12 in, for 0.4.0 beta 1). Hunter's Workshop (HW) started from Lantern Engine 0.9.37 beta 9 (`08173e6`).
This is the analysis of what the other Bloodborne PC projects fixed after that point, checked
against HW's own code, with what has been done since. Nothing here is merged wholesale: each fix
is taken on its own, with its author credited.

Ground rules for this work:

- No merge with Lantern Engine. Its 0.9.37 stable regressed from beta 8 (the fixed-refresh frame
  lock and the particle cap, both off in HW) and it removed community patches and their credits.
- Community patches and their credits stay. A fix taken from another project is credited in
  `README.md` and in its commit.
- The particle cap (`BB_INDIRECT_CLAMP`) and the fixed-refresh frame lock (`BB_VBLANK_PACING`)
  stay off.
- Work after 0.3.0 goes into the 0.4.x betas; A1–A12 are 0.4.0 beta 1. Linux waits until Windows is
  done.

Addresses: **patch** addresses are the ones `Bloodborne.xml`, `frame_rate_sites.inc`, bbhost and
Binary Ninja use (image offset + 0x400000). **Image offsets** are what bbport's `ImageBase + x`,
HW's `Prepare()` calls and the sampler's `+0x...` print. Each address below says which it is.

## Done since the analysis

| What | Commit | Notes |
|---|---|---|
| Menus the engine holds open are checked before a reference is added (the Dream mirror's editor, the Altar's level-up) | `68c04a1` | `menu_step_is` in `engine_state.cpp`: the step must still hold a reference and carry the expected type (+0x28) and name (+0x30), as the step constructor (patch 0x201cce0) stores them. Fixes the Hunter's Dream "can't interact until Back" case. After Lantern 0.9.37 stable (Awaken). |
| Character creation by mouse, at the title and the Dream mirror | `68c04a1` | Drag on the hunter turns them (right stick), click a slider steps toward that side, drag across a row steps its value, the wheel steps by ten (L1/R1). Right-click stays Back. After Lantern 0.9.37 stable (Awaken). |
| Hitch report, `BB_HITCH_REPORT_MS=<ms>` | `b5105f1` | Off by default. For a frame over the threshold: every guest thread's wait, and every thread's stack twice 20 ms apart, the busy ones first. No thread is unwound while suspended. At most 12 per session, 3 s apart, none in the first 20 s or within 10 s of a loading screen. |
| Online summon no longer holds the game while the server answers | `5177bfa` | `sceNpSignalingActivateConnection` returns the connection at once; the session server's address lookup runs on the NP thread (`sig_resolve`, `thd_np.cpp`), then ESTABLISHED and ACTIVE follow as before. The log line `np: signaling: ... resolved ... (conn N, T ms)` gives the time the game used to stand still. |
| PC memory model off by default (0.4.0 beta 4) | `5f3eed9` | The launcher's default and a one-time switch for existing settings files (`guest_in_place_off_040`); Lantern 0.9.37 beta 4 had turned it on for everyone. The toggle stays. Why, and what a fix would take: `docs/gpu-memory.md`. |
| B13, B15, B16, B17 (0.4.0 beta 5) | `f09580f`, `1aa7905` | **B13:** the wind's step is a per-frame load of half the frame's (`frame_rate_sites.inc`, Grass), its force weighed by the game's 1/60, the time step read from `[rbp-0x170]`; path B (a model without wind slots) leaves before it moves anything. **B15:** `FrameRate::Tick` looks for AtkParam_Pc 2900931 every 120 frames and makes hit0_Radius 0.6 when it is the game's 0.4 (the Hitbox group). **B16:** the keyboard's left stick turns toward the keys at 900°/s, at most 30° a read, a half turn through forward, 60 ms kept after a release: `KeyTurn` in `gpu/shim/input/bindings.cpp` for the PC controls (the default; 0.4.0, after a review found beta 5 had it only in the legacy keys), `key_move` in `src/runtime_pad.c` with them off. **B17:** a controller stick past the round gate is brought back onto it (`round_gate`, `pad_round_gate=0` turns it off). Tests: frame-rate-test, the table checker against 1.09, test_pad.c, pc-input-test. |
| 0.4.0: the log level, hitch reports and controller details | (this commit) | **Standard** (the default) writes save mounts only when they change, each thread name once, no semaphores, no 5 s draw counts (unless frame statistics are on) and the job pools' first cache and busy pools only: a 79-minute session from ~2,700 lines to ~700. **Detailed** (`BB_LOG_LEVEL=detailed`) brings them back with the GPU side's info messages; **Debug** adds its debug messages. **Hitch reports** (`BB_HITCH_REPORT_MS`) and **controller details** (`BB_PAD_LOG=1`: ids, connection, rumble, disconnects, the first vibration request, the round gate) in the launcher's Log submenu. A stable launcher over a beta's settings switches frame statistics and the sharp UI off once. Release notes: `docs/release-notes-0.4.0.md`. |
| B18 (0.4.1 beta 1) | `2a57049`, `22e070e` | The game's threads get the PS4's floating-point state: MXCSR 0x9fc0 (flush-to-zero, denormals-are-zero) and x87 control word 0x037f, the values dumped from hardware (shadPS4#3568, via ayozetr `caa9b4b`). `runtime_ps4_fpu` in `src/runtime_thread.c`: on the main thread before the game's first code (`probe.c`) and at the start of every thread the game creates (THD's dispatcher, which runs the network callbacks, is one); the engine's own threads keep the host's. On by default for the beta's A/B test; `BB_PS4_FPU=0` (the launcher's Game fixes › PS4 floating-point mode) keeps the host's. The log's first line on it gives the host's values. Test: runtime-test (the calling thread, a denormal read as 0, and a created thread). **Confirmed (A/B, 2026-10-29, the same route through Cainhurst and the Hunter's Dream):** with the host's state some of Cainhurst's ghosts were drawn without the lower half of their dresses, every time; with the PS4's they were whole. No measurable cost: 60.0 FPS and 0 frames over 1.5x the median both ways, CPU within the runs' own spread (3.4 against 3.6 cores in Cainhurst, 2.8-2.9 against 2.7-2.8 idle in the Dream). The host's state Windows gave: MXCSR 0x1fa1 (0x1f80 with the invalid and precision flags left by the engine's start), x87 0x27f (53-bit). Beta 2: the launcher's toggle no longer says "(test)". |
| 0.4.1 beta 2: from Lantern 0.9.38 | `22e070e`, `aea89b4` | Awaken's: the camera motion shader gives NaN or infinite motion as none and clamps the rest to four screens (`camera_motion.comp`); a pass camera with a NaN or infinite value is not valid (`vk_camera_motion.cpp`); a small skeleton the GPU wrote (`IsRegionGpuModified`) is not gated by its stale CPU copy's hash (`vk_rasterizer.cpp`); Menu Confirm and Menu Back press their buttons in menus only (`host_bindings_apply`: in the world Return and Backspace were Interact and Dodge; pc-input-test). DLSS 310.9.16 (EthanBlazkowicz, Lantern #81), and `tools/fetch_dlss.sh` replaces a DLSS of another version (`out/DLSS-VERSION.txt`; Lantern's kept any file present). The launcher's default window fits the screen at high Windows scaling (GHusr-y5, Lantern #78; WPF launcher; not its remembering the window's size and place: the launcher opens fresh and centred). |
| 0.4.2 beta 1: community patches, fixed our way | `2d35920`, `3fd3f38` | Checked against the 1.09 eboot.elf (every function from `.eh_frame_hdr` disassembled, the loader's relocations read). **Restore Debug Camera** (Lance McDonald) wrote 868 bytes over the function at 0x1de3330, which the game reaches from a callback a startup initialiser stores (0x1d5f2b6 -> 0x1d5dee0 -> 0x1dbe340 -> 0x1de3330); 483 bytes of it were no-ops. His 71 instructions, unchanged, now sit at 0x3abbe00-0x3abbf80 in the empty code area (patch 0x3abbde2-0x3abcdc6), every branch re-aimed and decoded again; the two hook jumps at 0x19729ab and 0x19729ba point there. **Enable Screen Space Reflections** (Kyo): its 2.0 sat in the clock function at 0x4632d0 (nothing calls it: harmless); the instruction at 0x12a99dd now reads the game's own 2.0 at 0x4bfc494 (read by seven of its instructions; no patch, table or per-frame load of ours touches it), one write fewer. **Performance Patch** removed: it never applied (writes past the executable; GitHub issues 28, 29). **Enemy Control** (stagvant) unchanged: Lantern's "dropped null check" is not there; its 32-bit string addresses only matter on an assert path after which the game itself crashes, and its unchecked value only on a lookup the game treats as an error. **Kept by decision:** 9 of our frame-rate table's 244 writes (and the same in Kyo's FPS lists) land where nothing reads in 1.09 (0x47df584 and 8 at 0x3abc208-0x3abcb20, from another game version); patches using the empty area stay below 0x3abc208. Lantern's remakes of these patches were not taken. test_patches.py (52). |
| B14, option C: taken back out (0.4.2) | `da485f0` | In for 0.4.2 beta 1 (`49cb563`), out again: side by side the flicker held at 30 steps a second could not be told from the game's own at 60, and it was one more hook on a game function. B14 is closed; bbhost's light module stays a reference. With the hook left out (`BB_FRAME_FIXES_SKIP=flicker`) beta 1 lost the GPU every time on loading one save in Yharnam (a write where no memory is bound, after the presenter's post process); 0.4.2 loads that save without it (the next row). |
| 0.4.2 (stable) | `82da137`, `f49ee53`, `461bdfe`, `c8d8db6` | 0.4.1 went out as betas only, so 0.4.2 is the stable after 0.4.0: its release notes (`docs/release-notes-0.4.2.md`) cover 0.4.1's betas and 0.4.2's, and 0.4.1's own notes are folded into them. Launchers and LauncherBuilder 0.4.2; engine id hunters-workshop-14 (beta 1's engine, 13, still had the flicker hook). The PS4 floating-point toggle's hint says "as before 0.4.2" (en, zh-CN). **Closed (2026-10-29):** the GPU loss on loading one save in Yharnam that 0.4.2 beta 1 showed with the flicker hook left out (the row above) does not happen on 0.4.2: that save loads and plays. It was seen only on beta 1's engine, which had the flicker code in. |
| 0.4.3 beta 1: cloth's last substep | `279cb7a`, `1f29ceb` | Found reading bbport 0.7's cloth factor (deadinside28). The 60 FPS++ list solves each cloth once a frame (0xb6e67a: the substep count made 1), the PS4's two solves at 30 at the same 1/60 s; but the cloth's two constraint helpers (0xb71f60, 0xb72180) read the count again from the cloth's data for Havok's stiffness factor (0x1567a80; set, mode, substep, substeps, a, b): 2, with the substep always 0. The factor (read whole): mode 2 adapts, (substeps x a x b)^-1.725, except type 8's share (substep + 1) / substeps and types 5 and 10, 1 on the last substep only, 0 before; other modes 1. So at 60 the adapting sets of types 5 and 10 never acted and type 8's stayed at half; every other factor was the PS4's. `frame_rate.cpp`, the ClothStep group (`clothstep`; `BB_CLOTH_STEP=0`, the launcher's Game fixes › Cloth as on the PS4 (test)): its 14 entry bytes hooked; on a frame that completes a whole 1/30 s the call's substep becomes the last (the stub's rdx changed, the game's function run): at 60 the frames alternate as the PS4's two substeps, above 60 the last comes 30 times a second, a frame of 1/30 s or longer is always one. Only with the list's one substep at 0xb6e67a. frame-rate-test's TestClothStep; test_game_code_sites.py. bbport replaces the factor for its own frame rate (no substep patch there): the frame's length times the game's, with types 5, 8 and 10 the game's. Test: a coat or dress in motion, the switch on and off. |
| 0.4.3 beta 1: from bbport 0.7 | `279cb7a`, `1f29ceb` | bbport 0.7 (deadinside28, 0fc2921) vetted against HW (the next section). **Crash guards** (`crash_guards.h/.cpp`, game fixes `hit_cap` and `gx_query`; crash-guards-test against stand-ins): Havok's hit collector push (0xb84ce0, a virtual returning nothing) skipped past 65536 hits or with a count and no data (SrShadowy's fork: a degenerate query filled one until an allocation failed, SIGSEGV at 0xf00010; bbport writes into a scratch array instead); GX occlusion queries (bbport #336, whp199's #343): the creation (0xe2f080, kind 1 from its one caller) watched, a failed one's query kept (256 slots); begin (0xe2f130) and end (0xe2f160) replaced whole (Replace; their 43 and 37 bytes checked, their calls of the device's 0x2169ec0 and 0x216a070 included) by host versions that do the same with a handle; without one a failed query is created again before its begin, one still without a handle is neither begun nor ended (state 0, result 0: nothing visible for it), a null handle no failed creation left only skipped. **Character creator** (0x21b2780: yaw and pitch += stick x 1.5 a frame at 0x21b27b0): a Menus load, Amount. **Frame limiter** (`driver.cpp`): on time (within a quarter of a period) the next slot a period after this one, late a period after the flip; bbport's `max(next, now) + period` would drift by a sleep's overshoot every frame, the old catch-up rushed the frame after a late one (`BB_PACE_CATCHUP=1`). **Motion vectors** (`motion_history.h`, test_motion_history.cpp): instanced draws hash their per-instance 3x4 matrices up to the instance count into the draw's identity; every draw anchors its object by its first bone's translation, and a match (Prepare and the small-skeleton gate) whose object moved over 0.5 m is another object: no history, and no hold inherited (HW's hold is Lantern's; bbport has none). **The frame's camera** (`vk_camera_motion.cpp`): the largest G-buffer pass's depth and camera, a smaller one after it (the sewers' reflection) a side pass whose camera is not taken but whose position still goes to MirroredPasses (else reflection frames would read as previews); the pass is counted once though OnGBufferPass runs every G-buffer draw. **Lamp shadows** (`vk_rasterizer.cpp` IsComputeImageClear, `TextureCache::FindImageLayer`): a clear of one layer of an array or cube (one level) clears that layer; the game's shader alone wrote memory, which marks an image for upload only from its first byte (InvalidateMemoryFromGPU), and then the whole cube over the faces the GPU drew (`BB_CLEAR_LAYERS=0`: whole images only). Syntax-checked against the full headers; built by the user. |
| 0.4.3 beta 2: caves for two hooks, the cloth's damping, the queries' memory, motion's reads and room | `f9b1863` | **Caves in place of two hooks** (`engine_hooks.h` Divert: the site jumps to a few instructions in a page of their own, which do the displaced instructions' work and jump back; no registers saved, no host call). The hit cap is now the hit array's growth (`crash_guards.h`): only a push that would grow the array passes through the cave, and at 65536 hits or more the array is not grown and the hit dropped (counted and logged by a call from the cave); the push's 142 bytes are checked whole, and crash-guards-test runs the game's push itself, its grow a stand-in. The cloth's last substep is a cave at Havok's stiffness factor that reads a per-frame slot of the frame-rate table (`ClothCave`, frame-rate-test unchanged). **The cloth's damping**, from an audit of the 60 FPS++ list for code that reads again what an entry changed: the cloth's damping operator (0xb6fb90) works out a factor a substep, powf(1 - the data's damping, the frame's seconds over the data's substep count, 2), and the integrator (0xb73df0) applies it at each substep, under the list's one a frame: half the PS4's damping a second, at 60 and above since the list. The count is read as 1 there too (`frame_rate_sites.inc`, the ClothStep group and its switch; only with the list's one substep at 0xb6e67a). The same function's rescale of the cloth's last positions takes a ratio of two substeps and is unchanged. The rest of the list: the other reads of the constants its loads were pointed away from are pointed with them or already ours (the clips' three paths, the wind's pairs, status effects, 0x2677780); its constants' and immediates' readers were checked for 0.3.0 (`docs/frame-rate-survey.md`); the damping exponent's entry (0xb6fbd4) reads one 1.0 for another; the count's other readers, the constraint helpers, are the cloth's last substep. **GX occlusion queries:** there is no pool of queries to size. The handle (0x18 bytes, the device's +0xdc10) and the query's 0x80 bytes of results (+0xdc18) come from the game's lock-and-TLSF graphics allocators every GX object shares, and the query's release (0xe2f020) gives both back. What filled the heaps was queries that never finished (corrected in the audit after beta 3: HW's own `liverpool.cpp` says so): stand-in occlusion counts written before the DMA that clears each query left them unfinished, and the game made new ones in their place every frame until it could make no more, then began one without a handle (0x2169edc). The counts are written in order since Lantern 0.9.37 beta 7 (`BB_ZPASS_IN_ORDER`), HW's base. The guard stays for anything else that runs the heaps out. **Motion vectors:** the motion code reads a buffer only when no GPU write to it is pending (`IsRegionGpuModified`): with Precise readbacks a read protected a page the GPU had written and could wait on a recording thread. Its storage counted by reason (characters, GPU-written small, moving small, held small, all) with the last frame's demand, a second stats line every 600 frames. The per-frame positions: 8 Mi vertices (256 MiB) with 6 GiB or more of GPU memory, 4 Mi below, `BB_OBJECT_MOTION_VERTICES=<millions>` to set it. In 0.4.2's Yharnam logs 13 of 15 reports ran out (up to 824 motion draws a frame, about 400 skipped): the counts say which rule fills it before 0.4.3 changes the rule. Test: frame statistics on, Yharnam and a coat in motion with the cloth switch on and off. |
| 0.4.3 beta 3: motion's room, by what fills it | `6b3ac2a`, `06ea505` | Beta 2's Yharnam log (RTX 3070 Ti, Model LOD -2, 38 reports): two rules fill the room. Small skeletons the GPU wrote asked for 59% of the vertices (1.83 M a frame on average, 2.69 M at most), character-size buffers for the rest (1.25 M, 2.41 M; the class is by size, so batched foliage, whose instance matrices look like bones, is in it, as intended for its wind). The small skeletons the gate reads were all still (28 a frame, none moving or held). With 8 Mi vertices 2 reports of 38 skipped draws, 0.27% of them, in short peaks the last frame's sample missed; 0.4.2's 4 Mi overflowed in 13 of 15. The GPU-written rule stays: a static prop stored costs room, not a wrong vector. **Giving way** (`motion_history.h`, History::Prepare): a GPU-written small skeleton is stored only if it leaves the room the other draws asked for last frame and have not asked for yet this frame, so characters, foliage and moving small skeletons come first whatever the draw order; motion-history-test's "giving way". **The busiest frame's ask** in the stats line (Stats::peak_demand) in place of the last frame's. **8 Mi on 6 GB cards too** (`vk_object_motion.cpp`: a budget of 4 GiB or more; a 6 GB card's is about 4.5 GiB after the driver's and the engine's reserve), 256 MiB. Not done: gating the GPU-written ones on their bones by hashing the copy's source at the copy (the game copies them from its own memory), about half the ask, at a hash for each of ~260,000 copied ranges a second; for if the room runs short again. Test: frame statistics on, Yharnam; the skips should be GPU-written small ones only. |
| 0.4.3 beta 4: the room kept for a crowd | (this commit) | Beta 3's log (14 minutes in m24 and m27, 1,100 to 1,670 draws a frame, 83 reports): the busiest frame of a report asked for 5.87 M vertices at the median and 10.48 M at most, over 8 M in 9; 0.48% of draws skipped, in 10 reports, about 99% of them GPU-written small skeletons. Characters still lost about one draw a frame in the two busiest stretches (9.95 M and 10.31 M asked): the room kept was last frame's ask, and a crowd coming into view asks for more than the frame before. **The room kept** (`motion_history.h`): the other draws' busiest ask of the last 32 frames (History::ReserveFrames, half a second at 60) that they have not asked for yet this frame; motion-history-test's "giving way" covers a busy frame followed by a quiet one, and the room coming back 32 frames later. 8 Mi kept rather than 12 (384 MiB): what overflows is mostly static props. No object is drawn or hidden by any of this: a draw without room keeps the camera's motion. Test: frame statistics on, a busy street; characters should show 0 skipped. |
| 0.4.3 beta 5: the motion bookkeeping, lighter and measured | `dc2cdde` | The section profile (`BB_SECTIONS=1`, beta 4, ~22 minutes at 60, ~1,000 draws and ~1,760 packets a frame in the busy stretches): the draw recording thread's 4.0 ms a frame, of which recording draws 2.9 (textures 0.65, vertex inputs 0.45, motion 0.34, buffers 0.32), ObtainBuffer 0.95 (the tracker's walk 0.49, page protection 0.30), the copy shader 0.42, ordered tasks 0.27; the GPU thread idle 76%. Object motion, 0.4.3's own, ~0.54 us a motion draw: a draw's identity is now hashed once for its three lookups (History::Key and GateEntry carry it), and the index range cache is an open-addressing table rebuilt on Trim instead of `std::unordered_map`'s node per mesh. A warm benchmark of 630 draws a frame puts the bookkeeping itself at ~41 ns a draw and the range lookup at 5.7 ns (6.8 before): most of the 0.54 us is elsewhere, likely reading the game's buffers. Three sub-sections say where (`motion buffers`, `motion ranges`, `motion history`; inclusive in `motion`), for the next change. Next (0.4.4): the copy shader's upload sources read without write tracking. |
| 0.4.3 beta 6: the copy shader's sources untracked, index ranges revalidated every 600 frames | `ac9a9bd` | Beta 5's profile (~27 minutes at 60, ~1,030 draws and ~1,780 packets a frame): motion 0.40 ms a frame, of which its buffers 0.02, its bookkeeping 0.03 and its index ranges 0.22: ~10 full rescans a frame of meshes whose ranges were cached. 3.15 million scans in every log since 0.4.0 found no change: `IndexRangeCache::Revalidate` 32 -> 600 frames. Write faults over the session, 1.8 million (~1,370 a second): 38% in the game's memcpy into the graphics device's per-frame upload area (0x26adb90, from 0x2168a6f) and 18% from the copy shader's range list beside it (0x26b2810: each entry's source and destination offsets), 11% at 0x28dd6b9, 6% at 0x261af9e. Both upload-area writers fault because the copy shader's sources, merged into spans of up to 64 KiB (`BB_COPY_MERGE_KB`), exceeded the 16 KiB stream threshold and took the tracked path (walk, upload, re-protect); its range list is read from game memory directly and was only on the same pages. Now those sources (`BufferCache::copy_source`, set around the two source fetches in `vk_shader_hle.cpp`) take the stream path up to 64 KiB (`BB_HLE_STREAM_KB`; 16 or less: as before): one copy on the copy threads, which guest fences wait for; a span the GPU wrote stays tracked. With the PC memory model on these are read in place as before. The in-place mapping of the upload area itself (the plan's "row one") would save no more for these and needs sparse binding: not done. Test: frame statistics and stage timings on, the same route; the `HLE copy ObtainBuffer paths` line's `src VRAM` should go to near 0 and `src stream` up, write faults about half, `protect` and `VRAM track` down; `motion ranges` near 0. |
| 0.4.3 (stable) | `279cb7a` to `ac9a9bd`, (this commit) | Beta 6's profile (~28 minutes, the Lecture Hall, ~1,060 draws a frame) against beta 5's: the draw recording thread 4.01 -> 3.51 ms a frame (ObtainBuffer 0.98 -> 0.71, the tracker's walk 0.54 -> 0.29, protection 0.35 -> 0.15, the copy shader 0.43 -> 0.24, motion 0.40 -> 0.19; the stream copies 0.08 new); write faults over the session 1.80 M -> 0.92 M, protection calls 7,440 -> 3,610 a second, CPU 3.53 -> 3.32 cores; every copy source on the stream path; 60 FPS, four frames of 33-37 ms in play, no new visual faults (the user). The Lecture Hall, which used to stutter, is smooth (the user credits the limiter's late-frame slot with the rest). Left for 0.4.4: the upload area's `memcpy` still 29% of the remaining faults (its data also drawn as vertex data, on the tracked path), the writer at 0x28dd6b9 (20%), and the plan's row two (meshes, then textures, uploaded once). Set aside: the Iosefka's Clinic wall (the game's, Lantern has it too). Release notes: `docs/release-notes-0.4.3.md`; the PGO profile trained on this code. |
| 0.4.4 beta 1: the GX region streamed, image memory reused, chunks kept, the long frame's own figures | `1e3e61c`, `ecfb172`, `c17c146`, (this commit) | **Defaults:** the unified frame time on (`BB_FRAME_TIMER=0` turns it off; the launcher turns it on once for existing files, `frame_timer_on_044`); the project's test settings for keys a config.ini lacks (FPS++ camera tuning, FPS counter off, sharpening 0.20, depth of field on, intro skipped); logging off at the standard level; upscaler, preset and model detail stay per-PC. **The GX range allocator's region** (`bbport_gx_ring.h/.cpp`, gx-ring-test): read from the game's objects once its graphics device exists (the holder at image 0x55406c8, the upload allocator at device +0xb3e0, whose first field is the range allocator, region [+0xb8, +0xc0) checked against its size at +0xd0; each offset checked against the 1.09 code that uses it). Read-only, non-texel buffers inside it up to 256 KiB (`BB_GX_RING_STREAM_KB`; 0: as before) take the stream path in `ObtainBuffer`; the write-fault report marks sites inside it `(GX range allocator)`. **0x28dd6b9** (image fn 0x28db130, from 0x2a92b95 in 0x2a926e0, from 0x28ecb83): a SIMD transpose writing `count` elements of 384 bytes backwards from its first argument, the output of 0x28f89a0 -> 0x26db1b0, a suballocator that maps a GX dynamic buffer through the device's +0xb730 with modes 4 and 3 (rename on full); the rename path reaches the range allocator's collector (0x26aa860). So likely inside the region; the fault report's marker says. **Image memory reuse** (`image.cpp`, `TrimImageMemory`): images of 4 MiB or more whose driver does not prefer dedicated memory get memory not tied to them (VMA `CAN_ALIAS`), kept 4 s after the image is freed for the next image of exactly that size and a matching memory type (create, then `vmaBindImageMemory`), up to `BB_IMAGE_REUSE_MB` (256; 0: off); everything kept is freed when the texture collector is at its pressure mark, and before the allocator goes. The `UniqueImage` move constructor now carries the device (it lost it). **Residency chunks** (`FreeEmptyChunks`): with room in VRAM, 30 s before an empty chunk goes (`BB_VRAM_CHUNK_KEEP_SECONDS`; 0: 5 s) and the 64 MiB chunk emptied last stays; tight, or with the PC memory model (its evacuations start on free slots): as before. **Long frames** (`driver.cpp`): the `of which:` parts were differences since the previous long frame (taken only then): now every frame; with images given new memory, a freed one's, and checked (`BbStats::image_allocs`, `image_reuses`, `image_checks`); the RAM line once a minute adds memory kept and the totals. **Uncapped flips** (non-precise path): the flip loop breaks once the vblank tick is due. **Frame capture:** each pass lists its draws (beta 5's engine part, `a05ff7f`). Version 0.4.4-beta.1, engine id 23. Syntax-checked against the full headers; built by the user. Test: frame statistics on, an area load or two and a busy stretch; the fault report's top sites should say `(GX range allocator)` or be gone, the RAM line should show reused images, stall lines their own figures. |
| 0.4.4 beta 2: the GX region reader fixed | (this commit) | Beta 1's log (`20261030_022242`, ~19 minutes, RTX 3070 Ti): `the game's GX range allocator not looked for (its code differs from 1.09)`, so the region was never streamed and the write faults stayed as in 0.4.3. The code was 1.09's (the four checked sequences match the eboot); the reader asked `runtime_memory_clamp` whether the image was mapped, and that table holds only the game's mappings (direct, flexible, reserved), never the image the loader placed. gx-ring-test's stub answered yes for everything. Now the loader's image pointer and size reach the reader (`BbGxRing::SetImage` from `bbgpu_patch_image`): the code and the device's holder are read inside the image, the game's objects through the table; the log names the offset of a sequence that differs. The test's stub answers as the table does (nothing in the image) and the range allocator object sits outside the image. Beta 1's other items, from the same log: **image memory reuse** 3 of 19,305 new images (0 to 10 MiB kept): large images live long here, small ones come from VMA blocks; kept as is, it costs nothing when unused. **Long frames** now their own: the first area load 112.5 ms, of which image allocation 16.0 ms (186 images), hashing 30.5, detiling 16.4; 7 long frames, all at loads; play 60 FPS with 0 to 2 frames over 33 ms a minute. Stage B 2.05 us a packet, as in beta 6. A YEBIS depth-of-field message (`SetDepthOfFieldEdgeQuality`) appears once, as in a 28 October log. **VRAM** (the user, with an overlay): about 1 GB less in two dense areas than before (7.4 GB, a little jittery, to 6.2 to 6.5 GB, smooth); the log's flip timing is as smooth as beta 6's (stddev 0.15 against 0.17 ms, p99 17.1 ms), so the jitter was likely the driver short of VRAM, which flips do not show. The engine's images were the same size (0.9 to 1.5 GB); beta 1's only change to how VRAM is taken is that images of 4 MiB or more the driver does not ask dedicated memory for are no longer tied to it (VMA `CAN_ALIAS`, for reuse); a fresh pipeline cache is the other difference. Windows logged no VRAM figures: now a `VRAM:` line once a minute with the Memory line (always on), the driver's count against its budget, then the engine's part (VMA blocks, images, the game's data and its unused part); `BB_IMAGE_REUSE_MB=0` (memory tied again) in the same area tells whether the untied memory is the cause. Audit after beta 2 (for beta 3): the GX region's buffers are now copied at every binding, where before one upload a frame served every draw that bound them; the stream buffer is a 128 MiB ring that waits for the GPU when it wraps (StreamBuffer::WaitPendingOperations, the frame stats' blocked GPU ticks). The HLE copy stats now add `GX region buffers copied when used: N/s, KiB each, MB/s; N/s kept tracked (the GPU wrote them)` (BufferCache::gx_ring_streams), so the bytes are measured, not guessed. Release notes: the beta 1 claims marked to be measured. Version 0.4.4-beta.2, engine id 24. Test: as beta 1; the log should say `the game's GX range allocator at ...`, and the fault report mark or lose `0x26adc05`, `0x26b2b0b` and `0x28dd6b9`. |
| 0.4.4 beta 3: the GX region looked for on its own, with fault-safe reads | (this commit) | Beta 2's log (`20261030_025009`, ~13 minutes, three area loads): neither `GX range allocator at` nor `not looked for`, so the code check passed and a later step failed without a word, or the look hardly ran: it ran only from ObtainBuffer, on every 256th read-only binding of 16 to 256 KiB per thread. The chain was checked again against the eboot: `lea rax, [0x55406c8]; mov rax, [rax]; mov rdi, [rax]` (the device behind the holder), the upload copy's `lea rdi, [rbx + 0xb3e0]` with rbx its second argument, the device (its caller passes r12 and r12 + 0xb3c0), the upload allocator's `mov rdi, [r14]; call 0x26a9d50` (its first field the range allocator), and the region's init storing start (+0xb8), start + size (+0xc0) and size (+0xd0). Now: reads through `BbPlatform::ReadProcessMemory` (Windows' ReadProcessMemory, Linux's process_vm_readv: an unreadable address fails the read; what the engine's other game readers use) instead of the memory table; the look runs once a second from ProcessIdleBlocks until found; after 60 seconds without it the log says once where it stopped and what it read (`not found yet: stopped at ...`). Every binding inside the region is counted by kind with its bytes, in the HLE copy stats: `GX region bindings: read, small (copied anyway); read, copied by its rule; read, GPU-written (tracked); read, larger (tracked); texel (tracked); written` (BufferCache::gx_ring_calls), so the next log says whether 16 to 256 KiB is the right window. gx-ring-test: no memory table stub; a device pointer into kernel space fails the look and the report names the step. Beta 2's log otherwise: 60 FPS; 11 long frames, all at the three area loads (a frame registering 186 to 851 images, then one of 44 to 54 ms with little in it) and an upscaler switch; image reuse 4 of 22,625; VRAM by the driver 3.2 to 4.7 GB of a 7.25 GB budget (an overlay counts every process). Version 0.4.4-beta.3, engine id 25. Test: as before; the log should say `GX range allocator at ...` within a minute of loading a save, or `not found yet` with the step. |

Checked and found already in HW (no change):

- **WASD acting as the d-pad after a menu or message.** HW's own fix (0.1.0 and `f9e343b`) matches
  the Lantern stable's code: `offers_choice`, the input-blocked early return, Escape as a closing
  button and the 250 ms settle.
- **Hover updating the item info.** HW's pointer already does what the game's list update does
  when the cursor moves (`SetCursor`, the listener at +0x30 slot 0x10, the cursor sound), through
  the vtable slot and all five direct callers of the list update (patch 0x1ecff30). No difference
  from the Lantern stable was found. If a menu is found where the info doesn't follow, look there.
- **Respawn quick re-entry (no 12-second minimum).** Already in HW (`src/probe.c`, AwesomeObserver
  `149972b`). The first version of this list wrongly said it was missing.

Investigated, not changed:

- **The game's file sync during saves.** HW turns `fsync` into a full disk flush on Windows
  (`src/win32_compat.h:45`: `_commit` → `FlushFileBuffers`). The logs show it isn't the hitch:
  the game saves every few seconds and almost never hitches, and during the hitches its main
  thread waits on another thread rather than writing. Left as it is.
- **Controller vibration through DS4Windows** (2026-10-26). A DualSense over Bluetooth, behind
  DS4Windows' virtual DualShock 4, had never vibrated in HW or Lantern; after a rumble test in
  DS4Windows it did, in 0.4.0 beta 4 too. Beta 5's logging (`823493c`, reverted) showed the whole
  path working: SDL's own driver has the virtual pad as wired (`054c:05c4`, HID path, rumble yes),
  and the game's requests (patch 0x2a9c790, from its vibration update at 0x1395be0; the large motor
  0–255, the small one 0 or 1) are sent. Whatever stopped it was outside the engine. If it comes
  back, that commit's logging says which side it is.

## Current investigation: the 40–120 ms hitches

| Log | What it showed |
|---|---|
| `20261025_180526` (60 FPS, ~13 min) | Pacing near perfect (median 16.67 ms, stddev ~0.05 ms). Four long frames, 42–106 ms. In 3 of 4 the hitch came before the nearest save, not during it. |
| `20261025_202235` (`BB_SAMPLE_THREADS=main`) | 17 long frames during play (43–122 ms) plus one area load. The main thread is in the game's condition-variable wait (image offset 0x2083ecd, Dantelion `PthreadCondition.cpp`), so it waits on another thread. Nearly every one has a 9.4 MB texture upload (a 1920×1080 image, 0x900000 bytes) and 1,400–3,700 memory-protection calls (90–235 MB of pages) in the same frame. |

### Hitch reports, six sessions (2026-10-25 evening, ~95 min of play, 7 reports)

Sessions `205240`, `211729`, `214630`, `221249` and `234823` had `BB_HITCH_REPORT_MS=40`;
`20261026_002308` had 30 and included an online co-op summon.

**Fewer long frames in play than this afternoon:** 0.1–0.3 frames a minute over 1.5× the median,
mostly 25–65 ms. That compares with 0.6 (`180526`) and 0.9 (`202235`). Loading screens are
excluded from all of these counts. The areas played differ, so no change is credited for this.

**Each report costs about 200 ms itself**, so "the next frame came 240 ms after" is not the
hitch's length. Leave the switch off for normal play.

What the reports found:

- **The game's job workers were never the cause.** In every report the six `CSChrThread`
  workers and the `EzWorkPool` threads sat idle in their condition-variable wait. A1 stays a
  throughput fix, after bbhost's measurement, but it will not remove these stalls.
- **Online summon: a synchronous server request on the game's network thread.**
  - Both reports in `002308` end with The Hunter's Dream's
    `sceNpSignalingActivateConnection ... (resolved ...)` line.
  - `thd_np.cpp:117` calls `ServerSignalingResolve`, an HTTPS POST to the session server
    (8 s read timeout, 5 s connect), inside the call.
  - The game's network thread (`NexusRevolution`) makes that call while holding a game mutex
    (0xf3ecd3b0). The main thread needs that mutex (`DLLightMutex` 0x207ee90), so the game
    stops until the server answers. The report named the mutex's holder.
  - On PS4 this call returns at once and reports the connection later through the signaling
    callback.
  - HW's matching2 calls already run on a worker (`DispatchAfter`); only this one didn't.
    Inherited from Lantern 0.9.37 beta 7; the unreleased `online-shadps4-0.20` branch has it too.
  - **Fixed:** the lookup now runs on that worker, and the connection is announced when the
    address comes back (see "Done since the analysis").
- **In-play streaming frames** (100–350 MB of uploads in one frame while moving):
  - The main thread sleeps in the game's pacer (0x2034770): the frame is late on the GPU side.
  - The engine's breakdown for those frames, as thread time across the draw-preparation
    workers: 85–133 ms checking uploaded images for changes and 35–58 ms of Vulkan memory
    allocation.
  - The draw-recording threads are inside the NVIDIA driver: kernel calls, and one of the
    driver's locks with `_nvngx.dll` (DLSS) on the stack.
- **The 9.4 MB full-screen image** (1920×1080, 0x900000 bytes) re-uploaded after the game writes
  it from the CPU: 42–65 ms. Seen in play and around the summon.
- **Flip step waiting for the game's render thread.** The main thread is in the flip step
  (0x1d976e0, `SprjFlipper`), which hands work to `GXRenderThread` and waits for it (0x2198fd0).
  Named with the user's `bb-probe.exe` (debug line tables; `scratchpad` tool
  `probe/sym_reports.py`):
  - **The render thread** is in `sceGnmSubmitCommandBuffers` → `WaitPreviousFrame`
    (`gnmdriver.cpp:190`), waiting for the GPU side to retire the previous frame. In one report
    it is in `SubmitGfx` → `Enqueue`'s lock (`liverpool.cpp:2484`). On average that wait is
    0.02–0.14 ms a frame; frames still overlap (no "no longer overlap" message).
  - **The GPU side is late because of the NVIDIA driver:**
    - the presenter in `scheduler.Flush` (`vk_presenter.cpp:1381`, the frame's `vkQueueSubmit`)
      or making kernel calls;
    - the draw recorders waiting on a driver-internal critical section, or in kernel calls
      (`win32u`) of their own.
  - **With the memory model off, one report also has the recorder in `SynchronizeMemory` →
    `UpdatePageWatchersForRegion` → `VirtualProtect`** (`buffer_cache.cpp:3202`,
    `win32_memory.c:201`).
  - **Another report is game work on the main thread itself** (0x103d040 / 0x1067530, the
    task manager and render jobs), 40 ms of it.

### PC memory model (`BB_GUEST_IN_PLACE`), on and off (RTX 3070 Ti, i5-14600KF, 60 FPS)

On in `180526`, `202235`, `205240`, `221249`, `003848`; off in `211729`, `214630`, `234823`,
`002308`.

| | On | Off |
|---|---|---|
| FPS over the session | 60 throughout, no sinking | 60 throughout |
| Pages re-protected a second | ~6,000 | 105,000–129,000 |
| Draw recorder (stage B) busy | 12–16 % | 15–22 % |
| CPU cores in use | 2.8–3.2 | 3.0–3.4 |
| In-play 5-s windows with a frame over 33 ms | 10 in 35 min (0.29/min; `202235` left out, it ran the sampler) | 6 in 57 min (0.10/min), 2 of them the online summon |

- **The sinking from 2026-10-22 is gone.** The in-place share follows the area (0.1–551 MB a
  frame) and doesn't climb.
- **The long-frame difference points to off being smoother at 60 FPS on this PC.** But the counts
  are small and the routes differed, so it isn't settled.
- **Off costs CPU that is spare at 60.** Above 60 that changes, and on would matter more.
- **To settle it:** the same route offline (no summon), 10 minutes each way, hitch report off.
- **Settled for now (2026-10-26):** off by default since 0.4.0 beta 4; see `docs/gpu-memory.md`.

### GPU memory work: set aside, written up in `docs/gpu-memory.md` (2026-10-26)

Late frames in play come mostly from the PC memory model's moves: sparse rebinds that wait for the
GPU, and promotions into VRAM in bursts. A 0.4.0 beta 3 session with the model on had 11 frames
over 30 ms in ~20 minutes, each inside a burst of moves; the session before it, with the model off,
had none in ~8.5 minutes. So the model is **off by default since 0.4.0 beta 4**, and the rest is
skipped for now. `docs/gpu-memory.md` has the evidence, the causes with their code (the model's
moves, pinning, image memory created per image, residency chunks, the full-screen image), and a
step-by-step solution, in case it's picked up again.

## A. Bugs HW has, with sound fixes (recommended)

| # | Fix | What it fixes | Where | Source and credit | Risk |
|---|---|---|---|---|---|
| A1 | **Job-pool lock convoy** | The game's job lock spins with `lock cmpxchg` + `rdtsc` and no `pause`, and every job pop frees its list node through the general allocator inside the queue lock. The six character workers spend ~60% of their time waiting; the main loop waits 1.4–3.5 ms a frame at Central Yharnam. Fix: the same lock transitions with a plain-load `pause` spin, and a per-pool cache of list nodes (a stand-in allocator in the list's allocator slot). | `DLAdaptiveMutex::Lock` patch 0x24368c0; `CSEzWork::ProcessOne` patch 0x2432370; allocator vtable patch 0x57aefa0 | droogie, bbhost v0.2.19: `src/decomp/sync/adaptive_mutex.cpp`, `ez_work.cpp` (`58af1c8`, `f764eff`) | Medium: replaces two game functions |
| A2 | **Compression level 9 → 1** | The game compresses a ~300 KB player recording on the main thread every ~50 s at zlib level 9: ~23 ms in one frame (bbhost's measurement). Not visible at 60 FPS in the logs so far; matters more at 120. Surgical form: give only the recording's producer its own `level=1` string. | String at patch 0x4d3a783 (`algorithm=deflate;level=9`; bbhost writes its `=9` at 0x4d3a79a), shared by 5 producers; the recording's is patch 0x1dc97a0 (its `lea` at 0x1dc981f) | droogie, bbhost `patches/deflate-level.toml`; via ayozetr `ebb79d3` | Low |
| A3 | **Indirect-buffer task leak** | Each nested command buffer's coroutine stays suspended when done and is never destroyed: ~600 bytes leaked per indirect buffer, every frame, for the whole session. | `liverpool.cpp`: `ProcessCeUpdate` (~920), the IB case of `ProcessGraphics` (~1994), `ProcessCompute` (~2168): `task.handle.destroy()` once done | upstream shadPS4 `12c5978f`; ayozetr `3f71604` (via Mrsuss60); DarkIzuku `195ab0b` | Trivial |
| A4 | **Render job slot leak** | Laurence's cinematic hangs above 30 FPS: the render job runner's two error exits return with the pool slot still held in `[runner + 0x120]`; after six failures every thread needing a slot sleeps for ever. Fix: give the slot back on those exits, and when the runner is reset with one held. | Image offsets: runner step 0x1067f60, error exit 0x1068092, resets 0x106798a and 0x1067a5c, give back 0x1056990 | deadinside28, bbport 0.6 (`8185b88`, `bbport_guest_hooks.cpp` `JobSlotFix`); ayozetr `087f051` (install after relocation: `7e396e9`) | Low |
| A5 | **Blur kernel clamp** | Crash `0xa0020007`, or a black screen (the guest stops submitting) when game data gives the blur kernel a base sample count of 2. Raise it to 3 at the function's entry. | Image offset 0xbbc3e0 (`GPUTexUtil_GetRecursiveSampleParameters`) | deadinside28, bbport 0.6 (`InstallKernelSampleClamp`) | Low. Matters more for HW than first thought: see the integration plan |
| A6 | Flexible-memory split | Splitting a flexible mapping keeps the left piece's offset for the right one; unmapping it frees and zeroes the wrong pages and leaks its own. | `src/runtime_memory.c:340`: `if (right.kind==KIND_DIRECT)` → `!= KIND_RESERVED` | bmy (bbport-mac `9ffc78d`), ported by 0xCydral (`efdf7b1` in ayozetr's fork) | Trivial |
| A7 | Pipeline creation failure | A pipeline the driver refuses ("ErrorUnknown", some AMD and older drivers) ends the game with code 23. Skip its draws and dispatches instead, and don't save it to the cache. | `vk_graphics_pipeline.cpp` ~651/668, `vk_compute_pipeline.cpp` ~142/158 (`ASSERT_MSG`), `vk_pipeline_cache.cpp` | Supermedo `188c1f6`; ayozetr `49a37b4` | Low |
| A8 | Pipeline cache stale state (NVIDIA) | A draw without a pixel shader takes the previous draw's fragment runtime info for NVIDIA's clip-distance emulation; a geometry-shader pipeline loaded from disk loses its fetch shader. | `vk_pipeline_cache.cpp` ~886 (`bind_stage(Fragment)` with no reset); `vk_pipeline_serialization.cpp` ~182 (`fetch_shader_data` overwritten) | deadinside28, bbport 0.6-pre1, via ayozetr `5ad6bb0` | Low |
| A9 | Tiler staging read hazard | Texture write-back's tiling dispatch reads a staging range that isn't recorded as read, so a later writer can reuse it first. | `tile_manager.cpp` `TileImage`: `AccessBuffer(staging, eComputeShader, eShaderRead)` after the dispatch | DarkIzuku `8e0842c` | Trivial |
| A10 | Rom's lake crash (FMOD) | The game's FMOD Ex frees a shared subsound that is still linked in the master sound group's ring; the next link beside it writes through freed memory. Seen arriving at Rom's lake from the Fishing Hamlet. Fix: at FMOD's pool free, unlink a sound still linked, and keep the shared subsound that refused its release. | Patch addresses: FMOD `MemPool` free 0x11ab3a0, `SoundI::release` 0x11be120, the shared subsound's free 0x11be682 | droogie, bbhost `src/engine/fmod_ring.cpp` (`f1d4fa3`) | Medium |
| A11 | AvPlayer queue race | `Pop()` checks the size outside the lock; a `Clear()` from another thread in between reads an empty queue. | `avplayer_common.h:72` | ayozetr with emurphy04, `3d2b8b3` | Trivial |
| A12 | Stencil linking | `FindDepthTarget` links whatever image sits at the stencil address; a colour target there gets reads and writes through an invalid view. Possible NVIDIA GPU hang (Xid 109). | `texture_cache.cpp` ~1225 | shadPS4#5118, ported by artemkaVG (`da5561d` in ayozetr's fork) | Low |

Suggested first batch: A1–A12, B13 and B15.

## A1–A12: integration plan

Analysis done 2026-10-25, and all twelve went in the same day for 0.4.0 beta 1 (built by the user,
not here). Addresses in this section are image offsets unless marked "patch".

### As built (0.4.0 beta 1)

| # | Commit | Differences from the plan below |
|---|---|---|
| Shared | `ebb9026` | As planned: `Hook::original`, `Replace`, `skip_list.h`. |
| A3 | `c108cd9` | HW has three nested waits, not ayozetr's five: the other two are in code only that fork has. |
| A6 | `f9e74c9` | As planned. |
| A9 | `3569d42` | As planned. |
| A11 | `fe331e3` | As planned; the video and audio reads also return on an empty queue. |
| A12 | `30a7d10` | As planned. |
| A8 | `8329a5d` | Two of the three parts were already in HW (the fetch shader kept per stage, a fresh selection per warm-up record). What went in: a draw without a pixel shader gets its own fragment state, and the key version 7 with the NVIDIA rule. |
| A7 | `af66653` | As planned, with the disk entry removed through the IO queue (`DataBase::RemoveQueued`). |
| A2 | `de3503b` | As planned. |
| A5 | `afd6bda` | As planned; the module `game_fixes.cpp` with it. |
| A4 | `5f74f46` | As planned. |
| A10 | `c0852fd` | As planned. Test: `fmod-ring-test`. |
| A1 | `f0dc6cd` | The lock replaced whole, as planned. **ProcessOne is not replaced**: a hook at its entry puts the cache in by a compare-exchange on the list's allocator slot (no queue lock needed), and the cache keeps a lock of its own, so it does not rely on every caller holding the queue lock (all 14 `Append` callers do). ProcessOne, `Append` and `~CSEzWork` are checked whole all the same, and so are the allocator class's 27 methods (alloc and free where expected). 64 caches. Test: `job-pool-test` (not `game-fixes-test`). |
| Version | `28ab95a` | Launchers and LauncherBuilder 0.4.0-beta.1 (shown as v0.4.0 beta 1); engine id `hunters-workshop-5`. |
| A1, beta 2 | `ff81dc9` | First game run (2026-10-26): `job_lock` in, `job_nodes` left out. The start program's link step rewrites every `mov rax, fs:[0]` to `gs` (17,127 sites), and `Append` has two of them (+0x4c, +0xbd, both on its out-of-memory path), so its fingerprint failed. The rewritten bytes are accepted too (`0x0d88125c856cf28e`, checked again by hand). Lock, ProcessOne and `~CSEzWork` have no `fs` loads, and no red-zone site or red-zone function touches any of the four. 0.4.0-beta.2, engine id `hunters-workshop-6`. |
| A1, beta 3 | (this commit) | Beta 2's game run still left `job_nodes` out. On Windows the loader rewrites each `gs:[0]` read once more (`src/probe.c` `patch_tls_reads`): to the TEB slot's displacement, or, past slot 63 (the user's: 93), to a jump to a stub of its own whose address changes every run. So no fixed hash of Append can match. The fingerprint now reads each listed thread-pointer read as the game's `mov rax, fs:[0]` when it holds one of those forms, and checks every other byte as before (the original hash again). The log says which check failed. Checked on the real Append bytes in all four forms; `job-pool-test` covers the forms on synthetic code. 0.4.0-beta.3, engine id `hunters-workshop-7`. |

Fingerprints of game code are checked against the image the engine actually sees: the eboot
after the start program's patches **and** its link step (`fs`→`gs` loads, red-zone store
redirects, `start/prepare.cpp`) **and** the Windows loader's own rewrites (`src/probe.c`: thread-
pointer reads to the TEB slot), not the patch output alone. A1's first fingerprints missed both.

Planned tests not added: A4's and A5's callbacks, A6's case in `test_runtime.c`, and A7/A8's in the
pipeline tests. They need the full engine build (the runtime and the renderer), which can't be
made where this was written. Each change was checked by hand against the code it touches instead.
The ones added (`engine-hooks-test`, `job-pool-test`, `fmod-ring-test`, `test_patches.py`) pass on
Linux, and `engine-hooks-test` and `job-pool-test` also pass as Windows builds under Wine.

### What each one touches

| # | Layer | Files | Game sites | How it goes in |
|---|---|---|---|---|
| A1 | Game code | new `gpu/shim/engine/job_pool.cpp` | `DLAdaptiveMutex::Lock` 0x20368c0 (whole function, 0xe8 bytes) replaced; `CSEzWork::ProcessOne` 0x2032370 (0x1c2 bytes) hooked at its entry (as built; the plan replaced it); the general allocator's vtable 0x53aefa0 copied, never written | `Replace` (new, below) and `Prepare` |
| A2 | Boot patch | `start/patches.cpp`, `scripts/patches.py` | The recording producer's `lea rdi, [string]` at patch 0x1dc981f: its displacement at patch 0x1dc9822, `5d0ff702` → `2e6af702` | A write in the patch list, checked against the original bytes like the render pools |
| A3 | Renderer | `liverpool.cpp` 920, 1994, 2168 | — | `task.handle.destroy()` after each wait loop |
| A4 | Game code | new `gpu/shim/engine/game_fixes.cpp` | Runner step 0x1067f60 (17 bytes); the two runner resets 0x1067960 and 0x1067a40 (14 bytes each); give-back 0x1056990; pool 0x54b8788 | `Prepare`; the step through `Hook::original` (new, below) |
| A5 | Game code | `game_fixes.cpp` | 0xbbc3e0 (17 bytes) | `Prepare`, raising `edi` in the saved registers |
| A6 | Runtime | `src/runtime_memory.c:340` | — | One condition |
| A7 | Renderer | `vk_pipeline_common.h/.cpp`, `vk_graphics_pipeline.cpp` (~650 job, ~664 direct), `vk_compute_pipeline.cpp` (~142 job, ~158 direct), `vk_rasterizer.cpp` (2085, 2265, 2381, 2550), `vk_pipeline_cache.cpp/.h`, `vk_pipeline_serialization.cpp` | — | A failed state on the pipeline (below) |
| A8 | Renderer | `vk_pipeline_cache.cpp` (`BuildRuntimeInfo` ~330, `RefreshGraphicsStages` 886), `vk_pipeline_serialization.cpp` (31, 182, `LoadGraphicsPipeline` ~305) | — | Three loading/state changes and a key version step |
| A9 | Renderer | `tile_manager.cpp` ~343 | — | One `AccessBuffer` |
| A10 | Game code | new `gpu/shim/engine/fmod_ring.cpp` and `fmod_ring.h` | FMOD `MemPool` free 0xdab3a0 (14 bytes); FMOD's critical-section enter 0xd1acd0 and leave 0xd1acf0 on the pointer at 0x546d260; the four `SoundI` vtables 0x52d7640, 0x52d7840, 0x52d7a30, 0x52d7c30; the shared subsound's free returns to 0xdbe687 | `Prepare` |
| A11 | Runtime (AvPlayer) | `avplayer_common.h:72` | — | Lock before the size check |
| A12 | Renderer | `texture_cache.cpp` ~1223 | — | A format check in `FindDepthTarget` |

Six of the twelve (A3, A6, A9, A11, A12 and A2's single write) touch nothing the others touch. The
game-code four (A1, A4, A5, A10) share one installer. A7 and A8 share two renderer files.

### Shared pieces, added first

1. **`engine_hooks` gets two additions.** Existing hooks are unchanged.
   - `Hook::original`: the address of the displaced instructions plus their jump back, which
     `Prepare` already writes into the stub. A callback can then run the game's function itself
     (`Call(hook.original, ...)`) and return its result through the return slot. A4 needs this:
     HW's hooks only see a function's entry and argument registers, not its exits.
   - `Replace(hook, image, size, offset, expected, fn)`: the entry jumps straight to a host
     function with the game's calling convention (`sysv_abi`): no stub and no register save.
     `Commit` writes both kinds. A1 needs this. `DLAdaptiveMutex::Lock` runs for every heap, GX
     and job-queue lock, thousands of times a frame. There the stub's six pushes and eight xmm
     saves would cost more than the fix saves.
   - Tests in `tests/test_engine_hooks.cpp`: a callback running the original through
     `original`, guest-ABI code calling a replaced function, and wrong bytes refused.
2. **`gpu/shim/engine/skip_list.h`** (header only). The comma-list parser `frame_rate.cpp` uses for
   `BB_FRAME_FIXES_SKIP` moves here, so the game fixes' switch uses the same one. `frame_rate.cpp`
   keeps its behaviour and messages; `test_frame_rate.cpp:279` covers it.
3. **`gpu/shim/engine/game_fixes.cpp/.h`**: one `GameFixes::Install(image, size)`, called from
   `BbEngine::Install` (`change_appearance.cpp`) beside `RenderGuard` and `OnlineFixes`, and
   counted in its "no compatible feature" check.
   - It reads the switches, installs A4 and A5 itself, and calls `job_pool.cpp` (A1) and
     `fmod_ring.cpp` (A10).
   - `BbEngine::Install` runs whenever the executable is the verified 1.09 (`start/main.cpp`
     always sets the identity), so the fixes need no launcher setting.
   - New source files are picked up by `gpu/CMakeLists.txt`'s glob. Only the new test target is
     added there by hand.

### Overlaps and conflicts found, and how each is settled

**No two changes write the same bytes.** Every A-site was checked against:
- every line of all 122 patches in `Bloodborne.xml`, enabled or not;
- the built 60 FPS and uncapped images, which hold the start program's own writes and the
  frame-rate table;
- `frame_rate_sites.inc`, the engine's hooks, `BbGuestHooks`, `probe.c`, and the online
  module's code check (0x1c43ee0).

The relationships that remain:

- **A5 and Kyo's 60 FPS++ entry at patch 0xfbc40f.** That entry (in HW's frame-rate table, at
  every rate) turns the function's first `nBaseSamples >= 3` assert into a jump past it.
  - So in HW a count of 2 doesn't crash: it runs on. bbport's own changelog (2026-10-16) found
    that running on with 2 ends in a loop: the guest stops submitting, a black screen.
  - A5 makes the count valid before the check, so the jump is never needed. The entry stays: it is
    Kyo's list content and the table tests compare it with his list.
  - bbport's clamp raises every count to 3. But the fourth caller (patch 0x1034a4d) passes a mode
    from `[r14+0x21690]`, and mode 2 asserts the count is even and above 3, so 3 fails there.
  - HW's version raises the count to the smallest one each mode accepts: 4 in mode 2, 3 otherwise.
    It leaves anything at or above that alone.
- **A10 and Dasaav's "FMOD Crash Fix"** (off by default). Different bug, different code: channel
  setup at patch 0x1122534, and 0x11f3e4d/0x11f3eab. Both can be on.
- **A7 and A8 share `vk_pipeline_cache.cpp` and `vk_pipeline_serialization.cpp`.** They go in back
  to back, A8 first.
  - HW already has part of A8: since key version 6 it saves the generated clip-discard shader, and
    loading uses it.
  - Still missing:
    - A later shader stage without a fetch shader overwrites the vertex stage's when loading
      (line 182).
    - A failed load leaves its stages behind for the next.
    - A draw without a pixel shader decides clip-distance emulation from the previous draw's
      fragment state (line 886).
  - That last one is part of what NVIDIA saved to disk. So the key version goes 6 → 7, and on
    NVIDIA a version 6 entry without a pixel shader is left out once: it compiles again in play
    and is saved as 7. Every other version 5 and 6 entry keeps loading. This follows the rule HW
    already has for ambiguous version 5 entries. A left-out entry counts as rejected, not
    damaged, so the cache isn't cleared.
- **A7 against HW's background compiles.** Supermedo's change covers only creation on the draw's
  thread; HW also creates pipelines on compile threads.
  - A failed pipeline becomes a third state beside "compiling" and "ready": ready, but with no
    pipeline. Without it, the draw path would treat a failed pipeline as still compiling and bind
    a null handle.
  - The four draw and dispatch sites skip it.
  - The first draw that meets it removes its key from the disk cache (`PipelineCache::Forget`).
    The key was written when the pipeline was made, and by the time a draw sees the failure, that
    write has certainly happened.
  - The start's warm-up already rejects a refused entry and rebuilds the cache; unchanged.
- **A1 against HW's rule that addresses the game can see stay below 1 TiB.** The game packs
  thread handles into 40 bits (`runtime_thread.c`); HW's history has the deadlock that caused.
  - The node caches are objects the game calls through, so they go in `runtime_low_map` memory.
  - The lock compares owners the way the game's own lock does, which HW's low thread handles
    satisfy.
  - Since A1 replaces whole functions, its install checks each function's full bytes, not just
    the entry. A change inside either one would otherwise be dropped silently.
- **A4: bbport hooks the middle of functions** (the shared exit 0x1068092; the stores 0x106798a and
  0x1067a5c). HW's hooks go on function entries, so:
  - the runner step is wrapped: run the original, and on result 1 with `[runner+0x120]` still
    held, give the slot to the pool (0x1057350 returns 0x54b8788's value);
  - the two resets are hooked at entry. Both clear `[rdi+0x120]` in their first instructions, with
    rdi the runner, so this is the same point bbport hooks.
- **A2: bbhost changed the string all five producers share.** Four of them are online: two in
  `FrpgNetMan`, one in the session manager, one in the playlog.
  - The game already holds its own `algorithm=deflate;level=1` at patch 0x4d40254 (and two
    more). Pointing only the recording's `lea` at it changes 4 bytes. It leaves online payloads
    at level 9, and needs no new memory or string.
  - Level 1 is ordinary deflate that every reader inflates. It is only somewhat larger.
  - Whether the recording itself leaves the PC isn't established; the online check in the
    checklist covers it.
- **A1 and the hitch investigation.** A1 changes how long the main thread waits for the character
  workers. Get a hitch-report session before A1 and one after, so the effect is measured, not
  assumed.

### Removed or replaced

- The four `ASSERT_MSG`s on pipeline creation (A7) become the failure path.
- `BuildRuntimeInfo`'s inline clip-distance test becomes a helper, shared with the
  no-pixel-shader case (A8).
- `frame_rate.cpp`'s skip-list parser moves to `skip_list.h`.
- Nothing else is removed and no files are deleted. These all stay as they are: Kyo's 0xfbc40f
  entry, Dasaav's FMOD patch, the warm-up's rejection and rebuild, and bbhost's FMOD detector
  (`BBHOST_FMOD_CHECK`), of which only the guard is ported.

### Switches and logs

| Switch | Effect |
|---|---|
| `BB_GAME_FIXES=0` | A1, A2, A4, A5 and A10 all off |
| `BB_GAME_FIXES_SKIP=<names>` | Leaves out the named ones: `job_lock` and `job_nodes` (A1's two halves), `job_slots` (A4), `blur_samples` (A5), `fmod_ring` (A10), `deflate` (A2) |

- **Logs.** One install line names what is on, and the patch block reports A2. After that each fix
  logs its events: the first five, then at powers of two, as `render_guard.cpp` does. For
  example: a slot given back, a count raised, a sound taken out of the ring, a pipeline the
  driver refused.
- **No switch for A3, A6–A9, A11 and A12.** They correct code with nothing anyone would want back,
  like HW's other renderer fixes.

### Credits

`README.md`:
- **The bbhost row** gains the job-pool lock and node cache, the FMOD sound-ring guard and the
  recording's compression level (A1, A2, A10).
- **"Crash and controller fixes" becomes "Crash, renderer and controller fixes"**, with these
  added:
  - deadinside28 (bbport 0.6: render job slots, blur kernel, pipeline cache state: A4, A5, A8);
  - Supermedo (pipeline failures, A7);
  - DarkIzuku (tiler staging, A9);
  - bmy (bbport-mac, flexible memory, A6);
  - ayozetr and emurphy04 (AvPlayer queue, A11);
  - artemkaVG (stencil linking, from shadPS4#5118, A12);
  - the ports by ayozetr, 0xCydral and Mrsuss60.
- **A3 is upstream shadPS4**, already credited; its commit names `12c5978f`.

Each commit message names its source the way `68c04a1` did ("After ... (author)").

### Commit order

Each is built and tested before the next. Version 0.4.0 beta 1.

1. `engine_hooks`: `Hook::original` and `Replace`, with tests.
2. `skip_list.h`, with `frame_rate.cpp` moved onto it (no change in behaviour).
3. A3, indirect-buffer tasks.
4. A6, flexible-memory split.
5. A9, tiler staging read.
6. A11, AvPlayer queue.
7. A12, stencil linking.
8. A8, pipeline cache state, with key version 7.
9. A7, pipeline failures.
10. A2, the recording's compression: `patches.cpp`, `patches.py`, tests, and the baseline images
    updated by those 4 bytes.
11. `game_fixes.cpp` with A5, blur kernel samples.
12. A4, render job slots.
13. A10, FMOD sound ring.
14. A1, job lock and node cache.
15. This document: the items marked done.

The independent one-file fixes come first, each alone so any of them can be reverted or bisected.
A8 comes before A7 (same files). The game-code fixes come in rising risk, with A1 last: it's the
largest, and the one to measure.

### Tests

Local, no CI. Linux builds of the tests, plus the Windows cross-build under Wine where a test
touches Windows code.

| Item | Test |
|---|---|
| Shared | `engine-hooks-test` (the additions); `frame-rate-test` (the moved parser) |
| A1 | New `game-fixes-test`: our lock against a model of the game's unlock (0x2036a40's transitions) on one word: mutual exclusion and the hand-over to a sleeper, many threads. The node cache's alloc, free and pool end through a stand-in vtable. |
| A2 | `test_patches.py`: written only over the expected `lea`; the verified executable has it; `patches.cpp` and `patches.py` give the same bytes |
| A4 | A synthetic runner through `Hook::original`: result 1 with a slot held gives it back, result 0 doesn't |
| A5 | The callback: mode 0 and 1 → at least 3, mode 2 → at least 4, larger counts untouched |
| A6 | `test_runtime.c`: a flexible mapping split by a partial unmap frees its own pages |
| A7, A8 | `test_pipeline_cache_blob` / `test_pipeline_archive`: a later stage without a fetch shader keeps the first one's; on NVIDIA a version 6 entry without a pixel shader is left out, other version 6 entries and version 7 load |
| A10 | `fmod_ring.h`'s helpers on a synthetic ring: linked, unlink, the refusing shared subsound |
| A3, A9, A11, A12 | Build plus the existing tests |

In-game checklist, for when you test:
- Laurence's cinematic at 60 and 120 (A4).
- Fishing Hamlet to Rom's lake (A10).
- Central Yharnam's lamp with `BB_FRAME_STATS` and `BB_HITCH_REPORT_MS=40`, before and after A1.
- A long session, for the recording every ~50 s (A2).
- An online session with a summon (A2).
- Motion blur on at 60 and 120: the log says if a count was raised (A5).
- Depth-only and shadow passes on NVIDIA (A8).
- No code 23 on AMD (A7).
- A movie played and skipped (A11).

## B. Above 30 FPS (0.3.0 territory)

HW already carries 161 of bbhost's 166 frame-rate sites (every redirected load, the physics, AI,
clip-drive and targeting steps, the pacer targets). These are what bbhost found beyond them.
B13, B15, B16 and B17 went in for 0.4.0 beta 5, and B18 for 0.4.1 beta 1 with a switch for its A/B test (the table above).

**B14 parked (2026-10-26).** bbhost's effects work is its `decomp/sfx/step_gate.cpp` (1,344 lines) and
the schedule in `engine/frame_rate.cpp` (`kSfxStepLoad` 0x2377d4a, `g_sfx_carry`). Effects move by time
(right at any rate) and count part of their lives in updates (the light module's flicker phase +0x30
and blend +0x110, a node's state edges, emissions, conditions, controllers), which run twice as often
at 60. The options weighed:

- **A, whole steps:** the effects in whole 1/30 s, the frames between running only each node's
  containers (sub_2c4ff30 and the light module sub_131e660 held). Counts right, Ebrietas' 1251721 ends
  without the data workaround, but the effects' own movement then changes 30 times a second. Medium.
- **B, bbhost's smooth steps:** counts at 30 and movement by the frame's time, through about 30
  effect classes converted one by one, the ribbons, GPU particle records and the wind. Several times A.
- **C, the flicker alone:** the light module's update (sub_131e660) with its phase rate (+0x80) as 0
  and its blend mode (+0x38, +0x3c) as keep on the frames between whole 1/30 s, all put back, a
  pre-roll's call left alone: lamps and candles flicker 30 steps a second, as on the PS4; nothing else
  changes. Small. The recommended one, if this is picked up.
- **D, nothing.** Chosen for now: the Ebrietas data fix covers the effect that never ended.

**C tried in 0.4.2 beta 1 and taken back out (2026-10-29):** side by side its flicker could not be told from the game's own at 60, so it did not earn a hook on a game function. D stands, and B14 is closed.

| # | Fix | What it fixes | Where | Source and credit |
|---|---|---|---|---|
| B13 | **Grass and tree wind** | At 30 the game steps the wind 1/60 s a frame (half its pace, with the GX device's +0x100 at 1). HW points that step at the frame's time, so at 60+ the wind runs twice the PS4's pace, and its force, weighted by the same step, bends less and twitches with each frame's length. Fix: the wind's step at half the task step; the force at the authored 1/60. | `sub_2713870`; HW `FRAME_SITE(Grass, 0x2713d8d)`; bbhost's force sites patch 0x27149ea and 0x2714b26 | droogie, bbhost `wind_pace.h`, `frame_rate.cpp` (`b2818b4`, `5cee912`) |
| B14 | **Effects step** | Effects count part of their lives in updates. With the frame's time as their step, lamp and candle flicker runs at the frame rate rather than 30 a second, and some effects never end (Ebrietas' 1251721 in 1 of 6 attacks without the gate). Fix: whole 1/30 s steps with the rest carried, and the frames between holding what effects count. Would also let HW drop the Ebrietas data workaround (Nexus mod 41's trail). Largest item here. | `SprjSfx::Update` patch 0x2377bd0, its step load 0x2377d4a | droogie, bbhost `sfx_schedule.h`, `decomp/sfx/step_gate.cpp` (`6f735f4`, `9c13c5a`, `5986a4b`, `e4fbb44`) |
| B15 | Bloodletter L2 | The transformed L2's self-hit (a 0.4 m sphere live for one 30 FPS frame) misses the hand's ragdoll capsule when it trails the pose by a fraction of a frame, so the frenzy aura never comes above 30. Fix: radius 0.6 at the converted rates. | `AtkParam_Pc` 2900931 `hit0_Radius` 0.4 → 0.6 | droogie, bbhost `6f735f4` |
| B16 | Keyboard sprint turns | Sprint drops when changing direction on the keyboard. Fix: the keyboard stick turns at most ~30° per read, a half turn passes through forward, and a release keeps the direction 60 ms. | `gpu/shim/input/bindings.cpp`, `src/runtime_pad.c` | Supermedo `4b930b1`; ayozetr `523c88e` (cassioKenji's measurements); Lantern PR #67 (ikeeasianboy) |
| B17 | DS4 diagonals | Some pads report diagonals past the stick's round gate (up to ~1.41× one axis), so the camera turns 2–3× faster there, worse at high FPS. Fix: bring a stick past the round gate back onto it. | `src/runtime_pad.c` | ayozetr `2b8d9e1` (issue #7) |
| B18 | PS4 float state on game threads | Game threads start with Windows' MXCSR (0x1f80) instead of the PS4's (0x9fc0: flush-to-zero, denormals-are-zero) and x87 control word 0x037f. Needs testing before it ships. | Guest thread start (`src/runtime_thread.c`) | StevenMiller123 and red-prig, shadPS4#3568; via ayozetr `caa9b4b` |

## C. Test first, or optional

- **Laurence's cinematic at 120 FPS.** bbport found the render resource cache (the same allocator
  as HW's GX range allocator: 0x2169c30 → 0x26d0d30 → the range allocator and its collector
  0x26aa860, image offsets) out of room at high frame rates: 20–40 KiB allocations fail, models go
  missing, 320 MiB wasn't enough, 512 MiB of port memory was. HW sets 288 MiB (beta 8, for
  Ebrietas), up to 416 with `BB_GX_REGION_MB`. Test the cinematic at 120 before changing anything.
- **"Bad PM4" at save load.** The game's main and render threads write one command buffer at once
  while an effect initialises (image offset 0xebb960; its three clears at 0xebbc86, 0xebbca4,
  0xebbcc2). bbport does those clears itself and copies command buffers at submit by default. HW
  has the copy (`BB_COPY_GPU_BUFFERS`) off by default, and a zero-dword skip. Source: deadinside28,
  bbport 0.6 (`bbport_fx_init_clear.h`).
- **Third resource post-processor in play** (experimental): patch 0x18281a8 stores 2 instead of 0,
  so the resource system's third loader thread keeps running in play. droogie, bbhost
  `patches/ingame-post-processors.toml`.
- **Uncapped only:** a flip queue that never empties can keep the precise-pacing loop from
  returning for vblank events (`driver.cpp` ~1296, the `wait_until` predicate is true past its
  deadline). DarkIzuku `58ee8f5`.
- **Bugs in community patches, found when Lantern remade them.** HW keeps the authors' patches and
  credits and would fix the bytes:
  - *Restore Debug Camera* (Lance McDonald) writes its code over the game function at patch
    0x1de3330, which 0x1d62765 and 0x1dbe4f2 call.
  - *Enemy Control* (stagvant) drops the controller's null check.
  - *Enable Screen Space Reflections* puts its 2.0 at patch 0x4632f8, inside a clock function
    (0x4632d0) that nothing calls: harmless, but writes into code.
  - *Performance Patch* never applies in HW: about 40 of its writes come from another game version
    and land mid-instruction, so HW's checks refuse it.
- **Lens and camera tuning** (bbhost's "Original" camera, the game's own camera converted from 30
  instead of Kyo's retune): already in HW, from Lantern 0.9.37. The launcher's Graphics > Follow camera,
  `camera_tuning=original` (the default) or `fps++` (`start/patches.cpp`, `FrameRateFixes`); the lens
  filter's rate is the frame-rate fixes' Lens group.

## D. Not recommended, not applicable, or already in HW

Not recommended:

- ayozetr's indirect-draw guard (`25a8ca7`): a compute pass before each indirect draw, splitting
  render passes. The same family as the particle cap.
- Supermedo's translation of instructions old CPUs lack (`a87b3b4`, Ivy Bridge and older): large,
  and not this hardware.

Not applicable to HW:

- bbport 0.6's fixes to its own new renderer and memory layer: push constants under the occlusion
  pass, the present stage wait, light-shaft tiling, the 4K frame buffer over the bus, FPS aging,
  the write-trap race, the hot-effects SSAO and SSR bugs.
- Supermedo's fixes for regressions from its shadPS4 0.20.1 update (bindings past 40, index buffer
  size, cache versions, the access memo). HW never took that update.
- VRAM fixes built on bbport 0.51's memory model (ayozetr `ddce44b`, `dac2621`, `5a88481`,
  `af2cc9f`, `7bacb0c`): HW's model was redesigned. Revisit only on VRAM-full reports.
- Supermedo's mouse-camera memory queries (`ee2bec4`): HW's mouse camera makes none.
- ayozetr's pad neutral measurement (`746d623`): HW doesn't measure one.
- Polaris end-of-pipe labels (`bb49ffe`): RADV only, Linux later.
- bbhost's renderer items (Forbidden Woods tessellation hang, the black world, its VRAM leak).

Already in HW: thread handles below 40 bits, guest in/out arguments outside the region lock,
write-fault texture skip, the drain race, stereo level, audio priority, the limiter guard and
pacing drift (`PrecisePacing::Cadence`), early Z, 1 MiB render pools, PM4 type-0 skip and resync,
the Intel 12th-gen red zone, occlusion counts in order, the SRT walker's fault handling, stale
vertex inputs, indexed indirect index buffers, the out-of-order flip IRQ, the Windows commit limit
(`BB_POOL_LAZY`), and the online certificate trust and kept-alive retry.

## bbport 0.7, reviewed (2026-10-29)

deadinside28/bloodborne_pc 0.7 (`0fc2921`, one commit over 0.6, 962 files): its notes' every fix checked against
HW's code and the 1.09 eboot. Taken for 0.4.3 beta 1: the two rows above. Already in HW: play time above 30 (HW's
own clock carries the milliseconds since 0.3), the per-draw `jthread::joinable` (HW's bind helper has bbport's
flag), the occlusion intervals (HW's real counts read results without waits, and are an Experimental option), the
NVIDIA power profile (HW's Reflex boost), FIFO Relaxed (the launcher's), the GX node guard of 0.6's #162 (HW's 1 MiB
render pools), the blur kernel and the job slots (0.4.0). Optional, not taken: the reactive mask's size guard (the
mask is off by default), the HUD icon grid (bbport's UI path; HW's differs; only with a scaled scene), the invalid
character id before a save (its cause unknown to bbport too), the compute copy into a new image (measure first).
Not for HW: bbport's memory model (its heap growth, the vertex explosions at heap pieces, the shrink race, the FPS
drop after a save switch from its layer's marks), its Windows build's own fixes (BC6H storage, the opening movie),
Wayland and the Steam Deck, the ring dedup and the copy shader filter (its buffer code). FSR 3.1 out of protected
memory (RADV handhelds): with the Linux build. Features noted for later: SMAA and FXAA, FSR 1, GT-VBAO, Anti-Lag 2,
the shader recipe pack (to be converted to HW's cache inputs), video skipping, invert mouse X.

## Lantern 0.9.38, reviewed (2026-10-29)

Taken for 0.4.1 beta 2: the table above. Already in HW: the native frame rate (HW's own table since 0.3.0;
every site Lantern's `NATIVE_FRAME_RATE.md` lists is in `frame_rate_sites.inc`, the 90 list's effect step
and the uncapped list's step in the start's patches), and the cutscene freeze fix (`job_slots.cpp`: HW's
A4 hooks the same runner, its destroy and its reset). Not taken for now: FSR 4 from the AMD driver
(RX 9000, Lantern still testing it; port if AMD players ask), the memory retry limit (the PC memory model
only, off by default in HW), the game status and Discord presence, the Extra Messenger Skins patch
(distpade; optional, cosmetic), muting while the window is in the background, the PC menus off switch, the
cheats' online message, the Wayland scale hint (Linux later), the Russian PC menus (GHusr-y5; HW's menu
builder has changed too much to copy) and the Help & FAQ redesign.

## Smoothness leads (for the smoothness phase)

- The hitch report's findings (above), first.
- A1 (job convoy) and A2 (compression), as frame-time work rather than bug fixes.
- eFMann's notes on stutters after loads (`documents/bb-dlc-judder-investigation.md`, `c57a2b81`):
  64 MiB VMA blocks instead of 256 (less VRAM held unused), 1 GiB of upload memory kept ready,
  write-backs of GPU-drawn images paced (128 MiB/s), the old area's textures freed before an area
  load, and new textures spread over 6 s after a load (64 MiB a frame). Its unsolved DLC judder had
  ~25 more barrier flushes a frame than normal frames. eFMann is upstream-shadPS4 based, so each is
  an idea to port, not a patch.

## Beta 9 audit

Apart from the two switches HW keeps off (the fixed-refresh frame lock, the particle cap), nothing
Beta 9 changed alters Windows defaults in a way that could regress:

- Fixes: the loading screen's pace restored after travel, Ebrietas' trail effect (from the
  player's own files), the startup commit-limit fix (`bb-start` makes the pool section).
- Off unless enabled: chunk buffers (`BB_CHUNK_BUFFERS`), the lazy pool (`BB_POOL_LAZY`).
- Linux only: engine features turned on there.
- Launcher and catalogs: per-vendor shader catalogs, frame generation with HDR, the sidebar,
  updates, the app icon.

## Sources examined

| Project | Up to | Notes |
|---|---|---|
| Lantern Engine (Awaken) | stable `d901fbd`, online branch `918505b`, PRs #66–#78 | Input fixes ported; the online fixes are already in HW |
| bbport (deadinside28) | 0.6, `8185b88` | Its own renderer since 0.6; game-level fixes portable |
| ayozetr (bbport Linux fork) | `23ce633` | Same shadPS4 layout as HW: direct ports |
| Supermedo (bbport Windows fork) | `5adea35` | Its later fixes are mostly for its shadPS4 0.20.1 update |
| bbhost (droogie) | v0.2.19, `56b868e` | Independent engine; its game-level fixes and frame-rate work are portable |
| DarkIzuku | `2f9f1c0` | Lantern's own starting point |
| eFMann (shadPS4 DLSS/FSR fork) | `36e2116a` | Upstream-shadPS4 based; ideas rather than patches |

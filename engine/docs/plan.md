# Hunter's Workshop: the plan

What comes next, the rules and decisions it works under, and every open item. Updated 2026-11-09.

- **Where it stands (2026-11-24):** 0.4.6 is in beta: the memory model release
  ([memory-model.md](memory-model.md)). Betas 1 to 13 moved the engine's caches onto the game's own
  calls (images owned by the game's textures, surfaces never read from memory, the game's buffers
  watched by their calls); beta 14 (engine ids 100-101) is The Hunter's Dream's online update;
  beta 15 (102) moved work into the loading screen, its first runs read on 2026-11-24. Two of the
  five rules 0.4.6 is to end with are not met yet (textures still watched by page protection where
  their calls already tell; shaders translated at a draw on the GPU thread): betas 16 and 17, then
  the release candidate. [The track to the release](#the-track-to-the-046-release) below is the
  order of work. 0.4.5 was released on 2026-11-07 (`a6b34b8`, engine id 84); 0.4.4 on 2026-11-03
  (`cbec5a2`, engine id 64).
- **Where the work goes:** the branch `claude/awesome-gates-cjba5e`.
- **How nothing gets lost:** every open item, whatever its size or source, is a row of
  [the register](#the-register-every-open-item) below, with where it is scheduled. See the ground
  rule "Nothing is dropped silently".

The other documents:

- [memory-model.md](memory-model.md): 0.4.6's design, the research behind it and its roadmap, beta
  by beta, with what each must show before the next.
- [performance.md](performance.md): the findings behind the performance work after 0.4.4.
- [changelog.md](changelog.md): every change since Hunter's Workshop (HW) began, with its
  measurements and credits.
- [community-fixes.md](community-fixes.md): what the other Bloodborne PC projects fixed, vetted
  against HW.
- [engine-map.md](engine-map.md): how the engine works.
- [archive/](archive/README.md): finished plans and closed investigations. The plan that led to
  0.4.4 is [archive/plan-0.4.4.md](archive/plan-0.4.4.md); the one to 0.4.5 and 0.4.6 beta 7,
  [archive/plan-0.4.5.md](archive/plan-0.4.5.md).

## Ground rules

- **No merge with Lantern Engine.** Its 0.9.37 stable regressed from beta 8 (the fixed-refresh
  frame lock and the particle cap, both off in HW), and it removed community patches and their
  credits.
- **Community patches and their credits stay.** A fix taken from another project is credited in
  `README.md` and in its commit.
- **AMD's and NVIDIA's own documents** (the FSR SDK, the DLSS Programming Guide) are credited as
  such, not to a project that applied them first. The tester, 2026-10-31: "this is AMD spec not
  theirs", "also nVidia spec and not theirs".
- **AMD's and NVIDIA's libraries are used as shipped,** never disassembled (the NVIDIA RTX SDKs
  licence, §4a).
- **The particle cap and the fixed-refresh frame lock stay off** (`BB_INDIRECT_CLAMP`,
  `BB_VBLANK_PACING`).
  - The frame lock with a "Match display" frame rate is pinned for a later version: the
    display's own rate, with its flips on the display's vblanks.
  - The tester found Lantern's lock not smooth; until then, there is no native-rate option.
- **Root causes, for every vendor.** A fix finds the cause and works on every GPU vendor; a
  workaround for one card is the last resort.
- **The launcher's pages and settings:** changes are proposed to the tester before they are
  built.
- **One step per beta, or several when one log can tell them apart** (the tester, 2026-11-08):
  each part then has its own switch in Extra variables that brings back the old behaviour. Each
  beta has its own engine id and is measured on the tester's PC against the one before.
- **Nothing is dropped silently** (the tester, 2026-11-08: "so that we don't lose on or forget any
  steps"). Every finding, deferral, idea, measurement still to make and tester report becomes a
  row of the register below in the commit that meets it, with where it is scheduled. A row leaves
  only as done (with its beta or commit) or dropped (with the tester's agreement and the reason).
  Before a beta is built, its section lists the rows it takes; its commit updates them. An item
  that waits on a log says which log and which line.
- **The engine follows the game's calls, not its memory** (the tester, 2026-11-08, on leaving the
  generic emulator model: "in order to have a fulfilling experience and optimal engine"). A change
  that would guard against the game where its calls already say what happens needs a reason.
- **Linux waits until Windows is done.**

Addresses in these documents: **patch** addresses are the ones `Bloodborne.xml`,
`frame_rate_sites.inc`, bbhost and Binary Ninja use (image offset + 0x400000). **Image offsets**
are what bbport's `ImageBase + x`, HW's `Prepare()` calls and the sampler's `+0x...` print. Each
address says which it is.

## Decisions

The decisions that still stand. How each was reached is in [archive/plan-0.4.4.md](archive/plan-0.4.4.md).

| Topic | Decision |
|---|---|
| FSR | AMD FSR SDK 2.4 (FidelityFX-SDK `v2.4.0`, 28 October 2026) on Windows: AMD's signed `amd_fidelityfx_loader_vk.dll` and `amd_fidelityfx_upscaler_vk.dll` with AMD's licence notice. Every unofficial FSR path was removed (0.4.4 beta 27). |
| DLSS | The driver's NGX core, started with the shared application id 231313132 (`Init_Ext2`). No Project ID (the tester's call, 2026-11-01): the core's entry point that takes one is declared nowhere public, and learning it means disassembling NVIDIA's libraries, which their licence forbids (§4a). Not Streamline: with its core loaded, NVIDIA's Vulkan driver presents through DXGI and holds V-Sync On (latest frame) to two frames a refresh. NVIDIA's NGX loader library is not linked: the DLSS SDK licence (§2e) forbids use that puts the SDK under an open-source licence (HW is GPL). |
| When AMD's or NVIDIA's runtime can't start | TAA, with a log line and a note in the game. |
| Frame generation | Not offered, and its code removed in 0.4.4 beta 28 with Streamline's and Reflex's (the tester's calls, 2026-11-01 and 02). If it returns, it is AMD's on AMD's swapchain, not built on that code. |
| NVIDIA Reflex | Not offered. If it returns, it is a design of its own on `VK_NV_low_latency2` directly (the sleep before the frame's work, markers with present IDs), measured against the 120 FPS cap first. |
| OptiScaler | Set aside: the compatibility mode (`vk_optiscaler.h`) stays as it is, untested. Re-checked after the performance work. |
| The upscaler at outputs other than 1080p | It stays on the game's finished frame (the tester's call, 2026-11-01). Taking it before tone mapping there would run the game's post-processing at the output size, a renderer split the game was not built for. |
| Live resolution change | Off or On only; Off is recommended and the default (the tester's call, 2026-11-01). |
| The follow camera | Fixed in HW's engine (0.4.4 beta 24): every per-frame weight converted each frame with the frame timer. FPS++ is that base plus only what Kyo changed. bbhost's weight list is a cross-check only. |
| PC memory model | Removed (0.4.4 beta 28). The game's data lives in VRAM; the GX region is read in place. |
| Online connections | The engine reports a connection to the game once the server gives the player's address, and a failure when the server says the player left or timed out. That is how bbhost and Lantern 0.9.39 do it; nothing to change (checked 2026-11-03). |
| The memory model (0.4.6) | The engine's caches follow the game's own calls (GX, YEBIS, Scaleform): images and buffers belong to the resources the game creates, GPU-owned surfaces never read the game's memory, and writes are learned from the calls; pages are watched only where no call tells (the tester, 2026-11-08). 0.4.6 is that release, depth buffers, the game's buffers and the shaders built ahead included. The roadmap: [memory-model.md](memory-model.md). |
| The VRAM manager | Off by default since 0.4.6 beta 5 (the tester's call): the game's calls decide. `BB_VRAM_MANAGER=1`, Use less VRAM and safe mode turn it on, its rules unchanged. |
| The game's GX calls straight to the host GPU | Not planned. Decided after 0.4.6 by profiling how much frame time replaying the command stream takes; done only if that share is worth it, and then one part at a time ([memory-model.md](memory-model.md), "After 0.4.6"). |
| Test PC | RX 9070 XT since 2026-11-10 (RTX 3070 Ti before), i5-14600KF, 60 Hz display (the 480 Hz vblank in beta 15's logs is the Uncapped preset, which the tester uses for the heavy test scene); Windows HDR is off. HDR-output items are checked in the code, not in the tester's runs. |

## Now: 0.4.6, the memory model release

The detail of every beta (its parts, switches, test runs and what it must show) is in
[memory-model.md](memory-model.md). The numbers after beta 8 may shift with what each log shows;
the order of the work does not change without the tester. Renumbered 2026-11-08 after the design
check: beta 10 measures the owner rule, beta 11 acts, and depth, buffers and shaders moved on one;
then the game's buffers went before depth (the tester, with beta 11): buffers 12, depth 13. On
2026-11-15 The Hunter's Dream's online update took beta 14 (the tester's call): the memory work's beta
14 is 15.

| Beta | What it does | State |
|---|---|---|
| 1 to 7 | The caches follow the game's create, destroy and release calls: textures built in the game's create call, nothing freed on a release, YEBIS's and Scaleform's textures followed, YEBIS's same-memory targets kept across its rebuilds | Done; beta 7's log read 2026-11-08 |
| 8 | GPU-owned surfaces start from their predecessor or empty, never from memory, and take no hashes; dead targets' images reused by new ones of their shape; write-watching replaced by the calls for game textures (off, counted); the logging for betas 9 to 13 | Built 2026-11-08 (engine id 92). Run A read: the rebuild stalls gone (frames in play 28.5 ms or less but one of 41.6). Run B read: no hitch, frames in play 33 ms or less; the tester's look at the picture decides part 4 |
| 9 | The game's targets unwatched by default; YEBIS's and Scaleform's write calls hooked; the GPU's earlier writes to a surface's memory measured (`BB_TAKE_EARLIER_WRITES=0` leaves them out); the writer of textures made with their data named; the design note for the owner rule | Built 2026-11-08 (engine id 93). The tester's run read: a clean session, frames in play 28.2 ms or less but two after loads; the writers named. The design check revised the order (beta 10 measures first) and the tester agreed |
| 10 | Beta 9's hole closed (a texture made with its data marks the images over it written); the census of live textures made over each other; the owner rule counted alongside today's rules without acting; textures made with their data unwatched | Built 2026-11-08 (engine id 94); waits on the tester's run |
| 11 | Images belong to their textures: an image of a game's texture retired with it (unless a draw used it since), the old guesses kept for the rest; any texture made without data takes the image of its shape it is made over (`BB_IMAGE_OWNERS=0` keeps the old rules) | Built 2026-11-08 (engine id 96). The tester's run read: clean, smooth, textures about 8-11% lower in VRAM and more handed back after loads; the driver's total set by the game's buffers |
| 12 | The game's buffers by the calls: the pages of the game's GX buffers holding no GPU data watched by their creations and releases, not by protection; every GX resource's memory handed back at its destroy; the engine's own CPU stores told; a sampling net for writes no call tells (`BB_WATCH_GAME_BUFFERS=1` watches as before) | Built 2026-11-09 (engine ids 97-98). The tester's run read: clean, write faults down 76%, the net found no untold write; read in full: each minute's report makes a 26-30 ms frame (since beta 5), and the extra compiles were the GPU cache recompiled for a file it need not hash |
| 13 | Beta 12's follow-ups: the labels' check, write faults counted by what watched their page, loading lines naming their area (the depth items, the minute report's move and the GPU cache's identity dropped by the tester, 2026-11-09) | Built 2026-11-09 (engine id 99). The tester's run read: clean; a label over a VRAM copy about every 25 s (fixed), the create calls' faults explained, the frames after loads broken down; the second m34 entry not in the route |
| 14 | The Hunter's Dream's 0.2.31 update: NpSignaling ported from bbhost v0.2.35 (the games' greeting), Matching2 on it, the P2P port (greetings, 64 KB, pieces over 1,200 bytes), the NpId compares, character names; bbhost's sign-in check and one poll in flight; players' addresses masked in the log; a line per session; tested against bbhost's own code (`online-update.md`) | Designed 2026-11-15. The tester (2026-11-16): parts 1-5 agreed, 6-9 to discuss, no 0.4.5 hotfix, no launcher sign-in display. Parts 1-5 built 2026-11-16 (engine id 100), their tests passing here (the handshake the same as bbhost's own code); parts 6-9 chosen by the tester and built 2026-11-21 (engine id 101): the game told it is signed out while the server refuses the sign-in, one poll out, addresses masked in the log, a line per session. Waits on the tester's runs with updated players |
| 15 | Work moved into the loading screen (the memory work's beta 14 until 2026-11-15): compile bursts on spare processor time (burst threads at idle priority, raised while a frame waits for theirs, at most one compile thread per physical core, on the efficiency cores where there are any, none under Wine by default; `BB_PIPELINE_BURST`, `BB_CPU_SETS`; the start-up build on all logical processors but one, at most 16), the game's buffers made during a loading screen uploaded during it (`BB_LOAD_UPLOAD`), surfaces' memory made during the loading screen by learned shape (`BB_READY_SURFACES`), the create calls telling the engine before their copy (`BB_COPY_NOTICE`), one line per load on its first frames. Pipelines for areas the PC has never drawn are not buildable from the game's calls: that needs keys from elsewhere (the catalog row) | Designed 2026-11-10 (`memory-model.md`). Discussed with the tester 2026-11-22: all five parts kept (part 3 for cards where making surfaces is slow (the 3070 Ti), though the tester is on the 9070 XT); part 1 redesigned around the game's threads and processor types, CPU sets for HW's burst threads agreed; 2026-11-23: no measuring of spare capacity (the scheduler does it), the system told what the burst threads are instead. Built 2026-11-23 (engine id 102): all five parts, each with its switch, all four off in safe mode |
| 16 | Textures follow their calls (the memory model's rule 3): YEBIS's textures made with their data unwatched (the faults are YEBIS's shared vertex buffer, packed beside its utility textures, written for every post-processing pass), Scaleform's planes unwatched (told by their unmap), a texture net for writes no call tells, what still faults named (`BB_WATCH_YEBIS_TEXTURES`, `BB_WATCH_SCALEFORM`, `BB_TEXTURE_NET`) | Designed 2026-11-24 (`memory-model.md`); the tester's go-ahead the same day. Built 2026-11-24 (engine id 103): all four parts, three with a switch (part 4 counts only); safe mode watches every image already (`BB_WATCH_GAME_TEXTURES=1`). The first run (`5dbb0102`): the YEBIS faults unchanged (its utility textures made without data stayed watched), the net finding nothing, beta 15's parts working; engine id 104 the same day: every YEBIS texture left to the calls, the net in two lanes, the hand-over fixed, the remaining watchers named |
| 17 | Shaders and pipelines (rule 5): shader translation off the GPU thread; the GPU cache kept across engine updates that change nothing it stores; the catalog decided | To research and design (the track, phase C) |
| Release | Switches settled with the tester, old paths agreed on removed, a small-card run, the docs, a release-candidate run | Planned (the track, phase D) |

### The track to the 0.4.6 release

Agreed order of work from 2026-11-24 (the tester: "I need a proper track for how we're going to be
working on finalizing this release"). Each phase ends at its gate, read from the tester's logs.

**How each beta goes:** research (code, logs, the 1.09 executable) → the design written into
`memory-model.md` with the register rows it takes → the tester's review and go-ahead → built with a
switch per part, native tests that fail on deliberate breaks, syntax checks on clang, g++ and MinGW
→ pushed with its engine id → the tester's named runs → the logs read, every finding a register row
→ the gate decided with the tester. Nothing in the launcher's pages or settings without asking first.

**Phase A: beta 15 closed (runs only, nothing to build; the tester runs them with beta 16's build,
2026-11-24: beta 15's switches stay).**

1. The usual route with a second load (a lantern, or the Dream and back): part 3 makes and hands out
   surfaces (`surfaces ready N, taken M` above 0 in the second load's line).
2. The same route with `BB_LOAD_UPLOAD=0 BB_COPY_NOTICE=0`: whether the 2 s first in-game frame
   comes back (which part removed it), and whether YEBIS's write faults drop (part 4's share).
3. Online with other players (beta 14 untested there): summoned, and joining another world (the
   "Checks not made" item).
4. AMD FSR with AMD's checks on (`BB_AMD_FSR_DEBUG=1`; the tester, 2026-11-24): any
   `Upscaler: AMD FSR warning` line is a finding. Then a short look with `BB_AMD_FSR_DEBUG_VIEW=1`
   (AMD's debug view in place of the picture; works from the build after `f275ad2`). **Done
   2026-11-25:** the checks on in `27b942f1` with no finding under FSR 4.1.2; the menu's "Show AMD
   FSR's own view" turned on and off in `c2979378`: FSR made again each way (81.0 MiB with the view
   allowed, 76.5 without; ready in 0.0-0.1 s), its cost unchanged (0.50 ms a frame); one frame of
   121 ms in the 5 s the view was turned on, none when it was turned off. The view itself (the tester's
   screenshots, FSR 4.1.2 at Native AA, standing still): motion vectors dark (zero, no jitter in
   them); the history blend grey on sharp still edges in both scenes, broad purple patches only on
   the walls and pedestals around candles and lamps (light that flickers every frame), and in a dark
   room only scattered specks that move from frame to frame (dust particles, which carry no motion
   vectors, and noise in dark lighting). As AMD describes it working.

Gate: no new problem; part 3 seen working; the A/B read.

**Phase B: beta 16, textures follow their calls (rule 3).**

- Why: textures are the last of the game's resources still watched by page protection. Its
  symptom in beta 15's runs: one writer (`+0xc21ef2`) faulting 8-12 times a second, into small
  tables (256x1, 16x16, 1x1) and the data beside them, each fault taking the texture cache's lock on
  the game's thread.
- Found 2026-11-24 in the 1.09 executable: the writes are YEBIS's quad vertices, written into its
  shared vertex buffer for every post-processing pass; that buffer is packed beside YEBIS's small
  utility textures (dither, spectrum, film grain, fade), whose images watch the shared pages. Those
  textures are made with their data and written only through YEBIS's own calls.
- Built 2026-11-24 (engine id 103; `memory-model.md`, beta 16): YEBIS's textures made with their
  data unwatched (`BB_WATCH_YEBIS_TEXTURES=1` watches them), Scaleform's planes unwatched
  (`BB_WATCH_SCALEFORM=1`), a texture net hashing the unwatched textures the CPU fills for any write
  no call tells (`BB_TEXTURE_NET=0` turns it off), and what still faults named in a minute line.
- Gate: image write faults "beside a live resource" and on Scaleform's planes near zero, the net
  finding nothing, no visual difference (YEBIS's dither, grain, spectrum and fade, the UI), frame
  times unchanged or better.

**Beta 16's last part, designed 2026-11-25 for the tester's review, built the same day (engine id `hunters-workshop-106`): the remaining faults' step 1, sharpening, the reactive mask's line.**

- *Where the remaining faults come from (read 2026-11-25).* 146-343 write faults a minute are left,
  almost all on images over memory no resource the engine follows holds, in YEBIS's heap: small
  RGBA8 textures (1x1 to 256x256), a 64x1/32x1/16x1 set and a 64x64-to-4x4 R8 chain, each its own
  allocation (a 0x100 gap between them). Writers beside them: YEBIS's quads on one page and the
  game's `memcpy` from `0x2929780` (reached through a function pointer). Ruled out by reading the
  1.09 executable: YEBIS's device has 1D and 3D texture creators beside its 2D one (vtable slots
  `+0x1e0`, `+0x1e8`, `+0x1f0`, thunks `0x2a39320/40/60`), but no YEBIS code calls the 1D or 3D slot
  of the device, and every YEBIS texture comes through the 2D creator, which the engine follows. All
  three take their memory from one allocator, `0x11e6810`, which six other functions call too, one
  of them (`0x11d7200`) behind the device's method wrappers: likely YEBIS's buffers. Most likely
  these images are YEBIS buffers or other allocations sampled as textures; not proven.
- *Step 1, one build, nothing changed in behaviour:* the allocator hooked (byte-checked, as the
  creators are), each allocation's range and the YEBIS function that asked for it kept; the log's
  named watchers then say which allocation an image lies in and who made it. YEBIS's 1D and 3D
  creators counted (expected 0). One run in the same places names the owner.
  *Built:* both allocators hooked, `0x11e6810` and `0x11e6700` (its other one, for memory given
  data; the same 15-byte prologue checked; `BB_YEBIS_ALLOCS=0`: not hooked; where they differ from
  1.09 only they are left out). Each allocation's range, time and the return address that asked
  for it kept (the newest over an older range replaces it; up to 200,000). A watched image over no
  game resource that faults is named with the allocation under it ("in YEBIS's allocation at ...
  asked for by +..., ... s ago", or "in no allocation of YEBIS's known"), and a new minute line,
  "GX memory, YEBIS's allocations this minute", gives the allocations by who asked (the three
  texture creators named: each allocates through a helper only it calls, `0x11deca0`, `0x11df840`,
  `0x11e1960`, read from the function table; and `0x11d7200`, behind the device's wrappers), the 1D
  and 3D creators' counts, and the faults by who asked for the memory under them. Both allocators
  are called from 13 functions in all.
- *Step 2, the fix, designed from step 1's answer:* that creator followed as YEBIS's textures are
  (its memory, size and release), its images left to the calls that write them (YEBIS's Unmap,
  hooked since beta 9) and checked by the texture net's first lane. Gate: write faults on images
  near zero.
- *Sharpening* (`upscaler.md`, "Sharpening"): one meaning for the slider whatever the upscaler, 0
  as off, off by default, the Linux launcher's 0.5 fallback made 0.20; whether existing settings
  switch once is the tester's call. *Built, as the tester agreed (2026-11-25):* one pass, the
  engine's, after every upscaler and TAA (AMD FSR's own off; `BB_SHARPEN_FSR_OWN=1` for
  comparison), AMD's RCAS on a display-like picture (the HDR scene through AMD's reversible tone
  mapper and gamma 2.0), unblended at AMD's mapping, 0-1; off by default, the untouched old default
  (on at 0.20) switched off once (`sharpen_off_046`). Found while building: AMD's code on the HDR
  scene needs a cap and exact reciprocals (register).
- *The reactive mask's menu line:* "FSR 4 may" replaced by what the running FSR answers (FSR 4.1.2:
  not used), as the startup line already reports it. *Built:* AMD's resource query, asked as the
  context is made, published to the menu (`BbSettings::fsr_reactive`): "FSR 4.1.2 does not read it
  (it says so itself)", "... reads it as its reactive mask", or, before FSR runs, that it shows
  there while FSR runs; in the 8 translated menus too.
- *The tester's run of it (2026-11-25, `4bc9ea19`, engine id 106, RX 9070 XT, FSR 4.1.2 at Native AA,
  AMD's checks on, about 7 minutes, two loads):* AMD's checks found nothing. Sharpening looked better
  and "cleaner" than FSR's own, but 0.20 much weaker than FSR 4's own 0.20 before; the dragged
  slider wrote 1,272 log lines. The scene is almost all below 1 (above 1: 0.01-0.15% of pixels a
  minute, at most 0.29% in a frame; above 4 at most 0.001%; the brightest 2.6-23.3). Write faults
  133, 39 and 99 in three minutes and none in the other four (146-343 a minute in the run before),
  those on images over no game resource (71 and 74) only in the minutes after a load. YEBIS's 1D
  and 3D creators made nothing (as read). The faulting images over no game resource are in no
  allocation of YEBIS's (35-64 a minute; all 15 named: the 64x1/32x1/16x1, 256x256, 32x32, 4x4 and
  1x1 RGBA8 textures and the 64x64-to-4x4 format-9 chain at `0x105eeb...`-`0x105ef7...`, each
  faulting on a write beside it), a few in the tiny allocations one YEBIS function makes by the
  thousand after a load (`0x11ecc30`, 3,000-4,600 a minute, about 200 bytes each; YEBIS's frees are
  not followed, so some of those may be memory handed back and reused). So the owner is not YEBIS.
  YEBIS's allocators take their memory through a function pointer (`0x54b9f38`, called by both and
  by `0x11e65f0`) that the game installs through YEBIS's setter `0x11e61f0`: the game's own GPU
  memory allocator under YEBIS, likely under these textures too.
- *Changed after it (engine id 107, the tester's asks):* the strength a percentage of RCAS's maximum
  (`sharpening`, 0-100%, 50% by default; linear in RCAS's lobe; an old `sharpness` converted); the
  launchers' and menu's tooltips and descriptions with it; the three costs found in the review of
  the first build: the scaled path's sharpening in the copy into the UI image (one pass for three),
  the 1080p path's TAA sharpened in the merge (its own pass not run), and the log's line written
  once a change has held for a second (no formatting each frame).
- *Next for the faults (to design for the tester's review):* follow the allocator behind YEBIS's
  function pointer: read what the game installs there and who else calls it, hook it as YEBIS's
  allocators are, and name the faulting images by who asked for their memory; then step 2 for that
  owner.

**Phase C: beta 17, shaders and pipelines (rule 5).**

- Why: beta 15's cold run spent 478 ms translating shaders on the GPU thread in its first minute,
  a 220.8 ms frame after the load; warm runs still translate 36-46 ms a minute in new places. And
  every engine update that changes a file in the GPU cache's identity throws every player's cache
  away: beta 15 did it with a thread count in `vk_pipeline_serialization.cpp`, and the 0.4.6 update
  would do it to everyone (`liverpool.cpp`, `vk_pipeline_cache.*` and that file changed since 0.4.5,
  none in what the cache stores, to be checked).
- Research first: what moving translation into the compile jobs needs (the program cache's lock,
  the translator's memory pools per thread, the shader information a draw needs before its pipeline
  exists, the draws that cannot be left out); the identity narrowed to what the cache stores; the
  catalog decision (HW's own catalogs from the tester's caches, or Lantern's inert seeding code
  removed); optionally AMD's pipeline library for the single pipelines a frame waits for (55-62 ms).
- Gate: the cold run's frames after a load without GPU-thread translation spikes; a build that
  changes no stored format keeps the cache; the warm run unchanged.

**Phase D: the release candidate.**

- The switches settled with the tester: every 0.4.6 switch (`BB_EMPTY_SURFACES`, `BB_TARGET_POOL`,
  `BB_WATCH_GAME_TEXTURES`, `BB_TAKE_EARLIER_WRITES`, `BB_IMAGE_OWNERS`, `BB_WATCH_GAME_BUFFERS`,
  `BB_LABEL_CHECK`, `BB_PIPELINE_BURST`, `BB_CPU_SETS`, `BB_LOAD_UPLOAD`, `BB_READY_SURFACES`,
  `BB_COPY_NOTICE`, `BB_GX_FOLLOW`, `BB_GX_RESOURCES`, `BB_VRAM_MANAGER`, betas 16-17's), and
  0.4.5's `BB_COPY_HOLD` and `BB_COPY_MERGE`: kept as troubleshooting switches or removed with their
  old paths; safe mode's list to match.
- The small-card run (`BB_VRAM_TEST_BUDGET_MB=3500`, the VRAM manager on); its unbind waits row.
- The NVIDIA checks (DLSS, part 3's time, the 8 GB budget) if the RTX 3070 Ti is at hand, else
  named as untested on NVIDIA in the release notes.
- The tester's open calls: V-Sync's text when AMD's driver lacks the mode chosen; the catalog (if
  not settled in phase C); the label wording pass. (Sharpening's default, pinned 2026-11-24,
  was decided and built in beta 16: off, 0 as off, the same strength under every upscaler.)
- Docs: release notes, `engine-map.md`'s memory parts, `gpu-memory.md`'s status, `memory-model.md`'s
  "Where it stands", this plan.
- The release-candidate run: the usual route with loads, a boss or a cutscene if possible, online;
  then 0.4.6 with its engine id.

**Outside the track:** the VRAM held after closing the game (the tester's Task Manager figures);
HW's name to The Hunter's Dream (when the team is asked); after 0.4.6: the performance and features
lists below, Linux. (The smaller FFmpeg was moved up and done, 2026-11-25.)

### Waiting on the tester

- **Beta 12's last gate:** met in kind by beta 13's `m33` frame (287 MB uploaded; 1.5 ms of
  protecting over 2,007 pages, against 23-25 ms over about 100,000). The exact case, `m34`, the
  Hunter's Dream, then straight back to `m34` (beta 10's 3.83 and 3.85 s loads), is still not run:
  with another area between (the tester's `e4fc8e33`), the return loads as a first entry (3.11 s,
  worst frame 19.1 ms). Optional now.
- **Not covered by any run since beta 9:** a boss, a cutscene, character creation, online play
  (the sampling net is the check there).
- **Optional, beta 11's A/B:** the same route twice, as shipped and with `BB_IMAGE_OWNERS=0`, to put
  a number on the VRAM difference the tester saw.
- **Run C** (the shader cache cleared, the same route), before beta 15 only.
- **Beta 15, part 3:** a run with a second load (the Dream and back, or a lantern): the first runs
  (2026-11-24) each had one load, so nothing was learned before it and no surface was made ahead.

## The register: every open item

Every item not done, wherever it was raised, with where it is scheduled (the ground rule "Nothing
is dropped silently"). Items the 0.4.6 roadmap took are listed first, so their old wording can be
found.

### Taken into 0.4.6

| Item | From | Scheduled |
|---|---|---|
| YEBIS's rebuilds stall 59-84 ms at area crossings (about 280 targets filled from memory) | Beta 7's log, 2026-11-08 | Beta 8 |
| A target's first fill hashes every level | Research, 2026-11-08 | Beta 8, part 2: not a bug (the hashes protect GPU-written images from page-sharing writes); surfaces started empty take none |
| Depth targets filled from memory; their HTILE followed by address | Research, 2026-11-08 | Beta 8 part 1 done; the HTILE by owner and the start from the game's clear dropped by the tester (2026-11-09): nothing pointed at either, and no depth target lacked the game's clear in 404 |
| Whether YEBIS frees its old context before building the new one; where new targets land | Research, 2026-11-08 | Beta 8's `YEBIS rebuild:` lines (run A) |
| Which owner objects build YEBIS contexts (the five virtual methods into `0x21d2dd0`) | Research, 2026-11-08 | Only if beta 8's numbers need it |
| **Beta 9's narrow hole:** a texture made with its data over an unwatched target's image, left to its draw (6-8 a minute), can show the target's old contents: its creation's copy no longer faults | The design check, 2026-11-08 | Done in beta 10: a creation with data marks the images over it written (a live surface reaching past it keeps beta 8's rule, counted) |
| Live textures overlap (6-8 a minute a new texture's memory holds a live texture's image): "one owner per image" not yet safe | The design check, 2026-11-08 | Beta 10's census: none in the quick run (`57912614`, 8 minutes). The 6-8 a minute were images whose own texture had no destroy, not a live texture under the new one (a misreading). The full run confirms; beta 11 acts |
| Drawn images kept by the "used just before" guess: 76-371 a minute at loads (up to 584 MiB), YEBIS's old targets under its new set, freed only when a later rebuild lands over them | Beta 10's quick run | Beta 11 (built): retired at the rebuild, the pool's donors first; its run tells |
| The design note's item 6: copies through memory between owned images done image to image | The design note | Closed by beta 10's quick run: up to 2,809 copies a minute through memory, none to an owned image |
| Beta 10's "kept because another live texture overlaps" counted the images "used just before" a second time | Beta 10's quick run | Fixed (engine id 95): each image counted once |
| The frame after a load (the 3.8 s one into m34): about 400 MB of the game's buffers uploaded and about 100,000 pages write-protected at once, 23-25 ms of a 44-49 ms frame | Beta 10's runs; there since 0.4.5 beta 9 at least (then 58-69 ms with texture work beside it) | Beta 12: the protecting gone on a frame of the kind (beta 13's run, after the `m33` load: 1.5 ms over 2,007 pages); the frame still waits for the buffers' upload and sparse binds (45.2 ms, 23.0 ms of them in sparse binds): the row on uploading during loading screens |
| 91% of write faults are GX's create calls copying data into memory the buffer cache still watches (beta 11: 20,899 of 22,991) | Beta 11's run, 2026-11-09 | Beta 12, parts 2 and 3 |
| GX writes a depth texture's HTILE (its clear pattern) when the game creates a depth view (`0x2166610` through `0x217a210`): a CPU write into a GX texture after its creation | The check of beta 12's design, 2026-11-09 | Textures stay watched (this write is why); the depth items were dropped by the tester (2026-11-09), so nothing more |
| Whether no CPU write reaches a live GX buffer in what beta 9-11's runs did not cover (bosses, cutscenes, character creation, online) | The check of beta 12's design | Beta 12's sampling net (part 7) and the tester's runs |
| The engine's own CPU stores for GPU commands (WRITE_DATA, DMA fills and copies, occlusion counts) counted on page faults to tell the buffer cache | Building beta 12, 2026-11-09 | Done in beta 12: each tells it (`NoteEngineStore`); the labels' fallback store too (engine id 98, from the tester's builder) |
| Labels written through the backing view (the usual path) never reach a VRAM copy of their page: a label inside memory the GPU reads as a buffer would be stale there, before beta 12 too; the "exact writes/s (labels, CPU bytes beside GPU data)" counter is never incremented (`late_vram_writes`) | The tester's builder and the check after it, 2026-11-09 | Done in beta 13 (engine id 99). Its run: 15,000-27,000 stores a second, 17 in 7 minutes over a VRAM copy (marked), none on a page the GPU wrote; the GPU thread's time a draw unchanged |
| The tester moved to an RX 9070 XT (2026-11-10) before the betas ended: the remaining betas run on AMD; the NVIDIA checks (DLSS, the small-card run's budget on an 8 GB card, beta 15 on NVIDIA) need the RTX 3070 Ti or another NVIDIA tester | The tester's first AMD runs, 2026-11-10 | The release, if the 3070 Ti is at hand; otherwise named as untested on NVIDIA in the release notes |
| With a cold GPU cache on AMD, frames wait for pipeline builds: 2,043 ms in the first load, 484 ms after the `m34` load, 35-351 ms frames in the first areas (pipelines about 10-26 ms each on AMD's driver) | The tester's first AMD runs (`d4be8f82`, `0f407d24`) | Beta 15 part 1: bursts on more threads (up to 10 on the tester's CPU, against 4). A first visit itself still compiles: only keys from another PC avoid it (the catalog row) |
| On AMD, the first in-game frame after the session's first load took about 2 seconds twice: 2,043.4 ms (`d4be8f82`, beta 13, 2026-11-10) and 2,046.5 ms (`9210eea2`, beta 14, 2026-11-22); the NVIDIA runs and the other AMD run show 40-160 ms there. The engine's GPU side was idle in it (no pipelines, no uploads of note): the game side waited. Two lengths this close look like a fixed 2 s timeout | The tester's beta 14 run, 2026-11-22 | Not beta 14's (beta 13 had it). Not seen in beta 15's two runs (2026-11-24: the first frames after the same first load 29-221 ms cold, 29-49 ms warm); which part removed it (part 2 uploads 849 MB during that screen, part 4 tells its copies) is not known: a run with `BB_LOAD_UPLOAD=0 BB_COPY_NOTICE=0` would say. To look at only if it returns |
| One new pipeline a draw cannot do without (effects, post-processing: bound on the recording threads) holds the frame for its whole compile: 54.5 and 61.6 ms on the RX 9070 XT in beta 14's run (39.8 ms frames). Compile bursts do not help one pipeline. AMD's driver offers the graphics pipeline library with fast linking, which `pipeline_compiles.md` set aside for NVIDIA's costs and the shared descriptor layout | The tester's beta 14 run, 2026-11-22 | A research item, not decided: whether linking such a pipeline from libraries on AMD would cut the wait. Each pipeline costs this once per PC (the GPU cache keeps it) |
| After closing the game, 1.5 GB of VRAM stays in use (3.2 GB against the 1.7 GB before), on the RTX 3070 Ti as on the RX 9070 XT; always about 1.5 GB, not more after more sessions, and only a restart of Windows frees it; not seen with other games so far (the tester is trying some) | The tester, 2026-11-16 | Both vendors, a fixed amount and a restart to free it point at something system-wide set up once (Windows' desktop compositor, a capture or overlay program), not a leak in the game's process or one driver. The engine changes no display mode and no Windows HDR setting (exclusive fullscreen is disallowed by default). Not HW's process, as far as the logs show: all 207 of the tester's logs end with the start helper's "bb-probe exited with code 0", which Windows reports only once every thread of the process is gone, and an ended process holds no VRAM (bbhost's case of a process stuck in the driver at exit, `9df4166`, would show no such line). The window close ends by TerminateProcess after a 4 s watchdog. The tester's Details tab after closing (2026-11-16): no program holds 1.5 GB; dwm.exe 326 MB (it was not listed before the game), Firefox 332 MB, Steam's helper 227 MB, AMD Software's three processes about 310 MB; the visible programs add up to about 1.7 GB, the tester's usual total, so the extra 1.5 GB may belong to no program (Windows or the driver). Waits on the tester: the Performance tab's total beside that list, Win+Ctrl+Shift+B, a run quit at the title screen, and the Details tab before playing and after closing (which process holds it, dwm.exe included); closing the launcher; Win+Ctrl+Shift+B (the graphics driver restarted) and signing out and in, each telling whether it frees it; the display setup (Windows HDR, monitors); that session's log |
| HW names itself to The Hunter's Dream as Lantern does (User-Agent `shadPS4/Awakenchan/v0.20.0` from the launcher's `BB_CLIENT_VERSION`, added to the game's own requests too; the sign-in's device name `BloodBorneButPc on <PC>`): the server cannot tell HW from Lantern or one HW version from another, and HW's traffic is credited to Lantern | The plan, 2026-11-15 | Kept for now (the tester, 2026-11-15: the team not asked yet). bbhost's name not used: it would present HW as The Hunter's Dream's own client. When the tester asks the team to accept `HuntersWorkshop/<version>` (yharnam-pc was refused after renaming itself), both names change |
| **Online: co-op, invasions and PvP with The Hunter's Dream's other players need bbhost v0.2.31's peer protocol** (bbhost's release notes, 2026-11-10: "with the current server, co-op, invasions and PvP need v0.2.31 or later"; messages, bloodstains and ghosts unaffected). HW's `thd/` is bbhost `566184a` (2026-10-11) plus HW's fixes. Missing: (1) the NpSignaling handshake between games (bbhost #217, `3f9c4c0`: `fe 'b' 'b' 's'` datagrams on the P2P port, ActivationRequest/Ack, Offer/Accept/Check/CheckAck, Established, Close; resent every 500 ms, DEAD after 30 s). HW announces ESTABLISHED and ACTIVE on timers (+200/+400 ms) and drops those datagrams, so an updated bbhost player's game drops an HW player after 30 s; (2) Matching2's ESTABLISHED from that handshake, not timers (HW: 500/800 ms); (3) P2P datagrams up to 64 KB (HW reads and sends 2 KB; bbhost now sends the game's batches whole, 5 KB on a warp); (4) `sceNpCmpNpId`/`sceNpCmpOnlineId` by name, opt and reserved (HW: `memcmp` of all bytes); (5) `sceNpScoreSanitizeComment` giving the text back (`f990415`: HW players' character names show blank to everyone). Optional: the sign-in check (`/auth/whoami`), the connection test (launcher UI: discussed first), Matching Regions starting at Worldwide (bbhost `matching_region`). The ports' notes (2026-11-11): Lantern v0.9.42 carries the same update, bbhost's `np_signaling.cpp` vendored unchanged behind a thin shim (`vendor/bbhost`, `vendor/bbhost_shim`, `thd_bbhost_glue.cpp`), and says older versions time out after about 30 s; it still names itself `shadPS4 Awakenchan/v0.20.0`, as HW does (the launcher's `BB_CLIENT_VERSION`), so the server cannot tell HW from Lantern and the break is between games, not at the server. yharnam-pc's online was refused when it renamed its client and stays off until The Hunter's Dream's team accepts it: HW's name is left as it is unless that team says otherwise. The Hunter's Dream's site has no client notes (`/docs` is its statistics API). The announcement (Discord, ancat, 2026-11-10): from 0.2.31 "previous versions will no longer be compatible" for co-op and PvP matchmaking (ghosts, messages, bloodstains keep working); the information went to Lantern's and bbport Linux's authors, not to HW. Lantern v0.9.42 read in full (2026-11-15): what The Hunter's Dream needs is (1) to (5) above; the server calls are those of bbhost 0.2.30 (no request, header or reply a client must change; 0.2.31's join reply adds `HostRuleset`, which Lantern only logs). Lantern's way: bbhost's `np_signaling.cpp` vendored unchanged (`vendor/bbhost`), a thin shim (`vendor/bbhost_shim`) and glue onto the backend's peers, dispatcher and P2P port (`thd_bbhost_glue.cpp`); `thd_matching2.cpp` takes the handshake's established and dead per member; `thd_net.cpp` reads and sends 64 KB and carries the signal datagrams (also relayed); `thd_np.cpp` drops its own signaling and gets the compares and the sanitize copy. Not needed for The Hunter's Dream, also in that release: the sign-in check with a 15 s notice and offline on a refused sign-in, one event poll in flight, Worldwide matching, rules from patches, NP request timeouts 8 to 5 s, seamless co-op for five, cheats off online (HW already). HW has every backend function the glue calls. Corrected by the tester (2026-11-15): The Hunter's Dream gave its server's changes to bbhost alone; Lantern copied bbhost. The plan is made from bbhost v0.2.35 (`online-update.md`): its server calls are HW's; its NpSignaling has not changed since 0.2.31; v0.2.35 splits datagrams over 1,200 bytes again (`e74e3ce`) | bbhost v0.2.31-v0.2.35, Lantern v0.9.42 and yharnam-pc's releases read against `thd/`, 2026-11-11 to 2026-11-15 | Beta 14 (`online-update.md`): parts 1-5 built 2026-11-16 (engine id 100) and tested against bbhost's own code here; the tester's runs with bbhost 0.2.31+, Lantern 0.9.42+ and HW players decide it. Parts 6-9 (bbhost's sign-in check, one poll in flight, addresses masked in the log, a line per session) built 2026-11-21 (engine id 101) at the tester's choice; the name to the server stays until The Hunter's Dream's team is asked |
| Building an area's pipelines during its loading screen, the roadmap's shader beta (15), cannot work from the game's calls: the game creates all its shaders at startup and pairs vertex and pixel shaders only at the draw; a loading screen's input layouts (646 in the first load) give a vertex shader and its formats, not its pixel shader, targets or blending. What the PC has drawn is already built at the start | Beta 14's design, 2026-11-10 | Beta 15 does what shortens the bursts (part 1); first visits need keys from another PC (the catalog row); the roadmap's "moves on when" changes with the tester's agreement |
| V-Sync's default, On (latest frame) (Vulkan's Mailbox), is not offered by AMD's driver on the tester's RX 9070 XT (2.0.416): the engine falls back to On (every frame) (FIFO) with a warning only in the log; the launcher and the in-game menu still show the default. Same picture at 60 FPS on a 60 Hz screen (no tearing, the frame rate held to the screen), up to a frame more latency | The tester's first AMD runs, 2026-11-10 | Proposed to the tester (a settings text, so asked first): the log line in plain words, and the launcher and the in-game V-Sync row naming the mode in use when the GPU lacks the one chosen |
| Surfaces made at their first draw get their memory then: after a load, 11.8-35.9 ms of allocation in the first frame (beta 13's run: 11 images 14.3 ms after the 4.31 s load; 21 images 11.8 ms after m34; 194 images 35.9 ms after the title) | Beta 13's run, 2026-11-09 | Beta 15 part 3 (designed): ready images of the shapes the loads before needed, made during the loading screen and taken at the first draw (not at the create call: a target's shape comes from its draw). On AMD a surface costs little (50 in 1.9 ms): the time is NVIDIA's |
| A create call's copy over a page holding a dead resource's GPU writes reads that page back from VRAM first (about 930 readbacks in 7 minutes), for the copy to overwrite it | Beta 13's run | Beta 15 part 4 (designed): a cave at each of the two `memcpy` call sites (`0x216d5ea`, `0x2171fea`) tells the engine first; the inside pages' GPU data dropped without a readback, uploaded copies marked, images told once; the earlier-writes rule (`BB_TAKE_EARLIER_WRITES`) is untouched, as a creation with data never needs them |
| YEBIS's CPU writes fault about 2 a second on pages its watched images share (about 20 of 800 land in an image); in beta 15's runs 8-12 a second (`+0xc21ef2` through the host's memcpy, the same two pages, "an image on a page beside a live resource") | Beta 13's run; beta 15's first runs, 2026-11-24 | Small (handlers 0.0-1.2 ms a frame), but each fault takes the texture cache's lock on the game's thread. Found 2026-11-24 in the 1.09 executable: YEBIS's `DrawRectGPU_*` writing quad vertices into its shared vertex buffer (`MapSharedVertexBuffer`) for every post-processing pass; `CTextureUtil::InitializeDevice` packs that buffer beside YEBIS's utility textures, whose images watch the pages. Beta 16 part 1 (built 2026-11-24, engine id 103): the first run (`5dbb0102`) showed the faults unchanged, 500-740 a minute, on YEBIS's utility textures made without data (still watched); engine id 104 leaves every YEBIS texture to the calls. The second run (`27b942f1`, engine id 105): none left (0 a minute); write faults 146-343 a minute in all. Done |
| An image the calls cover (not page-protected) keeps that when a texture made without its data takes it over (the owner rule's hand-over): the watch is decided once, at tracking, and the image stays tracked through the destroy and the hand-over. Safe for YEBIS (its textures made without data are drawn by the GPU or filled through calls that tell), not for a GX texture made with data handed to a GX texture made without (filled by the CPU, no call tells it): a fill after the image's next use (which uploads the memory) is missed, and the texture net does not check that kind | The builder session's review of beta 16, 2026-11-24 (beta 16 only clears the old "made with data" flag at the hand-over) | Since beta 11. Fixed in engine id 104 (the tester: "I don't mind"): at the hand-over, an image the new kind does not leave to the calls (`WatchRules::ByCalls`) and the GPU did not draw is marked written and untracked, so its next use protects its pages; counted in the beta 16 line |
| The texture net is slow for beta 16's newly unwatched kinds: one round over all unwatched textures at about 2 MiB a second takes minutes, and images over 8 MiB are never checked. If Scaleform wrote a plane without its unmap (not proven either way: beta 15's runs show 20-23 unmaps against 5-10 faults a minute, consistent with writes between map and unmap), the UI could stay stale that long | The builder session's review of beta 16, 2026-11-24 | The runs: the beta 16 line's untold count by kind, and the UI. `BB_WATCH_SCALEFORM=1` or `BB_WATCH_YEBIS_TEXTURES=1` if menus, the HUD or post-processing stop updating. Engine id 104: the net in two lanes, YEBIS's and Scaleform's images apart from GX's, each about 2 MiB a second; the minute line gives each lane's round time (the middleware's CPU-filled images are expected to be a few MiB, a round in seconds); images over 8 MiB still left out |
| AMD FSR at outputs other than 1080p runs on the game's finished frame, after its tonemap and post effects (AMD asks for upscaling before them): the game's post-processing cannot run at another size; the game's motion blur is then smeared unless `BB_UPSCALER_NO_BLUR` | The FSR review against AMD's API, 2026-11-24 | Kept so (the tester: too much work; motion blur is off by default). DLSS the same |
| Write faults on images over no game resource, in YEBIS's memory: 75-301 a minute in the second beta 16 run, small RGBA8 textures and format-9 mip chains (named in the log), written by YEBIS's quads on one page and by the game's `memcpy` from a virtual method at `0x2929780`; made by a creator the engine does not follow (YEBIS's device has two unhooked creation methods reaching the tiler, `0x11dea90`, `0x11e1750`) | Beta 16's second run (`27b942f1`), 2026-11-25 | Beta 16 left them for later (its design's "Not in beta 16"); the creator to find and follow, then those textures left to the calls. Step 1 built (engine id 106): YEBIS's allocators hooked, each faulting image named with the allocation under it and who asked for it, the 1D and 3D creators counted. Its run (`4bc9ea19`): the creators made nothing; the faulting images lie in no allocation of YEBIS's, so the owner is not YEBIS; faults only in the minutes after a load (133, 39, 99 in three minutes, none in four). Next: the game's allocator behind YEBIS's function pointer (`0x54b9f38`, set by YEBIS's `0x11e61f0`), to design |
| FSR 4.1.2 reads only colour, depth and motion vectors (AMD's own answer, logged since beta 16): the reactive mask (menu, `reactive=1`) and FSR 3.1's tuning do nothing under it | Beta 16's second run (`27b942f1`), 2026-11-25 | The tuning section shows only under FSR 3.1 already; the reactive mask's rows could say the same. A menu change: the tester's call. Done in beta 16 (engine id 106, the tester's go-ahead): the menu's line is the running version's own answer |
| The second beta 16 run cannot show whether AMD's checks were on (`BB_AMD_FSR_DEBUG=1`) or whether a pre-rendered movie played (the player logged only at the detailed level) | Beta 16's second run, 2026-11-25 | Both logged since: "AMD FSR's own checks on" when they are, and a `Movie:` line for each movie (its codecs and size, FFmpeg's version). Answered by the tester (2026-11-25): the checks were on in `27b942f1` (no finding under FSR 4.1.2), and `027d744e` (engine id 105) played a 1080p movie under the trimmed FFmpeg to its end: about 2 minutes, its NV12 frames (1920x1088 and 960x544 planes) about 29 a second, frames steady (median 20.6 ms, p99 at most 21.4 ms, none over 1.5x the median, worst 30.1 ms), no error. Done |
| The movie player's frames fault about 360 times a second on pages the buffer cache watches (its uploaded copies): the player copies each decoded frame into the game's memory, and the GPU reading it protects the pages again (`027d744e`: 1,200 a window at one address) | The movie run, 2026-11-25 | The movie plays steadily, so not urgent. The player is the engine's own code: it could tell the cache before each copy, as the create calls do since beta 15 (`BB_COPY_NOTICE`). A candidate for later |
| The Linux launcher's sharpness falls back to 0.5 when `config.ini` has none (`AvaloniaLauncher/Views/GraphicsPage.cs`); the engine, the Windows launcher and the shipped settings say 0.20 | Reading the sharpening settings, 2026-11-25 | Done in beta 16 (engine id 106): 0.20, its slider 0-1 as everywhere |
| `bb-shader-lab.exe` carries the engine's FFmpeg (about 6 MB of code and 3 MB of line tables) though it plays no movies: it links the whole GPU library, whose game library table reaches the movie player | The builder session's cross build of the trimmed FFmpeg, 2026-11-25 | Small beside the 111 MB saved. A follow-up could link the shader lab without the movie player |
| Sharpening: on by default at 0.20, AMD's RCAS. Under AMD FSR it is AMD's own (0.20 is mild); under DLSS the engine's pass, much weaker at the same value (blended in at 20%); FSR's 0 still sharpens a little in FSR 2/3.1's code, so only the switch turns it off. NVIDIA retired DLSS's own sharpening (2.5.1) and leaves it to the game; AMD calls it optional and asks for a slider | The tester's question, 2026-11-24 | Proposed to the tester: off by default, the slider kept, the same strength under both upscalers, 0 as off. Pinned by the tester until before 0.4.6 closes (phase D). Done in beta 16 (engine id 106, the tester's go-ahead 2026-11-25): one pass, the engine's, after every upscaler, AMD's RCAS on a display-like picture at AMD's mapping, 0-1, off by default, the untouched old default switched off once (`upscaler.md`, "Sharpening") |
| AMD's RCAS on the HDR scene through AMD's own reversible tone mapper (as AMD's FSR 1 suggests for HDR) can lift a pixel by orders of magnitude: its ceiling, 1, is infinite light there, and where one channel stands out of its ring (the guard per channel, the noise detection on luma) RCAS lifts it toward 1, which the inverse multiplies across the pixel (a blue of 0.47 came out at 12; 359 at 14386 in a search of random neighbourhoods; 1-3% of noisy synthetic neighbourhoods over twice their brightest plus one). Its approximate reciprocal in the resolve is 0.32% low even with nothing to sharpen: through the inverse a pixel at 100 came back at 61 | Building beta 16's sharpening, 2026-11-25 | The engine's RCAS (`rcas_extra.glsl`): exact reciprocals, the result held at twice the brightest of the five plus one (scaled down whole), a pixel RCAS leaves returned exactly; grey and tinted edges never reach the cap. A first cap tried as RCAS's own ceiling halved the sharpening on ordinary content (median 0.5-0.73 of AMD's change): dropped. `rcas-test` checks it against a double-precision copy of AMD's |
| How much of the 1080p scene sits above 1 (which the sharpening's HDR encoding is for) is not known | Building beta 16's sharpening, 2026-11-25 | Measured from engine id 106: once a second one pixel in 16 of the upscaled scene, a minute line "Upscaler: the 1080p scene's light" (above 1, 2, 4 and 16, the brightest, not finite). Answered by `4bc9ea19`: 0.01-0.15% of pixels above 1 a minute (at most 0.29% in a frame), above 4 at most 0.001%, the brightest 2.6-23.3, none not finite: the tone mapper matters for lamps, fire and the moon only. The line stays |
| Beta 16's first sharpening build took AMD's sharpness (0-1, 0.20 by default): 0.20 much weaker than FSR 4's own 0.20 before (the tester), and AMD's mapping is exponential (0.20 is 33% of RCAS's maximum, FSR's 0 is 25%, 0 jumps to off) | The tester's run `4bc9ea19`, 2026-11-25 | Engine id 107: a percentage of RCAS's maximum, linear in its lobe, 50% by default (the tester); FSR's 0.5 is 50% |
| The sharpening's log line was formatted every frame and written at each slider step (1,272 of the 9,871 lines of `4bc9ea19`) | The tester's run `4bc9ea19` | Engine id 107: compared each frame, written once a change has held for a second |
| The scaled path's sharpening (outputs other than 1080p) cost an RGBA32F copy of the output, a pass back into it and the blit into the UI image: about 500 MB of memory traffic a frame at 4K for 100 needed; under AMD FSR it had been FSR's own until beta 16. The 1080p path's TAA ran a pass of its own | The review of beta 16's first sharpening build, 2026-11-25 | Engine id 107: one pass, the output sharpened into the UI image (`sharpen_copy.comp`), the old way only where the UI image's format has no storage view; TAA sharpened in the merge. `taa-shader-test` checks the copy against the separate pass |
| Beta 15 part 3 makes more surfaces than the first frames take: 82-185 ready (94-243 MiB) at the later loads of beta 16's first run, 20-40 taken; the rest freed 3 s after | Beta 16's first run (`5dbb0102`), 2026-11-24 | Made only while VRAM is comfortable, so not harmful as it stands; the learned shapes could keep only those taken. The release's small-card run decides whether it matters |
| Frames of 27-32 ms right after some loading screens (beta 12: 3 of 14, beta 11: 3 of 14, not counting those the minute report made) have no breakdown: the long-frame breakdown starts at 40 ms | Beta 12's run, the plan checked, 2026-11-09 | Built in beta 13; its run: allocating the memory of surfaces made at their first draw (after the 4.31 s load, 14.3 ms for 11 images in a 27.1 ms frame), the buffers' upload and sparse binds (after `m33`), pipelines (the recompile). Rows below |
| A GPU of another kind (the tester's RX 9070 XT) gives the engine another profile, so its first start empties the GPU cache and everything compiles again in play (`pipeline_compiles.md`: "a profile change still empties the cache"). HW ships no shader catalog to soften it: the catalog seeding (`SeedGpuCache`, `scripts/gpu_cache.py`, the updater's catalog copy) is Lantern's and finds nothing to seed | The plan checked against `start/main.cpp`, 2026-11-09 (corrected: the catalogs are Lantern's, not HW's) | After beta 15, with the catalog decision (beta 15's design): keeping a cache's states and keys over a profile change and shipping catalogs both use the lab's seeding, so they go together. Whether HW ships catalogs of its own (from the tester's caches) is the tester's call; if not, the inert seeding code goes at the release. The tester's 3070 Ti keys went with the swap |
| The engine's minute report (RAM, VRAM, VRAM manager, GX lines; `LogRam`) runs on the presenting thread and makes that minute's worst frame 25.7-30.1 ms, in every session: every minute in every log of 0.4.6 betas 5-12 (85 of 85), 1 of 36 in betas 3-4; which part costs the 10-13 ms is not known | Beta 12's run read in full, 2026-11-09 | Not done: not needed (the tester, 2026-11-09) |
| The GPU cache's identity (`build.sh`, `out/gpu-cache.id`) hashes `video_core/amdgpu/liverpool.cpp`, the command processor, which stores nothing in the cache: beta 12's change to it recompiled the cache at the start (58 shaders and 113 pipeline keys lost, 229 compiles in play against 98, 12 draws left out for up to a second, one 40 ms frame) | Beta 12's run read in full | Not done: not needed (the tester, 2026-11-09); beta 13's label check changes the file, so its first start recompiles the cache once. Raised again 2026-11-24: beta 15's start-up thread count (`vk_pipeline_serialization.cpp`) reset the tester's cache (33 pipelines at the start, a 220.8 ms frame after the load), and the 0.4.6 update would reset every player's: the track, phase C |
| The create calls' remaining write faults (beta 12: textures 2,268, buffers 1,487): where they land is a guess (memory not handed back) | Beta 12's run read in full | Measured in beta 13: CreateTexture's 1,098 (the GPU's data on the page 599, an uploaded copy 443, an image 64), CreateBuffer's 497 (272, 223), nearly all on memory no live resource holds. Each GPU's-data fault reads the page back first (about 930 readbacks), for the copy to overwrite: the row below |
| The net hashes about 60 MiB a minute of up to 875 MiB under the calls: a pass takes 4-15 minutes, so a write it finds is that late | Beta 12's run read in full | Kept while it finds nothing; a faster pass if it ever finds a write |
| Which load a `Loading:` line is (the first or second entry into m34) is read only from the files opened around it | Beta 12's run, the tester ("I thought I went there this time") | Done in beta 13: `maps opened:` on each loading line |
| Buffers made before the renderer exists are never told to the buffer cache (no hook yet): watched by protection as before | Building beta 12 | Kept so; the net would count their writes as untold if any happened |
| Uploading the game's buffers during loading screens rather than at their first draw | Beta 12's design | Beta 15 part 2 (designed): the buffers made with their data during a loading screen get their VRAM and upload during it, 64 MB a frame. Beta 13's run: a frame after a load (`m33`) waiting for 287 MB of the game's buffers and 23.0 ms of sparse binds (45.2 ms); the first minute made 2,520 buffers with data (281 MiB) |
| The game's dead data in VRAM (memory no live GX resource holds) handed back | Beta 11's run (1.6 GB of the game's data by the end); beta 4's freeing at releases cost 82-201 ms frames | Read from beta 12's run: the game's data in VRAM 448 to 1,664 MiB; 9,914 of 13,150 buffers released had data in VRAM at their memory (6.2 GB), mostly memory used again in place; over longer sessions it levels off at 1.3-1.9 GB (0.4.5 beta 13: 1.34-1.41 GB from minute 16 to 36). The tester's RTX 3070 Ti peaked at 4.1 of 7.25 GB. Not a step of its own: the release's small-card run decides it |
| Surfaces filled from the GPU's buffer writes made before they existed (run A: 21-32 a minute, up to 108 MiB; most of its one 41.6 ms frame) | Beta 8 run A, 2026-11-08 | Beta 9 measures them (`BB_TAKE_EARLIER_WRITES=0` leaves them out); left out by default only if the counts and the tester's eyes agree (a depth buffer copied through memory before its texture is made needs them). Beta 12's run: 1-91 a minute (up to 156 MiB) from before, 1-84 since. Kept on; settled with the release's switches |
| CPU writes on YEBIS textures, 10-71 a minute: which of its textures (its CPU-updated ones expected) | Beta 8 run A | Answered by beta 9's run: YEBIS updates its sampled textures 58-145 times a minute (never a surface; no Map or Unmap), now told by the call |
| 21 CPU writes on small textures made with their data at every startup (32x32, 1024x512): which code writes them | Beta 8 runs A and B | Answered by beta 9's run: GX's create call copying the data in, and asset loads (told without faults). Textures made with their data unwatched in beta 10 (done) |
| `FindImage`'s fast path must forget an image whose owner changed (`registry_generation`) | The design check, 2026-11-08 | Not needed in beta 11: nothing is refused at a draw, so the fast path hands out only images `FindImage` itself would |
| Refusing a dead image at a draw (the design note's item 2) | The design note | Not built in beta 11: draws recorded after the game's destroy read the texture (0-3 a minute in beta 10's runs; the console's memory still holds it). Looked at again only if a stale image is seen |
| Images whose texture's memory the game released (no destroy event) are not dead for the owner rule | Beta 11 | None in beta 10's runs ("live resources whose memory the game unmapped 0"); a destroy event for them if they appear |
| A hand-over by shape alone: a texture made without data of another format over an image of the same size takes it (contents kept, the format's view decided at the draw) | Beta 11 | Counted in the owner rule line; looked at if images handed over go unused |
| The dead-target pool barely gains from the rule: YEBIS's retired targets are still in the GPU's hands when the new set is first used (5-186 taken, 30-469 found none a minute) | Beta 11's run | Later, if VRAM churn at loads matters: a pool that takes an image the GPU is finishing, its reuse ordered after that work on the same queue. Beta 15 part 3 makes images of the missing shapes ahead instead, for the frames after loads |
| Beta 10's "kept because used just before" label also counts images the beta 11 rule keeps by the exact "used since" (1-2 a minute) | Beta 11's run | The label is beta 10's; reworded or removed with the release's log pass |
| How much lower VRAM is with beta 11: the minute lines show textures 8-11% lower and more handed back after loads, the driver's total about the same (the game's buffers grow with the areas) | Beta 11's run, the tester | The optional A/B run above |
| Textures made with their data unwatched: a CPU write no call tells would go unseen (none in beta 9's run beyond the create copy and asset loads) | Beta 10 | Beta 16 part 3: the texture net hashes the unwatched textures the CPU fills (GX's and YEBIS's made with data, Scaleform's planes) and counts any change no call told; `BB_WATCH_GAME_TEXTURES=1` and safe mode watch everything |
| A live surface reaching past a new texture made with its data keeps its contents (the memory outside the new texture is older than the GPU's drawing) | Beta 10 | Counted in the owners line; kept as is in beta 11 (2 and 4 a minute in beta 10's runs) |
| GX's input-layout creator in 1.09 (for predicting shader variants) | Research, 2026-11-08 | Done in beta 8: `0x21657a0`, hooked and counted |
| "The game's data followed the same way (buffers built ahead, freed on release)" | Plan step 3 (0.4.5 beta 16) | Partly done (0.4.5 beta 17; 0.4.6 beta 5); the rest is beta 12 |
| "Shaders translated when the game creates them" (all 13,277 at startup) | 0.4.5 beta 17, "Not done" | Not done (beta 15's design): translation is the smaller cost (the second AMD run's `m34` minute: 228 shaders in 411 ms, against 1,392 ms of compiles in one frame), and a translated variant still needs the vertex and pixel shaders' pairing, which the game makes only at the draw |
| "The shader building and its threads" | 0.4.6 memory design, "Next" | Beta 15 part 1 (burst threads) |
| `pipeline_compiles.md`'s "Remaining (not done)": catalog coverage, the warm-up's state rebuilt on the main thread, a profile change emptying the cache, the scene-curve folder unseeded, AMD and Intel catalogs | `pipeline_compiles.md` | Looked at in beta 15's design: catalog coverage, the profile change, the scene-curve folder, and AMD and Intel catalogs are the catalog work (after beta 15, the catalog row); the warm-up's state on the main thread costs 0.1 s now (the second AMD run: 220 of 224 in 0.2 s): not needed |
| `BB_COPY_HOLD` and `BB_COPY_MERGE` removed ("stay through the stable release, then go") | The rest of 0.4.5, item 1 | The 0.4.6 release, with the tester |
| A small-card run: `BB_VRAM_TEST_BUDGET_MB=3500` with the VRAM manager on (its rules predate the memory model) | Plan step 3 (0.4.5 beta 16's test) | The 0.4.6 release |
| The VRAM manager's unbinds wait only for the work that used the evicted blocks (a last-use tick per block) | Plan step 3 | With the small-card run; built only if it shows bind waits |

### Candidates the logs decide

| Item | From | Scheduled |
|---|---|---|
| The game's area prefetch (made for a hard disk; its model could drive the loads) | 0.4.6 memory design, "Next" | Looked at after beta 11, if loading screens or the frames after them still stall. Read from beta 12's run: loading screens 1.7-4.3 s; a frame over 33 ms after 2 of 14 loads (the first from the title, and the one the cache recompile hit), against most loads in 0.4.5 (beta 9: 9 of 10, beta 10: 8 of 8, beta 17: 8 of 9). Not needed unless the second m34 entry still stalls |
| eFMann's smoothness leads ([archive/hitches-2026-10.md](archive/hitches-2026-10.md)) | Plan step 3, "Later" | Only if stutter after loads remains after 0.4.6 |
| A flicker at two area crossings: post-processing effects off and on for a split second | The tester, 2026-11-08 | Not the memory work: there in 0.4.4 and 0.4.5, and in beta 8 with `BB_EMPTY_SURFACES=0`, `BB_TARGET_POOL=0` or `BB_GX_FOLLOW=0` (log `8d281c42`). Likely the game's own reset of its post-processing when YEBIS rebuilds; parked until a PS4 recording of those spots shows otherwise |
| A hitch when a controller connects at startup (the UI changes its button prompts; possibly a YEBIS rebuild) | The tester, 2026-11-08: "something for later if I ever even bother with it" | Low priority; beta 8's rebuild logging may show whether it is a rebuild |
| A first visit with the cache cleared: the GPU thread builds the pipelines' shaders itself (beta 15's cold run: a 220.8 ms frame after the load, 185 pipelines, 323.6 ms of shader and pipeline work on the GPU thread); burst threads compile only what is queued after that | Beta 15's first runs, 2026-11-24 (`b53c92dc`) | A one-time cost per new build or cleared cache; looked at if new ground still stalls with the cache warm (translation off the GPU thread is the lead) |
| The tombstones in a graveyard do not look right to the tester | The tester, 2026-11-08 (beta 10's second run) | Not the memory work: the same in 0.4.5. Looked at only with a screenshot and the place, or a PS4 comparison |

### Performance, after 0.4.6

The evidence for each is in [performance.md](performance.md) and [engine-map.md](engine-map.md); the
plan they come from is [archive/plan-0.4.5.md](archive/plan-0.4.5.md).

| Item | From | Scheduled |
|---|---|---|
| Shader code (step 2): rank the game's shaders by GPU time and check the top ones with `bb-shader-gpu-stats`: registers and occupancy, inputs still interpolated by GCN's formula (321 of 1,201), the legacy multiply. The Hunter's Dream as its scene (DLAA 0.96 ms, the G-buffer pass 0.74 ms, the lighting passes) | Plan step 2 | After 0.4.6 |
| Vertex shader contraction with an invariant position (`shader-5`, about 4% of vertex code) | Plan step 2 | With step 2 |
| The native compiler's step 2 (control flow without goto variables), measured in the lab first | Plan step 2 | With step 2, if the hot shaders gain |
| The barriers that remain (about 23 a frame at the lamp spot): a measurement beta with reads tracked exactly (`BbToggle::CoarseReadTracking`) and barrier counts by what they wait for | The rest of 0.4.5, item 2 | After 0.4.6 |
| The GPU idle between submissions (about 0.12 ms a frame in 3.3 gaps): why the GPU runs dry there, and whether submissions can overlap | The rest of 0.4.5, item 3 | After 0.4.6 |
| `mem-4`, `upscale-2`, `upscale-3` (0.1 ms at most; the second only at reduced upscaler presets) | The rest of 0.4.5, item 6 | When nearby work passes them |
| The CPU (step 4): stage B on two threads (`parallel-stageb.md`, approach B), with `idle-1`, `pipe-1`, `asc-2` and `rt-1` | Plan step 4 | When a 6-core CPU or a frame rate above about 230 FPS makes it the limit |
| A GPU timer at pass ends that doesn't wait at each label | Plan, "Later, if at all" | If an item's gain needs a finer measure than FPS |
| The copy shader's ranges served from the engine's snapshots, so no copy runs | Plan, "Later, if at all" | Later, if at all |
| `engine-map.md` section 6: the first test session, the small measurement code and what the agents could not determine | `engine-map.md` | Gone through with step 2 and step 4; what was answered since is marked there then |

Not reopened: `seg-1`, `renderer-3`, `renderer-4`, `renderer-6` and the refuted ideas
(`engine-map.md` section 5); the October hitch leads (the game's job threads, the online summon,
the PC memory model), fixed, removed, or not the cause in 0.4.4's logs.

### Features and fixes, after 0.4.6

- AMD FSR frame generation through the SDK, on AMD's swapchain: only if the tester brings frame
  generation back (Decisions).
- OptiScaler checked again, then kept or dropped.
- A proper DLSS test, then its cost table.
- The SSR patch's tuning writes measured (bbhost: 7.8 ms against the game's 6.1).
- Less detail when the startup resolution patch renders below 1080.
- The passes left unjittered (Lantern's DLSS audit; a likely slight wobble while panning),
  checked in HW.
- The message boxes' confirm delay (bbhost, Kyoskii; optional, a change of feel).
- Features: Anti-Lag 2, lighting effects, lamp to lamp, SMAA/FXAA, FSR 1.
- The frame lock with Match display (pinned).
- The AMD and Intel shader item (`shader-3`).
- Small: the online probe's log line says how long the probing took, as bbhost's does
  (`after N ms`), since log lines carry no time.
- The features bbport 0.7 noted that HW has not taken: GT-VBAO, the shader recipe pack (to be
  converted to HW's cache inputs), video skipping, invert mouse X ([community-fixes.md](community-fixes.md),
  "bbport 0.7, reviewed").
- **Done (2026-11-25, engine id 105; the tester: "make it optimal"): a smaller FFmpeg.** Raised by the
  builder session (2026-11-24): the Windows packages bundled MSYS2's CLANG64 FFmpeg, built with GPL
  encoders and their libraries, 101 of the 110 DLLs in `engine/out` (123.5 of 137.4 MB). Now every
  package links the engine's own FFmpeg 9.1 (`packaging/build-ffmpeg.sh`; its release tarball pinned
  by the SHA-256 checked against FFmpeg's signature), static and LGPL-2.1-or-later
  (`licenses/FFMPEG.txt`), with only what the movie player plays: the MP4 family (the console's
  AvPlayer: `.mp4`, `.m4v`, `.m3d`, `.m4a`, `.mov`; HLS not implemented), H.264, HEVC and AAC, the file
  protocol. CMake links its archives by path (a system or MSYS2 FFmpeg is never picked) and Windows
  requires it. `movie-decode-test` plays synthetic movies in each container and codec by path and
  through custom I/O, the streams decoded at once on threads of their own; it fails without the HEVC
  decoder. Found on the way: the Linux FFmpeg (7.1.1, setup-ubuntu.sh) was built without threads
  (`--disable-autodetect` leaves them out), so its locks and one-time initialisations did nothing
  while the player runs FFmpeg on three threads: both builds now enable the platform's threads.
  Its `--disable-postproc` no longer exists in FFmpeg 8 and later; its default full debug
  information is line tables now, as the engine's; the `-latomic` FFmpeg's configure adds wherever
  the library exists is left out on x86-64 (native atomics), so the Linux library gains no run-time
  dependency; and the Linux package shipped FFmpeg with no licence file. To check: the intro and the
  cutscenes on Windows and Linux (the tester), the Windows DLL set (the builder, about 9 left).

### Checks not made

- **The launcher's texts on screen:** at 100% and 150% scaling, in English, German and Japanese,
  in both Classic and Modern layouts, plus the Insert menu's Enhancements section.
- **Launcher behaviour:**
  - Suggested settings with two monitors;
  - a Share code exported and imported on another PC;
  - Changed switches with Extra variables set;
  - the fixed-refresh frame lock greying out.
- **Online: joining another world** (summoned with the Small Resonant Bell, or invading). Hosting
  is checked: two invasions on 2026-11-03.
- **Untested for want of hardware:** FSR 2.3.4, HDR output. (AMD FSR 4.1 on Radeon: runs on the tester's RX 9070 XT since 2026-11-10, FSR 4.1.2 at native resolution, 0.51 ms a frame; the picture still to be judged.)

### Open decisions

- NVIDIA Reflex on `VK_NV_low_latency2` without Streamline: whether, and when.
- The GPU-path tracers that overlap.
- The System menu's PC pages under one parent entry, if the game's menu code allows it.
- Sharpening's default: decided 2026-11-25 (the tester), built in beta 16: off by default.

### Linux, for later

| Topic | Note |
|---|---|
| The launcher | The Avalonia launcher rebuilt on the Windows launcher's pages, with its stale texts, the dead DLSS model row and the 7 texts only it looks up. |
| FSR | AMD's SDK is Windows only. Linux has no FSR until AMD ships for Linux; the Linux build keeps compiling, and FSR is not offered there. |
| DLSS | NVIDIA's Linux loader library (`libnvsdk_ngx.a`) has the same licence problem as Windows': Linux keeps HW's own path (`libnvidia-ngx.so.1`), the driver's core, with the shared id. |
| Old Linux pieces | Removed in 0.4.4 beta 28: the Python GTK launcher, the Nix/AppImage build, `shell.nix` and `test_packaged_vulkan.py`. Linux keeps `packaging/linux/` (Ubuntu setup, `bb-start`, `play.sh`), `run.sh` with the Python start scripts, and the Avalonia launcher. A Nix or AppImage build, if wanted, is made again from those. |
| bbport's no-sparse memory module | For drivers without sparse binding (the 0.4.6 memory design, "Next"). |
| FSR 3.1 out of protected memory | For RADV handhelds (bbport 0.7's fix, [community-fixes.md](community-fixes.md)). |

## Closed since 0.4.4

- **Lamp and candle flicker (B14):** looked at again at 240 FPS and barely told from 30. Option C
  (the light module held to 30 steps a second) stays written down for a high-refresh report.
- **Online (two invasions, 2026-11-03):** the room, the join, the hole punch, traffic both ways
  and both leaves worked at 60 FPS. A travel started as an invader arrived held its loading
  screen 26.7 s until the game ended the session, which is the game's own handling. Four of five
  players never answered the probes; the game also connects to players whose signs and ghosts it
  shows, and the relay is the server's choice.
- **Beta 15's burst test raced at its start (found building beta 16, 2026-11-24):** with the burst
  thread asleep before the standing thread, the test's first job went to the burst thread and no
  burst ended in time (every run on a 4-processor machine). The queue was right; the test now waits
  for the first job to run before starting the burst thread.

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
| DLSS | The driver's NGX core, started with the shared application id 231313132 (`Init_Ext2`); with OptiScaler loaded, its module's NGX entry points on any GPU (engine id 126, "The OptiScaler rework"). No Project ID (the tester's call, 2026-11-01): the core's entry point that takes one is declared nowhere public, and learning it means disassembling NVIDIA's libraries, which their licence forbids (§4a). Not Streamline: with its core loaded, NVIDIA's Vulkan driver presents through DXGI and holds V-Sync On (latest frame) to two frames a refresh. NVIDIA's NGX loader library is not linked: the DLSS SDK licence (§2e) forbids use that puts the SDK under an open-source licence (HW is GPL). |
| When AMD's or NVIDIA's runtime can't start | TAA, with a log line and a note in the game. |
| Frame generation | Not offered, and its code removed in 0.4.4 beta 28 with Streamline's and Reflex's (the tester's calls, 2026-11-01 and 02). Deferred, the last item after 0.4.6 (the tester, 2026-11-27: "defer Frame Generation until a later date. I would like to implement it on both AMD and nVidia, which also means moving nVidia to streamline. This is a last thing to do"): AMD's through its SDK on AMD's swapchain (designed: "AMD's frame generation, designed for the tester's review"), NVIDIA's through Streamline, with NVIDIA's DLSS moved to Streamline too. Open when it is taken up: uncapped with frame generation (a cap at the display's refresh rate proposed, with a warning under the version's minimum real rate). |
| NVIDIA Reflex | Not offered. If it returns, it is a design of its own on `VK_NV_low_latency2` directly (the sleep before the frame's work, markers with present IDs), measured against the 120 FPS cap first. |
| OptiScaler | Audited and reworked (the tester, 2026-11-27: "it's clear that OptiScaler support needs a rework for us"; below, "The OptiScaler rework"). Engine id 126: the engine's DLSS calls go to OptiScaler on any GPU, every queue under the lock, the helpers in a folder of their own, the GPU's identity logged. Its frame generation and the rest after (the tester: "then we'll talk frame generation and everything else") |
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
| 17 | Shaders and pipelines (rule 5): shader translation off the GPU thread; the GPU cache kept across engine updates that change nothing it stores (built, then reverted in engine id 119); the catalog decided | Researched and designed 2026-11-25 (`memory-model.md`); the tester: parts 1 and 2 yes, the catalog later. Built the same day (engine id 114): the cache checked by its output on a new fingerprint and kept whole when it translates the same (`BB_GPU_CACHE_CHECK`), translations queued ahead by the draw-prep workers onto the compile threads and published by stage A (`BB_TRANSLATE_AHEAD`, off in safe mode). Its runs to read: a warm one (the first start keeps the cache) and a cold one |
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
  game heap's free from `0x2929780` (reached through a function pointer; first read as a `memcpy`). Ruled out by reading the
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
- *The second run (`64880ba6`, engine id 107) and after it (engine id 108):* AMD's checks clean; the
  sharpening without measurable cost (186-188 FPS off and at 20, 50 and 100% in one spot); faults only
  after the load, as before. The tester: 25% by default, a starting point; the rows say "RCAS" and name
  no vendor, with no line per upscaler. A run at an output other than 1080p is still needed to see the
  scaled path's copy at work ("Upscaler: the UI image is ...").
- *Next for the faults:* follow the allocator behind YEBIS's function pointer: read what the game
  installs there and who else calls it, hook it as YEBIS's allocators are, and name the faulting images
  by who asked for their memory; then step 2 for that owner. *Built as step 1b (engine id 110, the
  tester's go-ahead 2026-11-25; `memory-model.md`):* the game's heap class behind it hooked, its blocks,
  heaps and frees followed, each faulting image named by the block under it, a minute line; the second
  writer turned out to be that heap's free rewriting block headers, not a copy. One run with a load or
  two answers who owns the textures.
- *Step 1c and step 2 (engine ids 111 and 112):* every heap's range from the shared heap core put the
  textures in YEBIS's heap, the heap the game's YEBIS allocator takes from, through YEBIS's third
  allocation path (`0x11e65f0`), which step 1 had not hooked. Step 2 leaves the images over no game
  resource there to YEBIS's calls and the texture net (`BB_WATCH_YEBIS_HEAP=1` watches them), hooks
  that third path to name their maker at registration, and turns the costly research hooks off by
  default (the heap core's about 145 million calls a minute). YEBIS's allocations tell the texture
  cache when YEBIS takes memory there again (the images over the new block marked written), so
  YEBIS's allocator hooks are part of the rule now, not research only.

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
- *Research, first findings (2026-11-25, reading only):*
  - *The identity and 0.4.6:* `build.sh` hashes the shader recompiler, all of `video_core/amdgpu`,
    `cache_storage.*`, `serdes.h`, `liverpool_to_vk.*`, the `vk_*pipeline*` files (less the graphics
    and compute pipelines' and the common ones), `pipeline_cache_blob.h` and `vk_object_motion.*`.
    0.4.5's identity is `8e413f35b001886f`, today's `bc1f5b32fde7f850`: 0.4.6 as it stands would put
    every player's cache through a recompile at its first start. The files that changed store nothing
    differently: `liverpool.cpp` (the command processor's label stores telling the caches, betas 12-13),
    `vk_pipeline_cache.*` (the object-motion permutation asked first, 0.4.5 beta 17; the translation
    statistics, beta 8), `vk_pipeline_serialization.cpp` (the start's thread count, beta 15). So the
    identity is broader than what the cache stores; narrowing it (the stored formats' own versions, or
    hashing only the code that writes and reads them and the recompiler) would keep 0.4.5's caches.
  - *A changed identity is a recompile, not a loss:* `start/main.cpp` `ResetStaleGpuCache` runs
    `bb-shader-lab --recompile-cache` on each cache folder from the shader states kept beside it
    (`BB_SHADER_STATES`, since Lantern 0.9.37), and clears a folder only if that fails. Not lossless:
    beta 12's recompile lost 58 shaders and 113 pipeline keys (229 compiles in play against 98).
  - *Translation on the GPU thread in a warm run* is small: 1-3 shaders a minute, 1-2 ms each
    (`64880ba6`); the cost is the cold start (beta 15's first minute: 478 ms, one 220.8 ms frame) and
    after a cache is lost.
  - Next to read: where a draw's translation runs (`PipelineCache::GetProgram` and its callers on
    the recording threads), the program cache's lock, the translator's per-thread state, and what a
    draw needs before its pipeline exists.
  - *Research, read (2026-11-25; the tester: "go ahead with the research"; `memory-model.md`, beta
    17):* every translation runs on stage A (the draw-prep workers give an untranslated draw back);
    a draw needs its translation before its key, its binding or a deferred bind; the workers see the
    rest of the frame while stage A translates; the translator can run on several threads with pools
    of its own (publishing stays on stage A). 0.4.5's caches are valid for 0.4.6 (both fingerprints
    reproduced from git; the four changed files store nothing differently). A rebuild loses the
    shaders without a state and every key naming one.
  - *Designed (for the tester's review):* the cache kept on a new fingerprint when every shader with
    a state translates to the same output (else the rebuild); translations queued by the draw-prep
    workers onto the compile threads, published by stage A; the catalog decision.
  - *Built (engine id 114; the tester: "yes", the catalog later):* `bb-shader-lab --verify-cache` run by
    bb-start before any rebuild (`BB_GPU_CACHE_CHECK=0`: not); the workers' chains of untranslated
    stages queued as jobs on the compile threads, taken or waited for by stage A and published by it
    after its own specialization matches (`BB_TRANSLATE_AHEAD=0`: off; off in safe mode). Runs to read:
    a warm one, then a cold one with the cache folder moved aside.
  - *Part 1 reverted (engine id 119; the tester: "yeah revert it"):* the check compared a shader's
    SPIR-V and specialization, not everything its record keeps (the walker's code, the table plan),
    so an update changing those alone would have kept stale entries; a rebuild now loses nothing on
    the tester's cache. An engine update rebuilds the cache, as before beta 17.

**Before phase D: the double check (the tester asked, 2026-11-27).** The building is done (phase A's
FSR checks, phase B closed by engine id 113, phase C's catalog, splash and measurements), but runs the
phases left open were never made, and a problem they find would mean more building, so they come
before the cleanup:

- **Online with other players, never run on beta 14's protocol** (phase A item 3, "Checks not made"):
  being summoned and summoning, joining another world (the Small Resonant Bell, an invasion). The
  tester (2026-11-27): not tested for 0.4.6; the release notes name it untested.
- **What no run has covered since beta 9** (the memory betas changed how the engine follows the game's
  memory): a boss and character creation. Cutscenes: covered by the FFmpeg runs (engine id 105, the
  tester, 2026-11-27).
- **The tester's asks first (2026-11-27):** a texture mip bias slider in the in-game menu to test the
  upscalers with (one exists: Advanced, "Texture mip bias", -2 to +1, live, with FSR or DLSS; what it
  lacks to be asked), and OptiScaler revisited (the compatibility mode untested since October;
  OptiScaler 0.9.5 has FSR 4.1.2 and XeSS 3.0.2, its Vulkan frame generation only in v10 nightlies).
- **Sharpening at an output other than 1080p** (engine id 107's one-pass copy into the UI image, never
  seen at work: "Upscaler: the UI image is ...").
- **Beta 17's gate, to declare with the tester:** the cold start (a fresh folder: the catalog seeded,
  its pipelines built, the splash) and the warm runs read in 121-125's logs; no translation spikes on
  the GPU thread in them.
- **Optional, the tester's call to run or drop:** phase A's A/B (`BB_LOAD_UPLOAD=0 BB_COPY_NOTICE=0`;
  the 2 s first frame has not come back since beta 15), beta 12's exact `m34` case, beta 11's
  `BB_IMAGE_OWNERS=0` A/B.

**The OptiScaler audit (2026-11-27; the tester: "audit it, we've done a lot of work and never addressed
it since").** The compatibility mode (0.9.37 beta 7, 2026-10-22; last touched 2026-11-03): OptiScaler
found by a loaded module exporting NGX's Vulkan entry points (OptiScaler.ini beside it for a module
named like the driver's core); with it on, the Vulkan instance and device made on a thread of their
own, DLSS's NGX calls under the queue lock, and async submission off while its frame generation is on.
OptiScaler now: 0.9.5 (FFX SDK 2.4 with FSR 4.1.2, XeSS 3.0.2), no frame generation on Vulkan; v10
nightlies only have it, natively.

- **New since then, a likely fault:** OptiScaler installed as `winmm.dll` or `version.dll` beside the
  engine is loaded by every program there that imports those: `bb-shader-lab` (links winmm) and, through
  SDL3 (which links winmm and version), `bb-splash` and `bb-gpu-capabilities`. Since engine ids 121-122 the
  lab and the splash run at every start: OptiScaler starts in three processes it was not made for (its
  hooks in the splash's Direct3D 11 window, its `OptiScaler.log` written by each, a lab that fails
  leaving the GPU cache started again). Fix: those programs take winmm and version from the system
  folder only.
- **Since October, untested:** (1) DLSS needs NVIDIA's NGX core from the NVIDIA driver's own folder
  (`vk_dlss.cpp` `LoadCore`): on AMD none, so DLSS is never offered and OptiScaler's DLSS inputs cannot
  be used there (on AMD it would add XeSS only; FSR 4.1.2 the engine runs itself). (2) OptiScaler may
  call `vkDeviceWaitIdle` in its NGX calls (held under the queue lock), but the readback queue
  (`FastReadback`, `buffer_cache.cpp`) submits without that lock: every queue must be held for it.
  (3) Corrected (the tester: OptiScaler is installed as winmm.dll or dxgi.dll): both load in time. SDL's
  video start (the game window, before the Vulkan instance) loads `DXGI.DLL` by name
  (`SDL_windowsvideo.c`), from the engine's folder first, so `Detect` finds OptiScaler as dxgi.dll too.
  The helper programs' fault above then covers dxgi.dll as well (SDL in the splash and the capabilities
  check; the splash's Direct3D 11 renderer loads it too). (4) The texts on its frame generation describe 0.9's
  D3D12 bridge; v10's is native. (5) The engine's own AMD FSR (0.4.4 beta 27, after the mode) and
  OptiScaler's FSR input hooks: untested together. (6) The game window hidden until its first frame
  (122), HDR output and the 480 Hz pacing: untested with its frame generation.
- **d4r (the tester, 2026-11-27):** NVIDIA's DLSS (Super Resolution; Ray Reconstruction and frame
  generation experimental) on AMD RDNA2-4 through OptiScaler (shipped as `dxgi.dll`: OptiScaler
  10.0.0-pre1 or a nightly, with `d4r_boot.dll`, a `d4r` folder with NVIDIA's DLLs,
  `d3d12.dll`/`d3d12core.dll`). The NVIDIA GPU the game is shown is OptiScaler's spoofing (its
  `[Spoofing]` keys: DXGI on by default on AMD and Intel, Vulkan off by default), which d4r relies on
  and its package may turn on (the tester, 2026-11-27). Made for DirectX 12 games; its nightlies run Vulkan
  games through a native backend ("vkhip"; noted on RDNA3, Ray Reconstruction on RDNA4). For the engine:
  DLSS takes NGX from NVIDIA's driver only (`LoadCore`), so on AMD it never starts and OptiScaler gets no
  DLSS call (the launcher offers DLSS anywhere; the engine falls back to AMD FSR). Needed: with OptiScaler
  loaded, NGX taken from its module. A risk to check: OptiScaler's Vulkan spoofing, if turned on (d4r's
  package may), would steer the engine's vendor-dependent choices (AMD's workarounds, the shader profile and the cache, AMD
  FSR's availability); the engine needs no disguise once NGX comes from OptiScaler.

**The OptiScaler rework, designed for the tester's review (2026-11-27; the tester: "it's clear that
OptiScaler support needs a rework for us, whether it's for OptiScaler itself or users wishing to use
tools like this").** OptiScaler's docs (README, OptiScaler.ini, Spoofing, Frame Generation Options,
Manual Installation; v0.9.5's notes): installed as dxgi.dll (preferred), winmm, d3d12, dbghelp, version,
wininet, winhttp or an .asi; on Vulkan it takes a game's DLSS calls (`EnableDlssInputs`, hooking
nvngx), FSR 2 Vulkan calls (`UseFsr2VulkanInputs`); it outputs FSR 2.1/2.2, FFX (FSR 2.3/3.1), XeSS,
FSR 4 through Direct3D 12 interop (native Vulkan FSR 4 still in progress) or DLSS (`VulkanUpscaler`);
Vulkan GPU and extension spoofing off by default (`Vulkan`, `VulkanExtensionSpoofing`; which fields
they change is not documented); its menu on Insert (`ShortcutKey`); frame generation on Vulkan only
Nukem's DLSSG (needs the game's own DLSS-G calls) and, in v10 nightlies, its own. Tools such as d4r ride
on it: DLSS calls in, their own work out. So one channel serves both: the engine's DLSS calls.

What blocks it today, from the code: DLSS starts only with NVIDIA's NVX extensions on the device
(`IsDlssCapable`) and NVIDIA's NGX core from the driver's folder (`LoadCore`); on AMD neither, so
DLSS never starts and OptiScaler gets nothing (the launcher offers DLSS on any GPU; the engine falls
back to AMD FSR). The helper programs load OptiScaler too. The readback queue is outside the lock its
`vkDeviceWaitIdle` needs. Its menu key is ours.

Principles: one channel (the engine's DLSS calls to OptiScaler's module when it is there, on any GPU);
the engine keeps seeing the real GPU (no spoofing needed, and the engine's choices are made from the
driver's identity, `driverID`, as the shader profile already is); OptiScaler only in the game's own
process; everything it needs from the engine done for it; the player told plainly what runs.

- **E1, DLSS through OptiScaler:** with OptiScaler loaded, NGX is taken from its module (the one
  `Detect` found), on any GPU; the NVX-extension requirement and the driver's core are NVIDIA's own
  path only. DLSS is available as OptiScaler answers (`SuperSampling.Available`). The parameter self-test
  (`ParameterAbiOk`, custom keys) kept, a failure there reported as OptiScaler's.
- **E2, the real GPU:** the engine logs vendor, device and driver identity and says when they disagree
  (an NVIDIA vendor ID on AMD's driver: spoofed), and keeps its choices on the driver's identity; the
  driver cache's identity (vendor, device, driver) taken from what spoofing leaves alone, so turning a
  spoof on or off does not throw the driver's cache away. What OptiScaler's Vulkan spoofing changes
  (its docs do not say which fields) is read from the test run's log, with d4r's settings.
- **E3, every queue under the lock** while OptiScaler is loaded: the readback queue's submits take the
  queue lock as the graphics queue's and the presents do.
- **E4, detection and report:** the module and version, the name it was installed as, OptiScaler.ini's
  `VulkanUpscaler`, spoofing keys, `ShortcutKey` and frame generation, and d4r (its boot DLL or folder):
  one block in the log, the same in the in-game menu's OptiScaler section.
- **E5, the Insert key:** both menus open on it by default. The tester's call: our menu moves off Insert
  while OptiScaler is loaded with Insert (L3+R3 and another key stay), or OptiScaler's key is left to the
  player with a warning.
- **E6, frame generation:** the one submit thread kept while its frame generation is on; the texts say
  what is true now (none on Vulkan in 0.9.5 but Nukem's, which needs DLSS-G calls the engine does not
  make; v10's untested); its pacing and UI handling not designed for (after 0.4.6, if wanted).
- **E7, the in-game menu with OptiScaler:** the DLSS rows say the upscaler that runs is chosen in
  OptiScaler's own menu (its key named); a UI change, the tester's review.
- **H1, the helper programs apart:** `bb-splash`, `bb-shader-lab`, `bb-gpu-capabilities` and
  `bb-engine-assets` move to `engine/out/tools/` with their own copies of the libraries they need, so a
  DLL put beside the game (`engine/out`) is never theirs: Windows takes a program's own folder first
  for every DLL it loads, and SDL loads winmm, version and DXGI. Their callers (bb-start, the engine's
  `RunTool`, the launcher's FSR versions and cache export, run.sh, build.sh, both packages) follow; an
  update removes the old copies from `engine/out`. This also keeps the launcher's GPU check
  (`bb-gpu-capabilities`) from seeing a spoofed GPU.
- **L1, the launcher:** OptiScaler and d4r found in `engine\out` shown on the upscaler's settings (and
  DLSS named "through OptiScaler" on AMD); OptiScaler's files put beside the launcher instead (the
  package's top folder) named as misplaced; a UI change, the tester's review. **L2:** the crash report's
  texts made current.
- **D1, a player's guide:** OptiScaler or d4r into `engine\out` beside `bb-probe.exe` (not beside the
  launcher), as `dxgi.dll` or `winmm.dll`; DLSS chosen in the launcher; OptiScaler's upscaler in its
  own menu; spoofing left off; frame generation not supported yet; how to remove it. Windows only (the
  Linux build is native: OptiScaler is for Windows programs).
- **T1, tests:** native tests for the ini reading; the tester's runs: OptiScaler 0.9.5 as dxgi.dll with
  DLSS chosen (its output FSR 4, then XeSS), d4r's nightly (DLSS on the RX 9070 XT), each with a load
  and the warm-up; the helpers' isolation seen (OptiScaler's log written by the game only), the key, HDR.

Order: H1 and E3 (faults whatever is decided), then E1, E2, E4, E6, then the tester's choices (E5, E7,
L1) and D1, then T1. Not in it: OptiScaler's frame generation, its mip bias override against ours, a
launcher installer for OptiScaler or d4r.

The tester's calls (2026-11-27): E5 "don't do anything about it" (both menus stay on Insert); the
wording of E7, L1, L2 and D1 shown before it is built ("sure"); "let's focus on cleaning up and making
sure OptiScaler will work and that we are optimal, then we'll talk frame generation and everything else".

**Built, engine id 126:**

- **E1:** `OptiScaler::DlssThrough()` (loaded, its module found, `EnableDlssInputs` not false): the
  upscaler makes DLSS on any GPU and `vk_dlss.cpp` takes NGX's entry points from OptiScaler's module (its
  reference, never freed); NVIDIA's NVX extensions and driver core stay NVIDIA's own path. Its failures
  are named as OptiScaler's; no DLSS file version is claimed (`dlss_through_optiscaler` for the menu).
- **E2:** "GPU: identity: vendor, device, driver id, driver version", and a line when the vendor ID, the
  driver id and an AMD-only extension (`VK_AMD_shader_core_properties`) disagree: a spoof. What
  OptiScaler's Vulkan spoofing changes is still to be read from the tester's run; if it changes the
  driver id too, the engine's choices (the shader profile) follow it, and the guide says leave it off.
  The driver cache's identity left as it is: taking it from fields spoofing leaves alone would rebuild
  every player's driver cache once, for players who turn spoofing on or off.
- **E3:** the readback queue's submit under the queue lock with OptiScaler loaded (the graphics queue's
  submits, the presents and the device waits already were); its fence wait outside it.
- **E4, E6:** OptiScaler.ini read in one place (`optiscaler_ini.h`: `VulkanUpscaler`, `EnableDlssInputs`,
  `[Spoofing]` Vulkan, VulkanExtensionSpoofing and Dxgi, `FGOutput` or `FGType`); d4r named when its
  boot DLL or folder is beside it; the log's OptiScaler block says where DLSS goes, OptiScaler's Vulkan
  upscaler, its spoofing, and its frame generation as not supported yet. The crash report reads the new
  frame-generation line and the old.
- **H1 (undone in engine id 129, the tester's call; see the register):** `bb-splash`, `bb-shader-lab` and `bb-gpu-capabilities` in `engine\out\tools` on Windows
  (`BbStartup::ToolPath`, `Paths.Tool`, bb-start's splash; bb-start removes an older package's copies;
  build.sh builds them there). `bb-engine-assets` stays beside the engine: it links zlib and shell32
  only, none of the DLL names OptiScaler takes. Linux unchanged. The GPU cache's identity changes
  (`vk_pipeline_serialization.cpp`): players' caches are translated again once (a few seconds).
- **T1:** `optiscaler-ini-test` (the shipped ini, a player's, both frame-generation keys, keys of the
  same names in other sections). The tester's runs still to come.
- The tester's review of the drafts (2026-11-27): "Fine for the crash report, everything else is mostly
  unnecessary. Mark it, no need to do it right now". **L2 approved, built in engine id 130:** with OptiScaler's
  frame generation set, "…its frame generation set ({fg}), which the engine does not support yet. Set
  Frame Generation to none in OptiScaler's menu, then play again." (today's text points at a launcher
  frame generation that does not exist); 0xC0000135, "The engine could not start: a DLL it needs is
  missing from engine\out. Reinstall the package to put it back."; the OptiScaler device-lost text
  without the switch (done in engine id 128). **E7, L1 and D1 dropped** (the menu's OptiScaler lines stay
  as they are).

**Frame generation, back on the table (2026-11-27), then deferred the same day to the last item after
0.4.6 (Decisions).** The tester: "it's not for me honestly, people are
requesting it", "it's the nVidia users that requested it. But we can start with AMD then take it from
there." NVIDIA's own (DLSS Frame Generation) is offered only through Streamline, with Reflex required,
and brings back what removed Streamline in 0.4.4 beta 28 (NVIDIA's Vulkan driver presenting through
DXGI, V-Sync held to two frames a refresh); RTX 40 and 50 only, and neither of the tester's cards runs
it. AMD's (FidelityFX SDK 2.4, `amd_fidelityfx_framegeneration_vk.dll` beside the upscaler's DLLs
already shipped) runs on any vendor's GPU and can be tested on the RX 9070 XT. Order: AMD's first,
designed for the tester's review (research under way: AMD's Vulkan frame generation API, the engine's
present path); NVIDIA's after, as its own option.

**AMD's frame generation, designed for the tester's review (2026-11-27).** From AMD's FidelityFX SDK
v2.4.0 (its docs `frame-interpolation-api.md`, `frame-interpolation-swap-chain.md`,
`frame-interpolation-ml.md`, the headers `ffx_api_framegeneration_vk.h` and `ffx_framegeneration.h`,
its FSR sample and Cauldron2's Vulkan device and swapchain) and the engine's present path.

What AMD's Vulkan frame generation is: a replacement swapchain. The engine creates AMD's swapchain
(`ffxCreateContextDescFrameGenerationSwapChainVK`) instead of its own and calls AMD's replacements for
acquire, present, images and HDR metadata. Each real frame the engine records a "prepare" (depth,
motion vectors, camera, jitter, frame time, a reset flag) and hands its finished frame to present; AMD's
swapchain makes the in-between frame from the last two, composites the UI on both, and paces the
presents itself on two threads of its own, on queues of its own (present, image acquire, async compute).
A mode without the replacement exists but is undocumented and loses AMD's pacing: not used. Versions:
FSR 3.1 frame generation (60 FPS input or more) on any GPU, FSR 4's (30 FPS input or more) on RX 9000 with
Windows 11; AMD's loader picks the newest the GPU runs, as for the upscaler. Licence: the DLL is under
the SDK's MIT-style terms (its notice shipped, as for the upscaler's). Windows only (no Linux build).
GPU memory at 1080p about 170 MB. Not stated by AMD: support on NVIDIA and Intel (no restriction
either; its sample "can GPU-crash on NVIDIA", a known issue).

The design (the parts, in the order they would be built):
- **G1, the files:** `amd_fidelityfx_framegeneration_vk.dll` fetched with the upscaler's two
  (`tools/fetch_amd_fsr.sh`, AMD's hash), AMD's frame-generation headers vendored with its API headers.
- **G2, the queues:** with frame generation chosen, the device is made with the three extra queues AMD's
  swapchain needs (a presentable one besides ours, an image-acquire one, an async compute one), picked as
  AMD's sample picks them; none to be had: frame generation unavailable, said in the log and the menu.
  Every queue of ours stays under `Scheduler::submit_mutex`; AMD's are its own, so the engine's device
  waits become AMD's "wait for presents" plus a wait on our queue (`vkDeviceWaitIdle` needs every queue
  held).
- **G3, the swapchain:** `Swapchain` makes AMD's when frame generation is on, and goes through AMD's
  functions for everything after (acquire, present, images, HDR metadata, recreate in place on a resize,
  destroy in AMD's order). Off: the engine's own swapchain, as today.
- **G4, each frame:** the prepare from the temporal upscaler (it already holds depth, motion vectors in
  render pixels without jitter, the camera, the jitter and the camera-cut reset), on both its paths
  (1080p and scaled); frame IDs one a real frame, a reset at camera cuts, loads and the menus; the frame
  time. It needs the temporal upscaler running (FSR, DLSS or TAA): with the game's own AA alone there are
  no motion vectors, and frame generation is off.
- **G5, the UI:** the game's HUD is in the frame, so AMD is given the frame without it (the HUD-less
  colour: a copy at the game's first UI draw, which the scaled path already has and the 1080p path takes
  as it does for native HDR): it tells AMD which pixels are HUD, so the HUD is not smeared. Our own menu
  and FPS counter go into a UI image of their own, registered with AMD's swapchain and laid over every
  frame, real and generated (they then update at the real rate).
- **G6, pacing:** AMD's swapchain paces the frames it shows; the engine paces the real ones at half the
  output rate (AMD: on Vulkan the game's own frame limiter keeps the CPU from running ahead). The FPS
  limit means the output rate (120: 60 real frames); V-Sync on caps real frames at half the refresh, as
  AMD documents; the vblank lock is off with frame generation; the FPS counter shows both rates. The game's
  own timing never sees the generated frames: its frame pacer, the frame-rate fixes and the physics run
  at the real rate, so a fixed rate chosen with frame generation (120) runs the game and its fixes at
  half (60). Uncapped, the fixes that follow each frame's time are right at any real rate; the few set
  once (frame counts and constants in code, `FRAME_VALUE`/`FRAME_COUNT`, the cloth scale) are set for
  120, so an uncapped output of 240 with frame generation puts the real rate where those are exact.
- **G7, HDR:** AMD takes PQ (HDR10) and scRGB swapchains with its HDR flag: the HUD-less copy encoded as
  the frame is. After SDR works.
- **G8, settings and reports:** a launcher choice (Frame generation: Off, AMD FSR; read at start, the
  swapchain being made then) and a live on/off in the in-game menu when it started on; the log names the
  version AMD's loader picked, the queues and the rates; off with OptiScaler loaded (it hooks the same
  swapchain); the crash report names it.

Conflicts and risks: the game's HUD may still show artifacts where the HUD-less copy cannot tell it
apart (moving HUD elements); latency of about one real frame (Anti-Lag 2 through `VK_AMD_anti_lag`
a later step); below 60 real FPS (FSR 3.1's) the in-between frames get worse, AMD says turn it off; on
NVIDIA unproven until an NVIDIA player tests it, so the first NVIDIA run is the gate before it is
offered there. NVIDIA's own (DLSS Frame Generation through Streamline) stays a design of its own, after.

The tester's calls before it is built: the settings (G8: a start-time choice in the launcher with a live
switch in game), the FPS limit meaning the output rate (G6), SDR first and HDR after (G7), off with
OptiScaler (G8).

**Phase D: the release candidate.**

- **Settled, engine id 130 (the tester: "Use your own judgement ... Consider all angles"):** none of
  them is in the launcher (Extra variables and safe mode only). Removed, each default proven and its other
  value a test or a step back nothing needs: `BB_COPY_HOLD`, `BB_COPY_MERGE`, `BB_TAKE_EARLIER_WRITES`
  (with its test path and the map that told earlier writes apart for a log count), `BB_LABEL_CHECK` (the
  fix stays), `BB_CPU_SETS`, `BB_WATCH_YEBIS`, `BB_WATCH_SCALEFORM`, `BB_DMA_TELLS_TEXTURES` (on, kept),
  `BB_SHARPEN_FSR_OWN` (with AMD FSR's own sharpening path), `BB_TARGET_POOL` (`BB_EMPTY_SURFACES=0` turns
  it off too), `BB_WATCH_GAME_TEXTURES=targets`; and the owner rule's comparison count with the old
  guesses (beta 11's, done). Kept for safe mode: `BB_EMPTY_SURFACES`, `BB_WATCH_GAME_TEXTURES=1`,
  `BB_IMAGE_OWNERS`, `BB_WATCH_GAME_BUFFERS=1`, `BB_PIPELINE_BURST`, `BB_LOAD_UPLOAD`, `BB_READY_SURFACES`,
  `BB_COPY_NOTICE`, `BB_TRANSLATE_AHEAD`, `BB_VRAM_MANAGER` (Use less VRAM's too); safe mode's list without
  `BB_TARGET_POOL`. Kept for troubleshooting: `BB_GX_FOLLOW=0` (0.4.5's texture handling in one go),
  `BB_GX_RESOURCES=0`, `BB_SPLASH=0`, `BB_SHADER_CATALOG=0`, and `BB_TEXTURE_NET` until the performance
  work decides the net (slow, and it has found nothing). The approved crash-report texts built with it.
- The switches settled with the tester: every 0.4.6 switch (`BB_EMPTY_SURFACES`, `BB_TARGET_POOL`,
  `BB_WATCH_GAME_TEXTURES`, `BB_TAKE_EARLIER_WRITES`, `BB_IMAGE_OWNERS`, `BB_WATCH_GAME_BUFFERS`,
  `BB_LABEL_CHECK`, `BB_PIPELINE_BURST`, `BB_CPU_SETS`, `BB_LOAD_UPLOAD`, `BB_READY_SURFACES`,
  `BB_COPY_NOTICE`, `BB_GX_FOLLOW`, `BB_GX_RESOURCES`, `BB_VRAM_MANAGER`, betas 16-17's), and
  0.4.5's `BB_COPY_HOLD` and `BB_COPY_MERGE`: kept as troubleshooting switches or removed with their
  old paths; safe mode's list to match.
- Beta 16's research hooks off or out before the release: YEBIS's allocators (`BB_YEBIS_ALLOCS`) and
  the game heap's allocate and free methods (`BB_GAME_HEAP_ALLOCS`, on every allocation and free of that
  heap, with a lock and a map update each: far hotter than YEBIS's), and the scene-light measurement;
  their cost read first in the tester's run (the heap line's blocks made and freed a minute, frame times
  against a run with `BB_GAME_HEAP_ALLOCS=0`). The builder session's point (2026-11-25). The heap
  class's hooks showed no measurable cost (`498dc4e2`); the heap core's (`BB_GAME_HEAP_RANGES`) did; both
  off by default since engine id 112. Engine id 113 ("YEBIS by its calls") removed them, the update and
  Map hooks, the allocation records and the rebuild research; YEBIS's allocator hooks stay (the pool rule
  depends on them) with no switch. `BB_WATCH_YEBIS` and `BB_DMA_TELLS_TEXTURES` to settle with the tester.
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
| Write faults on images over no game resource, in YEBIS's memory: 75-301 a minute in the second beta 16 run, small RGBA8 textures and format-9 mip chains (named in the log), written by YEBIS's quads on one page and by the game heap's free (`0x20858a0`, from a virtual method at `0x2929780`, rewriting block headers; first read as a `memcpy`); made by a creator the engine does not follow (YEBIS's device has two unhooked creation methods reaching the tiler, `0x11dea90`, `0x11e1750`) | Beta 16's second run (`27b942f1`), 2026-11-25 | Beta 16 left them for later (its design's "Not in beta 16"); the creator to find and follow, then those textures left to the calls. Step 1 built (engine id 106): YEBIS's allocators hooked, each faulting image named with the allocation under it and who asked for it, the 1D and 3D creators counted. Its run (`4bc9ea19`): the creators made nothing; the faulting images lie in no allocation of YEBIS's, so the owner is not YEBIS; faults only in the minutes after a load (133, 39, 99 in three minutes, none in four). Next: the game's allocator behind YEBIS's function pointer (`0x54b9f38`, set by YEBIS's `0x11e61f0`), to design. Step 1b built (engine id 110): the game's heap class behind it (vtable `0x52e70c0`) hooked, its blocks and frees followed, each faulting image named by the heap block under it; the second writer is that heap's free rewriting block headers between blocks. Its run (`498dc4e2`): no measurable cost (231.3 FPS against 230.4 on the same route without it), but the textures lie in no block of that heap either: another allocator. They fault only in the minute after an area load (74, then 5), always beside them. The tester's call: one more step (every heap's range, from the shared heap core) or close it as it stands. The tester: one more step. Step 1c built (engine id 111): every heap using the shared core found at its first call, its range logged once, each faulting texture named by the heap whose range holds it. Its run (`e419b631`): all in YEBIS's heap (the game's allocator for YEBIS, `0x21d1e40`; 256 MiB at `0x105d400010`), made through YEBIS's third allocation path `0x11e65f0`, not hooked before; the core's hooks see about 145 million calls a minute: off for good. Step 2 built (engine id 112; the tester: "yeah go ahead, build it", 2026-11-25): the images over no game resource in YEBIS's heap left to YEBIS's calls and checked by the texture net's first lane, as YEBIS's registered textures since beta 16 (`BB_WATCH_YEBIS_HEAP=1`: watched as before); the heap's range read from where the game's callback reads it (the graphics object's pointer at image `0x56a6d18`; its heap in use when `[object + 0x2714] > 0`; its core at `+0x21e8`; the callback's bytes checked against 1.09's); YEBIS's third allocator hooked, each such image named and counted at its registration by the YEBIS allocation under it (it no longer faults to be named), and YEBIS's allocations in its heap told to the texture cache as written events for those images over the new block (what page protection learnt from the heap's header write beside them; `BB_YEBIS_ALLOCS=0` therefore watches them again); the heap class's and heap core's hooks off by default (`BB_GAME_HEAP_ALLOCS=1`, `BB_GAME_HEAP_RANGES=1` turn them on). Its run to read: no faults on images over no game resource after loads, those images' maker named, the frame rate on the route back to about 230 FPS, the texture net finding no write no call told. Its run (`d19667df`): no faults on those images, the net clean, its first lane 1 s a round. Their maker: YEBIS's update (`0x11d8d70`), which stages each subresource's data in a block from its heap and has the GPU copy it into the texture (DMA copies): the images over no game resource are those staging blocks, not textures, and the faults were the heap taking them back. The frame rate differs between runs more than any of these hooks could explain (medians in play 206.7-221.3 FPS on the same route across engine ids 109-112). Next: the tester (2026-11-25): "what we have been doing for some time now is not optimal"; YEBIS's handling to be reviewed with them (one model for its pool and its calls, or its effects drawn on the PC's GPU after 0.4.6). The tester: the review, root causes; YEBIS on the PC's GPU set aside. Designed 2026-11-25 ("YEBIS by its calls", `memory-model.md`): the pool as one rule, the engine's own stores told to the texture cache, the allocations' event kept and the research removed. The tester: "please proceed", the switches as recommended. Built (engine id 113): `BB_WATCH_YEBIS` for the pool, `BB_DMA_TELLS_TEXTURES` for the stores; the update and Map hooks, the nine heap hooks, the allocation records and the rebuild research removed, with `BB_WATCH_YEBIS_TEXTURES`, `BB_WATCH_YEBIS_HEAP`, `BB_YEBIS_ALLOCS`, `BB_GAME_HEAP_ALLOCS` and `BB_GAME_HEAP_RANGES`. Its run (`ab91610b`): no fault on an image in YEBIS's pool, the net clean; the pool's events marked 1,263-2,549 images in the minutes with loads (YEBIS's re-made targets among them) with no visible cost (post-load frames as good as 112's or better, YEBIS's targets still started empty); frame rate within the runs' spread. Closed, unless a later run disagrees |
| Beta 17's translations queued ahead raced on the translator's one global walker code buffer (`g_srt_codegen`, `flatten_extended_userdata_pass.cpp`): two compile threads emitting at once, Xbyak's `ready()` threw, and the game crashed at a loading screen; the research's audit of shared state had missed it (a text search). Also: the workers asked for 33 stages the GPU thread never took (a minute: 13,748 requests) | The tester's run of engine id 115 (`e11c75b9`), symbolized by the builder session | Fixed in engine id 116: walker emission under a lock; the audit redone from the compiled objects (the translator's 76 files and Sirit's 20: no other shared state written); jobs contained (exceptions caught, assertions thrown while a job runs); requests never taken not queued again. `BB_TRANSLATE_AHEAD=0` avoids the crash on 115 |
| The GPU cache check refused every cache: each stored shader record holds the resource-table walker's address in the process that translated it (`PersistentSrtInfo::Serialize` writes its struct raw, `walker_func` first), and reading a record registers the walker again at a new address, so `--verify-cache`'s round trip (added in engine id 115) could never give the same bytes for a shader with a walker | The tester's run of engine id 116 (`d09d8caa`): "translated differently by this engine (0x00000000006c079b_0: record format changed; 0 the same before it)", then a rebuild (673 shaders, 0 left out, 1.3 s) | Fixed in engine id 117: the address written as zero; the check accepts records written before 117 that differ only there (one span of at most 8 bytes, zero in the copy). No record version change: the loader never read the address. The builder session's review of 117: the struct's tail padding was written as held, and the tolerance accepted a zeroed span anywhere; engine id 118 writes the padding as zero too and accepts differences only in the struct's address and padding, the struct found in the record. Its run: the cache kept whole at the first start. Reverted with the check in engine id 119 (the records as before beta 17) |
| Translate-ahead requests the GPU thread never takes: in 116, eight pixel shaders' variants, about one to four requests a frame. The GPU command thread looks up no pipeline for the draws `FilterDraw` handles itself (fast clear elimination, FMASK decompression, resolve, no primitive type, depth/stencil copy); the prep workers prepared every draw, and a pass drawn with a pixel shader left bound from an earlier pass makes a variant only the workers see. Before beta 17 the workers' effort on those draws was wasted without a sign | The tester's run of engine id 116 (`d09d8caa`), the code read | Fixed in engine id 117: the workers leave those draws (`DrawSelectsPipeline`, the one test both sides use), counted on the Draw preparation line. 116's guess (a worker's view differing from the GPU thread's) was wrong. Its run: "never taken 0"; if not, another cause to find. 119's run (`ddfbb225`): the same eight, so 117's reading was the wrong one (the workers did leave 2,700 such draws a second, prepared for nothing before); the worker's and the GPU thread's specializations differ for those draws, the input not logged (registers, resource tables read ahead of the GPU thread, or the bindings' start). Small with the suppression (eight translations once a session); left, unless the tester wants it found |
| `bb-shader-lab` on Windows was a GUI-subsystem program: SDL3's pkg-config adds `-mwindows`, which bbgpu passed on to it (bb-probe's link drops it), so run from a terminal it printed nothing and the shell did not wait (no exit code); `--check-cache`, documented for players, never worked that way. bb-start reads it through pipes, which worked | The tester running `--check-cache` in PowerShell, 2026-11-26 | Fixed (built in engine id 120, confirmed by the tester's `--check-cache` in PowerShell): `-mwindows` removed from SDL3's imported target on Windows (`gpu/CMakeLists.txt`), so every program linked with bbgpu is a console one. Until then: `Start-Process ... -NoNewWindow -Wait -RedirectStandardOutput <file>` |
| Nothing shows between Play and the game taking the screen, shader building at the start included; it must work from the launcher and from the Steam shortcut alike | The tester, 2026-11-26 | Built in engine id 121: the start's splash in the engine (any start: launcher, Steam shortcut, Linux), a small centred window with the player's own `sce_sys/pic1` art (the tester's idea: nothing of the game shipped), the step and its progress; the game window hidden until its first frame. `BB_SPLASH=0`: off. The tester's 121 run: it showed only when the shader work began (it opened with the engine's window, after bb-start's work and the game's loading). Engine id 122: `bb-splash`, a program of its own that bb-start and `run.sh` start right after the game folder is checked, fed by a status file that they and the engine write (`memory-model.md`, "Engine id 122"); the game window shown at once if the splash goes |
| The "Building shaders" note in the top right corner; most games put such a note in the bottom right | The tester, 2026-11-26 | Built in engine id 121: bottom right (above the FPS counter when that is there), with a spinner at the end of the line |
| 46 of the shipped catalog's pipeline keys left out at every start (the tester's 121 log): their stages and key disagree on object motion (the key's motion attachment, the vertex stage's motion varyings, the fragment stage's motion output), so they never load; the lab's `--check-cache` counted only one of the three cases | The tester's 121 run, 2026-11-26 | Engine id 122: the warm-up removes such keys (counted in its "pipeline keys removed" line); `--check-cache` has the warm-up's whole rule |
| The launcher's Online page ran its three steps together in one paragraph | The tester's screenshot, 2026-11-26 | Engine id 122 (launcher): the steps as a numbered list, one a line, both launchers and all 13 languages; "Everyone needs game version 1.09." after them |
| The Linux package: the shader lab left out the catalog's 32 tessellation shaders (14 hull, 18 domain) that the Windows lab keeps; the packaged lab did not start (its RUNPATH named the build tree, not `$ORIGIN/gpu`), and the engine runs it at every start; the catalog came from the package template, not the repository; SDL's controller mappings shipped without their licence | The builder session's first Linux package, 2026-11-26 | Fixed in engine id 122 (Linux only): `LowWords` (the tessellation constants below 2^44) had no Linux version, so those states never loaded; now `mmap` at the same addresses. The lab's RUNPATH `$ORIGIN/gpu`; the catalog from `engine/shader-catalog`; the mappings' licence copied with them |
| A catalog shipped pipeline keys no player's warm-up can build: 46 in beta 17's (object motion), 55 in its merge with the tester's three caches; each player's first start removed them | The builder session's merge, 2026-11-26 | `--export-catalog` runs `--check-cache` on the cache first and leaves out every key it refuses (the warm-up's rules: records, SPIR-V, binding starts, object motion, layout); a catalog folder without SPIR-V is exported as it is. Takes effect at the next export (the tester's full cache); the merged catalog ships with its 55 for now (removed at the first start) |
| The shipped catalog covers what Lantern's two community catalogs had: 1,127 pipelines built at the start; the tester's play since October held more | The tester's three cache folders, 2026-11-26 ("whichever is best") | Merged in engine id 123 (bb9a4cd): 608 programs, 1,196 states, 1,416 of 1,471 keys built at the start, the same on an NVIDIA profile (`memory-model.md`). The tester's full-game cache, later, is the next update |
| Every new pipeline is built in its slowest form before a draw can use it: today whole and optimized (10-26 ms on AMD, some 55-80), the frame's non-skippable draws waiting for it (the 122 session's four pauses, 52-127 ms). Not known: what an unoptimized build or the pipeline library's parts and fast link cost on the tester's driver, and how often parts would be shared | The proposal's step 3; the tester, 2026-11-26 ("sure") | Engine id 124: `BB_PIPELINE_BENCH` (`memory-model.md`, "The pipeline measurement"). The tester's run (2026-11-26): a first-seen pipeline costs about 51 ms whole (90% under 84); unoptimized 99% of that (the quick build first is out); the pipeline library's parts 33 + 24 ms, their fast link 0.0 ms; with slots of their own at most 58% of the cache's pipelines would find both parts built. Engine id 125: the count in play. The tester's session (2026-11-27, `m22`-`m28`): 38 new pipelines, 1.74 s of waits; both parts built 17% of it, one 36% (a new fragment shader), neither 47%. The rework would remove about a third at best and leave most pauses, shorter: not done. The catalog (the tester's full-game cache, later) is the answer; `BB_PIPELINE_BENCH` and the count removed in the release candidate's cleanup |
| The engine id 126 copy-over zip did not start: `bb-probe.exe` exited with 0xC0000135 (a DLL not found) before its first line; the splash, from `tools\` with its own DLLs, ran | The tester's 126 run, 2026-11-27 | Not the zip: the tester had taken the DLLs out of `engine\out`, thinking they belonged in `tools\` only; copying `tools\`'s back fixed it. Both folders need their own (Windows looks for a program's DLLs in its own folder, then the system's; one shared copy means the helpers beside the game, where OptiScaler's DLL loads). The crash report to name 0xC0000135 plainly (L2's draft) |
| `bb-shader-lab --help` ended on an uncaught `std::filesystem_error` (exit 3): an unknown option was taken as a capture folder, and a folder that is not there was never caught | The builder session, checking the 126b zip, 2026-11-27 | Fixed (next engine id): an option it does not know, or one without its arguments, prints the usage (exit 2); a file error anywhere is said in one line (exit 3) |
| With d4r beside OptiScaler (v10 nightly as dxgi.dll), its DLSS output crashed on its first frame both ways (DLSS, DLSS w/Dx12): a read of address -1 in OptiScaler's own code (`dxgi.dll+0x2409cc`), on the recording thread. OptiScaler never found an NGX runtime: d4r's OptiScaler.ini names `NvngxPath=d4r\nvngx.dll`, and OptiScaler checks it with `std::filesystem::exists` from the working folder (its `Util::LoadProxyLibrary`), which was `engine\`, not `engine\out`; it then made the feature anyway. Its FSR 2.2 output ran without trouble before the switch | The tester's two 126 runs, 2026-11-27 | Engine id 127: bb-start runs the engine in its own folder (`engine\out`), as a game runs; the engine is given every path whole. The crash dump, which went to `engine\logs`, goes beside `logs\last_session.log` (`BB_CRASH_DIR`, from the launcher, else the package's logs). OptiScaler going on without a runtime is OptiScaler's. Confirmed in the tester's 127 run (2026-11-27): `_nvngx.dll loaded from override path: d4r\nvngx.dll`, OptiScaler's DLSS output made at once, about 9 minutes of play, 170-210 FPS, DLSS's evaluation 1.29 ms a frame on the GPU (1920x1080, DLAA, preset K), no OptiScaler error, a clean exit |
| The crash dump's path went through `getenv` and `CreateFileA`: a logs folder outside the system's code page (a user name in another script) got no dump | The builder session's review of 127, 2026-11-27 | Fixed after 127 (next engine id): the path read wide (`_wgetenv`), the folder and file made wide, the log line in UTF-8 |
| OptiScaler's compatibility mode was a switch (`optiscaler_compat` in the in-game menu, the launcher's `BB_OPTISCALER_COMPAT`): off took its protections away (NGX calls and every queue under one lock, the device made on a clean thread, one submit thread with its frame generation) and bought nothing, the first two costing next to nothing | The tester's question, 2026-11-27 ("does what exactly?"; then "yeah sure") | Engine id 128: always on with OptiScaler loaded; the switch, its menu row, the launcher's Engine switch and their texts gone; the menu's OptiScaler lines (loaded, version, compatibility mode on, its frame generation) kept, as the tester asked. An old `optiscaler_compat` line in config.ini is read as nothing |
| The in-game menu opened on Insert, OptiScaler's own menu key | The tester, 2026-11-27 ("may the menu open with End instead of Insert, for OptiScaler compatibility") | End is OptiScaler's frame-generation key (its `FGShortcutKey`; its others: Insert, Page Up and Page Down), Home ReShade's: the tester chose F1. Engine id 128: F1 or L3+R3; "Insert only" is "F1 only" (saved as `keyboard`, an old `insert` read as it); every text naming the key, in the menu, the launchers and their 13 catalogs |
| Engine id 128 in the tester's hands: F1, the OptiScaler section without its switch | The tester, 2026-11-27 ("I tested your changes and they work fine") | Confirmed |
| Engine id 130 in the tester's hands: the release's switches settled | The tester's 130 session, 2026-11-27 (`277d900f`) | No sign of the cleanup: about 5 minutes, AMD FSR, 80-448 FPS (median 214), the warm-up's 1,488 pipelines in 1.6 s, the surfaces' and the watched textures' lines as before, no texture-net finding, worst frames in play 22-27 ms (two over 50 ms at loads), a clean exit |
| The helpers' own folder (`engine\out\tools`, H1) guarded against a risk never seen (OptiScaler, put beside the game as dxgi.dll or winmm.dll, loading into the splash, the shader lab and the GPU check), at the cost of each folder's own DLLs (about 15 MB twice) and one real fault (the tester took the DLLs out of `engine\out`) | The tester, 2026-11-27 ("what risks? I'm not seeing any real and tangible risks here"; then "please revert them to a single folder. If something breaks I'd let you know. Don't build this change though") | Engine id 129, not built: the helpers beside the engine again; the old folder left for the tester to delete ("no need for the cleanup code, I'll delete the folder myself"; only test builds had it). `ToolPath`/`Paths.Tool` stay (one place each; the launcher's cache export keeps finding the lab on Linux); the GPU cache's identity unchanged (fed6a8570e2a2710). The builder checks meanwhile whether the helpers can be linked statically (no DLLs at all); the tester decides between the two after its report |
| Whether the helpers could be linked statically, a folder of their own without a second set of DLLs | The builder session's check, 2026-11-27 (the tester: "check if it can be done, but don't do it yet") | It works: the three link with no DLL but the system's `vulkan-1.dll` (the GPU check), run from a folder with nothing else, and the lab gives the same 1,416 of 1,471. Costs: Zydis and Zycore have no static archives in MSYS2 (built from MSYS2's sources; the engine library's Zydis users compiled with `ZYDIS_STATIC_BUILD`, which bb-probe shares); sizes 7.1 MB (splash), 32.0 MB (lab), 4.8 MB (GPU check), 44 MB against 20.6 MB for today's three programs (35.1 MB with their own DLLs), about 10 MB more in every compressed test zip (the programs ship with each build); the GPU check would not start on a PC without a Vulkan runtime; the lab carries OpenSSL's TLS client because the engine library links its online code (`bbgpu PUBLIC bbnet`), which a lab-only stub would cut; the package's licence list to follow the libraries linked, not the DLLs shipped. Licences: nothing new. The builder's order, if wanted: the splash and the GPU check first (SDL3 alone), the lab after cutting it off bbnet. The tester decides (the single folder, engine id 129, built meanwhile) |
| With the helpers beside the engine again (engine id 129), OptiScaler (v10, dxgi.dll) starts in the splash too: it loads its upscalers' DLLs, hooks D3D11CreateDevice, wraps the splash's swapchain in its own and subclasses its window with input hooks | The tester's 129 session, 2026-11-27 (`054a5566`) | Seen, harmless in this run: the splash showed and closed after 3.2 s as before, OptiScaler started once more in the game. The rest of the session: OptiScaler's output switched five times in its menu (DLSS through d4r, FSR 4 through its D3D12 bridge, and back), each a single 0.36-0.53 s pause and no fault; DLSS 1.29 ms a frame on the GPU, FSR 4 0.73-0.82 ms (1920x1080, native); 150-245 FPS in play; a clean exit. If OptiScaler ever breaks the splash, the splash alone moves (statically linked, SDL only: the builder's check) |
| The same few pipelines built in play every session, in places visited just before (the tester): 6 of 560 keys left out at every start ("a stage compiled after another fragment shader (binding start)", `--check-cache`). Play reuses a stage with no bindings at all beside any fragment shader (`StageSpecialization::operator==`) and saves the key with the permutation it finds first; the warm-up compared every stage's start. A rebuild's `--fix-keys` renamed the stages, play saved the same keys again | The tester, 2026-11-26; `--check-cache` on their cache | Fixed in engine id 120: one rule (`StageSpecialization::HasNoBindings`) for play, the warm-up, `--check-cache` and `--fix-keys`. Its run (`0cd9fa7c`): the rebuild (715 shaders, 0 left out), then 560 of 560 pipelines built at the start; no pipeline compiled in play in the places of earlier sessions (m21 four times, m24_01, m27, m32); 27 compiled in play on the way into and through m33, new in this session, and after it `--check-cache` (run from PowerShell, the console fix confirmed): 587 of 587 keys, the layout check 0 of 546 mismatched. Closed |
| First sight still costs a pipeline's build at the draw: an empty cache waits seconds in its first load and 150-480 ms frames at new areas (AMD); a warm cache 80-100 ms frames at new content (`m33`: 86.7 ms, 12 pipelines). Beta 17's translations ahead shorten the smaller part only | The tester after 120's run, 2026-11-26 ("patched the bleeding") | Researched 2026-11-26 (`memory-model.md`, "First sight"): keep the cache across profile changes; ship a catalog (coverage, other GPUs, start-up time); measure pipelines from parts before any rebuild of the slot layout. Lantern 0.9.43's catalogs tested (each survives the other vendor's profile) and merged by the builder session (1,173 keys, 1,159 built at the start). Built in engine id 121: the engine settles the cache at its start (new engine or profile: the states translated again, in parallel; the catalog seeded with this PC's profile), any vendor's catalog seeded, a refused pipeline dropped alone. Its run: the first start translates and seeds with the splash's progress, then every pipeline built at the start. Pipelines from parts: measured later |
| The engine's own DMA stores do not tell the texture cache: a DMA copy or fill the engine replays as a memory write on the CPU (`Rasterizer::CopyBuffer`, `FillBuffer`, up to about 6 a frame) tells the buffer cache (beta 12) only, so an image left to the calls over that memory (GX textures made with their data since beta 10, YEBIS's, Scaleform's) keeps its old contents until the texture net finds the change. YEBIS's linear updates (staged, then DMA-copied) were covered by beta 9's update hook marking the texture, by accident | The YEBIS review, 2026-11-25 (`vk_rasterizer.cpp` read) | Part 2 of "YEBIS by its calls" (`memory-model.md`): the images left to the calls over the destination marked written at the store. Built (engine id 113; `BB_DMA_TELLS_TEXTURES=0`: off). Its run (`ab91610b`): 27,106-80,989 stores told a minute, none over a texture left to the calls on this route; the gap stays closed for the cases it did not meet |
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

**Beta 18, the GPU's idle time: research (2026-11-27).** The tester's 129 session (`054a5566`, a
statistics window at 180 FPS, 1920x1080, OptiScaler's FSR 4): a frame is 5.56 ms; the GPU is busy 3.8 ms
of it (68%); the GPU command thread (stage A) works 3.28 us a draw over 1,010 draws and is idle 30.7%;
stage B is busy 70.8%; the game waits for its previous frame 1.46 ms in 80% of frames
(`WaitPreviousFrame`: it allows one frame in flight); at 1.5 of the 8.8 submissions a frame made at the
game's submission ends the GPU had already run out of work. So no stage is the limit: each is about 70%
busy, and they wait on each other around the game's one-frame loop (the game waits for its frame to
retire, the retirement waits for stage A, stage B and the GPU). Two cautions: the GPU profiler's marks
drain the GPU (`engine-map.md` 1.4), and the tester's runs have it on (Diagnostics "gpu"), so part of the
1.12 ms "between submissions" is the profiler's; and the GPU profile does not cover the presenter's own
submissions (its post pass, the swapchain copy), counted there as idle.

Where the loop's slack is decides the fix: (H1) stage A or B finishes the frame late (their cost a draw:
faster per draw, or stage B on two threads, `parallel-stageb.md` approach B); (H2) the GPU waits while
recorded work is held for the game's next submission point (submit earlier); (H3) the GPU's tail after
the frame's last submission (the upscaler, post-processing, UI) is the long part (GPU work, not the
engine's); (H4) the presenter's wait for the frame before (`BB_FRAMES_AHEAD=1`) adds to it. The timeline
(`BB_TIMELINE`, `bbport_timeline.h`) already has the game's submissions and waits, stage A's idle, the
Vulkan submissions and the GPU's progress; engine id 131 adds stage B's waits (20 us or more) and
`tools/timeline_report.py`, which says per frame how long the game waited, how long each stage and the
GPU sat idle, and how a frame's way from the game's frame end to its retirement splits between stage A,
stage B, the last submission and the GPU's tail. The builder's review of 131: the report's lookups
walked every interval for each frame, and overlapping waits would count twice; it now merges the
intervals and looks them up by bisection (0.3 s for 900 synthetic frames, 7 s before; the same output
when no waits overlap). The tester's run: `BB_TIMELINE` and
`BB_TIMELINE_START=120` in Extra variables, the GPU profile diagnostic off, a minute at the usual spot.

**Beta 18's research, read: the tester's 131 session (2026-11-27, log `54dfbbd3`).** 9 s of play
captured two minutes in: 1,839 frames, 400,000 events; 1920x1080, DLAA through OptiScaler and d4r,
uncapped, the GPU profile off. A frame is 4.74 ms median (211 FPS; the window's statistics 193 FPS).
**The GPU's queue is the slowest step.** It had nothing to do 2.5% of the time (0.12 ms a frame in 1.7
gaps, a lower bound); 86% of the submissions went in behind work still on the GPU; its work per frame
(one presenter submission to the next) is 4.63 ms median. Corrected after the tester's note
(2026-11-27): their GPU overlay read about 85% in these runs. The timeline measures whether the GPU
has work queued, not whether it is computing: about 15% of the time it waits inside its work
(barriers that wait for all earlier work, the end of each submission, render passes cut and begun
again, lower clocks at partial load). So "busy" here means never out of work, not flat out. So the 1.12 ms "between submissions" of the 129 profile
was the profiler's (its marks end the passes and wait) and the presenter's submissions (outside the
profile). The hypotheses:
- H1, stage A or B late: no. Stage A is idle 27% of the frame and stage B waits 38%. Stage A finishes a
  frame 1.36 ms after the game's frame end (about 32 of the frame's ~58 submissions come in its last 2 ms),
  but the GPU still has work queued then.
- H2, held work: no. The GPU runs dry at 1.7 points a frame, 0.07 ms median.
- H3, the GPU's own work: yes, in its general form. The last three submissions of a frame carry 54% of
  its GPU time (the upscaler, post-processing, the UI, the presenter's pass).
- H4, the presenter's wait for the frame before: no. Never needed in 1,839 frames.

The game waits for its previous frame 0.18 ms median (0.31 mean, in 68% of frames). Without that wait
its own frame is about 4.2-4.3 ms (4.0-5.5 across the capture). So at this spot GPU savings lift the
frame rate by about 10% (to about 235 FPS) before the game's own CPU frame becomes the limit; past that
the next limits are the game's threads and stage A (77% busy). The upscaler alone is 0.5 ms of the
difference: DLSS through d4r 1.29 ms a frame, FSR 4 0.73-0.82 ms (the 129 session).

The report, fixed with this reading: its GPU idle left out the presenter's frame submissions (PipeTask
5), so it counted the GPU idle while that work ran (0.18 ms median, now 0.07); its "frame's way" reached
into the next frame's submissions. It now takes submissions from both, says what share went in behind
earlier work, and follows a frame to the presenter's submission, then the GPU's queue and work: stage
A 1.36 ms, to the presenter's submission 0.04 ms, the GPU 2.28 ms, in all 3.74 ms median.

**Proposed for the tester's review (2026-11-27): beta 18 retargeted to the GPU's work.** Part 1, a
measurement build: the GPU profile without its distortion (marks only where passes end anyway, no
added waits, the presenter's submissions included), so per-pass costs add up to the GPU's 4.6 ms and
rank what to cut. Part 2, from that ranking: the engine's own GPU work first (the copy shaders, which
interrupt a render pass 37 times a frame, the barriers that wait for all commands, the presenter's passes),
then the game's hottest shaders (step 2). The ceiling at this spot is the 10% above.

**Two more captures (the tester, 2026-11-27).** The first one above had OptiScaler and DLAA on.
- A second spot (`98217950`, OptiScaler and DLAA off; the tester's GPU monitor at 85-86%): 5.08 ms
  frames, steady. The game almost never waits for its previous frame (0.08 ms a frame), stage A is
  idle 25%, the GPU's queue held work 96% of the time. The game's own frame (5.0 ms) is the limit.
- The lamp spot, standing still the whole run (`05080b94`, log `20261127_064043`, FSR 4.1.2 at
  1920x1080). Before a jump about 4.5 minutes in (the session lasted about 6): 204 FPS, 1,345 draws,
  stage A idle 2-5% (3.1 us a draw: 4.2 ms of a 4.85 ms frame), the game waiting 0.64 ms. After it:
  220 FPS, 1,296 draws, the game waiting 1.6 ms, stage A idle 2%. At the jump the game's own frame
  fell from about 4.15 to 2.9 ms; stage A, already near its limit, then holds the frame at 4.5 ms
  (H1, for stage A). Its time a draw: 1.4 us in draw packets, 2.5 us in all packets, 3.0-3.2 in all.
- The flip: in 91-97% of frames the game starts submitting only after the presenter prepared the
  frame before. But in play the game does not wait on flip events (`FlipEventQueue 0.0 waits`), so
  that order is the game's own.

So the limit moves with the scene and the settings: the GPU (DLAA through d4r), the game's own frame,
stage A. Two of them are the engine's to change: stage A's time a draw, and, if the game's threads run
on the CPU's efficiency cores, where they run (the jump, a 1.3 ms drop in the game's own frame without
a change of scene beyond 49 draws, fits a thread moved to a performance core). Engine id 132 measures
the second: every game thread's waits (10 us or more) and the kind of core it wakes on, the thread
that ends the frames, and the flip's way (the flip thread, the swap thread's present, stage A's waits
for a display buffer).

**Proposal, revised (for the tester's review):** beta 18 is stage A's cost a draw. If 132's run shows
the game's threads on efficiency cores, keeping them on the performance cores comes first: smaller,
and it lifts the game's own frame. The GPU-profile part above waits until the GPU is the limit again.

**Engine id 132's runs, read (the tester, 2026-11-27; logs `20261127_072134` and `_073834`, lamp spot,
FSR 4.1.2 at 1920x1080, standing still; captures at 120 s and 360 s).** The jump came at about 3 minutes
in the second run, so the first capture is before it and the second after.

| | Before (2,309 frames) | After (2,436 frames) |
|---|---|---|
| Frame, median | 4.76 ms (210 FPS) | 4.41 ms (227 FPS) |
| Draws a frame | 1,345 | 1,296 |
| Vulkan submissions a frame (the game's fenced submission ends) | 12 (11) | 6 (5) |
| The game waiting for its previous frame | 0.61 ms | 1.82 ms |
| The frame-ending thread waiting on its own condition variables | 2.23 ms | 0.83 ms |
| Stage A idle | 1% | 0% |
| GPU idle (between submissions) | 2% | 0% |
| A frame's way to retirement: stage A, the GPU | 2.37, 1.57 ms | 1.53, 1.96 ms |

- **Not the efficiency cores.** The game's threads wake on all 20 logical processors. The busiest one
  (about 2,900-3,150 waits a second, waiting 15-17% of the time) wakes on an efficiency core 50% of
  the time in both captures, the job workers 67-69%, the frame-ending thread 21-30%, and 22-23% of
  frames end on an efficiency core in both. Nothing moved at the jump; steering the game's threads
  is not the lever here.
- **The jump is the game's.** At it the game makes about half as many fenced submissions a frame,
  draws 49 fewer things, and its frame-ending thread waits 1.4 ms less on its own threads. Something
  in the scene stops: work the game syncs with the GPU several times a frame.
- **The pace-setter at the lamp spot is the game's main thread** (corrected the same day, from the
  frames themselves; it was first read as stage A). The busiest game thread (the main thread: it
  makes most of the GX resources) runs 4.0 ms of a frame before the jump and 3.75 ms after, and
  waits 0.81 and 0.68 ms; only 0.30 and 0.40 ms of its waiting falls while the frame-ending thread
  waits for the engine. The frame-ending thread waits for the main thread's handover (2.2 ms, then
  0.8 ms after the jump), sends the frame's 70-odd command buffers, and waits for the engine on the
  way (0.6, then 1.8 ms), mostly while the main thread works on. Stage A never runs out of work
  (idle 0-1%) because it runs at the same pace, not because it holds the main thread: a faster
  stage A gains about 0.3 ms a frame at most here. Stage A takes 3.0-3.2 us a draw (draw packets
  1.4 us, all packets 2.5 us). The GPU's queue never runs dry either; the tester's overlay reads
  85-86%.
- **Efficiency cores tested directly:** in frames where the main thread woke mostly on efficiency
  cores the frames were no slower (correlation -0.14 before, 0.10 after). Not the lever.
- **The flip is not a factor.** PrepareFrame to the flip taken 0.02 ms, the game told 0.03 ms later,
  the swap thread's present 0.32-0.33 ms on its own thread, and no stage A waits for a display buffer.

**Proposal, from these (for the tester's review, revised the same day):** most of the frame at these
spots is the game's own main thread. What the engine still owns there is small (stage A, about 0.3
ms; the GPU's waits inside its work). One measurement can still find a lever: samples of where the
main thread spends its 3.9 ms (the hitch reporter's stack sampling), split between the game's code
and the engine's (HLE calls, the runtime's sync primitives, memory and file calls). If the engine's
share is large, beta 18 cuts it; if small, the performance work closes for 0.4.6 and the release
candidate follows. Stage A's cost a draw and the GPU profile without its distortion stay in the
register for when they are the limit.

**The other area (the tester, 2026-11-27).** There (`98217950`, overlay 85-86%) the game's own frame set
the pace (5.0 of 5.08 ms, almost no wait for the engine): most likely the same main thread with more to
do. The efficiency-core question for it is answered by the lamp spot's frames (no effect), so no 132
capture there is needed for that.

**The diagnostics runs (the tester, 2026-11-27; logs `20261127_170032` lamp spot and `_170807` the other
area; FSR 4.1.2 at 1920x1080, GPU profile and render stage timings on).** The render stage timings slow
stage B to 96% busy (64% without them), so stage A waits for it (5.3% and 12.1%) and these runs' frame
rates sit below the usual (206 and 181 FPS): their breakdowns hold, not their limits.
- GPU, profiled (the profiler's marks add idle between submissions): the lamp spot 4.87 ms a frame,
  of which 0.79 ms idle between submissions (11 gaps), the G-buffer passes about 1.0 ms, FSR 4's
  evaluation 0.50 ms, the copy shaders 0.15 ms (57 a frame); the other area 5.47 ms, idle 1.30 ms (19.5
  gaps), the G-buffer about 1.6 ms (more passes, begun again more often), FSR 0.47 ms, the copy shaders
  0.17 ms (61). So about 4.1 ms of GPU work a frame in both.
- Stage A a draw (the draw packet's parts): 1.56 us at the lamp spot (constants 0.63, pipeline 0.28,
  packet 0.22, inputs 0.13) and 1.79 us in the other area (0.73, 0.33, 0.22, 0.19); all packets 2.9 and
  3.2 us a draw. The constants step is the largest known part: about 0.85-0.95 ms a frame at 1,300
  draws. A candidate if stage A becomes the limit.
- Stage B a packet: 1.74 and 2.42 us (draw 1.36 and 1.99, of which binding 0.46 and 0.74, textures 0.24
  and 0.42, ObtainBuffer 0.26 and 0.37); the upscaler's dispatch takes 169 us of stage B a frame.
- The other area makes 17.8 Vulkan submissions a frame (16.8 at the game's fenced submission ends)
  against 10.3 at the lamp spot.
Neither diagnostic shows how the main thread's time splits between the game's code and the engine's;
that stays the open measurement.
The measurement needs no build: `BB_SAMPLE_THREADS` (`bbport_sampler.cpp`, in the engine since 0.4.5's
loading work) samples named threads every 2 ms and reports every 5 s the share in game code, in each
host module (the engine is `bb-probe.exe`; waits show as `ntdll`/`KernelBase`) and the hottest sites.
The tester's run (2026-11-27): `BB_SAMPLE_THREADS=main,GXRenderThread,GXWorker`, `BB_SAMPLE_TOP=12`, the
GPU profile and render stage timings off, the slower spot then the lamp spot past its jump. The
engine's sites are named afterwards from 132's executable (the builder).

**The sampler run, read (the tester, 2026-11-27, log `20261127_173510`; the lamp spot at 205 FPS, then
221 after its jump; the slower spot at 177-181 FPS, about 1,400 draws, stage A idle 11-13%, the game
waiting 0.15 ms a frame).**
- The main thread: game code 62-67% (of which about 10% at one place, `+0x21b8890`..`+0x21b8899`: a
  spin in the game's code), waiting in the system 24-30% (many sites: the game's job waits), the engine
  6-8% (much of it the game's memcpy into the upload area through the runtime, `+0x26adc05`), ucrtbase
  1-2%. The engine's own overhead on the main thread is small.
- GXWorker:1-5 wait 82-83% of the time; GXRenderThread waits 83-84%, mostly for the main thread's
  handover (`+0x21685d1`: 42% at the lamp spot, 64% at the slower spot) and at the lamp spot for the
  engine (`+0x1073914`, 19%, against 3% at the slower spot).
- The main thread's waits (132's timeline, 10 us or more): about 14 a frame, semaphores median 28 us
  (8.5 a frame), condition variables median 55 us (3.7), mutexes 14 us (1.7).

So at the slower spot no thread is saturated: the main thread hands out short jobs and waits for each,
and every wait is a block and a wake on Windows. The runtime's semaphores and condition variables
block at once (`runtime_sema.c`, `runtime_mutex.c`). First proposed: a test switch, `BB_WAIT_SPIN_US`,
spinning before blocking. The tester asked for the fix at the source instead (2026-11-27), and the source
is the runtime itself:
- **Semaphores** (`runtime_sema.c`): one process-wide lock for every semaphore operation of every
  thread, the id found by walking a list under it, and `sem_signal` waking the waiter while still
  holding it: the woken thread, inside `SleepConditionVariableSRW`, must take that lock back, so it
  blocks again until the signaller lets go. Two wakes a handoff, and every other thread's semaphore
  work queued behind the same lock (the job workers alone make tens of thousands of these a second).
- **Read/write locks** (`runtime_rwlock.c`): every lock and unlock takes one process-wide lock, walks a
  list and allocates or frees a holder record under it.
- The mutexes and condition variables are per object over SRW locks and Windows condition variables:
  not part of it.
**Proposed as beta 18 (for the tester's go-ahead):** each semaphore and read/write lock with its own
state, found without a shared lock; a semaphore's waiters woken after its lock is let go, each sleeping
on its own word (wait on address), so a handoff is one wake; an atomic fast path when nobody waits.
Kept: FIFO waiters, timeouts, cancel and delete waking every waiter, the freeze report's descriptions
(`tests/test_sema.c`, `runtime-test`). Measured with the same sampler run at the slower spot. A spin
before blocking only if latency is still left afterwards.

**Thread priorities, raised by the tester (2026-11-27).** Redesigned the same day at the tester's direction
as one system instead of per-CPU cases: every thread gets a role (Pacing, Chain, Frame, Background,
Audio, Service) declared once, one module maps roles to the system's own flags (priority, QoS, CPU sets
by one topology rule), every wait parks on its own word, hot-path counters per thread, and a `Roles:`
line in the log; see the design. Earlier reading, kept for the record: researched with the semaphores
into one design,
[handoffs-and-placement.md](handoffs-and-placement.md), for the tester's review before any code. A
correction to the first reading: the engine's threads above normal are stage B, the 3 recording threads
and the 4 copy threads (mostly asleep); the 7 draw preparation threads run idle and stage A runs normal,
like the game's ~60 threads. So the raised threads do not crowd the main thread off the performance
cores; Windows' wake placement (an idle core first, the efficiency cores most often) is the likelier
reason. The design: the frame-critical chain (the game's main thread and GXRenderThread, stage A, stage B,
the recording threads) above normal and, on hybrid CPUs with 4 or more performance cores, on those
cores by CPU sets (soft); everything else as now; measured on and off.

**Beta 18's first run, read (the tester, 2026-11-27, log `20261127_195807`, engine 133; FSR 4.1.2 at
1920x1080, uncapped, frame statistics and `BB_SAMPLE_THREADS=main`; runs 2 and 4 of the measurement,
then fast travel and an area with new shaders; runs 1 and 3 deferred).** Against 132's sampler run
(`20261127_173510`, same PC, same spots):

| | 132 | 133 |
|---|---|---|
| Lamp spot, 1,345 draws | 201-207 FPS, stage A idle 4%, the game waiting 0.8 ms a frame | 215-221 FPS, stage A idle 0.3%, the game waiting 2.6 ms |
| Lamp spot past its jump, about 1,300 draws | 218-222 FPS, stage A idle 1.5%, waiting 1.7 ms | 225-228 FPS, stage A idle 0.6%, waiting 2.55 ms |
| Slower spot (1,400 draws in 132, 1,332 here: the same main-thread profile, `+0x4935c4a` its top wait) | 177-181 FPS, stage A idle 11-13%, waiting 0.15 ms | 200-205 FPS, stage A idle 3%, waiting 1.3 ms |
| The main thread's spin at `+0x21b8890`/`+0x21b8899` | 9-13% of its samples | gone; the fork-join wait shows as the engine's pause loop and yields, 5-7% |
| Stage A a draw at the lamp spot | 3.05-3.26 us | 2.85-3.09 us |

- **The limit moved from the game to the engine at both spots.** The game now waits for the engine
  1.3-2.6 ms a frame, and stage A is idle 0.3-3% of the time: its cost a draw (about 3.0 us, about 4 ms a
  frame at 1,345 draws) sets the frame rate now. The slower spot's part of the gain may also include its
  5% fewer draws.
- **The fork-join waits:** 17-19 a frame, 0.47-0.61 ms a frame, 11% of them past 50 us at the lamp spot
  (3% to 8% elsewhere); 1,250-1,700 yields a frame that find nobody, so on this CPU the workers never wait
  for a core and the wait is their own work. The main thread no longer spins without `pause`.
- **Placement:** the chain's 7 threads use 2.8 cores; the game's 62 Frame threads use 3.7-4.0 cores and
  wake on the performance cores only 40-43% of the time in play (84-88% in menus); audio 44-48%. Whether
  the main thread and `GXRenderThread` are among those that wake on the efficiency cores isn't in the
  line (it sums per role).
- **Loading and streaming:** the first load 3.57 s (132: 3.53 s); two fast travels 1.84 s and 3.32 s,
  no frame over 25 ms in the 2 s after either. Background threads used no CPU in play (no backlog).
- **The new-shader area:** two hitches (64 ms and 40 ms) where draws waited for pipelines being
  compiled (8 waits, 79 ms, the longest 16 ms; 13 compiles, 82 ms). The compile threads ran as before
  (normal priority, now Frame) and the burst threads had no backlog, so this is the known first-sight cost,
  not beta 18's.
- **No engine thread without a role** was logged.

**Beta 19's run, read (the tester, 2026-11-27, log `20261127_211019`, engine 134; frame statistics, no
sampler; the tester's overlay: GPU 96% at the lamp spot, 91% in the slow area, CPU 54%, about 100 W).**
Against 133's stage A profile run (`20261127_201857`, the same spot, the stage A sampler on) and 133's
first run (`20261127_195807`, the main thread sampled):

| | 133 | 134 |
|---|---|---|
| Lamp spot, 1,345 draws | 224-229 FPS; stage A 2.9-3.0 us a draw, idle 0.4-0.9% | 228-232 FPS; stage A 2.4-2.7 us a draw, idle 10-12%; the game waiting 1.5-1.9 ms |
| Lamp spot past its jump, 1,296 draws | 228-233 FPS | 232-235 FPS |
| The 1,780-1,795-draw view | 175-182 FPS (main thread sampled) | 190-194 FPS; stage A idle 5-6%, stage B 88% busy, the game waiting 2.3-2.6 ms |
| The slow area (1,332 draws in 133, 1,286 here) | 200-205 FPS (main thread sampled) | 210-217 FPS; stage A idle 20%, stage B 80% busy, the game waiting 0.4-0.5 ms, 9.7 Vulkan submissions a frame |
| Stage A's packets ("all packets", a draw) | 2.43 us | 2.03 us |

- **The lamp spot is the GPU's now** (96% on the overlay, stage A idle 11%). Part of stage A's drop is the
  statistics' own clock, which only runs with the statistics paid; the switch runs would separate it.
- **The slow area has no single limit:** the GPU 91%, stage B 80%, stage A 80%, the main thread 0.87 cores
  (0.43 ms a frame at its fork-join waits). It makes 9.7 Vulkan submissions a frame against 4.4 at the
  lamp spot: the game syncs with the GPU mid-frame, so each sync waits for the whole chain (stage A,
  stage B, the recorders, the GPU). The chain's latency is what is left there.
- **The heavy view is the engine's:** stage A idle 5%, stage B 88% busy. Stage B's cost a draw is the next
  CPU limit after stage A's.
- **The pending tables:** no read was noted (the dumps write 3 ranges no read touches); GPU writes listed
  145-160 thousand a second, 31-34 thousand to the table while the list is full (stage B more than 64
  writes behind). A full list makes each constant lookup scan 64 entries: to check in the next profile.
- **Late wakes:** 0 in play but one window (1), 2-4 in the menus and the load: the counting race the
  builder found, counted exactly from engine id 135.
- **Placement:** the main thread woke on the performance cores 69-80% of the time, `GXRenderThread` 38-79%.

### Performance, in 0.4.6 (moved in, 2026-11-27)

The tester (2026-11-27): "I wanted the Performance part to mostly be completed in 0.4.6!" and, of the
release candidate's list, "they don't close the 0.4.6". These come before phase D, as betas of their own.
All of them are ours (HW's measurements and plans). The evidence for each is in
[performance.md](performance.md) and [engine-map.md](engine-map.md); the plan they come from is
[archive/plan-0.4.5.md](archive/plan-0.4.5.md). The newest figure (the tester's 129 session,
`054a5566`, 1920x1080 with OptiScaler's FSR 4): 4.90 ms of GPU work a frame, of which 1.12 ms is the GPU
idle between submissions (12 gaps a frame), the upscaler 0.83 ms, the copy shaders 0.22 ms (56 a frame);
the GPU idle is now the largest item after the upscaler (0.12 ms in 3.3 gaps at the lamp spot in 0.4.5). The 131 timeline (beta 18's research, above) shows that idle
was mostly the profiler's: without it the GPU is busy 97.5% of the time.

| Item | From | Scheduled |
|---|---|---|
| Shader code (step 2): rank the game's shaders by GPU time and check the top ones with `bb-shader-gpu-stats`: registers and occupancy, inputs still interpolated by GCN's formula (321 of 1,201), the legacy multiply. The Hunter's Dream as its scene (DLAA 0.96 ms, the G-buffer pass 0.74 ms, the lighting passes) | Plan step 2 | After 0.4.6 |
| Vertex shader contraction with an invariant position (`shader-5`, about 4% of vertex code) | Plan step 2 | With step 2 |
| The native compiler's step 2 (control flow without goto variables), measured in the lab first | Plan step 2 | With step 2, if the hot shaders gain |
| The barriers that remain (about 23 a frame at the lamp spot): a measurement beta with reads tracked exactly (`BbToggle::CoarseReadTracking`) and barrier counts by what they wait for | The rest of 0.4.5, item 2 | After 0.4.6 |
| The GPU idle between submissions (about 0.12 ms a frame in 3.3 gaps): why the GPU runs dry there, and whether submissions can overlap | The rest of 0.4.5, item 3 | Closed by beta 18's research (the 131 timeline): without the profiler the GPU has nothing to do 2.5% of the time (0.12 ms a frame); 86% of submissions go in behind work still on it. Nothing to gain |
| `mem-4`, `upscale-2`, `upscale-3` (0.1 ms at most; the second only at reduced upscaler presets) | The rest of 0.4.5, item 6 | When nearby work passes them |
| The CPU (step 4): stage B on two threads (`parallel-stageb.md`, approach B), with `idle-1`, `pipe-1`, `asc-2` and `rt-1` | Plan step 4 | When a 6-core CPU or a frame rate above about 230 FPS makes it the limit |
| A GPU timer at pass ends that doesn't wait at each label | Plan, "Later, if at all" | When the GPU is the limit again (the 131 runs showed it isn't at the tester's spots without DLAA through d4r); was proposed as beta 18's part 1 before the threads' reading |
| **Beta 18: thread roles and handoffs** ([handoffs-and-placement.md](handoffs-and-placement.md)): the roles module with every engine and game thread on it, the runtime's semaphores and read/write locks rebuilt on a parking primitive, the game's fork-join barrier with `pause` then a yield, per-thread hot-path counters (`memory_calls` dropped), the `Roles:` line; temporary switches `BB_THREAD_ROLES`, `BB_RUNTIME_SYNC`, `BB_TEST_CPUS` | Beta 18's research, 2026-11-27 | Beta 18 (design agreed 2026-11-27; built on the tester's go: part 1 4fe1d91, part 2 with the barrier fix and the counters after it; engine id 133) |
| Beta 18's runs: the slower spot with `BB_THREAD_ROLES=0`, as built, then `BB_TEST_CPUS=8` roles off and on; the lamp spot as built; each with the frame statistics and `BB_SAMPLE_THREADS=main` (and `BB_GAME_FIXES_SKIP=job_barrier` once, for the barrier fix alone). Watch: hitches against the burst threads' raises (the `Pipelines:` burst lines), since Background and Assist run idle in play; the file read-ahead (`bb:Prefetch`) runs idle and eco in play now (below normal before), so streaming hitches without a loading screen would point at it | Beta 18's design, measurement; the builder's review of 133 | The tester, on 133 (built 2026-11-27: every test passes under Wine and natively, the cache id unchanged, the dispatcher's sites and hash checked against the 1.09 eboot) |
| Stage A is the limit at both of the tester's spots on 133 (idle 0.3-3%, the game waiting 1.3-2.6 ms a frame): its cost a draw, about 3.0 us, is what the next beta should cut (beta 20's item, its condition now met) | Beta 18's first run, read | Proposed to the tester as the next beta (2026-11-27) |
| How far stage A's work can lift these spots: the GPU's own work is about 4.1-4.2 ms a frame at both (the 132 diagnostics runs' profile, FSR 4.1.2 at 1920x1080), so about 240 FPS is the ceiling there: about +10% at the lamp spot (219 FPS) and +17% at the slower spot (204 FPS) before the GPU is the limit; more on CPUs slower per thread, where stage A's 3 us a draw is longer | Beta 18's first run, read; the 132 diagnostics runs | Told to the tester with the plan (2026-11-27) |
| Stage A's work splits by file: the draw step (`vk_rasterizer.cpp`, about 1.5 us a draw: constants 0.63, pipeline 0.28, packet 0.22, inputs 0.13) is outside the GPU cache's identity; the packet decode (`liverpool.cpp`, about 1.1 us a draw for the non-draw packets, plus the register checksum on every register word) is inside it, so changing it starts every player's cache settle once | Research for the next beta, 2026-11-27 | The tester (2026-11-27): fine during this development cycle ("we have the shader catalog"), so the decode tier may change the GPU cache's identity; the builder's check of the identity becomes "changed only where the beta says so" |
| A stage A profile before its design: `BB_SAMPLE_THREADS=shadPS4:GpuCommandProcessor`, `BB_SAMPLE_TOP=40`, frame statistics, at the lamp spot on 133 (no new build), the hot sites named by the builder from 133's executable; the overlay's GPU use noted | Research for the next beta, 2026-11-27 | Done (log `20261127_201857`, the lamp spot at 226-229 FPS without the main thread sampled): the statistics' per-packet clock about 10%, the pending tables 9.5%, the draw pipe's hand-over 4.6%, the submission queue's lock 4.2%, unplaced waits 4.8%, the loop bodies about 40%. The design: [stage-a-cost.md](stage-a-cost.md) |
| **Beta 19, revised: the GPU command thread's cost a draw (statistics without a per-packet clock, the hand-over without a fence a draw, the pending tables asked and filled less, the submission lock held only for the push, no heap per fence, small ones), with beta 19's engine waits and the per-thread `Roles:` entries** ([stage-a-cost.md](stage-a-cost.md)) | The stage A profile, 2026-11-27 | Built: engine id 134 (2026-11-27; the tester: "go ahead with beta 19") |
| Beta 19's runs: the lamp spot and the 1,780-draw view with the frame statistics (no sampler): FPS, stage A's microseconds a draw and idle, the draw pipe's late wakes (expected 0), the pending tables' counts; the same with each switch off once (`BB_STATS_EVERY_PACKET=1`, `BB_PIPE_HANDOVER=each`, `BB_PENDING_TABLES=old`, `BB_FRAME_POOL=0`, `BB_ENGINE_PARK=0`); a stage A profile with `BB_SAMPLE_TOP=200` for beta 20; runs 1 and 3 of beta 18 when there is time | stage-a-cost.md, measurement | The tester, on 134 |
| The Frame threads wake on the performance cores 40-43% of the time in play: per-thread shares for `main` and `GXRenderThread` in the `Roles:` line, then the measured option of Frame (or those two) preferring the fastest class | Beta 18's first run, read | With the next beta's line; the preference only if a run shows it helps |
| **The design gap (beta 18's principle 4 applied to hand-overs, not locks):** the render chain waits on locks lower roles hold: the draw preparation's (idle threads, 208 KB copies and wakes under it; about 12% of stage A), the submission queue's (about 5%); copy and compile threads waited on without a raise. Found by the profile of both stages on 135 (`20261127_214212`, named by the builder) | The tester ("this tells me there's been a design gap"), 2026-11-27 | Principle 4 corrected in handoffs-and-placement.md; the fix at its source in [chain-waits.md](chain-waits.md) (beta 20, part 1) |
| **Beta 20 (proposed): the engine's chain a draw:** stage A's remaining 40% (the packet loop, the draw step), stage B's cost a draw (88% busy in heavy views), the pending write list's lookups when it is full; from a profile of both stages | Beta 19's run, read | Became beta 20 as designed in chain-waits.md (the profile showed the locks first); the packet loop and the draw step themselves stay for a finer profile |
| **Beta 20: the render chain never waits on a lower role, and its cost a draw** ([chain-waits.md](chain-waits.md)) | The tester ("go ahead with beta 20"), 2026-11-27 | Built: engine id 136 (items 1-9; 10 and 11 not built, reasons in the design's "As built") |
| Beta 20's runs: the three spots as in the 135 profile run, frame statistics on (no sampler): FPS, stage A's microseconds a draw and idle, stage B busy, the `Roles:` line's "waited on lower roles" (expected none on the chain), the pending tables line (the filter's share); then the same profile run (both stages sampled) to check no chain wait lands on a lock; each switch off once where a figure moves (`BB_PREP_HANDOVER=old`, `BB_SUBMIT_QUEUE=locked`, `BB_LOCK_INHERIT=0`, `BB_RAISE_ON_WAIT=0`, `BB_PENDING_FILTER=0`, `BB_PIPE_COUNT=fenced`) | chain-waits.md, measurement | The tester, on 136 |
| **Beta 20's first run (136, `20261127_224829`):** the busy view +4%, the slow area +2-3%, the lamp spot at the GPU's limit; stage A idle 25-37% (from 6-20%), stage B 68-84% busy; the game waits longer for the engine; 1-1.2 cores less CPU; the chain's waits on lower roles under 0.2 ms a second (from about 17% of stage A) | The tester's run, read | Recorded in chain-waits.md; the GPU is now the limit (to confirm with the overlay's GPU use) |
| The page filter answered 8-27% of lookups in play, and stage A's draw step took about 0.1 us a draw more than in 134: one live wide write (over 2 MB) disabled it | Beta 20's first run | Fixed in 138 (wide writes checked one by one); the next run's pending tables line shows whether the 64 KB pages are too coarse as well |
| **Beta 20 on 138 (`20261127_230858`):** the filter answers 97-99% of lookups, the draw step back below 134's cost; the lamp spot (98% GPU) and the busy view (98%) at the GPU's limit, the slow area 94-95% (the GPU idle about 5%); the render chain nowhere the limit on this PC | The tester's run, read | Recorded in chain-waits.md |
| The slow area's GPU gaps: the game waits for GPU results mid-frame (3.8-4.0 fences a frame at its submission ends), the frame goes out in 4.8-5.0 submissions, and the GPU has run dry at about one in two: the round trip from the GPU's finish through the game, stage A, stage B and the recorders back to the GPU. To see each gap's cause: a timeline of the slow area (`BB_TIMELINE`) and the GPU profile's "between submissions" time | The 138 run | Asked of the tester; the design of beta 21 from it |
| The GPU's own work at the GPU-bound spots: the last profile (132) had the upscaler (FSR 4) about 0.48 ms a frame, the copy shaders 0.15 ms (58 barrier-and-dispatch pairs a frame); the 138 log has about 1,500 barriers a second waiting for all commands and about 9,000 copy-shader dispatches a second that end a render pass. A fresh `BB_GPU_PROFILE` run at the three spots ranks what the engine adds | The 138 run | Asked of the tester; beta 21's GPU part from it |
| **The GPU profile and slow-area timeline on 138 (`20261127_231846`, `BB_GPU_PROFILE=1`, `BB_TIMELINE`):** GPU ms a frame 4.34 at the lamp spot, 5.18 in the busy view, 4.43 in the slow area (the profiler adds about 0.1-0.15). The largest: the upscaler (FSR 4.1.2, input 1920x1080, so anti-aliasing only) 0.50-0.51 everywhere; the GPU between submissions 0.19 (3.4 a frame), 0.35 (5.5), 0.49 (6.2): 60-80 us a switch; the game's 6-target geometry pass in about 20 pieces a frame. The timeline (10 s in the slow area, 227 FPS): the GPU idle from the CPU's side 0.08 ms a frame (a lower bound) and 88% of submissions reached the GPU while it was still busy, so the gaps are mostly the switch between submissions (the work in flight finishing, caches, the next one starting), not the GPU waiting for the CPU. Stage A idle 40%, the game waiting 41% of the frame | The tester's run, read | Beta 21's design (proposed): fewer submissions a frame (held by the GPU's time left, not by count), passes suspended and resumed across the recording threads' cuts, the gap label split by the next submission's reason |
| **The GPU writes the game's fences (beta 21 research, [gpu-fences.md](gpu-fences.md)):** about 600 end-of-pipe fences a frame, all written today by the signal thread after their submission ends; the switches come from tying GPU feeding to that. `vkCmdWriteBufferMarker2AMD` waits for the work before it, keeps fences in order and does not hold later work (the spec), 32-bit; AMD and Mesa have it, NVIDIA not known to; the core barrier-and-write fallback would drain the GPU 600 times a frame (dropped); passes across the cuts: re-measured afresh (the tester: a lot changed since 0.4.4), a switch with no cuts inside a pass in the counting build, then pass-end cuts or suspendable passes if it pays. Open: where the fences live (in the GX import, no VRAM copy), their kinds, whether the game blocks on them mid-frame | The tester ("do a little more research"), 2026-11-27 | A counting build first (no behavior change), then the design |
| The copy shader's dispatches: 0.15-0.16 ms a frame (56-65 a frame), already merged into fewer dispatches and barriered only where needed: their time is the copying | The 138 GPU profile | Not planned |
| Pending write list: more than 16 wide writes live for a moment in the busy view (the scans left): the limit to 64 | The 138 run | With beta 21 (small) |
| The submission counters' lock: still about 20 contended takes a second by the chain (0.1 ms a second in all) | Beta 20's first run | Not worth more now; atomics instead of the lock if it ever grows |
| **Wine and Proton: the process-wide fence is a stub** (the builder's probe of 136: `fixme:virtual:NtFlushProcessWriteBuffers stub`), so beta 20's plain packet count could lose a sleeping stage A's wake, and its sleep had no timeout | The builder's review of 136, 2026-11-27 | Fixed in 137: the fenced count under Wine, stage A's sleep looking again every 2 ms (a missed wake counted, expected 0) |
| The texture state a draw reads (the image's barrier state, about 2% of stage B): kept where the draw reads it hot, if a finer profile shows the cold read is not the bind's own | chain-waits.md, item 10 | After beta 20's profile |
| A profile of stages A and B on 135: `BB_SAMPLE_THREADS=shadPS4:GpuCommandProcessor,bb:DrawRec`, `BB_SAMPLE_TOP=200`, the slow area and the heavy view (no new build) | Beta 19's run, read | Done (`20261127_214212`): see chain-waits.md's table; the design proposed to the tester |
| **Beta 19 (the original part, built in 134):** the engine's own waits (stage A's, the draw pipe's, the recorders', host copies') on the parking primitive with their spins set by role; pool sizes tuned from beta 18's `Roles:` lines; anything beta 18's runs ask for (Frame threads on the fastest class if measured better) | Beta 18's design, staging | Beta 19, after beta 18's runs |
| **Beta 20, if betas 18 and 19 make stage A the limit:** stage A's cost a draw, starting with its constants step (0.63-0.73 us a draw, about 0.9 ms a frame at 1,300 draws), then its pipeline and inputs steps | The diagnostics runs, 2026-11-27 | After beta 19, decided from its runs |
| The last-level-cache criterion in the roles' core rule (Ryzen with two core complexes) | Beta 18's design | When a tester with such a CPU can measure it |
| The copy shader's ranges served from the engine's snapshots, so no copy runs | Plan, "Later, if at all" | Later, if at all |
| `engine-map.md` section 6: the first test session, the small measurement code and what the agents could not determine | `engine-map.md` | Gone through with step 2 and step 4; what was answered since is marked there then |

Not reopened: `seg-1`, `renderer-3`, `renderer-4`, `renderer-6` and the refuted ideas
(`engine-map.md` section 5); the October hitch leads (the game's job threads, the online summon,
the PC memory model), fixed, removed, or not the cause in 0.4.4's logs.

### Features and fixes, after 0.4.6

Where each comes from: ours unless named. Lantern's (Lantern 0.9.39, reviewed in community-fixes.md):
Anti-Lag 2, the optional lighting effects, lamp to lamp; Lantern's DLSS audit: the passes left
unjittered. bbport 0.7's: SMAA and FXAA, FSR 1, GT-VBAO, the shader recipe pack, video skipping, invert
mouse X. bbhost's (Kyoskii's findings): the SSR patch's tuning, the message boxes' confirm delay, the
online probe's timing line.

- **Last of all: frame generation on AMD and NVIDIA** (Decisions): AMD's through its SDK (the design
  above), NVIDIA's through Streamline with its DLSS moved to Streamline, Reflex with it.
- The splash statically linked in a folder of its own, if OptiScaler beside the game ever breaks it
  (the builder's check: SDL3 alone, about 7 MB).
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

- **The runtime test under Wine** (the builder, engine id 132, 2026-11-27): `runtime-test` stops at
  `tests/test_runtime.c:193`, a direct-memory map (`map(&y, length, 3, 0, a, 0)`). Its first run under
  Wine; nothing in 132 touches mapping, so most likely Wine's placeholder mapping
  (`MapViewOfFile3`). The game runs on Windows itself; looked at if a Wine run ever matters.
- **The launcher's texts on screen:** at 100% and 150% scaling, in English, German and Japanese,
  in both Classic and Modern layouts, plus the in-game menu's (F1) Enhancements section.
- **Launcher behaviour:**
  - Suggested settings with two monitors;
  - a Share code exported and imported on another PC;
  - Changed switches with Extra variables set;
  - the fixed-refresh frame lock greying out.
- **Online: joining another world** (summoned with the Small Resonant Bell, or invading). Hosting
  is checked: two invasions on 2026-11-03.
- **Untested for want of hardware:** FSR 2.3.4, HDR output. (AMD FSR 4.1 on Radeon: runs on the tester's RX 9070 XT since 2026-11-10, FSR 4.1.2 at native resolution, 0.51 ms a frame; the picture still to be judged.)

### Open decisions

- NVIDIA Reflex: with Streamline when frame generation is taken up (the tester's call, 2026-11-27),
  no longer a design of its own on `VK_NV_low_latency2`.
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

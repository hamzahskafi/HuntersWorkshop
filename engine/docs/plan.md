# Hunter's Workshop: the plan

What comes next, the rules and decisions it works under, and every open item. Updated 2026-11-09.

- **Where it stands:** 0.4.6 is in beta: the memory model release
  ([memory-model.md](memory-model.md)). Betas 1 to 7 (engine ids 85 to 91) moved the engine's
  caches onto the game's own create, destroy and release calls; beta 7's log came back on
  2026-11-08. Beta 8 (engine id 92) starts the game's GPU-owned surfaces empty instead of filling
  them from memory, and reuses dead targets' images; beta 9 (93) leaves the game's targets unwatched
  and hooks YEBIS's and Scaleform's write calls. Beta 10 (94-95) closes beta 9's hole and counted the
  owner rule's decisions: no texture made over a live one, the guesses holding YEBIS's old targets at
  loads. Beta 11 (96, 2026-11-08) turns the owner rule on; its run was smooth. Beta 12 (97-98,
  2026-11-09) watches the game's buffers by their calls: write faults down 76%, nothing stale. 0.4.5 was released on 2026-11-07 from beta 13 (`a6b34b8`,
  engine id 84): the GPU steps, +8.4% at the lamp spot against 0.4.4. 0.4.4 was released on
  2026-11-03 (`cbec5a2`, engine id 64).
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
| Test PC | RTX 3070 Ti, i5-14600KF, 60 Hz display; Windows HDR is off. HDR-output items are checked in the code, not in the tester's runs. |

## Now: 0.4.6, the memory model release

The detail of every beta (its parts, switches, test runs and what it must show) is in
[memory-model.md](memory-model.md). The numbers after beta 8 may shift with what each log shows;
the order of the work does not change without the tester. Renumbered 2026-11-08 after the design
check: beta 10 measures the owner rule, beta 11 acts, and depth, buffers and shaders moved on one;
then the game's buffers went before depth (the tester, with beta 11): buffers 12, depth 13.

| Beta | What it does | State |
|---|---|---|
| 1 to 7 | The caches follow the game's create, destroy and release calls: textures built in the game's create call, nothing freed on a release, YEBIS's and Scaleform's textures followed, YEBIS's same-memory targets kept across its rebuilds | Done; beta 7's log read 2026-11-08 |
| 8 | GPU-owned surfaces start from their predecessor or empty, never from memory, and take no hashes; dead targets' images reused by new ones of their shape; write-watching replaced by the calls for game textures (off, counted); the logging for betas 9 to 13 | Built 2026-11-08 (engine id 92). Run A read: the rebuild stalls gone (frames in play 28.5 ms or less but one of 41.6). Run B read: no hitch, frames in play 33 ms or less; the tester's look at the picture decides part 4 |
| 9 | The game's targets unwatched by default; YEBIS's and Scaleform's write calls hooked; the GPU's earlier writes to a surface's memory measured (`BB_TAKE_EARLIER_WRITES=0` leaves them out); the writer of textures made with their data named; the design note for the owner rule | Built 2026-11-08 (engine id 93). The tester's run read: a clean session, frames in play 28.2 ms or less but two after loads; the writers named. The design check revised the order (beta 10 measures first) and the tester agreed |
| 10 | Beta 9's hole closed (a texture made with its data marks the images over it written); the census of live textures made over each other; the owner rule counted alongside today's rules without acting; textures made with their data unwatched | Built 2026-11-08 (engine id 94); waits on the tester's run |
| 11 | Images belong to their textures: an image of a game's texture retired with it (unless a draw used it since), the old guesses kept for the rest; any texture made without data takes the image of its shape it is made over (`BB_IMAGE_OWNERS=0` keeps the old rules) | Built 2026-11-08 (engine id 96). The tester's run read: clean, smooth, textures about 8-11% lower in VRAM and more handed back after loads; the driver's total set by the game's buffers |
| 12 | The game's buffers by the calls: the pages of the game's GX buffers holding no GPU data watched by their creations and releases, not by protection; every GX resource's memory handed back at its destroy; the engine's own CPU stores told; a sampling net for writes no call tells (`BB_WATCH_GAME_BUFFERS=1` watches as before) | Built 2026-11-09 (engine ids 97-98). The tester's run read: clean, write faults down 76%, the net found no untold write; read in full: each minute's report makes a 26-30 ms frame (since beta 5), and the extra compiles were the GPU cache recompiled for a file it need not hash |
| 13 | Beta 12's follow-ups: the labels' check, the minute report timed and off the presenting thread, the GPU cache's identity without the command processor, write faults counted by what watched their page, loading lines naming their area (the depth items dropped by the tester, 2026-11-09) | Planned |
| 14 | Shaders built ahead: translated at startup for the variants the game's calls predict, pipelines built during loading screens (moves earlier if beta 8 shows compiles are the bigger stutter) | Planned |
| Release | Switches settled with the tester, old paths agreed on removed, a small-card run, the docs | Planned |

### Waiting on the tester

- **Still open from beta 12:** a run through the 3.8 s load into m34 (its 400 MB buffer frame), and
  a boss, a cutscene, character creation or online play (what no run has covered yet).
- **Optional, beta 11's A/B:** the same route twice, as shipped and with `BB_IMAGE_OWNERS=0`, to put
  a number on the VRAM difference the tester saw.
- **Run C** (the shader cache cleared, the same route), before beta 14 only.

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
| The frame after a load (the 3.8 s one into m34): about 400 MB of the game's buffers uploaded and about 100,000 pages write-protected at once, 23-25 ms of a 44-49 ms frame | Beta 10's runs; there since 0.4.5 beta 9 at least (then 58-69 ms with texture work beside it) | Beta 12: the game's buffers by the calls, not by page; if beta 13 keeps some watched, their protecting spread over the load |
| 91% of write faults are GX's create calls copying data into memory the buffer cache still watches (beta 11: 20,899 of 22,991) | Beta 11's run, 2026-11-09 | Beta 12, parts 2 and 3 |
| GX writes a depth texture's HTILE (its clear pattern) when the game creates a depth view (`0x2166610` through `0x217a210`): a CPU write into a GX texture after its creation | The check of beta 12's design, 2026-11-09 | Textures stay watched in beta 12; the depth beta (13) looks at HTILE by the calls |
| Whether no CPU write reaches a live GX buffer in what beta 9-11's runs did not cover (bosses, cutscenes, character creation, online) | The check of beta 12's design | Beta 12's sampling net (part 7) and the tester's runs |
| The engine's own CPU stores for GPU commands (WRITE_DATA, DMA fills and copies, occlusion counts) counted on page faults to tell the buffer cache | Building beta 12, 2026-11-09 | Done in beta 12: each tells it (`NoteEngineStore`); the labels' fallback store too (engine id 98, from the tester's builder) |
| Labels written through the backing view (the usual path) never reach a VRAM copy of their page: a label inside memory the GPU reads as a buffer would be stale there, before beta 12 too; the "exact writes/s (labels, CPU bytes beside GPU data)" counter is never incremented (`late_vram_writes`) | The tester's builder and the check after it, 2026-11-09 | Beta 13, first: a label write tells the buffer cache only when its page holds an uploaded copy not marked to upload (a region lookup and a bit test per label, about 700 a second), counted; the never-incremented counter goes or counts this |
| The engine's minute report (RAM, VRAM, VRAM manager, GX lines; `LogRam`) runs on the presenting thread and makes that minute's worst frame 25.7-30.1 ms, in every session: every minute in every log of 0.4.6 betas 5-12 (85 of 85), 1 of 36 in betas 3-4; which part costs the 10-13 ms is not known | Beta 12's run read in full, 2026-11-09 | Beta 13: timed part by part and moved off the presenting thread |
| The GPU cache's identity (`build.sh`, `out/gpu-cache.id`) hashes `video_core/amdgpu/liverpool.cpp`, the command processor, which stores nothing in the cache: beta 12's change to it recompiled the cache at the start (58 shaders and 113 pipeline keys lost, 229 compiles in play against 98, 12 draws left out for up to a second, one 40 ms frame) | Beta 12's run read in full | Beta 13: left out of the identity (beta 13 changes the file again) |
| The create calls' remaining write faults (beta 12: textures 2,268, buffers 1,487): where they land is a guess (memory not handed back) | Beta 12's run read in full | Beta 13: each write fault counted by what watched its page |
| The net hashes about 60 MiB a minute of up to 875 MiB under the calls: a pass takes 4-15 minutes, so a write it finds is that late | Beta 12's run read in full | Kept while it finds nothing; a faster pass if it ever finds a write |
| Which load a `Loading:` line is (the first or second entry into m34) is read only from the files opened around it | Beta 12's run, the tester ("I thought I went there this time") | Beta 13: each loading line names the map opened during it |
| Buffers made before the renderer exists are never told to the buffer cache (no hook yet): watched by protection as before | Building beta 12 | Kept so; the net would count their writes as untold if any happened |
| Uploading the game's buffers during loading screens rather than at their first draw | Beta 12's design | After beta 12's long-frame breakdown shows whether the frame after a load still waits for it |
| The game's dead data in VRAM (memory no live GX resource holds) handed back | Beta 11's run (1.6 GB of the game's data by the end); beta 4's freeing at releases cost 82-201 ms frames | Measured in beta 12 (part 5); a step after, if it is large |
| Surfaces filled from the GPU's buffer writes made before they existed (run A: 21-32 a minute, up to 108 MiB; most of its one 41.6 ms frame) | Beta 8 run A, 2026-11-08 | Beta 9 measures them (`BB_TAKE_EARLIER_WRITES=0` leaves them out); left out by default only if the counts and the tester's eyes agree (a depth buffer copied through memory before its texture is made needs them) |
| CPU writes on YEBIS textures, 10-71 a minute: which of its textures (its CPU-updated ones expected) | Beta 8 run A | Answered by beta 9's run: YEBIS updates its sampled textures 58-145 times a minute (never a surface; no Map or Unmap), now told by the call |
| 21 CPU writes on small textures made with their data at every startup (32x32, 1024x512): which code writes them | Beta 8 runs A and B | Answered by beta 9's run: GX's create call copying the data in, and asset loads (told without faults). Textures made with their data unwatched in beta 10 (done) |
| `FindImage`'s fast path must forget an image whose owner changed (`registry_generation`) | The design check, 2026-11-08 | Not needed in beta 11: nothing is refused at a draw, so the fast path hands out only images `FindImage` itself would |
| Refusing a dead image at a draw (the design note's item 2) | The design note | Not built in beta 11: draws recorded after the game's destroy read the texture (0-3 a minute in beta 10's runs; the console's memory still holds it). Looked at again only if a stale image is seen |
| Images whose texture's memory the game released (no destroy event) are not dead for the owner rule | Beta 11 | None in beta 10's runs ("live resources whose memory the game unmapped 0"); a destroy event for them if they appear |
| A hand-over by shape alone: a texture made without data of another format over an image of the same size takes it (contents kept, the format's view decided at the draw) | Beta 11 | Counted in the owner rule line; looked at if images handed over go unused |
| The dead-target pool barely gains from the rule: YEBIS's retired targets are still in the GPU's hands when the new set is first used (5-186 taken, 30-469 found none a minute) | Beta 11's run | Later, if VRAM churn at loads matters: a pool that takes an image the GPU is finishing, its reuse ordered after that work on the same queue |
| Beta 10's "kept because used just before" label also counts images the beta 11 rule keeps by the exact "used since" (1-2 a minute) | Beta 11's run | The label is beta 10's; reworded or removed with the release's log pass |
| How much lower VRAM is with beta 11: the minute lines show textures 8-11% lower and more handed back after loads, the driver's total about the same (the game's buffers grow with the areas) | Beta 11's run, the tester | The optional A/B run above |
| Textures made with their data unwatched: a CPU write no call tells would go unseen (none in beta 9's run beyond the create copy and asset loads) | Beta 10 | The tester's runs; `BB_WATCH_GAME_TEXTURES=1` and safe mode watch everything |
| A live surface reaching past a new texture made with its data keeps its contents (the memory outside the new texture is older than the GPU's drawing) | Beta 10 | Counted in the owners line; kept as is in beta 11 (2 and 4 a minute in beta 10's runs) |
| GX's input-layout creator in 1.09 (for predicting shader variants) | Research, 2026-11-08 | Done in beta 8: `0x21657a0`, hooked and counted |
| "The game's data followed the same way (buffers built ahead, freed on release)" | Plan step 3 (0.4.5 beta 16) | Partly done (0.4.5 beta 17; 0.4.6 beta 5); the rest is beta 12 |
| "Shaders translated when the game creates them" (all 13,277 at startup) | 0.4.5 beta 17, "Not done" | Beta 14, measured in beta 8 |
| "The shader building and its threads" | 0.4.6 memory design, "Next" | Beta 14 |
| `pipeline_compiles.md`'s "Remaining (not done)": catalog coverage, the warm-up's state rebuilt on the main thread, a profile change emptying the cache, the scene-curve folder unseeded, AMD and Intel catalogs | `pipeline_compiles.md` | Looked at in beta 14 |
| `BB_COPY_HOLD` and `BB_COPY_MERGE` removed ("stay through the stable release, then go") | The rest of 0.4.5, item 1 | The 0.4.6 release, with the tester |
| A small-card run: `BB_VRAM_TEST_BUDGET_MB=3500` with the VRAM manager on (its rules predate the memory model) | Plan step 3 (0.4.5 beta 16's test) | The 0.4.6 release |
| The VRAM manager's unbinds wait only for the work that used the evicted blocks (a last-use tick per block) | Plan step 3 | With the small-card run; built only if it shows bind waits |

### Candidates the logs decide

| Item | From | Scheduled |
|---|---|---|
| The game's area prefetch (made for a hard disk; its model could drive the loads) | 0.4.6 memory design, "Next" | Looked at after beta 11, if loading screens or the frames after them still stall |
| eFMann's smoothness leads ([archive/hitches-2026-10.md](archive/hitches-2026-10.md)) | Plan step 3, "Later" | Only if stutter after loads remains after 0.4.6 |
| A flicker at two area crossings: post-processing effects off and on for a split second | The tester, 2026-11-08 | Not the memory work: there in 0.4.4 and 0.4.5, and in beta 8 with `BB_EMPTY_SURFACES=0`, `BB_TARGET_POOL=0` or `BB_GX_FOLLOW=0` (log `8d281c42`). Likely the game's own reset of its post-processing when YEBIS rebuilds; parked until a PS4 recording of those spots shows otherwise |
| A hitch when a controller connects at startup (the UI changes its button prompts; possibly a YEBIS rebuild) | The tester, 2026-11-08: "something for later if I ever even bother with it" | Low priority; beta 8's rebuild logging may show whether it is a rebuild |
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
- **Untested for want of hardware:** AMD FSR 4.1 on Radeon cards, FSR 2.3.4, HDR output.

### Open decisions

- NVIDIA Reflex on `VK_NV_low_latency2` without Streamline: whether, and when.
- The GPU-path tracers that overlap.
- The System menu's PC pages under one parent entry, if the game's menu code allows it.

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

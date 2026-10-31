# Hunter's Workshop: engine map and reviewed optimization ideas

As of 0.4.4 beta 19 (commit `9b8b057`, 2026-10-31). Written for the project owner. What has been done about each item since is kept in `community-fixes-plan.md` (from 0.4.4 beta 20 on); this document is not updated after the fact.

## How to read this document

Five agents each mapped one area of the engine and proposed ideas:

1. renderer core: drawing, recording, barriers and submission;
2. memory and caches;
3. upscaling, motion vectors, scene resolution and presentation;
4. shader recompiler and pipeline cache;
5. command processing, the C runtime and the game side.

A second agent per area (the "skeptic") then checked every idea against the code. It gave each idea a verdict (**confirmed**, **partly** or **refuted**), corrections and a priority from 1 (do first) to 5 (drop). This document merges the five maps into one picture. Where the skeptic corrected the original idea, the correction is what you read here.

- **Measured** means a number from the tester or the docs supports the claim.
- **Suspected** means the claim comes from reading code. In that case the doc names the log line or switch that would measure it.
- Idea ids (`renderer-3`, `mem-4`, `upscale-1`, `shader-2`, `seg-1` and so on) are the agents' ids, kept so the material can be traced.
- The skeptics read the code at `9b8b057` (beta 19). The latest measurements are from beta 17. Beta 19 has not been measured yet.

### Where the files are

Short paths are used below. They expand like this:

| Short form | Folder |
|---|---|
| `vk_*.cpp`, `vk_*.h` | `engine/gpu/shadps4/video_core/renderer_vulkan/` |
| `buffer_cache.cpp/.h`, `memory_tracker.h`, `region_manager.h` | `engine/gpu/shadps4/video_core/buffer_cache/` |
| `texture_cache.cpp`, `image.cpp`, `image_view.cpp`, `tile_manager.cpp` | `engine/gpu/shadps4/video_core/texture_cache/` |
| `page_manager.cpp` | `engine/gpu/shadps4/video_core/` |
| `liverpool.cpp/.h`, `pm4_cmds.h` | `engine/gpu/shadps4/video_core/amdgpu/` |
| `host_shaders/*.comp` | `engine/gpu/shadps4/video_core/host_shaders/` |
| `recompiler.cpp`, `specialization.h`, `profile.h`, `ir/passes/`, `backend/spirv/`, `frontend/` | `engine/gpu/shadps4/shader_recompiler/` |
| `gnmdriver.cpp` | `engine/gpu/shadps4/core/libraries/gnmdriver/` |
| `driver.cpp` | `engine/gpu/shadps4/core/libraries/videoout/` |
| `bbport_*` | `engine/gpu/shim/` |
| `runtime_*.c` | `engine/src/` |
| `config.default.ini` | `engine/` |
| `run_windows.py` | `engine/scripts/` |
| `GameEnvironment.cs`, `LauncherSettings.cs` | `launcher/windows/BloodborneLauncher/Core/` |
| `*.md` docs | `engine/docs/` |

Any other file can be found by name.

### Terms (each defined once)

- **Draw / dispatch.** A draw is a GPU command that renders triangles. A dispatch runs a compute shader over a grid of work groups.
- **Render pass.** A run of draws into the same set of target images. A **restart** (also "split") ends a pass and begins it again over the same targets. A restart **without a barrier** costs about 5 to 9 microseconds (us) on the tester's RTX 3070 Ti (measured, beta 14).
- **Barrier.** A command that makes later GPU work wait for earlier work. The engine's `FlushBarriers` records one global barrier that waits for all earlier work. That is a **drain**: the GPU runs empty for a moment. It also ends the open render pass.
- **Command buffer, segment, submission.** Vulkan commands are recorded into command buffers. The engine cuts its stream into **segments** (one command buffer each) so three threads can record in parallel. A **submission** hands a batch of segments to the GPU.
- **Stage A, stage B, packet.** These are the two CPU threads that turn the game's commands into Vulkan commands. Stage A decodes them. Stage B records them. A **packet** is stage A's record of one draw, dispatch or ordered task, sent to stage B through a ring buffer called the **DrawPipe**.
- **PM4.** The PS4 GPU's command format, which the game writes.
- **Copy shader, HLE.** The **copy shader** is a small compute shader the game uses to copy many small memory ranges (bone palettes and similar). Its hash is `0xfefebf9f` in code; the docs and the task say `fefebf9e` (see 1.6). **HLE** (high-level emulation) means the engine does not run that shader. It reads the shader's control data and does the copies itself. A **batch** is a group of ranges copied by one command.
- **GX region.** 288 MiB of the game's per-frame data in system memory. It is imported once, so the GPU reads it there directly ("in place"). This is Windows only.
- **Constant ring, stream buffer, arena.** These are the other places the GPU can read game data from:
  - the **constant ring** (32 MiB): snapshots of small constants, filled by stage A;
  - the **stream buffer** (128 MiB): snapshots of small read-only data;
  - the **arena**: copies in video memory, kept current by tracking the game's writes.
- **Image layout, view.** A **layout** is Vulkan's name for an image's current state, such as "depth attachment", "read-only depth" or "General". Changing it needs a barrier. A **view** is a handle through which a shader or pass sees an image.
- **G-buffer.** The pass where the game writes surface data for lighting. Here it has 6 colour targets plus a depth/stencil image (D32S8) at 1920x1080.
- **Scene proxy.** A reduced-size stand-in for a 1080p target. Proxies are used only at the DLSS and FSR quality presets, not at DLAA. **Resolving** a proxy copies it back to the full-size image. **Filling** it copies the full-size image into it.
- **Object motion.** Per-object motion vectors for the upscaler. Skinned G-buffer draws run a "motion" pipeline variant, and some of them store their vertex positions (the **history**) for next frame.
- **RCAS.** The sharpening filter used after upscaling.
- **Breadcrumbs.** A hang diagnostic: the GPU writes a marker before and after each command, so a hang can be located.
- **Profile label.** A named time span in `BB_GPU_PROFILE` output.
- **Toggle bit.** A runtime switch flipped live through `BB_TOGGLE_FILE` (`bbport_toggles.h`).
- **Honest labels.** The default mode: a fence value the game waits for is written only once the GPU has really passed it.

---

## 1. How the engine works

### 1.1 The short version

Bloodborne's code runs natively on the PC. The C runtime (`engine/src`) and the port layer (`engine/gpu/shim`) stand in for the PS4 system. The game writes PS4 GPU command buffers (PM4). The engine decodes them on stage A and records Vulkan commands on stage B. Three recorder threads build command buffers, which are then submitted. The game's GCN shaders are translated to SPIR-V by the shader recompiler. On top of the game's own work, the engine adds temporal upscaling and anti-aliasing (DLSS, FSR 3/4, TAA), motion vectors for the upscaler, reduced-resolution scene proxies and presentation.

### 1.2 Threads

| Thread | What it does | Main code |
|---|---|---|
| Game threads | Build command buffers through the game's own libGnm. With native calls (`BB_NATIVE_CALLS=1`, the launcher default) three draw functions are replaced by "record" stubs. The game submits about 60 to 70 times a frame. The render thread waits for the previous frame in `WaitPreviousFrameAtSubmit`. Game threads also take the rare write faults (0 to 1 a second in play). | `gnmdriver.cpp:134-197`, `2456-2550`; `bbport_gnm_hooks.cpp:341-418` |
| **Stage A**: `shadPS4:GpuCommandProcessor` | Decodes PM4, keeps the register file and picks pipelines. Copies small constants into the constant ring and writes one packet per draw into the DrawPipe. Fences, WriteData, DMA and flip become ordered packets. | `liverpool.cpp:195` (Process), `1334` (ProcessGraphics), `2143` (ProcessCompute); `vk_rasterizer.cpp:1956` (Draw), `1150-1298` (PostDraw), `1343` (RunInOrder) |
| `bb:DrawScan`, `bb:DrawPrepN` | Prepare draws ahead of stage A (pipeline lookup). 82 to 98% of draws arrive prepared. | `vk_draw_prep.cpp:236`, `340`, `394`; `vk_pipeline_cache.cpp:566-572` |
| `bb:DrawAhead` (lookahead) | Prefetches for stage B. On by default with 8 or more hardware threads. | `vk_rasterizer.cpp:1561-1571` |
| **Stage B**: `bb:DrawRec` | Does everything that changes the caches: binds buffers and textures, begins render passes, records barriers, runs the object motion bookkeeping and the copy shader HLE. Records Vulkan commands as closures into chunks. | `vk_draw_pipe.h:319`; `vk_rasterizer.cpp:1376` (RunDrawPacket), `2029` (DrawRecord), `2605` (DispatchRecord) |
| `bb:VkRecorder[0..2]` | Record chunks into command buffers, one segment each. Also issue the submissions. | `vk_scheduler.cpp:818`, `599` (MaybeSplit), `1125` (SubmitAsync) |
| `bb:GpuSignals` | Waits on the GPU's timeline semaphore, then writes the game's fence labels, raises interrupts and retires frames. | `vk_rasterizer.cpp:224` (SignalAfterGpu), `459` (GpuSignalLoop) |
| Compile threads | Create Vulkan pipelines: 100 to 300 ms each with a cold NVIDIA cache. | `pipeline_compile_queue.h:25-71` |
| Copy threads (`BbCopy`) | Game-memory to staging copies and image hashes. | `buffer_cache.cpp:719`; `texture_cache.cpp:559` |
| Present thread | Paces flips, copies the frame to the swapchain and presents it. | `driver.cpp:1117-1460`; `vk_presenter.cpp:1154` |
| One-off threads | The GX import thread runs once at start (`buffer_cache.cpp:3436`). `bb:Protect` runs only with `BB_DEFER_PROTECT=1`. The main thread rebuilds warm-up state. | |

### 1.3 One frame, from the game's commands to the screen

1. **The game submits.** Each `sceGnmSubmitCommandBuffers` call reaches `Liverpool::SubmitGfx` (`liverpool.cpp:2510`). That copies the command buffer for the draw-prep workers (`DrawPreparation::Build`, `vk_draw_prep.cpp:236`) and queues a decode job for stage A. After the second submission of a frame (`BB_SUBMIT_AHEAD=2`), the game thread waits until the GPU has finished the previous frame (`gnmdriver.cpp:134-197`). On the tester that wait is about 6.5 ms a frame.
2. **Stage A decodes.** It applies and hashes register writes (`liverpool.cpp:993`). For each draw:
   - `Rasterizer::Draw` picks the pipeline, usually from a prepared draw.
   - `PostDraw` packs the changed register blocks, the shader's user data, the constant-ring bindings and the vertex buffer descriptors into a packet in the DrawPipe (a 16 MiB ring).
   - About 7,900 small constant buffers a frame are copied into the constant ring (`CollectRingBindings`, `vk_rasterizer.cpp:1042-1112`).

   Commands that cannot be pipelined make stage A wait for stage B first (`liverpool.cpp:1417-1425`). On the compute queues every packet waits (`liverpool.cpp:2155-2159`).
3. **Stage B records each packet.** In `DrawRecord` order:
   - targets (`PrepareRenderState`, `vk_rasterizer.cpp:724`);
   - buffers (`BindBuffers`, `3950`, through `BufferCache::ObtainBuffer`, `buffer_cache.cpp:1118`). Each binding is read in place from the GX region, snapshotted into the stream buffer, or taken from the arena with uploads.
   - textures (`BindTextures`, `4327`). A memo of texture sets hits 97% of the time; otherwise `FindImage`, and `RefreshImage` if the image is stale.
   - beginning the render pass (`BeginRendering`, `5119`). A memo is used if possible; otherwise `BeginRenderingFull` does layout changes, scene proxies, upscaler redirects and the motion attachment.
   - `FlushBarriers` when a barrier is needed. This ends the pass.
   - object motion checks, descriptors and dynamic state;
   - the draw itself, wrapped in breadcrumbs (`vk_scheduler.h:877-889`);
   - `ResetBindings`, which notes what was accessed for the barrier tracker;
   - `KickRecording`, which may cut a segment.

   Dispatches go first through `ExecuteShaderHLE` (`vk_shader_hle.cpp:617`):
   - the copy shader becomes `ExecuteCopyShaderHLE` (`vk_shader_hle.cpp:349`);
   - the dword copy shader `3d5ebf4e` becomes image copies (beta 13);
   - dispatch `9a9cf8a9` (the game's post-processing combine) first triggers the upscaler's `Run()` (`vk_rasterizer.cpp:2627`).
4. **Recording and submission.** The recorder threads turn chunks into command buffers. A segment is cut every 64 KiB, or every 128 KiB inside a render pass. **A cut inside a pass ends the pass and reopens it** in the next command buffer. Submissions go out as ordered tasks with a timeline semaphore: 3.1 a frame with 12.3 command buffers each, so about 38 segments a frame. Work is submitted when the GPU is on its last submission or idle; otherwise it is held until the GPU announces it is close (`vk_rasterizer.cpp:362-420`).
5. **On the GPU**, in order:
   - shadow-like passes (R16G16F, 256x256 down to 32x32);
   - the G-buffer (about 26 passes);
   - lighting and the scene-colour pass (R16G16B16A16F with depth);
   - the game's compute work;
   - the upscaler segment, recorded directly on stage B (`Scheduler::EnterDirectMode`, `vk_scheduler.cpp:755-790`): edge clamp, motion compose, DLSS, extra sharpening, then the merge into the game's scene colour;
   - the game's own post-processing, tonemap and UI at native size.
6. **Signals.** `bb:GpuSignals` writes the fence labels once the GPU passes them. The frame-end task retires the frame, which wakes the game thread (`liverpool.cpp:171-193`).
7. **Flip and present.** The flip task runs `Presenter::PrepareFrame`. It waits for the frame before (`BB_FRAMES_AHEAD=1`, `vk_presenter.cpp:1023-1041`), runs the host post pass and optional FSR 1 or HDR. The present thread then copies the frame to the swapchain at its 120 Hz cadence. On the tester's 60 Hz display, Mailbox shows every other frame. That is by design: the 120 preset is fixed-timestep (`BB_VBLANK_HZ=120`, `run_windows.py:223-225`) and the game's speed follows the flip rate.

### 1.4 Where the GPU time goes

The tester's setup: beta 17, RTX 3070 Ti, DLAA, 120 FPS preset, about 1,760 draws a frame at the lamp spot. GPU busy is 8.30 to 8.39 ms against an 8.33 ms frame, so the GPU is the limit by a hair. Saving even 0.05 ms of GPU time would hold 120.

| Profile label | ms a frame | Count | What is inside, and notes |
|---|---|---|---|
| upscaler Run (DLSS) | 1.27 | 1 | From `Run()`'s start to the next dispatch mark: edge clamp, motion compose, DLSS, extra sharpening (a copy, then RCAS), merge and their barriers. The DLSS share is not measured (the mapper's guess: 0.8 to 1.0 ms). Run has no sub-labels. |
| G-buffer passes | about 2.3 | about 26 | Object motion is about 0.4 ms of this, of which the history is about 0.1 ms (beta 16). The motion attachment accounts for about 2 of the passes (26 against 23.9 with motion off). |
| copy shader HLE: barrier + dispatch | 0.45 | about 64 | Only the `MultiCopy` dispatches are labelled. About 130 more batches a frame go through `vkCmdCopyBuffer` with no label, so their time is charged to neighbouring labels. The copy shader's real cost is above 0.45 ms (renderer-8). |
| pass R16G16B16A16F + D32S8 (scene colour) | 0.26 | 11 | Mixes depth-writing blended layers with transparents (`vk_rasterizer.cpp:789-793`). |
| dispatch cs a040875f | 0.25 | 48 | About 5 us each, which is about the fixed cost of one dispatch on a drained GPU. The shader area judged its code an unlikely lever. |
| dispatch cs a954e79d | 0.2 | 5 | 35 to 43 us each. The beta 18 dump shows a `ds_append` loop. Beta 19 targets it (see `shader-4` in section 5); not yet measured. |
| R16G16F passes 256 / 128 / 64 / 32 | 0.1 to 0.15 per label | 7 to 12 each | Shadow-map-like. |
| pass 32768x32768, no attachments | 0.1 | 13 | Origin unknown. |
| image copies R8G8B8A8 1920x1080 | 0.07 | 3 | |
| depth to colour (shader) | 0.03 | | Beta 17. |
| GPU idle between submissions | 0.1 | | |

The listed labels add up to about 5.4 to 5.6 ms of the 8.48 ms profiled frame. The material does not break down the remaining 2.9 to 3.1 ms.

**The profiler changes what it measures.** Each mark ends the open render pass and writes a timestamp that waits for all earlier work (`vk_gpu_profiler.cpp:35-47`). So in a profiled run:

- every labelled dispatch runs on a drained GPU, and its label includes launch and drain;
- passes are split at marks;
- the beta 18 copy-shader pass statistics are wrong (bug `renderer-1`).

Profiling also costs about 0.1 to 0.15 ms (8.48 ms profiled against 8.30 to 8.39 ms unprofiled). Read pass counts only from runs without `BB_GPU_PROFILE` until `renderer-1` is fixed.

### 1.5 Where the CPU time goes

- **Game thread:** waits about 6.5 ms a frame for the previous frame. So the CPU is not the limit on the tester's i5-14600KF.
- **Stage A:** "3.4 us a draw" (about 6 ms a frame). This is the thread's CPU time, and it **includes spinning**: up to 200 us idle spins between submissions (`liverpool.cpp:233-256`) and spins while waiting for stage B (`vk_draw_pipe.h:276-291`). Its real work is roughly 100% minus "idle %" minus "stage A waited %" from the existing frame statistics lines (`clean-4`).
- **Stage B:** 1.85 us a packet, 67% busy on the tester. On a Ryzen 5 5600 in fights it was 5.3 us a packet and 89 to 94% busy, and there it limits FPS (`parallel-stageb.md`). The older per-step figures predate GX-in-place and beta 17: buffers and ObtainBuffer 0.61 to 0.89 us a packet, textures 0.71 to 0.99 us, object motion 0.26 us.
- **Copy shader:** about 54 game dispatches, 4,300 ranges, 193 batches (so about 386 `ObtainBuffer` calls) and 12.6 barrier flushes a frame (from 6,500 dispatches, 518,000 ranges, 23,000 batches and 1,500 flushes a second).
- **Constant ring:** about 7,900 small constant buffers a frame.
- **Recording:** about 38 command buffers a frame.

**What this means for the ideas.** On the tester only GPU savings raise FPS. CPU savings matter on 6-core and Ryzen 5600-class CPUs and on the Steam Deck.

### 1.6 Where the area maps disagreed, and how this document resolves it

1. **"Copy-shader dispatches inside a pass: none" (beta 18, `community-fixes-plan.md:66`).** The memory skeptic used this as evidence against `mem-4`. The renderer skeptic showed it is invalid: it was measured with `BB_GPU_PROFILE` on, and the profiler ends the pass before the counter looks (`renderer-1`). This document follows the renderer skeptic: whether the copy shader splits passes is **unknown until re-measured**. That also reopens the listed idea "run copy-shader copies before the render pass", which was closed on this data.
2. **How many restarts nobody can explain.** The renderer mapper said 13 G-buffer restarts are invisible to beta 18's counter. Its skeptic replied that beta 18 saw about 31 byte-identical restarts a frame. What was missing was what ended them, and beta 19 now tags that. Both are partly right. Even in beta 19, two kinds of restart are still counted nowhere: restarts whose state changed (clears, the motion slot, the depth view or layout), and restarts after a recording cut (`renderer-2`, `seg-1`).
3. **Cost of a restart.** The renderer area measured it in beta 14. With `BB_VK_SEGMENT_KB=2048`, the G-buffer went from 26 to 13 passes and from 2.75 to 2.63-2.69 ms, with GPU busy and FPS unchanged. That is about 5 to 9 us per restart without a barrier. The command-area mapper proposed `seg-1` with a 3 to 10 us guess and did not cite this result. This document uses the beta 14 numbers.
4. **Breadcrumbs.** The renderer mapper could not tell whether they run on the tester's card. Its skeptic found the tester's logs (driver 617.42) print breadcrumb lines, so they are active (`renderer-7`).
5. **Copy-shader counts and hash.** The profile shows about 64 labelled copy dispatches a frame; the rate gives about 54 game dispatches. The label counts `MultiCopy` GPU dispatches, and one game dispatch can produce more than one. The hash is `0xfefebf9f` in `vk_shader_hle.cpp:30`, but `fefebf9e` in `motion_copy_sources.h:2` and the plan. No one confirmed which value the logs print.
6. **Object motion's cost.** It was 0.8 to 0.9 ms in beta 14's measurement and is about 0.4 ms since beta 16. This document uses 0.4 ms.
7. **Stage A's 3.4 us a draw.** The renderer map treats it as work; the command map shows it includes spinning. See 1.5.
8. **The GX region.** The memory map calls in-place reads the default. That is true on Windows, the tester's platform. On Linux the copy shader's sources already go through the stream buffer (`bbport_gx_ring.h:18-31`).
9. **cs a954e79d.** The shader area could not say what it does. The shader skeptic and the beta 19 plan row say beta 18's dump identified the `ds_append` loop.
10. **Beta 19 overtook several ideas.** Commit `9b8b057` contains:
    - render-pass enders named (13 kinds, plus what each barrier waits for);
    - GDS appends per subgroup;
    - the copy shader's tiling guard;
    - staging requests sized to the copy;
    - colour-to-depth by a draw and wider depth-to-colour by a shader.

    The ideas these overtook are in section 5.

---

## 2. Optimization ideas that survived

Ranked by the skeptic's priority, then by expected effect. All of them are vendor-neutral and upscaler-neutral unless the entry says otherwise. "Switch" is the suggested `BB_` variable that restores today's behaviour. Where the material gave no name, the entry says "suggested here".

| # | Id | Title | Pri | Verdict | Evidence | Expected effect |
|---|---|---|---|---|---|---|
| 2.1 | upscale-1 | Do the extra sharpening inside the merge | 1 | partly | suspected | about 0.1-0.2 ms GPU at DLAA 1080p |
| 2.2 | seg-1 | Carry render passes across recording cuts | 1 | partly | bound measured (beta 14) | at most about 0.06-0.12 ms of G-buffer time |
| 2.3 | renderer-7 | Breadcrumbs on every draw | 2 | confirmed | suspected cost | unknown; zero-code test |
| 2.4 | renderer-3 | Depth layout and view follow depth-write toggles | 2 | partly | suspected | a full barrier per toggle at native size |
| 2.5 | renderer-6 | 4 KiB read rounding creates false barriers | 3 | partly | suspected | unknown; zero-code test |
| 2.6 | mem-4 | Copy-shader sources over 16 KiB read across PCIe | 3 | partly | suspected | at most about 0.1 ms GPU (Windows) |
| 2.7 | renderer-4 | A pass begun with a clear restarts at its second draw | 3 | partly | unit cost measured | 5-9 us per cleared pass |
| 2.8 | idle-1 | GPU-idle signal wakes the game thread for nothing | 3 | partly | suspected | CPU only |
| 2.9 | upscale-2 | Reduced presets resolve a proxy only to overwrite it | 3 | partly | suspected | 0.02-0.05 ms at reduced presets |
| 2.10 | upscale-3 | Proxies filled even when the pass clears them | 3 | partly | suspected | 0.01-0.04 ms per fill avoided, reduced presets |
| 2.11 | pipe-1 | DrawPipe false sharing between stage A and B | 3 | confirmed | suspected cost | CPU-bound systems only |
| 2.12 | shader-2 | Uniform-buffer decision tied to storage alignment | 3 | partly | suspected | AMD and Linux Intel only |
| 2.13 | renderer-5 | Motion and non-motion draws use different attachments | 4 | partly | bound measured | at most about 0.1 ms |
| 2.14 | shader-5 | Vertex exact-math only on the position | 4 | partly | bound measured | a few hundredths of a ms at most |
| 2.15 | shader-6 | Motion history stores a value it never reads | 4 | partly | bound measured | well under 0.03 ms |
| 2.16 | mem-3 | Copy-shader destination span marked GPU-written | 4 | partly | mostly measured negligible | nil to small |
| 2.17 | upscale-4 | Scene depth moved to General for the upscaler | 4 | partly | suspected | likely near zero on NVIDIA |
| 2.18 | asc-2 | Compute queues wait for stage B at every packet | 4 | partly | unsupported | CPU-bound systems only |
| 2.19 | rt-1 | Guest locks: global list walk and an allocation per lock | 4 | partly | suspected | CPU-bound game threads only |

### 2.1 Do the extra sharpening inside the merge, and drop the RGBA32F copy (`upscale-1`)

Priority 1, partly confirmed. Upscaling area.

- **What and where.** Sharpening is on by default (`config.default.ini:4` `sharpen=1`, `:50` `sharpness=0.20`). With DLSS, the port applies all of it itself (`own = 0`, `vk_temporal_upscaler.cpp:978`). So every DLAA frame, `ExtraSharpen` (`:974-1063`, called from `Run` at `:1495-1496`) runs three full-screen steps after DLSS:
  1. it copies the RGBA16F output into an RGBA32F image (`:993-1006`, the copy at `:1038`);
  2. RCAS reads that copy and writes the output back (`:1048-1058`, `host_shaders/taa_sharpen.comp:8-12`);
  3. the merge reads the output again and writes the game's scene colour (`:1527`, `upscale_merge.comp:29`).

  There are five barriers between these steps (`:1028-1061`), plus the merge's own (`:1521`). All of it sits inside the 1.27 ms "upscaler Run (DLSS)" label.
- **Root cause.** RCAS was added as a separate in-place pass. It reads neighbouring pixels, so in place it needs a copy of its input. The copy was made RGBA32F so that one image serves both the LDR and the HDR case.
- **Fix.**
  - *1080p output:* `upscale_merge.comp` runs RCAS itself. It reads the upscaled image through a sampled binding and writes `vec4(sharpened, original.a)` into the scene. The merge writes a different image from the one it reads, so there is no hazard. RGBA16F to RGBA32F is lossless, so the result is bit-identical. Debug views still override the colour afterwards (`upscale_merge.comp:30-44`).
  - *Other outputs (scaled path):* the LDR sharpen variant reads the output and writes `ui_image` directly. This replaces both the RCAS pass and the output-to-UI copy (`:2319-2329`). Skeptic correction: when `ui_image` uses the game's sRGB format (`EnsureUiResources`, `:1889-1891`), today's copy does the sRGB encode. The fused write goes through a UNORM view (`UiStorageView`, `:1065-1075`), so it must encode explicitly.
  - *TAA:* there is no copy today, because RecordTaa sharpens from the RGBA32F history (`:957-969`). Fusing saves one dispatch and one barrier there.
- **Evidence.** Suspected. The code path and defaults are confirmed. The skeptic's traffic estimate: the copy is 16.6 MB read plus 33 MB written, and the separate RCAS is 33 MB read plus 16.6 MB written. That is about 100 MB a 1080p frame removed, about 0.16 ms at the 3070 Ti's peak bandwidth, plus 3 to 4 drains. The mapper's 50 MB estimate was half the real figure.
- **Expected effect.** About 0.1 to 0.2 ms of GPU time a frame at DLAA 1080p. That is more than the tester's overshoot of about 0.05 ms. Roughly four times as much at a 4K output (not estimated precisely). It also frees the RGBA32F image: 33 MB at 1080p, 133 MB at 4K (see `upscale-7`). It applies to every upscaler mode whenever extra sharpening runs: DLSS always, FSR 3/4 above their own strength, TAA always.
- **Risk.** Low.
  - RCAS must clamp reads at the edges, as it does today.
  - The `BB_DUMP` "output" image (`:1510-1512`, `:2341`) becomes the unsharpened image.
  - For the FSR 3 scaled path, an RGBA16F copy of 8-bit values is not exact, but the difference is negligible.
- **How to test.**
  1. No code needed: compare the "upscaler Run (DLSS)" label with the menu's sharpening on and off. That bounds the gain.
  2. After the change: at the lamp spot with DLAA and sharpness 0.20, compare the Run label, GPU busy and FPS (against 117.8 to 119.0) between the default and the switch.
  3. `BB_FINAL_DUMP_TRIGGER` captures in both modes should be identical (`tools/dump_view.py`).
  4. Repeat at a 4K output with DLSS Quality ("upscaler RunScaled (DLSS)") and with TAA.
- **Switch.** `BB_SHARPEN_FUSED=0` keeps the separate pass and the RGBA32F copy.

### 2.2 Carry render passes across recording cuts instead of ending them (`seg-1`)

Priority 1, partly confirmed. Command-processing area (scheduler code).

- **What and where.** When a segment passes twice the cut size inside a render pass (128 KiB at the default 64 KiB, `vk_scheduler.cpp:138-140`), `MaybeSplit` ends the pass and marks it for resuming (`vk_scheduler.cpp:603-632`). `EnterDirectMode` does the same (`:761-775`). The resumed pass begins again with LOAD in the next command buffer. Such restarts appear in no counter: `BeginRendering` skips them (`:201-206`), and beta 19 added no resume count (`vk_gpu_savings.h:49-51`, `74-88`).
- **Root cause.** Recording is spread over three threads by cutting the stream into command buffers. Vulkan 1.3 can carry one render pass across command buffers of the same submission (the "suspending" and "resuming" rendering flags). The scheduler ends and restarts the pass instead. All of one submission's segments go into one submit, in order (`:1158-1194`), which that Vulkan feature requires.
- **Fix.**
  1. Count resumes, as part of `renderer-2`.
  2. End the pass with the suspending flag before a cut, and begin the next segment with the resuming flag (same targets, no clear).

  Skeptic correction: at cut time `MaybeSplit` cannot know what the next segment records first. If that is not a draw in the same pass (a barrier, a transfer, `EnterDirectMode`, or the submission's own pass end at `:1040`/`:1136`), record "resume, then end" there. That case costs what it costs today. The upscaler's direct segment keeps ending the pass.
- **Evidence.** There is a measured upper bound from beta 14 (`community-fixes-plan.md`, beta 14 row): 2048 KiB segments cut the G-buffer from 26 to 13 passes and from 38 to 5.6 command buffers a frame. The G-buffer label went from 2.75 to 2.63-2.69 ms, but **GPU busy and FPS did not change**. This measured bound comes from the renderer area. The command-area mapper's own estimate (10 to 20 restarts at 3 to 10 us each) was a guess.
- **Expected effect.** At most about 0.06 to 0.12 ms of G-buffer time at the lamp spot. It may not show in GPU busy at all, as in beta 14. Unlike the 2048 KiB experiment, recording stays parallel. Cost on AMD and Intel is not measured.
- **Risk.** Medium. Vulkan's suspend and resume rules must hold: identical attachments, and no other actions or barriers in between. Validation layers (`BB_VK_VALIDATION=1`) catch violations.
- **How to test.**
  1. Add the resume counter to the "GPU savings" line.
  2. Repeat the beta 14 test on beta 19 (`BB_VK_SEGMENT_KB=2048` or larger against the default, profile on). This is an upper bound only: it also removes recording parallelism.
  3. With validation on, compare `BB_RENDER_SUSPEND=0` and `1`, then profile.

  Build it only if the counter and the profile show the restarts still cost something.
- **Switch.** `BB_RENDER_SUSPEND=0`.

### 2.3 Breadcrumbs write two GPU markers around every draw and dispatch (`renderer-7`)

Priority 2, confirmed. Renderer area.

- **What and where.** Breadcrumbs are on whenever the `VK_AMD_buffer_marker` extension exists, unless `BB_BREADCRUMBS=0` (`vk_breadcrumbs.cpp:217-257`). `RecordCrumb` writes an end-of-pipe marker before and after every crumbed command, the copy shader's dispatches included (`vk_scheduler.h:873-889`; `Mark` at `vk_breadcrumbs.cpp:414-420`). That is about 3,600 to 3,900 marker writes a frame (about 430,000 a second at 120 FPS), plus a ring write and an atomic per command on stage B.
- **Root cause.** Hang location at single-draw precision, paid for in every frame.
- **Evidence.** Breadcrumbs are active on the tester. Their logs (driver 617.42, `hunters-workshop-20261025_*.log`) print "GPU breadcrumbs: stream 0 ... noted". The cost has never been measured; nothing in `engine/docs` covers it. So the cost is suspected.
- **Fix.**
  1. Measure first. No code is needed.
  2. If it costs something, write only the "after" marker per command, and keep "before" markers at segment and pass starts. At the end of the pipe, "before command n" equals "after command n-1", plus any barrier or copy between them. That keeps the same hang location with half the writes.
- **Expected effect.** Unknown. It is zero if the driver pipelines these writes; otherwise a share of GPU busy proportional to the command count. The frame is GPU-bound by about 0.05 ms, so even a small share matters.
- **Risk.** Halving makes hang reports slightly coarser: a hang inside the barrier between draws n-1 and n cannot be told apart from a hang at the start of n. Keep the switch so the community keeps hang localisation.
- **How to test.** `BB_BREADCRUMBS=0` against the default, three runs each at the lamp spot: GPU busy and FPS. Then the halved variant.
- **Switch.** `BB_BREADCRUMBS=0` exists. `BB_BREADCRUMBS=2` would restore both markers after halving.

### 2.4 At native size, the depth image's layout and view follow depth-write toggles (`renderer-3`)

Priority 2, partly confirmed. Renderer area. Affects DLAA and no-upscaler only.

- **What and where.**
  - The depth view's `is_storage` follows `depth_write_enable` (`image_view.cpp:86`), so a write toggle gives another view.
  - The new layout depends on `is_storage` and `stencil_enable` (`vk_rasterizer.cpp:5390-5396`).
  - The resulting transition (`:5397-5402`) adds an image barrier (`image.cpp:363-366`). `DrawRecord`'s `FlushBarriers` (`:2110`) then records a global barrier and ends the pass.
  - So every switch between a depth-writing draw and a test-only draw costs a drain plus a restart.
  - The code's own comment says the scene-colour pass mixes depth-writing blended layers (decals, wet or blood ground) with transparents (`:789-793`), so toggles happen there (R16G16B16A16F + D32S8, 11 a frame, 0.26 ms).
  - At reduced presets, proxies use one layout and one view (`vk_scene_resolution.cpp:651-665`), so the cost depends on the upscaler mode.
- **A second, possibly more frequent cost (found by the skeptic).** A draw that samples the depth image it is attached to gets **two image barriers every draw**:
  - `BindTextures` moves it to read-only depth with shader-read access (`:4634-4639`; the texture-set path refuses target images, `:5000-5004`);
  - `BeginRenderingFull` then moves it to a read-only attachment layout with attachment read and write access (`:5397-5402`).

  `GetBarriers` emits a barrier whenever the access differs, even for the same layout (`image.cpp:363`). So each such draw ends the pass with a full barrier, and the begin memo is refused for bound targets (`:5176`). This also happens only at native size.
- **Root cause.** The layout is chosen from "does this draw write depth". Depth writing is dynamic state (`:6007`), so the attachment layout does not need to follow it. Read-only layouts are needed only when the image is also sampled.
- **Fix.** Use the skeptic's "sticky" rule, not the mapper's per-draw rule. (The per-draw rule would add barriers where test-only draws follow depth-sampling draws, such as soft particles and fog.)
  - Transition only when the current layout cannot serve the draw. A depth or stencil write needs a writable layout. Sampling needs a read-only one. A test-only draw that does not sample keeps whatever layout the image is in.
  - Give the attachment one view regardless of `depth_write_enable`.
  - For the feedback case: in read-only layouts, pass attachment read access only, and skip read-to-read transitions within the same layout.
  - The "latent layout mismatch" the mapper noted (sampling depth while writing stencil) is probably legal. Confirm with validation layers before changing it.
- **Evidence.** Suspected. The code path is certain; how often it happens is not measured. Beta 19's counters already measure both cases without new code: "image barrier" pass splits and barrier waits on "attachments".
- **Expected effect.** Each toggle removed saves a full drain, not just a restart. Upper bound: a share of the 0.26 ms scene-colour pass and of the G-buffer's restarts at DLAA. Zero where the game does not toggle.
- **Risk.** Feedback cases (a draw sampling the depth it tests against) must keep a read-only layout. Run with validation layers.
- **How to test.**
  1. Read beta 19's pass reasons at DLAA from a run **without** `BB_GPU_PROFILE`.
  2. Profile the G-buffer and R16G16B16A16F passes (ms and segments) with the switch on and off.
  3. Check on screen: decals, wet and blood ground, transparents, soft particles, fog.
- **Switch.** `BB_DEPTH_RO_LAYOUTS=1` restores today's choice.

### 2.5 Read ranges rounded to 4 KiB cause false write-after-read barriers (`renderer-6`)

Priority 3, partly confirmed. Renderer area. Shares a root with `mem-3`.

- **What and where.** `AccessBuffer` rounds every read range to 4 KiB, whatever the buffer (`vk_runtime.cpp:1101-1108`). Write-after-read checks compare against those rounded ranges (`IsBufferAccessed`, `:1038-1053`). These checks run for the copy shader's destination (`vk_shader_hle.cpp:138`, `145`) and for written bindings (`vk_rasterizer.cpp:4129`). The rounding was meant for constant-ring allocations, which move every draw. It also applies to the game's memory bound in place.
- **Root cause.** A shortcut meant for the ring and stream buffers was applied to all buffers.
- **Skeptic correction.** For the copy shader, the bigger imprecision is the batch bounds, not the rounding. Each `MultiCopy` checks and records the whole span of its batch, up to 64 KiB (`MaxDistanceForMerge`, `vk_shader_hle.cpp:391-393`; `62-63`, `136-148`, `176-181`). `CopyBuffer` treats more than 4 regions the same way (`vk_runtime.cpp:131-150`). So the mapper's "writes are already exact" is wrong there. Dropping the read rounding alone may barely change the copy shader's 12.6 flushes a frame. Exact ranges for non-ring reads may also grow the tracker on stage B.
- **Fix.** Round reads only for the stream and constant-ring buffers, and track exact ranges elsewhere. If the copy shader's share matters, also check and record the batch's exact ranges instead of its span. That is the same root as `mem-3`: the HLE knows the exact ranges but hands spans on.
- **Evidence.** Suspected. A zero-code test exists: toggle bit 2048 (`CoarseReadTracking`, `bbport_toggles.h:50`) through `BB_TOGGLE_FILE`.
- **Expected effect.** Unknown until the toggle test. Each false barrier removed saves a drain.
- **Risk.** Tracker growth on stage B, which is 67% busy.
- **How to test.** At the lamp spot, flip toggle bit 2048 live. Read "HLE copy ... barrier flushes", beta 19's barrier counts and pass reasons, **and stage B time**. Then profile.
- **Switch.** `BB_COARSE_READS_ALL=1` keeps today's rounding everywhere.

### 2.6 Copy-shader sources of 16 to 64 KiB are read across PCIe (`mem-4`)

Priority 3, partly confirmed. Memory area. Windows only.

- **What and where.** For a GX-region binding over 16 KiB, the GX branch (`buffer_cache.cpp:1166`) returns before the copy-source rule is used (`:1246-1251`, `:1261`; `vk_shader_hle.cpp:527-529`, `588-590`). So copy-shader sources of 16 to 64 KiB are read by the GPU from system memory. Sources of 16 KiB or less are snapshotted into the stream buffer.
- **Skeptic corrections.**
  1. This is not a clash between two rules. The copy-source rule exists to keep these spans off the tracked path (write faults), not to put them in video memory (`buffer_cache.cpp:1238-1245`; `community-fixes-plan.md:45`).
  2. The gain is bounded. A near-empty dispatch on video memory (cs a040875f) costs about 5 us and a copy dispatch about 7 us. So PCIe latency is at most about 2 us times 54, or about 0.1 ms a frame, and likely less.
  3. GX in place measured equal to the tracked path within 0.05 ms (`gpu-memory.md:285`). A GPU-copy variant of the same idea (`ShadowCopy`) measured slower, 48 to 44 FPS (`buffer_cache.h:298-302`).
- **Disagreement resolved.** The skeptic also called the benefit "more copies could run before the pass" refuted, citing beta 18's "no copy-shader dispatches inside a pass". `renderer-1` shows that count is invalid, so this benefit is **unknown**, not refuted.
- **Fix.** A cheap A/B test: a switch that moves the copy-source test ahead of `:1166`. Note that the snapshot copies the whole span, not just its ranges. In the worst case, copy-thread writes into BAR memory could reach about 1.5 GB/s (23,000 batches a second times 64 KiB).
- **Evidence.** Suspected. First read the share of "src in place" and its KiB per call from the existing `HLE copy ObtainBuffer paths` line (`vk_shader_hle.cpp:441-452`).
- **Expected effect.** At most about 0.1 ms of GPU time, probably less. Windows only.
- **Risk.** Low. Stream-buffer use grows by a few MB a frame out of 128 MiB. Copy-thread time grows.
- **How to test.** Compare "copy shader HLE: barrier + dispatch" both ways at the lamp spot, plus the ObtainBuffer paths line and copy-thread time.
- **Switch.** `BB_HLE_GX_SNAPSHOT=0`.

### 2.7 A pass begun with a clear is restarted at its second draw (`renderer-4`)

Priority 3, partly confirmed. Renderer area.

- **What and where.** The stored pass state keeps the clear flags and clear values (`vk_scheduler.cpp:210`). It is compared byte for byte (`vk_scheduler.h:66`). A colour clear is consumed at the first draw (`vk_rasterizer.cpp:5313-5314`), and HTILE clears likewise (`:5383`). So the next draw, without clears, compares unequal, and the pass is ended and reopened with LOAD. With object motion on, at least one G-buffer restart a frame is certain: `ObjectMotion::Attach` sets a clear on slot 7 at the first motion draw (`vk_object_motion.cpp:407`), and it also ends the pass there with a full image barrier (`:387-403`).
- **Root cause.** The clear belongs to the pass's start, but it stays in the state that later draws are compared against.
- **Fix.**
  - After beginning the pass, store the state with `depth_clear`, `stencil_clear` and `clear_value` zeroed. Do not zero `has_depth` or `has_stencil`, which share the same union (`vk_scheduler.h:46-54`). A new clear request still differs and restarts with CLEAR, and resuming is unaffected.
  - Skeptic correction: also store the begin memo at the clearing begin, with clears zeroed. The memo is stored only for a state without clears (`vk_rasterizer.cpp:5200-5210`), so otherwise it hits only from the third draw.
- **Evidence.** The unit cost is measured: about 5 to 9 us per restart without a barrier (beta 14). The number of cleared passes is a guess ("20 to 40").
- **Expected effect.** Small on NVIDIA: 5 to 9 us per cleared pass, plus one fewer `BeginRenderingFull` on stage B.
- **Risk.** Low.
- **How to test.** `renderer-2`'s "clear consumed" category falls to 0. Segments a frame for the shadow passes and the G-buffer fall. The begin-memo hit rate rises. Images are identical.
- **Switch.** `BB_PASS_KEEP_CLEAR_STATE=1`.

### 2.8 The GPU-idle signal does no useful work in the default mode and wakes the game thread (`idle-1`)

Priority 3, partly confirmed. Command-processing area. CPU only.

- **What and where.** Every time stage A runs out of work, it posts a "GPU idle" ordered task and releases a submission lock (`liverpool.cpp:401-448`, `2616-2628`). With honest labels and the decode-time submission lock (both defaults):
  - the interrupt's only consumer, `ResetSubmissionLock` (`gnmdriver.cpp:3114`), clears a lock that is never set in this mode (`:2537-2541`);
  - `IsGpuIdle` ignores the value (`liverpool.h:145-153`);
  - the game cannot receive this event (`gnmdriver.cpp:242-246`, `272`).

  The skeptic found an extra cost: both lock functions call `notify_all` on the condition variable the game thread waits on (`gnmdriver.cpp:86-90`, `101-105`, waiting at `189`). So each idle cycle **wakes the game thread twice** for nothing during its 6.5 ms wait.
- **Skeptic correction.** The task is not useless. For recorded work that has no fence, the idle signal is the only submission trigger when stage A runs dry. `SubmitForSignalsNow` submits only if a signal is pending (`vk_rasterizer.cpp:362-381`), and the announce and safety-net paths need one too (`liverpool.cpp:211-225`). Removing the whole task could leave a frame's first submissions waiting while the game waits too: a GPU bubble on exactly the GPU-bound tester.
- **Fix.** Keep the submission decision. In the default mode, drop only `SignalGpuIdle`, `ReleaseSubmissionLock` and their `notify_all` calls. For example, treat "recorded since the last submit" (`Scheduler::RecordedSinceSubmit`) as pending in that decision.
- **Evidence.** Suspected; the cost is not measured. `BB_SECTIONS=1` lists `GpuIdleHonest` with us/s and count/s.
- **Expected effect.** Fewer ordered tasks and thread wakes (an estimated 0.1 to 0.3 ms of CPU a frame across threads), and no spurious game-thread wakes. No GPU change intended.
- **Risk.** Low to medium. "Safety net" flushes must stay 0. "EOP pending" must return to 0. "GPU idle between submissions" and "announcements" must not get worse.
- **How to test.** `BB_SECTIONS=1 BB_FRAME_STATS=1` with the switch at 0 and 1, plus a 15-minute soak.
- **Switch.** `BB_GPU_IDLE_SIGNAL=1` restores today's behaviour.

### 2.9 At reduced presets, the scene colour is resolved from its proxy only to be overwritten (`upscale-2`)

Priority 3, partly confirmed. Upscaling area. DLSS and FSR quality presets only, not DLAA.

- **What and where.** `Run`'s transition of the full-size scene colour (`vk_temporal_upscaler.cpp:1347`) goes through `NativeAccess` (`vk_runtime.cpp:254`; `vk_scene_resolution.cpp:142-143`). That resolves the dirty proxy into the full-size image with a 1080p RGBA16F blit (`vk_scene_resolution.cpp:225-253`). The upscaler then reads the proxy (`:1352-1361`). The merge rewrites every full-size pixel's RGB and keeps only the alpha (`upscale_merge.comp:28,44`; dispatch at `:1577`). So the resolved RGB is never used.
- **Root cause.** `NativeAccess` has no notion of a write that replaces everything (`vk_scene_resolution.cpp:132-157`).
- **Fix (with the skeptic's corrections).**
  - Do not discard at `:1347`. That transition comes before the upscaler's result is known. If the upscaler fails (`:1414-1420`, `:1492`), the merge never runs, and the game's post-processing would read undefined data.
  - Instead, skip the `:1347` colour transition at reduced presets and discard only at the merge's own transition (`:1524`).
  - The merge takes the alpha from the proxy through a sampler that follows the resolve's own conditions: `BB_SCENE_LINEAR_RESOLVE`, experiment bit 2 and the format's linear-filter support (`vk_scene_resolution.cpp:229-237`).
  - Fix `upscale-5` first, with an RGB-only edge clamp. The clamp is recorded on the proxy before the merge (`:1363-1364`), so it would otherwise spoil the proxy's edge alpha.
  - Leave out a general "whole-image discard" flag until it is counted. Beta 14's `CopyProxy` already avoids the resolve for copies from a dirty proxy (`:424-600`).
- **Evidence.** Suspected. Resolves per frame are not in the docs. The frame statistics already split "scene proxy resolves per second by native access", and the profile has a "scene proxy resolve R16G16B16A16Sfloat 1920x1080" label.
- **Expected effect.** An estimated 0.02 to 0.05 ms a frame at reduced presets with 1080p output. Nothing at DLAA.
- **Risk.** Medium; covered above.
- **How to test.** DLSS Quality at 1080p output: the resolve counter and label against the switch. Compare captures for post effects that use the scene's alpha.
- **Switch.** `BB_SCENE_DISCARD_WRITES=0`.
- **Related, undetermined.** The depth transition at `:1345` also resolves the depth proxy (a resample draw for D32S8, `vk_scene_resolution.cpp:186-208`), although the upscaler reads the depth proxy. Whether the full-size depth is read later in the frame is unknown.

### 2.10 Proxies are filled from the full-size image even when the pass clears them (`upscale-3`)

Priority 3, partly confirmed. Upscaling area. Reduced presets only.

- **What and where.** Any full-size write marks a proxy invalid (`vk_scene_resolution.cpp:156`). The merge writes the full-size scene colour every frame, for example. The next reduced use as an attachment calls `Get()`, which fills the proxy from the full-size image (`:608`, `:629`). This happens even when this very pass clears it. `Attachment` calls `Get` (`:650`) before it knows about the clear, although the caller already has the flags (colour at `vk_rasterizer.cpp:5313/5349` before `:5352`; depth and stencil at `:5378-5422` before `:5426`).
- **Root cause.** The clear state is not passed down.
- **Fix.** Pass the clear state to `SceneTargets::Attachment`. When every aspect is cleared over the whole proxy, mark it valid without the copy and start it from an undefined layout. Skeptic correction: a resumed pass begins with LOAD even when its state says clear (`vk_scheduler.cpp:206-211`, `218`). So skip the fill only when the pass will really begin with a clear. `WillBeginRendering` (`vk_scheduler.h:1061-1063`) ignores resuming, so it cannot be the guard as it stands.
- **Evidence.** Suspected; not counted. `BB_SCENE_DEBUG` prints each "Scene fill:" line, and the profile has a "scene proxy fill" label.
- **Expected effect.** Unknown until counted: an estimated 0.01 to 0.04 ms per avoided 1080p fill. Nothing at DLAA.
- **Risk.** Low, if limited to full clears of all aspects and passes that really begin with a clear.
- **How to test.** First count "Scene fill:" lines for cleared targets at DLSS Quality. Then compare the fill labels against the switch, and check that captures are unchanged.
- **Switch.** `BB_SCENE_FILL_ON_CLEAR=1`.

### 2.11 The DrawPipe ring shares cache lines between stage A and stage B (`pipe-1`)

Priority 3, confirmed. Command-processing area. CPU only.

- **What and where.** The layout was checked from the declarations:
  - two 262-entry statistics arrays take 4,192 bytes (`vk_draw_pipe.h:106-109`);
  - that puts `busy_cycles` at byte 4224, on the same 64-byte line as `handler`, `context`, `ring`, `head` and `pending_size` (`:380-384`);
  - stage A writes `pending_size` and `head` for every packet (`:75`, `:84`);
  - stage B does a locked add on `busy_cycles` (`:366`) and reads `handler`, `context` and `ring` for every packet (`:357`, `:365`);
  - the lookahead thread reads `ring` too (`:204`).

  So that line moves between cores two or three times per packet. In addition, stage A reloads stage B's index on every packet (`:259`) and stage B reloads stage A's (`:329`), although the ring is almost never full.
- **Root cause.** Producer-owned, consumer-owned and read-only fields sit together, and neither side caches the other's index (the standard ring-buffer technique).
- **Fix.**
  - Put stage-A-only fields on one cache line and stage-B-only fields on another. Make `busy_cycles` a single-writer counter published with a relaxed store.
  - Keep thread-local copies of `handler`, `context` and `ring`.
  - Each side keeps a cached copy of the other's index and reloads it only when needed.
  - Keep the sequentially consistent loads at `:348` and `:231`. The sequentially consistent add on `consumed_packets` at `:372` is the fence for the sleep handshake and must stay.
- **Evidence.** Suspected; the effect is not measured. A cross-core cache-line transfer costs about 40 to 120 ns.
- **Expected effect.** An estimated 0.05 to 0.2 us less per packet on stage B, and similar on stage A. That is 3 to 10% of stage B where it limits (Ryzen 5600 class at 5.3 us a packet, Steam Deck). Nothing on the GPU-bound tester.
- **Risk.** Low.
- **How to test.** With `BB_FRAME_STATS=1 BB_SECTIONS=1`, compare "stage B busy %", "stage B us per packet" and "GPU thread us/draw" before and after the build.
- **Switch.** `BB_PIPE_CACHED_INDEX=0` for the cached-index part. The layout change alters no behaviour, so compare it build against build.

### 2.12 Uniform buffers are switched on by an unrelated storage-buffer limit (`shader-2`)

Priority 3, partly confirmed. Shader area. AMD and Linux Intel only.

- **What and where.** The engine turns the game's read-only constant buffers into Vulkan uniform buffers ("native constant buffers") only when the device's storage-buffer offset alignment is above 4 bytes. `needs_buffer_offsets` is set from that limit (`vk_pipeline_cache.cpp:459`), `uniform_constant_buffers` requires it too (`:466-472`), and `DefineBufferProperties` returns early without it (`backend/spirv/spirv_emit_context.cpp:238-241`). On NVIDIA, storage buffers measured +18.9% pixel-shader code and +28.8% vertex-shader registers (`native_shader_compiler.md:289`).
- **Skeptic correction.** It is not "only NVIDIA". From public device data, Intel's Windows driver reports a 64-byte alignment and already qualifies. RADV, AMD's Windows driver and Mesa's Intel driver (Linux) report 4 and do not. The CPU side is already split by buffer type (`vk_rasterizer.cpp:3962-3963`, `4091-4096`, `4116-4121`), so the coupling is only in the shader profile and its emission.
- **Root cause.** The push-data offset machinery that uniform buffers need exists only when storage buffers need offsets.
- **Fix.** Add a per-type requirement: a new profile field (for example `needs_uniform_offsets` from the uniform alignment). `DefineBufferProperties` then picks per buffer by its type, and the storage term is dropped from `uniform_constant_buffers`.
- **Evidence.** Suspected. The gain on AMD is unmeasured and may be small: AMD's compiler already uses scalar loads for read-only storage buffers, and on 4-byte-alignment devices there is no offset arithmetic.
- **Expected effect.** AMD and Linux Intel run the same constant-buffer code as NVIDIA. The gain is unknown. Nothing on the tester's card.
- **Risk.** A one-time recompile of AMD and Intel shader caches. It cannot be validated on the tester's NVIDIA card. Before it ships as a default, run at least `bb-shader-gpu-stats` on an AMD driver.
- **How to test.**
  1. Confirm the "Native shaders: constant buffers as uniform buffers" start line is absent in an AMD log today.
  2. Run `bb-shader-lab --bench` with `BB_LAB_PROFILE='needs_buffer_offsets=1,uniform_constant_buffers=1'`, then `bb-shader-gpu-stats` on RADV.
  3. In game on AMD and Intel, compare the G-buffer labels.
- **Switch.** `BB_UBO_PER_TYPE=0` (name suggested here). `BB_NATIVE_SHADERS=0` still restores the legacy layout.

### 2.13 Motion and non-motion G-buffer draws use different attachment sets (`renderer-5`)

Priority 4, partly confirmed. Renderer area.

- **What and where.** Only motion pipelines get target 7 and 8 attachments (`vk_pipeline_cache.cpp:834-840`; `ObjectMotion::Attach`, `vk_object_motion.cpp:380-411`). So each switch between motion and non-motion draws changes the pass state and the begin-memo signature (`vk_rasterizer.cpp:4845-4847`), and the pass restarts (without a barrier).
- **Fix.** Give non-motion G-buffer pipelines the motion attachment too, with a zero write mask, and attach it for every G-buffer draw.
- **Skeptic correction.** The gain is bounded and small. Beta 14 left 13 G-buffer passes once cuts were removed, so all other restarts together number at most about 12 a frame. Even if every one were an alternation, that is at most about 0.1 ms. The fix costs:
  - a pipeline key change for every motion-capable G-buffer pipeline, with one-time recompiles and stored caches invalidated;
  - an 8th attachment on every G-buffer draw (a zero write mask is free in practice but not guaranteed);
  - a motion-target clear every frame, even when no motion draw runs.

  Draws that fail the motion test would still alternate.
- **Evidence.** Suspected; bounded by beta 14.
- **Expected effect.** Tens of microseconds; at most about 0.1 ms.
- **Risk.** As listed above.
- **How to test.** First `renderer-2`'s "motion slot" category. Build this only if alternations are a large share. Check that motion vectors are unchanged under DLSS and FSR.
- **Switch.** `BB_MOTION_PASS_ATTACH=0`.

### 2.14 Keep exact maths in vertex shaders only where it reaches the position (`shader-5`)

Priority 4, partly confirmed. Shader area.

- **What and where.** Every float add, multiply and fused multiply-add in every vertex shader is marked "no contraction" (no fusing), so that positions match bit for bit between the depth pre-pass and the colour pass (`spirv_emit_context.cpp:74-77`; `emit_spirv_floating_point.cpp:9-17`). Only the position slice needs that.
- **Fix.** An IR pass after the final dead-code pass marks the instructions that feed the position, clip distances and point size. It must follow data flow **and** control dependence (the conditions of branches whose results feed the position). Everything else may fuse.
- **Skeptic correction.** This is already on the doc's open list (`native_shader_compiler.md:403`). The realistic ceiling is about 4% of vertex code, not 14%. Dropping no-contraction while keeping the position invariant gave -4.3% binary and +1.6% registers (`native_shader_compiler.md:292`). Shadow and depth-only shaders are almost all position slice, so they gain nothing.
- **Evidence.** The bound is measured (code size, not time).
- **Expected effect.** A few hundredths of a millisecond at most; not quantified.
- **Risk.** Varyings change in the last bit, which is already accepted for pixel shaders. Varyings later converted to integers must stay in the slice. Caches recompile once.
- **How to test.** Measure first with `bb-shader-lab --bench` and `bb-shader-gpu-stats`. Then compare the G-buffer and shadow labels, and look for depth fighting.
- **Switch.** `BB_VS_CONTRACT=0`.

### 2.15 Motion history stores a component it never reads (`shader-6`)

Priority 4, partly confirmed. Shader area.

- **What and where.** Each stored vertex writes x, y, z and w (`emit_spirv_special.cpp:139-152`). The next frame's load replaces z with a "valid" flag (`:182-183`), and the fragment code reads only x, y and w plus that flag (`:190-223`). Nothing else reads the buffer (`vk_object_motion.cpp:103-122`, `199-200`).
- **Fix.** Skip the z store, but keep the 16-byte slot and the aligned 16-byte load. Do not pack to 12 bytes: that breaks the aligned load (`:172-174`). Because it is a shader change, the switch needs a recompile path: a motion shader version bump, or a flag in the draw parameters.
- **Evidence.** Bounded by measurement. With toggle bit 29, all history stores and loads together cost about 0.1 ms (G-buffer 2.31 to 2.19-2.20 ms, `community-fixes-plan.md:64`). One store in four is well under 0.03 ms. The bytes may not even reach memory, because partly written 32-byte sectors may need a fill. What is certain is one store instruction fewer per stored vertex (about 2.83 million a frame).
- **Expected effect.** Under 0.03 ms. Do it only alongside other motion-shader work.
- **Risk.** Very low.
- **How to test.** The G-buffer labels with the switch on and off. The "Show motion vectors (debug)" view must look identical.
- **Switch.** `BB_MOTION_STORE_Z=1`.

### 2.16 The copy shader's whole destination span is marked GPU-written (`mem-3`)

Priority 4, partly confirmed. Memory area. Shares a root with `renderer-6`.

- **What and where.** On the tracked (arena) path, each batch binds its destination span as written (`vk_shader_hle.cpp:531`, `592`). `SynchronizeMemory` and `ForEachUploadRange` then mark every 4 KiB page of the span GPU-modified and stamp it (`buffer_cache.cpp:1313`, `1323`; `memory_tracker.h:176-184`), including gap pages the shader never writes.
- **Skeptic corrections.**
  - This happens only on the tracked path. If the destination is in the GX region, only a 64 KiB tick is noted (`buffer_cache.cpp:1166-1182`, `57-62`).
  - Most claimed consequences are already measured as negligible:
    - Object motion: since beta 16, 354 to 358 of about 362 GPU-written small skeletons are unchanged and only 7 small draws are stored (`community-fixes-plan.md:64`). Beta 15 traced only 3 of 360 palettes to the copy shader (`:63`).
    - Readbacks: write faults are about 1 a second in play (`gpu-memory.md:265`).
    - Write ticks work in 64 KiB pages, so exact bytes would not help.
    - The write-stamp lock is uncontended.
  - What is left is unmeasured: small read-only bindings of gap-page bytes losing the ring and stream path.
- **Fix.**
  1. Read "dst VRAM" against "dst in place" from the existing `HLE copy ObtainBuffer paths` line (`vk_shader_hle.cpp:441-452`).
  2. Only if "dst VRAM" is a large share: keep the span for the descriptor, but mark only the pages the ranges touch.
- **Evidence.** Mostly measured negligible; the rest suspected.
- **Expected effect.** Nil to small.
- **Risk.** Low to medium: the ranges must cover every byte the shader writes.
- **How to test.** The paths line first. Then the object motion report, the readback counter and stage B us a packet.
- **Switch.** `BB_HLE_DST_SPAN=1`.

### 2.17 Scene depth is moved to the General layout for the upscaler and back (`upscale-4`)

Priority 4, partly confirmed. Upscaling area.

- **What and where.** `Run` and `RunScaled` move the full-size depth to General (`vk_temporal_upscaler.cpp:1345`, `2137`). The camera-motion shader's descriptor says General (`vk_camera_motion.cpp:242-243`). The game's next sampling moves it back (`vk_rasterizer.cpp:4634-4639`). On some drivers General may cost a decompression of the depth image.
- **Skeptic corrections.**
  - The cost is not measured anywhere. Beta 17's 0.36 to 0.03 ms was a transfer layout plus a copy, not General.
  - The cited counter-example is wrong: the frame-generation depth capture also reads in General (`vk_frame_generation.cpp:127-131`).
  - What DLSS assumes **cannot be determined from the repository**. NGX's Vulkan resource struct has no layout (`vk_dlss.cpp:67-74`, `196-209`), and that is the tester's path.
  - FSR 4 registers General as both its layout and its restore layout (`vk_fsr4.cpp:314-317`). FSR 4.1 moves its inputs out of General and back itself every frame (`vk_fsr4vk.cpp:430-453`). Each would need its own change.
- **Fix.** Read depth in the read-only depth layout in `camera_motion.comp` and TAA. Change each upscaler only where its contract is known.
- **Evidence.** Suspected.
- **Expected effect.** Likely near zero on NVIDIA; possibly more on AMD.
- **Risk.** Medium. A layout mismatch with a library's descriptors is undefined behaviour.
- **How to test.** Measure first on an AMD card: the next depth-sampling pass after `Run`, with validation layers on.
- **Switch.** `BB_UPSCALER_DEPTH_GENERAL=1`.

### 2.18 Compute queues make stage A wait for stage B at every packet (`asc-2`)

Priority 4, partly confirmed. Command-processing area. CPU-bound systems only.

- **What and where.** `ProcessCompute` makes stage A wait for stage B at the top of every packet (`liverpool.cpp:2155-2159`), though dispatches themselves are pipelined (`vk_rasterizer.cpp:2585-2603`, `2688-2706`).
- **Skeptic corrections.**
  - The cost is not supported by the record. Only waits that actually waited are counted (`vk_draw_pipe.h:112-125`), and `parallel_gpu.md:394-399` records about 7 such waits a frame in total, none from compute. The +19 to 32% figures belong to the graphics pipeline as a whole.
  - The change is also wider than listed:
    - compute ReleaseMem runs `SignalAfterGpu` and `ProcessDownloadImages` on stage A (`liverpool.cpp:2402-2424`);
    - compute DmaData calls `FillBuffer` and `CopyBuffer` directly (`:2226-2262`);
    - compute WaitRegMem would need the graphics path's "wait while not idle" step (`:2010-2014`).
- **Fix.** Do not build this unless "drains by reason/s: compute" (`vk_rasterizer.cpp:1810-1825`) shows a real share at a CPU-bound spot.
- **Evidence.** Unsupported so far.
- **Expected effect.** Only where stage A or B limits.
- **Risk.** Medium (ordering of signals and writes).
- **How to test.** Read the drains line first.
- **Switch.** `BB_ASC_PIPELINE=0`.

### 2.19 Guest locks walk a global list and allocate per lock (`rt-1`)

Priority 4, partly confirmed. Runtime area. CPU-bound game threads only.

- **What and where.** Each rwlock lock-and-unlock pair takes the process-wide registry lock three times (`runtime_rwlock.c:117-153`, `161-173`). Each time it walks a list, moving the found node to the front (`:84-97`). An uncontended pair also does a `calloc` and a `free` (`:110-115`, `130-134`). Semaphores walk a list by id under one lock (`runtime_sema.c:36-49`). Thread lookups serve only priority, affinity and name calls (`runtime_thread.c:223`, `397-443`), so they are not hot.
- **Fix.** Validate handles in O(1) and keep holders in a small fixed per-lock array. Skeptic correction: reading a "magic" field through a pointer the game hands back touches freed memory once a lock is destroyed (`rw_destroy` frees it, `:189`). So use type-stable or deferred-free storage, or a separate pointer hash set. Leave threads alone.
- **Evidence.** Suspected. Read the 60-second "Load:" line first: rwlock count, steps and rate (`driver.cpp:483-500`). No doc records it.
- **Expected effect.** Only where a game thread limits FPS.
- **Risk.** Medium (lifetime rules).
- **How to test.** The Load: line, then a CPU-bound A/B (`BB_FPS_LIMIT=0`, low preset) with game-thread CPU from `BB_SAMPLE_THREADS`.
- **Switch.** `BB_RUNTIME_O1_HANDLES=0`.

---

## 3. Cleanup ideas that survived

These are measurement fixes, code removals and corrections. Few change FPS.

| # | Id | Title | Pri | Verdict |
|---|---|---|---|---|
| 3.1 | renderer-2 | Count every restart by cause, including recording cuts | 1 | partly |
| 3.2 | upscale-6 | Copy-shader source tracing makes statistics runs unlike players' runs | 3 | partly |
| 3.3 | shader-9 | Push-descriptor limit off by one; fallback never counted | 3 | confirmed |
| 3.4 | clean-3 | Two defaults with no recorded measurement: lookahead and native calls | 3 | confirmed |
| 3.5 | upscale-7 | Upscaler images allocated for features that are off | 4 | partly |
| 3.6 | renderer-8 | `BB_HLE_COPY_MERGE`: find the corruption or remove it | 4 | partly |
| 3.7 | mem-8 | `BB_DEFER_PROTECT`: the comment says on, the code says off | 4 | partly |
| 3.8 | clean-4 | Stage A "us a draw" includes spinning | 4 | partly |
| 3.9 | upscale-8 | Duplicate FSR 3 setup; `Run` forces direct recording | 4 | partly |
| 3.10 | mem-9 | The same GPU-modified check up to three times per binding | 4 | confirmed |
| 3.11 | clean-1 | Retire three dead fence and submission experiments | 4 | partly |
| 3.12 | clean-2 | Dead command-processor code | 4 | partly |
| 3.13 | mem-7 | Remove `BB_SHADOWS` (`ShadowCopy`) | 4 | confirmed |
| 3.14 | renderer-9 | Empty `FrameCapture::Poll` called on every packet | 4 | confirmed |

### 3.1 Count every render-pass restart by cause, including recording cuts (`renderer-2`, with `seg-1` step 1)

Priority 1, partly confirmed. Renderer and command areas.

- **What and where.** A restart is counted only when the pass begins again with byte-identical state (`vk_scheduler.cpp:204`, compared at `vk_scheduler.h:66`). Restarts after a recording cut are excluded (`:201-206`). Beta 19 already names what ended each pass (13 kinds, plus what each barrier waited for). Two kinds are still invisible:
  1. restarts over the same images whose state differs: a consumed clear, the motion slot added or removed, the depth view or layout changed;
  2. restarts after a recording cut.
- **Fix.**
  - Classify restarts over the same images as "clear consumed", "motion slot", "depth view or layout" or "other".
  - Count resumes on their own, so the totals add up to the profiler's pass segments.
  - The scheduler's state holds only views, and the two depth views differ by `is_storage`. So the rasterizer must pass image ids, or a "same targets" key that ignores `is_storage` and slot 7.
- **Evidence.** Measured context (beta 14: 26 passes, 13 after removing cuts; beta 18: about 31 identical restarts a frame).
- **Expected effect.** No FPS change. It decides whether `renderer-3`, `renderer-4`, `renderer-5`, `seg-1` and "copies before the pass" are worth building.
- **Risk.** None; a few compares per pass begin.
- **How to test.** `BB_FRAME_STATS=1` at the lamp spot, **without** `BB_GPU_PROFILE` until `renderer-1` is fixed. Categories plus resumes should add up to the profile's G-buffer segment count.
- **Switch.** None needed: counters only.

### 3.2 Copy-shader source tracing makes statistics runs differ from players' runs (`upscale-6`)

Priority 3, partly confirmed. Upscaling area.

- **What and where.** Whenever frame statistics are on, every copy-shader range is recorded under a mutex on stage B (`vk_object_motion.cpp:206-209`; `vk_shader_hle.cpp:383-385`; `motion_copy_sources.h:77-100`). That is about 4,300 ranges a frame. The question it answered is settled: 3 of about 360 palettes come from the copy shader, and "version" mode is the default.
- **Skeptic correction.** It is not report-only. In version mode, a draw without a CPU-readable anchor takes `source_anchor` (`vk_rasterizer.cpp:2236-2240`), which is set only while tracing (`:2198-2210`). So statistics runs today give those draws an anchor and jump detection (`motion_history.h:347-355`) that players never get. Turning tracing off outside source mode makes statistics runs behave like players' runs. That is the goal, but it does change matching for those draws in statistics runs.
- **Fix.** Record and trace only with `BB_MOTION_GPU_SMALL=source`. Keep source mode itself; the user's rules keep comparison switches.
- **Evidence.** The question is settled by measurement; the cost (50 to 150 us a frame of stage B) is an estimate.
- **Expected effect.** Every CPU figure in this document comes from statistics runs. After this change they measure what players run.
- **Risk.** None for rendering.
- **How to test.** `BB_FRAME_STATS`: stage B busy and us a packet before and after; the motion report's version counts.
- **Switch.** `BB_MOTION_GPU_SMALL=source` (existing) keeps tracing.

### 3.3 Push-descriptor limit is off by one, and its slow path is never counted (`shader-9`)

Priority 3, confirmed. Shader area.

- **What and where.** Both pipeline kinds use push descriptors only when the descriptor count is *below* the device limit (`vk_graphics_pipeline.cpp:786`; `vk_compute_pipeline.cpp:79`; counted at `:745-784`). Vulkan allows *up to and including* it (VUID-VkDescriptorSetLayoutCreateInfo-flags-00281). So a pipeline with exactly 32 descriptors falls back. The fallback commits a descriptor set, updates and binds it per draw on the recording thread (`vk_pipeline_common.cpp:252-261`). Nothing counts it. Pipelines of 32 or more are plausible: a pixel shader can have up to 37 bindings and a vertex shader up to 11 (`native_shader_compiler.md:151-153`).
- **Fix.** Use "less than or equal". Count fallback draws per frame in `BB_FRAME_STATS`. If the count is large, fix it at the root, for example by removing duplicate sampler bindings.
- **Evidence.** The comparison is confirmed; the count is unknown.
- **Expected effect.** Pipelines with exactly 32 descriptors take the fast path, and the counter gives a number to decide on.
- **Risk.** None.
- **How to test.** A "draws a frame by descriptor set (no push)" line, and the warm-up count of pipelines with 32 or more descriptors.
- **Switch.** `BB_PUSH_DESC_STRICT=1` restores "less than" (name suggested here).

### 3.4 Two defaults have no recorded measurement: the lookahead thread and native draw calls (`clean-3`)

Priority 3, confirmed. Command area.

- **What and where.**
  - The lookahead is on with 8 or more hardware threads (`vk_rasterizer.cpp:1561-1571`). Its design note says "default off" (`parallel-stageb.md:297-299`, `168-169`), and no A/B result exists in the docs. It uses `hardware_concurrency`, while other spinning threads use the affinity-aware `BbThreads::Available()` (`vk_draw_pipe.h:337`; `liverpool.cpp:249`).
  - Native calls are on by default (`LauncherSettings.cs:92`; `GameEnvironment.cs:198`). The only recorded result is "their cost on stage B barely changed" (`EMULATION_REMOVAL_PLAN.md:97-98`). Stage A still reads each draw record twice (`liverpool.cpp:1098-1110`, `1401-1404`).
- **Fix.** Run the A/Bs at a **CPU-bound** spot (`BB_FPS_LIMIT=0`): experiment bit 16 pauses the lookahead live, and `BB_NATIVE_CALLS=1/0` covers native calls. Keep what wins and document it. If the lookahead stays, gate it on `BbThreads::Available()` and fix the doc.
- **Evidence.** The settings and docs are confirmed; neither default has a measurement.
- **Expected effect.** Either a documented gain, or one fewer spinning thread or game-code patch.
- **Risk.** Low.
- **How to test.** `BB_FRAME_STATS=1 BB_SECTIONS=1` in 20-second phases toggling bit 16. Then two runs with native calls on and off: FPS, stage B us a packet, stage A us a draw. The GPU-bound tester would show nothing.
- **Switch.** Existing (`BB_DRAWREC_WORKERS`, experiment bit 16, `BB_NATIVE_CALLS`).

### 3.5 Upscaler images allocated for features that are off (`upscale-7`)

Priority 4, partly confirmed. VRAM only.

- **What and where.** At the default settings (reactive mask off, DLSS), these are still allocated:
  - the opaque RGBA16F and reactive R8 images (`vk_temporal_upscaler.cpp:672-676`);
  - an FSR 4 linear image, created whenever `UseFsr4()` is true, which includes DLSS (`:679-682`, `2648-2652`), though only `RunScaled`'s FSR 4 branch uses it (`:2210-2272`);
  - the RGBA32F sharpen copy (`:992-1006`).

  That is about 16.6 + 2 + 16.6 + 33 MB at 1080p.
- **Skeptic corrections.**
  - TAA binds the opaque view every frame (`:907-914`). The merge (`:1531`) and the debug view (`:1109`) bind the reactive view every frame. These need a dummy view or conditional descriptors.
  - Do not route this through `EnsureResources`' key. Any change to that key calls `scheduler.Finish()` and recreates the FSR 3 context (`:552-564`), which resets history on a menu toggle. Allocate lazily instead.
  - The RGBA32F part goes with `upscale-1`.
- **Expected effect.** About 35 to 70 MB of VRAM at 1080p, up to about 190 MB at a 4K output. No FPS change.
- **Risk.** Low.
- **How to test.** The VRAM report before and after. Toggle the reactive mask and FSR 4 live, with no validation errors.
- **Switch.** `BB_UPSCALER_EAGER_IMAGES=1` (name suggested here).

### 3.6 `BB_HLE_COPY_MERGE`: find its corruption or remove it (`renderer-8`)

Priority 4, partly confirmed.

- **What and where.** The merged copy path has been off since beta 8 because of rainbow noise in ground textures (`vk_gpu_savings.h:26-35`). It makes all `ObtainBuffer` calls first (`vk_shader_hle.cpp:520-542`); the normal path interleaves them (`:586-609`). Some text is stale:
  - the comment at `:36-38` says about 57 dispatches of about 1,024 ranges; the measurement is about 55 of about 80;
  - `motion_copy_sources.h:2` says `fefebf9e`, but the code's hash is `0xfefebf9f`.
- **Skeptic corrections.**
  - The per-dispatch check is negligible (a function-local static).
  - "193 batches down to 54" is an upper bound: runs split wherever the obtained buffers differ (`:548-549`), and `MultiCopy` refuses runs beyond the buffer range or workgroup limits (`:66-71`).
  - **The 0.45 ms label covers only `MultiCopy` dispatches** (the mark is inside `MultiCopy`, `:126-127`). The other roughly 130 batches a frame go through `vkCmdCopyBuffer` unlabelled, so the copy shader costs more than 0.45 ms.
  - The cause of the corruption is unproven. Finding it needs a tester session with images.
- **Fix.** Either find the cause (merge on, with `BB_SCENE_DEBUG` or `BB_SHADER_DISASM` near a corrupted ground texture, comparing per-batch results) and make it the default with the switch kept, or delete it. Fix the stale text. Check which hash the logs print before changing it.
- **Ordering.** Ranks after `renderer-1`, whose fix shows whether these copies split passes at all.
- **Switch.** `BB_HLE_COPY_MERGE` (existing).

### 3.7 `BB_DEFER_PROTECT`: the comment says on by default, the code says off (`mem-8`)

Priority 4, partly confirmed.

- **What and where.**
  - `page_manager.cpp:436` says "on by default"; `:481-490` makes it off unless `BB_DEFER_PROTECT=1`. It was turned off after half-written Cathedral Ward textures in Lantern 0.9.21 to 0.9.23, and the cause was never found.
  - `parallel-stageb.md:48` and `:80` describe it as live.
  - Protection now costs about 0.004 ms a frame (`gpu-memory.md:265-267`).
  - Default cost: 4 `FlushDeferredProtects` call sites (`buffer_cache.cpp:722`, `1477`, `3800`, `3830`, not 8), each one atomic load (`page_manager.cpp:1167-1172`), plus a check in `region_manager.h:250-255`.
  - It can still be turned on live through experiment bit 4 (`page_manager.cpp:478`, `496-500`).
- **Fix.** Fix the comment and the doc first. Removal is optional. If done, credit Lantern and bbport in the note.
- **Expected effect.** Clarity only.

### 3.8 Stage A's "us a draw" includes spinning (`clean-4`)

Priority 4, partly confirmed.

- **What and where.** "GPU thread us/draw" is thread CPU time divided by draws (`driver.cpp:766-773`; `liverpool.cpp:2124-2131`). It includes the idle spin (`liverpool.cpp:240-256`) and the spins while waiting for stage B (`vk_draw_pipe.h:276-291`).
- **Skeptic correction.** The separation mostly exists already:
  - the frame statistics' GPU thread "idle %" counts spin as idle (`liverpool.cpp:236`, `267-270`; `driver.cpp:779-786`);
  - "stage A waited %" comes from the draw-pipe line (`vk_rasterizer.cpp:1802-1808`);
  - `BB_SECTIONS` prints "stage A us per draw" by section (`vk_rasterizer.cpp:1843-1855`).

  A new combined line would be a convenience. The "measured" label on the spin share was wrong; it is inferred from code.
- **Fix.** Optionally print "stage A busy %". Meanwhile, read 3.4 us a draw as an upper bound on stage A's work.

### 3.9 `Run` and `RunScaled` duplicate the FSR 3 setup; `Run` forces direct recording (`upscale-8`)

Priority 4, partly confirmed.

- **What and where.** The FSR 3 dispatch setup is built twice (`vk_temporal_upscaler.cpp:1421-1490` and `2369-2440`), and so are the dump blocks (`:1500-1519` and `2336-2358`). `Run` records every backend directly on stage B (`:1366`; in direct mode every recorded command runs at once, `vk_scheduler.h:858-865`). `RunScaled` records FSR 3 on a recorder thread with failure handling (`:2428-2434`).
- **Skeptic corrections.**
  - TAA cannot move: it records directly in `RunScaled` too (`:2194-2201`).
  - For FSR 3 in `Run`, the merge must go into the same recorded step and be skipped on failure. Otherwise it would write undefined output into the scene colour.
  - The comment at `:2145-2148` says recording the motion pass and FSR 3 on the draw thread cost about 5% of its time. That matters for FSR 3 users (AMD and Intel without FSR 4). The tester's DLSS path gains nothing.
- **Switch.** `BB_FSR3_DIRECT=1` (name suggested here).

### 3.10 The same "GPU-modified?" check runs up to three times per binding (`mem-9`)

Priority 4, confirmed.

- **What and where.** For a small read-only binding in the GX region, `IsRegionGpuModified` runs at `buffer_cache.cpp:1166`, `1185` and `1262`, plus `:1160` for tail-page bindings. Each call walks the page bits and a two-level table, and may check a semaphore (`:1487-1508`).
- **Fix.** Compute it once. That is safe on stage B: what sets the bits runs on that same thread, and GPU progress only clears them. A pure refactor, so no switch.
- **Expected effect.** Negligible (tens of microseconds a second). Clearer code.

### 3.11 Retire three dead fence and submission experiments (`clean-1`)

Priority 4, partly confirmed.

- **Delete:**
  - `BB_ASYNC_FENCES` and its toggle bit (`liverpool.cpp:586-596`). It measured slower (79 against 83 FPS), and with honest labels the code returns before reaching it (`:577-584`).
  - `BB_FENCE_DELAY_TEST` (`:536-544`).
  - `BB_SUBMIT_LOCK=ahead` (`gnmdriver.cpp:118-131`). It leaks the heap, per `:134-140`.
- **Keep:** the seven pipeline sub-toggles (used by `tools/ab.sh` and `BB_TOGGLE_FILE`), `BB_WORK_RETIRED` and `BB_ZPASS_IN_ORDER=0`. They are diagnostic fallbacks that cost nothing.
- **Skeptic correction.** The measurements back the features, not their removal. The gain is nanoseconds, and folding live A/B bits would cut against the user's "keep a switch" rule. None of these is set by the launcher or scripts.
- **Expected effect.** Clarity. Record the history in the docs.

### 3.12 Dead command-processor code (`clean-2`)

Priority 4, partly confirmed.

- **Delete:** `CopyCmdBuffers` (`liverpool.cpp:2477-2508`, declared at `liverpool.h:252`), `ReserveCopyBufferSpace` (`liverpool.h:185-191`) and the `dcb_buffer`/`ccb_buffer` fields (`:277-280`). None has a caller. Also delete the offset resets in `SubmitDone` (`liverpool.h:117-118`).
- **Skeptic corrections.**
  - The comment at `liverpool.cpp:2513-2514` is not stale. Reword it, don't drop it.
  - The Gnm hook diagnostics cannot simply move out: `BbGnmEnter`/`BbGnmLeave` (`bbport_gnm_hooks.cpp:270-339`) serve `BB_GNM_TRACE`, observe mode and `BB_NATIVE_CALLS=verify` together.
  - `BB_DCB_STATS` costs one flag test per buffer and still produces `parallel_gpu.md`'s table; removing it is cosmetic.
  - Keep `BB_COPY_GPU_BUFFERS` (credit deadinside28 and bbport).
- **Expected effect.** Simpler files; no run-time change.

### 3.13 Remove `BB_SHADOWS` (`ShadowCopy`) (`mem-7`)

Priority 4, confirmed.

- **What and where.** `ShadowCopy` (`buffer_cache.cpp:3041-3080`) is reached only from the in-place branch (`:1223-1227`). It is live only with `BB_GUEST_IN_PLACE=1` and `BB_SHADOWS=1`. Its own comment records 85 copies and 20 MB a frame for 48 to 44 FPS (`buffer_cache.h:298-302`). The default cost is trivial (`:1143`, `:3912`, `:3976`; `vk_rasterizer.cpp:5723`).
- **Fix.** Delete it. Record the removal and its measurement in `community-fixes-plan.md` with bbport's credit. The measurement is also evidence against copying in-place data into video memory (relevant to `mem-4`).

### 3.14 Empty `FrameCapture::Poll` called on every packet (`renderer-9`)

Priority 4, confirmed.

- **What and where.** The body is empty (`vk_frame_capture.cpp:148`). Its header comment (`vk_frame_capture.h:38-39`) is stale: state changes now happen in `OnFlip` and `BeginPass`. It is called at five sites: `vk_rasterizer.cpp:1516`, `1999`, `2034`, `2447`, `2608`.
- **Fix.** Remove it, or make it inline if a future poll is planned. No switch.

---

## 4. Bugs found

| # | Id | Title | Pri | Verdict |
|---|---|---|---|---|
| 4.1 | renderer-1 | Copy-shader pass statistics are wrong with the profiler on | 1 | confirmed |
| 4.2 | gnm-1 | The frame-overlap heap-leak guard never runs on Windows | 2 | confirmed |
| 4.3 | shader-1 | The default table walker drops draws on unmapped tables | 2 | partly |
| 4.4 | fence-1 | A wait on label+4 reads a pending 32-bit label's high half as 0 | 3 | confirmed |
| 4.5 | asc-1 | Compute-queue WRITE_DATA lands before earlier GPU work | 3 | partly |
| 4.6 | mem-5 | Copy-shader writes never mark textures over them stale | 3 | partly |
| 4.7 | upscale-5 | The edge clamp overwrites the scene colour's edge alpha | 4 | partly |
| 4.8 | shader-3 | Wave-size assumptions not checked per stage | 4 | partly |
| 4.9 | shader-7 | "Coherent" taken from the first draw, not part of the key | 4 | partly |
| 4.10 | shader-8 | An uneliminated `v_writelane` becomes the constant 0 | 4 | partly |

### 4.1 Beta 18/19 copy-shader pass statistics are wrong with the GPU profile on (`renderer-1`)

Priority 1, confirmed. Measurement bug.

- **Failure.** In `MultiCopy`, the profiler mark (`vk_shader_hle.cpp:126-127`) runs before `in_pass = IsRendering()` and the copy-shader tag (`:130-131`). That mark calls `WriteTimestamp`, which ends the pass (`vk_gpu_profiler.cpp:41`; it skips this only when its slice is held or full, `:36-37`). So with `BB_GPU_PROFILE=1`, "copy shader dispatches inside a pass" is never counted (`:154`). In beta 19 these splits read "profiler".

  Without the profiler, two other paths also end the pass before the tag:
  - a first batch of fewer than 8 ranges goes to `Runtime::CopyBuffer` (`vk_shader_hle.cpp:607-608`), which ends the pass (`vk_runtime.cpp:128`);
  - `ObtainBuffer` uploads end it too (`buffer_cache.cpp:3014`, `3033`, `3751`, `3755`).

  Those read "transfer", never "copy shader".
- **Why it matters.**
  - Beta 18's conclusion, "copy shader dispatches inside a pass: none ... running its copies before the pass gains nothing" (`community-fixes-plan.md:66`), came from a run with frame statistics and the GPU profile on. It is not evidence.
  - The plan's remark that "the GPU profiler is not it (it marks only where a pass begins anyway)" overlooks this mark. `MarkPass` fires only where a pass begins (`vk_rasterizer.cpp:6197-6200`); the copy-shader mark does not.
  - **The beta 19 test plan** (`community-fixes-plan.md:67`) again says "frame statistics and GPU profile on". Read against this finding, the beta 19 "render pass reasons" line from that run will show copy-shader splits as "profiler". This is this document's inference from the plan row and the skeptic's finding.
- **Fix.** Sample `in_pass` and tag once per game dispatch at the top of `ExecuteCopyShaderHLE`, before the `ObtainBuffer` calls (`:527`, `:588`). At minimum, move the mark after `:131`. Caveat: an upload's own `FlushBarriers` inside `ObtainBuffer` clears the tracker before `MultiCopy` checks it (`:137-148`), so "no barrier needed" can be over-counted.
- **Interim rule.** Read pass counts and pass reasons only from runs **without** `BB_GPU_PROFILE`. Take milliseconds from a separate profiled run.
- **Expected effect.** Correct inputs; it reopens "run copy-shader copies before the render pass". No FPS change by itself.
- **Switch.** None needed (measurement only).

### 4.2 The frame-overlap heap-leak guard never runs on Windows (`gnm-1`)

Priority 2, confirmed. The tester's platform.

- **Failure.** `BB_SUBMIT_AHEAD=2` lets the game build the next frame while the GPU finishes the previous one. The comment at `gnmdriver.cpp:134-140` records that too much overlap leaks the game's heap until it runs out of memory. `WatchOverlapLeak` (`:154-175`, called once a frame at `:2534`) is meant to turn overlap off when the heap grows. But it returns at `:158-160` whenever `BbHeapSites::Counting()` is false. On Windows that is always: the function is a stub (`bbport_windows_experiments.cpp:26-33`), and the real file is excluded from the Windows build (`gpu/CMakeLists.txt:216-220`). So the heap-growth test at `:169` never runs, although `runtime_heap_growths()` works everywhere (`runtime_memory.c:610-613`; printed at `driver.cpp:502`). The same gate also skips it on Linux when the malloc table is refused (`bbport_heap_sites.cpp:235-240`).
- **Fix.** Check `runtime_heap_growths()` before the `Counting()` gate. Keep the live-allocation test where counters exist. Print once on Windows that only the heap-growth test is active.
- **Evidence.** Code confirmed. No leak has been reported on Windows.
- **Expected effect.** No FPS change in normal play. Windows gets the safety net Linux has.
- **Risk.** Heap growth during area streaming (`runtime_memory.c:605-607`) would turn overlap off for the session, as on Linux. Overlap was worth 101 to 154 FPS at Yahar'gul, so **check that normal streaming does not trip it**.
- **How to test.** A long Windows session crossing areas: watch for the "frames no longer overlap (BB_SUBMIT_AHEAD 0)" line and the Load: line's "heap growths". Force it with `BB_SUBMIT_AHEAD=8`, which leaks per the comment.
- **Switch.** `BB_OVERLAP_GUARD=0`.

### 4.3 The default table walker drops draws on unmapped tables; switch to the table plan after a guard (`shader-1`)

Priority 2, partly confirmed. Robustness.

- **Failure.** The resource-table walker is still the default (`table_plan.cpp:287-300`). When it faults on a table pointer that is not null but unmapped, its handler zeroes the register and counts the fault (`flatten_extended_userdata_pass.cpp:89-138`; the warning at `:133-137` wrongly says "null table pointer"). The whole draw or dispatch is then dropped (`vk_rasterizer.cpp:2005`/`2015`, `2430-2432`, `2593-2595`, `2695-2697`). The doc already plans the next step: verify, flip the default, remove the walker (`native_shader_compiler.md:248-254`).
- **Skeptic corrections.**
  - The 45,084 faults are synthetic: lab case 3 deliberately points at reserved memory (`native_shader_compiler.md:196-198`, `211`). No in-game count exists.
  - The Ebrietas case (90 to 25 FPS) was null tables, which the walker already handles without a fault (`flatten_extended_userdata_pass.cpp:635-691`).
  - **A risk the mapper missed.** The table plan treats "mapped" as readable. `guest_readable` is `ClampRangeSize` (`shim/bbgpu.cpp:141-143`), and the memory checks look only at the region kind (`engine/src/runtime_memory.c:1043-1057`), not at the game's own page protection (`sceKernelMprotect`, `:647-656`). A table on a page the game made unreadable would fault in the plan's `memcpy` (`table_plan.h:150-178`), outside any handler, and **end the game**, where the walker would only skip the draw.
  - The plan's per-draw CPU cost against the walker is unmeasured (`vk_pipeline_cache.cpp:596`; `table_plan.h:130-160`).
- **Fix.**
  1. Make `guest_readable` require the game's read permission too.
  2. Run `BB_NATIVE_TABLES=verify` at Ebrietas and at the lamp spot. Log the "walker faulted" count, and compare stage A us a draw against `BB_NATIVE_TABLES=0`.
  3. Flip the default.
  4. Remove the walker in a later beta.
- **Evidence.** Suspected (corrected from "measured").
- **Expected effect.** No draws dropped on unmapped tables, and one table reader to maintain. Probably nothing measurable at the test spot.
- **Switch.** `BB_NATIVE_TABLES=0` restores the walker.

### 4.4 A wait on label+4 reads the high half of a pending 32-bit label as 0 (`fence-1`)

Priority 3, confirmed.

- **Failure.** When the exact lookup misses and the address ends in 4, the code returns the high 32 bits of whatever signal sits at address−4 (`liverpool.cpp:885-888`). The signal record has no size (`vk_rasterizer.h:890-895`). 4-byte values are registered by EOS (`liverpool.cpp:813-816`), by EOP with "Data32Low" (`:579-583`) and by 4-byte GPU WriteData (`vk_rasterizer.cpp:331-335`). So a wait on label+4 reads 0, and the wait counts as met if the comparison passes for 0 (`liverpool.cpp:1997-2006`). The window runs from stage B publishing the signal until the signal thread writes the label. The earlier window is covered by `pending_fences`, which is used with honest labels too (`:870-872`, `1864`, `1881`); the mapper's "non-honest" remark was slightly off.
- **Related wrong values fixed by the same change.**
  - Compute ReleaseMem registers an 8-byte value even for Data32Low, putting `data_hi` in the high half (`liverpool.cpp:2412-2421`).
  - EOP with the GPU clock or a performance counter registers an 8-byte data value, although the clock is what gets written (`:581-583`; `pm4_cmds.h:484-491`).
- **Fix.** Store the size in the signal record. Answer only inside `[label, label + size)`, and give a high half only for 8-byte signals.
- **Evidence.** Code confirmed; how often it happens is unknown. Add a counter in `PendingFenceValue` for hits of the high-half branch where the stored value fits in 32 bits.
- **Expected effect.** Correctness: no early pass of a command-processor wait.
- **Risk.** Very low; a miss falls back to polling real memory.
- **How to test.** The counter over 10 minutes of play. Any non-zero count means the bug was live.
- **Switch.** `BB_FENCE_HALF_CHECK=0`.

### 4.5 Compute-queue WRITE_DATA is stored by the CPU before earlier GPU work has run (`asc-1`)

Priority 3, partly confirmed.

- **Failure.** On the compute queues, WRITE_DATA is a CPU `memcpy` at decode (`liverpool.cpp:2352-2370`). Only the wait for stage B comes before it, and that means earlier work is recorded, not executed. The graphics queue writes on the GPU in stream order instead (`RunWriteData`, `:699-707`; `vk_rasterizer.cpp:317-337`).
- **Skeptic correction.** The cause is narrower than claimed. Compute WaitRegMem polls real memory (`:2387-2397`), so "wait on a fence, then write" is already ordered. The real exposures are two:
  - EVENT_WRITE (for example a compute flush) is ignored on compute (`:2449-2452`), so "dispatch, flush, WRITE_DATA" lands before the GPU ran the dispatch;
  - a WRITE_DATA used as a completion label becomes visible to the game early. That is the same kind of dishonest label that honest labels fixed for EOP and EOS.
- **Fix.** Call `RunWriteData` itself, not only `WriteDataOnGpu`. That keeps its address-targeted waits on the CPU fallback instead of the full waits at `:2356-2357`. Also call `NotePendingGpuWrite`. This fits the design: compute DmaData already calls the rasterizer from stage A (`:2226-2262`).
- **Evidence.** Code confirmed. Whether Bloodborne issues compute WRITE_DATA at all is unknown; count it first.
- **Expected effect.** Correctness only.
- **Switch.** `BB_ASC_WRITE_ON_GPU=0`.

### 4.6 Copy-shader writes never mark textures over their destination stale (`mem-5`)

Priority 3, partly confirmed.

- **Failure.** The HLE calls no texture-cache function (`vk_shader_hle.cpp`; destination at `:531`/`:592`). The game's own shader, without the HLE, would have gone through `BindBuffers`' `InvalidateMemoryFromGPU` (`vk_rasterizer.cpp:4116-4120`). That function matches only an image's base address (`texture_cache.cpp:691-703`).
- **Skeptic corrections.**
  - The proposed global "invalidate by overlap" is **unsafe**. An image marked GPU-dirty is re-uploaded whole from memory: `RefreshImage` bypasses the hash guard (`texture_cache.cpp:1336`) and reads the full guest size (`:1377`). An image the GPU rendered is never written back to memory, so marking it on a partial overlap would replace its untouched texels with stale memory.
  - Changing `InvalidateMemoryFromGPU` also changes every written storage binding (`vk_rasterizer.cpp:4119`).
  - The mapper's example does not hold: beta 13's images are made for the other copy shader (`3d5ebf4e`, `vk_rasterizer.cpp:3821`). `CheckMadeForCopy` (`buffer_cache.cpp:1098-1105`) already sees writes to images made for a copy.
- **Fix.**
  1. Measure only: log once per range when a copy-shader destination overlaps a registered image, with whether that image is GPU-modified.
  2. If the log fires, add the base-address call the game's shader would have had, or a subresource-precise invalidation.
- **Evidence.** Suspected; no artifact has been reported.
- **Switch.** `BB_HLE_IMAGE_INVALIDATE` (for the fix, if any).

### 4.7 The edge clamp overwrites the game's alpha in the scene colour's last row and column (`upscale-5`)

Priority 4, partly confirmed. Affects DLAA (the tester's setting) and TAA at 1080p.

- **Failure.** On the native path, the upscaler's input is the game's scene colour (`vk_temporal_upscaler.cpp:1350`). `ClampSceneEdges` copies whole texels, alpha included (`:79-101`, called at `:1363-1364`). The merge then keeps the image's alpha (`upscale_merge.comp:28,44`), which is now the neighbour's. The reduced path is unaffected.
- **Skeptic correction.** Whether this is visible cannot be determined:
  - the clamp exists because the game's passes leave the edge wrong in some jitter phases (`:62-65`);
  - the same passes may write that alpha;
  - what the alpha holds is not documented.

  Two of the three barriers are full ones (`:72`, `:105`). "Apply it on the input view only" is not possible.
- **Fix.** An RGB-only compute clamp with one barrier. Its main value is as the precondition for `upscale-2`. The time saved is negligible.
- **How to test.** `BB_DUMP_TRIGGER` "input" dumps at DLAA: compare the edge alpha with jitter on and off. Check the right and bottom edges visually; GitHub issue 43's shimmer must stay fixed.
- **Switch.** `BB_EDGE_CLAMP_COPY=1`.

### 4.8 Wave-size assumptions are fixed per device, not checked per stage (`shader-3`)

Priority 4, partly confirmed. Not visible on NVIDIA.

- **Failure.**
  - A required subgroup size of 64 goes on every stage whenever the device supports 64 (`vk_graphics_pipeline.cpp:368-373`; `vk_compute_pipeline.cpp:27-31`; `vk_instance.h:286-289`). The check never looks at which stages allow it, so on drivers that allow it for compute only, graphics stages break the Vulkan spec.
  - The wave64 emulation hard-codes 32-lane maths (`lower_wave64_pass.cpp:87-90`, `143-160`). On Intel, subgroups may be 8 or 16 wide.
- **Skeptic correction.** The shared-memory-barrier part is moot for Bloodborne: it applies only to compute shaders with shared memory and exactly 64 threads, and the census finds shared memory only in tessellation stages (`native_shader_compiler.md:83`). The real exposure is Intel compute shaders that use ballot or readlane with more than 32 threads: a handful (3 with ballots, 12 with readlane/writelane).
- **Fix.** Require 64 only for stages the driver lists. On Intel, decide per shader: only compute programs using ballot, lane id or similar get a required full subgroup. Do not force SIMD32 on everything, which costs registers. The wave64 pass reads the size from that decision.
- **How to test.** The validation layer on an AMD driver (VUID-VkPipelineShaderStageCreateInfo-pNext-02755). On Intel: log the chosen size, and read the "Lowering ... for wave64" and "non uniform control flow" lines.
- **Switch.** `BB_WAVE_STRICT=0`.

### 4.9 "Coherent" buffers are decided by the first draw's memory type, which the cache key ignores (`shader-7`)

Priority 4, partly confirmed.

- **Failure.** Whether a written buffer is declared Coherent comes from the memory-type field (MTYPE) of whichever draw compiled the shader first (`spirv_emit_context.cpp:924-931`, `890-895`). The permutation key has no such field (`specialization.h:24-43`). So later draws with another MTYPE reuse that module.
- **Skeptic correction.** The program's own per-instruction GLC bit (`globally_coherent`, `frontend/translate/vector_memory.cpp:223`, `291`, `359`) is probably the right signal. It is unused for this today (read only in `ring_access_elimination.cpp:66-130`). Deciding Coherent from it needs no extra permutations.
- **Fix.** Measure first: log when the MTYPE at draw time differs from the compiled one. Then decide Coherent from the GLC bits, or add the MTYPE bit to the key. In that case the shader metadata version must change.
- **Expected effect.** None known at the test spot.
- **Switch.** `BB_COHERENT_FROM_MTYPE=1` (name suggested here).

### 4.10 A `v_writelane` the elimination pass cannot remove becomes the constant 0 (`shader-8`)

Priority 4, partly confirmed.

- **Failure.** `EmitWriteLane` returns zero (`backend/spirv/emit_spirv_warp.cpp:41-43`). The elimination pass runs once and gives up when anything other than ReadLane, Phi or Undef sits between a write and its readers (`readlane_elimination_pass.cpp:21-56`; `recompiler.cpp:122`). What escapes it silently reads as zero, with no log.
- **Skeptic correction.** The mapper's per-lane select is not correct for every subgroup size. In 32-wide vertex and pixel shaders, lanes 32 to 63 do not exist. The translator's lane id is the subgroup invocation id (`emit_spirv_warp.cpp:17-19`). And the lane can be a register, not only a constant (`data_share.cpp:113-117`).
- **Fix.**
  1. A once-per-shader log and a lab count of WriteLanes reaching emission.
  2. If the count is non-zero, make the elimination complete: treat each (register, constant lane) pair as a variable and run SSA construction.
  3. Handle a non-constant lane explicitly (log or assert).
- **Switch.** `BB_WRITELANE_SSA=0` for the fix (name suggested here).

---

## 5. Refuted ideas (do not chase)

- **`mem-1`: 128 MiB staging buffer per depth/colour copy without maintenance8.** Already fixed. Beta 19's plan row (`community-fixes-plan.md:67`, item 3, "this commit") sizes the request to the copy, and beta 19 also moves these copies to shaders. Beta 18 counted zero such copies on the tester.
- **`mem-2`: skip the upload when converting between depth and colour images.** Already on the list with a better root fix (`community-fixes-plan.md:67`). Beta 18 counted zero conversions. The skip is also unsafe: it leaves the image's memory hashes unrecorded, which reopens the "black character-creator preview" bug that the guard at `texture_cache.cpp:1319-1337` fixed.
- **`mem-6`: tag uploads and transfers as pass enders, then move them before the pass.** Beta 19 tags every pass ender. The transfers it lists are rare in play. The likely real causes (draw-time barrier flushes; submissions clearing the resume flag, `vk_scheduler.cpp:1131-1137`) were missing from it. Its second step had no number and a high ordering risk.
- **`shader-4`: GDS append and consume, one atomic per subgroup.** Shipped in beta 19 (`BB_GDS_APPEND=lane` restores per-lane atomics). Its effect on `cs a954e79d` is still to be measured. The shipped pattern relies on reconvergence that core Vulkan does not strictly guarantee without `VK_KHR_shader_maximal_reconvergence`. It is the standard idiom every major driver handles, so it is not a defect to act on.
- **`shader-10`: retire `BB_UBO_EXACT`, `BB_EARLY_Z`, `BB_UBO_STAGES`.** No gain: one branch per buffer. `BB_EARLY_Z` was re-measured in 0.4.4 (`community-fixes-plan.md:61`) and is the console-faithful comparison mode (`profile.h:71-73`). No recorded result settles `BB_UBO_STAGES`' question.

---

## 6. Open questions and what to measure next

### 6.1 A first test session that needs no new code

Run each at the lamp spot (DLAA, 120 preset) on beta 19 unless noted.

1. **Pass reasons without the profiler.** `BB_FRAME_STATS=1` **without** `BB_GPU_PROFILE`: the render-pass reasons and barrier-wait kinds (beta 19). Then a separate profiled run for milliseconds. This works around `renderer-1` and feeds `renderer-3`, `renderer-4` and `seg-1`.
2. **Breadcrumbs.** `BB_BREADCRUMBS=0` against the default, three runs each: GPU busy and FPS (`renderer-7`).
3. **Sharpening.** The menu's sharpening on against off: the "upscaler Run (DLSS)" label (bounds `upscale-1`).
4. **Read rounding.** Toggle bit 2048 flipped live with `BB_TOGGLE_FILE`: HLE barrier flushes, beta 19's barrier counts, stage B time (`renderer-6`).
5. **Copy-shader paths.** The existing `HLE copy ObtainBuffer paths` line: source in place against stream (`mem-4`), destination in place against VRAM (`mem-3`).
6. **Beta 19's own test.** The `dispatch cs a954e79d` label, default against `BB_GDS_APPEND=lane`.
7. **Sections.** `BB_SECTIONS=1`: `GpuIdleHonest` count and us a second (`idle-1`), and "stage A us per draw" (`clean-4`).
8. **Load line.** The 60-second "Load:" line: rwlock rate and steps (`rt-1`), and on Windows "heap growths" over a long session (`gnm-1`).
9. **At a CPU-bound spot** (`BB_FPS_LIMIT=0`, ideally a 6-core CPU): "drains by reason/s: compute" (`asc-2`); lookahead via experiment bit 16 and `BB_NATIVE_CALLS=1/0` (`clean-3`); stage B us a packet as the `pipe-1` baseline.
10. **At DLSS Quality** (a reduced preset): "scene proxy resolves per second by native access", the "scene proxy resolve" and "scene proxy fill" labels, and `BB_SCENE_DEBUG`'s "Scene fill:" lines (`upscale-2`, `upscale-3`).

### 6.2 Small measurement code worth adding (no behaviour change)

- **Pass statistics:**
  - the `renderer-1` fix (tag copy-shader splits once per game dispatch);
  - `renderer-2`'s restart categories, plus a resume count (`seg-1` step 1).
- **Time attribution:**
  - profiler marks inside `Run`: motion compose, upscaler evaluate, sharpen, merge. This is the only way to learn DLSS's own share of the 1.27 ms;
  - a label (or counter) for the copy shader's `vkCmdCopyBuffer` batches, which today charge their time to neighbouring labels (`renderer-8`).
- **Correctness counters:**
  - `fence-1`'s high-half counter;
  - `asc-1`'s count of compute WRITE_DATA packets;
  - `mem-5`'s overlap log;
  - `shader-9`'s push-descriptor fallback counter;
  - `shader-8`'s WriteLane count (lab);
  - `shader-7`'s MTYPE mismatch log.

### 6.3 What the agents could not determine

**GPU**

- What the 13 passes a frame of 32768x32768 with no attachments are (0.1 ms).
- How the 1.27 ms "upscaler Run (DLSS)" label splits between DLSS and the port's own passes.
- What fills the 2.9 to 3.1 ms of the 8.48 ms profiled frame not covered by the listed labels.
- What `cs a040875f` does. How much of any labelled dispatch's time is work, as opposed to launch and drain forced by the profiler. The profiler ends the pass and writes a full-wait timestamp at each mark (`vk_gpu_profiler.cpp:35-47`), and the dispatch mark follows `FlushBarriers` (`vk_rasterizer.cpp:2640-2644`). This needs marks reduced to frame boundaries, or Nsight or RGP.
- The total barrier flushes a frame outside the copy shader. Beta 19's barrier counts may now give it.
- Restart and barrier costs on AMD and Intel. Every unit cost here is from the tester's NVIDIA card.
- What depth layout DLSS (NGX) expects in Vulkan.
- What the game stores in the scene colour's alpha.
- Whether the game's tonemapped UI target is ever sRGB on the scaled path. If it is, `ExtraSharpen`'s LDR path would shift gamma with FSR 3 at sharpness above 1, and so would the FSR 3 scaled output.
- The presenter's costs (host post pass, swapchain copy, FSR 1). They run on the presenter's own scheduler, which `BB_GPU_PROFILE` does not cover.
- How many uniform buffers take the "copied by the GPU to an aligned place" path (a transfer plus a forced barrier per draw, `vk_rasterizer.cpp:4092-4105`). The "Uniform buffers per frame" line in `BB_FRAME_STATS` shows it.

**CPU**

- Stage B's current split on beta 17: `ObtainBuffer`, texture binding, write stamps, and object motion per draw. Every per-step figure predates GX-in-place.
- Stage A's real work per draw, and how often it runs dry.
- How many compute-queue submissions and dispatches the game makes.
- What the DrawPipe's cache-line sharing costs.
- `present_wait_ns` (the `BB_FRAMES_AHEAD` wait). It may be redundant with the game's own wait under honest labels.
- Guest lock rates.

**Memory and caches**

- Which memory type the 128 MiB stream buffer gets on the tester's PC.
- How many bytes the copy shader's spans cover beyond their ranges.
- Whether any copy-shader destination overlaps an image.
- Why `BB_HLE_COPY_MERGE` corrupted ground textures.
- Whether `BB_GPU_WRITE_TWINS`, `BB_PREUPLOAD`, `BB_CHUNK_*` and the PC memory model's promotion code have results. Whether `BB_FREE_CHECK`, `BB_LABEL_TRAP` (1,095 lines) and `BB_HEAP_SITES` still serve an open crash.

**Shaders**

- Whether any program is used with different memory types (`shader-7`).
- Whether any WriteLane reaches emission (`shader-8`).
- What the AMD and Intel drivers report for the alignment limits (`shader-2`) and for the stages that allow a required subgroup size (`shader-3`).
- Whether anything besides the motion vertex shader reads the history's z. Only `vk_object_motion.cpp` was searched.

**Identity**

- Whether the copy shader's hash is `fefebf9e` or `fefebf9f` in the logs.

### 6.4 Status of the "known open ideas" from the task

- **Run copy-shader copies whose source cannot change before the render pass.** Reopened: the data that closed it is invalid (`renderer-1`). Decide after 6.1 item 1 and the `renderer-1` fix.
- **A cheaper "still" motion variant.** No agent examined it. The bound from beta 16: the whole motion variant costs about 0.4 ms, the history about 0.1 ms.
- **Colour-to-depth copies by a draw.** Done in beta 19.
- **Scene proxies freed only when the preset changes.** No agent examined it.
- **The two small compute shaders.** `a954e79d` is addressed by beta 19's subgroup appends, with the result pending. For `a040875f`, the shader area found its ~5 us a dispatch to be about the fixed cost of a dispatch on a drained GPU. It is unlikely to shrink through translation; profiling without per-dispatch drains would show its real cost.

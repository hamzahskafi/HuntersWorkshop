#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
if [[ ${1:-} == --software ]]; then
    shift
    if [[ -z ${VK_DRIVER_FILES:-} ]]; then
        for candidate in /run/opengl-driver/share/vulkan/icd.d/lvp_icd*.json /usr/share/vulkan/icd.d/lvp_icd*.json; do
            if [[ -f $candidate ]]; then export VK_DRIVER_FILES=$candidate; break; fi
        done
    fi
    if [[ -z ${VK_DRIVER_FILES:-} ]]; then echo 'Lavapipe not found; set VK_DRIVER_FILES.' >&2; exit 1; fi
    export VK_LOADER_LAYERS_DISABLE='~implicit~'
fi
# BB_PREBUILT=1 (packaged builds): out/bb-probe and its GPU library are installed next to this
# script; nothing is built.
# BB_DATA_DIR: writable directory for the generated files (out/), saves (user/) and config.ini;
# by default this directory. BB_OUT_DIR: the generated files elsewhere (the Linux package keeps
# them in engine/out beside its programs, as on Windows; packaging/linux/bb-start).
data=${BB_DATA_DIR:-.}
out=${BB_OUT_DIR:-$data/out}
mkdir -p "$out"
export BB_CONFIG=${BB_CONFIG:-$data/config.ini}
# 0.4.4 beta 28: switches that stand for several (start/main.cpp ExpandSwitchGroups holds the same table;
# tests/test_switch_groups.py checks they agree). Safe mode (BB_SAFE_MODE=1) sets its values whatever
# was set; the others turn off their parts, except a part set on its own.
# switch-groups-begin
switch_groups=(
    "BB_SAFE_MODE=1 force BB_FAST_LOADING=0 BB_FRAME_FIXES=0 BB_HOT_PAGES=0 BB_GX_IN_PLACE=0 BB_NATIVE_CALLS=0 BB_PRECISE_PACING=0 BB_VBLANK_PACING=0 BB_INDIRECT_CLAMP=1 BB_GPU_THREAD_PRIORITY=0 BB_AUDIO_THREAD_PRIORITY=0 BB_NATIVE_SHADERS=0 BB_SLEEP_SPIN_US=600 BB_ASYNC_PIPELINES=0 BB_OCCLUSION=0 BB_MODS_ENABLED=0 BB_FAST_DRAWS=0 BB_COMPILE_SKIP=0 BB_VRAM_MANAGER=1 BB_EMPTY_SURFACES=0 BB_TARGET_POOL=0 BB_WATCH_GAME_TEXTURES=1"
    "BB_FAST_DRAWS=0 keep BB_SUBMIT_END_TASKS=0 BB_MEMO_REVALIDATE=0 BB_SAMPLER_MEMO=0 BB_DRAIN_SLEEP=0 BB_FAULT_SKIP_TEXTURES=0"
    "BB_COMPILE_SKIP=0 keep BB_SKIP_UNCOMPILED=0 BB_LATE_PIPELINE_BIND=0 BB_PIPELINE_TAKE=0"
    "BB_SYNC=legacy keep BB_HONEST_LABELS=0 BB_GX_IN_PLACE=0 BB_PIPE_SIGNALS=0 BB_SUBMIT_LOCK=gpu BB_WORK_RETIRED=0 BB_ZPASS_IN_ORDER=0 BB_FENCE_ORDER=all BB_DMA_WAITS=all BB_HOST_COPY_WAITS=all BB_PRODUCER_CHECK=1"
)
# switch-groups-end
for group in "${switch_groups[@]}"; do
    read -r trigger mode sets <<< "$group"
    name=${trigger%%=*}
    [[ ${!name:-} == "${trigger#*=}" ]] || continue
    read -ra assignments <<< "$sets"
    for assignment in "${assignments[@]}"; do
        part=${assignment%%=*}
        if [[ $mode == force || -z ${!part+set} ]]; then export "$assignment"; fi
    done
    echo "Engine: $trigger sets ${#assignments[@]} switches"
done
# The diagnostics as one list, BB_DIAG=part,part,... (the launcher's Log page): each part stands for
# engine switches, set unless set on their own. "hitch=N" takes the hitch report's threshold in ms
# (40 without one), "threads=a+b" the thread samplers' names. Each entry: the part, its value when
# none is given (- for none), what it sets (an empty value takes the part's value). The same table
# is in start/main.cpp (kDiagParts); tests/test_switch_groups.py checks they agree.
# diag-parts-begin
diag_parts=(
    "frame - BB_FRAME_STATS=1"
    "hitch 40 BB_HITCH_REPORT_MS="
    "gpu - BB_GPU_PROFILE=1"
    "sections - BB_SECTIONS=1"
    "load - BB_SAMPLE_THREADS=load"
    "audio - BB_AUDIO_STATS=1"
    "pad - BB_PAD_LOG=1"
    "heap - BB_WRITE_LOG=1 BB_FREE_CHECK=1 BB_HEAP_SITES=1"
    "shaders - BB_SHADER_CAPTURE=1"
    "timeline - BB_TIMELINE=1"
    "waits - BB_WAIT_TRACE=1"
    "threads - BB_SAMPLE_THREADS="
)
# diag-parts-end
if [[ -n ${BB_DIAG:-} ]]; then
    IFS=, read -ra diag_items <<< "$BB_DIAG"
    diag_named=
    for item in "${diag_items[@]}"; do
        item=${item//[[:space:]]/}
        [[ -n $item ]] || continue
        diag_name=${item%%=*}
        diag_name=${diag_name,,}
        diag_value=
        [[ $item == *=* ]] && diag_value=${item#*=}
        diag_value=${diag_value//+/,}
        diag_found=
        for entry in "${diag_parts[@]}"; do
            read -r part fallback sets <<< "$entry"
            [[ $part == "$diag_name" ]] || continue
            diag_found=1
            [[ -n $diag_value || $fallback == - ]] || diag_value=$fallback
            read -ra assignments <<< "$sets"
            for assignment in "${assignments[@]}"; do
                variable=${assignment%%=*}
                value=${assignment#*=}
                [[ -n $value ]] || value=$diag_value
                if [[ -n $value && -z ${!variable+set} ]]; then export "$variable=$value"; fi
            done
        done
        if [[ -z $diag_found ]]; then echo "Diagnostics: no part \"$diag_name\" in BB_DIAG"; continue; fi
        diag_named+=${diag_named:+, }$diag_name${diag_value:+=$diag_value}
    done
    [[ -z $diag_named ]] || echo "Diagnostics: $diag_named (BB_DIAG)"
fi
# BB_SAVE_LOG=1 (launcher: "Save the log and statistics to a file"): this run's output and its
# per-frame statistics also go to $data/logs/<time>.log, .frames.csv and .readbacks.csv.
if [[ ${BB_SAVE_LOG:-} == 1 ]]; then
    logs=$data/logs
    mkdir -p "$logs"
    printf -v stamp '%(%Y%m%d_%H%M%S)T' -1
    export BB_FRAME_STATS=1 BB_FRAME_LOG=${BB_FRAME_LOG:-$logs/$stamp.frames.csv}
    export BB_READBACK_LOG=${BB_READBACK_LOG:-$logs/$stamp.readbacks.csv}
    echo "Log: $logs/$stamp.log"
    exec 3>"$logs/$stamp.log"
    # Bash builtins only (a minimal PATH may have no tee).
    exec > >(while IFS= read -r line || [[ -n $line ]]; do printf '%s\n' "$line"; printf '%s\n' "$line" >&3; done) 2>&1
fi
if [[ -z ${PYTHON:-} ]]; then
    PYTHON=$(command -v python3 || true)
fi
if [[ -z ${PYTHON:-} ]]; then echo 'Install Python 3 or set PYTHON.' >&2; exit 1; fi
# BB_GAME_DIR: the game's folder (eboot.bin, sce_module, ...); default next to this directory.
game=${BB_GAME_DIR:-../CUSA03173}
if [[ ! -f $game/eboot.bin ]]; then echo "No eboot.bin in $game (set BB_GAME_DIR)." >&2; exit 1; fi
# Configure the selected online backend on Linux, as bb-start does on Windows.
# The launcher already supplies the signed-in account in the environment. Capture changes
# privately: credentials must never be printed into the game log or evaluated as shell code.
online_env=$("$PYTHON" scripts/online_env.py --config "$BB_CONFIG" --data "$data")
while IFS= read -r line; do
    case $line in
        env:*) export "${line#env:}" ;;
        unset:*) unset "${line#unset:}" ;;
        log:*) printf '%s\n' "${line#log:}" ;;
    esac
done <<< "$online_env"
unset online_env line
original_game=$game
game=$("$PYTHON" scripts/mods.py "$game" --out "$out" \
    --mods-dir "${BB_MODS_DIR:-$data/mods}" --config "${BB_MODS_CONFIG:-$data/mods.json}" \
    --enabled "${BB_MODS_ENABLED:-1}")
# A private merged view lasts for this launch, including restarts. Cleanup only our own view.
if [[ $game != "$(realpath "$original_game")" ]]; then
    mod_game=$game
    trap '"$PYTHON" -c '\''import shutil,sys; shutil.rmtree(sys.argv[1])'\'' "$mod_game"' EXIT
fi
"$PYTHON" scripts/prepare.py "$game" --out "$out"
"$PYTHON" scripts/link_libc.py "$game" --out "$out"
"$PYTHON" scripts/link_modules.py "$game" --out "$out"
"$PYTHON" scripts/content_profile.py "$game" --out "$out" --sku "${BB_CONTENT_SKU:-full}"
# The engine's own features (the executable's identity, which every engine hook checks: keyboard
# and mouse, PC menus, camera, widescreen...; the Dream mirror and PC menus), as bb-start.exe does
# on Windows. Never taken from the caller's environment.
unset BB_DREAM_MIRROR_ASSET BB_ENGINE_IMAGE_SHA256 BB_FILE_FIXES BB_PC_MENU_ASSETS BB_REBIRTH_ASSET
while IFS= read -r line; do
    if [[ $line == env:* ]]; then export "${line#env:}"; else printf '%s\n' "$line"; fi
done < <("$PYTHON" scripts/engine_features.py "$game" --out "$out" --data "$data" --config "$BB_CONFIG")
# Sizes chosen below for the previous launch are recomputed after an in-game restart.
if [[ ${BB_AUTO_RENDER_RES:-} == 1 ]]; then
    unset BB_RENDER_RES BB_OUTPUT_RES BB_AUTO_RENDER_RES
fi
# BB_RENDER_RES=WxH explicitly sets the game's render resolution (a patch at start).
# Frame rate: BB_FPS=uncap (default; vblank 480 Hz, frames shown at once), 60/90/120/144/240 or 30
# (unpatched). Above 30 the 60 FPS++ patch, the engine setting the pace and the values for the rate
# (frame_rate.cpp; BB_FRAME_FIXES=0: the older FPS++ patches above 60). BB_PATCHES adds patch names ("a;b").
# Fixes for the game's own bugs (gpu/shim/engine/game_fixes.cpp; the recording's compression level in
# scripts/patches.py): BB_GAME_FIXES=0 leaves them out, BB_GAME_FIXES_SKIP the ones it names (job_lock,
# job_nodes, job_slots, blur_samples, fmod_ring, hit_cap, gx_query, deflate).
# The log (the launcher's Settings > Log, capture and extra variables): BB_LOG_LEVEL=detailed brings
# back every save mount, thread, semaphore and memory map, the draw counts every 5 s, the job pools'
# caches and the GPU side's info messages (the driver's missing extensions and formats among them);
# debug adds the GPU side's debug messages (a large log, slower). BB_PAD_LOG=1:
# controller details (ids, connection, rumble, disconnects, the first vibration request, round gate).
# BB_HITCH_REPORT_MS=N: a frame over N ms gets every thread's wait and stack (about twice the frame time).
fps=${BB_FPS:-uncap}
# 0.4.4 beta 22: 240 is a fixed rate on the frame-rate corrections; without them (BB_FRAME_FIXES=0) the
# patch file's lists stop at 144.
if [[ $fps == 240 && ${BB_FRAME_FIXES:-1} == 0* ]]; then
    echo '240 FPS needs the frame-rate corrections (BB_FRAME_FIXES=0): running at 144 FPS' >&2
    fps=144
fi
# The engine reads it too (fast loading above 60, frame generation's limit with uncapped).
export BB_FPS=$fps
# config.ini output_res other than 1080p (720p for the Steam Deck, 1440p, 2160p): the whole game
# renders at the preset's size of the output (a patch), the upscaler fills the output, the UI is
# drawn at the output size. Preset and output changes need a restart.
# Live resolution changes keep the guest at 1920x1080 and scale host targets at run time instead
# (output and presets change in the menu without a restart, but post-processing stays at 1080p
# and scene targets are copied back: much slower on the Steam Deck and older GPUs). Chosen by
# config.ini live_resolution=0/1; off when unset (recommended). "auto" (a GPU
# check) is no longer offered from 0.4.4 beta 25c and reads as off. 1080p output and TAA always use
# the live path.
if [[ -z ${BB_RENDER_RES:-} ]]; then
    read -r scaled_render scaled_output < <("$PYTHON" scripts/patches.py --print-scaled --settings "$BB_CONFIG") || true
fi
live=0
if [[ -n ${scaled_output:-} ]]; then
    # Bash builtins only: a minimal PATH may have no sed/grep (a missing one ended run.sh silently).
    if [[ -f $BB_CONFIG ]]; then
        while IFS= read -r line || [[ -n $line ]]; do
            [[ $line =~ ^live_resolution=([01]|auto)$ ]] && live=${BASH_REMATCH[1]}
        done < "$BB_CONFIG"
    fi
    if [[ $live == auto ]]; then
        echo "Settings: live_resolution=auto is no longer offered; off (recommended)"
        live=0
    fi
    [[ $live == 1 ]] || live=0
fi
if [[ $live == 1 ]]; then
    echo "Output ${scaled_output}: live resolution changes (live_resolution=0: startup patch)"
elif [[ -n ${scaled_output:-} ]]; then
    export BB_RENDER_RES=$scaled_render BB_OUTPUT_RES=$scaled_output BB_AUTO_RENDER_RES=1
    export BB_DMEM_MB=${BB_DMEM_MB:-9152}
    echo "Output ${scaled_output}: scene ${scaled_render}, direct memory ${BB_DMEM_MB} MiB (live_resolution=1: live changes)"
fi
"$PYTHON" scripts/patches.py --out "$out" --fps "$fps" --extra "${BB_PATCHES:-}" --settings "$BB_CONFIG" --game-dir "$game" --render-res "${BB_RENDER_RES:-}" --output-res "${BB_OUTPUT_RES:-}" \
    --patches-dir "${BB_PATCHES_DIR:-$data/patches}" --patches-config "${BB_PATCHES_CONFIG:-$data/patches.json}"
# The GPU cache as bb-start.exe keeps it on Windows: recompiled by bb-shader-lab when the shader
# compiler changed, then the shipped shader catalog seeded, so pipelines are built at the start
# instead of in play. Packages only (packaging/linux/bb-start sets BB_GPU_USER_DIR).
if [[ -n ${BB_PREBUILT:-} && -n ${BB_GPU_USER_DIR:-} ]]; then
    "$PYTHON" scripts/gpu_cache.py --cache "$BB_GPU_USER_DIR/cache" --engine . --out "$out" \
        --package "$data/package.json" --game "$game" || echo 'GPU cache: not prepared (scripts/gpu_cache.py failed)' >&2
fi
# Background upload of the game's GPU memory into VRAM ahead of use (BufferCache::Preupload):
# 1 = only memory already in VRAM that the game rewrote (no new VRAM), 2 = all of it (~3 GB more
# VRAM), 0 = off.
export BB_PREUPLOAD=${BB_PREUPLOAD:-1}
# MangoHud (MANGOHUD=1) must be drawn once: the Steam Deck's performance overlay (mangoapp, game
# mode) plus the in-game layer showed doubled, offset text.
if [[ ${MANGOHUD:-0} == 1 ]] && pgrep -x mangoapp > /dev/null 2>&1; then
    echo "MangoHud: Steam's performance overlay is on; the in-game MangoHud stays off"
    unset MANGOHUD
    export DISABLE_MANGOHUD=1
fi
# The command buffers of each submission are copied when the game submits them and decoded from
# the copy: while an area loads the game reused that memory before the GPU thread got to it
# ("Unimplemented PM4 type 0" a few seconds after loading a save). 0 decodes guest memory.
export BB_COPY_GPU_BUFFERS=${BB_COPY_GPU_BUFFERS:-1}
# A guest write next to small GPU outputs the GPU is still writing (counters, compute results,
# up to BB_GPU_WRITE_TWINS_MAX bytes per page) does not wait for the GPU (BufferCache twins).
export BB_GPU_WRITE_TWINS=${BB_GPU_WRITE_TWINS:-1} BB_GPU_WRITE_TWINS_MAX=${BB_GPU_WRITE_TWINS_MAX:-65536}
if [[ -z ${BB_VBLANK_HZ:-} ]]; then
    # 0.4.4 beta 22: 120, 144 and 240 at their own rate, as on Windows (60 before: their frame limit
    # followed it, so they ran at 60).
    case $fps in uncap) export BB_VBLANK_HZ=480 ;; 90|120|144|240) export BB_VBLANK_HZ=$fps ;; *) export BB_VBLANK_HZ=60 ;; esac
fi
if [[ -n ${BB_PREBUILT:-} ]]; then
    probe=${BB_PROBE:-bin/bb-probe}
else
    bash build.sh
    probe=${BB_PROBE:-out/bb-probe}  # BB_PROBE: a wrapper (gdb) around it
fi
probe_args=("$out/boot-linked.bin" --content-profile "$out/content.bin" --patches "$out/patches.bin" --app0 "$game" --user "${BB_USER_DIR:-$data/user}" --timeout "${BB_TIMEOUT:-0}" "$@")
if [[ -n ${mod_game:-} ]]; then
    "$probe" "${probe_args[@]}" &
    mod_pid=$!
    trap 'kill -TERM "$mod_pid" 2>/dev/null || true' TERM INT
    mod_status=0
    wait "$mod_pid" || mod_status=$?
    # An interrupted wait must finish the child before removing its mounted view.
    if kill -0 "$mod_pid" 2>/dev/null; then wait "$mod_pid" || mod_status=$?; fi
    exit "$mod_status"
fi
exec "$probe" "${probe_args[@]}"

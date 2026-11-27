// SPDX-License-Identifier: GPL-2.0-or-later
// bb-start.exe: starts Bloodborne PC natively on Windows (0.9.32 replaced the Python start with it).
//
//     bb-start.exe [--game-dir DIR] [bb-probe options...]
//
// The game folder: --game-dir, else BB_GAME_DIR, else the last one used (out/game_dir.txt).
// Then: the online profile, the GPU caches of another shader compiler cleared, mods, the boot
// image (prepared again only when the game or this program changes), the engine's assets, the
// patches, and bb-probe with the session log as its output. Its exit code is this program's.
#include "assets.h"
#include "common.h"
#include "elf.h"
#include "mods.h"
#include "online.h"
#include "patches.h"
#include "prepare.h"
#include "splash.h"

#include <windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>


namespace start {
namespace {

fs::path SelfPath() {
    wchar_t buffer[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
    return fs::path(std::wstring(buffer, n));
}

struct Folders {
    fs::path engine;   // engine\ (out\, patches\, data\)
    fs::path package;  // the package: package.json, config.ini, user, mods, logs
    fs::path out;      // engine\out: the programs and what each start generates
};

Folders Locate() {
    Folders f;
    f.out = SelfPath().parent_path();
    f.engine = f.out.parent_path();
    std::error_code error;
    f.package = Lower(Utf8(f.engine.filename())) == "engine" && fs::is_regular_file(f.engine.parent_path() / "package.json", error)
                    ? f.engine.parent_path()
                    : f.engine;
    return f;
}

std::optional<std::string> SettingValue(const fs::path& config, const std::string& key) {
    return Setting(ReadSettings(config), key);
}

// 0.4.6 beta 17: the GPU cache is settled by the engine at its start (a new engine or profile: its
// states translated again; the shipped catalog seeded with this PC's profile): PipelineCache::WarmUp,
// docs/memory-model.md, "First sight". bb-start's RecompileGpuCache, SeedGpuCache and
// ResetStaleGpuCache did it before the engine ran, without this PC's profile.

std::string ConfigureFrameRate() {
    std::string fps = Env("BB_FPS", "60");
    // 0.4.4 beta 22: 240 is a fixed rate like 120 and 144, on the frame-rate corrections (their values follow
    // the rate); without them (BB_FRAME_FIXES=0, safe mode) the patch file's lists stop at 144.
    if (fps == "240" && Env("BB_FRAME_FIXES", "1").substr(0, 1) == "0") {
        Log("240 FPS needs the frame-rate corrections (off): running at 144 FPS");
        fps = "144";
    }
    if (fps == "120" || fps == "144" || fps == "240") {
        SetEnv("BB_FPS", fps);
        SetEnv("BB_FPS_LIMIT", fps);
        SetEnv("BB_VBLANK_HZ", fps);
        return fps;
    }
    const bool digits = !fps.empty() && fps.find_first_not_of("0123456789") == std::string::npos && fps.size() < 6;
    if (fps == "uncap" || (digits && std::stoi(fps) > 90)) {
        // bbport: Uncapped stops at 240 FPS unless BB_FPS_LIMIT says otherwise (0: none). With no
        // limit on a 480 Hz screen the Uncap FPS++ patch froze Ludwig's second phase (its boss
        // intros are known to freeze at extreme frame rates); past 240 nothing is gained.
        std::string limit = digits ? fps : Env("BB_FPS_LIMIT", "240");
        if (limit.empty() || limit.find_first_not_of("0123456789") != std::string::npos) limit = "0";
        SetEnv("BB_FPS", "uncap");
        SetEnv("BB_FPS_LIMIT", limit);
        SetEnv("BB_VBLANK_HZ", "0");
        return "uncap";
    }
    if (fps != "30" && fps != "60" && fps != "90") {
        Log("Unsupported Windows FPS preset '%s'; using 60 FPS.", fps.c_str());
        fps = "60";
    }
    SetEnv("BB_FPS", fps);
    SetEnv("BB_FPS_LIMIT", fps);
    SetEnv("BB_VBLANK_HZ", fps == "90" ? "90" : "60");
    return fps;
}

std::optional<Size> DisplaySize() {
    SetProcessDPIAware();
    const int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
    if (w < 640 || h < 360) return std::nullopt;
    return Size{w - w % 2, h - h % 2};
}

int RunProbe(const fs::path& probe, const std::vector<std::wstring>& arguments, const fs::path& cwd) {
    HANDLE log = static_cast<HANDLE>(LogHandle());
    // The session log when the launcher names one, else this program's own output (the launcher
    // reads it from a pipe): bb-probe writes where this program does.
    STARTUPINFOW startup{sizeof startup};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = log ? log : GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = log ? log : GetStdHandle(STD_ERROR_HANDLE);
    std::fflush(stdout);
    std::wstring line = CommandLine(probe, arguments);
    std::string shown;
    for (const auto& a : arguments) shown += " '" + Utf8(a) + "'";
    Log("Starting: '%s'%s", Utf8(probe).c_str(), shown.c_str());
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(probe.c_str(), line.data(), nullptr, nullptr, TRUE, 0, nullptr, cwd.c_str(), &startup, &process))
        throw Error("cannot start " + Utf8(probe));
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    Log("bb-probe exited with code %u", unsigned(code));
    return int(code);
}

// 0.4.4 beta 28: switches that stand for several (the launcher passes one value; the engine's own
// switches keep their names for tests). Safe mode (BB_SAFE_MODE=1, the run after a crash): every
// experimental option and the mods off, a crash guard on; it sets its values whatever was set. Fast
// draws and compile skipping off (BB_FAST_DRAWS=0, BB_COMPILE_SKIP=0) turn off their parts, except a
// part set on its own (0.4.5 beta 14: BB_VRAM_BUDGET went with the one VRAM manager); 0.4.6 beta 5: safe mode turns
// the VRAM manager on (off by default while the game's own calls are tested alone); BB_SYNC=legacy puts back the synchronisation of
// before the GPU work's fences were honest (every fallback the A/B tests kept, the GX region tracked). The same table is in run.sh (switch groups);
// tests/test_switch_groups.py checks they agree.
// switch-groups-begin
struct SwitchGroup {
    const char* name;
    const char* when;
    bool force;
    std::vector<std::pair<const char*, const char*>> sets;
};
const SwitchGroup kSwitchGroups[] = {
    {"BB_SAFE_MODE", "1", true,
     {
         {"BB_FAST_LOADING", "0"}, {"BB_FRAME_FIXES", "0"}, {"BB_HOT_PAGES", "0"}, {"BB_GX_IN_PLACE", "0"},
         {"BB_NATIVE_CALLS", "0"}, {"BB_PRECISE_PACING", "0"}, {"BB_VBLANK_PACING", "0"},
         {"BB_INDIRECT_CLAMP", "1"}, {"BB_GPU_THREAD_PRIORITY", "0"},
         {"BB_AUDIO_THREAD_PRIORITY", "0"}, {"BB_NATIVE_SHADERS", "0"}, {"BB_SLEEP_SPIN_US", "600"},
         {"BB_ASYNC_PIPELINES", "0"}, {"BB_OCCLUSION", "0"}, {"BB_MODS_ENABLED", "0"},
         {"BB_FAST_DRAWS", "0"}, {"BB_COMPILE_SKIP", "0"}, {"BB_VRAM_MANAGER", "1"},
         {"BB_EMPTY_SURFACES", "0"}, {"BB_TARGET_POOL", "0"},
         {"BB_WATCH_GAME_TEXTURES", "1"}, {"BB_IMAGE_OWNERS", "0"},
         {"BB_WATCH_GAME_BUFFERS", "1"}, {"BB_PIPELINE_BURST", "0"}, {"BB_LOAD_UPLOAD", "0"},
         {"BB_READY_SURFACES", "0"}, {"BB_COPY_NOTICE", "0"}, {"BB_TRANSLATE_AHEAD", "0"}}},
    {"BB_FAST_DRAWS", "0", false,
     {
         {"BB_SUBMIT_END_TASKS", "0"}, {"BB_MEMO_REVALIDATE", "0"}, {"BB_SAMPLER_MEMO", "0"},
         {"BB_DRAIN_SLEEP", "0"}, {"BB_FAULT_SKIP_TEXTURES", "0"}}},
    {"BB_COMPILE_SKIP", "0", false,
     {
         {"BB_SKIP_UNCOMPILED", "0"}, {"BB_LATE_PIPELINE_BIND", "0"}, {"BB_PIPELINE_TAKE", "0"}}},
    {"BB_SYNC", "legacy", false,
     {
         {"BB_HONEST_LABELS", "0"}, {"BB_GX_IN_PLACE", "0"}, {"BB_PIPE_SIGNALS", "0"}, {"BB_SUBMIT_LOCK", "gpu"},
         {"BB_WORK_RETIRED", "0"}, {"BB_ZPASS_IN_ORDER", "0"}, {"BB_FENCE_ORDER", "all"}, {"BB_DMA_WAITS", "all"},
         {"BB_HOST_COPY_WAITS", "all"}, {"BB_PRODUCER_CHECK", "1"}}},
};
// switch-groups-end

void ExpandSwitchGroups() {
    for (const SwitchGroup& group : kSwitchGroups) {
        if (Env(group.name, "") != group.when) continue;
        for (const auto& [name, value] : group.sets) {
            if (group.force || !Env(name)) SetEnv(name, value);
        }
        Log("Engine: %s=%s sets %u switches", group.name, group.when, unsigned(group.sets.size()));
    }
}

// 0.4.4 beta 28: the diagnostics as one list, BB_DIAG=part,part,... (the launcher's Log page): each part
// stands for engine switches, set unless set on their own. "hitch=N" takes the hitch report's
// threshold in ms (40 without one), "threads=a+b" the thread samplers' names. The same table is in run.sh
// (diag parts); tests/test_switch_groups.py checks they agree.
// diag-parts-begin
struct DiagPart {
    const char* name;
    const char* fallback; // the part's value when BB_DIAG names it without one ("" for none)
    std::vector<std::pair<const char*, const char*>> sets; // a value of "" takes the part's value
};
const DiagPart kDiagParts[] = {
    {"frame", "", {{"BB_FRAME_STATS", "1"}}},
    {"hitch", "40", {{"BB_HITCH_REPORT_MS", ""}}},
    {"gpu", "", {{"BB_GPU_PROFILE", "1"}}},
    {"sections", "", {{"BB_SECTIONS", "1"}}},
    {"load", "", {{"BB_SAMPLE_THREADS", "load"}}},
    {"audio", "", {{"BB_AUDIO_STATS", "1"}}},
    {"pad", "", {{"BB_PAD_LOG", "1"}}},
    {"heap", "", {{"BB_WRITE_LOG", "1"}, {"BB_FREE_CHECK", "1"}, {"BB_HEAP_SITES", "1"}}},
    {"shaders", "", {{"BB_SHADER_CAPTURE", "1"}}},
    {"timeline", "", {{"BB_TIMELINE", "1"}}},
    {"waits", "", {{"BB_WAIT_TRACE", "1"}}},
    {"threads", "", {{"BB_SAMPLE_THREADS", ""}}},
};
// diag-parts-end

void ExpandDiagnostics() {
    const std::string list = Env("BB_DIAG", "");
    std::string named;
    size_t at = 0;
    while (at <= list.size() && !list.empty()) {
        const size_t comma = std::min(list.find(',', at), list.size());
        const std::string item = Trim(list.substr(at, comma - at));
        at = comma + 1;
        if (item.empty()) continue;
        const size_t eq = item.find('=');
        const std::string name = Lower(item.substr(0, eq));
        std::string value = eq == std::string::npos ? "" : item.substr(eq + 1);
        std::replace(value.begin(), value.end(), '+', ',');
        const DiagPart* part = nullptr;
        for (const DiagPart& p : kDiagParts)
            if (name == p.name) part = &p;
        if (!part) {
            Log("Diagnostics: no part \"%s\" in BB_DIAG", name.c_str());
            continue;
        }
        if (value.empty()) value = part->fallback;
        for (const auto& [variable, fixed] : part->sets) {
            const std::string set = *fixed ? std::string(fixed) : value;
            if (!set.empty() && !Env(variable)) SetEnv(variable, set);
        }
        named += (named.empty() ? "" : ", ") + name + (value.empty() ? "" : "=" + value);
    }
    if (!named.empty()) Log("Diagnostics: %s (BB_DIAG)", named.c_str());
}

int Main(std::vector<std::wstring> arguments) {
    ExpandSwitchGroups();
    ExpandDiagnostics();
    const Folders folders = Locate();
    std::optional<std::string> game = Env("BB_GAME_DIR");
    if (arguments.size() > 1 && arguments[0] == L"--game-dir") {
        game = Utf8(arguments[1]);
        arguments.erase(arguments.begin(), arguments.begin() + 2);
    }
    const fs::path data = Path(Env("BB_DATA_DIR", Utf8(folders.package)));
    const fs::path& out = folders.out;
    // bb-probe takes its shader cache's folder from this at its start, before it reads its
    // arguments; without it the folder was %APPDATA%\shadPS4 (bb-probe runs in engine\,
    // which has no user folder): shared with the shadPS4 emulator, never reset with an
    // engine update nor by the launcher's Clear, so a damaged or outdated shader stayed.
    if (Env("BB_GPU_USER_DIR", "").empty()) SetEnv("BB_GPU_USER_DIR", Utf8(folders.package / "user"));
    // The engine's crash dump: the launcher names its logs folder; started without it, the package's.
    if (Env("BB_CRASH_DIR", "").empty()) SetEnv("BB_CRASH_DIR", Utf8(folders.package / "logs"));
    {
        std::error_code error;
        fs::create_directories(Path(Env("BB_GPU_USER_DIR", "")), error); // bb-probe falls back without it
    }
    fs::create_directories(out);
    // bbport: a package in a OneDrive folder (the Desktop and Documents usually are) has every log,
    // shader cache and capture file the game writes synced while it plays: CPU and disk the game's
    // threads then miss (seen with 0.9.37 beta 3 on a Desktop in OneDrive: the CPU busier and
    // draw preparation falling behind more often than in a session from E:\, not proven to be it).
    if (Lower(Utf8(folders.package)).find("onedrive") != std::string::npos)
        Log("Performance: this Hunter's Workshop folder is synced by OneDrive (%s); OneDrive uploads its logs and "
            "shader cache while you play. A folder outside OneDrive (C:\\Games, for example) avoids that",
            Utf8(folders.package).c_str());
    // The game's settings, config.ini in the data folder: a folder without one starts with the shipped
    // settings (engine\config.default.ini).
    {
        std::error_code ec;
        const fs::path ini = data / "config.ini", shipped = folders.engine / "config.default.ini";
        if (!fs::exists(ini, ec) && fs::exists(shipped, ec)) fs::copy_file(shipped, ini, ec);
    }
    if (!Env("BB_CONFIG")) SetEnv("BB_CONFIG", Utf8(data / "config.ini"));
    const fs::path config = Path(*Env("BB_CONFIG"));
    const auto settings = ReadSettings(config);
    try {
        Log(ConfigureOnline(settings, data) ? "Online: configured server profile and automatic game redirect"
                                            : "Online: offline mode");
    } catch (const Error& error) {
        Log("Online configuration: %s", error.what());
        return 1;
    }
    std::error_code error;
    // 0.4.4 beta 27: AMD's FSR SDK DLLs (tools/fetch_amd_fsr.sh) in the package's engine\amd-fsr; the
    // engine looks for an amd-fsr folder beside itself (out\) without this (vk_amd_fsr.cpp).
    const fs::path amd_fsr_dir = Env("BB_AMD_FSR_DIR") ? Path(Env("BB_AMD_FSR_DIR", "")) : folders.engine / "amd-fsr";
    const bool amd_fsr_files = Env("BB_AMD_FSR", "") != "0" &&
                               fs::is_regular_file(amd_fsr_dir / "amd_fidelityfx_loader_vk.dll", error) &&
                               fs::is_regular_file(amd_fsr_dir / "amd_fidelityfx_upscaler_vk.dll", error);
    if (!Env("BB_AMD_FSR_DIR") && amd_fsr_files) SetEnv("BB_AMD_FSR_DIR", Utf8(amd_fsr_dir));
    const fs::path remembered = out / "game_dir.txt";
    if ((!game || game->empty()) && fs::is_regular_file(remembered, error)) game = Trim(ReadText(remembered));
    const fs::path game_dir = game && !game->empty() ? Path(*game) : folders.package.parent_path() / "CUSA03173";
    if (!fs::is_regular_file(game_dir / "eboot.bin", error)) {
        Log("No eboot.bin in %s (pass --game-dir or set BB_GAME_DIR).", Utf8(game_dir).c_str());
        return 1;
    }
    const fs::path original = fs::canonical(game_dir);
    WriteText(remembered, Utf8(original));
    SetEnv("BB_GAME_DIR", Utf8(original));
    // 0.4.6 beta 17: the helper programs are in out\tools, a folder of their own with their own DLLs:
    // Windows loads a DLL from a program's folder first, so one put beside the game (OptiScaler as
    // dxgi.dll or winmm.dll, which SDL loads) would be loaded into them too. An update copies the new
    // files over the old and removes none: the old copies beside the engine go here.
    for (const wchar_t* helper : {L"bb-splash.exe", L"bb-shader-lab.exe", L"bb-gpu-capabilities.exe"}) {
        if (fs::is_regular_file(out / L"tools" / helper, error) && fs::exists(out / helper, error)) {
            fs::remove(out / helper, error);
        }
    }
    // 0.4.6 beta 17: the start's splash, from here until the game's first frame (splash.h); the
    // engine writes its steps into the splash's file too.
    Splash splash;
    splash.Start(out, original);
    // The in-game menu's "Apply and restart" runs this program again (probe.c runtime_restart).
    {
        std::vector<std::wstring> all(arguments);
        all.insert(all.begin(), {L"--game-dir", original.native()});
        SetEnv("BB_RESTART_COMMAND", Utf8(CommandLine(SelfPath(), all)));
    }

    fs::path merged;
    try {
        merged = BuildModsOverlay(original, out, Path(Env("BB_MODS_DIR", Utf8(data / "mods"))),
                                  Path(Env("BB_MODS_CONFIG", Utf8(data / "mods.json"))), Env("BB_MODS_ENABLED", "1") == "1");
    } catch (const std::exception& e) {
        Log("Mods: %s", e.what());
        return 1;
    }
    std::optional<fs::path> overlay;
    if (fs::canonical(merged) != original) overlay = merged;
    struct Cleanup {
        std::optional<fs::path>& overlay;
        ~Cleanup() {
            if (overlay) try {
                    RemoveOverlay(*overlay);
                } catch (...) {
                }
        }
    } cleanup{overlay};
    const char* const core[] = {"dvdroot_ps4/shader/gxshader.shaderbnd.dcx", "dvdroot_ps4/other/systex.tpf.dcx"};
    std::string missing;
    for (const char* name : core)
        if (!fs::is_regular_file(merged / Path(name), error)) missing += (missing.empty() ? "" : ", ") + std::string(name);
    if (!missing.empty() && overlay &&
        std::all_of(std::begin(core), std::end(core), [&](const char* n) { return fs::is_regular_file(original / Path(n), error); })) {
        Log("Mods: the merged game lost its own files; starting without mods");
        RemoveOverlay(*overlay);
        overlay.reset();
        merged = original;
        missing.clear();
    }
    if (!missing.empty()) {
        Log("The game folder %s is incomplete: %s missing. Dump or extract the game again (base game plus the 1.09 update).",
            Utf8(original).c_str(), missing.c_str());
        return 1;
    }
    // The game as the engine sees it, mods over it: the PC menus are built from these files.
    SetEnv("BB_GAME_MERGED", Utf8(merged));

    try {
        PrepareImage(merged, out, folders.engine);
    } catch (const GameCheckError& e) {
        Log("\nUnsupported game files: %s\nSet BB_SKIP_GAME_CHECK=1 to start anyway.", e.what());
        return 2;
    } catch (const std::exception& e) {
        Log("prepare failed: %s", e.what());
        return 1;
    }
    for (const char* key : {"BB_DREAM_MIRROR_ASSET", "BB_ENGINE_IMAGE_SHA256", "BB_PC_MENU_ASSETS", "BB_REBIRTH_ASSET"})
        UnsetEnv(key);
    // Another executable than the 1.09 code (prepare.cpp CheckGame: the verified image or a patched
    // one; CUSA00900 1.09 is that same code): the patches write fixed addresses of that code, so
    // none are applied (CompilePatches); hooks check the code they replace and leave themselves out.
    bool other_edition = false;
    try {
        (void)VerifyExecutable(out / "eboot.elf");
    } catch (const std::exception&) {
        std::string code;
        try {
            code = Trim(ReadText(out / "game-code.txt"));
        } catch (const std::exception&) {
        }
        std::string title;
        try {
            title = SfoText(ParseSfo(ReadFile(merged / "sce_sys" / "param.sfo")), "TITLE_ID");
        } catch (const std::exception&) {
        }
        // BB_SKIP_GAME_CHECK=1 left the code unchecked: the title decides, as before.
        other_edition = code == "unchecked" ? title != "CUSA03173" : code != "1.09";
    }
    if (other_edition) SetEnv("BB_OTHER_EDITION", "1");
    else UnsetEnv("BB_OTHER_EDITION");
    const auto is_on = [&](const char* key) { return Setting(settings, key) == "1"; };
    try {
        SetEnv("BB_ENGINE_IMAGE_SHA256", VerifyExecutable(out / "eboot.elf"));
        if (is_on("change_appearance")) {
            // A modded Hunter's Dream layout only costs the mirror, not the PC menus below.
            try {
                const fs::path asset = PrepareMirror(merged, out / "eboot.elf", data / "engine-assets");
                SetEnv("BB_DREAM_MIRROR_ASSET", Utf8(asset));
                Log("Engine: Dream mirror prepared locally: %s", Utf8(asset).c_str());
            } catch (const std::exception& e) {
                Log("Engine: Dream mirror skipped: %s", e.what());
            }
        }
        if (is_on("change_appearance") || is_on("camera_controls") || is_on("rebirth") || is_on("graphics_controls") ||
            Setting(settings, "pc_controls") != "0" || Setting(settings, "widescreen") != "0") {
            try {
                const fs::path menus = PrepareMenuAssets(merged, out / "eboot.elf", data / "engine-assets",
                                                         out / "bb-engine-assets.exe", is_on("rebirth"));
                SetEnv("BB_PC_MENU_ASSETS", Utf8(menus));
                Log("Engine: native PC menus prepared locally: %s", Utf8(menus).c_str());
                if (is_on("rebirth"))
                    SetEnv("BB_REBIRTH_ASSET", Utf8(menus / Path("dvdroot_ps4/script/talk/m24_02_00_00.talkesdbnd.dcx")));
            } catch (const std::exception& e) {
                Log("Engine: native PC menus disabled: %s", e.what());
            }
        }
    } catch (const std::exception& e) {
        Log("Engine: enhancement preparation skipped: %s", e.what());
    }
    // Ebrietas' trail effect replaced as Nexus mod 41 does, from the game as mods leave it
    // (assets.cpp PrepareEffectFixes); the runtime mounts the files (BB_FILE_FIXES).
    UnsetEnv("BB_FILE_FIXES");
    if (Env("BB_EFFECT_FIXES", "") != "0") {
        try {
            std::vector<std::string> notes;
            const auto fixed = PrepareEffectFixes(merged, data / "engine-assets", notes);
            for (const std::string& note : notes) Log("Engine: Ebrietas effect fix: %s", note.c_str());
            if (!fixed.empty()) {
                std::string list;
                for (const auto& [relative, file] : fixed) list += relative + "\t" + Utf8(file) + "\n";
                WriteText(out / "file-fixes.txt", list);
                SetEnv("BB_FILE_FIXES", Utf8(out / "file-fixes.txt"));
                Log("Engine: Ebrietas' trail effect replaced as Nexus mod 41 does, in %zu effect files "
                    "(BB_EFFECT_FIXES=0 keeps the game's)", fixed.size());
            }
        } catch (const std::exception& e) {
            Log("Engine: Ebrietas effect fix skipped: %s", e.what());
        }
    }
    if (!Env("BB_DISPLAY_SIZE") || Env("BB_DISPLAY_SIZE", "").empty()) {
        if (const auto size = DisplaySize()) {
            SetEnv("BB_DISPLAY_SIZE", std::to_string(size->w) + "x" + std::to_string(size->h));
            if (Lower(Setting(settings, "output_res").value_or("")) == "auto")
                Log("Output resolution: the display's own, %dx%d", size->w, size->h);
        }
    }
    if (Env("BB_AUTO_RENDER_RES", "") == "1")
        for (const char* key : {"BB_RENDER_RES", "BB_OUTPUT_RES", "BB_AUTO_RENDER_RES"}) UnsetEnv(key);
    const std::string fps = ConfigureFrameRate();
    std::optional<std::pair<Size, Size>> scaled;
    if (Env("BB_RENDER_RES", "").empty() && !other_edition) {
        try {
            // 0.4.4 beta 27: without AMD's FSR files the engine runs TAA instead of AMD FSR
            // (BbSettings::ConfigureUpscalerSupport), which renders at the output size: no patch.
            auto start_settings = ReadSettings(config);
            const std::string upscaler = Setting(start_settings, "upscaler").value_or("off");
            if (!amd_fsr_files && (upscaler == "fsr" || upscaler == "fsr3" || upscaler == "fsr4" || upscaler == "fsr411")) {
                Log("Upscaler: AMD FSR's files are not in %s: TAA, at the output size", Utf8(amd_fsr_dir).c_str());
                start_settings["upscaler"] = "taa";
            }
            scaled = ScaledSizes(start_settings);
        } catch (const Error&) {
        }
    }
    std::string live = "0";
    if (scaled) {
        live = SettingValue(config, "live_resolution").value_or("0");
        if (live.empty()) live = "0";
        // 0.4.4 beta 25c: Off or On; "auto" (a GPU check) is no longer offered and reads as off.
        if (live == "auto") {
            Log("Settings: live_resolution=auto is no longer offered; off (recommended)");
            live = "0";
        }
        live = live == "1" ? "1" : "0";
    }
    const auto size_text = [](Size s) { return std::to_string(s.w) + "x" + std::to_string(s.h); };
    if (live == "1") {
        Log("Output %s: live resolution changes (live_resolution=0: startup patch)", size_text(scaled->second).c_str());
    } else if (scaled) {
        SetEnv("BB_RENDER_RES", size_text(scaled->first));
        SetEnv("BB_OUTPUT_RES", size_text(scaled->second));
        SetEnv("BB_AUTO_RENDER_RES", "1");
        if (std::int64_t(scaled->first.w) * scaled->first.h > 1920ll * 1080 && !Env("BB_DMEM_MB")) {
            if (const unsigned long error = PoolRefused(9152))
                Log("Memory: Windows refused the 9152 MiB pool a scene above 1080p uses (error %lu); 5056 MiB, "
                    "some models may not show at this resolution", error);
            else
                SetEnv("BB_DMEM_MB", "9152");
        }
        Log("Output %s: scene %s, direct memory %s MiB (live_resolution=1: live changes)", size_text(scaled->second).c_str(),
            size_text(scaled->first).c_str(), Env("BB_DMEM_MB", "5056").c_str());
    }
    PatchResult patches;
    try {
        patches = CompilePatches(PatchRequest{
            .out = out,
            .xml = folders.engine / "patches" / "Bloodborne.xml",
            .settings = config,
            .game = merged,
            .patches_dir = Path(Env("BB_PATCHES_DIR", Utf8(data / "patches"))),
            .patches_config = Path(Env("BB_PATCHES_CONFIG", Utf8(data / "patches.json"))),
            .fps = fps,
            .extra = Env("BB_PATCHES", ""),
            .render_res = Env("BB_RENDER_RES", ""),
            .output_res = Env("BB_OUTPUT_RES", ""),
        });
    } catch (const std::exception& e) {
        Log("Patches: %s", e.what());
        return 1;
    }
    if (!(patches.ui == Size{1920, 1080})) {
        SetEnv("BB_UI_SIZE", size_text(patches.ui));
        Log("Widescreen: UI viewport %s (the 16:9 stage centred in it)", size_text(patches.ui).c_str());
    } else {
        UnsetEnv("BB_UI_SIZE");
    }
    if (patches.heap_patch && !Env("BB_DMEM_MB")) {
        SetEnv("BB_DMEM_MB", "9152");
        Log("Patches: the game's heaps are enlarged; direct memory 9152 MiB");
    }
    if (const auto dmem = Env("BB_DMEM_MB"); dmem && *dmem == "9152") {
        // Made now if no check made it yet; on a refusal the engine tries again on its own.
        if (const unsigned long error = PoolRefused(9152))
            Log("Memory: Windows refused the 9152 MiB pool before the start (error %lu)", error);
        PoolHandOff(9152);
    } else if (!dmem) {
        // bbport: the default pool too: its commit charged here, before bb-probe and the graphics
        // driver take theirs. Made by the engine only, it was refused a moment after a start whose
        // PC had enough (RTX 5060, about 7 GB of commit free, error 1455, 2026-10-22).
        if (const unsigned long error = PoolRefused(5056))
            Log("Memory: Windows refused the 5056 MiB pool before the start (error %lu)", error);
        PoolHandOff(5056);
    } else {
        PoolHandOff(0);
    }
    const fs::path probe = out / "bb-probe.exe";
    std::vector<std::wstring> command{(out / "boot-linked.bin").native(), L"--content-profile", (out / "content.bin").native(),
                                      L"--patches", (out / "patches.bin").native(), L"--app0", merged.native(), L"--user",
                                      Path(Env("BB_USER_DIR", Utf8(data / "user"))).native(), L"--timeout",
                                      Wide(Env("BB_TIMEOUT", "0"))};
    command.insert(command.end(), arguments.begin(), arguments.end());
    splash.Step(BbSplashStatus::Step::Engine);
    // 0.4.6 beta 17: in its own folder, as a game runs. Tools put beside it give their paths from
    // there: d4r's OptiScaler.ini names d4r\nvngx.dll, which OptiScaler looks for from the working
    // folder (the tester's 126 runs: not found from engine\, so its DLSS had no runtime and crashed).
    // The engine itself is given every path whole.
    return RunProbe(probe, command, out);
}

} // namespace
} // namespace start

int wmain(int argc, wchar_t** argv) {
    start::InitLog();
    std::vector<std::wstring> arguments(argv + 1, argv + argc);
    try {
        // Tests (engine/tests/test_start.py): one stage into a chosen folder, against the scripts'.
        //   --test-prepare GAME OUT ENGINE
        //   --test-patches OUT ENGINE SETTINGS GAME FPS RENDER_RES OUTPUT_RES PATCHES_DIR PATCHES_CONFIG
        if (arguments.size() == 4 && arguments[0] == L"--test-prepare") {
            start::PrepareImage(arguments[1], arguments[2], arguments[3]);
            return 0;
        }
        if (arguments.size() == 5 && arguments[0] == L"--test-mods") {
            const fs::path overlay = start::BuildModsOverlay(arguments[1], arguments[2], arguments[3], arguments[4], true);
            std::printf("%s\n", start::Utf8(overlay).c_str());
            return 0;
        }
        if (arguments.size() == 10 && arguments[0] == L"--test-patches") {
            const auto result = start::CompilePatches(start::PatchRequest{
                .out = arguments[1],
                .xml = fs::path(arguments[2]) / "patches" / "Bloodborne.xml",
                .settings = arguments[3],
                .game = arguments[4],
                .patches_dir = arguments[8],
                .patches_config = arguments[9],
                .fps = start::Utf8(arguments[5]),
                .extra = start::Env("BB_PATCHES", ""),
                .render_res = start::Utf8(arguments[6]),
                .output_res = start::Utf8(arguments[7]),
            });
            start::Log("heap_patch=%d ui=%dx%d", int(result.heap_patch), result.ui.w, result.ui.h);
            return 0;
        }
        return start::Main(std::move(arguments));
    } catch (const std::exception& e) {
        start::Log("bb-start: %s", e.what());
        return 1;
    }
}

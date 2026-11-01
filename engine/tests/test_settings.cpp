// SPDX-License-Identifier: GPL-2.0-or-later
// bbport_settings.cpp: the menu saves its keys into config.ini and keeps the launcher's (controls).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <string>
#include "gpu/shim/bbport_settings.h"

static std::string Read(const char* path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

int main() {
    const auto temporary = std::filesystem::temp_directory_path() /
        ("bbport-settings-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::string filename = temporary.string();
    const char* path = filename.c_str();
    const char ini[] = "# launcher\nupscaler=fsr3\nkey.cross=X, Space\npad.circle=a\nshow_fps=0\n"
                       "fullscreen_hint=1\n";
    { std::ofstream file(path); file << ini; assert(file.good()); }
#ifdef _WIN32
    _putenv_s("BB_CONFIG", path);
#else
    setenv("BB_CONFIG", path, 1);
#endif

    BbSettings::Load();
    auto& s = BbSettings::Get();
    // 0.4.4 beta 27: the older FSR names read as AMD's FSR.
    assert(s.upscaler == BbSettings::UpscalerFsr && !s.show_fps && s.menu_x < 0.0f);
    s.show_fps = true;
    s.upscaler = BbSettings::UpscalerTaa;
    s.menu_x = 0.625f;
    s.menu_y = 0.125f;
    s.fullscreen = true;
    BbSettings::Save();

    const std::string saved = Read(path);
    // The launcher's controls, its other keys and comments stay; the menu's keys are replaced
    // in place, new ones appended.
    assert(saved.find("# launcher\nupscaler=taa\nkey.cross=X, Space\npad.circle=a\nshow_fps=1\n"
                      "fullscreen_hint=1\n") == 0);
    assert(saved.find("menu_pos=0.6250,0.1250\n") != std::string::npos);
    assert(saved.find("upscaler=fsr3") == std::string::npos);
    assert(saved.find("fullscreen=1\n") != std::string::npos);

    s.menu_x = -1.0f;
    s.menu_y = -1.0f;
    BbSettings::Load();
    assert(s.menu_x == 0.625f && s.menu_y == 0.125f && s.upscaler == BbSettings::UpscalerTaa);
    // Replacing an existing file works on Windows too, and must not erase key bindings.
    s.upscaler = BbSettings::UpscalerDlss;
    BbSettings::Save();
    assert(Read(path).find("upscaler=dlss\n") != std::string::npos);
    assert(Read(path).find("key.cross=X, Space\n") != std::string::npos);
    // 0.4.4 beta 27: DLSS where it cannot run gives way to AMD FSR, AMD FSR to TAA.
    BbSettings::ConfigureUpscalerSupport(true, false);
    assert(s.upscaler == BbSettings::UpscalerFsr && !s.dlss_supported && s.fsr_supported);
    s.upscaler = BbSettings::UpscalerDlss;
    BbSettings::ConfigureUpscalerSupport(false, true);
    assert(s.upscaler == BbSettings::UpscalerDlss && s.dlss_supported);
    BbSettings::ConfigureUpscalerSupport(false, false);
    assert(s.upscaler == BbSettings::UpscalerTaa);
    s.upscaler = BbSettings::UpscalerFsr;
    BbSettings::ConfigureUpscalerSupport(false, true);
    assert(s.upscaler == BbSettings::UpscalerTaa);
    // fsr_version: AMD's choice by default, "3.1" kept.
    assert(s.fsr_version == BbSettings::FsrVersionAuto);
    s.fsr_version = BbSettings::FsrVersion31;
    BbSettings::Save();
    assert(Read(path).find("fsr_version=3.1\n") != std::string::npos);
    s.fsr_version = BbSettings::FsrVersionAuto;
    BbSettings::Load();
    assert(s.fsr_version == BbSettings::FsrVersion31);
    s.camera_controls = true;
    s.camera_fov_scale = 1.25f;
    s.camera_distance_scale = 0.80f;
    s.camera_height_scale = 1.10f;
    BbSettings::Save();
    s.camera_controls = false;
    s.camera_fov_scale = s.camera_distance_scale = s.camera_height_scale = 1.0f;
    BbSettings::Load();
    assert(s.camera_controls && s.camera_fov_scale == 1.25f);
    assert(s.camera_distance_scale == 0.80f && s.camera_height_scale == 1.10f);
    s.change_appearance=true;s.rebirth=true;s.graphics_controls=true;
    s.graphics_bloom=0.7f;s.graphics_shadow_scale=1.4f;
    BbSettings::Save();
    s.rebirth=false;s.graphics_bloom=1.0f;
    BbSettings::Load();
    assert(s.change_appearance && s.rebirth && s.graphics_controls && s.graphics_bloom==0.7f && s.graphics_shadow_scale==1.4f);
    // Edited configs must not feed infinite/NaN camera transforms to the engine.
    { std::ofstream file(path); file << "camera_fov_scale=nan\ncamera_distance_scale=-10\ncamera_height_scale=inf\n"; }
    BbSettings::Load();
    assert(s.camera_fov_scale == 1.0f && s.camera_distance_scale == 0.5f && s.camera_height_scale == 1.0f);
    // Menu languages: the code is read and written back; the added languages translate by the
    // English text and fall back to it; Russian keeps its own text.
    { std::ofstream file(path); file << "menu_language=pt_BR\n"; }
    BbSettings::Load();
    assert(s.menu_language == BbSettings::MenuLanguage::PortugueseBrazil);
    assert(std::string(BbSettings::MenuText("Close", "Закрыть")) == "Fechar");
    assert(std::string(BbSettings::MenuText("Not in the tables", "x")) == "Not in the tables");
    BbSettings::Save();
    assert(Read(path).find("menu_language=pt_BR\n") != std::string::npos);
    s.menu_language = BbSettings::MenuLanguage::Turkish;
    assert(std::string(BbSettings::MenuText("Close", "Закрыть")) == "Kapat");
    s.menu_language = BbSettings::MenuLanguage::Russian;
    assert(std::string(BbSettings::MenuText("Close", "Закрыть")) == "Закрыть");
    // Cheats: off, infinite or an amount 1-99, written back as read; anything else is off.
    { std::ofstream file(path); file << "cheat_vials=infinite\ncheat_bullets=50\n"; }
    BbSettings::Load();
    assert(s.cheat_vials == BbSettings::CheatInfinite && s.cheat_bullets == 50);
    BbSettings::Save();
    assert(Read(path).find("cheat_vials=infinite\n") != std::string::npos);
    assert(Read(path).find("cheat_bullets=50\n") != std::string::npos);
    { std::ofstream file(path); file << "cheat_vials=100\ncheat_bullets=lots\n"; }
    BbSettings::Load();
    assert(s.cheat_vials == BbSettings::CheatOff && s.cheat_bullets == BbSettings::CheatOff);
    BbSettings::Save();
    assert(Read(path).find("cheat_vials=off\n") != std::string::npos);
    // The other cheats: 0/1 switches, firearm range off/150/200/300, echo multiplier 1/2/5/10;
    // written back as read, anything else the game's own.
    assert(Read(path).find("cheat_gun_range=off\n") != std::string::npos);
    assert(Read(path).find("cheat_echo_multiplier=1\n") != std::string::npos);
    assert(Read(path).find("cheat_stamina=0\n") != std::string::npos);
    {
        std::ofstream file(path);
        file << "cheat_stamina=1\ncheat_stealth=1\ncheat_gun_range=200\ncheat_keep_echoes=1\n"
                "cheat_echo_multiplier=5\ncheat_no_durability=1\ncheat_consumables=1\n";
    }
    BbSettings::Load();
    assert(s.cheat_stamina && s.cheat_stealth && s.cheat_keep_echoes && s.cheat_no_durability && s.cheat_consumables);
    assert(s.cheat_gun_range == 200 && s.cheat_echo_multiplier == 5);
    BbSettings::Save();
    for (const char* line : {"cheat_stamina=1\n", "cheat_stealth=1\n", "cheat_gun_range=200\n", "cheat_keep_echoes=1\n",
                             "cheat_echo_multiplier=5\n", "cheat_no_durability=1\n", "cheat_consumables=1\n"}) {
        assert(Read(path).find(line) != std::string::npos);
    }
    {
        std::ofstream file(path);
        file << "cheat_stamina=0\ncheat_stealth=0\ncheat_gun_range=250\ncheat_keep_echoes=0\n"
                "cheat_echo_multiplier=3\ncheat_no_durability=0\ncheat_consumables=0\n";
    }
    BbSettings::Load();
    assert(!s.cheat_stamina && !s.cheat_stealth && !s.cheat_keep_echoes && !s.cheat_no_durability && !s.cheat_consumables);
    assert(s.cheat_gun_range == 0 && s.cheat_echo_multiplier == 1);
    { std::ofstream file(path); file << "cheat_gun_range=300\ncheat_echo_multiplier=10\n"; }
    BbSettings::Load();
    assert(s.cheat_gun_range == 300 && s.cheat_echo_multiplier == 10);
    { std::ofstream file(path); file << "cheat_gun_range=off\ncheat_echo_multiplier=x2\n"; }
    BbSettings::Load();
    assert(s.cheat_gun_range == 0 && s.cheat_echo_multiplier == 1);
    BbSettings::Save();
    assert(Read(path).find("cheat_gun_range=off\n") != std::string::npos);
    assert(Read(path).find("cheat_echo_multiplier=1\n") != std::string::npos);
    // HDR output: hdr 0/1 (applied at start), paper white 10-500 and peak 10-4000 nits, boost
    // 0-1; written back as read; anything else clamped or the default.
    assert(Read(path).find("hdr=0\n") != std::string::npos);
    assert(Read(path).find("hdr_paper_white=200\n") != std::string::npos);
    assert(Read(path).find("hdr_peak=1000\n") != std::string::npos);
    assert(Read(path).find("hdr_boost=0.25\n") != std::string::npos);
    // hdr_mode: native (the default: highlights from the game's HDR scene) or expand (from the
    // SDR picture); live, so it is not a startup setting.
    assert(s.hdr_mode == BbSettings::HdrModeNative);
    assert(Read(path).find("hdr_mode=native\n") != std::string::npos);
    { std::ofstream file(path); file << "hdr=1\nhdr_paper_white=250\nhdr_peak=20000\nhdr_boost=0.75\n"
                                        "hdr_mode=expand\n"; }
    BbSettings::Load();
    assert(s.hdr && s.startup_hdr && s.hdr_paper_white == 250.0f && s.hdr_peak == 4000.0f &&
           s.hdr_boost == 0.75f && s.hdr_mode == BbSettings::HdrModeExpand);
    BbSettings::Save();
    for (const char* line : {"hdr=1\n", "hdr_paper_white=250\n", "hdr_peak=4000\n", "hdr_boost=0.75\n",
                             "hdr_mode=expand\n"}) {
        assert(Read(path).find(line) != std::string::npos);
    }
    assert(Read(path).find("hdr_mode=native\n") == std::string::npos);
    { std::ofstream file(path); file << "hdr=0\nhdr_paper_white=10\nhdr_peak=nan\nhdr_boost=-1\n"
                                        "hdr_mode=bright\n"; }
    BbSettings::Load();
    assert(!s.hdr && !s.startup_hdr && s.hdr_paper_white == 10.0f && s.hdr_peak == 1000.0f &&
           s.hdr_boost == 0.0f && s.hdr_mode == BbSettings::HdrModeNative);
    { std::ofstream file(path); file << "hdr_mode=expand\n"; }
    BbSettings::Load();
    assert(s.hdr_mode == BbSettings::HdrModeExpand);
    { std::ofstream file(path); file << "hdr_mode=native\n"; }
    BbSettings::Load();
    assert(s.hdr_mode == BbSettings::HdrModeNative);
    // BB_HDR* override the file.
#ifdef _WIN32
    _putenv_s("BB_HDR", "1");
    _putenv_s("BB_HDR_PEAK", "600");
    _putenv_s("BB_HDR_MODE", "expand");
#else
    setenv("BB_HDR", "1", 1);
    setenv("BB_HDR_PEAK", "600", 1);
    setenv("BB_HDR_MODE", "expand", 1);
#endif
    BbSettings::Load();
    assert(s.hdr && s.startup_hdr && s.hdr_peak == 600.0f && s.hdr_paper_white == 10.0f &&
           s.hdr_mode == BbSettings::HdrModeExpand);
#ifdef _WIN32
    _putenv_s("BB_HDR", "");
    _putenv_s("BB_HDR_PEAK", "");
    _putenv_s("BB_HDR_MODE", "");
#else
    unsetenv("BB_HDR");
    unsetenv("BB_HDR_PEAK");
    unsetenv("BB_HDR_MODE");
#endif
    BbSettings::Load();
    assert(s.hdr_mode == BbSettings::HdrModeNative);
    // hdr_peak_auto: the peak the display reports (window.cpp sets hdr_display_peak) when it
    // reports one, else hdr_peak; BB_HDR_PEAK (a peak for this run) turns it off.
    assert(!s.hdr_peak_auto); // BB_HDR_PEAK was set above
    { std::ofstream file(path); file << "hdr_peak=800\nhdr_peak_auto=1\n"; }
    BbSettings::Load();
    assert(s.hdr_peak_auto && s.hdr_peak == 800.0f);
    s.hdr_display_peak = 450.0f;
    assert(BbSettings::HdrPeakNits(s) == 450.0f);
    s.hdr_display_peak = 5.0f; // nothing usable reported
    assert(BbSettings::HdrPeakNits(s) == 800.0f);
    s.hdr_display_peak = 9000.0f;
    assert(BbSettings::HdrPeakNits(s) == BbSettings::HdrPeakMax);
    s.hdr_peak_auto = false;
    s.hdr_display_peak = 450.0f;
    assert(BbSettings::HdrPeakNits(s) == 800.0f);
    BbSettings::Save();
    assert(Read(path).find("hdr_peak_auto=0\n") != std::string::npos);
    { std::ofstream file(path); file << "hdr_peak_auto=1\n"; }
    BbSettings::Load();
    assert(s.hdr_peak_auto);
    BbSettings::Save();
    assert(Read(path).find("hdr_peak_auto=1\n") != std::string::npos);
    // The menu's comparison is not saved.
    s.hdr_compare = true;
    BbSettings::Save();
    assert(Read(path).find("hdr_compare") == std::string::npos);
    s.hdr_compare = false;
    s.hdr_display_peak = 0.0f;
    // Live resolution: off or on; "auto" (no longer offered, 0.4.4 beta 25c) reads as off and saves as 0.
    {
        std::ofstream file(path);
        file << "live_resolution=auto\n";
    }
    BbSettings::Load();
    assert(s.live_resolution == 0 && s.startup_live_resolution == 0);
    BbSettings::Save();
    assert(Read(path).find("live_resolution=0") != std::string::npos);
    assert(Read(path).find("live_resolution=auto") == std::string::npos);
    {
        std::ofstream file(path);
        file << "live_resolution=1\n";
    }
    BbSettings::Load();
    assert(s.live_resolution == 1);
    s.live_resolution = 0;
    // The DLSS model and its mip bias (live in the menu), the OptiScaler switches (read at start).
    {
        std::ofstream file(path);
        file << "dlss_preset=j\ndlss_mip_bias=0\noptiscaler_compat=0\noptiscaler_sync_submit=1\n";
    }
    BbSettings::Load();
    assert(s.dlss_preset == 10 && !s.dlss_mip_bias && !s.optiscaler_compat &&
           s.optiscaler_sync_submit == BbSettings::OptiSubmitOn);
    assert(!s.startup_optiscaler.compat && s.startup_optiscaler.sync_submit == BbSettings::OptiSubmitOn);
    s.dlss_preset = BbSettings::DlssPresetDefault;
    s.optiscaler_compat = true;
    s.optiscaler_sync_submit = BbSettings::OptiSubmitAuto;
    BbSettings::Save();
    {
        const std::string text = Read(path);
        assert(text.find("dlss_preset=default\n") != std::string::npos);
        assert(text.find("dlss_mip_bias=0\n") != std::string::npos);
        assert(text.find("optiscaler_compat=1\n") != std::string::npos);
        assert(text.find("optiscaler_sync_submit=auto\n") != std::string::npos);
    }
    assert(!s.startup_optiscaler.compat); // the menu's change waits for a restart
#ifdef _WIN32
    _putenv_s("BB_DLSS_PRESET", "K");
#else
    setenv("BB_DLSS_PRESET", "K", 1);
#endif
    BbSettings::Load();
    assert(s.dlss_preset == BbSettings::DlssPresetK && s.optiscaler_compat && s.startup_optiscaler.compat);
#ifdef _WIN32
    _putenv_s("BB_DLSS_PRESET", "");
#else
    unsetenv("BB_DLSS_PRESET");
#endif
    assert(BbSettings::DlssPresetFromText("m") == 13 && std::string(BbSettings::DlssPresetText(13)) == "m");
    assert(std::string(BbSettings::DlssPresetText(BbSettings::DlssPresetDefault)) == "default");
    // Letters DLSS 310.9 removed (A-D) or ignores (G-I, N, O): K.
    assert(BbSettings::DlssPresetFromText("a") == BbSettings::DlssPresetK);
    assert(BbSettings::DlssPresetFromText("h") == BbSettings::DlssPresetK);
    assert(BbSettings::DlssPresetFromText("j") == 10);
    // The CNN presets NVIDIA deprecated read as Auto (0.4.4 beta 25b).
    assert(BbSettings::DlssPresetFromText("e") == BbSettings::DlssPresetAuto);
    assert(BbSettings::DlssPresetFromText("F") == BbSettings::DlssPresetAuto);
    assert(!BbSettings::DlssPresetValid(5) && !BbSettings::DlssPresetValid(6) && BbSettings::DlssPresetValid(13));
    // DLSS Frame Generation, Reflex, the Custom preset's ratio, the mip bias offset.
    {
        std::ofstream file(path);
        file << "frame_gen=dlss\nframe_gen_frames=9\nreflex=boost\npreset=5\nrender_scale=1.3\n"
                "mip_bias=-5\ndlss_auto_exposure=1\n";
    }
    BbSettings::Load();
    // 0.4.4 beta 26: frame generation reads as off while it is not offered (FrameGenOffered).
    const int dlss_fg = BbSettings::FrameGenOffered ? BbSettings::FrameGenDlss : BbSettings::FrameGenOff;
    const int fsr_fg = BbSettings::FrameGenOffered ? BbSettings::FrameGenFsr3 : BbSettings::FrameGenOff;
    assert(s.frame_gen == dlss_fg && s.startup_frame_gen == dlss_fg);
    assert(s.frame_gen_frames == BbSettings::FrameGenFramesMax && s.reflex == BbSettings::ReflexBoost);
    assert(s.preset == BbSettings::PresetCustom && s.render_scale == 1.3f && s.dlss_auto_exposure);
    assert(s.mip_bias == BbSettings::MipBiasMin);
    assert(BbSettings::PresetScale(BbSettings::PresetCustom) == 1.3f);
    assert(BbSettings::ModelPreset(BbSettings::PresetCustom) == BbSettings::Quality);
    assert(BbSettings::ModelPreset(BbSettings::Performance) == BbSettings::Performance);
    s.render_scale = 1.1f;
    assert(BbSettings::ModelPreset(BbSettings::PresetCustom) == BbSettings::NativeAA);
    BbSettings::Save();
    {
        const std::string text = Read(path);
        assert(text.find(BbSettings::FrameGenOffered ? "frame_gen=dlss\n" : "frame_gen=off\n") != std::string::npos);
        assert(text.find("frame_gen_frames=5\n") != std::string::npos);
        assert(text.find("reflex=boost\n") != std::string::npos);
        assert(text.find("render_scale=1.10\n") != std::string::npos);
        assert(text.find("mip_bias=-2.00\n") != std::string::npos);
        assert(text.find("dlss_auto_exposure=1\n") != std::string::npos);
    }
    { std::ofstream file(path); file << "frame_gen=fsr3\nreflex=sometimes\nrender_scale=9\n"; }
    BbSettings::Load();
    assert(s.frame_gen == fsr_fg && s.reflex == BbSettings::ReflexOn &&
           s.render_scale == BbSettings::RenderScaleMax);
    // 0.4.4 beta 26b: Live resolution changes off holds at a 1920x1080 start too (no startup patch,
    // BB_RENDER_RES unset): an output chosen in game waits for a restart; with it on, it is live.
    {
#ifdef _WIN32
        _putenv_s("BB_RENDER_RES", "");
#else
        unsetenv("BB_RENDER_RES");
#endif
        { std::ofstream file(path); file << "output_res=1920x1080\nlive_resolution=0\n"; }
        BbSettings::Load();
        assert(!BbSettings::FixedRenderSession() && !BbSettings::OutputChangesLive());
        assert(!BbSettings::ResolutionNeedsRestart());
        s.output_res = BbSettings::OutputDefault == 2 ? 3 : 2; // another named size
        assert(BbSettings::ResolutionNeedsRestart());
        s.output_res = s.startup_output_res;
        s.preset = BbSettings::Performance; // the preset at a 1080p start stays live
        assert(!BbSettings::ResolutionNeedsRestart());
        { std::ofstream file(path); file << "output_res=1920x1080\nlive_resolution=1\n"; }
        BbSettings::Load();
        assert(BbSettings::OutputChangesLive());
        s.output_res = BbSettings::OutputDefault == 2 ? 3 : 2;
        assert(!BbSettings::ResolutionNeedsRestart());
    }
    std::filesystem::remove(temporary);
    std::puts("PASS: settings save keeps other keys, menu position");
}

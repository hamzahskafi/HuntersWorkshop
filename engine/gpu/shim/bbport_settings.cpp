#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_settings.h"
#include "bbport_menu_i18n.h"
#include "input/ini_bindings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BbSettings {

namespace {

const char* Path() {
    const char* env = std::getenv("BB_CONFIG");
    return env && env[0] ? env : "config.ini";
}

float Clamp(float v, float lo, float hi) {
    return std::clamp(v, lo, hi);
}

// cheat_vials / cheat_bullets: off, infinite or an amount 1..CheatMaxAmount; anything else is off.
int CheatValue(const std::string& key, const std::string& value) {
    if (value == "off" || value == "0") {
        return CheatOff;
    }
    if (value == "infinite") {
        return CheatInfinite;
    }
    char* end = nullptr;
    const long amount = std::strtol(value.c_str(), &end, 10);
    if (!value.empty() && end && *end == '\0' && amount >= 1 && amount <= CheatMaxAmount) {
        return int(amount);
    }
    std::printf("Settings: unknown %s '%s' (expected off, infinite or 1-%d); off\n", key.c_str(),
                value.c_str(), CheatMaxAmount);
    return CheatOff;
}

std::string CheatText(int value) {
    return value == CheatInfinite ? "infinite"
           : value >= 1 && value <= CheatMaxAmount ? std::to_string(value)
                                                   : "off";
}

// cheat_gun_range: off or one of CheatGunRanges (percent); cheat_echo_multiplier: one of
// CheatEchoMultipliers. Anything else is the game's own (off / 1).
template <std::size_t N>
int CheatChoice(const std::string& key, const std::string& value, const int (&choices)[N], const char* expected) {
    if (value == "off") {
        return choices[0];
    }
    char* end = nullptr;
    const long v = std::strtol(value.c_str(), &end, 10);
    if (!value.empty() && end && *end == '\0' && std::find(choices, choices + N, int(v)) != choices + N) {
        return int(v);
    }
    std::printf("Settings: unknown %s '%s' (expected %s); %d\n", key.c_str(), value.c_str(), expected, choices[0]);
    return choices[0];
}

void Set(Values& v, const std::string& key, const std::string& value) {
    const float f = float(std::atof(value.c_str()));
    const int i = std::atoi(value.c_str());
    if (key == "menu_language") {
        if (value == "zh" || value == "zh-CN" || value == "zh_CN") {
            v.menu_language = MenuLanguage::ChineseSimplified;
            return;
        }
        bool known = false;
        for (int l = 0; l < MenuLanguageCount; ++l) {
            if (value == MenuLanguageCodes[l]) {
                v.menu_language = MenuLanguage(l);
                known = true;
            }
        }
        if (!known) {
            std::printf("Settings: unknown menu_language '%s' (expected en, ru, es, pt_BR, fr, de, it, pl "
                        "or tr, zh_CN)\n",
                        value.c_str());
        }
    } else if (key == "upscaler") {
        if (const int u = UpscalerFromName(value.c_str()); u >= 0) {
            v.upscaler = u;
        } else {
            std::printf("Settings: unknown upscaler '%s' (expected fsr, dlss, taa or off)\n", value.c_str());
        }
    } else if (key == "fsr_version") {
        // 0.4.4 beta 27c: "auto" or a version's name; 27a's "3.1" (and "31") the newest FSR 3.1.
        SetFsrVersion(value == "31" ? std::string("3.1") : value);
    } else if (key == "render_scale") {
        v.render_scale = Clamp(std::isfinite(f) && f > 0.0f ? f : 1.3f, RenderScaleMin, RenderScaleMax);
    } else if (key == "mip_bias") {
        v.mip_bias = Clamp(std::isfinite(f) ? f : 0.0f, MipBiasMin, MipBiasMax);
    } else if (key == "preset") {
        // 0.4.4 beta 27c: "auto" (the default), or a preset's number as before.
        v.preset = value == "auto" || value.empty() ? PresetAuto : std::clamp(i, 0, PresetCount - 1);
    } else if (key == "sharpen") {
        v.sharpen = i != 0;
    } else if (key == "sharpness") {
        v.sharpness = Clamp(f, 0.0f, 2.0f);
    } else if (key == "jitter") {
        v.jitter = i != 0;
    } else if (key == "reactive") {
        v.reactive = i != 0;
    } else if (key == "object_motion") {
        v.object_motion = i != 0;
    } else if (key == "reactive_scale") {
        v.reactive_scale = Clamp(f, 0.0f, 16.0f);
    } else if (key == "reactive_threshold") {
        v.reactive_threshold = Clamp(f, 0.0f, 1.0f);
    } else if (key == "reactive_max") {
        v.reactive_max = Clamp(f, 0.0f, 1.0f);
    } else if (key == "fsr3_velocity_factor") {
        v.fsr3_velocity_factor = Clamp(f, 0.0f, 1.0f);
    } else if (key == "fsr3_reactiveness_scale") {
        v.fsr3_reactiveness_scale = Clamp(f, 0.0f, 4.0f);
    } else if (key == "fsr3_shading_change_scale") {
        v.fsr3_shading_change_scale = Clamp(f, 0.0f, 4.0f);
    } else if (key == "fsr3_accumulation") {
        v.fsr3_accumulation = Clamp(f, 0.0f, 1.0f);
    } else if (key == "fsr3_min_disocclusion") {
        v.fsr3_min_disocclusion = Clamp(f, -1.0f, 1.0f);
    } else if (key == "debug_view") {
        // Debug views last one session: a saved one came back on every start and looked like a bug.
    } else if (key == "show_fps") {
        v.show_fps = i != 0;
    } else if (key == "show_fps_rate") {
        v.show_fps_rate = i != 0;
    } else if (key == "show_fps_frame_time") {
        v.show_fps_frame_time = i != 0;
    } else if (key == "show_fps_upscaler") {
        v.show_fps_upscaler = i != 0;
    } else if (key == "show_fps_cpu") {
        v.show_fps_cpu = i != 0;
    } else if (key == "show_fps_gpu") {
        v.show_fps_gpu = i != 0;
    } else if (key == "compile_note") {
        v.show_compile_note = i != 0;
    } else if (key == "fps_position") {
        v.fps_position = std::clamp(i, 0, FpsPositionCount - 1);
    } else if (key == "fps_scale") {
        v.fps_scale = Clamp(f, 0.75f, 2.0f);
    } else if (key == "fps_text_color" || key == "fps_bg_color") {
        // "#RRGGBB" (or without the #).
        const char* hex = value.c_str() + (value.starts_with('#') ? 1 : 0);
        char* end = nullptr;
        const unsigned long rgb = std::strtoul(hex, &end, 16);
        if (end && end != hex && *end == '\0' && rgb <= 0xFFFFFFul) {
            (key == "fps_text_color" ? v.fps_text_color : v.fps_bg_color) = unsigned(rgb);
        }
    } else if (key == "fps_bg_opacity") {
        v.fps_bg_opacity = Clamp(f, 0.0f, 1.0f);
    } else if (key == "fps_text_opacity") {
        v.fps_text_opacity = Clamp(f, 0.1f, 1.0f);
    } else if (key == "fps_border") {
        v.fps_border = i != 0;
    } else if (key == "hide_cursor") {
        v.hide_cursor = i != 0;
    } else if (key == "menu_pos") {
        float x = -1.0f, y = -1.0f;
        if (std::sscanf(value.c_str(), "%f,%f", &x, &y) == 2 && x >= 0.0f && x <= 1.0f && y >= 0.0f &&
            y <= 1.0f) {
            v.menu_x = x;
            v.menu_y = y;
        }
    } else if (key == "dlss_preset") {
        v.dlss_preset = DlssPresetFromText(value.c_str());
    } else if (key == "optiscaler_compat") {
        v.optiscaler_compat = i != 0;
    } else if (key == "optiscaler_ngx_lock") {
        v.optiscaler_ngx_lock = i != 0;
    } else if (key == "optiscaler_clean_thread") {
        v.optiscaler_clean_thread = i != 0;
    } else if (key == "optiscaler_sync_submit") {
        v.optiscaler_sync_submit = value == "1" ? OptiSubmitOn : value == "0" ? OptiSubmitOff : OptiSubmitAuto;
    } else if (key == "fsr4_invert_jitter") {
        v.fsr4_invert_jitter = i != 0;
    } else if (key == "widescreen") {
        v.widescreen=i!=0;
    } else if (key == "pc_controls") {
        v.pc_controls=i!=0;
    } else if (key == "mouse_camera") {
        v.mouse_camera=i!=0;
    } else if (key == "mouse_menu") {
        v.mouse_menu=i!=0;
    } else if (key == "menu_move") {
        v.menu_move=i!=0;
    } else if (key == "mouse_invert_x") {
        v.mouse_invert_x=i!=0;
    } else if (key == "mouse_invert_y") {
        v.mouse_invert_y=i!=0;
    } else if (key == "mouse_auto_rotation") {
        v.mouse_auto_rotation=i!=0;
    } else if (key == "button_prompts") {
        int mode = -1;
        for (int m = 0; m < PromptsCount; ++m) {
            if (value == ButtonPromptsNames[m]) {
                mode = m;
            }
        }
        if (mode < 0) {
            std::printf("Settings: unknown button_prompts '%s' (expected auto, keyboard or controller); auto\n",
                        value.c_str());
        }
        v.button_prompts = mode < 0 ? PromptsAuto : mode;
    } else if (key == "pad_icons") {
        int mode = -1;
        for (int m = 0; m < PadIconsCount; ++m) {
            if (value == PadIconsNames[m]) {
                mode = m;
            }
        }
        if (mode < 0) {
            std::printf("Settings: unknown pad_icons '%s' (expected auto, playstation or xbox); auto\n",
                        value.c_str());
        }
        v.pad_icons = mode < 0 ? PadIconsAuto : mode;
    } else if (key == "mouse_sensitivity") {
        v.mouse_sensitivity=std::clamp(i,-10,20); // bbport: below 0 and past 10 (mouse_camera_step.h)
    } else if (key == "camera_controls") {
        v.camera_controls = i != 0;
    } else if (key == "change_appearance") {
        v.change_appearance = i != 0;
    } else if (key == "rebirth") {
        v.rebirth = i != 0;
    } else if (key == "camera_fov_scale") {
        v.camera_fov_scale = Clamp(std::isfinite(f) ? f : 1.0f, 1.0f, 1.5f);
    } else if (key == "camera_distance_scale") {
        v.camera_distance_scale = Clamp(std::isfinite(f) ? f : 1.0f, 0.5f, 1.5f);
    } else if (key == "camera_height_scale") {
        v.camera_height_scale = Clamp(std::isfinite(f) ? f : 1.0f, 0.5f, 1.5f);
    } else if (key == "graphics_controls") {
        v.graphics_controls=i!=0;
    } else if (key == "graphics_vignette") {
        v.graphics_vignette=i!=0;
    // Wide limits: the menu's sliders show the useful range, Ctrl+click types any value up to these.
    } else if (key == "graphics_ao_strength") {
        v.graphics_ao_strength=Clamp(std::isfinite(f)?f:1.0f,0.0f,5.0f);
    } else if (key == "graphics_shadow_scale") {
        v.graphics_shadow_scale=Clamp(std::isfinite(f)?f:1.0f,0.25f,8.0f);
    } else if (key == "graphics_bloom") {
        v.graphics_bloom=Clamp(std::isfinite(f)?f:1.0f,0.0f,10.0f);
    } else if (key == "graphics_saturation") {
        v.graphics_saturation=Clamp(std::isfinite(f)?f:1.0f,0.0f,5.0f);
    } else if (key == "graphics_fog") {
        v.graphics_fog=Clamp(std::isfinite(f)?f:1.0f,0.0f,5.0f);
    } else if (key == "graphics_brightness") {
        v.graphics_brightness=Clamp(std::isfinite(f)?f:1.0f,0.0f,5.0f);
    } else if (key == "graphics_contrast") {
        v.graphics_contrast=Clamp(std::isfinite(f)?f:1.0f,0.5f,1.5f);
    } else if (key == "graphics_hue") {
        v.graphics_hue=Clamp(std::isfinite(f)?f:0.0f,-180.0f,180.0f);
    } else if (key == "model_lod") {
        v.model_lod = std::clamp(i, -2, 2);
    } else if (key == "fullscreen") {
        v.fullscreen = i != 0;
    } else if (key == "hdr") {
        v.hdr = i != 0;
    } else if (key == "hdr_paper_white") {
        v.hdr_paper_white = Clamp(std::isfinite(f) ? f : HdrPaperWhiteDefault, HdrPaperWhiteMin,
                                  HdrPaperWhiteMax);
    } else if (key == "hdr_peak") {
        v.hdr_peak = Clamp(std::isfinite(f) ? f : HdrPeakDefault, HdrPeakMin, HdrPeakMax);
    } else if (key == "hdr_peak_auto") {
        v.hdr_peak_auto = i != 0;
    } else if (key == "hdr_boost") {
        v.hdr_boost = Clamp(std::isfinite(f) ? f : HdrBoostDefault, 0.0f, 1.0f);
    } else if (key == "hdr_ui_brightness") {
        v.hdr_ui_brightness = Clamp(std::isfinite(f) ? f : 1.0f, HdrUiBrightnessMin, HdrUiBrightnessMax);
    } else if (key == "hdr_ui_color") {
        v.hdr_ui_color = Clamp(std::isfinite(f) ? f : HdrUiColorDefault, 0.0f, 1.0f);
    } else if (key == "hdr_mode") {
        int mode = -1;
        for (int m = 0; m < HdrModeCount; ++m) {
            if (value == HdrModeNames[m]) {
                mode = m;
            }
        }
        if (mode < 0) {
            std::printf("Settings: unknown hdr_mode '%s' (expected native or expand); native\n",
                        value.c_str());
        }
        v.hdr_mode = mode < 0 ? HdrModeNative : mode;
    } else if (key == "live_resolution") {
        // 0.4.4 beta 25c: Off or On; "auto" (a GPU check that chose On for strong GPUs) is no longer offered.
        if (value == "auto") {
            std::printf("Settings: live_resolution=auto is no longer offered; off (recommended)\n");
            v.live_resolution = 0;
        } else {
            v.live_resolution = std::clamp(i, 0, 1);
        }
    } else if (key == "cheat_vials") {
        v.cheat_vials = CheatValue(key, value);
    } else if (key == "cheat_bullets") {
        v.cheat_bullets = CheatValue(key, value);
    } else if (key == "cheat_stamina") {
        v.cheat_stamina = i != 0;
    } else if (key == "cheat_stealth") {
        v.cheat_stealth = i != 0;
    } else if (key == "cheat_keep_echoes") {
        v.cheat_keep_echoes = i != 0;
    } else if (key == "cheat_no_durability") {
        v.cheat_no_durability = i != 0;
    } else if (key == "cheat_consumables") {
        v.cheat_consumables = i != 0;
    } else if (key == "cheat_gun_range") {
        v.cheat_gun_range = CheatChoice(key, value, CheatGunRanges, "off, 150, 200 or 300");
    } else if (key == "cheat_echo_multiplier") {
        v.cheat_echo_multiplier = CheatChoice(key, value, CheatEchoMultipliers, "1, 2, 5 or 10");
    } else if (key == "output_res") {
        int w = 0, h = 0;
        char tail = 0;
        if (value == "auto") {
            v.output_res = OutputAuto;
        } else if (std::sscanf(value.c_str(), "%dx%d%c", &w, &h, &tail) == 2 && ValidOutputSize(w, h)) {
            v.output_res = OutputCustom;
            v.custom_output_w = w & ~1;
            v.custom_output_h = h & ~1;
        }
        for (int r = 0; r < OutputAuto; ++r) {
            if (value == std::to_string(OutputWidths[r]) + "x" + std::to_string(OutputHeights[r])) {
                v.output_res = r;
            }
        }
    } else {
        for (int e = 0; e < EffectCount; ++e) {
            if (key == Effects[e].key) {
                v.effects[e] = i != 0;
            }
        }
    }
}

// The menu's language for the launcher's code (BB_MENU_LANGUAGE: en, ru, pt_BR...) or a locale
// (ru_RU.UTF-8, de_DE...): its first two letters; Portuguese only as pt_BR. Else English.
// Simplified Chinese is never picked from the locale, only chosen (menu_language=zh_CN).
MenuLanguage LanguageOfLocale(const char* lang) {
    if (!lang || !lang[0] || !lang[1]) {
        return MenuLanguage::English;
    }
    const auto lower = [lang](int i) { return char(std::tolower(static_cast<unsigned char>(lang[i]))); };
    if (lower(0) == 'p' && lower(1) == 't') {
        return (lang[2] == '_' || lang[2] == '-') && lower(3) == 'b' && lower(4) == 'r'
                   ? MenuLanguage::PortugueseBrazil
                   : MenuLanguage::English;
    }
    for (int l = 0; l < MenuLanguageCount; ++l) {
        const char* code = MenuLanguageCodes[l];
        if (code[2] == '\0' && lower(0) == code[0] && lower(1) == code[1]) {
            return MenuLanguage(l);
        }
    }
    return MenuLanguage::English;
}

} // namespace

// The display's size for output_res = auto: BB_DISPLAY_SIZE ("WxH", from bb-start or run.sh), even
// (the game has half-resolution buffers); 1920x1080 without it.
static std::pair<int, int> DisplaySize() {
    static const std::pair<int, int> size = [] {
        int w = 0, h = 0;
        const char* env = std::getenv("BB_DISPLAY_SIZE");
        if (env && std::sscanf(env, "%dx%d", &w, &h) == 2 && w >= 640 && h >= 360 && w <= 16384 &&
            h <= 8640) {
            return std::pair<int, int>{w & ~1, h & ~1};
        }
        return std::pair<int, int>{1920, 1080};
    }();
    return size;
}

bool ValidOutputSize(int w, int h) {
    return w >= 640 && h >= 360 && w <= 16384 && h <= 8640;
}

int OutputWidth(int index) {
    index = std::clamp(index, 0, OutputCount - 1);
    if (index == OutputCustom) return Get().custom_output_w;
    return index == OutputAuto ? DisplaySize().first : OutputWidths[index];
}

int OutputHeight(int index) {
    index = std::clamp(index, 0, OutputCount - 1);
    if (index == OutputCustom) return Get().custom_output_h;
    return index == OutputAuto ? DisplaySize().second : OutputHeights[index];
}

Values& Get() {
    static Values values;
    return values;
}

const char* MenuText(const char* english, const char* russian) {
    switch (const MenuLanguage language = Get().menu_language) {
    case MenuLanguage::English: return english;
    case MenuLanguage::Russian: return russian;
    default: return MenuTranslation(language, english);
    }
}

void Load() {
    auto& v = Get();
    for (int e = 0; e < EffectCount; ++e) {
        v.effects[e] = Effects[e].default_on;
    }
    // bbport: the menu's language until one is chosen in it (menu_language in config.ini): the
    // launcher's (BB_MENU_LANGUAGE), else the system's, as the launcher picks its own.
    {
        const char* lang = std::getenv("BB_MENU_LANGUAGE");
        if (!lang || !*lang) {
            for (const char* key : {"LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE"}) {
                if (const char* value = std::getenv(key); value && *value) {
                    lang = value;
                    break;
                }
            }
        }
        v.menu_language = LanguageOfLocale(lang);
    }
    if (FILE* file = std::fopen(Path(), "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), file)) {
            std::string text{line};
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            const auto eq = text.find('=');
            if (text.empty() || text[0] == '#' || eq == std::string::npos) {
                continue;
            }
            Set(v, text.substr(0, eq), text.substr(eq + 1));
        }
        std::fclose(file);
        std::printf("Settings: %s\n", Path());
    }
    // Environment overrides (scripts, A/B tests).
    if (const char* env = std::getenv("BB_UPSCALER")) {
        v.upscaler = std::max(UpscalerFromName(env), int(UpscalerOff)); // "none" (no upscaler path): off
    }
    const std::pair<const char*, const char*> env_keys[] = {
        {"BB_FSR_SHARPNESS", "sharpness"},        {"BB_JITTER", "jitter"},
        {"BB_REACTIVE", "reactive"},              {"BB_REACTIVE_SCALE", "reactive_scale"},
        {"BB_REACTIVE_THRESHOLD", "reactive_threshold"}, {"BB_REACTIVE_MAX", "reactive_max"},
        {"BB_UPSCALE_PRESET", "preset"},            {"BB_OBJECT_MOTION", "object_motion"},
        {"BB_HDR", "hdr"},                          {"BB_HDR_PAPER_WHITE", "hdr_paper_white"},
        {"BB_HDR_PEAK", "hdr_peak"},                {"BB_HDR_BOOST", "hdr_boost"},
        {"BB_HDR_MODE", "hdr_mode"},                {"BB_DLSS_PRESET", "dlss_preset"},
        {"BB_RENDER_SCALE", "render_scale"},        {"BB_MIP_BIAS", "mip_bias"},
    };
    for (const auto& [env, key] : env_keys) {
        if (const char* value = std::getenv(env)) {
            Set(v, key, value);
        }
    }
    if (std::getenv("BB_HDR_PEAK")) {
        v.hdr_peak_auto = false; // a peak given for this run is meant
    }
    v.startup_preset = v.preset;
    v.startup_render_scale = v.render_scale;
    v.startup_upscaler = v.upscaler;
    v.startup_object_motion = v.object_motion;
    for (int e = 0; e < EffectCount; ++e) {
        v.startup_effects[e] = v.effects[e];
    }
    v.startup_model_lod = v.model_lod;
    v.startup_output_res = v.output_res;
    v.startup_live_resolution = v.live_resolution;
    v.startup_hdr = v.hdr;
    v.startup_optiscaler = {v.optiscaler_compat, v.optiscaler_ngx_lock, v.optiscaler_clean_thread,
                            v.optiscaler_sync_submit};
}

void ConfigureUpscalerSupport(bool fsr, bool dlss) {
    auto& v = Get();
    v.fsr_supported = fsr;
    v.dlss_supported = dlss;
    const int requested = v.upscaler;
    // 0.4.4 beta 27: TAA when neither runs (community-fixes-plan.md, Decisions: "When AMD's or NVIDIA's
    // runtime can't start"); bb-start chose the start's render size for TAA already when AMD's files
    // are missing (start/main.cpp), so TAA runs at the output size.
    if (requested == UpscalerDlss && !dlss) {
        v.fsr4_problem = fsr ? "DLSS needs an NVIDIA RTX GPU, its driver's NGX and nvngx_dlss; using AMD FSR"
                             : "DLSS needs an NVIDIA RTX GPU, its driver's NGX and nvngx_dlss; using TAA";
        std::printf("Upscaler: dlss unavailable; %s before the first frame\n", fsr ? "AMD FSR" : "TAA");
        v.upscaler = fsr ? UpscalerFsr : UpscalerTaa;
    } else if (requested == UpscalerFsr && !fsr) {
        v.fsr4_problem = "AMD FSR cannot run here (its files, this GPU, or not Windows); using TAA";
        std::printf("Upscaler: AMD FSR unavailable; TAA before the first frame\n");
        v.upscaler = UpscalerTaa;
    }
}

bool FixedRenderSession() {
    const char* size = std::getenv("BB_RENDER_RES");
    return size && size[0];
}

int ResolvePreset(int preset, int output) {
    return preset == PresetAuto ? AutoPreset(OutputHeight(output)) : std::clamp(preset, 0, int(PresetCustom));
}

int RenderPreset() {
    const auto& v = Get();
    if (FixedRenderSession()) {
        return ResolvePreset(v.startup_preset, v.startup_output_res);
    }
    if (v.upscaler == UpscalerTaa) {
        return NativeAA;
    }
    // The output the scene follows: the one at start while one chosen in game waits for a restart.
    return ResolvePreset(v.preset, ResolutionNeedsRestart() ? v.startup_output_res : v.output_res.load());
}

bool OutputChangesLive() {
    return !FixedRenderSession() && Get().startup_live_resolution == 1;
}

bool ResolutionNeedsRestart() {
    const auto& v = Get();
    // 0.4.4 beta 26b: a start at 1920x1080 needs no startup patch, so the session was not fixed and
    // an output chosen in game applied at once whatever Live resolution changes said (the tester's
    // report): the scene stayed at 1080p and the upscaler took the finished frame, the slower way
    // Off avoids. Off now means a restart wherever the game started; the quality preset at 1080p
    // stays live (nothing is lost there: post-processing runs at the output size either way).
    if (!FixedRenderSession())
        return !OutputChangesLive() && v.output_res != v.startup_output_res;
    // TAA needs the live path (native guest targets): run.sh selects it on restart.
    // 0.4.4 beta 27c: presets compared as they resolve (Auto at 1440p is Quality).
    const int preset = ResolvePreset(v.preset, v.output_res);
    return FixedRenderSession() &&
        (preset != ResolvePreset(v.startup_preset, v.startup_output_res) || v.output_res != v.startup_output_res ||
         (preset == PresetCustom && v.render_scale != v.startup_render_scale) ||
         (v.upscaler == UpscalerOff) != (v.startup_upscaler == UpscalerOff) ||
         (v.upscaler == UpscalerTaa) != (v.startup_upscaler == UpscalerTaa));
}

void Save() {
    const auto& v = Get();
    // The keys the menu writes, in this order; the rest of the file stays as it is: the
    // launcher's controls (key.* / pad.*) and its other keys, comments.
    std::vector<std::pair<std::string, std::string>> keys;
    const auto put = [&](const char* key, std::string value) { keys.emplace_back(key, std::move(value)); };
    const auto fixed = [](float value, int digits) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.*f", digits, value);
        return std::string(text);
    };
    const auto flag = [](bool value) { return std::string(value ? "1" : "0"); };
    put("menu_language", MenuLanguageCodes[int(v.menu_language.load())]);
    put("upscaler", UpscalerName(v.upscaler));
    put("fsr_version", FsrVersion());
    put("preset", v.preset == PresetAuto ? std::string("auto") : std::to_string(v.preset.load()));
    put("render_scale", fixed(v.render_scale, 2));
    put("mip_bias", fixed(v.mip_bias, 2));
    put("sharpen", flag(v.sharpen));
    put("sharpness", fixed(v.sharpness, 2));
    put("jitter", flag(v.jitter));
    put("reactive", flag(v.reactive));
    put("object_motion", flag(v.object_motion));
    put("reactive_scale", fixed(v.reactive_scale, 2));
    put("reactive_threshold", fixed(v.reactive_threshold, 2));
    put("reactive_max", fixed(v.reactive_max, 2));
    put("fsr3_velocity_factor", fixed(v.fsr3_velocity_factor, 3));
    put("fsr3_reactiveness_scale", fixed(v.fsr3_reactiveness_scale, 3));
    put("fsr3_shading_change_scale", fixed(v.fsr3_shading_change_scale, 3));
    put("fsr3_accumulation", fixed(v.fsr3_accumulation, 3));
    put("fsr3_min_disocclusion", fixed(v.fsr3_min_disocclusion, 3));
    put("show_fps", flag(v.show_fps));
    put("show_fps_rate", flag(v.show_fps_rate));
    put("show_fps_frame_time", flag(v.show_fps_frame_time));
    put("show_fps_upscaler", flag(v.show_fps_upscaler));
    put("show_fps_cpu", flag(v.show_fps_cpu));
    put("show_fps_gpu", flag(v.show_fps_gpu));
    put("compile_note", flag(v.show_compile_note));
    const auto colour = [](unsigned rgb) {
        char text[8];
        std::snprintf(text, sizeof(text), "#%06X", rgb & 0xFFFFFFu);
        return std::string(text);
    };
    put("fps_position", std::to_string(v.fps_position.load()));
    put("fps_scale", fixed(v.fps_scale, 2));
    put("fps_text_color", colour(v.fps_text_color));
    put("fps_bg_color", colour(v.fps_bg_color));
    put("fps_bg_opacity", fixed(v.fps_bg_opacity, 2));
    put("fps_text_opacity", fixed(v.fps_text_opacity, 2));
    put("fps_border", flag(v.fps_border));
    put("hide_cursor", flag(v.hide_cursor));
    put("fsr4_invert_jitter", flag(v.fsr4_invert_jitter));
    put("dlss_preset", DlssPresetText(v.dlss_preset));
    put("optiscaler_compat", flag(v.optiscaler_compat));
    put("optiscaler_ngx_lock", flag(v.optiscaler_ngx_lock));
    put("optiscaler_clean_thread", flag(v.optiscaler_clean_thread));
    put("optiscaler_sync_submit", v.optiscaler_sync_submit == OptiSubmitAuto ? std::string("auto")
                                                                            : flag(v.optiscaler_sync_submit != 0));
    // Read by patches.py at start.
    for (int e = 0; e < EffectCount; ++e) {
        put(Effects[e].key, flag(v.effects[e]));
    }
    put("fullscreen", flag(v.fullscreen));
    put("hdr", flag(v.hdr));
    put("hdr_paper_white", fixed(v.hdr_paper_white, 0));
    put("hdr_peak", fixed(v.hdr_peak, 0));
    put("hdr_peak_auto", flag(v.hdr_peak_auto));
    put("hdr_boost", fixed(v.hdr_boost, 2));
    put("hdr_ui_brightness", fixed(v.hdr_ui_brightness, 2));
    put("hdr_ui_color", fixed(v.hdr_ui_color, 2));
    put("hdr_mode", HdrModeNames[std::clamp(v.hdr_mode.load(), 0, HdrModeCount - 1)]);
    put("graphics_controls",flag(v.graphics_controls));
    put("graphics_vignette",flag(v.graphics_vignette));
    put("graphics_ao_strength",fixed(v.graphics_ao_strength,2));
    put("graphics_shadow_scale",fixed(v.graphics_shadow_scale,2));
    put("graphics_bloom",fixed(v.graphics_bloom,2));
    put("graphics_saturation",fixed(v.graphics_saturation,2));
    put("graphics_fog",fixed(v.graphics_fog,2));
    put("graphics_brightness",fixed(v.graphics_brightness,2));
    put("graphics_contrast",fixed(v.graphics_contrast,2));
    put("graphics_hue",fixed(v.graphics_hue,0));
    put("camera_controls", flag(v.camera_controls));
    put("widescreen",flag(v.widescreen));
    put("pc_controls",flag(v.pc_controls));
    put("mouse_camera",flag(v.mouse_camera));
    put("mouse_menu",flag(v.mouse_menu));
    put("menu_move",flag(v.menu_move));
    put("mouse_invert_x",flag(v.mouse_invert_x));
    put("mouse_invert_y",flag(v.mouse_invert_y));
    put("mouse_auto_rotation",flag(v.mouse_auto_rotation));
    put("mouse_sensitivity",std::to_string(v.mouse_sensitivity.load()));
    put("button_prompts", ButtonPromptsNames[std::clamp(v.button_prompts.load(), 0, PromptsCount - 1)]);
    put("pad_icons", PadIconsNames[std::clamp(v.pad_icons.load(), 0, PadIconsCount - 1)]);
    put("change_appearance", flag(v.change_appearance));
    put("rebirth", flag(v.rebirth));
    put("camera_fov_scale", fixed(v.camera_fov_scale, 2));
    put("camera_distance_scale", fixed(v.camera_distance_scale, 2));
    put("camera_height_scale", fixed(v.camera_height_scale, 2));
    put("model_lod", std::to_string(v.model_lod.load()));
    put("cheat_vials", CheatText(v.cheat_vials));
    put("cheat_bullets", CheatText(v.cheat_bullets));
    put("cheat_stamina", flag(v.cheat_stamina));
    put("cheat_stealth", flag(v.cheat_stealth));
    put("cheat_gun_range", v.cheat_gun_range == 0 ? "off" : std::to_string(v.cheat_gun_range.load()));
    put("cheat_keep_echoes", flag(v.cheat_keep_echoes));
    put("cheat_echo_multiplier", std::to_string(v.cheat_echo_multiplier.load()));
    put("cheat_no_durability", flag(v.cheat_no_durability));
    put("cheat_consumables", flag(v.cheat_consumables));
    put("output_res", v.output_res == OutputAuto ? std::string("auto")
                                                  : std::to_string(OutputWidth(v.output_res)) + "x" +
                                                        std::to_string(OutputHeight(v.output_res)));
    // Read by run.sh at start.
    put("live_resolution", flag(v.live_resolution != 0));
    if (v.menu_x >= 0.0f && v.menu_y >= 0.0f) {
        put("menu_pos", fixed(v.menu_x, 4) + "," + fixed(v.menu_y, 4));
    }

    BbInputConfig::Append(keys);
    std::string out;
    bool had_lines = false;
    std::vector<bool> written(keys.size());
    if (FILE* file = std::fopen(Path(), "r")) {
        char line[512];
        while (std::fgets(line, sizeof(line), file)) {
            had_lines = true;
            std::string text{line};
            text.erase(text.find_last_not_of("\r\n") + 1);
            const auto eq = text.find('=');
            if (!text.empty() && text[0] != '#' && eq != std::string::npos) {
                std::string key = text.substr(0, eq);
                key.erase(key.find_last_not_of(" \t") + 1);
                key.erase(0, key.find_first_not_of(" \t"));
                const auto it = std::find_if(keys.begin(), keys.end(),
                                             [&](const auto& k) { return k.first == key; });
                if (it != keys.end()) {
                    out += it->first + "=" + it->second + "\n";
                    written[size_t(it - keys.begin())] = true;
                    continue;
                }
            }
            out += text + "\n";
        }
        std::fclose(file);
    }
    if (!had_lines) {
        out = "# Hunter's Workshop settings (in-game menu: Insert / L3+R3)\n";
    }
    for (size_t k = 0; k < keys.size(); ++k) {
        if (!written[k]) {
            out += keys[k].first + "=" + keys[k].second + "\n";
        }
    }
    // Replaced whole: a crash while writing leaves the old file.
    const std::string temporary = std::string(Path()) + ".tmp";
    FILE* file = std::fopen(temporary.c_str(), "w");
    bool ok = file && std::fwrite(out.data(), 1, out.size(), file) == out.size();
    ok = file && std::fclose(file) == 0 && ok;
#ifdef _WIN32
    const bool replaced = ok && MoveFileExA(temporary.c_str(), Path(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    const bool replaced = ok && std::rename(temporary.c_str(), Path()) == 0;
#endif
    if (!replaced) {
        std::printf("Settings: cannot write %s\n", Path());
    }
}

float PresetScale(int preset) {
    static constexpr float scales[PresetCustom] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
    preset = ResolvePreset(preset, Get().output_res); // Auto: for the output chosen
    if (preset == PresetCustom) {
        // The startup patch's render size is fixed for the session (BB_RENDER_RES, run.sh).
        const auto& v = Get();
        const float scale = FixedRenderSession() ? v.startup_render_scale : v.render_scale.load();
        return std::clamp(std::isfinite(scale) ? scale : 1.0f, RenderScaleMin, RenderScaleMax);
    }
    return scales[std::clamp(preset, 0, PresetCustom - 1)];
}

const char* PresetName(int preset) {
    static constexpr const char* names[PresetCount] = {"Native AA", "Quality", "Balanced", "Performance",
                                                       "Ultra Performance", "Custom", "Auto"};
    return names[std::clamp(preset, 0, PresetCount - 1)];
}

namespace {
/// The output index the presets apply to: now in a live session (the one at start while a chosen one
/// waits for a restart), the one chosen in a startup-patched session (its next start).
int PresetOutputIndex() {
    const auto& v = Get();
    return !FixedRenderSession() && ResolutionNeedsRestart() ? v.startup_output_res : v.output_res.load();
}
} // namespace

SceneSize PresetOutput() {
    const int output = PresetOutputIndex();
    return {OutputWidth(output), OutputHeight(output)};
}

int RenderPercentMin() {
    const auto& v = Get();
    const SceneSize out = PresetOutput();
    const bool patched =
        FixedRenderSession() && !(out.width == 1920 && out.height == 1080) && v.live_resolution != 1;
    const double most = patched ? std::max(1.0, std::min({3.0, out.width / 640.0, out.height / 360.0})) : 3.0;
    return int(std::lround(100.0 / most));
}

std::string NewestFsrVersion(const std::vector<std::string>& offered) {
    const auto numbers = [](const std::string& name) {
        std::vector<int> parts;
        int part = -1;
        for (const char c : FsrVersionKey(name)) {
            if (std::isdigit(static_cast<unsigned char>(c))) {
                part = (part < 0 ? 0 : part * 10) + (c - '0');
            } else if (part >= 0) {
                parts.push_back(part);
                part = -1;
            }
        }
        if (part >= 0) {
            parts.push_back(part);
        }
        return parts;
    };
    std::string newest;
    for (const auto& name : offered) {
        if (newest.empty() || numbers(name) > numbers(newest)) {
            newest = name;
        }
    }
    return newest;
}

SceneSize PresetRenderSize(int preset) {
    const auto& v = Get();
    // The output it applies to (PresetOutputIndex); Auto resolves for it.
    const bool fixed = FixedRenderSession();
    const int output = PresetOutputIndex();
    preset = ResolvePreset(preset, output);
    double scale = PresetScale(std::min(preset, PresetCustom - 1));
    if (preset == PresetCustom) {
        const float custom = v.render_scale;
        scale = std::clamp(std::isfinite(custom) ? double(custom) : 1.0, double(RenderScaleMin), double(RenderScaleMax));
    }
    const int w = OutputWidth(output), h = OutputHeight(output);
    if (!fixed || (w == 1920 && h == 1080) || v.live_resolution == 1) {
        const double s = std::clamp(scale, 1.0, 3.0);
        return SceneSize{int(std::max(1l, std::lround(w / s / 4)) * 4), int(std::max(1l, std::lround(h / s / 4)) * 4)};
    }
    const double s = std::max(1.0, std::min({scale, w / 640.0, h / 360.0}));
    SceneSize size{std::max(4, int(std::nearbyint(w / s / 4)) * 4), std::max(4, int(std::nearbyint(h / s / 4)) * 4)};
    if (size.width == 1920 && size.height == 1080) {
        size = {1916, 1076};
    }
    return size;
}

namespace {
std::mutex fsr_version_mutex;
std::string fsr_version_wanted = "auto";
std::vector<std::string> fsr_versions_offered;
} // namespace

std::string FsrVersionKey(const std::string& name) {
    std::string key = name;
    while (!key.empty() && (key.back() == '*' || key.back() == ' ')) {
        key.pop_back(); // AMD's " *": provided by the driver
    }
    key.erase(0, key.find_first_not_of(' '));
    for (char& c : key) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    if (key.rfind("fsr", 0) == 0) {
        key.erase(0, 3);
        key.erase(0, key.find_first_not_of(' '));
    }
    return key.empty() ? std::string("auto") : key;
}

int FsrVersionMatch(const std::vector<std::string>& offered, const std::string& wanted) {
    const std::string want = FsrVersionKey(wanted);
    if (want == "auto") {
        return -1;
    }
    int found = -1;
    bool found_driver = false;
    for (int i = 0; i < int(offered.size()); ++i) {
        const std::string name = FsrVersionKey(offered[i]);
        const bool same = name == want || (name.size() > want.size() && name.compare(0, want.size(), want) == 0 &&
                                           name[want.size()] == '.');
        if (!same) {
            continue;
        }
        const bool driver = offered[i].find('*') != std::string::npos;
        if (found < 0 || (found_driver && !driver)) {
            found = i;
            found_driver = driver;
        }
    }
    return found;
}

std::string FsrVersion() {
    std::lock_guard lock{fsr_version_mutex};
    return fsr_version_wanted;
}

void SetFsrVersion(const std::string& version) {
    std::lock_guard lock{fsr_version_mutex};
    fsr_version_wanted = FsrVersionKey(version);
}

std::vector<std::string> FsrVersionsOffered() {
    std::lock_guard lock{fsr_version_mutex};
    return fsr_versions_offered;
}

void SetFsrVersionsOffered(std::vector<std::string> names) {
    std::lock_guard lock{fsr_version_mutex};
    fsr_versions_offered = std::move(names);
}

int ModelPreset(int preset) {
    preset = ResolvePreset(preset, Get().output_res);
    if (preset != PresetCustom) {
        return std::clamp(preset, 0, PresetCustom - 1);
    }
    // The fixed preset whose ratio is nearest: a 1.3 ratio runs the Quality model, 1.0 Native.
    const float scale = PresetScale(preset);
    int nearest = NativeAA;
    for (int p = NativeAA; p < PresetCustom; ++p) {
        if (std::abs(PresetScale(p) - scale) < std::abs(PresetScale(nearest) - scale)) {
            nearest = p;
        }
    }
    return nearest;
}

bool DlssPresetValid(int preset) {
    // NVSDK_NGX_DLSS_Hint_Render_Preset, as NVIDIA's DLSS Programming Guide 310.6.0 has it: A-D
    // removed, E and F (the CNN models) deprecated, G-I reserved; J, K, L, M (the transformers).
    return preset == DlssPresetDefault || (preset >= 10 && preset <= 13);
}

int DlssPresetFromText(const char* text) {
    if (!text || std::strcmp(text, "default") == 0) {
        return DlssPresetDefault;
    }
    if (std::strcmp(text, "auto") == 0) {
        return DlssPresetAuto;
    }
    const char letter = char(std::tolower(static_cast<unsigned char>(text[0])));
    if (letter >= 'a' && letter <= 'z' && text[1] == '\0') {
        // 0.4.4 beta 25b: E and F, the CNN models NVIDIA deprecated (310.6.0; F "should not be used"),
        // are no longer offered: a saved one runs Auto, NVIDIA's model for each quality mode.
        if (letter == 'e' || letter == 'f') {
            std::printf("Settings: DLSS preset '%s' (a CNN model) is deprecated by NVIDIA; auto\n", text);
            return DlssPresetAuto;
        }
        // bbport: letters NVIDIA removed (A-D) or ignores (G-I, N, O) ran the DLL's default
        // while the menu showed the letter: K instead, said once here.
        if (!DlssPresetValid(letter - 'a' + 1)) {
            std::printf("Settings: DLSS preset '%s' is not one of NVIDIA's (J, K, L, M); auto\n", text);
            return DlssPresetAuto;
        }
        return letter - 'a' + 1;
    }
    std::printf("Settings: unknown dlss_preset '%s' (expected auto, default or a letter); auto\n", text);
    return DlssPresetAuto;
}

const char* DlssPresetText(int preset) {
    static constexpr const char* letters[DlssPresetMax] = {"a", "b", "c", "d", "e", "f", "g", "h", "i",
                                                           "j", "k", "l", "m", "n", "o", "p", "q", "r",
                                                           "s", "t", "u", "v", "w", "x", "y", "z"};
    if (preset == DlssPresetAuto) {
        return "auto";
    }
    return preset >= 1 && preset <= DlssPresetMax ? letters[preset - 1] : "default";
}

const char* UpscalerName(int upscaler) {
    static constexpr const char* names[UpscalerCount] = {"off", "fsr", "taa", "dlss"};
    return names[std::clamp(upscaler, 0, UpscalerCount - 1)];
}

int UpscalerFromName(const char* name) {
    if (!name) {
        return -1;
    }
    for (int u = 0; u < UpscalerCount; ++u) {
        if (std::strcmp(name, UpscalerName(u)) == 0) {
            return u;
        }
    }
    // 0.4.4 beta 27: FSR 3.1 (FireBurn's), FSR 4 and FSR 4.1.1 are AMD's FSR now, which picks the version.
    for (const char* old : {"fsr3", "fsr4", "fsr411"}) {
        if (std::strcmp(name, old) == 0) {
            static bool told = false;
            if (!told) {
                told = true;
                std::printf("Settings: upscaler=%s is AMD FSR from 0.4.4 beta 27 (AMD's FSR SDK picks the version)\n",
                            name);
            }
            return UpscalerFsr;
        }
    }
    return -1;
}

} // namespace BbSettings

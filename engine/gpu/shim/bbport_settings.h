// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: user settings changed at run time from the in-game menu (bbport_overlay.h) and kept
// in config.ini (BB_CONFIG overrides the path). Environment variables override the file at
// start. Readers load the atomics every frame; writers are the menu and Load().

#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace BbSettings {

/// 0.4.4 beta 27: one FSR, AMD's (AMD FSR SDK 2.4, vk_amd_fsr.h), which picks the version this GPU
/// runs; it replaced FSR 3.1 (FireBurn's), FSR 4 (the built-in v07 model) and FSR 4.1.1 (fsr4vk and
/// the replay). "fsr3", "fsr4" and "fsr411" in config.ini read as "fsr".
enum Upscaler : int { UpscalerOff = 0, UpscalerFsr = 1, UpscalerTaa = 2, UpscalerDlss = 3, UpscalerCount };
/// AMD FSR or DLSS: one frame's inputs to a separate upscaler, which writes the output image
/// (TemporalUpscaler::RecordFrameUpscaler); TAA is recorded in place.
inline bool IsFrameUpscaler(int upscaler) {
    return upscaler == UpscalerFsr || upscaler == UpscalerDlss;
}
/// 0.4.4 beta 27b: Upscaler None in the menus (the launcher's too): TAA, or no upscaler when the game's
/// own anti-aliasing is on (Anti-aliasing: TAA, the game's own, or off).
inline int NoUpscaling(bool game_aa) { return game_aa ? UpscalerOff : UpscalerTaa; }
/// The game's own anti-aliasing pass applies (a patch at start) only without an upscaler or TAA:
/// patches.cpp NoUpscaler leaves it out under them.
inline bool GameAaApplies(bool game_aa, int upscaler) { return game_aa && upscaler == UpscalerOff; }
/// fsr_version in config.ini (0.4.4 beta 27c): "auto", AMD's choice (no version override: the newest
/// this GPU runs, FSR 4.1 on RX 6000 and newer, FSR 3.1 elsewhere), or a version AMD's SDK offers on
/// this GPU by its name ("3.1.6", "2.3.4", "4.1.2"; 27a's "3.1" asks for the newest FSR 3.1). One
/// AMD does not offer here runs AMD's choice (the log says so). Live: the context is made again.
std::string FsrVersion();
void SetFsrVersion(const std::string& version);
/// The versions AMD's SDK offers on this GPU, as AMD lists and names them (" *": provided by the
/// driver); empty until the renderer has asked, or when AMD FSR cannot run. Set by the renderer.
std::vector<std::string> FsrVersionsOffered();
void SetFsrVersionsOffered(std::vector<std::string> names);
/// A version name as config.ini keeps it: AMD's " *" and an "FSR " prefix dropped, lowercase ("auto"
/// for nothing).
std::string FsrVersionKey(const std::string& name);
/// Which of `offered` a wanted version asks for: its exact name, or the newest listed of a family
/// ("3.1": the first 3.1.x), one shipped with the game before the driver's; -1 for "auto" or none.
int FsrVersionMatch(const std::vector<std::string>& offered, const std::string& wanted);
/// bbport: frame_gen in config.ini (BB_FRAME_GEN): generated frames between the game's own
/// (vk_frame_generation.h): NVIDIA DLSS Frame Generation through Streamline (vk_streamline.h; RTX
/// 40/50, loaded at start, frame_gen_frames per real one). FSR 3.1 frame interpolation went with
/// FireBurn's FSR in 0.4.4 beta 27: "fsr3" reads as off (AMD's own needs AMD's swapchain; after 0.4.4).
enum FrameGen : int { FrameGenOff = 0, FrameGenFsr3 = 1, FrameGenDlss = 2, FrameGenCount };
inline constexpr const char* FrameGenNames[] = {"off", "fsr3", "dlss"};
static_assert(sizeof(FrameGenNames) / sizeof(FrameGenNames[0]) == FrameGenCount);
/// frame_gen_frames: DLSS generated frames per real one (1 = 2x .. 5 = 6x; RTX 50 for more than 1).
inline constexpr int FrameGenFramesMax = 5;
/// 0.4.4 beta 26: frame generation (FSR 3.1 and DLSS) is not offered (the tester's call in beta 23;
/// community-fixes-plan.md, Decisions): frame_gen reads as off (BB_FRAME_GEN too, logged), the
/// in-game menu and the launcher leave it out, and DLSS Frame Generation's plugin is never
/// requested. The code stays until it is decided on.
inline constexpr bool FrameGenOffered = false;
/// reflex in config.ini (BB_REFLEX): NVIDIA Reflex low latency while Streamline is loaded; on (at
/// least) whenever DLSS Frame Generation runs, as NVIDIA requires.
enum Reflex : int { ReflexOff = 0, ReflexOn = 1, ReflexBoost = 2, ReflexCount };
inline constexpr const char* ReflexNames[] = {"off", "on", "boost"};
static_assert(sizeof(ReflexNames) / sizeof(ReflexNames[0]) == ReflexCount);
/// Custom (render_scale in config.ini): output / render size of the player's choosing. 0.4.4 beta 27c:
/// Auto (preset=auto, the default) picks one by the output's height (AutoPreset); config.ini keeps the
/// others' numbers as before.
enum Preset : int { NativeAA = 0, Quality, Balanced, Performance, UltraPerformance, PresetCustom, PresetAuto, PresetCount };
/// Auto's choice for an output this many lines tall: Native AA up to 1200 (1080p, 1920x1200), Quality up
/// to 1600 (1440p, 2560x1600), Performance above (4K renders 1920x1080): by default the scene is never much
/// more work than native 1080p. bb-start (patches.cpp) and patches.py choose the same.
inline constexpr int AutoPreset(int height) { return height <= 1200 ? NativeAA : height <= 1600 ? Quality : Performance; }
inline constexpr float RenderScaleMin = 1.0f, RenderScaleMax = 3.0f;
/// mip_bias in config.ini: added to the scene's texture LOD bias while an upscaler runs.
inline constexpr float MipBiasMin = -2.0f, MipBiasMax = 1.0f;
/// bbport: where the FPS counter sits (fps_position).
enum FpsPosition : int { FpsTopRight = 0, FpsTopLeft, FpsBottomRight, FpsBottomLeft, FpsTopCentre, FpsPositionCount };
enum DebugView : int { DebugNone = 0, DebugReactive = 1, DebugMotion = 2, DebugViewCount };
/// The menu's language, in the Language selector's order (bbport_overlay.cpp).
enum class MenuLanguage {
    English, Russian, Spanish, PortugueseBrazil, French, German, Italian, Polish, Turkish, ChineseSimplified
};
/// menu_language in config.ini, by MenuLanguage; the launcher's BB_MENU_LANGUAGE uses the same codes.
inline constexpr const char* MenuLanguageCodes[] = {"en", "ru", "es", "pt_BR", "fr", "de", "it", "pl", "tr", "zh_CN"};
inline constexpr int MenuLanguageCount = int(sizeof(MenuLanguageCodes) / sizeof(MenuLanguageCodes[0]));
static_assert(MenuLanguageCount == int(MenuLanguage::ChineseSimplified) + 1);

/// Game effects switched by the community patches at start (patches.py EFFECTS): ini key,
/// menu label, default (the game's own behaviour).
struct Effect {
    const char* key;
    const char* label;
    const char* label_ru;
    bool default_on;
};
inline constexpr Effect Effects[] = {
    // bbport: chromatic aberration, motion blur and the game's own anti-aliasing start off - a clearer
    // picture, and the upscalers anti-alias. Depth of field on and the intros skipped (0.4.4).
    {"effect_chromatic_aberration", "Chromatic aberration", "Хроматическая аберрация", false},
    {"effect_dof", "Depth of field (DoF)", "Глубина резкости (DoF)", true},
    {"effect_motion_blur", "Motion blur", "Размытие в движении", false},
    {"effect_ssao", "Ambient occlusion (SSAO)", "Затенение SSAO", true},
    {"effect_game_aa", "Game's own anti-aliasing", "Собственное сглаживание игры", false},
    {"effect_dynamic_shadows", "Shadows from dynamic lights", "Тени от динамических источников", true},
    {"effect_ssr", "Screen-space reflections (not in original game)", "Отражения SSR (не было в игре)", false},
    {"skip_intro", "Skip startup intros", "Пропуск заставок при запуске", true},
    {"debug_camera", "Free camera (Cross + L3)", "Свободная камера (Cross + L3)", false},
    {"debug_menu", "Debug menu (requires font files)", "Debug menu (нужны файлы шрифтов)", false},
};
inline constexpr int EffectCount = int(sizeof(Effects) / sizeof(Effects[0]));
/// effects[] index of the game's own anti-aliasing (the launcher's and menus' Anti-aliasing choice).
inline constexpr int EffectGameAa = 4;
constexpr bool SameKey(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static_assert(SameKey(Effects[EffectGameAa].key, "effect_game_aa"));
/// Live output resolutions: the upscaler's output and the UI host targets. OutputAuto is the
/// display's own size (output_res = auto): BB_DISPLAY_SIZE, which bb-start reads from Windows at
/// start, else 1920x1080. OutputCustom is any other WxH the player wrote (output_res = 1280x1024, a
/// 5:4 screen). Use OutputWidth/OutputHeight, which know both.
inline constexpr int OutputWidths[] = {1280,1600,1920,2560,3840,2560,3440,3840,3840,5120,5120,7680,0,0};
inline constexpr int OutputHeights[] = {720,900,1080,1440,2160,1080,1440,1600,1080,1440,2160,2160,0,0};
inline constexpr int OutputCount = 14;
inline constexpr int OutputAuto = 12;
inline constexpr int OutputCustom = 13;
/// A custom output size is even, 640x360 to 16384x8640 (the swapchain and the game's half-size buffers).
bool ValidOutputSize(int w, int h);
inline constexpr int OutputDefault = 2; ///< 1920x1080, the game's own size
static_assert(sizeof(OutputWidths) / sizeof(OutputWidths[0]) == OutputCount &&
              sizeof(OutputHeights) / sizeof(OutputHeights[0]) == OutputCount);
int OutputWidth(int index);
int OutputHeight(int index);
/// Live cheats (engine/cheats.cpp): cheat_vials / cheat_bullets in config.ini are "off",
/// "infinite" or an amount kept in the inventory, 1..CheatMaxAmount.
inline constexpr int CheatOff = 0, CheatInfinite = -1, CheatMaxAmount = 99;
/// cheat_gun_range: "off" (0) or the firearm bullets' range in percent.
inline constexpr int CheatGunRanges[] = {0, 150, 200, 300};
/// cheat_echo_multiplier: Blood Echoes from kills times this (1: the game's own).
inline constexpr int CheatEchoMultipliers[] = {1, 2, 5, 10};
/// HDR output (hdr_paper_white / hdr_peak in nits, hdr_boost 0..1): defaults and ranges.
inline constexpr float HdrPaperWhiteDefault = 200.0f, HdrPaperWhiteMin = 10.0f, HdrPaperWhiteMax = 500.0f;
inline constexpr float HdrPeakDefault = 1000.0f, HdrPeakMin = 10.0f, HdrPeakMax = 4000.0f;
inline constexpr float HdrBoostDefault = 0.25f;
/// Native mode: the game's UI at hdr_ui_brightness times paper white, and hdr_ui_color of the way
/// from sRGB's colours (0) to a wide-gamut SDR monitor's (1, DCI-P3).
inline constexpr float HdrUiBrightnessMin = 0.5f, HdrUiBrightnessMax = 2.0f, HdrUiColorDefault = 1.0f;
/// The swapchain's HDR signal (Values::hdr_output).
enum HdrOutput : int { HdrOutputOff = 0, HdrOutputHdr10 = 1, HdrOutputScRgb = 2 };
/// hdr_mode in config.ini (docs/hdr.md). Native: the highlights' brightness from the game's own
/// HDR scene (vk_hdr_native.h), expand where a frame has none (menus, loading, movies). Expand:
/// the SDR picture's highlights stretched towards the peak.
enum HdrMode : int { HdrModeNative = 0, HdrModeExpand = 1, HdrModeCount };
inline constexpr const char* HdrModeNames[] = {"native", "expand"};
static_assert(sizeof(HdrModeNames) / sizeof(HdrModeNames[0]) == HdrModeCount);

/// dlss_preset in config.ini (BB_DLSS_PRESET): DLSS's render preset, "default" (0: the DLL's own
/// for each quality mode) or a letter (1 = A .. 26 = Z). K (11, the transformer model) unless set.
inline constexpr int DlssPresetDefault = 0, DlssPresetK = 11, DlssPresetMax = 26;
/// bbport: "auto": the model per quality mode as NVIDIA's DLSS 4.5 pairs them: K (DLAA, Quality,
/// Balanced), M (Performance), L (Ultra Performance).
inline constexpr int DlssPresetAuto = -1, DlssPresetL = 12, DlssPresetM = 13;
int DlssPresetFromText(const char* text);
/// "default" or the preset's letter (lowercase), as config.ini keeps it.
const char* DlssPresetText(int preset);

/// optiscaler_sync_submit in config.ini (BB_OPTISCALER_SYNC_SUBMIT): one submit thread only while
/// OptiScaler's frame generation is on (auto), always, or never.
enum OptiSubmit : int { OptiSubmitAuto = -1, OptiSubmitOff = 0, OptiSubmitOn = 1 };

/// button_prompts in config.ini: the game's button icons drawn as the bound keys while the keyboard
/// or mouse is in use (auto), always (keyboard), or never (controller). engine/key_prompts.h.
enum ButtonPrompts : int { PromptsAuto = 0, PromptsKeyboard = 1, PromptsController = 2, PromptsCount };
inline constexpr const char* ButtonPromptsNames[] = {"auto", "keyboard", "controller"};
static_assert(sizeof(ButtonPromptsNames) / sizeof(ButtonPromptsNames[0]) == PromptsCount);

/// pad_icons in config.ini: the game's controller icons as they are (PlayStation's) or as an Xbox
/// controller's buttons; auto follows the gamepad in use. engine/key_prompts.h.
enum PadIcons : int { PadIconsAuto = 0, PadIconsPlayStation = 1, PadIconsXbox = 2, PadIconsCount };
inline constexpr const char* PadIconsNames[] = {"auto", "playstation", "xbox"};
static_assert(sizeof(PadIconsNames) / sizeof(PadIconsNames[0]) == PadIconsCount);

struct Values {
    std::atomic<MenuLanguage> menu_language{MenuLanguage::English};
    std::atomic<int> upscaler{UpscalerFsr};
    /// fsr_version: FsrVersion() / SetFsrVersion() (a name); counts its changes, for the renderer.
    std::atomic<unsigned> fsr_version_serial{0};
    std::atomic<int> preset{PresetAuto};
    std::atomic<int> frame_gen{FrameGenOff}; ///< FrameGen; live
    /// While generated frames are shown, the game runs at half the refresh rate so every
    /// generated frame reaches the display (any present mode); live.
    std::atomic<bool> frame_gen_cap{true};
    /// Set by the renderer: frame generation is running (the FPS counter shows both rates), and
    /// the frames shown and the game's own frames a second.
    std::atomic<bool> frame_gen_active{false};
    std::atomic<float> frame_gen_shown_fps{0.0f}, frame_gen_game_fps{0.0f};
    /// bbport: frames shown per game frame while frame_gen_active (2 for FSR 3.1, generated + 1
    /// for DLSS): the flip driver's frame limit and cap divide by it. Set by the renderer.
    std::atomic<int> frame_gen_multiplier{2};
    /// DLSS Frame Generation: generated frames per real one (1..FrameGenFramesMax); live.
    std::atomic<int> frame_gen_frames{1};
    /// bbport: DLSS dynamic multi frame generation (frame_gen_dynamic; live; RTX 50, Streamline's
    /// DLSSGMode::eDynamic): the driver picks 2x..(frame_gen_frames + 1)x per moment to reach
    /// frame_gen_target_fps (0: the display's refresh rate), as OptiScaler's ForceDMFG.
    std::atomic<bool> frame_gen_dynamic{false};
    std::atomic<int> frame_gen_target_fps{0};
    /// Set by the renderer: this GPU and driver can run DLSS dynamic multi frame generation.
    std::atomic<bool> dlss_fg_dynamic_supported{false};
    /// Reflex (Reflex); live while Streamline is loaded.
    std::atomic<int> reflex{ReflexOn};
    /// Set by the renderer: Streamline is loaded (frame_gen=dlss at start), DLSS Frame Generation
    /// runs on this GPU, the most frames it may generate per real one, and why not (or null).
    std::atomic<bool> streamline_loaded{false}, dlss_fg_supported{false};
    std::atomic<int> dlss_fg_max_frames{0};
    std::atomic<const char*> dlss_fg_problem{nullptr};
    std::atomic<bool> sharpen{true};
    std::atomic<float> sharpness{0.2f};
    std::atomic<bool> jitter{true};
    std::atomic<bool> reactive{false};
    std::atomic<bool> object_motion{true};
    std::atomic<float> reactive_scale{1.0f};
    std::atomic<float> reactive_threshold{0.2f};
    std::atomic<float> reactive_max{0.9f};
    /// bbport: FSR 3.1's tuning constants (fsr3_* in config.ini; live), as OptiScaler offers them:
    /// velocity factor (lower: steadier bright pixels in motion), reactiveness and shading change
    /// scales, accumulation added per frame (lower: less ghosting, more thin-detail flicker) and
    /// the least accumulation at disocclusions (higher: less white-pixel flicker). SDK defaults.
    std::atomic<float> fsr3_velocity_factor{1.0f}, fsr3_reactiveness_scale{1.0f},
        fsr3_shading_change_scale{1.0f}, fsr3_accumulation{0.333f}, fsr3_min_disocclusion{-0.333f};
    std::atomic<int> debug_view{DebugNone};
    std::atomic<bool> show_fps{false};
    /// What the FPS counter shows: the frame rate, the frame time, the upscaler.
    std::atomic<bool> show_fps_rate{true};
    std::atomic<bool> show_fps_frame_time{true};
    std::atomic<bool> show_fps_upscaler{true};
    std::atomic<bool> show_fps_cpu{false}; ///< bbport: the PC's CPU use (bbport_usage.h)
    std::atomic<bool> show_fps_gpu{false}; ///< bbport: the GPU's 3D engine use
    /// "Building shaders" in a corner while pipelines are built in play (compile_note).
    std::atomic<bool> show_compile_note{true};
    /// bbport: the FPS counter's look (fps_* in config.ini; live): its corner (FpsPosition),
    /// size, text and background colours (0xRRGGBB), background opacity and border.
    std::atomic<int> fps_position{0};
    std::atomic<float> fps_scale{1.0f};
    std::atomic<unsigned> fps_text_color{0xFFFFFFu};
    std::atomic<unsigned> fps_bg_color{0x0F0F0Fu};
    std::atomic<float> fps_bg_opacity{0.5f};
    std::atomic<float> fps_text_opacity{1.0f};
    std::atomic<bool> fps_border{true};
    /// The mouse cursor hidden over the game 2 s after the mouse last moved (window.cpp).
    std::atomic<bool> hide_cursor{true};
    /// The settings menu's position (fraction of the screen), -1 until it is moved.
    std::atomic<float> menu_x{-1.0f}, menu_y{-1.0f};
    // FSR 4 checks (menu): the provider's auto exposure, the jitter sign it is given.
    std::atomic<bool> fsr4_auto_exposure{true};
    std::atomic<bool> fsr4_invert_jitter{false};
    std::atomic<int> active_render_width{1920}, active_render_height{1080};
    /// Set by the renderer: the game's motion blur is held off (engine/graphics.cpp), as a temporal
    /// upscaler takes the game's finished frame, blur and all (BB_UPSCALER_NO_BLUR).
    std::atomic<bool> motion_blur_held{false};
    /// Applied at start (patches.py); the menu shows when a restart is needed.
    std::atomic<bool> effects[EffectCount]{};
    std::atomic<bool> camera_controls{false}; // startup engine hook, opt-in
    std::atomic<bool> widescreen{true},pc_controls{true},mouse_camera{true},mouse_menu{true};
    /// bbport: menu_move, the movement and look keys (and the mouse camera, with mouse_menu off)
    /// stay the character's while a menu is open in the world; the arrow keys navigate it.
    std::atomic<bool> menu_move{false};
    std::atomic<bool> mouse_invert_x{false},mouse_invert_y{false},mouse_auto_rotation{false};
    std::atomic<int> mouse_sensitivity{5};
    std::atomic<int> button_prompts{PromptsAuto}; ///< ButtonPrompts; live
    std::atomic<int> pad_icons{PadIconsAuto}; ///< PadIcons; live
    std::atomic<bool> change_appearance{false}, rebirth{false}; // next launch, engine features
    std::atomic<bool> graphics_controls{false}, graphics_vignette{true};
    std::atomic<float> graphics_ao_strength{1},graphics_shadow_scale{1},graphics_bloom{1},graphics_saturation{1};
    /// Fog opacity (0..1 of the area's), the tone map's brightness (ColorV) and contrast (Contrust)
    /// scales and a hue offset in degrees (ColorH); 1, 1, 1 and 0 leave the area's own.
    std::atomic<float> graphics_fog{1},graphics_brightness{1},graphics_contrast{1},graphics_hue{0};
    std::atomic<float> camera_fov_scale{1.0f}, camera_distance_scale{1.0f}, camera_height_scale{1.0f};
    std::atomic<int> model_lod{0}; ///< -2 highest .. 2 lowest, 0 the game's
    std::atomic<int> output_res{OutputDefault}; ///< index into OutputWidths
    /// output_res = OutputCustom's size, from config.ini (fixed for the session).
    int custom_output_w = 1920, custom_output_h = 1080;
    /// CheatOff, CheatInfinite or the amount to keep (1..CheatMaxAmount); live.
    std::atomic<int> cheat_vials{CheatOff}, cheat_bullets{CheatOff};
    /// The other cheats (0/1 in config.ini); live.
    std::atomic<bool> cheat_stamina{false}, cheat_stealth{false}, cheat_keep_echoes{false},
        cheat_no_durability{false}, cheat_consumables{false};
    /// One of CheatGunRanges / CheatEchoMultipliers; live.
    std::atomic<int> cheat_gun_range{0}, cheat_echo_multiplier{1};
    /// Borderless fullscreen window at the desktop size (F11 toggles; BB_FULLSCREEN overrides).
    std::atomic<bool> fullscreen{false};
    /// HDR output (hdr, BB_HDR): the game's SDR picture shown in an HDR swapchain when the
    /// display offers one (Windows HDR on). Applied at start (startup_hdr).
    std::atomic<bool> hdr{false};
    /// Nits of the SDR picture's white, the display's peak, and the highlight expansion (0: SDR
    /// white stays at paper white, 1: up to the peak); live (BB_HDR_PAPER_WHITE, BB_HDR_PEAK,
    /// BB_HDR_BOOST).
    std::atomic<float> hdr_paper_white{HdrPaperWhiteDefault}, hdr_peak{HdrPeakDefault},
        hdr_boost{HdrBoostDefault};
    /// hdr_peak_auto: the peak the display reports (hdr_display_peak) instead of hdr_peak, when it
    /// reports one; live. BB_HDR_PEAK turns it off.
    std::atomic<bool> hdr_peak_auto{true};
    /// Set by the window: the peak brightness Windows reports for the game's display (DXGI
    /// MaxLuminance, the Windows HDR Calibration app's value when it has run), 0 when unknown.
    std::atomic<float> hdr_display_peak{0.0f};
    /// The in-game menu's SDR | HDR comparison: the left half as SDR. Not saved.
    std::atomic<bool> hdr_compare{false};
    std::atomic<float> hdr_ui_brightness{1.0f}, hdr_ui_color{HdrUiColorDefault};
    /// HdrMode (hdr_mode, BB_HDR_MODE); live. In native mode boost is the share of the scene's
    /// highlight range shown: sqrt(boost) of it in log2 (0.25: half, 1: all of it).
    std::atomic<int> hdr_mode{HdrModeNative};
    /// Set by the renderer: the HDR output in use (HdrOutput).
    std::atomic<int> hdr_output{HdrOutputOff};
    /// Live resolution and preset changes (run.sh): 0 off, the default and recommended (startup patch,
    /// fastest), 1 on. On restart. "auto" (a GPU check) is read as off from 0.4.4 beta 25c.
    std::atomic<int> live_resolution{0};
    /// Why the chosen upscaler cannot run (AMD's files, this GPU, a failure), or null. Set by the renderer.
    std::atomic<const char*> fsr4_problem{nullptr};
    /// Set by the renderer: AMD FSR and DLSS can run here.
    std::atomic<bool> fsr_supported{false}, dlss_supported{false};
    /// Set by the renderer: AMD's name of the FSR version running ("FSR 4.1.2", "FSR 3.1.6"), or null;
    /// whether it is FSR 3.1 (it takes the fsr3_* tuning).
    std::atomic<const char*> fsr_running{nullptr};
    std::atomic<bool> fsr_runs_31{false};
    /// Set by the renderer (0.4.4 beta 27c): the running FSR version is one AMD's driver provides.
    std::atomic<bool> fsr_running_driver{false};
    /// Set by DLSS at start (0.4.4 beta 27c): the version of the DLSS file it loads ("310.9.16"), or null.
    std::atomic<const char*> dlss_version{nullptr};
    /// DLSS's render preset (DlssPreset*); live: its feature is created again (vk_dlss.cpp). Set by
    /// DLSS: the preset its feature runs (0 the DLL's default, a refused one included), -1 before.
    std::atomic<int> dlss_preset{DlssPresetAuto}, dlss_applied_preset{-1};
    /// dlss_mip_bias (BB_DLSS_MIP_BIAS): one more mip level of texture detail under DLSS; live.
    std::atomic<bool> dlss_mip_bias{true};
    /// dlss_auto_exposure: DLSS's own exposure for any input (HDR inputs always use it); live.
    std::atomic<bool> dlss_auto_exposure{false};
    /// render_scale (BB_RENDER_SCALE): the Custom preset's output / render ratio; live as presets.
    std::atomic<float> render_scale{1.3f};
    /// mip_bias (BB_MIP_BIAS): texture LOD bias added while an upscaler runs (MipBiasMin..Max); live.
    std::atomic<float> mip_bias{0.0f};
    /// OptiScaler compatibility (vk_optiscaler.h), optiscaler_* in config.ini: the mode, NGX calls
    /// under the queue lock, the Vulkan instance and device made on a thread of their own, one
    /// submit thread (OptiSubmit). The menu edits these; the engine reads startup_optiscaler, and
    /// BB_OPTISCALER_* (the launcher's switch) override that.
    std::atomic<bool> optiscaler_compat{true}, optiscaler_ngx_lock{true}, optiscaler_clean_thread{true};
    std::atomic<int> optiscaler_sync_submit{OptiSubmitAuto};
    struct OptiScalerSwitches {
        bool compat = true, ngx_lock = true, clean_thread = true;
        int sync_submit = OptiSubmitAuto;
    } startup_optiscaler;

    /// Startup settings for the explicit BB_RENDER_RES compatibility patch only.
    int startup_preset = PresetAuto;
    float startup_render_scale = 1.3f;
    /// frame_gen at start: DLSS Frame Generation loads Streamline before the Vulkan instance.
    int startup_frame_gen = FrameGenOff;
    int startup_upscaler = UpscalerFsr;
    bool startup_object_motion = true;
    bool startup_effects[EffectCount]{};
    int startup_model_lod = 0;
    int startup_output_res = OutputDefault;
    int startup_live_resolution = 0;
    bool startup_hdr = false;
};

Values& Get();
/// The peak brightness HDR output uses, in nits: the display's (hdr_peak_auto) or hdr_peak.
inline float HdrPeakNits(const Values& v) {
    const float display = v.hdr_display_peak.load(std::memory_order_relaxed);
    const float peak = v.hdr_peak_auto.load(std::memory_order_relaxed) && display >= HdrPeakMin
                           ? display
                           : v.hdr_peak.load(std::memory_order_relaxed);
    return peak < HdrPeakMin ? HdrPeakMin : peak > HdrPeakMax ? HdrPeakMax : peak;
}
/// Localized overlay text; the Language selector itself stays in English. The other languages
/// look the English text up (bbport_menu_i18n.cpp) and show it when they have no translation.
const char* MenuText(const char* english, const char* russian);
/// Text without a Russian translation at its call site: English in Russian too.
inline const char* MenuText(const char* english) { return MenuText(english, english); }

/// Reads the file, then the environment overrides. Called once at start.
void Load();
/// Checks the loaded choice before the first frame: DLSS where it cannot run becomes AMD FSR, and AMD
/// FSR where it cannot run (its files, this GPU, Linux) TAA.
void ConfigureUpscalerSupport(bool fsr, bool dlss);
/// Startup-patched scene dimensions cannot change until run.sh prepares a new image.
bool FixedRenderSession();
/// 0.4.4 beta 26b: whether an output resolution chosen in game (the Insert menu, the pause menu's
/// Graphics page) applies at once: only with Live resolution changes on when the game started.
bool OutputChangesLive();
/// The preset in effect: Auto resolved for the output it applies to, NativeAA under TAA, the startup's
/// in a startup-patched session. Never PresetAuto.
int RenderPreset();
/// A preset with Auto resolved for an output index (OutputWidths); the others as they are.
int ResolvePreset(int preset, int output);
/// A resolution chosen in game that waits for a restart: in a startup-patched session, or with Live
/// resolution changes off in a session that needed no patch (started at 1920x1080).
bool ResolutionNeedsRestart();
/// Writes the file (menu changes).
void Save();

/// Render resolution divisor of a preset (1.0 native, 1.5 quality, ..., Custom: render_scale).
float PresetScale(int preset);
const char* PresetName(int preset);
/// The fixed preset nearest a preset's scale (Custom: its render_scale): the DLSS quality mode it
/// runs with.
int ModelPreset(int preset);
struct SceneSize {
    int width, height;
};
/// 0.4.4 beta 27b: the scene size a preset renders at, for the menus' Render resolution list (the
/// launcher's Choices.RenderSize works it out the same). Live: SceneResolution::ForScale of the output
/// it applies to (the ratio 1 to 3, multiples of 4). In a startup-patched session, the size the next
/// start makes for the output chosen: patched (patches.cpp ScaledSizes: never below 640x360, never
/// exactly 1920x1080) unless that start is live (1920x1080, or Live resolution changes on). Custom:
/// render_scale as set now.
SceneSize PresetRenderSize(int preset);
/// 0.4.4 beta 27c: the output the presets apply to (as PresetRenderSize), and the lowest render share
/// of it per side, in percent, the Custom slider offers there: a third, or what keeps 640x360 where the
/// start is patched (50 on the Steam Deck).
SceneSize PresetOutput();
int RenderPercentMin();
/// The newest of AMD's versions by number (the one Automatic runs), as AMD names it; empty for none.
std::string NewestFsrVersion(const std::vector<std::string>& offered);
/// dlss_preset letters offered: J, K, L, M (the transformers). NVIDIA removed A-D and deprecated E
/// and F (the CNN models; read as auto from 0.4.4 beta 25b); G-I, N, O fall back to its default.
bool DlssPresetValid(int preset);
const char* UpscalerName(int upscaler);
/// An upscaler's name in config.ini or BB_UPSCALER, older names included ("fsr3", "fsr4", "fsr411":
/// AMD FSR); -1 when unknown.
int UpscalerFromName(const char* name);

} // namespace BbSettings

// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_overlay.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#include <SDL3/SDL.h>
#include "bbport_menu_i18n.h"
#include "bbport_settings.h"
#include "bbport_usage.h"
#include "engine/cheats.h"
#include "engine/graphics.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_optiscaler.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

// DejaVu Sans (Latin with Latin Extended, Greek, Cyrillic), embedded (third_party/fonts, Bitstream
// Vera license).
#ifdef _WIN32
// PE/COFF assemblers have no .hidden/.previous: the compiler embeds the file (#embed, a GCC
// extension in C++).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
alignas(16) static const unsigned char bb_font_ttf[] = {
#embed BB_FONT_PATH
};
#pragma GCC diagnostic pop
static const unsigned char* const bb_font_ttf_end = bb_font_ttf + sizeof(bb_font_ttf);
#else
asm(".section .rodata\n"
    ".balign 16\n"
    ".hidden bb_font_ttf\n"
    ".global bb_font_ttf\n"
    "bb_font_ttf:\n"
    ".incbin \"" BB_FONT_PATH "\"\n"
    ".hidden bb_font_ttf_end\n"
    ".global bb_font_ttf_end\n"
    "bb_font_ttf_end:\n"
    ".previous\n");
extern "C" const unsigned char bb_font_ttf[];
extern "C" const unsigned char bb_font_ttf_end[];
#endif

// bbport: Lantern Menu SC (a Noto Sans SC subset: the Simplified Chinese menu's characters,
// third_party/fonts, SIL Open Font License 1.1), merged into DejaVu Sans at Init.
#ifdef _WIN32
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
alignas(16) static const unsigned char bb_font_cjk[] = {
#embed BB_CJK_FONT_PATH
};
#pragma GCC diagnostic pop
static const unsigned char* const bb_font_cjk_end = bb_font_cjk + sizeof(bb_font_cjk);
#else
asm(".section .rodata\n"
    ".balign 16\n"
    ".hidden bb_font_cjk\n"
    ".global bb_font_cjk\n"
    "bb_font_cjk:\n"
    ".incbin \"" BB_CJK_FONT_PATH "\"\n"
    ".hidden bb_font_cjk_end\n"
    ".global bb_font_cjk_end\n"
    "bb_font_cjk_end:\n"
    ".previous\n");
extern "C" const unsigned char bb_font_cjk[];
extern "C" const unsigned char bb_font_cjk_end[];
#endif

extern "C" void runtime_restart(void); // bb-probe (probe.c)
extern "C" void bbnet_exit_begin(int timeout_ms); // bbnet_glue.cpp: out of a co-op room at exit
extern "C" void bbnet_exit_wait(void);

namespace Vulkan {
extern std::atomic<u64> g_bb_compile_ns_total; // vk_pipeline_cache.cpp
extern std::atomic<u32> g_bb_compiles_total;
} // namespace Vulkan

namespace BbOverlay {

namespace {

std::mutex imgui_mutex; // the ImGui context: window thread (input) and present thread
bool initialized = false;
std::atomic<bool> menu_open{false};
bool l3_down = false, r3_down = false;
bool dirty = false; // settings changed while open: saved on close
float base_scale = 1.0f;

// The game's text dialog (ImeDialog, the character name), typed on the keyboard: drawn while it
// is open. In fullscreen the window title that showed it is not visible (issues #17, #19).
std::mutex prompt_mutex;
std::atomic<bool> prompt_active{false};
std::string prompt_title, prompt_text;

// Present rate for the FPS counter.
std::chrono::steady_clock::time_point last_present{};
float frame_ms_avg = 0.0f;

float PixelDensity(SDL_WindowID id);

void SetOpen(bool value) {
    if (menu_open.exchange(value) == value) {
        return;
    }
    // The system cursor shows over the menu (window.cpp); ImGui learns where it is now, not at
    // the next motion: mouse motion is not passed on while the menu is closed.
    if (value) {
        if (SDL_Window* window = SDL_GetMouseFocus()) {
            float x = 0.0f, y = 0.0f;
            SDL_GetMouseState(&x, &y);
            const float density = PixelDensity(SDL_GetWindowID(window));
            ImGui::GetIO().AddMousePosEvent(x * density, y * density);
        }
    }
    if (!value && dirty) {
        dirty = false;
        BbSettings::Save();
    }
}

ImGuiKey KeyFromSdl(SDL_Keycode key) {
    // Letters: Ctrl+F (search) and the text fields' Ctrl+A / C / V / X / Z.
    if (key >= SDLK_A && key <= SDLK_Z) {
        return ImGuiKey(ImGuiKey_A + int(key - SDLK_A));
    }
    switch (key) {
    case SDLK_TAB: return ImGuiKey_Tab;
    case SDLK_LEFT: return ImGuiKey_LeftArrow;
    case SDLK_RIGHT: return ImGuiKey_RightArrow;
    case SDLK_UP: return ImGuiKey_UpArrow;
    case SDLK_DOWN: return ImGuiKey_DownArrow;
    case SDLK_PAGEUP: return ImGuiKey_PageUp;
    case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
    case SDLK_HOME: return ImGuiKey_Home;
    case SDLK_END: return ImGuiKey_End;
    case SDLK_DELETE: return ImGuiKey_Delete;
    case SDLK_BACKSPACE: return ImGuiKey_Backspace;
    case SDLK_SPACE: return ImGuiKey_Space;
    case SDLK_RETURN: return ImGuiKey_Enter;
    case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDLK_ESCAPE: return ImGuiKey_Escape;
    case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
    case SDLK_RCTRL: return ImGuiKey_RightCtrl;
    case SDLK_LSHIFT: return ImGuiKey_LeftShift;
    case SDLK_RSHIFT: return ImGuiKey_RightShift;
    case SDLK_LALT: return ImGuiKey_LeftAlt;
    case SDLK_RALT: return ImGuiKey_RightAlt;
    default: return ImGuiKey_None;
    }
}

ImGuiKey KeyFromGamepad(u8 button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return ImGuiKey_GamepadFaceDown;
    case SDL_GAMEPAD_BUTTON_EAST: return ImGuiKey_GamepadFaceRight;
    case SDL_GAMEPAD_BUTTON_WEST: return ImGuiKey_GamepadFaceLeft;
    case SDL_GAMEPAD_BUTTON_NORTH: return ImGuiKey_GamepadFaceUp;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return ImGuiKey_GamepadDpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return ImGuiKey_GamepadDpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return ImGuiKey_GamepadDpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return ImGuiKey_GamepadDpadRight;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ImGuiKey_GamepadL1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ImGuiKey_GamepadR1;
    case SDL_GAMEPAD_BUTTON_START: return ImGuiKey_GamepadStart;
    case SDL_GAMEPAD_BUTTON_BACK: return ImGuiKey_GamepadBack;
    default: return ImGuiKey_None;
    }
}

float PixelDensity(SDL_WindowID id) {
    SDL_Window* window = SDL_GetWindowFromID(id);
    const float density = window ? SDL_GetWindowPixelDensity(window) : 1.0f;
    return density > 0.0f ? density : 1.0f;
}

// Marks the settings dirty when a widget changed them.
template <typename T>
void Store(std::atomic<T>& target, T value, bool changed) {
    if (changed) {
        target = value;
        dirty = true;
    }
}

// The widget runs before Store reads v: argument evaluation order is unspecified (clang on
// Windows copied v before the widget changed it, so clicks stored the old value).
void Checkbox(const char* label, std::atomic<bool>& value) {
    bool v = value;
    const bool changed = ImGui::Checkbox(label, &v);
    Store(value, v, changed);
}

bool Slider(const char* label, std::atomic<float>& value, float lo, float hi,
            const char* format = "%.2f", ImGuiSliderFlags flags = ImGuiSliderFlags_None) {
    float v = value;
    const bool changed = ImGui::SliderFloat(label, &v, lo, hi, format, flags);
    Store(value, v, changed);
    return changed;
}

// The amounts Custom starts from (the last one chosen in this session).
int custom_vials = 20, custom_bullets = 20;

// A cheat (engine/cheats.cpp): Off / Infinite / Custom, and the amount for Custom; live.
void Cheat(const char* label, std::atomic<int>& value, BbEngine::Cheats::Which which, int& custom) {
    ImGui::PushID(label);
    const char* modes[] = {BbSettings::MenuText("Off", "Выкл."),
                           BbSettings::MenuText("Infinite", "Бесконечно"),
                           BbSettings::MenuText("Custom", "Своё количество")};
    const int current = value;
    int mode = current == BbSettings::CheatOff ? 0 : current == BbSettings::CheatInfinite ? 1 : 2;
    if (current > 0) {
        custom = current;
    }
    if (ImGui::Combo(label, &mode, modes, 3)) {
        Store(value, mode == 0 ? BbSettings::CheatOff : mode == 1 ? BbSettings::CheatInfinite : custom, true);
    }
    if (mode == 2) {
        int amount = std::clamp(custom, 1, BbSettings::CheatMaxAmount);
        const bool changed = ImGui::SliderInt(BbSettings::MenuText("Amount", "Количество"), &amount, 1,
                                              BbSettings::CheatMaxAmount, "%d", ImGuiSliderFlags_AlwaysClamp);
        if (changed) {
            custom = std::clamp(amount, 1, BbSettings::CheatMaxAmount);
            Store(value, custom, true);
        }
    }
    if (const char* why = BbEngine::Cheats::Unavailable(which)) {
        ImGui::TextDisabled("%s: %s", BbSettings::MenuText("Unavailable", "Недоступно"), BbSettings::MenuText(why));
    }
    ImGui::PopID();
}

// The last "(?)" drawn: a hint right after another is not shown for it.
ImVec2 last_hint{-1.0f, -1.0f};

void Hint(const char* text) {
    // The setting it follows shows it too, when hovered or selected by the keyboard or a
    // controller ("(?)" itself is not a stop for their navigation).
    const ImVec2 setting_at = ImGui::GetItemRectMin();
    const bool setting = (setting_at.x != last_hint.x || setting_at.y != last_hint.y) &&
                         ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    last_hint = ImGui::GetItemRectMin();
    if (ImGui::BeginItemTooltip() || (setting && ImGui::BeginTooltip())) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void CheatUnavailable(BbEngine::Cheats::Which which) {
    if (const char* why = BbEngine::Cheats::Unavailable(which)) {
        ImGui::TextDisabled("%s: %s", BbSettings::MenuText("Unavailable", "Недоступно"), BbSettings::MenuText(why));
    }
}

// An on/off cheat (engine/cheats.cpp); live.
void CheatToggle(const char* label, std::atomic<bool>& value, BbEngine::Cheats::Which which, const char* hint) {
    ImGui::PushID(label);
    Checkbox(label, value);
    Hint(hint);
    CheatUnavailable(which);
    ImGui::PopID();
}

// A cheat with a few levels, the first the game's own; live.
template <std::size_t N>
void CheatLevels(const char* label, std::atomic<int>& value, const int (&levels)[N], const char* const (&names)[N],
                 BbEngine::Cheats::Which which, const char* hint) {
    ImGui::PushID(label);
    const int current = value;
    int index = 0;
    for (std::size_t i = 0; i < N; ++i) {
        if (levels[i] == current) {
            index = int(i);
        }
    }
    if (ImGui::Combo(label, &index, names, int(N))) {
        Store(value, levels[index], true);
    }
    Hint(hint);
    CheatUnavailable(which);
    ImGui::PopID();
}

// Adds Blood Echoes / Insight to the character in the world (applied by the engine's frame hook).
void CheatStats() {
    const bool off = BbEngine::Cheats::Unavailable(BbEngine::Cheats::Echoes) != nullptr;
    ImGui::BeginDisabled(off);
    ImGui::TextUnformatted(BbSettings::MenuText("Blood Echoes", "Отголоски крови"));
    for (int amount : {1000, 10000, 100000}) {
        char label[32];
        std::snprintf(label, sizeof(label), "+%d,%03d", amount / 1000, 0);
        ImGui::SameLine();
        if (ImGui::Button(label)) {
            BbEngine::Cheats::AddEchoes(amount);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(BbSettings::MenuText("+1 Insight", "+1 озарение"))) {
        BbEngine::Cheats::AddInsight(1);
    }
    ImGui::EndDisabled();
    Hint(BbSettings::MenuText(
        "Added to the character in the world, up to the game's limits (999,999,999 Blood Echoes, 99 Insight). "
        "They are saved with your character: turning cheats off does not take them back.",
        "Добавляются персонажу в игре, до пределов игры (999 999 999 отголосков, 99 озарения). Они "
        "сохраняются вместе с персонажем: выключение читов их не отнимает."));
    CheatUnavailable(BbEngine::Cheats::Echoes);
}

// ---------------------------------------------------------------- search, tabs and sections

// The search box above the tabs: its text, and the query folded to lower case (empty: not
// searching). While searching, every tab's settings whose label holds the query in any of the
// menu's languages show in one list, each under its tab and section; a section whose title
// matches shows whole.
char search_text[96] = "";
std::string search_query;
const char* tab_title = "";          // the tab being drawn (section titles while searching)
const char* section_title = nullptr; // the section being drawn, until its title is drawn
bool section_matches = false;
int shown_items = 0; // settings drawn this frame (the "nothing matches" line)

/// Appends `c` to `out` as UTF-8, folded to lower case for the menu's scripts (Latin with its
/// Latin-1 and Extended-A letters, Greek, Cyrillic); other characters as they are.
void AppendFolded(std::string& out, char32_t c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) ||
        (c >= 0x391 && c <= 0x3AB && c != 0x3A2) || (c >= 0x410 && c <= 0x42F)) {
        c += 32;
    } else if (c >= 0x400 && c <= 0x40F) {
        c += 80;
    } else if (c == 0x130 || c == 0x131) {
        c = 'i'; // Turkish İ and ı find i
    } else if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) {
        c |= 1; // Latin Extended-A pairs: capital even, small odd
    } else if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) {
        c += c & 1; // here capital odd, small even
    } else if (c == 0x178) {
        c = 0xFF;
    }
    if (c < 0x80) {
        out += char(c);
    } else if (c < 0x800) {
        out += char(0xC0 | (c >> 6));
        out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += char(0xE0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    } else {
        out += char(0xF0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 0x3F));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    }
}

/// `text` folded to lower case, up to an ImGui ID ("##..."), without its outer spaces.
std::string Fold(const char* text) {
    std::string out;
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    while (*p && !(p[0] == '#' && p[1] == '#')) {
        char32_t c = *p++;
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        c &= extra == 3 ? 0x07 : extra == 2 ? 0x0F : extra == 1 ? 0x1F : 0x7F;
        for (; extra > 0 && (*p & 0xC0) == 0x80; --extra) {
            c = (c << 6) | (*p++ & 0x3F);
        }
        AppendFolded(out, c);
    }
    const auto first = out.find_first_not_of(' ');
    const auto last = out.find_last_not_of(' ');
    return first == std::string::npos ? std::string{} : out.substr(first, last - first + 1);
}

bool Holds(const char* text) {
    return text && Fold(text).find(search_query) != std::string::npos;
}

/// The label, by its English and Russian text as the call site passes them to MenuText, holds the
/// query in one of the menu's languages.
bool Matches(const char* english, const char* russian) {
    if (Holds(english) || (russian && Holds(russian))) {
        return true;
    }
    for (int l = 0; l < BbSettings::MenuLanguageCount; ++l) {
        const char* text = BbSettings::MenuTranslation(BbSettings::MenuLanguage(l), english);
        if (text != english && Holds(text)) {
            return true;
        }
    }
    return false;
}

bool Searching() {
    return !search_query.empty();
}

/// Starts a section; its title is drawn with its first setting that shows.
void Section(const char* english, const char* russian = nullptr) {
    section_title = russian ? BbSettings::MenuText(english, russian) : BbSettings::MenuText(english);
    section_matches = Searching() && Matches(english, russian);
}

/// Everything in the section shows: not searching, or its title matches.
bool WholeSection() {
    return !Searching() || section_matches;
}

/// A setting (or a line of the section) by its label: true when it shows, after the section's
/// title if it is the first.
bool Shown(const char* english, const char* russian = nullptr) {
    if (!WholeSection() && !Matches(english, russian)) {
        return false;
    }
    if (section_title) {
        if (Searching()) {
            const std::string title = std::string(tab_title) + "  >  " + section_title;
            ImGui::SeparatorText(title.c_str());
        } else {
            ImGui::SeparatorText(section_title);
        }
        section_title = nullptr;
    }
    ++shown_items;
    return true;
}

/// The setting's label in the menu's language when it shows, else null.
const char* Item(const char* english, const char* russian = nullptr) {
    if (!Shown(english, russian)) {
        return nullptr;
    }
    return russian ? BbSettings::MenuText(english, russian) : BbSettings::MenuText(english);
}

// HDR output (config.ini hdr*): on/off on restart; paper white, peak and boost live.
void HdrSection() {
    auto& s = BbSettings::Get();
    Section("HDR output", "Вывод HDR");
    if (const char* label = Item("HDR output (restart required)", "Вывод HDR (после перезапуска)")) {
        Checkbox(label, s.hdr);
        Hint(BbSettings::MenuText(
            "Shows the game's SDR picture on an HDR display without washed-out colors, with optional "
            "highlight expansion. Windows HDR must be on (Settings > Display > Use HDR). Turning it "
            "on or off applies after restarting the game.",
            "Показывает SDR-картинку игры на HDR-дисплее без блёклых цветов, с необязательным "
            "расширением светлых участков. В Windows должен быть включён HDR (Параметры > Дисплей > "
            "Использовать HDR). Включение и выключение применяются после перезапуска игры."));
    }
    const int output = s.hdr_output.load();
    if (output == BbSettings::HdrOutputOff) {
        if (s.startup_hdr && WholeSection() && Shown("HDR output", "Вывод HDR")) {
            ImGui::PushTextWrapPos();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s",
                               BbSettings::MenuText(
                                   "Not active: the display offers no HDR format (is Windows HDR on?)",
                                   "Не активен: дисплей не предлагает HDR-формат (включён ли HDR "
                                   "в Windows?)"));
            ImGui::PopTextWrapPos();
        }
        return;
    }
    if (WholeSection() && Shown("HDR output", "Вывод HDR")) {
        ImGui::TextDisabled("%s", output == BbSettings::HdrOutputScRgb
                                      ? BbSettings::MenuText("Active: scRGB (FP16)", "Активен: scRGB (FP16)")
                                      : BbSettings::MenuText("Active: HDR10 (PQ, BT.2020)",
                                                             "Активен: HDR10 (PQ, BT.2020)"));
    }
    // bbport: hdr_mode, live (docs/hdr.md).
    int mode = std::clamp(s.hdr_mode.load(), 0, BbSettings::HdrModeCount - 1);
    if (const char* label = Item("HDR source", "Источник HDR")) {
        const char* modes[BbSettings::HdrModeCount] = {
            BbSettings::MenuText("Native (from the game's HDR scene)", "Нативный (из HDR-сцены игры)"),
            BbSettings::MenuText("Expand (from the SDR picture)", "Расширение (из SDR-картинки)"),
        };
        const bool mode_changed = ImGui::Combo(label, &mode, modes, BbSettings::HdrModeCount);
        Store(s.hdr_mode, mode, mode_changed);
        Hint(BbSettings::MenuText(
            "Native: the brightness of lamps, fire, the moon and other highlights comes from the "
            "game's own HDR scene, before its tonemap clipped it to SDR white. Midtones, colours and "
            "the UI stay as in the SDR picture. Menus, loading screens and movies (no scene) use "
            "Expand. Expand: the SDR picture's brightest parts are stretched towards the peak.",
            "Нативный: яркость ламп, огня, луны и других светлых мест берётся из собственной "
            "HDR-сцены игры, до того как тонмаппинг обрезал её до белого SDR. Средние тона, цвета и "
            "интерфейс остаются как в SDR-картинке. В меню, на загрузках и в видеороликах (без сцены) "
            "работает «Расширение». Расширение: самые яркие места SDR-картинки растягиваются к пику."));
    }
    if (const char* label = Item("Paper white (nits)", "Белый SDR (нит)")) {
        Slider(label, s.hdr_paper_white, BbSettings::HdrPaperWhiteMin, BbSettings::HdrPaperWhiteMax, "%.0f",
               ImGuiSliderFlags_AlwaysClamp);
    }
    // bbport: the peak the display reports (window.cpp UpdateDisplayPeak), else the slider's.
    const float display_peak = s.hdr_display_peak.load();
    const bool from_display = s.hdr_peak_auto.load() && display_peak >= BbSettings::HdrPeakMin;
    if (const char* label = Item("Peak brightness from the display", "Пиковая яркость от дисплея")) {
        Checkbox(label, s.hdr_peak_auto);
        if (from_display) {
            char reported[96];
            std::snprintf(reported, sizeof(reported), "%s: %.0f",
                          BbSettings::MenuText("The display reports (nits)", "Дисплей сообщает (нит)"),
                          display_peak);
            ImGui::TextDisabled("%s", reported);
        } else if (s.hdr_peak_auto.load()) {
            ImGui::TextDisabled("%s", BbSettings::MenuText("The display reports no peak: the value below is used",
                                                           "Дисплей не сообщает пик: используется значение ниже"));
        }
    }
    if (const char* label = Item("Peak brightness (nits)", "Пиковая яркость (нит)")) {
        ImGui::BeginDisabled(from_display);
        Slider(label, s.hdr_peak, BbSettings::HdrPeakMin, BbSettings::HdrPeakMax, "%.0f",
               ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
        ImGui::EndDisabled();
    }
    if (const char* label = Item("Highlight boost", "Усиление светлых участков")) {
        Slider(label, s.hdr_boost, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        Hint(BbSettings::MenuText(
            "Paper white: how bright the game's white (and this menu) is; about 200 suits a dim room. "
            "Peak: your display's peak brightness. From the display: the value Windows reports for it "
            "(the Windows HDR Calibration app's once it has run); turn it off to set it yourself. "
            "Highlight boost: 0 shows the SDR picture as it is; higher values let the brightest parts "
            "(lamps, fire, the moon) rise towards the peak. In Native mode it is how much of the "
            "scene's own highlight range is shown: 0.25 about half, 1 all of it (up to the peak). "
            "Changes apply live. Ctrl+click a slider to type an exact value.",
            "Белый SDR: яркость белого в игре (и этого меню); около 200 подходит для неяркой комнаты. "
            "Пиковая яркость: максимум вашего дисплея. «От дисплея» — значение, которое сообщает Windows "
            "(после приложения «Калибровка HDR» — его значение); выключите, чтобы задать вручную. "
            "Усиление светлых участков: 0 — SDR-картинка как есть; больше — "
            "самые яркие места (лампы, огонь, луна) поднимаются к пику. В нативном режиме — какая "
            "доля собственного диапазона светлых мест сцены показывается: 0.25 — около половины, "
            "1 — весь (до пика). Меняются сразу. Ctrl+клик по ползунку — ввести точное значение."));
    }
    if (mode == BbSettings::HdrModeNative) {
        if (const char* label = Item("UI brightness", "Яркость интерфейса")) {
            Slider(label, s.hdr_ui_brightness, BbSettings::HdrUiBrightnessMin, BbSettings::HdrUiBrightnessMax,
                   "%.2f", ImGuiSliderFlags_AlwaysClamp);
        }
        if (const char* label = Item("UI colour", "Цвета интерфейса")) {
            Slider(label, s.hdr_ui_color, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            Hint(BbSettings::MenuText(
                "Native mode only: the game's HUD, menus, subtitles and loading screens. UI brightness: their "
                "white, times paper white. UI colour: 0 shows their exact sRGB colours, 1 the wider colours a "
                "wide-gamut monitor shows SDR with (the usual look; exact sRGB can look washed out next to "
                "it). Live.",
                "Только в нативном режиме: интерфейс игры, меню, субтитры и экраны загрузки. Яркость "
                "интерфейса: его белый, умноженный на белый SDR. Цвета интерфейса: 0 — точные цвета sRGB, "
                "1 — более насыщенные, как на мониторе с широким охватом в SDR (привычный вид; точный sRGB "
                "рядом с ним может казаться блёклым). Меняются сразу."));
        }
    }
    // bbport: not saved; the encode shows the left half as SDR (hdr_output.frag FLAG_COMPARE).
    if (const char* label = Item("Compare: SDR | HDR", "Сравнение: SDR | HDR")) {
        Checkbox(label, s.hdr_compare);
        Hint(BbSettings::MenuText(
            "The left half of the screen as an SDR display shows the game (at paper white), the right "
            "half in HDR, to judge the settings above. Not saved.",
            "Левая половина экрана — как игру показывает SDR-дисплей (при белом SDR), правая — в HDR, "
            "чтобы оценить настройки выше. Не сохраняется."));
    }
}

// The game's own picture for each area (engine/graphics.cpp), live: Picture tuning too.
void PictureSection() {
    auto& s = BbSettings::Get();
    Section("Picture", "Изображение");
    const bool live = BbEngine::Graphics::Enabled();
    if (!live && WholeSection() && Shown("Picture", "Изображение")) {
        ImGui::TextDisabled("%s", BbSettings::MenuText("Unavailable: the game's code differs from version 1.09.",
                                                       "Недоступно: код игры отличается от версии 1.09."));
    }
    ImGui::BeginDisabled(!live);
    if (const char* label = Item("Picture tuning", "Настройка изображения")) {
        Checkbox(label, s.graphics_controls);
        Hint(BbSettings::MenuText(
            "Changes apply live, Picture tuning too, to the game's own colour, fog and lighting of each area "
            "(the HUD and menus keep theirs). Moving one turns Picture tuning on; off: the game's own picture. 1.00 and Hue 0 are the game's. Fog 0 clears the haze over distant streets. The "
            "same settings are in the game's System menu (Graphics, Effects, Colour) and the "
            "launcher. Ctrl+click a slider to type any value, beyond its ends too (brightness, saturation and fog "
            "up to 5, contrast 0.5-1.5, hue -180-180, bloom up to 10, shadow distance 0.25-8).",
            "Меняются сразу, как и сама настройка: собственные цвет, туман и освещение каждой области "
            "(интерфейс и меню не меняются). Выкл. — картинка игры. 1.00 и оттенок 0 — как в игре. Туман 0 убирает дымку над дальними улицами. Те же "
            "настройки есть в системном меню игры (Graphics, Effects, Colour) и в лаунчере. "
            "Ctrl+клик по ползунку — ввести любое значение, в том числе за его краями (яркость, "
            "насыщенность и туман до 5, контраст 0.5-1.5, оттенок -180-180, bloom до 10, дальность теней "
            "0.25-8)."));
    }
    // A moved setting turns Picture tuning on: it gates them all.
    const bool vignette = s.graphics_vignette;
    // The useful range on the slider; Ctrl+click types any value (bbport_settings.cpp keeps it
    // within wide limits).
    bool moved = false;
    const auto slider = [&](const char* english, const char* russian, std::atomic<float>& value, float lo,
                            float hi, const char* format = "%.2f") {
        if (const char* label = Item(english, russian)) {
            moved |= Slider(label, value, lo, hi, format);
        }
    };
    slider("Brightness", "Яркость", s.graphics_brightness, 0.0f, 3.0f);
    slider("Contrast", "Контраст", s.graphics_contrast, 0.8f, 1.2f, "%.3f");
    slider("Saturation", "Насыщенность", s.graphics_saturation, 0.0f, 3.0f);
    slider("Hue", "Оттенок", s.graphics_hue, -180.0f, 180.0f, "%.0f");
    slider("Fog", "Туман", s.graphics_fog, 0.0f, 3.0f);
    slider("Bloom", nullptr, s.graphics_bloom, 0.0f, 3.0f);
    if (const char* label = Item("Vignette", "Виньетка")) {
        Checkbox(label, s.graphics_vignette);
        moved |= vignette != s.graphics_vignette;
    }
    slider("Ambient occlusion strength", "Сила затенения (AO)", s.graphics_ao_strength, 0.0f, 3.0f);
    slider("Shadow distance", "Дальность теней", s.graphics_shadow_scale, 0.5f, 4.0f);
    if (moved && !s.graphics_controls) {
        Store(s.graphics_controls, true, true);
    }
    if (const char* label = Item("Game's own picture", "Как в игре")) {
        if (ImGui::Button(label)) {
            Store(s.graphics_brightness, 1.0f, true);
            Store(s.graphics_contrast, 1.0f, true);
            Store(s.graphics_saturation, 1.0f, true);
            Store(s.graphics_hue, 0.0f, true);
            Store(s.graphics_fog, 1.0f, true);
            Store(s.graphics_bloom, 1.0f, true);
            Store(s.graphics_vignette, true, true);
            Store(s.graphics_ao_strength, 1.0f, true);
            Store(s.graphics_shadow_scale, 1.0f, true);
        }
    }
    ImGui::EndDisabled();
}

// Game effects switched by patches at start (restart required), and the model detail.
void EffectsSection() {
    auto& s = BbSettings::Get();
    Section("Game effects (restart required)", "Эффекты игры (после перезапуска)");
    if (const char* label = Item("Model detail", "Детализация моделей")) {
        const char* lods[] = {BbSettings::MenuText("Highest (-2)", "Максимальная (-2)"),
                              BbSettings::MenuText("Game default", "Как в игре"),
                              BbSettings::MenuText("Lower (1)", "Ниже (1)"),
                              BbSettings::MenuText("Lowest (2)", "Минимальная (2)")};
        static constexpr int lod_values[] = {-2, 0, 1, 2};
        int lod_index = 1;
        for (int i = 0; i < 4; ++i) {
            if (lod_values[i] == s.model_lod) lod_index = i;
        }
        if (ImGui::BeginCombo(label, lods[lod_index])) {
            for (int i = 0; i < 4; ++i) {
                if (ImGui::Selectable(lods[i], i == lod_index)) {
                    Store(s.model_lod, lod_values[i], true);
                }
            }
            ImGui::EndCombo();
        }
    }
    for (int e = 0; e < BbSettings::EffectCount; ++e) {
        // 0.4.4 beta 27b: the game's own anti-aliasing is in Upscaling › Anti-aliasing.
        if (e == BbSettings::EffectGameAa) {
            continue;
        }
        const auto& effect = BbSettings::Effects[e];
        const char* label = Item(effect.label, effect.label_ru);
        if (!label) {
            continue;
        }
        Checkbox(label, s.effects[e]);
        if (std::strcmp(effect.key, "effect_dynamic_shadows") == 0) {
            Hint(BbSettings::MenuText(
                "Effects are enabled and disabled by game patches at startup (patches/Bloodborne.xml). "
                "Motion blur and shadows from dynamic lights place a significant load on the GPU.",
                "Эффекты включаются и выключаются патчами игры при запуске (patches/Bloodborne.xml). "
                "Размытие в движении и тени от динамических источников заметно нагружают GPU."));
        } else if (std::strcmp(effect.key, "debug_menu") == 0) {
            Hint(BbSettings::MenuText(
                "Free camera: hold Cross and press L3 (keyboard: Space + Z). "
                "Debug menu: left touchpad / Tab. Requires DbgFont14h.ccm and DbgFont14h.tpf "
                "in dvdroot_ps4/font from Nexus mod #253. Right touchpad: Backspace.",
                "Свободная камера: удерживайте Cross и нажимайте L3 (клавиатура: Space + Z). "
                "Debug menu: левый touchpad / Tab. Нужны DbgFont14h.ccm и DbgFont14h.tpf "
                "в dvdroot_ps4/font из мода Nexus #253. Правый touchpad: Backspace."));
        }
    }
}

// 0.4.4 beta 27c: the upscaler's quality (Auto first: by the output's height, BbSettings::AutoPreset)
// and under it the render resolution: a slider showing the share of the output the scene renders at
// (the engine's sizes, PresetRenderSize), which only Custom moves. As the launcher's Graphics page.
void QualityRows() {
    auto& s = BbSettings::Get();
    const bool dlss = s.upscaler == BbSettings::UpscalerDlss;
    const auto name = [&](int preset) {
        return preset == BbSettings::NativeAA && dlss ? "DLAA" : BbSettings::MenuText(BbSettings::PresetName(preset));
    };
    const BbSettings::SceneSize out = BbSettings::PresetOutput();
    // Auto with what it picks for an output this tall: "Auto (Quality)".
    const auto describe = [&](int preset, int height, char* text, std::size_t size) {
        if (preset == BbSettings::PresetAuto) {
            std::snprintf(text, size, "%s (%s)", BbSettings::MenuText("Auto"), name(BbSettings::AutoPreset(height)));
        } else {
            std::snprintf(text, size, "%s", name(preset));
        }
    };
    const int saved = std::clamp(s.preset.load(), 0, BbSettings::PresetCount - 1);
    if (const char* label = Item("Quality", "Качество")) {
        static constexpr int order[] = {BbSettings::PresetAuto,   BbSettings::NativeAA,    BbSettings::Quality,
                                        BbSettings::Balanced,     BbSettings::Performance, BbSettings::UltraPerformance,
                                        BbSettings::PresetCustom};
        char shown[64];
        describe(saved, out.height, shown, sizeof(shown));
        if (ImGui::BeginCombo(label, shown)) {
            for (const int preset : order) {
                char choice[64];
                describe(preset, out.height, choice, sizeof(choice));
                if (ImGui::Selectable(choice, preset == saved)) {
                    Store(s.preset, preset, true);
                }
            }
            ImGui::EndCombo();
        }
        Hint(BbSettings::MenuText(
            "How far below the output the game renders: lower is faster and softer. Auto: Native AA up "
            "to 1200 lines, Quality up to 1600, Performance above.",
            "Насколько ниже вывода рендерит игра: ниже — быстрее и мягче. Авто: Native AA до 1200 строк, "
            "Quality до 1600, выше — Performance."));
    }
    ImGui::Indent();
    if (const char* label = Item("Render resolution", "Разрешение рендера")) {
        const bool custom = saved == BbSettings::PresetCustom;
        const auto scene = BbSettings::PresetRenderSize(saved);
        int percent = out.width > 0 ? int(std::lround(100.0 * scene.width / out.width)) : 100;
        char format[64];
        std::snprintf(format, sizeof(format), "%%d%%%% — %d x %d", scene.width, scene.height);
        ImGui::BeginDisabled(!custom);
        if (ImGui::SliderInt(label, &percent, BbSettings::RenderPercentMin(), 100, format,
                             ImGuiSliderFlags_AlwaysClamp) &&
            custom) {
            Store(s.render_scale,
                  std::clamp(100.0f / float(std::max(percent, 1)), BbSettings::RenderScaleMin, BbSettings::RenderScaleMax),
                  true);
        }
        ImGui::EndDisabled();
        Hint(custom ? BbSettings::MenuText(
                          "The share of the output's width and height the game renders at. DLSS runs the "
                          "nearest quality's model. Ctrl+click to type a value.",
                          "Доля ширины и высоты вывода, в которой рендерит игра. DLSS берёт модель "
                          "ближайшего качества. Ctrl+клик — ввести значение.")
                    : BbSettings::MenuText("Choose Custom to set your own.", "Выберите Custom, чтобы задать своё."));
    }
    ImGui::Text(BbSettings::MenuText("Active scene render: %d x %d"), s.active_render_width.load(),
                s.active_render_height.load());
    if (BbSettings::FixedRenderSession()) {
        char startup[64];
        describe(std::clamp(s.startup_preset, 0, BbSettings::PresetCount - 1),
                 BbSettings::OutputHeight(s.startup_output_res), startup, sizeof(startup));
        ImGui::Text(BbSettings::MenuText("Quality at startup: %s", "Качество при запуске: %s"), startup);
        if (const char* automatic = std::getenv("BB_AUTO_RENDER_RES"); automatic && automatic[0] == '1') {
            Hint(BbSettings::MenuText(
                "Patched at startup for this output: quality and output changes apply after a restart, or "
                "at once with Live resolution changes on (slower).",
                "Задано при запуске для этого вывода: смена качества и вывода — после перезапуска, или "
                "сразу с «Живой сменой разрешения» (медленнее)."));
        } else {
            Hint(BbSettings::MenuText(
                "BB_RENDER_RES fixes the scene size at startup. Remove this explicit variable "
                "to change resolution and presets without restarting the game."));
        }
    }
    ImGui::Unindent();
}

// 0.4.4 beta 27c: AMD FSR's version: Automatic (the newest AMD runs here) or any AMD's SDK lists for this
// GPU (fsr_version by name, BbSettings::FsrVersionMatch); live, the context made again.
void FsrVersionRow() {
    auto& s = BbSettings::Get();
    const char* label = Item("FSR version", "Версия FSR");
    if (!label) {
        return;
    }
    const auto offered = BbSettings::FsrVersionsOffered();
    const std::string wanted = BbSettings::FsrVersion();
    const int match = BbSettings::FsrVersionMatch(offered, wanted);
    const auto shown_name = [](const std::string& name) {
        std::string text = "FSR " + BbSettings::FsrVersionKey(name);
        if (name.find('*') != std::string::npos) {
            text += std::string(" ") + BbSettings::MenuText("(driver)", "(драйвер)");
        }
        return text;
    };
    std::string automatic = BbSettings::MenuText("Automatic", "Автоматически");
    if (const std::string newest = BbSettings::NewestFsrVersion(offered); !newest.empty()) {
        automatic += " (" + shown_name(newest) + ")";
    }
    const std::string preview = match >= 0 ? shown_name(offered[size_t(match)]) : automatic;
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable(automatic.c_str(), match < 0)) {
            BbSettings::SetFsrVersion("auto");
            dirty = true;
        }
        for (int i = 0; i < int(offered.size()); ++i) {
            const std::string text = shown_name(offered[size_t(i)]);
            if (ImGui::Selectable(text.c_str(), i == match)) {
                BbSettings::SetFsrVersion(BbSettings::FsrVersionKey(offered[size_t(i)]));
                dirty = true;
            }
        }
        ImGui::EndCombo();
    }
    Hint(BbSettings::MenuText(
        "The versions AMD's FSR SDK offers on this GPU; Automatic runs the newest. A change builds the "
        "upscaler again (a few seconds at full size, without it).",
        "Версии, которые FSR SDK от AMD предлагает для этой видеокарты; «Автоматически» — новейшая. "
        "Смена пересобирает апскейлер (несколько секунд в полном разрешении без него)."));
    if (const char* running = s.fsr_running.load()) {
        ImGui::TextDisabled(s.fsr_running_driver.load()
                                ? BbSettings::MenuText("Running: %s (from AMD's driver)", "Работает: %s (из драйвера AMD)")
                                : BbSettings::MenuText("Running: %s", "Работает: %s"),
                            running);
    }
    if (match < 0 && wanted != "auto" && !offered.empty()) {
        ImGui::TextDisabled(BbSettings::MenuText("FSR %s is not offered on this GPU: Automatic runs",
                                                 "FSR %s нет для этой видеокарты: работает автоматический выбор"),
                            wanted.c_str());
    }
}

// 0.4.4 beta 27c: DLSS's model, under the upscaler (was the DLSS section's first row); live.
void DlssModelRow() {
    auto& s = BbSettings::Get();
    if (const char* label = Item("DLSS model", "Модель DLSS")) {
        // The letters NVIDIA's DLSS 4 and 4.5 DLLs know; a preset the DLL refuses runs its default.
        // 0.4.4 beta 25b: E and F (CNN) left out: NVIDIA deprecated them (DLSS 310.6.0).
        static constexpr int presets[] = {BbSettings::DlssPresetAuto, 11, 10, 13, 12,
                                          BbSettings::DlssPresetDefault};
        const char* names[] = {
            BbSettings::MenuText("Auto: K, M for Performance, L for Ultra Performance",
                                 "Авто: K, M для Performance, L для Ultra Performance"),
            BbSettings::MenuText("K: transformer", "K: трансформер"),
            BbSettings::MenuText("J: transformer", "J: трансформер"),
            BbSettings::MenuText("M: transformer, 2nd generation", "M: трансформер 2-го поколения"),
            BbSettings::MenuText("L: transformer, 2nd generation (Ultra Performance)",
                                 "L: трансформер 2-го поколения (Ultra Performance)"),
            BbSettings::MenuText("The DLL's default", "По умолчанию DLL"),
        };
        static_assert(std::size(presets) == std::size(names));
        const int current = s.dlss_preset;
        int index = -1;
        for (int i = 0; i < int(std::size(presets)); ++i) {
            if (presets[i] == current) index = i;
        }
        char other[48];
        std::snprintf(other, sizeof(other), "%s %c", BbSettings::MenuText("Preset", "Пресет"),
                      char('A' + std::clamp(current, 1, BbSettings::DlssPresetMax) - 1));
        if (ImGui::BeginCombo(label, index >= 0 ? names[index] : other)) {
            for (int i = 0; i < int(std::size(presets)); ++i) {
                if (ImGui::Selectable(names[i], i == index)) {
                    Store(s.dlss_preset, presets[i], true);
                }
            }
            ImGui::EndCombo();
        }
        Hint(BbSettings::MenuText(
            "Which NVIDIA model DLSS runs; it changes at once (a brief pause). Auto, the default, is "
            "NVIDIA's pairing for each quality. M and L need DLSS 310.5 or newer; a model the DLL refuses "
            "runs its default.",
            "Какую модель NVIDIA запускает DLSS; меняется сразу (короткая пауза). Авто, по умолчанию, — "
            "выбор NVIDIA для каждого качества. Для M и L нужен DLSS 310.5 или новее; модель, которую DLL "
            "не принимает, заменяется её моделью по умолчанию."));
        // 0.4.4 beta 27c: with the DLSS file's version (BbSettings::dlss_version).
        const int applied = s.dlss_applied_preset;
        const char* version = s.dlss_version.load();
        if (applied > 0 && version) {
            ImGui::TextDisabled(BbSettings::MenuText("Running: DLSS %s, model %c", "Работает: DLSS %s, модель %c"),
                                version, char('A' + applied - 1));
        } else if (applied > 0) {
            ImGui::TextDisabled(BbSettings::MenuText("Running: preset %c", "Работает: пресет %c"),
                                char('A' + applied - 1));
        } else if (applied == 0 && version) {
            ImGui::TextDisabled(BbSettings::MenuText("Running: DLSS %s, the DLL's default model",
                                                     "Работает: DLSS %s, модель DLL по умолчанию"),
                                version);
        } else if (applied == 0) {
            ImGui::TextDisabled("%s", BbSettings::MenuText("Running: the DLL's default preset",
                                                           "Работает: пресет DLL по умолчанию"));
        }
        if (applied == 0 && current != BbSettings::DlssPresetDefault) {
            ImGui::PushTextWrapPos();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                               BbSettings::MenuText("This DLSS version refuses preset %c: its default runs",
                                                    "Эта версия DLSS не принимает пресет %c: работает её пресет"),
                               char('A' + std::clamp(current, 1, BbSettings::DlssPresetMax) - 1));
            ImGui::PopTextWrapPos();
        }
    }
}

// The anti-aliasing, the upscaler with its quality, version or model, sharpening, and the fine settings
// (Advanced).
void UpscalerSection() {
    auto& s = BbSettings::Get();
    Section("Temporal upscaler", "Временной апскейлер");
    const bool upscaling = BbSettings::IsFrameUpscaler(s.upscaler);
    // 0.4.4 beta 27b: the anti-aliasing without an upscaler, as the launcher's (27c: first, as there): TAA (live), the game's
    // own pass (a patch at start) or none. An upscaler anti-aliases itself; the game's pass is left out
    // under it (patches.cpp NoUpscaler).
    if (const char* label = Item("Anti-aliasing", "Сглаживание")) {
        // 0.4.4 beta 28 (the tester): the game's own first, then TAA and Off.
        const char* modes[] = {BbSettings::MenuText("The game's own", "Собственное игры"), "TAA",
                               BbSettings::MenuText("Off", "Выкл")};
        const bool game_aa = s.effects[BbSettings::EffectGameAa];
        const int mode = s.upscaler == BbSettings::UpscalerOff ? (game_aa ? 0 : 2)
                         : s.upscaler == BbSettings::UpscalerTaa ? 1
                                                                 : (game_aa ? 0 : 1); // what None goes back to
        int picked = mode;
        // 0.4.4 beta 27d (the tester): with an upscaler the box names it, greyed, as the launcher's; the
        // choice is kept for Upscaler None.
        char instead[64] = {};
        if (upscaling) {
            std::snprintf(instead, sizeof(instead), BbSettings::MenuText("Upscaler (%s)", "Апскейлер (%s)"),
                          s.upscaler == BbSettings::UpscalerDlss ? "DLSS" : "AMD FSR");
        }
        ImGui::BeginDisabled(upscaling);
        if (ImGui::BeginCombo(label, upscaling ? instead : modes[mode])) {
            for (int i = 0; i < int(std::size(modes)); ++i) {
                if (ImGui::Selectable(modes[i], i == mode)) {
                    picked = i;
                }
            }
            ImGui::EndCombo();
        }
        if (picked != mode) {
            Store(s.upscaler, picked == 1 ? int(BbSettings::UpscalerTaa) : int(BbSettings::UpscalerOff), true);
            Store(s.effects[BbSettings::EffectGameAa], picked == 0, true);
        }
        ImGui::EndDisabled();
        if (!upscaling) {
            Hint(BbSettings::MenuText(
                "The game's own: its anti-aliasing pass, as on the PS4, after a restart. TAA: "
                "smooth edges at the output resolution, live (sharpening helps). Off: the sharpest "
                "picture, with jagged edges.",
                "Собственное игры: её проход сглаживания, как на PS4, после перезапуска. TAA: "
                "гладкие края в разрешении вывода, сразу (поможет резкость). Выкл: самая резкая "
                "картинка, с лесенками."));
        }
    }
    if (const char* label = Item("Upscaler", "Апскейлер")) {
        // 0.4.4 beta 27: one FSR, AMD's (AMD's FSR SDK picks the version); FSR 3.1 (FireBurn's), FSR 4
        // and FSR 4.1.1 went. 27b: the upscalers or None, as the launcher's Graphics page; with None,
        // Anti-aliasing below decides (TAA, the game's own pass or none).
        const char* upscalers[] = {"AMD FSR", "DLSS (NVIDIA)", BbSettings::MenuText("None", "Нет")};
        const int kinds[] = {BbSettings::UpscalerFsr, BbSettings::UpscalerDlss, BbSettings::UpscalerOff};
        static const char* later[] = {"XeSS"};
        const int upscaler = s.upscaler;
        const int shown = upscaler == BbSettings::UpscalerFsr ? 0 : upscaler == BbSettings::UpscalerDlss ? 1 : 2;
        if (ImGui::BeginCombo(label, upscalers[shown])) {
            for (int i = 0; i < int(std::size(upscalers)); ++i) {
                const bool supported = kinds[i] == BbSettings::UpscalerFsr    ? s.fsr_supported.load()
                                       : kinds[i] == BbSettings::UpscalerDlss ? s.dlss_supported.load()
                                                                              : true;
                ImGui::BeginDisabled(!supported);
                if (ImGui::Selectable(upscalers[i], i == shown) && i != shown) {
                    // None: TAA, or no upscaler with the game's own anti-aliasing on.
                    Store(s.upscaler, kinds[i] != BbSettings::UpscalerOff ? kinds[i]
                                      : BbSettings::NoUpscaling(s.effects[BbSettings::EffectGameAa]),
                          true);
                }
                ImGui::EndDisabled();
                if (!supported) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", kinds[i] == BbSettings::UpscalerFsr
                                                  ? BbSettings::MenuText("— not available here (AMD's files, or not Windows)",
                                                                         "— недоступен (нет файлов AMD или не Windows)")
                                                  : BbSettings::MenuText("— not supported by this GPU",
                                                                         "— не поддерживается видеокартой"));
                }
            }
            for (const char* name : later) {
                ImGui::BeginDisabled();
                ImGui::Selectable(name, false);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", BbSettings::MenuText("— in development", "— в работе"));
            }
            ImGui::EndCombo();
        }
        if (const char* problem = s.fsr4_problem.load()) {
            ImGui::PushTextWrapPos();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), BbSettings::MenuText("Upscaler unavailable: %s"),
                               BbSettings::MenuText(problem));
            if (!BbSettings::IsFrameUpscaler(s.upscaler))
                ImGui::TextUnformatted(BbSettings::MenuText(
                    "Anti-aliasing above runs instead. Select the upscaler again to retry.",
                    "Вместо него работает сглаживание выше. Выберите апскейлер снова, чтобы повторить."));
            ImGui::PopTextWrapPos();
        }
    }
    // 0.4.4 beta 27b/27c: the upscaler's own rows, under it: its quality and render resolution, then
    // the chosen upscaler's version or model (the DLSS section's model moved here).
    ImGui::Indent();
    if (upscaling || Searching()) {
        QualityRows();
    }
    if (s.upscaler == BbSettings::UpscalerFsr || Searching()) {
        // 0.4.4 beta 27d: its auto exposure as AMD advises (no checkbox), the jitter sign check in
        // config.ini only (fsr_invert_jitter): neither a player's choice.
        FsrVersionRow();
    }
    if (s.upscaler == BbSettings::UpscalerDlss || Searching()) {
        DlssModelRow();
    }
    ImGui::Unindent();
    const bool temporal = s.upscaler != BbSettings::UpscalerOff; // an upscaler or TAA
    // Sharpening after an upscaler or TAA, its strength under it. 0.4.4 beta 27d (the tester): their
    // hints say where it runs for what runs, as the launcher's lines: FSR sharpens up to 1 itself and an
    // RCAS pass adds the rest; DLSS has none of its own, so the pass does it all; TAA's runs after it.
    const bool fsr = s.upscaler == BbSettings::UpscalerFsr;
    const char* where = s.upscaler == BbSettings::UpscalerDlss
                            ? BbSettings::MenuText("After DLSS (it has none of its own).", "После DLSS (своей у него нет).")
                        : fsr ? BbSettings::MenuText("FSR's own sharpening.", "Собственная резкость FSR.")
                        : s.upscaler == BbSettings::UpscalerTaa ? BbSettings::MenuText("After TAA.", "После TAA.")
                                                                : BbSettings::MenuText("Needs an upscaler or TAA.",
                                                                                       "Нужен апскейлер или TAA.");
    ImGui::BeginDisabled(!temporal);
    if (const char* label = Item("Sharpening (RCAS)")) {
        Checkbox(label, s.sharpen);
        char hint[384];
        std::snprintf(hint, sizeof(hint), "%s %s", where,
                      BbSettings::MenuText("AMD's RCAS: brings back detail that upscaling softens.",
                                           "RCAS от AMD: возвращает детали, которые смягчает апскейлинг."));
        Hint(hint);
    }
    ImGui::Indent();
    if (const char* label = Item("Sharpness")) {
        ImGui::BeginDisabled(!s.sharpen);
        Slider(label, s.sharpness, 0.0f, 2.0f);
        ImGui::EndDisabled();
        char hint[384];
        std::snprintf(hint, sizeof(hint), "%s %s",
                      fsr ? BbSettings::MenuText("Above 1 adds a second pass.", "Выше 1 добавляется второй проход.")
                          : BbSettings::MenuText("Higher is sharper.", "Чем выше, тем резче."),
                      BbSettings::MenuText("0.20 is light; high values can halo edges and show grain. Ctrl+click to "
                                           "type a value.",
                                           "0.20 — слегка; высокие значения дают ореолы по краям и зерно. Ctrl+щелчок — "
                                           "ввести значение."));
        Hint(hint);
    }
    ImGui::Unindent();
    ImGui::EndDisabled(); // no upscaler or TAA

    // 0.4.4 beta 27b: the fine settings most players leave alone, as the launcher's Advanced (character
    // motion vectors were in Motion vectors, with the debug view).
    Section("Advanced", "Дополнительно");
    if (const char* label = Item("Character motion vectors", "Векторы движения персонажей")) {
        ImGui::BeginDisabled(!temporal);
        Checkbox(label, s.object_motion);
        ImGui::EndDisabled();
        Hint(BbSettings::MenuText(
            "Accurate vectors for animated objects: clothing and weapons break up less "
            "during movement, at about 10% of the frame rate. The static scene does not get an "
            "additional pass. With an upscaler or TAA. Changes apply after restarting the game.",
            "Точные векторы для анимированных объектов: одежда и оружие меньше рассыпаются "
            "при движении, ценой около 10% частоты кадров. Статичная сцена не получает "
            "дополнительный проход. С апскейлером или TAA. Изменение применяется после "
            "перезапуска игры."));
    }
    // 0.4.4 beta 27d: with an upscaler only (TemporalUpscaler::SceneMipBias: TAA renders at the output).
    // DLSS's sharper textures folded in (the extra level NVIDIA and AMD both advise, always); DLSS's auto
    // exposure as NVIDIA advises, without a checkbox; sub-pixel jitter in config.ini only (jitter=0).
    ImGui::BeginDisabled(!upscaling);
    if (const char* label = Item("Texture mip bias", "Mip bias текстур")) {
        Slider(label, s.mip_bias, BbSettings::MipBiasMin, BbSettings::MipBiasMax, "%+.2f");
        Hint(BbSettings::MenuText(
            "Added to the texture detail the upscaler asks for: the render-to-output ratio and one "
            "level more, as NVIDIA and AMD advise. Below 0 sharper textures (more shimmer), above 0 "
            "softer. Live.",
            "Добавляется к детализации текстур, которую задаёт апскейлер: соотношение рендера и вывода "
            "и ещё один уровень, как советуют NVIDIA и AMD. Меньше 0 — чётче (больше мерцания), больше "
            "0 — мягче. Меняется сразу."));
    }
    ImGui::EndDisabled(); // no upscaler
}


// bbport: FSR 3.1's tuning constants (AMD's key-value configuration, AmdFsr::Tune), as OptiScaler
// offers them, with presets; live. Shown while AMD FSR runs FSR 3.1 (and to a search).
void Fsr3Section() {
    auto& s = BbSettings::Get();
    if ((s.upscaler != BbSettings::UpscalerFsr || !s.fsr_runs_31.load()) && !Searching()) {
        return;
    }
    Section("FSR 3.1 tuning", "Настройка FSR 3.1");
    struct Tuning {
        const char* en;
        const char* ru;
        float velocity, reactiveness, shading, accumulation, disocclusion;
    };
    static constexpr Tuning presets[] = {
        {"Default (AMD's)", "По умолчанию (AMD)", 1.0f, 1.0f, 1.0f, 0.333f, -0.333f},
        {"Steady bright pixels", "Стабильные яркие пиксели", 0.5f, 1.0f, 1.0f, 0.333f, -0.333f},
        {"Less ghosting", "Меньше шлейфов", 1.0f, 1.5f, 1.5f, 0.25f, -0.333f},
        {"Less flicker", "Меньше мерцания", 1.0f, 1.0f, 1.0f, 0.4f, -0.1f},
    };
    // 0.4.4 beta 27c: "Tuning" (was "Preset", now the quality's word elsewhere).
    if (const char* label = Item("Tuning", "Настройка")) {
        int index = -1;
        for (int i = 0; i < int(std::size(presets)); ++i) {
            const auto& p = presets[i];
            const auto near = [](float a, float b) { return std::abs(a - b) < 0.0005f; };
            if (near(s.fsr3_velocity_factor, p.velocity) && near(s.fsr3_reactiveness_scale, p.reactiveness) &&
                near(s.fsr3_shading_change_scale, p.shading) && near(s.fsr3_accumulation, p.accumulation) &&
                near(s.fsr3_min_disocclusion, p.disocclusion)) {
                index = i;
            }
        }
        if (ImGui::BeginCombo(label, index >= 0 ? BbSettings::MenuText(presets[index].en, presets[index].ru)
                                                : BbSettings::MenuText("Custom", "Своё"))) {
            for (int i = 0; i < int(std::size(presets)); ++i) {
                if (ImGui::Selectable(BbSettings::MenuText(presets[i].en, presets[i].ru), i == index)) {
                    Store(s.fsr3_velocity_factor, presets[i].velocity, true);
                    Store(s.fsr3_reactiveness_scale, presets[i].reactiveness, true);
                    Store(s.fsr3_shading_change_scale, presets[i].shading, true);
                    Store(s.fsr3_accumulation, presets[i].accumulation, true);
                    Store(s.fsr3_min_disocclusion, presets[i].disocclusion, true);
                }
            }
            ImGui::EndCombo();
        }
        Hint(BbSettings::MenuText(
            "FSR 3.1's own tuning, as OptiScaler sets it. Steady bright pixels: lamps and sparks shimmer "
            "less in motion. Less ghosting: fewer trails behind moving things, thin details flicker a "
            "little more. Less flicker: thin swaying things (grass, hair) flicker less, a little more "
            "ghosting.",
            "Собственная настройка FSR 3.1, как в OptiScaler. Стабильные яркие пиксели: фонари и искры "
            "меньше мерцают в движении. Меньше шлейфов: меньше следов за движущимся, тонкие детали "
            "мерцают чуть больше. Меньше мерцания: тонкое (трава, волосы) мерцает меньше, шлейфов "
            "чуть больше."));
    }
    if (const char* label = Item("Velocity factor", "Учёт скорости")) {
        Slider(label, s.fsr3_velocity_factor, 0.0f, 1.0f, "%.2f");
    }
    if (const char* label = Item("Reactiveness", "Реактивность")) {
        Slider(label, s.fsr3_reactiveness_scale, 0.0f, 4.0f, "%.2f");
    }
    if (const char* label = Item("Shading change", "Изменение освещения")) {
        Slider(label, s.fsr3_shading_change_scale, 0.0f, 4.0f, "%.2f");
    }
    if (const char* label = Item("Accumulation per frame", "Накопление за кадр")) {
        Slider(label, s.fsr3_accumulation, 0.0f, 1.0f, "%.3f");
    }
    if (const char* label = Item("Disocclusion accumulation", "Накопление при раскрытии")) {
        Slider(label, s.fsr3_min_disocclusion, -1.0f, 1.0f, "%.3f");
    }
}

// The reactive mask (vk_temporal_upscaler.cpp ReactiveOn) and the motion vectors, with their debug
// views.
void MaskSection() {
    auto& s = BbSettings::Get();
    const bool upscaler_on = s.upscaler != BbSettings::UpscalerOff;
    const bool taa = s.upscaler == BbSettings::UpscalerTaa;
    // 0.4.4 beta 25b: DLSS takes no mask (ReactiveOn): its bias mask is one NVIDIA advises against.
    const bool dlss = s.upscaler == BbSettings::UpscalerDlss;
    ImGui::BeginDisabled(!upscaler_on);
    Section("Reactive mask");
    ImGui::BeginDisabled(taa || dlss);
    if (const char* label = Item("Enable mask")) {
        Checkbox(label, s.reactive);
        Hint(BbSettings::MenuText(
            "Marks transparent effects (particles, haze) so the upscaler relies less on past "
            "frames. Fewer trails behind effects, but shimmer returns underneath them."));
        // What the selected upscaler does with it.
        const char* use = s.upscaler == BbSettings::UpscalerFsr
                              ? BbSettings::MenuText("AMD FSR takes it as its reactive mask (FSR 3.1 uses it; FSR 4 "
                                                     "may).",
                                                     "AMD FSR берёт её как реактивную маску (FSR 3.1 её использует; "
                                                     "FSR 4 может).")
                          : dlss
                              ? BbSettings::MenuText("DLSS does not use it.", "DLSS её не использует.")
                              : BbSettings::MenuText("TAA does not use it.", "TAA её не использует.");
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", use);
        ImGui::PopTextWrapPos();
    }
    ImGui::BeginDisabled(!s.reactive);
    if (const char* label = Item("Scale")) {
        Slider(label, s.reactive_scale, 0.0f, 4.0f);
    }
    if (const char* label = Item("Threshold")) {
        Slider(label, s.reactive_threshold, 0.0f, 1.0f);
    }
    if (const char* label = Item("Maximum")) {
        Slider(label, s.reactive_max, 0.0f, 1.0f);
    }
    if (const char* label = Item("Show mask (debug)")) {
        bool show_mask = s.debug_view == BbSettings::DebugReactive;
        if (ImGui::Checkbox(label, &show_mask)) {
            s.debug_view = show_mask ? BbSettings::DebugReactive : BbSettings::DebugNone;
        }
        Hint(BbSettings::MenuText(
            "Red: how far each pixel stops relying on past frames (Maximum = full red); the rest of "
            "the picture is darkened. Scale makes changes count more, Threshold drops faint ones "
            "(thin mist). Not saved.",
            "Красный: насколько пиксель перестаёт полагаться на прошлые кадры (Maximum — полностью "
            "красный); остальная картинка затемнена. Scale усиливает изменения, Threshold отбрасывает "
            "слабые (лёгкий туман). Не сохраняется."));
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    Section("Motion vectors", "Векторы движения");
    if (const char* label = Item("Show motion vectors (debug)", "Показать векторы движения (отладка)")) {
        bool show_motion = s.debug_view == BbSettings::DebugMotion;
        if (ImGui::Checkbox(label, &show_motion)) {
            s.debug_view = show_motion ? BbSettings::DebugMotion : BbSettings::DebugNone;
        }
        Hint(BbSettings::MenuText(
            "Red/green: horizontal/vertical motion (8 pixels = full brightness). "
            "Blue: the pixel received an accurate object vector, not just camera motion. "
            "The upscaler treats a moving object with no blue or red/green as "
            "stationary, causing trails.",
            "Красный/зелёный: движение по горизонтали/вертикали (8 пикселей = полная яркость). "
            "Синий: пиксель получил точный вектор объекта, а не только движение камеры. "
            "Движущийся предмет без синего и без красного/зелёного апскейлер считает "
            "неподвижным, отсюда шлейф."));
    }
    ImGui::EndDisabled(); // upscaler off
}

// bbport: OptiScaler (vk_optiscaler.h): what was found at start, and the engine's compatibility
// mode for it (optiscaler_compat in config.ini, applied on restart).
void OptiScalerSection() {
    auto& s = BbSettings::Get();
    const auto& opti = Vulkan::OptiScaler::Detect();
    Section("OptiScaler");
    if (WholeSection() && Shown("OptiScaler")) {
        ImGui::PushTextWrapPos();
        if (!opti.loaded) {
            ImGui::TextDisabled("%s", BbSettings::MenuText(
                                          "Not loaded. OptiScaler is a DLL put next to the engine (often as winmm.dll "
                                          "or dxgi.dll) with its OptiScaler.ini; it takes over DLSS. The switches below "
                                          "apply when it is there.",
                                          "Не загружен. OptiScaler — это DLL рядом с движком (обычно winmm.dll или "
                                          "dxgi.dll) с файлом OptiScaler.ini; он перехватывает DLSS. Переключатели ниже "
                                          "действуют, когда он есть."));
        } else {
            ImGui::Text(BbSettings::MenuText("Loaded: %s as %s", "Загружен: %s как %s"),
                        opti.version.empty() ? "OptiScaler" : ("v" + opti.version).c_str(), opti.module.c_str());
            ImGui::Text("%s",
                        Vulkan::OptiScaler::Compat()
                            ? BbSettings::MenuText("Compatibility mode in this run: on", "Режим совместимости сейчас: вкл.")
                            : BbSettings::MenuText("Compatibility mode in this run: off", "Режим совместимости сейчас: выкл."));
            if (!opti.frame_generation.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                                   BbSettings::MenuText(
                                       "Its own frame generation is on (%s). If the GPU is lost, set Frame "
                                       "Generation to none in OptiScaler's menu.",
                                       "Включена его собственная генерация кадров (%s). Если GPU теряется, "
                                       "выберите none для Frame Generation в меню OptiScaler."),
                                   opti.frame_generation.c_str());
            }
        }
        ImGui::PopTextWrapPos();
    }
    if (const char* label = Item("Compatibility mode (restart required)", "Режим совместимости (после перезапуска)")) {
        Checkbox(label, s.optiscaler_compat);
        Hint(BbSettings::MenuText(
            "Keeps the engine on the paths OptiScaler's hooks handle: DLSS calls wait for the GPU queue, "
            "the Vulkan device is created where OptiScaler can prepare it, and one thread sends the GPU "
            "work while its frame generation is on (a little FPS). Off: the engine runs as without "
            "OptiScaler. OptiScaler's own settings (its upscaler, its frame generation) are in its own "
            "overlay and OptiScaler.ini. Applies after restarting the game.",
            "Держит движок на путях, которые выдерживают хуки OptiScaler: вызовы DLSS ждут очередь GPU, "
            "устройство Vulkan создаётся там, где OptiScaler может его подготовить, а работу GPU "
            "отправляет один поток, пока включена его генерация кадров (немного FPS). Выкл.: движок "
            "работает как без OptiScaler. Собственные настройки OptiScaler (его апскейлер, его генерация "
            "кадров) — в его оверлее и OptiScaler.ini. Применяется после перезапуска игры."));
        if (const char* env = std::getenv("BB_OPTISCALER_COMPAT"); env && env[0] == '0') {
            ImGui::TextDisabled("%s", BbSettings::MenuText(
                                          "Off for this run: the launcher's Engine > OptiScaler compatibility is off.",
                                          "Выключен в этом запуске: в лаунчере выключено Engine > OptiScaler "
                                          "compatibility."));
        }
    }
}

// Output resolution and live resolution changes.
void OutputSection() {
    auto& s = BbSettings::Get();
    Section("Output resolution", "Разрешение вывода");
    if (const char* label = Item("Output resolution", "Разрешение вывода")) {
        const char* outputs[] = {"1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160",
            "2560 x 1080", "3440 x 1440", "3840 x 1600", "3840 x 1080", "5120 x 1440", "5120 x 2160",
            "7680 x 2160", BbSettings::MenuText("Display (auto)"), "Custom"};
        static_assert(std::size(outputs) == BbSettings::OutputCount);
        static char custom[48];
        std::snprintf(custom, sizeof custom, "Custom (%d x %d)", s.custom_output_w, s.custom_output_h);
        outputs[BbSettings::OutputCustom] = custom;
        const int output = s.output_res;
        if (ImGui::BeginCombo(label, outputs[output])) {
            for (int i = 0; i < BbSettings::OutputCount; ++i) {
                if (ImGui::Selectable(outputs[i], i == output)) {
                    Store(s.output_res, i, true);
                }
            }
            ImGui::EndCombo();
        }
        if (!BbSettings::OutputChangesLive()) { // 0.4.4 beta 26b: Live resolution changes off, 1080p starts too
            Hint(BbSettings::MenuText(
                "Size of the final frame and UI. The preset sets the scene size relative to "
                "the output: 4K Performance = 1920x1080. Applies after restarting the game.",
                "Размер готового кадра и интерфейса. Пресет задаёт размер сцены относительно "
                "вывода: 4K Performance = 1920x1080. Применяется после перезапуска игры."));
        } else {
            Hint(BbSettings::MenuText(
                "The final frame and UI size changes at the next frame boundary. "
                "The preset sets the scene size relative to the output: 4K Performance = 1920x1080. "
                "Changing the size resets FSR history and may cause a brief pause.",
                "Размер готового кадра и интерфейса меняется на границе следующего кадра. "
                "Пресет задаёт размер сцены относительно вывода: 4K Performance = 1920x1080. "
                "Смена размера сбрасывает историю FSR и может вызвать короткую паузу."));
        }
    }
    if (const char* label = Item("Live resolution changes", "Смена разрешения на лету")) {
        // 0.4.4 beta 25c: Off (recommended) or On; no "Auto".
        const char* live_modes[] = {BbSettings::MenuText("Off (recommended)", "Выключена (рекомендуется)"),
                                    BbSettings::MenuText("On", "Включена")};
        const int live = std::clamp(s.live_resolution.load(), 0, 1);
        if (ImGui::BeginCombo(label, live_modes[live])) {
            for (int i = 0; i < 2; ++i) {
                if (ImGui::Selectable(live_modes[i], i == live)) {
                    Store(s.live_resolution, i, true);
                }
            }
            ImGui::EndCombo();
        }
        Hint(BbSettings::MenuText(
            "On: output resolution and preset change without restarting, but game post-processing "
            "stays at 1080p, which is noticeably slower on Steam Deck and older GPUs. "
            "Off (recommended): everything renders at the preset resolution, and changes require a "
            "restart. Applies after restarting the game.",
            "Включена: разрешение вывода и пресет меняются без перезапуска, но постобработка игры "
            "остаётся в 1080p — на Steam Deck и старых видеокартах это заметно медленнее. "
            "Выключена (рекомендуется): всё рисуется в разрешении пресета, смена — через "
            "перезапуск. Применяется после перезапуска игры."));
    }
}

void CameraSection() {
    auto& s = BbSettings::Get();
    // 0.4.4 beta 26: the launcher's name for it (Gameplay › Camera tuning), was "PC Camera".
    Section("Camera tuning", "Настройка камеры");
    if (WholeSection() && Shown("Camera tuning")) {
        ImGui::TextDisabled("%s", BbSettings::MenuText(
            "Turn on Camera tuning in the launcher (Gameplay) before starting; changes here are live.",
            "Включите настройку камеры в лаунчере (Игровой процесс) перед запуском; изменения здесь применяются сразу."));
    }
    if (const char* label = Item("Camera FOV scale")) {
        Slider(label, s.camera_fov_scale, 1.0f, 1.5f);
    }
    if (const char* label = Item("Camera distance scale")) {
        Slider(label, s.camera_distance_scale, 0.5f, 1.5f);
    }
    if (const char* label = Item("Camera height scale")) {
        Slider(label, s.camera_height_scale, 0.5f, 1.5f);
    }
}

void CheatsSection() {
    auto& s = BbSettings::Get();
    Section("Cheats", "Читы");
    if (const char* label = Item("Blood Vials", "Флаконы крови")) {
        Cheat(label, s.cheat_vials, BbEngine::Cheats::Vials, custom_vials);
    }
    if (const char* label = Item("Quicksilver Bullets", "Ртутные пули")) {
        Cheat(label, s.cheat_bullets, BbEngine::Cheats::Bullets, custom_bullets);
        Hint(BbSettings::MenuText(
            "Changes apply live. Infinite: kept full at the normal carry limit, even from none. Custom: the count is "
            "refilled to the amount and never lowered. Off restores the game's item data and keeps the "
            "counts you have. While a cheat is on, the counts it gives are saved with your character. "
            "Do not play online with cheats on.",
            "Применяются сразу. Бесконечно: запас всегда полный, даже если предметов не было. Своё "
            "количество: запас пополняется до него и никогда не уменьшается. Выкл. возвращает данные "
            "предметов игры, а имеющееся количество остаётся. Пока чит включён, полученные предметы "
            "сохраняются вместе с персонажем. Не играйте по сети с включёнными читами."));
    }
    if (const char* label = Item("Infinite stamina", "Бесконечная выносливость")) {
        CheatToggle(label, s.cheat_stamina, BbEngine::Cheats::Stamina,
                    BbSettings::MenuText("Your stamina stays full (enemies are not affected). Off: nothing to restore.",
                                         "Ваша выносливость всегда полная (на врагов не действует). Выкл.: "
                                         "восстанавливать нечего."));
    }
    if (const char* label = Item("Stealth", "Скрытность")) {
        CheatToggle(label, s.cheat_stealth, BbEngine::Cheats::Stealth,
                    BbSettings::MenuText("Enemies neither see nor hear you: the game's debug flags Player Hide and "
                                         "Player Silence. Off puts the flags back as they were.",
                                         "Враги вас не видят и не слышат: отладочные флаги игры Player Hide и Player "
                                         "Silence. Выкл. возвращает флаги как были."));
    }
    if (const char* label = Item("Firearm range", "Дальность огнестрела")) {
        static const char* const ranges[] = {"Off", "150%", "200%", "300%"};
        const char* const names[] = {BbSettings::MenuText(ranges[0], "Выкл."), ranges[1], ranges[2], ranges[3]};
        CheatLevels(label, s.cheat_gun_range, BbSettings::CheatGunRanges, names, BbEngine::Cheats::GunRange,
                    BbSettings::MenuText(
                        "Bullets of the pistols, rifles, blunderbusses, cannons, Gatling Gun and the trick "
                        "weapons' guns fly farther. Not Rosmarinus, the Flamesprayer, arcane or hunter tools. "
                        "Off restores the game's bullet data.",
                        "Пули пистолетов, ружей, мушкетонов, пушек, пулемёта и огнестрела трансформируемого "
                        "оружия летят дальше. Не Розмарин, огнемёт, тайные и охотничьи инструменты. Выкл. "
                        "возвращает данные пуль игры."));
    }
    if (const char* label = Item("Keep Blood Echoes on death", "Не терять отголоски при смерти")) {
        CheatToggle(label, s.cheat_keep_echoes, BbEngine::Cheats::KeepEchoes,
                    BbSettings::MenuText("Dying keeps your Blood Echoes; the bloodstain holds none. Off restores the "
                                         "game's code. Echoes you kept stay in your save.",
                                         "При смерти отголоски остаются у вас, в пятне крови их нет. Выкл. "
                                         "возвращает код игры. Сохранённые отголоски остаются в сохранении."));
    }
    if (const char* label = Item("Blood Echo multiplier", "Множитель отголосков")) {
        static const char* const multipliers[] = {"x1", "x2", "x5", "x10"};
        const char* const names[] = {BbSettings::MenuText("x1 (off)", "x1 (выкл.)"), multipliers[1], multipliers[2],
                                     multipliers[3]};
        CheatLevels(label, s.cheat_echo_multiplier, BbSettings::CheatEchoMultipliers, names,
                    BbEngine::Cheats::EchoMultiplier,
                    BbSettings::MenuText(
                        "Blood Echoes from enemies and bosses times this. An enemy already loaded may give the "
                        "normal amount until the area loads again. x1 restores the game's data; echoes gained "
                        "stay in your save.",
                        "Отголоски за врагов и боссов умножаются. Уже загруженный враг может дать обычное "
                        "количество, пока область не загрузится заново. x1 возвращает данные игры; полученные "
                        "отголоски остаются в сохранении."));
    }
    if (const char* label = Item("No weapon wear", "Оружие не изнашивается")) {
        CheatToggle(label, s.cheat_no_durability, BbEngine::Cheats::NoDurability,
                    BbSettings::MenuText("Equipped weapons are kept at full durability. Off stops it; durability is "
                                         "saved as it is.",
                                         "Экипированное оружие всегда с полной прочностью. Выкл. прекращает это; "
                                         "прочность сохраняется как есть."));
    }
    if (const char* label = Item("Infinite consumables", "Бесконечные расходники")) {
        CheatToggle(label, s.cheat_consumables, BbEngine::Cheats::Consumables,
                    BbSettings::MenuText(
                        "Molotovs, knives, pebbles, oil urns, antidotes, sedatives, elixirs, papers, Numbing Mist, "
                        "Shining Coins and similar tools are not used up. Not Blood Vials or Quicksilver Bullets "
                        "(above), key items, materials, or Insight and Blood Echo items. Off restores the item data.",
                        "Коктейли Молотова, ножи, камешки, масло, антидоты, успокоительное, эликсиры, бумага, "
                        "онемляющий туман, блестящие монеты и подобные инструменты не расходуются. Не флаконы крови и "
                        "ртутные пули (выше), не ключевые предметы, материалы, предметы озарения и отголосков. Выкл. "
                        "возвращает данные предметов."));
    }
    if (Shown("Blood Echoes", "Отголоски крови")) {
        CheatStats();
    }
}

void MouseSection();

void FpsSection() {
    auto& s = BbSettings::Get();
    Section("FPS counter", "Счётчик FPS");
    if (const char* label = Item("FPS counter", "Счётчик FPS")) {
        Checkbox(label, s.show_fps);
    }
    if (const char* label = Item("Note while shaders build", "Надпись при сборке шейдеров")) {
        Checkbox(label, s.show_compile_note);
        Hint(BbSettings::MenuText(
            "Shows \"Building shaders\" in a corner while the game builds shaders for a place it "
            "shows for the first time. A stutter with this note is shader building and is gone on "
            "the next visit; one without it is loading.",
            "Показывает «Сборка шейдеров» в углу, пока игра собирает шейдеры для места, которое "
            "показывает впервые. Рывок с этой надписью — сборка шейдеров, при следующем посещении "
            "его не будет; без неё — загрузка."));
    }
    // What it shows and its look, while it is on (searching: each by its own label).
    if (!s.show_fps && !Searching()) {
        MouseSection();
        return;
    }
    ImGui::Indent();
    // Its own ID scope: "Upscaler" here would clash with the upscaler choice.
    ImGui::PushID("fps_counter");
    if (const char* label = Item("Frame rate", "Частота кадров")) {
        Checkbox(label, s.show_fps_rate);
    }
    if (const char* label = Item("Frame time", "Время кадра")) {
        Checkbox(label, s.show_fps_frame_time);
    }
    if (const char* label = Item("Upscaler", "Апскейлер")) {
        Checkbox(label, s.show_fps_upscaler);
    }
    if (const char* label = Item("CPU usage", "Загрузка ЦП")) {
        Checkbox(label, s.show_fps_cpu);
    }
    if (const char* label = Item("GPU usage", "Загрузка ГП")) {
        Checkbox(label, s.show_fps_gpu);
    }
    // bbport: the counter's look.
    if (const char* label = Item("Position", "Положение")) {
        const char* positions[] = {BbSettings::MenuText("Top right", "Сверху справа"),
                                   BbSettings::MenuText("Top left", "Сверху слева"),
                                   BbSettings::MenuText("Bottom right", "Снизу справа"),
                                   BbSettings::MenuText("Bottom left", "Снизу слева"),
                                   BbSettings::MenuText("Top centre", "Сверху по центру")};
        static_assert(std::size(positions) == BbSettings::FpsPositionCount);
        int position = std::clamp(s.fps_position.load(), 0, BbSettings::FpsPositionCount - 1);
        if (ImGui::Combo(label, &position, positions, BbSettings::FpsPositionCount)) {
            Store(s.fps_position, position, true);
        }
    }
    if (const char* label = Item("Size", "Размер")) {
        float scale = s.fps_scale;
        if (ImGui::SliderFloat(label, &scale, 0.75f, 2.0f, "%.2fx")) {
            Store(s.fps_scale, scale, true);
        }
    }
    const auto edit_colour = [](const char* label, std::atomic<unsigned>& rgb) {
        const unsigned v = rgb;
        float c[3] = {float((v >> 16) & 0xFF) / 255.0f, float((v >> 8) & 0xFF) / 255.0f,
                      float(v & 0xFF) / 255.0f};
        if (ImGui::ColorEdit3(label, c, ImGuiColorEditFlags_NoInputs)) {
            const auto byte = [](float f) { return unsigned(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
            Store(rgb, (byte(c[0]) << 16) | (byte(c[1]) << 8) | byte(c[2]), true);
        }
    };
    if (const char* label = Item("Text colour", "Цвет текста")) {
        edit_colour(label, s.fps_text_color);
    }
    if (const char* label = Item("Text opacity", "Непрозрачность текста")) {
        float text_opacity = s.fps_text_opacity;
        if (ImGui::SliderFloat(label, &text_opacity, 0.1f, 1.0f, "%.2f")) {
            Store(s.fps_text_opacity, text_opacity, true);
        }
    }
    if (const char* label = Item("Background colour", "Цвет фона")) {
        edit_colour(label, s.fps_bg_color);
    }
    if (const char* label = Item("Background opacity", "Непрозрачность фона")) {
        float opacity = s.fps_bg_opacity;
        if (ImGui::SliderFloat(label, &opacity, 0.0f, 1.0f, "%.2f")) {
            Store(s.fps_bg_opacity, opacity, true);
        }
    }
    if (const char* label = Item("Border", "Рамка")) {
        Checkbox(label, s.fps_border);
    }
    ImGui::PopID();
    ImGui::Unindent();
    MouseSection();
}

void MouseSection() {
    auto& s = BbSettings::Get();
    Section("Mouse", "Мышь");
    if (const char* label = Item("Hide mouse cursor", "Скрывать курсор мыши")) {
        Checkbox(label, s.hide_cursor);
        Hint(BbSettings::MenuText("Hidden 2 seconds after the mouse stops moving; moving it brings it back.",
                                  "Скрывается через 2 секунды после остановки мыши; движение возвращает его."));
    }
}

enum Tab : int { TabUpscaling, TabDisplay, TabPicture, TabGameplay, TabInterface, TabCount };

const char* TabTitle(int tab) {
    switch (tab) {
    case TabUpscaling: return BbSettings::MenuText("Upscaling", "Апскейлинг");
    case TabDisplay: return BbSettings::MenuText("Display", "Экран");
    case TabPicture: return BbSettings::MenuText("Picture", "Изображение");
    case TabGameplay: return BbSettings::MenuText("Gameplay", "Игра");
    default: return BbSettings::MenuText("Interface", "Интерфейс");
    }
}

/// One tab's sections, in an ID scope of its own (the search results draw them all at once).
void DrawTab(int tab) {
    tab_title = TabTitle(tab);
    ImGui::PushID(tab);
    switch (tab) {
    case TabUpscaling:
        UpscalerSection();
        Fsr3Section();
        MaskSection();
        OptiScalerSection();
        break;
    case TabDisplay:
        OutputSection();
        HdrSection();
        break;
    case TabPicture:
        PictureSection();
        EffectsSection();
        break;
    case TabGameplay:
        CameraSection();
        CheatsSection();
        break;
    default:
        FpsSection();
        break;
    }
    section_title = nullptr;
    ImGui::PopID();
}

int current_tab = TabUpscaling; // the tab shown (kept while the game runs)
int pending_tab = -1;           // a tab chosen by L2 / R2 or Ctrl+PgUp / PgDn, selected next frame

void Menu() {
    auto& s = BbSettings::Get();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    // Where it was moved last (config.ini menu_pos, a fraction of the screen), kept on screen.
    ImVec2 pos(viewport->WorkPos.x + 40.0f * base_scale, viewport->WorkPos.y + 40.0f * base_scale);
    if (s.menu_x >= 0.0f && s.menu_y >= 0.0f) {
        const float margin = 80.0f * base_scale;
        pos.x = viewport->WorkPos.x +
                std::clamp(s.menu_x * viewport->WorkSize.x, 0.0f, std::max(viewport->WorkSize.x - margin, 0.0f));
        pos.y = viewport->WorkPos.y +
                std::clamp(s.menu_y * viewport->WorkSize.y, 0.0f, std::max(viewport->WorkSize.y - margin, 0.0f));
    }
    // The window ends above the screen's bottom edge wherever its top is, so its resize corner
    // stays in reach; the tab's page scrolls inside it, between the tabs and the buttons.
    static int last_frame = -2;
    static float window_top = 0.0f;
    const float min_height = 240.0f * base_scale;
    const float bottom = viewport->WorkPos.y + viewport->WorkSize.y - 8.0f * base_scale;
    pos.y = std::min(pos.y, std::max(viewport->WorkPos.y, bottom - min_height));
    const bool appearing = ImGui::GetFrameCount() != last_frame + 1;
    const float top = appearing ? pos.y : window_top;
    const float max_height = std::max(min_height, bottom - top);
    ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f * base_scale, min_height), ImVec2(FLT_MAX, max_height));
    ImGui::SetNextWindowPos(pos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(640.0f * base_scale, std::min(max_height, 820.0f * base_scale)),
                             ImGuiCond_Appearing);
    bool keep_open = true;
    if (!ImGui::Begin(
            BbSettings::MenuText("Bloodborne - Graphics  (Insert / L3+R3)###bbport_settings",
                                 "Bloodborne — настройки  (Insert / L3+R3)###bbport_settings"),
            &keep_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return;
    }
    last_frame = ImGui::GetFrameCount();
    window_top = ImGui::GetWindowPos().y;
    // Moved: remembered (saved with the settings when the menu closes).
    if (!ImGui::IsWindowAppearing() && viewport->WorkSize.x > 0.0f && viewport->WorkSize.y > 0.0f) {
        const ImVec2 at = ImGui::GetWindowPos();
        const float fx = (at.x - viewport->WorkPos.x) / viewport->WorkSize.x;
        const float fy = (at.y - viewport->WorkPos.y) / viewport->WorkSize.y;
        if (std::abs(at.x - pos.x) >= 1.0f || std::abs(at.y - pos.y) >= 1.0f) {
            s.menu_x = fx;
            s.menu_y = fy;
            dirty = true;
        }
    }

    // Search: Ctrl+F focuses it; Esc empties it, then leaves it (and then closes the menu).
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_RouteGlobal)) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##search", BbSettings::MenuText("Search settings (Ctrl+F)", "Поиск настроек (Ctrl+F)"),
                             search_text, sizeof(search_text), ImGuiInputTextFlags_EscapeClearsAll);
    search_query = Fold(search_text);

    // Always readable, whatever language the rest of the menu is in: each language by its own
    // name, in MenuLanguage order.
    static const char* languages[] = {"English",  "Русский",  "Español", "Português (Brasil)",
                                      "Français", "Deutsch",  "Italiano", "Polski", "Türkçe", "简体中文"};
    static_assert(std::size(languages) == BbSettings::MenuLanguageCount);
    int language = int(s.menu_language.load());
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
    if (ImGui::Combo(BbSettings::MenuText("Language"), &language, languages, BbSettings::MenuLanguageCount)) {
        s.menu_language =
            BbSettings::MenuLanguage(std::clamp(language, 0, BbSettings::MenuLanguageCount - 1));
        BbSettings::Save();
    }
    ImGui::SameLine();
    ImGui::Text(BbSettings::MenuText("%.0f FPS  (%.1f ms)", "%.0f FPS  (%.1f мс)"),
                frame_ms_avg > 0.0f ? 1000.0f / frame_ms_avg : 0.0f, frame_ms_avg);

    // The tabs: L2 / R2 on a controller, Ctrl+PgUp / Ctrl+PgDn on the keyboard.
    if (!Searching()) {
        const bool previous = ImGui::IsKeyPressed(ImGuiKey_GamepadL2, false) ||
                              ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_PageUp, ImGuiInputFlags_RouteGlobal);
        const bool next = ImGui::IsKeyPressed(ImGuiKey_GamepadR2, false) ||
                          ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_PageDown, ImGuiInputFlags_RouteGlobal);
        if (previous != next) {
            pending_tab = (current_tab + (next ? 1 : TabCount - 1)) % TabCount;
        }
    }
    // The page fills the window down to the buttons below it (their height last frame).
    static float footer_height = 0.0f;
    const ImVec2 page(0.0f, -std::max(footer_height, ImGui::GetFrameHeightWithSpacing() * 2.0f));
    shown_items = 0;
    if (!Searching()) {
        if (ImGui::BeginTabBar("##tabs")) {
            for (int tab = 0; tab < TabCount; ++tab) {
                const std::string label = std::string(TabTitle(tab)) + "###tab" + std::to_string(tab);
                const ImGuiTabItemFlags flags = tab == pending_tab ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(label.c_str(), nullptr, flags)) {
                    current_tab = tab;
                    if (ImGui::BeginChild("##page", page, ImGuiChildFlags_NavFlattened)) {
                        DrawTab(tab);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        pending_tab = -1;
    } else {
        if (ImGui::BeginChild("##results", page, ImGuiChildFlags_NavFlattened)) {
            for (int tab = 0; tab < TabCount; ++tab) {
                DrawTab(tab);
            }
            if (shown_items == 0) {
                ImGui::TextDisabled(BbSettings::MenuText("Nothing matches \"%s\"", "Ничего не найдено по «%s»"),
                                    search_text);
            }
        }
        ImGui::EndChild();
    }

    const float footer_top = ImGui::GetCursorPosY();
    bool restart =
        s.object_motion != s.startup_object_motion || s.model_lod != s.startup_model_lod ||
        s.live_resolution != s.startup_live_resolution || s.hdr != s.startup_hdr ||
        BbSettings::ResolutionNeedsRestart();
    for (int e = 0; e < BbSettings::EffectCount; ++e) {
        restart |= e != BbSettings::EffectGameAa && s.effects[e] != s.startup_effects[e];
    }
    // 0.4.4 beta 27b: the game's own anti-aliasing, patched at start, applies only without an upscaler.
    restart |= BbSettings::GameAaApplies(s.effects[BbSettings::EffectGameAa], s.upscaler) !=
               BbSettings::GameAaApplies(s.startup_effects[BbSettings::EffectGameAa], s.startup_upscaler);
    // OptiScaler's switches matter only with it loaded.
    restart |= Vulkan::OptiScaler::Detect().loaded && s.optiscaler_compat != s.startup_optiscaler_compat;
    if (restart) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s",
                           BbSettings::MenuText("Changes apply after restarting the game",
                                                "Изменения применятся после перезапуска игры"));
        ImGui::SameLine();
        if (ImGui::Button(
                BbSettings::MenuText("Apply and restart game", "Применить и перезапустить игру"))) {
            BbSettings::Save();
            // Out of a co-op room first, as closing the window is (bbgpu.cpp): the new process
            // signs in as a new member, and the server kept the old one until its heartbeat aged.
            bbnet_exit_begin(2000);
            bbnet_exit_wait();
            runtime_restart();
        }
    }
    if (ImGui::Button(BbSettings::MenuText("Close", "Закрыть"))) {
        keep_open = false;
    }
    // Quit (issue #21): the same clean exit as closing the window, so a controller player need not
    // force-close the game. Asks once more before quitting; the game's own save is untouched.
    static bool quit_armed = false;
    ImGui::SameLine();
    // One ID for both labels ("###quit"): a label change made it a new button, and a controller's
    // focus stayed on the old one, so the confirmation was dropped before the second press.
    const std::string quit_label =
        std::string(quit_armed ? BbSettings::MenuText("Press again to quit", "Нажмите ещё раз")
                               : BbSettings::MenuText("Quit game", "Выйти из игры")) + "###quit";
    if (ImGui::Button(quit_label.c_str())) {
        if (quit_armed) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
        }
        quit_armed = !quit_armed;
    }
    if (quit_armed && !ImGui::IsItemHovered() && !ImGui::IsItemFocused()) {
        quit_armed = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", BbSettings::MenuText("Settings are saved to config.ini",
                                             "Настройки сохраняются в config.ini"));
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", BbSettings::MenuText(
                                  "Tabs: L2 / R2 or Ctrl+PgUp / Ctrl+PgDn. Search: Ctrl+F, Esc clears it. "
                                  "A setting's (?) also shows while it is selected.",
                                  "Вкладки: L2 / R2 или Ctrl+PgUp / Ctrl+PgDn. Поиск: Ctrl+F, Esc очищает. "
                                  "Подсказка (?) видна и когда настройка выбрана."));
    ImGui::PopTextWrapPos();
    footer_height = ImGui::GetCursorPosY() - footer_top + ImGui::GetStyle().ItemSpacing.y;
    ImGui::End();
    if (!keep_open) {
        SetOpen(false);
    }
}

// bbport: the FPS counter's box this frame (the compile note sits next to it); zero: not drawn.
ImVec2 fps_box_min{}, fps_box_max{};

// bbport: "Building shaders" while pipelines are built in play (a place seen for the first time):
// a hitch with the note was shader building, one without it loading (Linux report, 0.9.37 beta
// 6). A burst of compiles (gaps under 2 s) shows it once it took 30 ms, until 1.5 s after its last.
std::mutex compile_note_mutex;
bool compile_note_primed = false;
u64 compile_note_seen_ns = 0, compile_note_burst_ns = 0;
u32 compile_note_seen = 0, compile_note_burst = 0;
std::chrono::steady_clock::time_point compile_note_last{};

bool CompileNoteActive() {
    if (!BbSettings::Get().show_compile_note) {
        return false;
    }
    const u64 ns = Vulkan::g_bb_compile_ns_total.load(std::memory_order_relaxed);
    const u32 count = Vulkan::g_bb_compiles_total.load(std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock lock{compile_note_mutex};
    if (!compile_note_primed) {
        // The start's warm-up is not a hitch in play.
        compile_note_primed = true;
        compile_note_seen_ns = ns;
        compile_note_seen = count;
        return false;
    }
    if (ns != compile_note_seen_ns) {
        if (now - compile_note_last > std::chrono::seconds(2)) {
            compile_note_burst_ns = 0;
            compile_note_burst = 0;
        }
        compile_note_burst_ns += ns - compile_note_seen_ns;
        compile_note_burst += count - compile_note_seen;
        compile_note_seen_ns = ns;
        compile_note_seen = count;
        compile_note_last = now;
    }
    return compile_note_burst_ns >= 30'000'000ull &&
           now - compile_note_last < std::chrono::milliseconds(1500);
}

void CompileNote() {
    u32 burst;
    {
        std::scoped_lock lock{compile_note_mutex};
        burst = compile_note_burst;
    }
    // Under the FPS counter (above it at the bottom), else in the top right corner, where the
    // game's HUD has nothing.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = 12.0f * base_scale;
    const bool beside_counter = fps_box_max.x > fps_box_min.x;
    const bool bottom = beside_counter && fps_box_min.y > viewport->WorkPos.y + viewport->WorkSize.y * 0.5f;
    const float x = beside_counter ? fps_box_max.x : viewport->WorkPos.x + viewport->WorkSize.x - pad;
    const float y = !beside_counter ? viewport->WorkPos.y + pad
                    : bottom        ? fps_box_min.y - pad * 0.5f
                                    : fps_box_max.y + pad * 0.5f;
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always, ImVec2(1.0f, bottom ? 1.0f : 0.0f));
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##compile_note", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::Text("%s (%u)", BbSettings::MenuText("Building shaders", "Сборка шейдеров"), burst);
    ImGui::End();
}

void FpsCounter() {
    // What it shows (the menu's checkboxes under the counter); nothing chosen: no window.
    const auto& s = BbSettings::Get();
    char text[128];
    int n = 0;
    if (s.show_fps_rate) {
        n += std::snprintf(text + n, sizeof(text) - n, "%.0f FPS",
                           frame_ms_avg > 0.0f ? 1000.0f / frame_ms_avg : 0.0f);
    }
    if (s.show_fps_frame_time) {
        n += std::snprintf(text + n, sizeof(text) - n, "%s%.1f ms", n ? "  " : "", frame_ms_avg);
    }
    if (s.show_fps_upscaler) {
        // 0.4.4 beta 27c: what runs: AMD's version name ("FSR 3.1.6"), DLSS with its model ("DLSS K"),
        // TAA; "off" without.
        const int upscaler = s.upscaler;
        const int model = s.dlss_applied_preset;
        const char* fsr = s.fsr_running.load();
        char running[48];
        if (upscaler == BbSettings::UpscalerFsr) {
            std::snprintf(running, sizeof(running), "%s", fsr ? fsr : "AMD FSR");
        } else if (upscaler == BbSettings::UpscalerDlss && model > 0) {
            std::snprintf(running, sizeof(running), "DLSS %c", char('A' + model - 1));
        } else {
            std::snprintf(running, sizeof(running), "%s",
                          upscaler == BbSettings::UpscalerDlss ? "DLSS"
                          : upscaler == BbSettings::UpscalerTaa ? "TAA"
                                                                : BbSettings::UpscalerName(upscaler));
        }
        n += std::snprintf(text + n, sizeof(text) - n, "%s%s", n ? "  " : "", running);
    }
    if (s.show_fps_cpu || s.show_fps_gpu) {
        const BbUsage::Sample use = BbUsage::Get(s.show_fps_cpu, s.show_fps_gpu);
        if (s.show_fps_cpu && use.cpu >= 0.0f) {
            n += std::snprintf(text + n, sizeof(text) - n, "%sCPU %.0f%%", n ? "  " : "", use.cpu);
        }
        if (s.show_fps_gpu && use.gpu >= 0.0f) {
            n += std::snprintf(text + n, sizeof(text) - n, "%sGPU %.0f%%", n ? "  " : "", use.gpu);
        }
    }
    if (n == 0) {
        return;
    }
    // bbport: the text changes twice a second: redrawn every frame its numbers flickered too fast
    // to read (at 100+ frames a second).
    static char shown[sizeof(text)] = "";
    static auto shown_at = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (shown[0] == '\0' || now - shown_at >= std::chrono::milliseconds(500)) {
        std::memcpy(shown, text, sizeof(text));
        shown_at = now;
    }
    // bbport: its corner, size, colours, background opacity and border (fps_* settings).
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = 12.0f * base_scale;
    const int position = std::clamp(s.fps_position.load(), 0, BbSettings::FpsPositionCount - 1);
    const bool right = position == BbSettings::FpsTopRight || position == BbSettings::FpsBottomRight;
    const bool bottom = position == BbSettings::FpsBottomRight || position == BbSettings::FpsBottomLeft;
    const bool centre = position == BbSettings::FpsTopCentre;
    const float x = centre  ? viewport->WorkPos.x + viewport->WorkSize.x * 0.5f
                    : right ? viewport->WorkPos.x + viewport->WorkSize.x - pad
                            : viewport->WorkPos.x + pad;
    const float y = bottom ? viewport->WorkPos.y + viewport->WorkSize.y - pad : viewport->WorkPos.y + pad;
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always,
                            ImVec2(centre ? 0.5f : right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f));
    // bbport: the box only grows (until what it shows or its size changes): sized to each text,
    // it moved sideways whenever a number got a digit narrower or wider and the text shook.
    static float min_width = 0.0f;
    static size_t min_width_key = 0;
    const size_t width_key = size_t(s.show_fps_rate) | size_t(s.show_fps_frame_time) << 1 |
                             size_t(s.show_fps_upscaler) << 2 | size_t(s.show_fps_cpu) << 3 |
                             size_t(s.show_fps_gpu) << 4 | size_t(s.upscaler) << 6 | size_t(s.fps_scale * 100.0f) << 12;
    if (width_key != min_width_key) {
        min_width = 0.0f;
        min_width_key = width_key;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(min_width, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    const auto colour = [](unsigned rgb, float alpha) {
        return ImVec4(float((rgb >> 16) & 0xFF) / 255.0f, float((rgb >> 8) & 0xFF) / 255.0f,
                      float(rgb & 0xFF) / 255.0f, alpha);
    };
    ImGui::PushStyleColor(ImGuiCol_WindowBg, colour(s.fps_bg_color, s.fps_bg_opacity));
    ImGui::PushStyleColor(ImGuiCol_Text, colour(s.fps_text_color, std::clamp(s.fps_text_opacity.load(), 0.1f, 1.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, s.fps_border ? 1.0f : 0.0f);
    ImGui::Begin("##fps", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::SetWindowFontScale(std::clamp(s.fps_scale.load(), 0.75f, 2.0f));
    ImGui::TextUnformatted(shown);
    min_width = std::max(min_width, ImGui::GetWindowWidth());
    fps_box_min = ImGui::GetWindowPos();
    fps_box_max = ImVec2(fps_box_min.x + ImGui::GetWindowWidth(), fps_box_min.y + ImGui::GetWindowHeight());
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void TextPrompt() {
    std::string title, text;
    {
        std::scoped_lock lock{prompt_mutex};
        title = prompt_title;
        text = prompt_text;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                   viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::Begin("##textprompt", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextUnformatted(title.c_str());
    ImGui::Separator();
    ImGui::Text("%s_", text.c_str());
    ImGui::Separator();
    ImGui::TextUnformatted(BbSettings::MenuText("Keyboard: type, Backspace = delete, Enter = OK, Esc = cancel"));
    ImGui::End();
}

} // namespace

void SetTextPrompt(bool active, const std::string& prompt, const std::string& text) {
    {
        std::scoped_lock lock{prompt_mutex};
        prompt_title = prompt;
        prompt_text = text;
    }
    prompt_active = active;
}

/// bbport: what opens this menu (BB_OVERLAY_KEYS, the launcher's "In-game menu shortcut"): "both"
/// (Insert and L3+R3, the default), "insert" (a controller can't open it by accident: L3 and R3
/// pressed together in a fight did) or "off" (only the game's own PC menus).
enum class OverlayKeys { Both, Insert, Off };
static OverlayKeys MenuKeys() {
    static const OverlayKeys keys = [] {
        const char* env = std::getenv("BB_OVERLAY_KEYS");
        const std::string value = env ? env : "";
        return value == "off" ? OverlayKeys::Off : value == "insert" ? OverlayKeys::Insert : OverlayKeys::Both;
    }();
    return keys;
}

void Init(const Vulkan::Instance& instance, vk::Format format, u32 image_count) {
    std::scoped_lock lock{imgui_mutex};
    if (initialized) {
        return;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // window positions are not kept
    io.ConfigDebugHighlightIdConflicts = false; // a programmer's warning, not for players
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendPlatformName = "hunters-workshop";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.92f;

    // No GlyphRanges: the Vulkan backend has ImGuiBackendFlags_RendererHasTextures (ImGui 1.92), so
    // the atlas loads each glyph of the font when first drawn (Cyrillic, ñ ç ü ß ą ł ş ğ ı, —).
    // Ranges would only limit that.
    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(bb_font_ttf),
                                   int(bb_font_ttf_end - bb_font_ttf), 18.0f, &font_config);
    // bbport: the Chinese characters DejaVu Sans lacks, merged into it (loaded when first drawn too).
    ImFontConfig cjk_config;
    cjk_config.FontDataOwnedByAtlas = false;
    cjk_config.MergeMode = true;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(bb_font_cjk),
                                   int(bb_font_cjk_end - bb_font_cjk), 18.0f, &cjk_config);

    const vk::Instance vk_instance = instance.GetInstance();
    ImGui_ImplVulkan_LoadFunctions(
        instance.ApiVersion(),
        [](const char* name, void* user) {
            return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(
                *static_cast<const vk::Instance*>(user), name);
        },
        const_cast<vk::Instance*>(&vk_instance));

    const VkFormat color_format = static_cast<VkFormat>(format);
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = instance.ApiVersion();
    info.Instance = vk_instance;
    info.PhysicalDevice = instance.GetPhysicalDevice();
    info.Device = instance.GetDevice();
    info.QueueFamily = instance.GetGraphicsQueueFamilyIndex();
    info.Queue = instance.GetGraphicsQueue();
    info.DescriptorPoolSize = 16;
    info.MinImageCount = std::max(image_count, 2u);
    info.ImageCount = std::max(image_count, 2u);
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
    };
    if (!ImGui_ImplVulkan_Init(&info)) {
        std::printf("Overlay: ImGui Vulkan backend init failed\n");
        ImGui::DestroyContext();
        return;
    }
    initialized = true;
    std::printf("Overlay: menu ready (%s)\n", MenuKeys() == OverlayKeys::Both     ? "Insert or L3+R3"
                                             : MenuKeys() == OverlayKeys::Insert ? "Insert; L3+R3 off"
                                                                                 : "shortcuts off");
}

void UpdateTextInput(SDL_Window* window) {
    bool want = false;
    {
        std::scoped_lock lock{imgui_mutex};
        want = initialized && menu_open && ImGui::GetIO().WantTextInput;
    }
    if (want != SDL_TextInputActive(window)) {
        if (want) {
            SDL_StartTextInput(window);
        } else {
            SDL_StopTextInput(window);
        }
    }
}

bool HandleEvent(const SDL_Event& event) {
    std::scoped_lock lock{imgui_mutex};
    if (!initialized) {
        return false;
    }
    ImGuiIO& io = ImGui::GetIO();
    const bool is_open = menu_open;
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = event.type == SDL_EVENT_KEY_DOWN;
        if (down && !event.key.repeat &&
            ((event.key.key == SDLK_INSERT && (is_open || MenuKeys() != OverlayKeys::Off)) ||
             // Esc in a text field (the search, a typed value) is the field's: it clears, or leaves.
             (is_open && event.key.key == SDLK_ESCAPE && !io.WantTextInput))) {
            SetOpen(event.key.key == SDLK_INSERT ? !is_open : false);
            return true;
        }
        if (!is_open) {
            return false;
        }
        io.AddKeyEvent(ImGuiMod_Ctrl, (event.key.mod & SDL_KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (event.key.mod & SDL_KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (event.key.mod & SDL_KMOD_ALT) != 0);
        if (const ImGuiKey key = KeyFromSdl(event.key.key); key != ImGuiKey_None) {
            io.AddKeyEvent(key, down);
        }
        return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        const u8 button = event.gbutton.button;
        if (button == SDL_GAMEPAD_BUTTON_LEFT_STICK) {
            l3_down = down;
        } else if (button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
            r3_down = down;
        }
        if (down && l3_down && r3_down && (is_open || MenuKeys() == OverlayKeys::Both)) {
            SetOpen(!is_open);
            return true;
        }
        if (!is_open) {
            return false;
        }
        if (const ImGuiKey key = KeyFromGamepad(button); key != ImGuiKey_None) {
            io.AddKeyEvent(key, down);
        }
        return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        // The triggers switch the tabs (L2 / R2), pressed past half way.
        if (!is_open) {
            return false;
        }
        const u8 axis = event.gaxis.axis;
        if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
            const float value = std::clamp(float(event.gaxis.value) / 32767.0f, 0.0f, 1.0f);
            io.AddKeyAnalogEvent(axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? ImGuiKey_GamepadL2 : ImGuiKey_GamepadR2,
                                 value > 0.5f, value);
        }
        return true;
    }
    case SDL_EVENT_TEXT_INPUT: {
        // Typed characters (Ctrl+click on a slider, a text field): key events alone erase but
        // do not type. SDL sends them while text input is on (UpdateTextInput).
        if (!is_open) {
            return false;
        }
        io.AddInputCharactersUTF8(event.text.text);
        return true;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        if (!is_open) {
            return false;
        }
        const float density = PixelDensity(event.motion.windowID);
        io.AddMousePosEvent(event.motion.x * density, event.motion.y * density);
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (!is_open) {
            return false;
        }
        const int button = event.button.button == SDL_BUTTON_LEFT    ? 0
                           : event.button.button == SDL_BUTTON_RIGHT  ? 1
                           : event.button.button == SDL_BUTTON_MIDDLE ? 2
                                                                      : -1;
        if (button >= 0) {
            io.AddMouseButtonEvent(button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        }
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        if (!is_open) {
            return false;
        }
        io.AddMouseWheelEvent(event.wheel.x, event.wheel.y);
        return true;
    default:
        return false;
    }
}

bool Visible() {
    return initialized &&
           (menu_open || prompt_active || BbSettings::Get().show_fps || CompileNoteActive());
}

bool MenuOpen() {
    return menu_open;
}

bool CapturesInput() {
    // The text dialog too: keys typed into it (Backspace is the touchpad) stay out of the game.
    return menu_open || prompt_active;
}

void Render(vk::CommandBuffer cmdbuf, vk::ImageView view, vk::Extent2D extent) {
    // Present interval for the FPS readout (measured also while nothing is drawn).
    const auto now = std::chrono::steady_clock::now();
    const float ms = std::chrono::duration<float, std::milli>(now - last_present).count();
    last_present = now;
    if (ms > 0.0f && ms < 1000.0f) {
        frame_ms_avg = frame_ms_avg == 0.0f ? ms : frame_ms_avg * 0.95f + ms * 0.05f;
    }
    if (!Visible()) {
        return;
    }
    std::scoped_lock lock{imgui_mutex};
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(extent.width), float(extent.height));
    io.DeltaTime = ms > 0.0f && ms < 1000.0f ? ms / 1000.0f : 1.0f / 60.0f;
    // UI scale follows the display height (1080p = 1).
    const float scale = std::max(float(extent.height) / 1080.0f, 0.75f);
    if (std::abs(scale - base_scale) > 0.01f) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(scale / base_scale);
        style.FontScaleMain = scale;
        base_scale = scale;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    if (menu_open) {
        Menu();
    }
    fps_box_min = fps_box_max = ImVec2{};
    if (BbSettings::Get().show_fps && !menu_open) {
        FpsCounter();
    }
    if (!menu_open && CompileNoteActive()) {
        CompileNote();
    }
    if (prompt_active && !menu_open) {
        TextPrompt();
    }
    ImGui::Render();

    const vk::RenderingAttachmentInfo attachment{
        .imageView = view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    cmdbuf.beginRendering(vk::RenderingInfo{
        .renderArea = {{0, 0}, extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment,
    });
    {
        // Font atlas uploads submit to the graphics queue themselves.
        std::scoped_lock submit_lock{Vulkan::Scheduler::submit_mutex};
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdbuf);
    }
    cmdbuf.endRendering();
}

} // namespace BbOverlay

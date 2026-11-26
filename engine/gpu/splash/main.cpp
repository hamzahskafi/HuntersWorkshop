// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.6 beta 17: bb-splash, the start's splash. bb-start (run.sh on Linux) starts it
// first, so it shows as soon as the game is started, from the launcher or from a Steam shortcut, and
// it shows the step the start is on (status.h) in the player's language until the game window shows
// at the game's first frame. A small window of its own, centred on the game's monitor, with the
// game's own art (sce_sys/pic1 in the player's game folder), drawn with SDL's renderer apart from the
// engine; its text with the menus' font (DejaVu Sans, stb_truetype as Dear ImGui carries it).
//
//     bb-splash --status FILE --parent PID [--game-dir DIR]
//
// It closes when the engine writes "close", when the program that started it ends (a start that
// failed, a crash), or when its window is closed; it removes the status file as it goes.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

#include <SDL3/SDL.h>

#include "bbport_settings.h"
#include "splash_art.h"
#include "status_file.h"

// stb_truetype's functions this program does not call: static, unused.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "imstb_truetype.h"
#pragma GCC diagnostic pop

// DejaVu Sans (Latin with Latin Extended, Greek, Cyrillic), embedded (third_party/fonts, Bitstream
// Vera license), as the menus have it (bbport_overlay.cpp).
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

namespace {

namespace fs = std::filesystem;
using BbSplashStatus::Status;
using BbSplashStatus::Step;

/// A line into the session log (the start's output, which this program shares). Written directly: a
/// Windows program without a console has no C runtime output of its own to rely on.
void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));
void Log(const char* format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(text, sizeof text - 1, format, args);
    va_end(args);
    if (n <= 0) return;
    size_t size = std::min(size_t(n), sizeof text - 2);
    text[size++] = '\n';
#ifdef _WIN32
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, text, DWORD(size), &written, nullptr);
#else
    [[maybe_unused]] const ssize_t written = ::write(STDOUT_FILENO, text, size);
#endif
}

struct Options {
    fs::path status, game;
    unsigned long parent = 0;
};

std::optional<Options> ParseArguments(const std::vector<fs::path>& args) {
    Options options;
    for (size_t i = 1; i + 1 < args.size(); i += 2) {
        const auto name = args[i].native();
        const fs::path& value = args[i + 1];
        if (name == fs::path("--status").native()) {
            options.status = value;
        } else if (name == fs::path("--game-dir").native()) {
            options.game = value;
        } else if (name == fs::path("--parent").native()) {
            options.parent = std::strtoul(value.string().c_str(), nullptr, 10);
        } else {
            return std::nullopt;
        }
    }
    if (options.status.empty() || !options.parent) return std::nullopt;
    return options;
}

/// The program that started this one (bb-start, or run.sh's process, which becomes bb-probe).
class Parent {
public:
    bool Watch(unsigned long pid) {
#ifdef _WIN32
        process = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
        return process != nullptr;
#else
        id = pid_t(pid);
        child = getppid() == id; // run.sh's: a child, which tells its parent's end without a doubt
        return Alive();
#endif
    }
    bool Alive() const {
#ifdef _WIN32
        return WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
#else
        return child ? getppid() == id : (::kill(id, 0) == 0 || errno == EPERM);
#endif
    }

private:
#ifdef _WIN32
    HANDLE process = nullptr;
#else
    pid_t id = 0;
    bool child = false;
#endif
};

struct StepText {
    const char *english, *russian, *note_english, *note_russian;
};
// By Step (status.h); the other languages' text is in the menus' tables (bbport_menu_i18n.cpp).
constexpr StepText kStepTexts[] = {
    {"Preparing the game", "Подготовка игры", nullptr, nullptr},
    {"Starting the engine", "Запуск движка", nullptr, nullptr},
    {"Preparing the shader cache for this graphics card", "Подготовка кэша шейдеров для этой видеокарты",
     "Once after a graphics card or driver change.", "Один раз после смены видеокарты или драйвера."},
    {"Updating the shader cache for this version", "Обновление кэша шейдеров для этой версии",
     "Once after an update.", "Один раз после обновления."},
    {"Installing the shader catalog", "Установка каталога шейдеров", "Once for each catalog update.",
     "Один раз для каждого обновления каталога."},
    {"Building shaders", "Сборка шейдеров", nullptr, nullptr},
    {"Starting the game", "Запуск игры", nullptr, nullptr},
};
static_assert(sizeof(kStepTexts) / sizeof(kStepTexts[0]) == size_t(Step::Close));

/// The menus' language (config.ini, else the system's); Chinese in English here, the font having no
/// Chinese characters.
const char* Words(const char* english, const char* russian) {
    if (!english) return "";
    if (BbSettings::Get().menu_language == BbSettings::MenuLanguage::ChineseSimplified) return english;
    return BbSettings::MenuText(english, russian ? russian : english);
}

std::vector<std::uint8_t> ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// The player's own background art for the game: sce_sys/pic1.png, else pic1.dds.
bool LoadArt(const fs::path& game, BbSplashArt::Image& image) {
    if (game.empty()) return false;
    const fs::path sce_sys = game / "sce_sys";
    const auto png = ReadFile(sce_sys / "pic1.png");
    if (!png.empty() && BbSplashArt::DecodePng(png, image)) return true;
    const auto dds = ReadFile(sce_sys / "pic1.dds");
    return !dds.empty() && BbSplashArt::DecodeDdsBc1(dds, image);
}

/// The monitor the launcher chose for the game (BB_MONITOR_BOUNDS, as the game window takes it:
/// window.cpp), else the primary one.
SDL_DisplayID GameDisplay() {
    if (const char* selection = std::getenv("BB_MONITOR_BOUNDS"); selection && *selection) {
        int x, y, w, h;
        char trailing;
        if (SDL_sscanf(selection, "%d,%d,%d,%d%c", &x, &y, &w, &h, &trailing) == 4 && w > 0 && h > 0) {
            const SDL_Point point{x + w / 2, y + h / 2};
            SDL_Rect bounds{};
            if (const SDL_DisplayID selected = SDL_GetDisplayForPoint(&point);
                selected && SDL_GetDisplayBounds(selected, &bounds) && bounds.x == x && bounds.y == y &&
                bounds.w == w && bounds.h == h) {
                return selected;
            }
        }
    }
    return SDL_GetPrimaryDisplay();
}

#ifdef _WIN32
HWND WindowHandle(SDL_Window* window) {
    return static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
}

/// The game's icon (bloodborne.ico beside the engine's programs, engine\out: this program is in its
/// tools folder), as the game window has it.
void SetIcon(SDL_Window* window) {
    const HWND hwnd = WindowHandle(window);
    wchar_t exe[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (!hwnd || !n || n >= MAX_PATH) return;
    const fs::path here = fs::path(exe).parent_path();
    std::error_code error;
    const fs::path icon = fs::is_regular_file(here / L"bloodborne.ico", error) ? here / L"bloodborne.ico"
                                                                              : here.parent_path() / L"bloodborne.ico";
    for (const bool small : {false, true}) {
        const int cx = GetSystemMetrics(small ? SM_CXSMICON : SM_CXICON);
        const int cy = GetSystemMetrics(small ? SM_CYSMICON : SM_CYICON);
        if (const auto handle = static_cast<HICON>(
                LoadImageW(nullptr, icon.c_str(), IMAGE_ICON, cx, cy, LR_LOADFROMFILE | LR_DEFAULTCOLOR))) {
            SendMessageW(hwnd, WM_SETICON, small ? ICON_SMALL : ICON_BIG, reinterpret_cast<LPARAM>(handle));
        }
    }
}
#endif

class Splash {
public:
    bool Open(const fs::path& game);
    void Draw(const Status& status);
    void Close();
    SDL_Window* Window() const { return window; }

private:
    struct Text {
        std::string text;
        int px = 0;
        SDL_Texture* texture = nullptr;
        int width = 0, height = 0, baseline = 0;
    };
    void MakeText(Text& t, const std::string& text, int px);
    void DrawText(const Text& t, float x, float y, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
    static void Release(Text& t);

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* art = nullptr;
    float art_aspect = 16.0f / 9.0f;
    stbtt_fontinfo font{};
    bool font_ok = false;
    Text brand, step, note, count;
    std::chrono::steady_clock::time_point last_draw{}, opened{};
};

std::vector<int> CodePoints(const std::string& text) {
    std::vector<int> out;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = text[i];
        int cp = c, extra = 0;
        if (c >= 0xF0) cp = c & 0x07, extra = 3;
        else if (c >= 0xE0) cp = c & 0x0F, extra = 2;
        else if (c >= 0xC0) cp = c & 0x1F, extra = 1;
        ++i;
        for (; extra > 0 && i < text.size(); --extra, ++i) cp = (cp << 6) | (text[i] & 0x3F);
        out.push_back(cp);
    }
    return out;
}

/// `text` at `px` pixels as a white texture with the glyphs' coverage as alpha (made again only when
/// either changes).
void Splash::MakeText(Text& t, const std::string& text, int px) {
    if (t.texture && t.text == text && t.px == px) return;
    Release(t);
    t.text = text;
    t.px = px;
    if (!font_ok || text.empty() || px <= 0) return;
    const float scale = stbtt_ScaleForPixelHeight(&font, float(px));
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &gap);
    const int above = int(std::ceil(ascent * scale)), below = int(std::ceil(-descent * scale));
    const auto cps = CodePoints(text);
    float width = 0;
    for (size_t i = 0; i < cps.size(); ++i) {
        int advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&font, cps[i], &advance, &lsb);
        width += advance * scale;
        if (i + 1 < cps.size()) width += stbtt_GetCodepointKernAdvance(&font, cps[i], cps[i + 1]) * scale;
    }
    const int w = int(std::ceil(width)) + 4, h = above + below + 2;
    std::vector<std::uint8_t> coverage(size_t(w) * h, 0), glyph;
    float x = 2;
    for (size_t i = 0; i < cps.size(); ++i) {
        const float shift = x - std::floor(x);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBoxSubpixel(&font, cps[i], scale, scale, shift, 0, &x0, &y0, &x1, &y1);
        const int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            glyph.assign(size_t(gw) * gh, 0);
            stbtt_MakeCodepointBitmapSubpixel(&font, glyph.data(), gw, gh, gw, scale, scale, shift, 0, cps[i]);
            for (int gy = 0; gy < gh; ++gy) {
                const int py = above + 1 + y0 + gy;
                if (py < 0 || py >= h) continue;
                for (int gx = 0; gx < gw; ++gx) {
                    const int px_ = int(std::floor(x)) + x0 + gx;
                    if (px_ < 0 || px_ >= w) continue;
                    auto& dst = coverage[size_t(py) * w + px_];
                    dst = std::max(dst, glyph[size_t(gy) * gw + gx]);
                }
            }
        }
        int advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&font, cps[i], &advance, &lsb);
        x += advance * scale;
        if (i + 1 < cps.size()) x += stbtt_GetCodepointKernAdvance(&font, cps[i], cps[i + 1]) * scale;
    }
    std::vector<std::uint8_t> rgba(size_t(w) * h * 4);
    for (size_t i = 0; i < coverage.size(); ++i) {
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = coverage[i];
    }
    t.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t.texture) return;
    SDL_UpdateTexture(t.texture, nullptr, rgba.data(), w * 4);
    SDL_SetTextureBlendMode(t.texture, SDL_BLENDMODE_BLEND);
    t.width = w;
    t.height = h;
    t.baseline = above + 1;
}

void Splash::DrawText(const Text& t, float x, float y, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    if (!t.texture) return;
    SDL_SetTextureColorMod(t.texture, r, g, b);
    SDL_SetTextureAlphaMod(t.texture, a);
    const SDL_FRect dst{std::round(x), std::round(y), float(t.width), float(t.height)};
    SDL_RenderTexture(renderer, t.texture, nullptr, &dst);
}

void Splash::Release(Text& t) {
    if (t.texture) SDL_DestroyTexture(t.texture);
    t = {};
}

/// 1234567 as "1,234,567".
std::string Count(std::uint64_t n) {
    std::string digits = std::to_string(n), out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

bool Splash::Open(const fs::path& game) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        Log("Splash: not shown (SDL video: %s)", SDL_GetError());
        return false;
    }
    const SDL_DisplayID display = GameDisplay();
    SDL_Rect bounds{0, 0, 1920, 1080};
    SDL_GetDisplayBounds(display, &bounds);
    // About two fifths of the monitor's width, 16:9 as the art.
    const int width = std::clamp(int(bounds.w * 0.42f), std::min(560, bounds.w), 1280);
    const int height = width * 9 / 16;
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Bloodborne");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
    // Shown once its first picture is drawn (no blank window first).
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
    window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!window) {
        Log("Splash: not shown (window: %s)", SDL_GetError());
        SDL_Quit();
        return false;
    }
#ifdef _WIN32
    SetIcon(window);
#endif
    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) renderer = SDL_CreateRenderer(window, SDL_SOFTWARE_RENDERER);
    if (!renderer) {
        Log("Splash: not shown (renderer: %s)", SDL_GetError());
        SDL_DestroyWindow(window);
        window = nullptr;
        SDL_Quit();
        return false;
    }
    BbSplashArt::Image image;
    if (LoadArt(game, image)) {
        art = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(image.width),
                                int(image.height));
        if (art) {
            SDL_UpdateTexture(art, nullptr, image.rgba.data(), int(image.width) * 4);
            SDL_SetTextureScaleMode(art, SDL_SCALEMODE_LINEAR);
            art_aspect = float(image.width) / float(image.height);
        }
    }
    const size_t font_size = size_t(bb_font_ttf_end - bb_font_ttf);
    font_ok = font_size > 0 && stbtt_InitFont(&font, bb_font_ttf, stbtt_GetFontOffsetForIndex(bb_font_ttf, 0)) != 0;
    opened = std::chrono::steady_clock::now();
    const char* driver = SDL_GetRendererName(renderer);
    Log("Splash: %dx%d on display %u (%s; %s)", width, height, unsigned(display), driver ? driver : "?",
        art ? "the game's sce_sys/pic1" : "no art in the game folder");
    return true;
}

void Splash::Draw(const Status& status) {
    if (!window || !renderer) return;
    const auto now = std::chrono::steady_clock::now();
    if (last_draw.time_since_epoch().count() && now - last_draw < std::chrono::milliseconds(33)) return;
    const bool first = !last_draw.time_since_epoch().count();
    last_draw = now;
    int W = 0, H = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &W, &H);
    if (W <= 0 || H <= 0) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 10, 13, 16, 255);
    SDL_RenderClear(renderer);
    if (art) { // covering the window, centred
        float w = float(W), h = float(W) / art_aspect;
        if (h < float(H)) {
            h = float(H);
            w = h * art_aspect;
        }
        const SDL_FRect dst{(float(W) - w) * 0.5f, (float(H) - h) * 0.5f, w, h};
        SDL_RenderTexture(renderer, art, nullptr, &dst);
    }
    // The bottom darkened under the text.
    {
        const float top = float(H) * 0.58f;
        const SDL_FColor clear{0, 0, 0, 0}, dark{0, 0, 0, 0.88f};
        const SDL_Vertex v[4] = {{{0, top}, clear, {}}, {{float(W), top}, clear, {}},
                                 {{float(W), float(H)}, dark, {}}, {{0, float(H)}, dark, {}}};
        const int indices[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(renderer, nullptr, v, 4, indices, 6);
    }
    const StepText& words = kStepTexts[std::min(int(status.step), int(Step::Starting))];
    const float margin = std::round(float(W) * 0.045f);
    MakeText(brand, "Hunter's Workshop", std::max(11, int(H * 0.032f)));
    MakeText(step, Words(words.english, words.russian), std::max(13, int(H * 0.045f)));
    MakeText(note, Words(words.note_english, words.note_russian), std::max(11, int(H * 0.034f)));
    MakeText(count, status.total ? Count(std::min(status.done, status.total)) + " / " + Count(status.total) : "",
             std::max(11, int(H * 0.036f)));
    DrawText(brand, margin * 0.8f, margin * 0.6f, 226, 218, 198, 150);
    // From the bottom: the note, the bar, the step with its count at the right.
    float y = float(H) - margin * 0.75f;
    if (note.texture) {
        y -= float(note.height);
        DrawText(note, margin, y, 196, 188, 170, 200);
        y -= float(H) * 0.018f;
    }
    const float bar_h = std::max(3.0f, std::round(float(H) * 0.008f));
    y -= bar_h;
    const float bar_x = margin, bar_w = float(W) - 2 * margin;
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 40);
    const SDL_FRect track{bar_x, y, bar_w, bar_h};
    SDL_RenderFillRect(renderer, &track);
    SDL_SetRenderDrawColor(renderer, 218, 207, 182, 235);
    if (status.total) {
        const float part = float(std::min(status.done, status.total)) / float(status.total);
        const SDL_FRect fill{bar_x, y, bar_w * part, bar_h};
        SDL_RenderFillRect(renderer, &fill);
    } else { // no count: a quarter of the bar sweeping across
        const float t = std::chrono::duration<float>(now - opened).count();
        const float phase = t / 1.6f - std::floor(t / 1.6f);
        const float seg = bar_w * 0.25f;
        const float start = bar_x - seg + (bar_w + seg) * phase;
        const float left = std::max(bar_x, start), right = std::min(bar_x + bar_w, start + seg);
        if (right > left) {
            const SDL_FRect fill{left, y, right - left, bar_h};
            SDL_RenderFillRect(renderer, &fill);
        }
    }
    y -= float(H) * 0.02f;
    if (step.texture) {
        const float step_y = y - float(step.height);
        DrawText(step, margin - 2.0f, step_y, 236, 228, 208, 255);
        if (count.texture) {
            DrawText(count, float(W) - margin - float(count.width) + 2.0f,
                     step_y + float(step.baseline - count.baseline), 220, 212, 192, 230);
        }
    }
    SDL_RenderPresent(renderer);
    if (first) {
        SDL_ShowWindow(window);
        SDL_RaiseWindow(window);
    }
#ifdef _WIN32
    // In front, this program may hand the foreground on: the game window, from bb-probe, then comes
    // to the front when it shows (Windows lets only the foreground program's choice through).
    if (GetForegroundWindow() == WindowHandle(window)) AllowSetForegroundWindow(ASFW_ANY);
#endif
}

void Splash::Close() {
    if (!window) return;
    for (Text* t : {&brand, &step, &note, &count}) Release(*t);
    if (art) SDL_DestroyTexture(art);
    art = nullptr;
    SDL_DestroyRenderer(renderer);
    renderer = nullptr;
    SDL_DestroyWindow(window);
    window = nullptr;
    SDL_Quit();
}

int Run(const std::vector<fs::path>& args) {
    const auto options = ParseArguments(args);
    if (!options) {
        Log("usage: bb-splash --status FILE --parent PID [--game-dir DIR]");
        return 2;
    }
    BbSplashStatus::Reader file(options->status);
    Parent parent;
    if (!parent.Watch(options->parent)) {
        file.Remove();
        return 0;
    }
    BbSettings::Get().menu_language = BbSettings::ReadMenuLanguage();
    Splash splash;
    if (!splash.Open(options->game)) {
        file.Remove(); // the engine shows its window at once without it (BbStartup::SplashRunning)
        return 1;
    }
    const auto opened = std::chrono::steady_clock::now();
    Status status;
    const char* why = nullptr;
    while (!why) {
        for (SDL_Event event; SDL_PollEvent(&event);) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) why = "closed";
        }
        if (const auto read = file.Read()) status = *read;
        if (status.step == Step::Close) {
            why = "the game window shown";
        } else if (!parent.Alive()) {
            why = "the start ended";
        } else if (!why) {
            splash.Draw(status);
            SDL_WaitEventTimeout(nullptr, 33);
        }
    }
    splash.Close();
    file.Remove();
    Log("Splash: closed after %.1f s (%s)",
        std::chrono::duration<double>(std::chrono::steady_clock::now() - opened).count(), why);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<fs::path> args;
#ifdef _WIN32
    // The arguments as Windows has them (wide): a game folder outside the system's code page.
    (void)argc;
    (void)argv;
    int count = 0;
    if (wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 0; i < count; ++i) args.emplace_back(wide[i]);
        LocalFree(wide);
    }
#else
    signal(SIGPIPE, SIG_IGN); // the log's pipe gone with the start: no end for this program
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    return Run(args);
}

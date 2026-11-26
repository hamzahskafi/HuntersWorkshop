// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.6 beta 17: the start's splash (splash.h). Drawn with SDL's own renderer, apart
// from the game's Vulkan device, so it goes on drawing while the start builds shaders and cannot hold
// the start up; its text with the menus' font (stb_truetype, as Dear ImGui carries it).

#include "splash.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "bbport_overlay.h"
#include "engine/splash_art.h"
#include "engine/startup.h"
#include "sdl_window.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "imstb_truetype.h"

namespace BbSplash {

namespace {

struct Text {
    std::string text;
    int px = 0;
    SDL_Texture* texture = nullptr;
    int width = 0, height = 0, baseline = 0;
};

SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;
SDL_Texture* g_art = nullptr;
float g_art_aspect = 16.0f / 9.0f;
stbtt_fontinfo g_font{};
bool g_font_ok = false;
Text g_brand, g_step, g_note, g_count;
std::chrono::steady_clock::time_point g_last_draw{}, g_opened{};

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// The player's own background art for the game: sce_sys/pic1.png, else pic1.dds.
bool LoadArt(BbSplashArt::Image& image) {
    std::filesystem::path game = BbStartup::EnvPath("BB_GAME_DIR");
    if (game.empty()) game = BbStartup::EnvPath("BB_GAME_MERGED"); // the Linux start's
    if (game.empty()) return false;
    const std::filesystem::path sce_sys = game / "sce_sys";
    const auto png = ReadFile(sce_sys / "pic1.png");
    if (!png.empty() && BbSplashArt::DecodePng(png, image)) return true;
    const auto dds = ReadFile(sce_sys / "pic1.dds");
    return !dds.empty() && BbSplashArt::DecodeDdsBc1(dds, image);
}

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
void MakeText(Text& t, const std::string& text, int px) {
    if (t.texture && t.text == text && t.px == px) return;
    if (t.texture) SDL_DestroyTexture(t.texture);
    t = {};
    t.text = text;
    t.px = px;
    if (!g_font_ok || text.empty() || px <= 0) return;
    const float scale = stbtt_ScaleForPixelHeight(&g_font, float(px));
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &gap);
    const int above = int(std::ceil(ascent * scale)), below = int(std::ceil(-descent * scale));
    const auto cps = CodePoints(text);
    float width = 0;
    for (size_t i = 0; i < cps.size(); ++i) {
        int advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&g_font, cps[i], &advance, &lsb);
        width += advance * scale;
        if (i + 1 < cps.size()) width += stbtt_GetCodepointKernAdvance(&g_font, cps[i], cps[i + 1]) * scale;
    }
    const int w = int(std::ceil(width)) + 4, h = above + below + 2;
    std::vector<std::uint8_t> coverage(size_t(w) * h, 0), glyph;
    float x = 2;
    for (size_t i = 0; i < cps.size(); ++i) {
        const float shift = x - std::floor(x);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBoxSubpixel(&g_font, cps[i], scale, scale, shift, 0, &x0, &y0, &x1, &y1);
        const int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            glyph.assign(size_t(gw) * gh, 0);
            stbtt_MakeCodepointBitmapSubpixel(&g_font, glyph.data(), gw, gh, gw, scale, scale, shift, 0, cps[i]);
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
        stbtt_GetCodepointHMetrics(&g_font, cps[i], &advance, &lsb);
        x += advance * scale;
        if (i + 1 < cps.size()) x += stbtt_GetCodepointKernAdvance(&g_font, cps[i], cps[i + 1]) * scale;
    }
    std::vector<std::uint8_t> rgba(size_t(w) * h * 4);
    for (size_t i = 0; i < coverage.size(); ++i) {
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = coverage[i];
    }
    t.texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t.texture) return;
    SDL_UpdateTexture(t.texture, nullptr, rgba.data(), w * 4);
    SDL_SetTextureBlendMode(t.texture, SDL_BLENDMODE_BLEND);
    t.width = w;
    t.height = h;
    t.baseline = above + 1;
}

void DrawText(const Text& t, float x, float y, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    if (!t.texture) return;
    SDL_SetTextureColorMod(t.texture, r, g, b);
    SDL_SetTextureAlphaMod(t.texture, a);
    const SDL_FRect dst{std::round(x), std::round(y), float(t.width), float(t.height)};
    SDL_RenderTexture(g_renderer, t.texture, nullptr, &dst);
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

void Release(Text& t) {
    if (t.texture) SDL_DestroyTexture(t.texture);
    t = {};
}

} // namespace

bool Open(const char* title) {
    if (const char* env = std::getenv("BB_SPLASH"); env && env[0] == '0') return false;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::printf("Splash: not shown (SDL video: %s)\n", SDL_GetError());
        return false;
    }
    SDL_DisplayID display = Frontend::GameDisplay(false);
    if (!display) display = SDL_GetPrimaryDisplay();
    SDL_Rect bounds{0, 0, 1920, 1080};
    SDL_GetDisplayBounds(display, &bounds);
    // About two fifths of the monitor's width, 16:9 as the art.
    const int width = std::clamp(int(bounds.w * 0.42f), std::min(560, bounds.w), 1280);
    const int height = width * 9 / 16;
    g_window = SDL_CreateWindow(title, width, height, SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN);
    if (!g_window) {
        std::printf("Splash: not shown (window: %s)\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    Frontend::SetWindowIcon(g_window);
    g_renderer = SDL_CreateRenderer(g_window, nullptr);
    if (!g_renderer) g_renderer = SDL_CreateRenderer(g_window, SDL_SOFTWARE_RENDERER);
    if (!g_renderer) {
        std::printf("Splash: not shown (renderer: %s)\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    BbSplashArt::Image art;
    const bool have_art = LoadArt(art);
    if (have_art) {
        g_art = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(art.width),
                                  int(art.height));
        if (g_art) {
            SDL_UpdateTexture(g_art, nullptr, art.rgba.data(), int(art.width) * 4);
            SDL_SetTextureScaleMode(g_art, SDL_SCALEMODE_LINEAR);
            g_art_aspect = float(art.width) / float(art.height);
        }
    }
    const auto font = BbOverlay::UiFont();
    g_font_ok = !font.empty() && stbtt_InitFont(&g_font, font.data(), stbtt_GetFontOffsetForIndex(font.data(), 0));
    BbStartup::SetStep("Starting the engine", "Запуск движка");
    SDL_ShowWindow(g_window);
    g_opened = std::chrono::steady_clock::now();
    g_last_draw = {};
    Draw();
    const char* driver = SDL_GetRendererName(g_renderer);
    std::printf("Splash: %dx%d on display %u (%s; %s)\n", width, height, unsigned(display),
                driver ? driver : "?", g_art ? "the game's sce_sys/pic1" : "no art in the game folder");
    std::fflush(stdout);
    return true;
}

void Draw() {
    if (!g_window || !g_renderer) return;
    const auto now = std::chrono::steady_clock::now();
    if (g_last_draw.time_since_epoch().count() && now - g_last_draw < std::chrono::milliseconds(33)) return;
    g_last_draw = now;
    int W = 0, H = 0;
    SDL_GetCurrentRenderOutputSize(g_renderer, &W, &H);
    if (W <= 0 || H <= 0) return;
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, 10, 13, 16, 255);
    SDL_RenderClear(g_renderer);
    if (g_art) { // covering the window, centred
        float w = float(W), h = float(W) / g_art_aspect;
        if (h < float(H)) {
            h = float(H);
            w = h * g_art_aspect;
        }
        const SDL_FRect dst{(float(W) - w) * 0.5f, (float(H) - h) * 0.5f, w, h};
        SDL_RenderTexture(g_renderer, g_art, nullptr, &dst);
    }
    // The bottom darkened under the text.
    {
        const float top = float(H) * 0.58f;
        const SDL_FColor clear{0, 0, 0, 0}, dark{0, 0, 0, 0.88f};
        const SDL_Vertex v[4] = {{{0, top}, clear, {}}, {{float(W), top}, clear, {}},
                                 {{float(W), float(H)}, dark, {}}, {{0, float(H)}, dark, {}}};
        const int indices[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(g_renderer, nullptr, v, 4, indices, 6);
    }
    const auto status = BbStartup::Get();
    const float margin = std::round(float(W) * 0.045f);
    MakeText(g_brand, "Hunter's Workshop", std::max(11, int(H * 0.032f)));
    MakeText(g_step, status.step, std::max(13, int(H * 0.045f)));
    MakeText(g_note, status.note, std::max(11, int(H * 0.034f)));
    MakeText(g_count, status.total ? Count(std::min(status.done, status.total)) + " / " + Count(status.total) : "",
             std::max(11, int(H * 0.036f)));
    DrawText(g_brand, margin * 0.8f, margin * 0.6f, 226, 218, 198, 150);
    // From the bottom: the note, the bar, the step with its count at the right.
    float y = float(H) - margin * 0.75f;
    if (g_note.texture) {
        y -= float(g_note.height);
        DrawText(g_note, margin, y, 196, 188, 170, 200);
        y -= float(H) * 0.018f;
    }
    const float bar_h = std::max(3.0f, std::round(float(H) * 0.008f));
    y -= bar_h;
    const float bar_x = margin, bar_w = float(W) - 2 * margin;
    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 40);
    const SDL_FRect track{bar_x, y, bar_w, bar_h};
    SDL_RenderFillRect(g_renderer, &track);
    SDL_SetRenderDrawColor(g_renderer, 218, 207, 182, 235);
    if (status.total) {
        const float part = float(std::min(status.done, status.total)) / float(status.total);
        const SDL_FRect fill{bar_x, y, bar_w * part, bar_h};
        SDL_RenderFillRect(g_renderer, &fill);
    } else { // no count: a quarter of the bar sweeping across
        const float t = std::chrono::duration<float>(now - g_opened).count();
        const float phase = t / 1.6f - std::floor(t / 1.6f);
        const float seg = bar_w * 0.25f;
        const float start = bar_x - seg + (bar_w + seg) * phase;
        const float left = std::max(bar_x, start), right = std::min(bar_x + bar_w, start + seg);
        if (right > left) {
            const SDL_FRect fill{left, y, right - left, bar_h};
            SDL_RenderFillRect(g_renderer, &fill);
        }
    }
    y -= float(H) * 0.02f;
    if (g_step.texture) {
        const float step_y = y - float(g_step.height);
        DrawText(g_step, margin - 2.0f, step_y, 236, 228, 208, 255);
        if (g_count.texture) {
            DrawText(g_count, float(W) - margin - float(g_count.width) + 2.0f,
                     step_y + float(g_step.baseline - g_count.baseline), 220, 212, 192, 230);
        }
    }
    SDL_RenderPresent(g_renderer);
}

void Close() {
    if (!g_window) return;
    Release(g_brand);
    Release(g_step);
    Release(g_note);
    Release(g_count);
    if (g_art) SDL_DestroyTexture(g_art);
    g_art = nullptr;
    SDL_DestroyRenderer(g_renderer);
    g_renderer = nullptr;
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    const double shown = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_opened).count();
    std::printf("Splash: closed after %.1f s, the game window shown\n", shown);
    std::fflush(stdout);
}

bool IsOpen() {
    return g_window != nullptr;
}

} // namespace BbSplash

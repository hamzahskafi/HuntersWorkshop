// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 17 (shim/engine/splash_art.cpp): the start's splash reads the game's own background art,
// sce_sys/pic1.png or pic1.dds. PNGs of every 8-bit colour type, each row with another filter, are
// written here (miniz) and read back pixel for pixel; a 16-bit and an interlaced PNG, a cut file and
// junk are refused; a DDS of BC1 blocks in both modes is read back.
#include "engine/splash_art.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <miniz.h>
#include <string>
#include <vector>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

void Put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(std::uint8_t(v >> s));
}

void Chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
    Put32(out, std::uint32_t(data.size()));
    std::vector<std::uint8_t> crc_data(type, type + 4);
    crc_data.insert(crc_data.end(), data.begin(), data.end());
    out.insert(out.end(), crc_data.begin(), crc_data.end());
    Put32(out, std::uint32_t(mz_crc32(MZ_CRC32_INIT, crc_data.data(), crc_data.size())));
}

std::uint8_t Paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return std::uint8_t(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

/// A PNG of `rows` (samples of `channels` each), row y filtered with filter y % 5.
std::vector<std::uint8_t> Png(std::uint32_t w, std::uint32_t h, std::uint8_t color, int channels,
                              const std::vector<std::uint8_t>& samples, std::uint8_t depth = 8,
                              std::uint8_t interlace = 0, const std::vector<std::uint8_t>& palette = {},
                              const std::vector<std::uint8_t>& alpha = {}) {
    const size_t stride = size_t(w) * channels;
    std::vector<std::uint8_t> raw;
    for (std::uint32_t y = 0; y < h; ++y) {
        const std::uint8_t filter = std::uint8_t(y % 5);
        raw.push_back(filter);
        const std::uint8_t* cur = &samples[y * stride];
        const std::uint8_t* up = y ? &samples[(y - 1) * stride] : nullptr;
        for (size_t i = 0; i < stride; ++i) {
            const int a = i >= size_t(channels) ? cur[i - channels] : 0, b = up ? up[i] : 0,
                      c = up && i >= size_t(channels) ? up[i - channels] : 0;
            const int predicted = filter == 0 ? 0 : filter == 1 ? a : filter == 2 ? b : filter == 3 ? (a + b) / 2 : Paeth(a, b, c);
            raw.push_back(std::uint8_t(cur[i] - predicted));
        }
    }
    mz_ulong packed_size = mz_compressBound(mz_ulong(raw.size()));
    std::vector<std::uint8_t> packed(packed_size);
    mz_compress(packed.data(), &packed_size, raw.data(), mz_ulong(raw.size()));
    packed.resize(packed_size);
    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> header;
    Put32(header, w);
    Put32(header, h);
    header.insert(header.end(), {depth, color, 0, 0, interlace});
    Chunk(out, "IHDR", header);
    if (!palette.empty()) Chunk(out, "PLTE", palette);
    if (!alpha.empty()) Chunk(out, "tRNS", alpha);
    // The image data in two chunks, as encoders may split it.
    Chunk(out, "IDAT", {packed.begin(), packed.begin() + packed.size() / 2});
    Chunk(out, "IDAT", {packed.begin() + packed.size() / 2, packed.end()});
    Chunk(out, "IEND", {});
    return out;
}

} // namespace

int main() {
    std::srand(17);
    const std::uint32_t w = 37, h = 23;
    const std::vector<std::uint8_t> palette = {10, 20, 30, 200, 100, 50, 0, 255, 128, 77, 88, 99};
    const std::vector<std::uint8_t> palette_alpha = {255, 0, 128};
    for (const std::uint8_t color : {0, 2, 3, 4, 6}) {
        const int channels = color == 2 ? 3 : color == 4 ? 2 : color == 6 ? 4 : 1;
        std::vector<std::uint8_t> samples(size_t(w) * h * channels);
        for (auto& v : samples) v = std::uint8_t(color == 3 ? std::rand() % 4 : std::rand() % 256);
        const auto file = Png(w, h, color, channels, samples, 8, 0, color == 3 ? palette : std::vector<std::uint8_t>{},
                              color == 3 ? palette_alpha : std::vector<std::uint8_t>{});
        BbSplashArt::Image image;
        const bool read = BbSplashArt::DecodePng(file, image);
        bool same = read && image.width == w && image.height == h && image.rgba.size() == size_t(w) * h * 4;
        for (size_t p = 0; same && p < size_t(w) * h; ++p) {
            const std::uint8_t* s = &samples[p * channels];
            const std::uint8_t* d = &image.rgba[p * 4];
            std::uint8_t e[4];
            switch (color) {
            case 0: e[0] = e[1] = e[2] = s[0]; e[3] = 255; break;
            case 2: e[0] = s[0]; e[1] = s[1]; e[2] = s[2]; e[3] = 255; break;
            case 3: std::memcpy(e, &palette[s[0] * 3], 3); e[3] = s[0] < palette_alpha.size() ? palette_alpha[s[0]] : 255; break;
            case 4: e[0] = e[1] = e[2] = s[0]; e[3] = s[1]; break;
            default: std::memcpy(e, s, 4); break;
            }
            same = std::memcmp(d, e, 4) == 0;
        }
        char what[64];
        std::snprintf(what, sizeof what, "a PNG of colour type %u read pixel for pixel", unsigned(color));
        check(same, what);
    }
    {
        std::vector<std::uint8_t> samples(size_t(w) * h * 3, 7);
        BbSplashArt::Image image;
        check(!BbSplashArt::DecodePng(Png(w, h, 2, 3, samples, 16), image), "a 16-bit PNG refused");
        check(!BbSplashArt::DecodePng(Png(w, h, 2, 3, samples, 8, 1), image), "an interlaced PNG refused");
        auto cut = Png(w, h, 2, 3, samples);
        cut.resize(cut.size() - 40);
        check(!BbSplashArt::DecodePng(cut, image), "a cut PNG refused");
        const std::vector<std::uint8_t> junk(200, 0x5A);
        check(!BbSplashArt::DecodePng(junk, image) && !BbSplashArt::DecodeDdsBc1(junk, image), "junk refused");
    }
    {
        // An 8x4 DDS of two BC1 blocks: four colours (c0 > c1), then three and transparent (c0 <= c1).
        std::vector<std::uint8_t> dds(128 + 16, 0);
        std::memcpy(dds.data(), "DDS ", 4);
        const std::uint32_t height = 4, width = 8;
        std::memcpy(&dds[12], &height, 4);
        std::memcpy(&dds[16], &width, 4);
        std::memcpy(&dds[84], "DXT1", 4);
        const std::uint16_t red = 0xF800, blue = 0x001F;
        std::memcpy(&dds[128], &red, 2);  // c0 red > c1 blue
        std::memcpy(&dds[130], &blue, 2);
        const std::uint32_t first = 0b11100100u; // texels 0..3 of row 0: indices 0, 1, 2, 3
        std::memcpy(&dds[132], &first, 4);
        std::memcpy(&dds[136], &blue, 2); // c0 blue <= c1 red: index 3 transparent
        std::memcpy(&dds[138], &red, 2);
        const std::uint32_t second = 0b11100100u;
        std::memcpy(&dds[140], &second, 4);
        BbSplashArt::Image image;
        const bool read = BbSplashArt::DecodeDdsBc1(dds, image);
        check(read && image.width == 8 && image.height == 4, "a BC1 DDS read");
        if (read) {
            const auto at = [&](int x, int y) { return &image.rgba[(size_t(y) * 8 + x) * 4]; };
            check(at(0, 0)[0] == 255 && at(0, 0)[2] == 0 && at(0, 0)[3] == 255, "BC1: colour 0");
            check(at(1, 0)[0] == 0 && at(1, 0)[2] == 255, "BC1: colour 1");
            check(at(2, 0)[0] == 170 && at(2, 0)[2] == 85, "BC1: two thirds of colour 0");
            check(at(3, 0)[0] == 85 && at(3, 0)[2] == 170, "BC1: two thirds of colour 1");
            check(at(6, 0)[0] == 127 && at(6, 0)[2] == 127 && at(6, 0)[3] == 255, "BC1: the half of three-colour mode");
            check(at(7, 0)[3] == 0, "BC1: transparent in three-colour mode");
            check(at(0, 1)[0] == 255 && at(0, 3)[0] == 255, "BC1: index 0 for the other rows");
        }
        auto cut = dds;
        cut.resize(130);
        check(!BbSplashArt::DecodeDdsBc1(cut, image), "a cut DDS refused");
    }
    if (g_fail) {
        std::printf("splash-art-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("splash-art-test: ok\n");
    return 0;
}

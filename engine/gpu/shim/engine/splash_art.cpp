// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.6 beta 17: the splash's picture from the game folder (engine/splash_art.h).

#include "engine/splash_art.h"

#include <cstdlib>
#include <cstring>
#include <miniz.h>

namespace BbSplashArt {

namespace {
std::uint32_t BigEndian32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3];
}

std::uint8_t Paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return std::uint8_t(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}
} // namespace

bool DecodePng(std::span<const std::uint8_t> file, Image& out) {
    static constexpr std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (file.size() < 8 || std::memcmp(file.data(), signature, 8) != 0) return false;
    std::uint32_t width = 0, height = 0;
    std::uint8_t depth = 0, color = 0, interlace = 0;
    std::vector<std::uint8_t> idat, palette, palette_alpha;
    bool header = false;
    for (size_t at = 8; at + 12 <= file.size();) {
        const std::uint32_t length = BigEndian32(&file[at]);
        if (length > file.size() - at - 12) return false;
        const std::uint8_t* type = &file[at + 4];
        const std::uint8_t* data = &file[at + 8];
        if (!std::memcmp(type, "IHDR", 4) && length >= 13) {
            width = BigEndian32(data);
            height = BigEndian32(data + 4);
            depth = data[8];
            color = data[9];
            interlace = data[12];
            header = data[10] == 0 && data[11] == 0;
        } else if (!std::memcmp(type, "PLTE", 4)) {
            palette.assign(data, data + length);
        } else if (!std::memcmp(type, "tRNS", 4)) {
            palette_alpha.assign(data, data + length);
        } else if (!std::memcmp(type, "IDAT", 4)) {
            idat.insert(idat.end(), data, data + length);
        } else if (!std::memcmp(type, "IEND", 4)) {
            break;
        }
        at += size_t(length) + 12;
    }
    int channels = 0;
    switch (color) {
    case 0: channels = 1; break; // grey
    case 2: channels = 3; break; // RGB
    case 3: channels = 1; break; // palette index
    case 4: channels = 2; break; // grey and alpha
    case 6: channels = 4; break; // RGBA
    default: return false;
    }
    if (!header || depth != 8 || interlace != 0 || width == 0 || height == 0 || width > 16384 ||
        height > 16384 || idat.empty() || (color == 3 && palette.size() < 3)) {
        return false;
    }
    const size_t stride = size_t(width) * size_t(channels);
    std::vector<std::uint8_t> raw((stride + 1) * height);
    mz_ulong raw_size = mz_ulong(raw.size());
    if (mz_uncompress(raw.data(), &raw_size, idat.data(), mz_ulong(idat.size())) != MZ_OK ||
        raw_size != raw.size()) {
        return false;
    }
    // The rows' filters undone in place, each against the row above (already undone).
    for (std::uint32_t y = 0; y < height; ++y) {
        std::uint8_t* row = &raw[y * (stride + 1)];
        const std::uint8_t filter = row[0];
        std::uint8_t* cur = row + 1;
        const std::uint8_t* up = y ? &raw[(y - 1) * (stride + 1) + 1] : nullptr;
        for (size_t i = 0; i < stride; ++i) {
            const int a = i >= size_t(channels) ? cur[i - channels] : 0;
            const int b = up ? up[i] : 0;
            const int c = up && i >= size_t(channels) ? up[i - channels] : 0;
            switch (filter) {
            case 0: break;
            case 1: cur[i] = std::uint8_t(cur[i] + a); break;
            case 2: cur[i] = std::uint8_t(cur[i] + b); break;
            case 3: cur[i] = std::uint8_t(cur[i] + ((a + b) >> 1)); break;
            case 4: cur[i] = std::uint8_t(cur[i] + Paeth(a, b, c)); break;
            default: return false;
            }
        }
    }
    out.width = width;
    out.height = height;
    out.rgba.resize(size_t(width) * height * 4);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* src = &raw[y * (stride + 1) + 1];
        std::uint8_t* dst = &out.rgba[size_t(y) * width * 4];
        for (std::uint32_t x = 0; x < width; ++x, dst += 4) {
            switch (color) {
            case 0: dst[0] = dst[1] = dst[2] = src[x]; dst[3] = 255; break;
            case 2: std::memcpy(dst, &src[x * 3], 3); dst[3] = 255; break;
            case 3: {
                const size_t index = src[x];
                if (index * 3 + 2 >= palette.size()) {
                    dst[0] = dst[1] = dst[2] = 0;
                } else {
                    std::memcpy(dst, &palette[index * 3], 3);
                }
                dst[3] = index < palette_alpha.size() ? palette_alpha[index] : 255;
                break;
            }
            case 4: dst[0] = dst[1] = dst[2] = src[x * 2]; dst[3] = src[x * 2 + 1]; break;
            case 6: std::memcpy(dst, &src[x * 4], 4); break;
            }
        }
    }
    return true;
}

bool DecodeDdsBc1(std::span<const std::uint8_t> file, Image& out) {
    // "DDS ", then a 124-byte header: height at 12, width at 16, the pixel format's FourCC at 84.
    if (file.size() < 128 || std::memcmp(file.data(), "DDS ", 4) != 0 || std::memcmp(&file[84], "DXT1", 4) != 0) {
        return false;
    }
    std::uint32_t height = 0, width = 0;
    std::memcpy(&height, &file[12], 4);
    std::memcpy(&width, &file[16], 4);
    if (width == 0 || height == 0 || width > 16384 || height > 16384) return false;
    const std::uint32_t blocks_x = (width + 3) / 4, blocks_y = (height + 3) / 4;
    if (file.size() - 128 < size_t(blocks_x) * blocks_y * 8) return false;
    out.width = width;
    out.height = height;
    out.rgba.assign(size_t(width) * height * 4, 0);
    const auto expand = [](std::uint16_t c, std::uint8_t rgb[3]) {
        rgb[0] = std::uint8_t(((c >> 11) & 31) * 255 / 31);
        rgb[1] = std::uint8_t(((c >> 5) & 63) * 255 / 63);
        rgb[2] = std::uint8_t((c & 31) * 255 / 31);
    };
    const std::uint8_t* block = &file[128];
    for (std::uint32_t by = 0; by < blocks_y; ++by) {
        for (std::uint32_t bx = 0; bx < blocks_x; ++bx, block += 8) {
            std::uint16_t c0, c1;
            std::uint32_t indices;
            std::memcpy(&c0, block, 2);
            std::memcpy(&c1, block + 2, 2);
            std::memcpy(&indices, block + 4, 4);
            std::uint8_t palette[4][4];
            expand(c0, palette[0]);
            expand(c1, palette[1]);
            palette[0][3] = palette[1][3] = 255;
            for (int k = 0; k < 3; ++k) {
                if (c0 > c1) {
                    palette[2][k] = std::uint8_t((2 * palette[0][k] + palette[1][k]) / 3);
                    palette[3][k] = std::uint8_t((palette[0][k] + 2 * palette[1][k]) / 3);
                } else {
                    palette[2][k] = std::uint8_t((palette[0][k] + palette[1][k]) / 2);
                    palette[3][k] = 0;
                }
            }
            palette[2][3] = 255;
            palette[3][3] = c0 > c1 ? 255 : 0;
            for (std::uint32_t py = 0; py < 4; ++py) {
                for (std::uint32_t px = 0; px < 4; ++px) {
                    const std::uint32_t x = bx * 4 + px, y = by * 4 + py;
                    if (x >= width || y >= height) continue;
                    const std::uint32_t index = (indices >> (2 * (py * 4 + px))) & 3;
                    std::memcpy(&out.rgba[(size_t(y) * width + x) * 4], palette[index], 4);
                }
            }
        }
    }
    return true;
}

} // namespace BbSplashArt

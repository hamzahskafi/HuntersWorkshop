// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.6 beta 17: the splash's picture, read from the player's own game folder
// (sce_sys/pic1.png, else sce_sys/pic1.dds: the PS4's background art for the game; nothing of the
// game is shipped). A PNG of 8-bit samples, not interlaced; a DDS of BC1 (DXT1).

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace BbSplashArt {

struct Image {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba; ///< width * height * 4, rows top to bottom
};

/// A PNG: 8-bit grey, grey and alpha, RGB, RGBA or palette, not interlaced. False for anything else.
bool DecodePng(std::span<const std::uint8_t> file, Image& out);
/// A DDS of BC1 (DXT1) blocks, its first mip level. False for anything else.
bool DecodeDdsBc1(std::span<const std::uint8_t> file, Image& out);

} // namespace BbSplashArt

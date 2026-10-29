// SPDX-License-Identifier: GPL-2.0-or-later
// The game's GX range allocator: one region of GPU memory the CPU writes for the GPU every frame
// (per-draw shader parameters, CPU-deformed vertices, the graphics device's per-frame upload area,
// the copy shader's sources and range lists). Read once from the game's own objects after it made
// its graphics device: the device (image offset 0x55406c8, two pointers in), its upload allocator
// (+0xb3e0), whose first field is the range allocator, whose region is [+0xb8, +0xc0). Each offset is
// checked against the 1.09 code that uses it. The buffer cache streams read-only buffers inside it
// (BufferCache::ObtainBuffer): the game rewrites them before each use, and tracking them only cost
// write faults and re-protection.
#pragma once

#include <atomic>
#include <cstdint>

namespace BbGxRing {
inline std::atomic<std::uint64_t> g_start{0}, g_end{0};

/// Looks for the region once the game has made its graphics device; false until then. Cheap to
/// call again; logs the region once.
bool Find();

inline bool Known() {
    return g_start.load(std::memory_order_acquire) != 0;
}
inline bool Contains(std::uint64_t address, std::uint64_t size) {
    const std::uint64_t start = g_start.load(std::memory_order_acquire);
    return start && address >= start && address + size <= g_end.load(std::memory_order_relaxed);
}
} // namespace BbGxRing

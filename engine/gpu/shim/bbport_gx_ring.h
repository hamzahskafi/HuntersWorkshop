// SPDX-License-Identifier: GPL-2.0-or-later
// The game's GX range allocator: one region of GPU memory the CPU writes for the GPU every frame
// (per-draw shader parameters, CPU-deformed vertices, the graphics device's per-frame upload area,
// the copy shader's sources and range lists). The game makes it once, when it creates its graphics
// device: the allocator's init (image 0x26a9b50, one caller) is given a base and a size, and a hook
// on its entry (engine/gx_region.cpp) hands them here. The buffer cache binds it in place, as a PC
// game's upload heap (BB_GX_IN_PLACE, BufferCache::PollGxRegion): the game rewrites it every frame,
// and tracking it cost write faults, uploads and re-protection.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace BbGxRing {
inline std::atomic<std::uint64_t> g_start{0}, g_end{0};

/// BB_GX_IN_PLACE (default on): the region is to be read in place. Windows only, where the GPU
/// imports it (BbGuestMemory::ImportRange); elsewhere it stays tracked. Decided at start, before
/// the region exists: the device's import extension and the fences' timing
/// (Rasterizer::HonestLabels) follow it even if the region is never found or its import fails.
inline bool InPlaceWanted() {
#ifdef _WIN32
    static const bool on = [] {
        const char* env = std::getenv("BB_GX_IN_PLACE");
        return !(env && env[0] == '0');
    }();
    return on;
#else
    return false;
#endif
}

/// The region the init makes of the base and size it is given: the base rounded up to 16 bytes,
/// the size less that and rounded down to 16 (0x26a9b5e..0x26a9b88).
inline void FromInit(std::uint64_t base, std::uint64_t size, std::uint64_t& start, std::uint64_t& end) {
    const std::uint64_t pad = (0u - static_cast<std::uint32_t>(base)) & 0xf;
    start = base + pad;
    end = start + ((size - pad) & ~std::uint64_t(0xf));
}

/// From the hook on the init: keeps the region and logs it once. False for one that cannot be the
/// game's (empty, or over 1 GiB).
bool NoteRegion(std::uint64_t start, std::uint64_t end);

/// After a minute without a region: logs once that the game was not seen making it.
void ReportMissing();

inline bool Known() {
    return g_start.load(std::memory_order_acquire) != 0;
}
inline bool Contains(std::uint64_t address, std::uint64_t size) {
    const std::uint64_t start = g_start.load(std::memory_order_acquire);
    return start && address >= start && address + size <= g_end.load(std::memory_order_relaxed);
}
} // namespace BbGxRing

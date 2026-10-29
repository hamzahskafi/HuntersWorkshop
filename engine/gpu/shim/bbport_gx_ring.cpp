// SPDX-License-Identifier: GPL-2.0-or-later
// See bbport_gx_ring.h.
#include "bbport_gx_ring.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" std::uint64_t runtime_memory_clamp(std::uintptr_t address, std::uint64_t size);

namespace BbGxRing {
namespace {
using u64 = std::uint64_t;
constexpr u64 ImageBase = 0x800000000ull; // where the loader maps the game (bbport_guest_hooks)

constexpr u64 DeviceHolder = 0x55406c8; // the graphics device, behind one more pointer
constexpr u64 UploadAllocator = 0xb3e0; // in the device: its per-frame upload allocator
constexpr u64 RegionStart = 0xb8, RegionEnd = 0xc0, RegionSize = 0xd0; // in the range allocator

struct Code {
    u64 offset;
    std::array<unsigned char, 7> bytes;
};
// The 1.09 code these offsets come from (image offsets):
constexpr std::array<Code, 4> kCode{{
    // the GX query creation reads the device: lea rax, [0x55406c8]
    {0xe2f0e9, {0x48, 0x8d, 0x05, 0xd8, 0x15, 0x71, 0x04}},
    // the upload copy (0x26adb90) takes the device's allocator: lea rdi, [rbx + 0xb3e0]
    {0x26adbcb, {0x48, 0x8d, 0xbb, 0xe0, 0xb3, 0x00, 0x00}},
    // the range allocator's region (0x26a9b50): mov [rbx + 0xb8], rax ... mov [rbx + 0xc0], rax
    {0x26a9b7d, {0x48, 0x89, 0x83, 0xb8, 0x00, 0x00, 0x00}},
    {0x26a9b88, {0x48, 0x89, 0x83, 0xc0, 0x00, 0x00, 0x00}},
}};

bool Read(u64 address, u64& out) {
    if (!address || runtime_memory_clamp(address, sizeof out) < sizeof out) return false;
    std::memcpy(&out, reinterpret_cast<const void*>(address), sizeof out);
    return true;
}

// 0 unchecked, 1 the code is 1.09's, -1 not looked for (another version, or BB_GX_RING_STREAM_KB=0).
std::atomic<int> g_state{0};
std::atomic<bool> g_logged{false};
} // namespace

bool Find() {
    if (Known()) return true;
    int state = g_state.load(std::memory_order_acquire);
    if (state < 0) return false;
    if (state == 0) {
        const char* env = std::getenv("BB_GX_RING_STREAM_KB");
        bool fine = !(env && std::strtoull(env, nullptr, 10) == 0);
        for (const Code& c : kCode) {
            if (!fine) break;
            const u64 at = ImageBase + c.offset;
            fine = runtime_memory_clamp(at, c.bytes.size()) >= c.bytes.size() &&
                   !std::memcmp(reinterpret_cast<const void*>(at), c.bytes.data(), c.bytes.size());
        }
        if (!fine && !(env && std::strtoull(env, nullptr, 10) == 0)) {
            std::printf("GPU memory: the game's GX range allocator not looked for (its code differs from 1.09)\n");
        }
        g_state.store(fine ? 1 : -1, std::memory_order_release);
        if (!fine) return false;
    }
    u64 holder = 0, device = 0, ring = 0, start = 0, end = 0, size = 0;
    if (!Read(ImageBase + DeviceHolder, holder) || !Read(holder, device) ||
        !Read(device + UploadAllocator, ring) || !Read(ring + RegionStart, start) ||
        !Read(ring + RegionEnd, end) || !Read(ring + RegionSize, size)) {
        return false; // not made yet
    }
    // The region as the allocator's init (0x26a9b50) left it: its end its start plus its size.
    if (!start || end <= start || end - start != size || size > (u64(1) << 30)) return false;
    g_end.store(end, std::memory_order_relaxed);
    g_start.store(start, std::memory_order_release);
    if (!g_logged.exchange(true)) {
        std::printf("GPU memory: the game's GX range allocator at %#llx..%#llx (%llu MiB): read-only buffers in "
                    "it are copied when used, not tracked (BB_GX_RING_STREAM_KB=0: tracked as before)\n",
                    (unsigned long long)start, (unsigned long long)end, (unsigned long long)((end - start) >> 20));
    }
    return true;
}
} // namespace BbGxRing

// SPDX-License-Identifier: GPL-3.0-or-later
// The game's GX range allocator (bbport_gx_ring.h): its region is made once, when the game creates
// its graphics device (0x216b0f0, on the device's +0xb730), by the allocator's init (0x26a9b50, its
// one caller 0x216b274), from a base (rdx) and a size (rcx). A hook on the init's entry hands them to
// BbGxRing, which works out the region as the init does. Before 0.4.4 beta 4 the region was read back
// through the device's objects (the holder at 0x55406c8, the upload allocator at +0xb3e0); in the game
// the allocator's field there read 0, so the region was never found (beta 3's log).
#include "gx_region.h"
#include "engine_hooks.h"
#include "engine_state.h"
#include "bbport_gx_ring.h"
#include <cstdlib>

namespace BbEngine::GxRegion {
namespace {
#define GUEST_ABI __attribute__((sysv_abi))

constexpr std::size_t kRegionInit = 0x26a9b50;
// push rbp; mov rbp, rsp; push r15; push r14; push r13; push r12; push rbx; push rax; mov r12, rcx
constexpr std::uint8_t kRegionInitPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                                0x55, 0x41, 0x54, 0x53, 0x50, 0x49, 0x89, 0xcc};

// saved[3] rdx (the base), saved[2] rcx (the size); the game's init runs on.
GUEST_ABI std::int64_t RegionInit(std::uint64_t, const std::uint64_t* saved) {
    std::uint64_t start = 0, end = 0;
    BbGxRing::FromInit(saved[3], saved[2], start, end);
    BbGxRing::NoteRegion(start, end);
    return 0;
}
} // namespace

bool Install(std::uint8_t* image, std::size_t size) {
    if (const char* env = std::getenv("BB_GX_RING_STREAM_KB"); env && std::strtoull(env, nullptr, 10) == 0) {
        return false;
    }
    Hook hook;
    if (!Prepare(hook, image, size, kRegionInit, kRegionInitPrologue, RegionInit)) {
        host_log("GPU memory: the GX range allocator's init at 0x%zx differs from 1.09: its region is tracked as "
                 "before",
                 kRegionInit);
        return false;
    }
    Commit(std::span{&hook, 1});
    return true;
}
} // namespace BbEngine::GxRegion

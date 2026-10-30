// SPDX-License-Identifier: GPL-2.0-or-later
// The game's GX range allocator's region (gpu/shim/bbport_gx_ring.h), taken from its init by the engine's hook
// (gpu/shim/engine/gx_region.cpp): a stand-in for the init with 1.09's entry at its image offset, called as the
// game's device creation calls it; the region as the init makes it of its base and size; the bounds.
#include "bbport_gx_ring.h"
#include "engine_hooks.h"
#include "gx_region.h"
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace BbEngine {
void host_log(const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::putchar('\n');
}
} // namespace BbEngine

namespace {
constexpr std::size_t kInit = 0x26a9b50, kSize = kInit + 0x100;
// 1.09's entry (push rbp; mov rbp, rsp; push r15..r12; push rbx; push rax; mov r12, rcx), then the
// pushes undone and a return: the stand-in keeps the game's entry and does nothing else.
constexpr std::uint8_t kStandIn[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54,
                                     0x53, 0x50, 0x49, 0x89, 0xcc, 0x58, 0x5b, 0x41, 0x5c, 0x41, 0x5d, 0x41,
                                     0x5e, 0x41, 0x5f, 0x5d, 0xc3};

std::uint8_t* MapImage() {
#ifdef _WIN32
    return static_cast<std::uint8_t*>(VirtualAlloc(nullptr, kSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
#else
    void* p = mmap(nullptr, kSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(p);
#endif
}

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1); else unsetenv(name);
#endif
}
} // namespace

int main() {
    // The init's arithmetic (0x26a9b5e..0x26a9b88): the base up to 16, the size less that, down to 16.
    std::uint64_t start = 0, end = 0;
    BbGxRing::FromInit(0x1040000000ull, 288ull << 20, start, end);
    assert(start == 0x1040000000ull && end == 0x1040000000ull + (288ull << 20));
    BbGxRing::FromInit(0x1040000008ull, 0x100000ull, start, end);
    assert(start == 0x1040000010ull && end == start + 0xffff0ull);

    // Another version's entry: no hook, the region stays unknown.
    std::uint8_t* image = MapImage();
    assert(image);
    std::uint8_t other[sizeof kStandIn];
    std::memcpy(other, kStandIn, sizeof other);
    other[1] = 0x90;
    std::memcpy(image + kInit, other, sizeof other);
    assert(!BbEngine::GxRegion::Install(image, kSize));

    // BB_GX_RING_STREAM_KB=0: no hook either.
    std::memcpy(image + kInit, kStandIn, sizeof kStandIn);
    SetEnv("BB_GX_RING_STREAM_KB", "0");
    assert(!BbEngine::GxRegion::Install(image, kSize));
    SetEnv("BB_GX_RING_STREAM_KB", nullptr);

    // 1.09: hooked; the game's call (the device's base and size in rdx and rcx) gives the region, and the
    // init itself still runs and returns.
    assert(BbEngine::GxRegion::Install(image, kSize));
    assert(!BbGxRing::Known());
    BbEngine::Call(reinterpret_cast<std::uint64_t>(image + kInit), 0x10282a2e40ull, 0, 0x1040000004ull,
                   (288ull << 20) + 4);
    assert(BbGxRing::Known());
    assert(BbGxRing::Contains(0x1040000010ull, 0x40000) && BbGxRing::Contains(0x1052000000ull - 16, 16));
    assert(!BbGxRing::Contains(0x1040000000ull, 32) && !BbGxRing::Contains(0x1052000000ull - 8, 16));
    assert(!BbGxRing::Contains(0x1052000010ull, 1));

    // What cannot be the game's region is not taken, and does not replace the one known.
    assert(!BbGxRing::NoteRegion(0, 0x1000) && !BbGxRing::NoteRegion(0x2000, 0x1000));
    assert(!BbGxRing::NoteRegion(0x1000, 0x1000 + (2ull << 30)));
    assert(BbGxRing::Contains(0x1040000010ull, 16));
    BbGxRing::ReportMissing(); // known: says nothing
    std::puts("gx-ring-test: all ok");
    return 0;
}

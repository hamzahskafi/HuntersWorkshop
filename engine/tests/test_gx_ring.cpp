// SPDX-License-Identifier: GPL-2.0-or-later
// The GX range allocator's region (gpu/shim/bbport_gx_ring.h) read from a stand-in for the game: its
// image at the loader's address with the 1.09 code the offsets come from, a device, its upload
// allocator and the range allocator, as the game lays them out. Reads are fault-safe: a pointer into
// unmapped memory fails the look, not the process.
#include "bbport_gx_ring.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace {
constexpr std::uint64_t kBase = 0x800000000ull, kSize = 0x5541000; // up to the device's holder

void Put(std::uint64_t offset, std::initializer_list<unsigned char> bytes) {
    std::memcpy(reinterpret_cast<void*>(kBase + offset), bytes.begin(), bytes.size());
}
void Put64(std::uint64_t address, std::uint64_t value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, 8);
}
} // namespace

int main() {
#ifdef _WIN32
    void* image = VirtualAlloc(reinterpret_cast<void*>(kBase), kSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void* image = mmap(reinterpret_cast<void*>(kBase), kSize, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (image == MAP_FAILED) image = nullptr;
#endif
    if (image != reinterpret_cast<void*>(kBase)) {
        std::puts("gx-ring-test: the game's address is taken here, skipped");
        return 0;
    }
    // The 1.09 code: the query creation's lea of the device holder, the upload copy's lea of its allocator,
    // the range allocator's region stores.
    Put(0xe2f0e9, {0x48, 0x8d, 0x05, 0xd8, 0x15, 0x71, 0x04});
    Put(0x26adbcb, {0x48, 0x8d, 0xbb, 0xe0, 0xb3, 0x00, 0x00});
    Put(0x26a9b7d, {0x48, 0x89, 0x83, 0xb8, 0x00, 0x00, 0x00});
    Put(0x26a9b88, {0x48, 0x89, 0x83, 0xc0, 0x00, 0x00, 0x00});

    // Before the loader placed the image: not looked at, and looked for again later.
    assert(!BbGxRing::Find() && !BbGxRing::Known());
    BbGxRing::SetImage(reinterpret_cast<const std::uint8_t*>(kBase), kSize);

    // Before the game made its device: not found, and looked for again later.
    assert(!BbGxRing::Find() && !BbGxRing::Known());

    // The objects: holder -> device in the image's spare room; device + 0xb3e0 -> the range allocator,
    // in memory the game mapped (outside the image). First a device pointer no process can read (kernel
    // space): the read fails, the game goes on, and it is looked for again.
    static std::uint64_t ring_object[0x100 / 8];
    const std::uint64_t holder = kBase + 0x1000, device = kBase + 0x10000;
    const auto ring = reinterpret_cast<std::uint64_t>(ring_object);
    Put64(kBase + 0x55406c8, holder);
    Put64(holder, 0xffff800000000000ull);
    assert(!BbGxRing::Find() && !BbGxRing::Known());
    BbGxRing::ReportMissing(); // "stopped at the upload allocator", the device's field unreadable
    Put64(holder, device);
    Put64(device + 0xb3e0, ring);
    const std::uint64_t start = 0x1040000000ull, size = 288ull << 20;
    Put64(ring + 0xb8, start);
    Put64(ring + 0xc0, start + size);
    Put64(ring + 0xd0, size + 0x10); // its size field disagrees: not taken
    assert(!BbGxRing::Find() && !BbGxRing::Known());
    Put64(ring + 0xd0, size);
    assert(BbGxRing::Find() && BbGxRing::Known());
    assert(BbGxRing::Contains(start, 0x40000) && BbGxRing::Contains(start + size - 16, 16));
    assert(!BbGxRing::Contains(start - 16, 32) && !BbGxRing::Contains(start + size - 8, 16));
    assert(!BbGxRing::Contains(start + size, 1));
    std::puts("gx-ring-test: all ok");
    return 0;
}

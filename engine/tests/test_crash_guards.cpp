// SPDX-License-Identifier: GPL-3.0-or-later
// The crash guards (gpu/shim/engine/crash_guards.h) against stand-ins for the game's functions in host memory:
// Havok's hit collector at its cap (the game's push itself, its growth through the cave), and GX occlusion
// queries whose creation runs out of the device's room.
#include "crash_guards.h"
#include "engine_hooks.h"
#include <cassert>
#include <cstdarg>
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

namespace BbEngine {
void host_log(const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::putchar('\n');
}
} // namespace BbEngine

using namespace BbEngine::CrashGuards;

namespace {
// A stub's saved registers: [0] the return slot (saved[-1]), then r9, r8, rcx, rdx, rsi, rdi.
struct Saved {
    std::uint64_t slots[7] = {};
    std::uint64_t* regs() { return slots + 1; }
    std::uint64_t& rdi() { return slots[6]; }
    std::uint64_t& rsi() { return slots[5]; }
    std::uint64_t result() const { return slots[0]; }
};

// The GX device: room for `room` more handles; its begin and end counted with the handle they were given.
int room = 0;
std::uint64_t next_handle = 0x1000, began = 0, ended = 0, last_handle = 0, last_queries = 0;
const std::uint64_t kDevice = 0x40000;

__attribute__((sysv_abi)) std::int64_t Create(std::uint64_t query, std::uint64_t kind) {
    assert(kind == 1);
    std::uint64_t handle = room > 0 ? next_handle++ : 0;
    if (room > 0) --room;
    std::memcpy(reinterpret_cast<void*>(query), &handle, 8);
    return handle ? 1 : 0;
}
__attribute__((sysv_abi)) std::int64_t DeviceBegin(std::uint64_t device, std::uint64_t queries, std::uint64_t handle) {
    assert(device == kDevice);
    ++began, last_handle = handle, last_queries = queries;
    return 0;
}
__attribute__((sysv_abi)) std::int64_t DeviceEnd(std::uint64_t device, std::uint64_t queries, std::uint64_t handle) {
    assert(device == kDevice && queries == kDevice + kDeviceQueries);
    ++ended, last_handle = handle;
    return 0;
}

struct Query {
    alignas(16) std::uint8_t bytes[0x30] = {};
    std::uint64_t at() { return reinterpret_cast<std::uint64_t>(bytes); }
    std::uint64_t handle() const { std::uint64_t h; std::memcpy(&h, bytes, 8); return h; }
    std::uint32_t state() const { std::uint32_t s; std::memcpy(&s, bytes + 8, 4); return s; }
};

// The pool's creation, through the guard's callback.
bool Made(Query& q) {
    Saved saved;
    saved.rdi() = q.at();
    saved.rsi() = 1;
    assert(QueryCreated(0, saved.regs()) == 1); // the call is the guard's
    return saved.result() != 0;
}

// The hit array's grow (0x819100: allocator, array, element size), as Havok's: the capacity doubled (16 at first).
std::uint64_t g_allocator = 0;
unsigned g_grown = 0;
__attribute__((sysv_abi)) void Grow(std::uint64_t allocator, std::uint64_t array, std::int64_t element) {
    assert(allocator == g_allocator && (element & 0xffffffff) == 0x60);
    void* data;
    std::uint32_t capacity;
    std::memcpy(&data, reinterpret_cast<void*>(array), 8);
    std::memcpy(&capacity, reinterpret_cast<void*>(array + 0xc), 4);
    capacity = capacity ? capacity * 2 : 16;
    data = std::realloc(data, std::size_t(capacity) * 0x60);
    assert(data && (reinterpret_cast<std::uintptr_t>(data) & 15) == 0);
    std::memcpy(reinterpret_cast<void*>(array), &data, 8);
    std::memcpy(reinterpret_cast<void*>(array + 0xc), &capacity, 4);
    ++g_grown;
}

struct Collector {
    alignas(16) std::uint8_t bytes[0x30] = {};
    std::int32_t count() const { std::int32_t n; std::memcpy(&n, bytes + kHitCount, 4); return n; }
    float hit(std::int32_t i) const {
        const std::uint8_t* data;
        std::memcpy(&data, bytes + kHitData, 8);
        float f;
        std::memcpy(&f, data + std::size_t(i) * 0x60 + 0x50, 4);
        return f;
    }
    void start(std::uint32_t capacity) {
        void* data;
        std::memcpy(&data, bytes + kHitData, 8);
        std::free(data);
        data = capacity ? std::malloc(std::size_t(capacity) * 0x60) : nullptr;
        std::memset(bytes, 0, sizeof bytes);
        std::memcpy(bytes + kHitData, &data, 8);
        std::memcpy(bytes + 0x2c, &capacity, 4);
    }
    ~Collector() { start(0); }
};

// The push as the game has it, its grow's call landing on Grow; pushes `n` hits, each its index at +0x50.
void TestHits() {
    constexpr std::size_t kSize = (kHitPush + sizeof kHitPushCode + 4095) & ~std::size_t(4095);
#ifdef _WIN32
    auto* image = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, kSize, MEM_RESERVE | MEM_COMMIT,
                                                          PAGE_EXECUTE_READWRITE));
    assert(image);
#else
    auto* image = static_cast<std::uint8_t*>(mmap(nullptr, kSize, PROT_READ | PROT_WRITE | PROT_EXEC,
                                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(image != MAP_FAILED);
#endif
    const auto base = reinterpret_cast<std::uint64_t>(image);
    g_allocator = base + kHitAllocator;
    std::memcpy(image + kHitPush, kHitPushCode, sizeof kHitPushCode);
    std::int32_t call;
    std::memcpy(&call, image + kHitGrowCall + 1, 4);
    std::uint8_t* const grow = image + kHitGrowCall + 5 + call; // 0x819100's image offset
    const std::uint8_t jump[6] = {0xff, 0x25, 0, 0, 0, 0};
    const auto host = reinterpret_cast<std::uint64_t>(&Grow);
    std::memcpy(grow, jump, 6);
    std::memcpy(grow + 6, &host, 8);
    using Push = void(__attribute__((sysv_abi)) *)(void*, const void*);
    const auto push = reinterpret_cast<Push>(image + kHitPush);
    alignas(16) float hit[24] = {};
    Collector collector;
    const auto pushes = [&](std::int32_t n) {
        for (std::int32_t i = 0; i < n; ++i) {
            hit[20] = float(i);
            push(collector.bytes, hit);
        }
    };

    // The game's: no end to it.
    pushes(kHitCap + 10);
    assert(collector.count() == kHitCap + 10 && collector.hit(kHitCap + 9) == float(kHitCap + 9));

    // The cave on the growth: the array stops at the cap; a push into room left still goes in.
    BbEngine::Hook hook;
    std::uint8_t cave[kHitCaveSize];
    HitCave(base, cave);
    const std::span<const std::uint8_t> code = kHitPushCode;
    assert(BbEngine::Divert(hook, image, kSize, kHitGrow, code.subspan(kHitGrow - kHitPush, kHitGrowBytes), cave));
    BbEngine::Commit(std::span{&hook, 1});
    collector.start(0);
    g_grown = 0;
    pushes(kHitCap + 10);
    assert(collector.count() == kHitCap && g_grown == 13 && Counted().hits_dropped == 10); // 16 << 12 = 65536
    for (const std::int32_t i : {0, 1, 17, kHitCap - 1}) assert(collector.hit(i) == float(i));
    // A capacity that is no power of two: the first growth past the cap is the last.
    collector.start(40000);
    pushes(80000 + 3);
    assert(collector.count() == 80000 && collector.hit(79999) == 79999.0f && Counted().hits_dropped == 13);
    BbEngine::Discard(hook);
#ifdef _WIN32
    VirtualFree(image, 0, MEM_RELEASE);
#else
    munmap(image, kSize);
#endif
}

void TestQueries() {
    const std::uint64_t context[2] = {0, kDevice};
    const auto ctx = reinterpret_cast<std::uint64_t>(context);

    // With room: as the game's begin and end.
    room = 1;
    Query a;
    assert(Made(a) && a.handle());
    assert(QueryBegin(a.at(), ctx) == 1 && a.state() == 2 && began == 1 && last_handle == a.handle() &&
           last_queries == kDevice + kDeviceQueries);
    assert(QueryBegin(a.at(), ctx) == 1 && began == 1); // begun already: nothing
    assert(QueryEnd(a.at(), ctx) == 1 && a.state() == 1 && ended == 1 && last_handle == a.handle());

    // Out of room: the creation fails and leaves no handle; its begin and end are skipped, its state stays 0.
    Query b;
    assert(!Made(b) && !b.handle() && Counted().queries_failed == 1);
    assert(QueryBegin(b.at(), ctx) == 1 && b.state() == 0 && began == 1); // created again, failed again
    assert(Counted().queries_failed == 2 && Counted().queries_skipped == 1);
    assert(QueryEnd(b.at(), ctx) == 1 && b.state() == 0 && ended == 1);

    // Room again: the next begin creates it, then begins it.
    room = 1;
    assert(QueryBegin(b.at(), ctx) == 1 && b.handle() && b.state() == 2 && began == 2 && last_handle == b.handle());
    assert(Counted().queries_recreated == 1);
    assert(QueryEnd(b.at(), ctx) == 1 && b.state() == 1 && ended == 2);

    // A null handle no failed creation left (a query released): never created here, only skipped.
    Query c;
    room = 1;
    assert(QueryBegin(c.at(), ctx) == 1 && !c.handle() && c.state() == 0 && room == 1 && began == 2);
    assert(Counted().queries_skipped == 2);
}
} // namespace

int main() {
    Reset();
    SetQueryGame(reinterpret_cast<std::uint64_t>(&Create), reinterpret_cast<std::uint64_t>(&DeviceBegin),
                 reinterpret_cast<std::uint64_t>(&DeviceEnd));
    TestHits();
    TestQueries();
    std::puts("crash-guards-test: all ok");
    return 0;
}

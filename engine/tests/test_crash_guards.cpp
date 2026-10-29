// SPDX-License-Identifier: GPL-3.0-or-later
// The crash guards (gpu/shim/engine/crash_guards.h) against stand-ins for the game's functions in host memory:
// Havok's hit collector at its cap, and GX occlusion queries whose creation runs out of the device's room.
#include "crash_guards.h"
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

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

void TestHits() {
    alignas(16) std::uint8_t hit[96] = {};
    alignas(16) std::uint8_t collector[0x30] = {};
    const std::uint64_t data = reinterpret_cast<std::uint64_t>(hit);
    std::memcpy(collector + kHitData, &data, 8);
    Saved saved;
    saved.rdi() = reinterpret_cast<std::uint64_t>(collector);
    for (const std::int32_t count : {0, 1, kHitCap - 1}) {
        std::memcpy(collector + kHitCount, &count, 4);
        assert(!HitFull(saved.rdi()) && HitPush(0, saved.regs()) == 0); // the game's push
    }
    for (const std::int32_t count : {kHitCap, kHitCap * 4}) {
        std::memcpy(collector + kHitCount, &count, 4);
        assert(HitFull(saved.rdi()) && HitPush(0, saved.regs()) == 1); // skipped
    }
    const std::int32_t one = 1;
    std::memset(collector + kHitData, 0, 8);
    std::memcpy(collector + kHitCount, &one, 4);
    assert(HitPush(0, saved.regs()) == 1); // a count with no data
    saved.rdi() = 0;
    assert(HitPush(0, saved.regs()) == 0);
    assert(Counted().hits_dropped == 3);
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

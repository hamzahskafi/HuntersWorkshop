// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 18 (job_barrier; docs/handoffs-and-placement.md, "The game's own waiting code"): the
// waits of the game's fork-join dispatcher (0x21b8710, 0x282 bytes; image offsets, Binary Ninja's
// are 0x400000 higher). The main thread hands a job list to its N-1 workers (one semaphore signal,
// 0x263b0d0), then waits in two loops of plain loads, without `pause` and without ever giving the
// core up:
// - the count wait (0x21b8890): until the group's counter ([r15], the workers count themselves in)
//   reaches N-1 (r12d), or the byte at 0x56a6c31 says more work arrived meanwhile (the dispatcher
//   then wakes the workers again);
// - the slot wait (0x21b8940): then each worker's slot (list base rcx, 0x530 bytes each, its state
//   at +0x528) until it reads 2.
// The thread sampler found ~10% of the main thread's time in the first (the slower spot, 0.4.6 beta
// 17's logs). Each loop becomes a cave that calls a wait of the engine's: `pause` on each pass, and
// from about 50 us on a yield on each pass, so a worker that waits for a core gets this one (where
// cores are spare the yield finds nobody and returns at once). The loops' own exits stay the game's:
// the same conditions, looked at in the same order. The whole dispatcher is checked against 1.09
// (FNV-1a over its bytes) and nothing branches into either site but at its start (the builder's scan
// of every branch in it). Plain code: tests/test_job_barrier.cpp runs both caves.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <initializer_list>
#include <x86intrin.h>

namespace BbEngine::JobBarrier {

constexpr std::size_t kDispatch = 0x21b8710, kDispatchSize = 0x282;
constexpr std::uint64_t kDispatchHash = 0xb2e9c1c939057541ull;
constexpr std::size_t kMoreWork = 0x56a6c31; ///< u8: work arrived while the dispatcher waits

// The count wait and where it goes on: all in, or work arrived.
constexpr std::size_t kCountWait = 0x21b8890, kCountDone = 0x21b8930, kCountMore = 0x21b88a2;
inline constexpr std::uint8_t kCountWaitCode[] = {
    0x45, 0x3b, 0x27,                         // cmp r12d, [r15]
    0x0f, 0x8e, 0x97, 0x00, 0x00, 0x00,       // jle 0x21b8930: all in
    0x80, 0x3d, 0x91, 0xe3, 0x4e, 0x03, 0x00, // cmp byte [rip + 0x34ee391], 0: 0x56a6c31
    0x74, 0xee,                               // je 0x21b8890
};
// The slot wait (its outer loop whole: 0x21b896d is a branch target, so the site ends before it).
constexpr std::size_t kSlotsWait = 0x21b8940, kSlotsDone = 0x21b896d;
inline constexpr std::uint8_t kSlotsWaitCode[] = {
    0x89, 0xd6,                                     // mov esi, edx
    0x48, 0x69, 0xf6, 0x30, 0x05, 0x00, 0x00,       // imul rsi, rsi, 0x530
    0x48, 0x8d, 0xb4, 0x31, 0x28, 0x05, 0x00, 0x00, // lea rsi, [rcx + rsi + 0x528]
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x2e, 0x0f, // nop (15 bytes)
    0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,       //
    0x8b, 0x3e,                                     // mov edi, [rsi]
    0x83, 0xff, 0x02,                               // cmp edi, 2
    0x75, 0xf9,                                     // jne 0x21b8960
    0xff, 0xc2,                                     // inc edx
    0x39, 0xc2,                                     // cmp edx, eax
    0x72, 0xd3,                                     // jb 0x21b8940
};
constexpr std::uint64_t kSlotStride = 0x530, kSlotState = 0x528;
constexpr std::uint32_t kSlotDone = 2;

/// How long a wait spins with `pause` only before it yields on each pass.
constexpr double kSpinUs = 50.0;

/// How a wait ended and what it took.
struct Waited {
    int result = 0;           ///< `ready`'s nonzero answer
    std::uint64_t ticks = 0;  ///< time-stamp ticks
    std::uint32_t yields = 0; ///< passes that gave the core up
};

/// Looks at `ready` until it answers nonzero, with `pause` between looks; once `budget` ticks have
/// passed, `yield()` between them instead.
template <typename Ready, typename Yield>
Waited Wait(Ready ready, std::uint64_t budget, Yield yield) {
    const std::uint64_t start = __rdtsc();
    Waited w;
    bool past = false;
    for (unsigned turn = 1;; ++turn) {
        if ((w.result = ready()) != 0) {
            w.ticks = __rdtsc() - start;
            return w;
        }
        if (past) {
            yield();
            ++w.yields;
            continue;
        }
        _mm_pause();
        if ((turn & 15) == 0) {
            past = __rdtsc() - start >= budget;
        }
    }
}

/// The count wait's condition, as the game's loop has it: all in when need <= the counter (signed,
/// as `jle`), else work arrived when the byte is set. 1 all in, 2 work arrived, 0 neither.
inline int CountReady(const void* counter, std::int32_t need, const void* more) {
    if (need <= __atomic_load_n(static_cast<const std::int32_t*>(counter), __ATOMIC_ACQUIRE)) {
        return 1;
    }
    return __atomic_load_n(static_cast<const std::uint8_t*>(more), __ATOMIC_ACQUIRE) != 0 ? 2 : 0;
}

/// The slot wait's condition: slots `next` to `end` (unsigned, as `jb`), in order, each until its
/// state reads 2. Advances `next` past the done ones; 1 when all are.
inline int SlotsReady(std::uint64_t base, std::uint32_t& next, std::uint32_t end) {
    while (next < end && __atomic_load_n(reinterpret_cast<const std::uint32_t*>(
                                             base + next * kSlotStride + kSlotState),
                                         __ATOMIC_ACQUIRE) == kSlotDone) {
        ++next;
    }
    return next >= end ? 1 : 0;
}

namespace Detail {
struct Writer {
    std::uint8_t* at;
    void Put(std::initializer_list<std::uint8_t> bytes) {
        for (const std::uint8_t b : bytes) *at++ = b;
    }
    void Put64(std::uint64_t value) {
        std::memcpy(at, &value, 8);
        at += 8;
    }
    void Call(std::uint64_t fn) { // movabs rax, fn; call rax
        Put({0x48, 0xb8});
        Put64(fn);
        Put({0xff, 0xd0});
    }
    void Jump(std::uint64_t to) { // jmp [rip + 0]; the address
        Put({0xff, 0x25, 0, 0, 0, 0});
        Put64(to);
    }
};
} // namespace Detail

constexpr std::size_t kCaveSize = 64;

/// The count wait's cave for the image at `image`. `wait` (sysv_abi: counter, need, more byte)
/// returns 0 all in, 1 work arrived. Reached by the site's jump inside the dispatcher's body, so rsp
/// is as at its calls (16-byte aligned) and the call keeps it so. Live there: rbx, r12, r15 (kept by
/// the call, as are r13, r14 and rbp); dead: rax, rcx, rdx, rsi, rdi, r8-r11 and the flags (both
/// exits set them before reading). Returns its length.
inline std::size_t CountCave(std::uint64_t image, std::uint64_t wait, std::uint8_t (&cave)[kCaveSize]) {
    Detail::Writer w{cave};
    w.Put({0x4c, 0x89, 0xff}); // mov rdi, r15: the counter
    w.Put({0x44, 0x89, 0xe6}); // mov esi, r12d: N-1
    w.Put({0x48, 0xba});       // movabs rdx, the byte
    w.Put64(image + kMoreWork);
    w.Call(wait);
    w.Put({0x85, 0xc0}); // test eax, eax
    w.Put({0x75, 0x0e}); // jne: past the next jump
    w.Jump(image + kCountDone);
    w.Jump(image + kCountMore);
    return static_cast<std::size_t>(w.at - cave);
}

/// The slot wait's cave. `wait` (sysv_abi: list base, first slot, end) returns when all are done.
/// Live at the site: rcx (the list), edx (the first slot, 1), eax (N), rbx, r15; at its exit rbx and
/// r15 only. Returns its length.
inline std::size_t SlotsCave(std::uint64_t image, std::uint64_t wait, std::uint8_t (&cave)[kCaveSize]) {
    Detail::Writer w{cave};
    w.Put({0x48, 0x89, 0xcf}); // mov rdi, rcx: the list
    w.Put({0x89, 0xd6});       // mov esi, edx: the first slot
    w.Put({0x89, 0xc2});       // mov edx, eax: N
    w.Call(wait);
    w.Jump(image + kSlotsDone);
    return static_cast<std::size_t>(w.at - cave);
}

// job_barrier.cpp:
/// 1.09's dispatcher, whole. False with `why` saying what differs.
bool Ready(const std::uint8_t* image, std::size_t size, const char** why);
/// The waits the caves call (the engine's yield).
__attribute__((sysv_abi)) std::int32_t CountWait(const void* counter, std::int32_t need, const void* more);
__attribute__((sysv_abi)) void SlotsWait(std::uint64_t base, std::uint32_t first, std::uint32_t end);
/// With the frame statistics: the waits per frame, their time, and how many yielded.
void Report(double frames, double seconds);

} // namespace BbEngine::JobBarrier

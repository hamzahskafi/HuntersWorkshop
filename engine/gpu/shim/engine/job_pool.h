// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's work pools (A1). Image offsets; Binary Ninja's are 0x400000 higher.
//
// Every pool's queue is a DLAdaptiveMutex (Dantelion's spin-then-sleep lock, Lock 0x20368c0) and a
// job list whose nodes come from the engine's general allocator. At Central Yharnam the character
// pool pushes ~375 jobs a frame and its six workers spend ~60% of their time waiting on the queue:
// - the game's spin is a `lock cmpxchg` of the lock word with its own value and an rdtsc on every
//   turn, no pause, so the word's cache line keeps moving and the holder's unlock has to win it
//   back;
// - every pop frees its node through the general allocator inside the queue lock's hold, and that
//   allocator takes its heap's lock.
//
// The lock (job_lock): the same transitions in the same order, spinning on a plain load with
// pause; the game's unlock (0x2036a40) is unchanged. The node cache (job_nodes): each pool's job
// list gets a stand-in for the general allocator in its allocator slot (pool +0x28) that keeps the
// nodes it frees and hands them back.
//
// After droogie (bbhost v0.2.19: src/decomp/sync/adaptive_mutex.cpp, f764eff; src/decomp/sync/
// ez_work.cpp, 58af1c8), whose measurements these are. bbhost replaces ProcessOne to put the cache
// in under the queue lock; here the game's ProcessOne stays, and the cache goes in at its entry
// with a compare-exchange on the slot and keeps a lock of its own.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <immintrin.h>
#include <optional>
#include <span>
#include <x86intrin.h>

namespace BbEngine::JobPool {

// ---- DLAdaptiveMutex ------------------------------------------------------------------------------
// The lock word is the u64 at +0x18: bits 0-39 the owner (the low 40 bits of scePthreadSelf; 0 free,
// 0xffffffffff handed to a woken sleeper), bits 40-63 how many threads sleep on the semaphore at
// +0x10. The s32 at +0x8 is how long a contender spins, in time-stamp ticks, before it counts itself
// in and sleeps. The game's unlock frees the word or, with a sleeper counted, hands the lock over
// (owner 0xffffffffff, one fewer counted) and signals the semaphore once: the woken thread owns it.
constexpr std::uint64_t kOwnerMask = 0xffffffffffull;
constexpr std::uint64_t kSleepersMask = ~kOwnerMask;
constexpr std::uint64_t kOneSleeper = kOwnerMask + 1;

// The game's Lock, as it moves the word: take it when its owner is free or this thread, keeping the
// sleepers' count; while the spin budget lasts, look again; after it, count in and sleep. `budget()`
// is the mutex's s32 at +0x8, read again every turn as the game reads it; `sleep()` waits on the
// semaphore. The owner is compared in its 40 bits (the game compares them with the whole handle:
// the same for HW's thread handles, which stay below 1 TiB).
template <typename Budget, typename Sleep>
void Take(std::atomic<std::uint64_t>& word, std::uint64_t self, Budget budget, Sleep sleep) {
    const std::uint64_t me = self & kOwnerMask;
    const std::uint64_t start = __rdtsc();
    for (;;) {
        const std::uint64_t now = __rdtsc();
        const std::uint64_t until = start + static_cast<std::uint64_t>(static_cast<std::int64_t>(budget()));
        std::uint64_t old = word.load(std::memory_order_relaxed);
        for (;;) {
            const std::uint64_t owner = old & kOwnerMask;
            if (owner == 0 || owner == me) {
                if (word.compare_exchange_weak(old, (old & kSleepersMask) | me, std::memory_order_acquire,
                                               std::memory_order_relaxed))
                    return;
            } else if (now < until) {
                break; // held, within the spin: watch the word, don't swap it
            } else if (word.compare_exchange_weak(old, old + kOneSleeper, std::memory_order_acquire,
                                                  std::memory_order_relaxed)) {
                sleep(); // an unlock hands the lock over
                return;
            }
        }
        _mm_pause();
    }
}

constexpr std::size_t kLock = 0x20368c0;
// push rbp; mov rbp, rsp; push r15; push r14; push r12; push rbx; mov r14, rdi
constexpr std::uint8_t kLockEntry[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41,
                                       0x56, 0x41, 0x54, 0x53, 0x49, 0x89, 0xfe};
// DLAdaptiveMutex::Lock's replacement (Replace: the game's code jumps here). Returns 0, as the game's.
__attribute__((sysv_abi)) std::int64_t Lock(std::uint64_t mutex, std::uint64_t timeout);
// 1.09's Lock, whole, and the code it calls. False with `why` saying what differs.
bool LockReady(std::uint8_t* image, std::size_t size, const char** why);

// ---- Fingerprints -----------------------------------------------------------------------------------
// The code these rely on is checked whole: FNV-1a over a function's bytes, with the game's thread-
// pointer reads (`mov rax, fs:[0]`, at `reads`: offsets into the function) read as the game has
// them. Before the engine starts, the start program's link step makes each one `gs:[0]`, and on
// Windows the loader then gives it the TEB slot's displacement (`gs:[disp]`) or, past slot 63, a
// jump to a stub of its own (`jmp rel32` and four `nop`s; src/probe.c patch_tls_reads). Anything
// else at a read gives no fingerprint.
std::optional<std::uint64_t> Fingerprint(const std::uint8_t* code, std::size_t size,
                                         std::span<const std::size_t> reads = {});

// ---- The job list's node cache --------------------------------------------------------------------
// A pool (NS_SPRJ::CSEzWork): the queue lock at +0x30; the job list's sentinel at +0x18, its count
// at +0x20 and its allocator at +0x28, which the constructor (0x20313e0) sets to the general
// allocator (the pointer at 0x5540420) and allocates the sentinel from. Its users:
// - CSEzWorkList::Append (0x2032780; all 14 callers hold a lock, the pools' queue lock at +0x30)
//   allocates each node through the slot (+0x58: 0x18 bytes, aligned 8);
// - ProcessOne (0x2032370) unlinks the first node and frees it through the slot (+0x70) under the
//   queue lock;
// - ~CSEzWork (0x2031cf0) frees what is left, then the sentinel last, through the slot.
// The general allocator's class (vtable 0x53aefa0, 27 methods) reads only its +0x8 (the heap) in
// every method: a stand-in is that vtable's copy, alloc and free replaced, and the heap.
constexpr std::uint64_t kAllocSlot = 0x58, kFreeSlot = 0x70;
constexpr int kVtableSlots = 27;
constexpr std::uint64_t kNodeSize = 0x18;
constexpr std::uint32_t kCap = 1024; // nodes a pool keeps (24 KB)
constexpr int kCaches = 64;          // pools with a cache

struct alignas(64) NodeCache {
    std::uint64_t vtable = 0; // what the game sees: the vtable, then the heap (+0x8)
    std::uint64_t heap = 0;
    std::uint64_t general = 0;  // the allocator it stands in for
    std::uint64_t sentinel = 0; // the job list's sentinel: its free is the pool's end
    std::uint64_t pool = 0;
    std::uint64_t top = 0; // the nodes kept, linked through their first word
    std::uint32_t count = 0;
    std::atomic<std::uint32_t> state{0}; // kFree, kClaimed, kInUse
    std::atomic<bool> busy{false};       // held around the cache's own fields
    std::uint64_t hits = 0, misses = 0, kept = 0, passed = 0;
    std::uint64_t slots[kVtableSlots] = {};
};

// The stand-in's alloc and free, which the game calls through its vtable.
__attribute__((sysv_abi)) std::uint64_t NodeAlloc(std::uint64_t cache, std::uint64_t size, std::uint64_t align);
__attribute__((sysv_abi)) std::uint64_t NodeFree(std::uint64_t cache, std::uint64_t block);
// The caches' memory (low memory on Windows: the game holds these pointers). False without it.
bool SetUpCaches();
// The pool's job list gets a cache of its own if it still allocates from `general`, an object of
// the class whose vtable is `vtable`, and a cache is free. True when this call put one in.
bool Adopt(std::uint64_t pool, std::uint64_t general, std::uint64_t vtable);

constexpr std::size_t kProcessOne = 0x2032370;
// push rbp; mov rbp, rsp; push r15; push r14; push r13; push r12; push rbx; sub rsp, 0x18
constexpr std::uint8_t kProcessOnePrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                                0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x18};
// The hook at ProcessOne's entry (pool in rdi): a pool still on the general allocator adopts a cache.
__attribute__((sysv_abi)) std::int64_t PoolPop(std::uint64_t, const std::uint64_t* saved);
// 1.09's ProcessOne, Append, ~CSEzWork and the general allocator's class; keeps the image for
// PoolPop and sets the caches up. False with `why` saying what differs or is missing.
bool NodesReady(std::uint8_t* image, std::size_t size, const char** why);

} // namespace BbEngine::JobPool

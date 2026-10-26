// SPDX-License-Identifier: GPL-3.0-or-later
// The work pools' lock and node cache (gpu/shim/engine/job_pool.h), without game files:
// - the lock: ours and a model of the game's Lock (0x20368c0) taking one word, released by a model of
//   the game's Unlock (0x2036a40), many threads, at three spin budgets: exclusion, exact counts, a
//   free word at the end;
// - the node cache: pools through a stand-in general allocator, as the game's Append, ProcessOne
//   and ~CSEzWork use their list's allocator slot;
// - the fingerprints: a thread-pointer read in each of the forms the link step and the Windows
//   loader leave, on synthetic code.
#include "engine_hooks.h"
#include "job_pool.h"
#include <cassert>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
#include <windows.h>
extern "C" void* runtime_low_map(std::size_t size, int) {
    return VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}
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

using namespace BbEngine::JobPool;
#define GUEST_ABI __attribute__((sysv_abi))

namespace {

// ---- The lock -----------------------------------------------------------------------------------------

struct Semaphore {
    std::mutex mu;
    std::condition_variable cv;
    int count = 0;
    void Signal() {
        std::lock_guard lock(mu);
        ++count;
        cv.notify_one();
    }
    void Wait() {
        std::unique_lock lock(mu);
        cv.wait(lock, [this] { return count > 0; });
        --count;
    }
};

// 0x2036a40: free the word, or hand the lock to a sleeper (owner 0xffffffffff, one fewer counted)
// and signal once.
void GameUnlock(std::atomic<std::uint64_t>& word, Semaphore& sema) {
    std::uint64_t old = word.load(), want;
    do {
        want = (old >> 40) == 0 ? (old & kSleepersMask) : ((old - kOneSleeper) | kOwnerMask);
    } while (!word.compare_exchange_weak(old, want));
    if (want & kOwnerMask) sema.Signal();
}

// 0x20368c0, instruction for instruction: a swap on every turn, the owner compared with the whole
// handle.
void GameLock(std::atomic<std::uint64_t>& word, std::uint64_t self, const std::atomic<std::int32_t>& budget,
              Semaphore& sema) {
    const std::uint64_t start = __rdtsc();
    for (;;) {
        const std::uint64_t now = __rdtsc();
        const std::uint64_t until = start + static_cast<std::uint64_t>(static_cast<std::int64_t>(budget.load()));
        std::uint64_t old = word.load(), want;
        do {
            const std::uint64_t owner = old & kOwnerMask;
            want = owner == 0 || owner == self ? (old & kSleepersMask) | (self & kOwnerMask)
                   : now < until               ? old
                                               : old + kOneSleeper;
        } while (!word.compare_exchange_strong(old, want));
        if ((want & kOwnerMask) == self) return;
        if (now < until) continue;
        sema.Wait();
        return;
    }
}

void LockRun(std::int32_t spin, int threads, int rounds, std::uint64_t self_base, bool mixed) {
    std::atomic<std::uint64_t> word{0};
    std::atomic<std::int32_t> budget{spin};
    Semaphore sema;
    std::atomic<int> inside{0};
    long long count = 0; // only ever changed while the lock is held
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&, t] {
            const std::uint64_t self = self_base + 0x1000ull * static_cast<std::uint64_t>(t + 1);
            std::mt19937 random(static_cast<unsigned>(t * 7919 + spin));
            for (int i = 0; i < rounds; ++i) {
                if (mixed && (random() & 1))
                    GameLock(word, self, budget, sema);
                else
                    Take(word, self, [&] { return budget.load(std::memory_order_relaxed); }, [&] { sema.Wait(); });
                assert(inside.fetch_add(1) == 0);
                ++count;
                if ((random() & 15) == 0) std::this_thread::yield();
                assert(inside.fetch_sub(1) == 1);
                GameUnlock(word, sema);
            }
        });
    }
    for (std::thread& t : pool) t.join();
    assert(count == static_cast<long long>(threads) * rounds);
    assert(word.load() == 0);
    assert(sema.count == 0);
}

// Taken again by its holder while another thread sleeps on it: the sleeper stays counted, and the
// one unlock hands the lock over to it.
void LockAgainRun() {
    std::atomic<std::uint64_t> word{0};
    Semaphore sema;
    const auto budget = [] { return std::int32_t{0}; };
    Take(word, 0x1000, budget, [&] { sema.Wait(); });
    std::atomic<bool> woke{false};
    std::thread sleeper([&] {
        Take(word, 0x2000, budget, [&] { sema.Wait(); });
        woke = true;
        GameUnlock(word, sema);
    });
    while ((word.load() >> 40) != 1) std::this_thread::yield();
    Take(word, 0x1000, budget, [&] { sema.Wait(); });
    assert(word.load() == (kOneSleeper | 0x1000));
    GameUnlock(word, sema);
    sleeper.join();
    assert(woke && word.load() == 0);
}

// ---- The node cache ------------------------------------------------------------------------------------

// The general allocator: a vtable of 27 methods (then a zero word), the heap at +0x8.
std::uint64_t g_vtable[kVtableSlots + 1];
struct Allocator {
    std::uint64_t vtable;
    std::uint64_t heap;
};
Allocator g_general = {reinterpret_cast<std::uint64_t>(g_vtable), 0x5eed};
std::mutex g_heap_mu;
std::unordered_map<std::uint64_t, std::uint64_t> g_live; // block -> size

GUEST_ABI std::uint64_t GeneralAlloc(std::uint64_t self, std::uint64_t size, std::uint64_t align) {
    assert(self == reinterpret_cast<std::uint64_t>(&g_general) && align <= 16);
    const auto block = reinterpret_cast<std::uint64_t>(std::malloc(size));
    std::lock_guard lock(g_heap_mu);
    g_live[block] = size;
    return block;
}
GUEST_ABI std::uint64_t GeneralFree(std::uint64_t self, std::uint64_t block) {
    assert(self == reinterpret_cast<std::uint64_t>(&g_general));
    if (!block) return 0;
    {
        std::lock_guard lock(g_heap_mu);
        assert(g_live.erase(block) == 1); // every block back exactly once
    }
    std::free(reinterpret_cast<void*>(block));
    return 0;
}
GUEST_ABI std::uint64_t Unused(std::uint64_t, std::uint64_t) {
    std::abort();
}

std::size_t Live() {
    std::lock_guard lock(g_heap_mu);
    return g_live.size();
}

std::uint64_t Word(std::uint64_t at) {
    return *reinterpret_cast<std::uint64_t*>(at);
}
void Put(std::uint64_t at, std::uint64_t value) {
    *reinterpret_cast<std::uint64_t*>(at) = value;
}
std::uint64_t Method(std::uint64_t object, std::uint64_t slot) {
    return Word(Word(object) + slot);
}

// A CSEzWork as far as its job list goes: the sentinel at +0x18, the count at +0x20, the
// allocator at +0x28; its constructor, Append, ProcessOne's pop and destructor.
struct Pool {
    alignas(16) std::uint64_t fields[8] = {};
    std::mutex queue; // the pool's queue lock
    std::uint64_t At(std::uint64_t offset) const {
        return reinterpret_cast<std::uint64_t>(fields) + offset;
    }
    std::uint64_t Allocator() const {
        return Word(At(0x28));
    }
    Pool() {
        Put(At(0x28), reinterpret_cast<std::uint64_t>(&g_general));
        const std::uint64_t sentinel = GeneralAlloc(reinterpret_cast<std::uint64_t>(&g_general), 0x18, 8);
        Put(sentinel, sentinel);
        Put(sentinel + 8, sentinel);
        Put(At(0x18), sentinel);
        Put(At(0x20), 0);
    }
    void Append(std::uint64_t job) {
        std::lock_guard lock(queue);
        const std::uint64_t a = Allocator();
        const std::uint64_t node = BbEngine::Call(Method(a, kAllocSlot), a, kNodeSize, 8);
        const std::uint64_t sentinel = Word(At(0x18)), last = Word(sentinel + 8);
        Put(node, sentinel);
        Put(node + 8, last);
        Put(node + 16, job);
        Put(last, node);
        Put(sentinel + 8, node);
        Put(At(0x20), Word(At(0x20)) + 1);
    }
    std::uint64_t Pop() {
        std::lock_guard lock(queue);
        if (!Word(At(0x20))) return 0;
        const std::uint64_t sentinel = Word(At(0x18)), first = Word(sentinel);
        const std::uint64_t job = Word(first + 16);
        Put(Word(first + 8), Word(first));
        Put(Word(first) + 8, Word(first + 8));
        const std::uint64_t a = Allocator();
        BbEngine::Call(Method(a, kFreeSlot), a, first);
        Put(At(0x20), Word(At(0x20)) - 1);
        return job;
    }
    // What is left, then the sentinel last, through the slot.
    void Destroy() {
        const std::uint64_t sentinel = Word(At(0x18));
        std::uint64_t node = Word(sentinel);
        Put(sentinel, sentinel);
        Put(sentinel + 8, sentinel);
        Put(At(0x20), 0);
        const std::uint64_t a = Allocator();
        while (node != sentinel) {
            const std::uint64_t next = Word(node);
            BbEngine::Call(Method(a, kFreeSlot), a, node);
            node = next;
        }
        BbEngine::Call(Method(a, kFreeSlot), a, sentinel);
    }
};

const std::uint64_t kGeneral = reinterpret_cast<std::uint64_t>(&g_general);
const std::uint64_t kClass = reinterpret_cast<std::uint64_t>(g_vtable);

void NodeCacheRun() {
    for (int s = 0; s < kVtableSlots; ++s) g_vtable[s] = reinterpret_cast<std::uint64_t>(&Unused);
    g_vtable[kAllocSlot / 8] = reinterpret_cast<std::uint64_t>(&GeneralAlloc);
    g_vtable[kFreeSlot / 8] = reinterpret_cast<std::uint64_t>(&GeneralFree);
    assert(SetUpCaches());

    // Another class, or a list already off the general allocator: left alone.
    std::uint64_t other_vtable[kVtableSlots + 1] = {};
    Allocator other = {reinterpret_cast<std::uint64_t>(other_vtable), 0};
    {
        Pool p;
        Put(p.At(0x28), reinterpret_cast<std::uint64_t>(&other));
        assert(!Adopt(p.At(0), reinterpret_cast<std::uint64_t>(&other), kClass));
        assert(!Adopt(p.At(0), kGeneral, kClass)); // the slot is not the general allocator
        Put(p.At(0x28), kGeneral);
        p.Destroy();
    }
    assert(Live() == 0);

    // The stand-in: the class's vtable with alloc and free replaced, the heap at +0x8.
    Pool a;
    assert(Adopt(a.At(0), kGeneral, kClass));
    assert(!Adopt(a.At(0), kGeneral, kClass)); // once
    const std::uint64_t stand_in = a.Allocator();
    assert(stand_in != kGeneral && Word(stand_in + 8) == g_general.heap);
    for (int s = 0; s < kVtableSlots; ++s) {
        const std::uint64_t method = Word(Word(stand_in) + 8ull * s);
        if (s == kAllocSlot / 8)
            assert(method == reinterpret_cast<std::uint64_t>(&NodeAlloc));
        else if (s == kFreeSlot / 8)
            assert(method == reinterpret_cast<std::uint64_t>(&NodeFree));
        else
            assert(method == g_vtable[s]);
    }

    // Nodes come back: after the first round, a round of pushes and pops takes nothing new.
    for (int i = 0; i < 100; ++i) a.Append(0x1000 + i);
    for (int i = 0; i < 100; ++i) assert(a.Pop() == static_cast<std::uint64_t>(0x1000 + i));
    const std::size_t held = Live(); // the sentinel and 100 kept nodes
    assert(held == 101);
    for (int i = 0; i < 100; ++i) a.Append(0x2000 + i);
    assert(Live() == held);
    for (int i = 0; i < 100; ++i) assert(a.Pop() == static_cast<std::uint64_t>(0x2000 + i));

    // The cap: beyond 1,024 kept, frees pass on.
    for (int i = 0; i < 3000; ++i) a.Append(0x3000 + i);
    for (int i = 0; i < 3000; ++i) assert(a.Pop() == static_cast<std::uint64_t>(0x3000 + i));
    assert(Live() == 1 + kCap);

    // Anything but a node's size passes through.
    const std::uint64_t big = BbEngine::Call(Method(stand_in, kAllocSlot), stand_in, 0x40, 8);
    {
        std::lock_guard lock(g_heap_mu);
        assert(g_live.at(big) == 0x40);
    }
    GeneralFree(kGeneral, big);

    // Several workers on one pool, the queue lock held around each push and pop, as the game's.
    {
        std::vector<std::thread> workers;
        std::atomic<long long> popped{0};
        for (int t = 0; t < 6; ++t) {
            workers.emplace_back([&, t] {
                std::mt19937 random(static_cast<unsigned>(t));
                for (int i = 0; i < 20000; ++i) {
                    if (random() % 3)
                        a.Append(1);
                    else if (a.Pop())
                        ++popped;
                }
            });
        }
        for (std::thread& w : workers) w.join();
        while (a.Pop()) ++popped;
    }

    // The end: every node back exactly once, and the cache free for another pool.
    a.Destroy();
    assert(Live() == 0);

    // Workers of a fresh pool race to put a cache in: one does, the others' are given back.
    for (int round = 0; round < 1000; ++round) {
        Pool p;
        std::atomic<int> won{0}, ready{0};
        std::vector<std::thread> racers;
        for (int t = 0; t < 4; ++t) {
            racers.emplace_back([&] {
                ++ready;
                while (ready.load() < 4) {
                }
                won += Adopt(p.At(0), kGeneral, kClass);
            });
        }
        for (std::thread& r : racers) r.join();
        assert(won == 1);
        p.Append(7);
        assert(p.Pop() == 7);
        p.Destroy();
    }
    assert(Live() == 0);

    // Every cache taken: the next pool keeps the general allocator; one ends, and it gets one. (No
    // cache was lost above: all of them are free again.)
    {
        std::vector<Pool> pools(kCaches + 1);
        for (int i = 0; i < kCaches; ++i) assert(Adopt(pools[i].At(0), kGeneral, kClass));
        assert(!Adopt(pools[kCaches].At(0), kGeneral, kClass));
        pools[kCaches].Append(9);
        assert(pools[kCaches].Pop() == 9);
        pools[0].Destroy();
        assert(Adopt(pools[kCaches].At(0), kGeneral, kClass));
        for (int i = 1; i <= kCaches; ++i) pools[i].Destroy();
    }
    assert(Live() == 0);
}

// A function of synthetic bytes with two thread-pointer reads; each form at the reads gives the
// game's fingerprint, anything else none or another.
void FingerprintRun() {
    std::vector<std::uint8_t> code(0x60);
    for (std::size_t i = 0; i < code.size(); ++i) code[i] = static_cast<std::uint8_t>(i * 37 + 11);
    const std::size_t reads[] = {0x10, 0x40};
    const std::uint8_t fs_read[] = {0x64, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0, 0};
    for (const std::size_t r : reads) std::memcpy(code.data() + r, fs_read, sizeof fs_read);
    const auto game = Fingerprint(code.data(), code.size(), reads);
    assert(game && game == Fingerprint(code.data(), code.size())); // the game's bytes, read as they are
    const auto with = [&](auto edit) {
        std::vector<std::uint8_t> v = code;
        for (const std::size_t r : reads) edit(v.data() + r);
        return Fingerprint(v.data(), v.size(), reads);
    };
    assert(with([](std::uint8_t* p) { p[0] = 0x65; }) == game); // the link step: gs:[0]
    assert(with([](std::uint8_t* p) { p[0] = 0x65; p[5] = 0xa8; p[6] = 0x14; }) == game); // TEB slot 5
    assert(with([](std::uint8_t* p) { // past slot 63: jmp to a stub, four nops
        p[0] = 0xe9;
        p[1] = 0x78, p[2] = 0x56, p[3] = 0x34, p[4] = 0x12;
        std::memset(p + 5, 0x90, 4);
    }) == game);
    assert(!with([](std::uint8_t* p) { p[0] = 0xe8; }));                      // a call there
    assert(!with([](std::uint8_t* p) { p[5] = 1; }));                         // fs with a displacement
    assert(!with([](std::uint8_t* p) { p[0] = 0xe9; p[8] = 0xcc; }));         // a jump without its nops
    assert(!Fingerprint(code.data(), 0x45, reads));                           // a read past the end
    std::vector<std::uint8_t> other = code;
    other[0x30] ^= 1;
    assert(Fingerprint(other.data(), other.size(), reads) != game); // the rest is still checked
}

} // namespace

int main() {
    for (const std::int32_t spin : {0, 2000, 200000}) {
        LockRun(spin, 8, 20000, 0, true);
        std::printf("PASS: job lock, ours and the game's on one word, spin %d ticks\n", spin);
    }
    // Handles above 40 bits: ours compares the owner in its 40 bits (the game's would not).
    LockRun(2000, 4, 20000, 0x50000000000ull, false);
    std::puts("PASS: job lock with handles above 40 bits");
    LockAgainRun();
    std::puts("PASS: job lock taken again by its holder keeps its sleeper counted");
    NodeCacheRun();
    std::puts("PASS: job list node cache: stand-in, reuse, cap, pass-through, workers, end, race, all taken");
    FingerprintRun();
    std::puts("PASS: fingerprints: the game's, the link step's and the Windows loader's thread-pointer reads");
}

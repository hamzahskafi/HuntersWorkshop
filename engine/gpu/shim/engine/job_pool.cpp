// SPDX-License-Identifier: GPL-3.0-or-later
// The work pools' lock and job-list nodes (job_pool.h). Image offsets; Binary Ninja's are 0x400000
// higher.
#include "job_pool.h"
#include "engine_state.h"
#include "bbport_log_level.h"
#include <cstring>
#include <new>

#ifdef _WIN32
// The runtime's allocator for host memory the guest sees (src/runtime_memory.c).
extern "C" void* runtime_low_map(std::size_t size, int prot);
#endif

namespace BbEngine::JobPool {
namespace {
#define GUEST_ABI __attribute__((sysv_abi))

// The replaced and relied-on code, checked whole (Fingerprint), not only where a hook goes. None of
// them holds a relocation; Append has two thread-pointer reads (its out-of-memory path).
struct Whole {
    const char* differs; // the log's words when it does
    std::size_t offset, size;
    std::uint64_t hash;
    std::span<const std::size_t> reads = {};
};
constexpr std::size_t kAppendReads[] = {0x4c, 0xbd};
constexpr Whole kLockCode = {"DLAdaptiveMutex::Lock differs from 1.09", kLock, 0xe8, 0xd42072c375950c3dull};
constexpr Whole kNodeCode[] = {
    {"CSEzWork::ProcessOne differs from 1.09", kProcessOne, 0x1c2, 0xcf93e76a74851e7bull},
    {"CSEzWorkList::Append differs from 1.09", 0x2032780, 0xe9, 0x1198af2d0b7d6d94ull, kAppendReads},
    {"~CSEzWork differs from 1.09", 0x2031cf0, 0x3b9, 0xbaac3ee8cf6de4cfull},
};
// mov rax, fs:[0]
constexpr std::uint8_t kFsRead[] = {0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00};
// What the game's Lock calls: the import stubs of scePthreadSelf and sceKernelWaitSema (each a
// `jmp [rip + slot]`), and DL_PANIC with its file and text.
constexpr std::size_t kSelf = 0x2bbfe78, kWaitSema = 0x2bbec78, kPanic = 0x20b55b0;
constexpr std::size_t kPanicFile = 0x493b017, kPanicText = 0x493b0cb;
constexpr std::int32_t kPanicLine = 0xa9;
// The general allocator: the pointer (set by the game at start) and its class's vtable, with the
// aligned alloc and the free it holds.
constexpr std::size_t kGeneral = 0x5540420, kAllocatorVtable = 0x53aefa0;
constexpr std::size_t kGeneralAlloc = 0x1fc5f30, kGeneralFree = 0x1fc5fc0;
constexpr std::uint64_t kListAllocator = 0x28, kListSentinel = 0x18;

using SelfFn = std::uint64_t(GUEST_ABI*)();
using WaitFn = std::int32_t(GUEST_ABI*)(std::uint64_t, std::int32_t, std::uint64_t);
using PanicFn = void(GUEST_ABI*)(std::uint64_t, std::int32_t, std::uint64_t, ...); // al cleared, as the game
using AllocFn = std::uint64_t(GUEST_ABI*)(std::uint64_t, std::uint64_t, std::uint64_t);
using FreeFn = std::uint64_t(GUEST_ABI*)(std::uint64_t, std::uint64_t);

std::uint8_t* g_image = nullptr;
SelfFn g_self = nullptr;
WaitFn g_wait = nullptr;
PanicFn g_panic = nullptr;
std::uint64_t g_vtable = 0; // the general allocator's class, in the image
NodeCache* g_caches = nullptr;
std::atomic<bool> g_full{false}; // every cache taken: the pops don't look until one is given back
std::atomic<unsigned> g_adopted{0}, g_ended{0};

bool Report(unsigned n) {
    return n <= 5 || (n & (n - 1)) == 0;
}
// The standard log (BB_LOG_LEVEL): the first pool's cache, and the pools that ended after 100,000
// node allocations or more (the character and cloth workers', a few each area); detailed: as Report.
constexpr std::uint64_t kBusyPool = 100000;

// The game's read, the link step's (gs:[0]), the Windows loader's (gs:[disp], or jmp to a stub).
bool IsThreadPointerRead(const std::uint8_t* p) {
    if (p[0] == 0xe9) return p[5] == 0x90 && p[6] == 0x90 && p[7] == 0x90 && p[8] == 0x90;
    if (std::memcmp(p + 1, kFsRead + 1, 4)) return false;
    return p[0] == 0x65 || (p[0] == 0x64 && !std::memcmp(p + 5, kFsRead + 5, 4));
}

bool Is(const std::uint8_t* image, std::size_t size, const Whole& code) {
    if (code.offset + code.size > size) return false;
    const auto print = Fingerprint(image + code.offset, code.size, code.reads);
    return print && *print == code.hash;
}

bool IsStub(const std::uint8_t* image, std::size_t size, std::size_t offset) {
    return offset + 6 <= size && image[offset] == 0xff && image[offset + 1] == 0x25;
}

std::uint64_t Word(std::uint64_t at) {
    std::uint64_t value;
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof value);
    return value;
}

std::uint64_t Method(std::uint64_t object, std::uint64_t slot) {
    return Word(Word(object) + slot);
}

NodeCache& Of(std::uint64_t cache) {
    return *reinterpret_cast<NodeCache*>(cache);
}

// The cache's own fields. Every use already runs under the pool's queue lock or in its destructor
// (job_pool.h), so this is never waited on; it keeps the cache whole without relying on that.
class Hold {
public:
    explicit Hold(NodeCache& c) : c_(c) {
        while (c_.busy.exchange(true, std::memory_order_acquire))
            while (c_.busy.load(std::memory_order_relaxed)) _mm_pause();
    }
    ~Hold() {
        c_.busy.store(false, std::memory_order_release);
    }
    Hold(const Hold&) = delete;
    Hold& operator=(const Hold&) = delete;

private:
    NodeCache& c_;
};

} // namespace

// ---- The lock ---------------------------------------------------------------------------------------

GUEST_ABI std::int64_t Lock(std::uint64_t mutex, std::uint64_t) {
    const std::uint64_t self = g_self();
    auto& word = *reinterpret_cast<std::atomic<std::uint64_t>*>(mutex + 0x18);
    const auto* budget = reinterpret_cast<const std::int32_t*>(mutex + 0x8);
    Take(
        word, self, [budget] { return __atomic_load_n(budget, __ATOMIC_RELAXED); },
        [mutex] {
            if (g_wait(Word(mutex + 0x10), 1, 0) != 0)
                g_panic(reinterpret_cast<std::uint64_t>(g_image + kPanicFile), kPanicLine,
                        reinterpret_cast<std::uint64_t>(g_image + kPanicText));
        });
    return 0;
}

std::optional<std::uint64_t> Fingerprint(const std::uint8_t* code, std::size_t size,
                                         std::span<const std::size_t> reads) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    const auto take = [&h](std::uint8_t byte) { h = (h ^ byte) * 0x100000001b3ull; };
    std::size_t next = 0;
    for (std::size_t i = 0; i < size;) {
        if (next < reads.size() && i == reads[next]) {
            if (i + sizeof kFsRead > size || !IsThreadPointerRead(code + i)) return std::nullopt;
            for (const std::uint8_t byte : kFsRead) take(byte);
            i += sizeof kFsRead;
            ++next;
        } else {
            take(code[i++]);
        }
    }
    if (next != reads.size()) return std::nullopt;
    return h;
}

bool LockReady(std::uint8_t* image, std::size_t size, const char** why) {
    if (!Is(image, size, kLockCode)) {
        *why = kLockCode.differs;
        return false;
    }
    if (!IsStub(image, size, kSelf) || !IsStub(image, size, kWaitSema)) {
        *why = "an import stub Lock calls differs from 1.09";
        return false;
    }
    g_image = image;
    g_self = reinterpret_cast<SelfFn>(image + kSelf);
    g_wait = reinterpret_cast<WaitFn>(image + kWaitSema);
    g_panic = reinterpret_cast<PanicFn>(image + kPanic);
    return true;
}

// ---- The node cache ---------------------------------------------------------------------------------

GUEST_ABI std::uint64_t NodeAlloc(std::uint64_t cache, std::uint64_t size, std::uint64_t align) {
    NodeCache& c = Of(cache);
    if (size == kNodeSize && align <= 8) {
        Hold hold(c);
        if (c.top) {
            const std::uint64_t node = c.top;
            c.top = Word(node);
            --c.count;
            ++c.hits;
            return node;
        }
        ++c.misses;
    }
    return reinterpret_cast<AllocFn>(Method(c.general, kAllocSlot))(c.general, size, align);
}

GUEST_ABI std::uint64_t NodeFree(std::uint64_t cache, std::uint64_t block) {
    NodeCache& c = Of(cache);
    const std::uint64_t general = c.general;
    const auto pass = [general](std::uint64_t p) {
        return reinterpret_cast<FreeFn>(Method(general, kFreeSlot))(general, p);
    };
    if (block && block == c.sentinel) {
        // ~CSEzWork's last free: the pool is gone. Its nodes go back to the general allocator,
        // exactly as many as were counted in, and the cache to the free ones.
        std::uint64_t node, n, hits, misses, kept, passed;
        const std::uint64_t pool = c.pool;
        {
            Hold hold(c);
            node = c.top;
            n = c.count;
            hits = c.hits, misses = c.misses, kept = c.kept, passed = c.passed;
            c.top = 0;
            c.count = 0;
            c.sentinel = 0;
            c.pool = 0;
        }
        c.state.store(0, std::memory_order_release);
        g_full.store(false, std::memory_order_relaxed);
        for (; n && node; --n) {
            const std::uint64_t next = Word(node);
            pass(node);
            node = next;
        }
        const unsigned ended = g_ended.fetch_add(1) + 1;
        if (BbLogLevel() >= 1 ? Report(ended) : hits + misses >= kBusyPool)
            host_log("Game fixes: job pool %#llx ended; its node cache served %llu of %llu node allocations and "
                     "kept %llu of %llu frees (%u pools ended so far)",
                     static_cast<unsigned long long>(pool), static_cast<unsigned long long>(hits),
                     static_cast<unsigned long long>(hits + misses), static_cast<unsigned long long>(kept),
                     static_cast<unsigned long long>(kept + passed), ended);
        return pass(block);
    }
    if (block) {
        Hold hold(c);
        if (c.count < kCap) {
            std::memcpy(reinterpret_cast<void*>(block), &c.top, sizeof c.top);
            c.top = block;
            ++c.count;
            ++c.kept;
            return 0;
        }
        ++c.passed;
    }
    return pass(block);
}

bool SetUpCaches() {
    if (g_caches) return true;
    void* memory = nullptr;
#ifdef _WIN32
    memory = runtime_low_map(sizeof(NodeCache) * kCaches, 3);
#else
    static NodeCache storage[kCaches];
    memory = storage;
#endif
    if (!memory) return false;
    g_caches = new (memory) NodeCache[kCaches];
    return true;
}

bool Adopt(std::uint64_t pool, std::uint64_t general, std::uint64_t vtable) {
    auto* const slot = reinterpret_cast<std::uint64_t*>(pool + kListAllocator);
    if (!g_caches || !general || __atomic_load_n(slot, __ATOMIC_RELAXED) != general || Word(general) != vtable)
        return false;
    for (int i = 0; i < kCaches; ++i) {
        NodeCache& c = g_caches[i];
        std::uint32_t unused = 0;
        if (c.state.load(std::memory_order_relaxed) != 0 ||
            !c.state.compare_exchange_strong(unused, 1, std::memory_order_acquire))
            continue;
        for (int s = 0; s < kVtableSlots; ++s) c.slots[s] = Word(vtable + 8ull * s);
        c.slots[kAllocSlot / 8] = reinterpret_cast<std::uint64_t>(&NodeAlloc);
        c.slots[kFreeSlot / 8] = reinterpret_cast<std::uint64_t>(&NodeFree);
        c.vtable = reinterpret_cast<std::uint64_t>(&c.slots[0]);
        c.heap = Word(general + 8);
        c.general = general;
        c.sentinel = Word(pool + kListSentinel);
        c.pool = pool;
        c.top = 0;
        c.count = 0;
        c.hits = c.misses = c.kept = c.passed = 0;
        c.state.store(2, std::memory_order_release);
        // Another worker of the same pool may get there first: then this one is given back.
        std::uint64_t expected = general;
        if (!__atomic_compare_exchange_n(slot, &expected, reinterpret_cast<std::uint64_t>(&c), false,
                                         __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            c.pool = c.sentinel = 0;
            c.state.store(0, std::memory_order_release);
            return false;
        }
        const unsigned n = g_adopted.fetch_add(1) + 1;
        if (BbLogLevel() >= 1 ? Report(n) : n == 1)
            host_log("Game fixes: job pool %#llx's list nodes now kept by a cache of its own (%u pools so far)",
                     static_cast<unsigned long long>(pool), n);
        return true;
    }
    if (!g_full.exchange(true, std::memory_order_relaxed)) {
        static std::atomic<bool> said{false};
        if (!said.exchange(true))
            host_log("Game fixes: all %d job-pool node caches are taken; a later pool keeps the general allocator "
                     "until one ends",
                     kCaches);
    }
    return false;
}

GUEST_ABI std::int64_t PoolPop(std::uint64_t, const std::uint64_t* saved) {
    if (g_full.load(std::memory_order_relaxed)) return 0;
    const std::uint64_t pool = saved[5];
    const std::uint64_t general = __atomic_load_n(reinterpret_cast<const std::uint64_t*>(g_image + kGeneral),
                                                  __ATOMIC_RELAXED);
    if (general && __atomic_load_n(reinterpret_cast<const std::uint64_t*>(pool + kListAllocator),
                                   __ATOMIC_RELAXED) == general)
        Adopt(pool, general, g_vtable);
    return 0; // the game's ProcessOne runs on
}

bool NodesReady(std::uint8_t* image, std::size_t size, const char** why) {
    for (const Whole& code : kNodeCode) {
        if (!Is(image, size, code)) {
            *why = code.differs;
            return false;
        }
    }
    // The class: 27 methods (the next word is the padding before the next vtable), the aligned
    // alloc and the free where job_pool.h says.
    const std::uint64_t vtable = reinterpret_cast<std::uint64_t>(image + kAllocatorVtable);
    bool same = kAllocatorVtable + 8 * (kVtableSlots + 1) <= size && kGeneral + 8 <= size;
    for (int s = 0; same && s < kVtableSlots; ++s) same = Word(vtable + 8ull * s) != 0;
    if (!same || Word(vtable + 8ull * kVtableSlots) != 0 ||
        Word(vtable + kAllocSlot) != reinterpret_cast<std::uint64_t>(image + kGeneralAlloc) ||
        Word(vtable + kFreeSlot) != reinterpret_cast<std::uint64_t>(image + kGeneralFree)) {
        *why = "the general allocator's class differs from 1.09";
        return false;
    }
    if (!SetUpCaches()) {
        *why = "no low memory for the node caches";
        return false;
    }
    g_image = image;
    g_vtable = vtable;
    return true;
}

} // namespace BbEngine::JobPool

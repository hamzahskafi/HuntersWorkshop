// SPDX-License-Identifier: GPL-3.0-or-later
// FMOD's sound-group ring guard (fmod_ring.h). Image offsets; Binary Ninja's are 0x400000 higher.
#include "fmod_ring.h"
#include "engine_state.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_set>

namespace BbEngine::FmodRing {
namespace {

// SoundI::release frees its shared subsound here (fmod_soundi.cpp line 0x367): a call to MemPool's
// free, whose return address tells that free from the others.
constexpr std::size_t kSharedFreeCall = 0xdbe682, kSharedFreeReturn = kSharedFreeCall + 5;
// FMOD's critical section: the pointer its enter and leave take (a recursive scePthreadMutex).
constexpr std::size_t kCrit = 0x546d260, kCritEnter = 0xd1acd0, kCritLeave = 0xd1acf0;
// push rbp; mov rbp, rsp; xor eax, eax; test rdi, rdi; je; call scePthreadMutexLock / Unlock
constexpr std::uint8_t kCritEnterCode[] = {0x55, 0x48, 0x89, 0xe5, 0x31, 0xc0, 0x48, 0x85,
                                           0xff, 0x74, 0x11, 0xe8, 0x18, 0x52, 0xea, 0x01};
constexpr std::uint8_t kCritLeaveCode[] = {0x55, 0x48, 0x89, 0xe5, 0x31, 0xc0, 0x48, 0x85,
                                           0xff, 0x74, 0x11, 0xe8, 0x08, 0x52, 0xea, 0x01};
// The SoundI classes' vtables as objects hold them: SampleSoftware, SoundI, the bank container
// (Sample) and Stream.
constexpr std::size_t kSoundVtables[] = {0x52d7640, 0x52d7840, 0x52d7a30, 0x52d7c30};

std::uint8_t* g_image = nullptr;
std::atomic<unsigned> g_guarded{0}, g_kept_count{0}, g_kept_freed{0}, g_kept_live{0};
std::mutex g_kept_mu;
std::unordered_set<std::uint64_t> g_kept; // under g_kept_mu: refusing shared subsounds kept

bool Report(unsigned n) {
    return n <= 5 || (n & (n - 1)) == 0;
}

const auto Read = [](std::uint64_t at, auto* out) {
    return at >= 0x10000 && host_read_safe(reinterpret_cast<const void*>(at), out, sizeof *out);
};
const auto Write = [](std::uint64_t at, auto value) { std::memcpy(reinterpret_cast<void*>(at), &value, sizeof value); };

// A block MemPool frees is live memory: its first word is read directly.
bool IsSound(std::uint64_t block) {
    std::uint64_t vtable = 0;
    std::memcpy(&vtable, reinterpret_cast<const void*>(block), sizeof vtable);
    for (const std::size_t v : kSoundVtables) {
        if (vtable == reinterpret_cast<std::uint64_t>(g_image + v)) return true;
    }
    return false;
}

// FMOD's critical section, taken as FMOD takes it (recursive: MemPool's free may run inside it).
struct Crit {
    std::uint64_t crit = 0;
    Crit() {
        std::memcpy(&crit, g_image + kCrit, sizeof crit);
        if (crit) Call(reinterpret_cast<std::uint64_t>(g_image + kCritEnter), crit);
    }
    ~Crit() {
        if (crit) Call(reinterpret_cast<std::uint64_t>(g_image + kCritLeave), crit);
    }
    Crit(const Crit&) = delete;
    Crit& operator=(const Crit&) = delete;
};

} // namespace

// saved[4] rsi: the block; saved[6]: the return address. Nonzero: the free is not run (its caller
// ignores the result).
__attribute__((sysv_abi)) std::int64_t PoolFree(std::uint64_t, const std::uint64_t* saved) {
    const std::uint64_t block = saved[4];
    if (block < 0x10000 || !IsSound(block)) return 0;
    if (g_kept_live.load(std::memory_order_relaxed)) {
        std::lock_guard lock(g_kept_mu);
        if (g_kept.erase(block)) { // one kept while its release ran on another thread, freed by it
            g_kept_live.fetch_sub(1, std::memory_order_relaxed);
            const unsigned freed = g_kept_freed.fetch_add(1) + 1;
            if (freed <= 16)
                host_log("Game fixes: FMOD: kept sound %#llx freed by its own release (%u so far)",
                         static_cast<unsigned long long>(block), freed);
        }
    }
    if (!Linked(block, Read)) return 0;
    const bool keep =
        saved[6] == reinterpret_cast<std::uint64_t>(g_image + kSharedFreeReturn) && RefusedShared(block, Read);
    bool done = false;
    {
        Crit crit;
        done = Unlink(block, Read, Write);
        if (done && keep) Orphan(block, Write);
    }
    if (!done) return 0;
    const unsigned n = g_guarded.fetch_add(1) + 1;
    if (keep) {
        std::lock_guard lock(g_kept_mu);
        if (g_kept.insert(block).second) g_kept_live.fetch_add(1, std::memory_order_relaxed);
        g_kept_count.fetch_add(1, std::memory_order_relaxed);
    }
    if (Report(n)) {
        std::uint32_t flags = 0;
        Read(block + kFlags, &flags);
        host_log(keep ? "Game fixes: FMOD: a stream's shared subsound %#llx (flags %#x) that refused its release was "
                        "about to be freed in its sound group; taken out of the ring and kept as a sound of its own "
                        "(%u so far)"
                      : "Game fixes: FMOD: sound %#llx (flags %#x) freed while still in its sound group; taken out of "
                        "the ring first (%u so far)",
                 static_cast<unsigned long long>(block), flags, n);
    }
    return keep ? 1 : 0;
}

bool Check(std::uint8_t* image, std::size_t size) {
    const auto at = [&](std::size_t offset, const std::uint8_t* code, std::size_t n) {
        return offset + n <= size && !std::memcmp(image + offset, code, n);
    };
    // The shared subsound's free: a call to MemPool's free.
    std::int32_t rel = 0;
    if (kSharedFreeReturn > size || image[kSharedFreeCall] != 0xe8) return false;
    std::memcpy(&rel, image + kSharedFreeCall + 1, sizeof rel);
    if (static_cast<std::int64_t>(kSharedFreeReturn) + rel != static_cast<std::int64_t>(kPoolFree)) return false;
    if (!at(kCritEnter, kCritEnterCode, sizeof kCritEnterCode) || !at(kCritLeave, kCritLeaveCode, sizeof kCritLeaveCode) ||
        kCrit + 8 > size)
        return false;
    g_image = image;
    return true;
}

} // namespace BbEngine::FmodRing

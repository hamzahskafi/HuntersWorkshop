// SPDX-License-Identifier: GPL-3.0-or-later
// The game's own GX resource calls (gx_resources.h). Sites from bbhost's engine/gx_resources.cpp and
// hle/gx_trace.cpp (its addresses less 0x400000, checked against 1.09's bytes); see
// licenses/BBHOST-PORTS.md.
//
//   creators   0x2165c10 CreateBuffer, 0x2165f90 CreateTexture (1D/2D), 0x2166300 CreateTexture3D:
//              (ctx, holder** out, desc*, initial data*, ...). The holder exists once the method
//              returns, so the entry hook keeps the return address and points it at a return stub
//              (ReturnStub), which reads *out.
//   T# builder 0x2ba6af0 (size*, T# out, desc, subresource info, scratch), run by the 2D creator
//              (through 0x2171d60) before the memory is allocated: its T# out is read at its return
//              (through the same stub) and given the texture's memory as its base at the creator's.
//   destroyers a texture holder's deleting and in-place destructors (0x216d1a0 / 0x216d1e0; 3D
//              0x216d310 / 0x216d350; rdi = holder), and a buffer's release (0x216d690, rdi =
//              holder + 0x18). The destructors' fifth instruction is a rip-relative lea of the
//              holder's vtable: the stub runs it as a movabs of the absolute address.
//   shaders    the creators 0x2166d00 (vertex, and stage types 5 and 6), 0x2166f20 (vertex and 5),
//              0x21672d0 (pixel), 0x21673e0 (compute): counted (research).
//   a holder   +0x20 its memory, +0x2c its size in bytes (u32), +0x38 its type (1: buffer; 2 and 3
//              textures).
// 1D textures have no creator of their own (GX builds their holder inline): not counted.
#include "gx_resources.h"
#include "engine_hooks.h"
#include "engine_state.h"
#include "loading.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace BbEngine::GxResources {
namespace {
#define GUEST_ABI __attribute__((sysv_abi))

constexpr std::size_t kCreateBuffer = 0x2165c10, kCreateTexture = 0x2165f90, kCreateTexture3D = 0x2166300;
// push rbp; mov rbp, rsp; push r15; push r14; push r13; push r12; push rbx; sub rsp, 0x58
constexpr std::uint8_t kCreatorPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                             0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x58};
constexpr std::size_t kBufferRelease = 0x216d690;
// push rbp; mov rbp, rsp; push r15; push r14; push rbx; push rax; mov r14, rdi; mov rdi, [r14]
constexpr std::uint8_t kReleasePrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
                                             0x53, 0x50, 0x49, 0x89, 0xfe, 0x49, 0x8b, 0x3e};
// The texture destructors: push rbp; mov rbp, rsp; push rbx; push rax; mov rbx, rdi; lea rax, [rip+d]
struct Destructor {
    std::size_t site;
    std::uint32_t disp; // the lea's displacement
    std::size_t vtable; // its target
};
constexpr Destructor kDestructors[] = {
    {0x216d1a0, 0x031f2230, 0x535f3e0}, // 2D, deleting
    {0x216d1e0, 0x031f21f0, 0x535f3e0}, // 2D, in place
    {0x216d310, 0x031f20e0, 0x535f400}, // 3D, deleting
    {0x216d350, 0x031f20a0, 0x535f400}, // 3D, in place
};

constexpr std::size_t kTsharpBuild = 0x2ba6af0;
// push rbp; mov rbp, rsp; push r15; push r14; push r13; push r12; push rbx; and rsp, -32
constexpr std::uint8_t kTsharpPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
                                            0x41, 0x54, 0x53, 0x48, 0x81, 0xe4, 0xe0, 0xff, 0xff, 0xff};
// The shader creators: push rbp; mov rbp, rsp; push r15; push r14; push rbx; sub rsp, 0x138 (0x128)
constexpr std::size_t kShaderCreators[] = {0x2166d00, 0x2166f20, 0x21672d0, 0x21673e0};
constexpr std::uint8_t kShaderPrologue[2][16] = {
    {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x81, 0xec, 0x38, 0x01, 0x00, 0x00},
    {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x81, 0xec, 0x28, 0x01, 0x00, 0x00},
};

enum Kind : std::uint8_t { kBuffer = 0, kTexture = 1, kTexture3D = 2, kKinds = 3 };

struct Entry {
    std::uint64_t memory;
    std::uint32_t bytes;
    Kind kind;
    bool loading;
    std::uint64_t ns;
    std::uint8_t type;
};

struct Pending {
    std::uint64_t ret;
    std::uint64_t out; // creators: holder**; the T# builder: the T# out
    Kind kind;
    bool initial = false; // creators: initial data given
    bool tsharp = false;  // the T# builder
};
thread_local std::vector<Pending> t_pending;
// The T# the builder produced on this thread, for its creator's return.
thread_local std::array<std::uint32_t, 8> t_tsharp{};
thread_local bool t_tsharp_valid = false;

std::mutex g_mutex;
std::unordered_map<std::uint64_t, Entry> g_by_holder;
std::unordered_map<std::uint64_t, std::uint64_t> g_texture_at; // memory -> holder (live textures)
std::vector<Gone> g_gone, g_released;
std::vector<Ahead> g_ahead;
std::uint8_t* g_return_stub = nullptr;
bool g_active = false, g_follow = false;
std::atomic<DestroyedHook> g_destroyed_hook{nullptr};
std::atomic<std::uint64_t> g_live_texture_bytes{0};
constexpr std::size_t kQueueCap = 65536, kAheadCap = 16384;

struct Counts {
    std::array<std::atomic<std::uint64_t>, kKinds> created{}, created_bytes{}, created_loading{}, destroyed{},
        destroyed_bytes{};
    std::atomic<std::uint64_t> destroyed_unknown{0}, failed{0}, gone_dropped{0};
    // The texture cache's side.
    std::atomic<std::uint64_t> images_matched{0}, images_unmatched{0}, matched_fast{0}, matched_mid{0},
        matched_slow{0}, matched_loading{0};
    std::atomic<std::uint64_t> destroyed_with_image{0}, destroyed_without_image{0}, destroyed_image_bytes{0};
    std::atomic<std::uint64_t> dead_images{0}, dead_bytes{0};
    // Following (beta 16).
    std::atomic<std::uint64_t> ahead_queued{0}, ahead_queue_full{0}, ahead_no_tsharp{0}, ahead_built{0},
        ahead_built_bytes{0}, ahead_existed{0}, ahead_gone{0}, ahead_old{0}, ahead_invalid{0}, ahead_hits{0},
        ahead_misses{0}, ahead_unused{0}, ahead_unused_bytes{0}, ahead_waiting{0};
    std::atomic<std::uint64_t> dead_freed{0}, dead_freed_bytes{0};
    // Research (beta 16).
    std::atomic<std::uint64_t> released_checked{0}, released_resident{0}, released_resident_bytes{0};
    std::array<std::atomic<std::uint64_t>, 4> shaders{};
    std::atomic<std::uint64_t> shaders_loading{0};
};
Counts g_counts;

std::uint64_t NowNs() {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count());
}

template <typename T> T Read(std::uint64_t address) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), sizeof(T));
    return value;
}

Entry Register(std::uint64_t holder, Kind kind) {
    Entry e{Read<std::uint64_t>(holder + 0x20), Read<std::uint32_t>(holder + 0x2c), kind,
            Loading::ScreenUp(), NowNs(), Read<std::uint8_t>(holder + 0x38)};
    g_counts.created[kind].fetch_add(1, std::memory_order_relaxed);
    g_counts.created_bytes[kind].fetch_add(e.bytes, std::memory_order_relaxed);
    if (e.loading) {
        g_counts.created_loading[kind].fetch_add(1, std::memory_order_relaxed);
    }
    std::scoped_lock lock{g_mutex};
    if (const auto old = g_by_holder.find(holder); old != g_by_holder.end() && old->second.kind != kBuffer) {
        g_live_texture_bytes.fetch_sub(old->second.bytes, std::memory_order_relaxed); // reused unseen
    }
    if (kind != kBuffer) {
        g_live_texture_bytes.fetch_add(e.bytes, std::memory_order_relaxed);
        if (e.memory) {
            g_texture_at[e.memory] = holder;
        }
    }
    g_by_holder[holder] = e;
    return e;
}

void Unregister(std::uint64_t holder, bool texture) {
    std::uint64_t memory = 0, bytes = 0;
    {
        std::scoped_lock lock{g_mutex};
        const auto it = g_by_holder.find(holder);
        if (it == g_by_holder.end()) {
            g_counts.destroyed_unknown.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const Entry e = it->second;
        g_by_holder.erase(it);
        g_counts.destroyed[e.kind].fetch_add(1, std::memory_order_relaxed);
        g_counts.destroyed_bytes[e.kind].fetch_add(e.bytes, std::memory_order_relaxed);
        if (e.kind != kBuffer) {
            g_live_texture_bytes.fetch_sub(e.bytes, std::memory_order_relaxed);
        }
        if (!e.memory) {
            return;
        }
        if (!texture || e.kind == kBuffer) {
            // For the buffer cache (research); bounded should it not run.
            if (e.kind == kBuffer && g_released.size() < kQueueCap) {
                g_released.push_back({e.memory, e.bytes, NowNs()});
            }
            return;
        }
        if (const auto at = g_texture_at.find(e.memory); at != g_texture_at.end() && at->second == holder) {
            g_texture_at.erase(at);
        }
        // For the texture cache's collector; bounded should it not run (no renderer yet).
        if (g_gone.size() < kQueueCap) {
            g_gone.push_back({e.memory, e.bytes, NowNs()});
        } else {
            g_counts.gone_dropped.fetch_add(1, std::memory_order_relaxed);
        }
        memory = e.memory;
        bytes = e.bytes;
    }
    // The texture cache stops watching its pages now, on this thread, before the game writes the
    // next texture there (without it each page faulted once on the game's thread: thousands a
    // loading screen).
    if (g_follow && bytes != 0) {
        if (const DestroyedHook hook = g_destroyed_hook.load(std::memory_order_acquire)) {
            hook(memory, bytes);
        }
    }
}

// A 2D texture created with its data and its T#: queued for the texture cache to build ahead.
void QueueAhead(const Entry& e) {
    if (!t_tsharp_valid) {
        g_counts.ahead_no_tsharp.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (e.memory == 0 || (e.type != 2 && e.type != 3)) {
        return;
    }
    Ahead job{t_tsharp, e.memory, e.bytes, e.ns, e.loading};
    // The builder runs before the memory is allocated: the base (address >> 8, 38 bits over the first
    // word and the low 6 bits of the second) is filled in here.
    const std::uint64_t base = e.memory >> 8;
    job.tsharp[0] = static_cast<std::uint32_t>(base);
    job.tsharp[1] = (job.tsharp[1] & ~0x3fu) | static_cast<std::uint32_t>((base >> 32) & 0x3f);
    std::scoped_lock lock{g_mutex};
    if (g_ahead.size() >= kAheadCap) {
        g_counts.ahead_queue_full.fetch_add(1, std::memory_order_relaxed); // built at its first draw
        return;
    }
    g_ahead.push_back(job);
    g_counts.ahead_queued.fetch_add(1, std::memory_order_relaxed);
}

// The creators' entry: keep the return address and the out pointer (rsi), return through the stub.
template <Kind kind> GUEST_ABI std::int64_t CreatorEntry(std::uint64_t, const std::uint64_t* saved) {
    t_pending.push_back({saved[6], saved[4], kind, saved[2] != 0, false});
    const_cast<std::uint64_t*>(saved)[6] = reinterpret_cast<std::uint64_t>(g_return_stub);
    return 0;
}

// The T# builder: inside the 2D creator only (rsi = the T# out), read at its return.
GUEST_ABI std::int64_t TsharpEntry(std::uint64_t, const std::uint64_t* saved) {
    if (!t_pending.empty() && !t_pending.back().tsharp && t_pending.back().kind == kTexture) {
        t_pending.push_back({saved[6], saved[4], kTexture, false, true});
        const_cast<std::uint64_t*>(saved)[6] = reinterpret_cast<std::uint64_t>(g_return_stub);
    }
    return 0;
}

template <int stage> GUEST_ABI std::int64_t ShaderCreated(std::uint64_t, const std::uint64_t*) {
    g_counts.shaders[stage].fetch_add(1, std::memory_order_relaxed);
    if (Loading::ScreenUp()) {
        g_counts.shaders_loading.fetch_add(1, std::memory_order_relaxed);
    }
    return 0;
}

GUEST_ABI std::uint64_t CreatorReturn(std::uint64_t result) {
    if (t_pending.empty()) {
        // A return through the stub without its entry: nothing to go back to (cannot happen unless
        // the method unwound past its entry's hook).
        host_log("GX resources: a creator returned with no record of its call; stopping");
        std::abort();
    }
    const Pending p = t_pending.back();
    t_pending.pop_back();
    if (p.tsharp) {
        t_tsharp_valid = p.out != 0;
        if (t_tsharp_valid) {
            t_tsharp = Read<std::array<std::uint32_t, 8>>(p.out);
        }
        return p.ret;
    }
    if (static_cast<std::uint32_t>(result) != 0) {
        g_counts.failed.fetch_add(1, std::memory_order_relaxed);
    } else if (p.out) {
        if (const auto holder = Read<std::uint64_t>(p.out)) {
            const Entry e = Register(holder, p.kind);
            if (g_follow && p.kind == kTexture && p.initial) {
                QueueAhead(e);
            }
        }
    }
    t_tsharp_valid = false;
    return p.ret;
}

GUEST_ABI std::int64_t TextureDestructor(std::uint64_t, const std::uint64_t* saved) {
    Unregister(saved[5], true);
    return 0;
}

GUEST_ABI std::int64_t BufferRelease(std::uint64_t, const std::uint64_t* saved) {
    Unregister(saved[5] - 0x18, false);
    return 0;
}

double Mib(std::uint64_t bytes) {
    return double(bytes) / (1024.0 * 1024.0);
}
} // namespace

bool Install(std::uint8_t* image, std::size_t size) {
    if (const char* env = std::getenv("BB_GX_RESOURCES"); env && env[0] == '0') {
        return false;
    }
    g_return_stub = ReturnStub(&CreatorReturn);
    if (!g_return_stub) {
        return false;
    }
    if (const char* env = std::getenv("BB_GX_FOLLOW"); !(env && env[0] == '0')) {
        g_follow = true;
    }
    Hook hooks[13];
    bool ok = Prepare(hooks[0], image, size, kCreateBuffer, kCreatorPrologue, &CreatorEntry<kBuffer>) &&
              Prepare(hooks[1], image, size, kCreateTexture, kCreatorPrologue, &CreatorEntry<kTexture>) &&
              Prepare(hooks[2], image, size, kCreateTexture3D, kCreatorPrologue, &CreatorEntry<kTexture3D>) &&
              Prepare(hooks[3], image, size, kBufferRelease, kReleasePrologue, &BufferRelease) &&
              Prepare(hooks[8], image, size, kTsharpBuild, kTsharpPrologue, &TsharpEntry) &&
              Prepare(hooks[9], image, size, kShaderCreators[0], kShaderPrologue[0], &ShaderCreated<0>) &&
              Prepare(hooks[10], image, size, kShaderCreators[1], kShaderPrologue[1], &ShaderCreated<1>) &&
              Prepare(hooks[11], image, size, kShaderCreators[2], kShaderPrologue[1], &ShaderCreated<2>) &&
              Prepare(hooks[12], image, size, kShaderCreators[3], kShaderPrologue[1], &ShaderCreated<3>);
    for (std::size_t i = 0; ok && i < std::size(kDestructors); ++i) {
        const Destructor& d = kDestructors[i];
        std::uint8_t expected[16] = {0x55, 0x48, 0x89, 0xe5, 0x53, 0x50, 0x48, 0x89, 0xfb, 0x48, 0x8d, 0x05};
        std::memcpy(expected + 12, &d.disp, 4);
        // The same, the lea as movabs rax, <the vtable's address in this image>.
        std::uint8_t displaced[19] = {0x55, 0x48, 0x89, 0xe5, 0x53, 0x50, 0x48, 0x89, 0xfb, 0x48, 0xb8};
        const std::uint64_t vtable = reinterpret_cast<std::uint64_t>(image) + d.vtable;
        std::memcpy(displaced + 11, &vtable, 8);
        ok = d.site + 16 + d.disp == d.vtable && // the lea's target, from its own bytes
             PrepareDisplaced(hooks[4 + i], image, size, d.site, expected, displaced, &TextureDestructor);
    }
    if (!ok) {
        for (auto& hook : hooks) {
            Discard(hook);
        }
        FreeStub(g_return_stub);
        g_return_stub = nullptr;
        g_follow = false;
        host_log("GX resources: the game's creators or destructors differ from 1.09: not listened to");
        return false;
    }
    Commit(hooks);
    g_active = true;
    host_log(g_follow ? "GX resources: the texture cache follows the game's textures: built ahead when the game "
                        "creates them with their data (during loading screens, below the VRAM manager's "
                        "comfortable line), freed when it destroys them (BB_GX_FOLLOW=0: counted only; "
                        "BB_GX_RESOURCES=0: off)"
                      : "GX resources: listening to the game's texture and buffer creators and destructors "
                        "(counted only, BB_GX_FOLLOW=0; BB_GX_RESOURCES=0: off)");
    return true;
}

bool Active() {
    return g_active;
}

bool Following() {
    return g_follow;
}

void TakeDestroyed(std::vector<Gone>& out) {
    std::scoped_lock lock{g_mutex};
    out.swap(g_gone);
    g_gone.clear();
}

void TakeReleased(std::vector<Gone>& out) {
    std::scoped_lock lock{g_mutex};
    out.swap(g_released);
    g_released.clear();
}

void TakeAhead(std::vector<Ahead>& out) {
    std::scoped_lock lock{g_mutex};
    out.swap(g_ahead);
    g_ahead.clear();
}

bool StillLive(std::uint64_t memory, std::uint64_t ns) {
    std::scoped_lock lock{g_mutex};
    const auto at = g_texture_at.find(memory);
    if (at == g_texture_at.end()) {
        return false;
    }
    const auto it = g_by_holder.find(at->second);
    return it != g_by_holder.end() && it->second.ns == ns;
}

void SetDestroyedHook(DestroyedHook hook) {
    g_destroyed_hook.store(hook, std::memory_order_release);
}

Made TextureAt(std::uint64_t memory) {
    Made made{false, 0, false};
    if (!g_active) {
        return made;
    }
    std::scoped_lock lock{g_mutex};
    const auto at = g_texture_at.find(memory);
    if (at == g_texture_at.end()) {
        return made;
    }
    const auto it = g_by_holder.find(at->second);
    if (it == g_by_holder.end()) {
        return made;
    }
    made.found = true;
    made.age_ns = NowNs() - it->second.ns;
    made.during_loading = it->second.loading;
    return made;
}

void NoteImage(const Made& made) {
    if (!g_active) {
        return;
    }
    if (!made.found) {
        g_counts.images_unmatched.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_counts.images_matched.fetch_add(1, std::memory_order_relaxed);
    (made.age_ns < 500'000'000ull     ? g_counts.matched_fast
     : made.age_ns < 2'000'000'000ull ? g_counts.matched_mid
                                      : g_counts.matched_slow)
        .fetch_add(1, std::memory_order_relaxed);
    if (made.during_loading) {
        g_counts.matched_loading.fetch_add(1, std::memory_order_relaxed);
    }
}

void NoteDestroyedImage(bool had_image, std::uint64_t image_bytes) {
    (had_image ? g_counts.destroyed_with_image : g_counts.destroyed_without_image)
        .fetch_add(1, std::memory_order_relaxed);
    g_counts.destroyed_image_bytes.fetch_add(image_bytes, std::memory_order_relaxed);
}

void NoteDeadInVram(std::uint64_t images, std::uint64_t bytes) {
    g_counts.dead_images.store(images, std::memory_order_relaxed);
    g_counts.dead_bytes.store(bytes, std::memory_order_relaxed);
}

void NoteDeadFreed(std::uint64_t bytes) {
    g_counts.dead_freed.fetch_add(1, std::memory_order_relaxed);
    g_counts.dead_freed_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void NoteAhead(AheadResult result, std::uint64_t bytes) {
    switch (result) {
    case AheadResult::Built:
        g_counts.ahead_built.fetch_add(1, std::memory_order_relaxed);
        g_counts.ahead_built_bytes.fetch_add(bytes, std::memory_order_relaxed);
        break;
    case AheadResult::Existed:
        g_counts.ahead_existed.fetch_add(1, std::memory_order_relaxed);
        break;
    case AheadResult::Gone:
        g_counts.ahead_gone.fetch_add(1, std::memory_order_relaxed);
        break;
    case AheadResult::TooOld:
        g_counts.ahead_old.fetch_add(1, std::memory_order_relaxed);
        break;
    case AheadResult::Invalid:
        g_counts.ahead_invalid.fetch_add(1, std::memory_order_relaxed);
        break;
    }
}

void NoteAheadHit() {
    g_counts.ahead_hits.fetch_add(1, std::memory_order_relaxed);
}

void NoteAheadMiss() {
    g_counts.ahead_misses.fetch_add(1, std::memory_order_relaxed);
}

void NoteAheadUnused(std::uint64_t bytes) {
    g_counts.ahead_unused.fetch_add(1, std::memory_order_relaxed);
    g_counts.ahead_unused_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void NoteAheadWaiting(std::uint64_t jobs) {
    g_counts.ahead_waiting.store(jobs, std::memory_order_relaxed);
}

void NoteReleasedBuffer(std::uint64_t resident_bytes) {
    g_counts.released_checked.fetch_add(1, std::memory_order_relaxed);
    if (resident_bytes != 0) {
        g_counts.released_resident.fetch_add(1, std::memory_order_relaxed);
        g_counts.released_resident_bytes.fetch_add(resident_bytes, std::memory_order_relaxed);
    }
}

void LogMinute() {
    if (!g_active) {
        return;
    }
    auto take = [](std::atomic<std::uint64_t>& a) { return a.exchange(0, std::memory_order_relaxed); };
    std::uint64_t created[kKinds], created_bytes[kKinds], loading[kKinds], destroyed[kKinds], destroyed_bytes[kKinds];
    for (int k = 0; k < kKinds; ++k) {
        created[k] = take(g_counts.created[k]);
        created_bytes[k] = take(g_counts.created_bytes[k]);
        loading[k] = take(g_counts.created_loading[k]);
        destroyed[k] = take(g_counts.destroyed[k]);
        destroyed_bytes[k] = take(g_counts.destroyed_bytes[k]);
    }
    std::size_t live = 0, live_textures = 0;
    {
        std::scoped_lock lock{g_mutex};
        live = g_by_holder.size();
        live_textures = g_texture_at.size();
    }
    const std::uint64_t matched = take(g_counts.images_matched), unmatched = take(g_counts.images_unmatched);
    const std::uint64_t fast = take(g_counts.matched_fast), mid = take(g_counts.matched_mid),
                        slow = take(g_counts.matched_slow), matched_loading = take(g_counts.matched_loading);
    const std::uint64_t with_image = take(g_counts.destroyed_with_image),
                        without_image = take(g_counts.destroyed_without_image),
                        image_bytes = take(g_counts.destroyed_image_bytes);
    host_log("GX resources this minute: textures created %llu (%.0f MiB, %llu during loading screens), 3D %llu, "
             "destroyed %llu (%.0f MiB) + 3D %llu; buffers created %llu (%.0f MiB, %llu during loading screens), "
             "released %llu (%.0f MiB); %llu failed, %llu destroyed unknown; live %zu (%zu textures, %.0f MiB)",
             (unsigned long long)created[kTexture], Mib(created_bytes[kTexture]),
             (unsigned long long)loading[kTexture], (unsigned long long)created[kTexture3D],
             (unsigned long long)destroyed[kTexture], Mib(destroyed_bytes[kTexture]),
             (unsigned long long)destroyed[kTexture3D], (unsigned long long)created[kBuffer],
             Mib(created_bytes[kBuffer]), (unsigned long long)loading[kBuffer],
             (unsigned long long)destroyed[kBuffer], Mib(destroyed_bytes[kBuffer]),
             (unsigned long long)take(g_counts.failed), (unsigned long long)take(g_counts.destroyed_unknown), live,
             live_textures, Mib(g_live_texture_bytes.load(std::memory_order_relaxed)));
    host_log("GX resources against the texture cache: images made %llu, %llu of them at a live game texture "
             "(%llu under 0.5 s after the game made it, %llu under 2 s, %llu later; %llu made during a loading "
             "screen); textures destroyed with an image still there %llu (%.0f MiB), without %llu; images of "
             "destroyed textures still in VRAM, unused since: %llu (%.0f MiB)%s",
             (unsigned long long)(matched + unmatched), (unsigned long long)matched, (unsigned long long)fast,
             (unsigned long long)mid, (unsigned long long)slow, (unsigned long long)matched_loading,
             (unsigned long long)with_image, Mib(image_bytes), (unsigned long long)without_image,
             (unsigned long long)g_counts.dead_images.load(std::memory_order_relaxed),
             Mib(g_counts.dead_bytes.load(std::memory_order_relaxed)),
             g_counts.gone_dropped.load(std::memory_order_relaxed) ? " (some destroyed textures not checked)" : "");
    if (g_follow) {
        host_log("GX follow this minute: %llu textures queued to build ahead (%llu without a T#, %llu over the "
                 "queue), %llu built (%.0f MiB), %llu already there, %llu destroyed first, %llu too old, %llu not "
                 "buildable, %llu waiting; drawn %llu, a draw made another image %llu times, freed unused %llu "
                 "(%.0f MiB); images freed when the game destroyed their texture %llu (%.0f MiB)",
                 (unsigned long long)take(g_counts.ahead_queued), (unsigned long long)take(g_counts.ahead_no_tsharp),
                 (unsigned long long)take(g_counts.ahead_queue_full), (unsigned long long)take(g_counts.ahead_built),
                 Mib(take(g_counts.ahead_built_bytes)), (unsigned long long)take(g_counts.ahead_existed),
                 (unsigned long long)take(g_counts.ahead_gone), (unsigned long long)take(g_counts.ahead_old),
                 (unsigned long long)take(g_counts.ahead_invalid),
                 (unsigned long long)g_counts.ahead_waiting.load(std::memory_order_relaxed),
                 (unsigned long long)take(g_counts.ahead_hits), (unsigned long long)take(g_counts.ahead_misses),
                 (unsigned long long)take(g_counts.ahead_unused), Mib(take(g_counts.ahead_unused_bytes)),
                 (unsigned long long)take(g_counts.dead_freed), Mib(take(g_counts.dead_freed_bytes)));
    }
    host_log("GX research this minute: buffers released with the game's data in VRAM at their memory %llu of %llu "
             "(%.0f MiB); shaders created: vertex %llu + %llu, pixel %llu, compute %llu (%llu during loading screens)",
             (unsigned long long)take(g_counts.released_resident), (unsigned long long)take(g_counts.released_checked),
             Mib(take(g_counts.released_resident_bytes)), (unsigned long long)take(g_counts.shaders[0]),
             (unsigned long long)take(g_counts.shaders[1]), (unsigned long long)take(g_counts.shaders[2]),
             (unsigned long long)take(g_counts.shaders[3]), (unsigned long long)take(g_counts.shaders_loading));
}
} // namespace BbEngine::GxResources

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
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

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

// 0.4.6 beta 5 (bbhost's gx_resources.cpp, checked against 1.09): Map 0x2169c30 (ctx, ?, holder,
// subresource, type, mapped*; 0 on success: a CPU write is coming. Types 0-2 write in place after
// waiting for the GPU; 3, buffers only, writes into a renamed copy from GX's own pool, kept in its
// rename table at ctx+0xb6f8 with the holder's memory unchanged; 4 returns the copy in flight, or
// fails with 4 when there is none), the render-target
// and depth-stencil view creators 0x21667a0 / 0x2166610 (ctx, view** out, texture holder, D3D11-
// shaped description: the view keeps the holder at +0x08), and the 1D texture creator 0x2171790
// (resource* out = the holder's +0x18, ctx, desc, initial data, ...; GX builds the 1D holder inline
// around it), whose holders' destructors are 0x216d030 / 0x216d070 (vtable 0x535f3c0).
constexpr std::size_t kMap = 0x2169c30, kCreateRtv = 0x21667a0, kCreateDsv = 0x2166610, kCreate1D = 0x2171790;
// push rbp; mov rbp, rsp; push r15; push r14; push r13; push r12; push rbx; sub rsp, 0x68
constexpr std::uint8_t kDsvPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                         0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x68};
// ... sub rsp, 0xc8
constexpr std::uint8_t kCreate1DPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
                                              0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0xc8, 0x00, 0x00, 0x00};
constexpr Destructor k1DDestructors[] = {
    {0x216d030, 0x031f2380, 0x535f3c0}, // 1D, deleting
    {0x216d070, 0x031f2340, 0x535f3c0}, // 1D, in place
};
// A 1D texture's registry key: its memory, as bbhost keys it (the holder is built around the
// resource after its creator returns).
constexpr std::uint64_t kMemoryKey = 0x8000000000000000ull;

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

enum Kind : std::uint8_t { kBuffer = 0, kTexture = 1, kTexture3D = 2, kTexture1D = 3, kKinds = 4 };

struct Entry {
    std::uint64_t memory;
    std::uint32_t bytes;
    Kind kind;
    bool loading;
    std::uint64_t ns;
    std::uint8_t type;
    // 0.4.6 beta 5: whether it was made with its data, has a render-target or depth-stencil view, had
    // an image prepared (at its first Map), and the T# its creator built (2D textures).
    bool initial = false, render_target = false, depth_target = false, prepared = false;
    bool has_tsharp = false;
    std::array<std::uint32_t, 8> tsharp{};
};

// What a return through the stub belongs to.
enum Op : std::uint8_t { kOpCreate, kOpTsharp, kOpMap, kOpView, kOpCreate1D };

struct Pending {
    std::uint64_t ret;
    std::uint64_t out; // creators: holder**; the T# builder: the T# out; views: view**; 1D: the resource
    Kind kind;
    bool initial = false; // creators: initial data given
    bool tsharp = false;  // the T# builder
    std::uint64_t entry_ns = 0;
    Op op = kOpCreate;
    std::uint64_t holder = 0, type = 0; // Map: the holder and the map type; views: 1 render target, 2 depth
};
thread_local std::vector<Pending> t_pending;
// The T# the builder produced on this thread, for its creator's return.
thread_local std::array<std::uint32_t, 8> t_tsharp{};
thread_local bool t_tsharp_valid = false;

std::mutex g_mutex;
std::unordered_map<std::uint64_t, Entry> g_by_holder;
// memory -> holder (live textures), ordered: LiveTextureOverlaps asks by range (beta 19).
std::map<std::uint64_t, std::uint64_t> g_texture_at;
// memory -> holder (live buffers; 0.4.6 beta 5: Classify).
std::map<std::uint64_t, std::uint64_t> g_buffer_at;
std::vector<Gone> g_gone, g_released;
std::uint8_t* g_return_stub = nullptr;
bool g_active = false, g_follow = false;
std::atomic<DestroyedHook> g_destroyed_hook{nullptr};
std::atomic<CreatedHook> g_created_hook{nullptr};
std::atomic<std::uint64_t> g_live_texture_bytes{0};
std::atomic<bool> g_gone_waiting{false}, g_released_waiting{false}, g_created_waiting{false};
// 0.4.6 beta 5: resources made since the texture cache last asked (any kind with memory): their
// memory is theirs now, so images left there from before belong to nothing (TakeCreated).
std::vector<Gone> g_created;
constexpr std::size_t kQueueCap = 65536, kAheadCap = 16384;

std::uint64_t NowNs() {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count());
}

// 0.4.6 beta 4: who creates the game's textures and how long that takes, per thread (by name); the
// game's main thread (and its render thread) hand the preparing to a worker.
struct ThreadSlot {
    char name[32] = {};
    bool frame_critical = false;
    std::atomic<std::uint64_t> textures{0}, buffers{0}, creator_ns{0}, prepared{0}, prepare_ns{0};
};
constexpr std::size_t kThreadSlots = 32;
std::array<ThreadSlot, kThreadSlots> g_slots;
std::size_t g_slot_count = 0; // under g_mutex
ThreadSlot g_other_slot;      // past kThreadSlots names
thread_local ThreadSlot* t_slot = nullptr;

std::string CurrentThreadName() {
    std::string name;
#ifdef _WIN32
    PWSTR wide = nullptr;
    if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &wide)) && wide) {
        char text[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, sizeof(text) - 1, nullptr, nullptr);
        name = text;
        LocalFree(wide);
    }
#else
    char text[32] = {};
    if (pthread_getname_np(pthread_self(), text, sizeof(text)) == 0) {
        name = text;
    }
#endif
    return name.empty() ? std::string("(unnamed)") : name;
}

// The game's main loop thread ("main", runtime) and its render thread: the frame waits for them.
bool FrameCritical(const std::string& name) {
    return name == "main" || name == "GXRenderThread";
}

ThreadSlot& CurrentSlot() {
    if (t_slot) {
        return *t_slot;
    }
    const std::string name = CurrentThreadName();
    std::scoped_lock lock{g_mutex};
    for (std::size_t i = 0; i < g_slot_count; ++i) {
        if (name == g_slots[i].name) {
            return *(t_slot = &g_slots[i]);
        }
    }
    if (g_slot_count == kThreadSlots) {
        return *(t_slot = &g_other_slot);
    }
    ThreadSlot& slot = g_slots[g_slot_count++];
    std::snprintf(slot.name, sizeof(slot.name), "%s", name.c_str());
    slot.frame_critical = FrameCritical(name);
    return *(t_slot = &slot);
}

// The worker for textures the game's main thread creates (started at the first).
std::mutex g_worker_mutex;
std::condition_variable g_worker_cv;
std::deque<Ahead> g_worker_jobs;
bool g_worker_started = false;

void WorkerLoop() {
#ifdef _WIN32
    SetThreadDescription(GetCurrentThread(), L"bb:GxPrepare");
#else
    pthread_setname_np(pthread_self(), "bb:GxPrepare");
#endif
    ThreadSlot& slot = CurrentSlot();
    for (;;) {
        Ahead job;
        {
            std::unique_lock lock{g_worker_mutex};
            g_worker_cv.wait(lock, [] { return !g_worker_jobs.empty(); });
            job = g_worker_jobs.front();
            g_worker_jobs.pop_front();
        }
        if (const CreatedHook hook = g_created_hook.load(std::memory_order_acquire)) {
            const std::uint64_t start = NowNs();
            hook(job);
            slot.prepare_ns.fetch_add(NowNs() - start, std::memory_order_relaxed);
            slot.prepared.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

bool HandToWorker(const Ahead& job) {
    {
        std::scoped_lock lock{g_worker_mutex};
        if (g_worker_jobs.size() >= kAheadCap) {
            return false;
        }
        g_worker_jobs.push_back(job);
        if (!g_worker_started) {
            g_worker_started = true;
            std::thread(WorkerLoop).detach();
        }
    }
    g_worker_cv.notify_one();
    return true;
}

struct Counts {
    std::array<std::atomic<std::uint64_t>, kKinds> created{}, created_bytes{}, created_loading{}, destroyed{},
        destroyed_bytes{};
    std::atomic<std::uint64_t> destroyed_unknown{0}, failed{0}, gone_dropped{0}, moved{0};
    // The texture cache's side.
    std::atomic<std::uint64_t> images_matched{0}, images_unmatched{0}, matched_fast{0}, matched_mid{0},
        matched_slow{0}, matched_loading{0};
    std::atomic<std::uint64_t> destroyed_with_image{0}, destroyed_without_image{0}, destroyed_image_bytes{0};
    std::atomic<std::uint64_t> dead_images{0}, dead_bytes{0};
    // Following (beta 16).
    std::atomic<std::uint64_t> ahead_queued{0}, ahead_queue_full{0}, ahead_no_tsharp{0}, ahead_built{0},
        ahead_built_bytes{0}, ahead_existed{0}, ahead_gone{0}, ahead_old{0}, ahead_invalid{0}, ahead_hits{0},
        ahead_misses{0}, ahead_unused{0}, ahead_unused_bytes{0}, ahead_waiting{0};
    // 0.4.6 beta 4.
    std::atomic<std::uint64_t> ahead_refreshed{0}, ahead_no_memory{0}, ahead_no_room{0}, ahead_no_renderer{0},
        occupant_live{0},
        occupant_used{0}, occupant_gpu{0}, build_passes{0}, build_ns{0}, build_images{0}, build_max_ns{0},
        stale_live_bytes{0}, stale_orphan_bytes{0}, orphans_freed{0}, orphans_freed_bytes{0};
    std::atomic<std::uint64_t> build_ns_total{0}, build_images_total{0};
    // 0.4.6 beta 5.
    std::array<std::array<std::atomic<std::uint64_t>, 5>, kKinds> maps{};
    std::atomic<std::uint64_t> map_unknown{0}, map_moved{0}, map_prepared{0}, views_rt{0}, views_ds{0},
        views_unknown{0}, created_dropped{0};
    std::array<std::atomic<std::uint64_t>, std::size_t(Owner::kCount)> draw_made{}, draw_made_bytes{},
        draw_made_total{};
    std::array<std::atomic<std::uint64_t>, std::size_t(Owner::kCount)> stale_bytes{}, stale_images{};
    std::atomic<std::uint64_t> stale_gpu_bytes{0}, reuse_dead{0}, reuse_dead_bytes{0};
    std::array<char, 1024> stale_top{};
    std::mutex stale_top_mutex;
    std::atomic<std::uint64_t> dead_freed{0}, dead_freed_bytes{0};
    // Research (beta 16).
    std::atomic<std::uint64_t> released_checked{0}, released_resident{0}, released_resident_bytes{0};
    std::array<std::atomic<std::uint64_t>, 4> shaders{};
    std::atomic<std::uint64_t> shaders_loading{0};
};
Counts g_counts;

template <typename T> T Read(std::uint64_t address) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), sizeof(T));
    return value;
}

// `key`: the holder, or kMemoryKey | memory for a 1D texture (its holder is read for the fields).
Entry Register(std::uint64_t key, std::uint64_t holder, Kind kind, bool initial) {
    Entry e{Read<std::uint64_t>(holder + 0x20), Read<std::uint32_t>(holder + 0x2c), kind,
            Loading::ScreenUp(), NowNs(), Read<std::uint8_t>(holder + 0x38)};
    e.initial = initial;
    if (kind == kTexture && t_tsharp_valid) {
        // The builder runs before the memory is allocated: the base (address >> 8, 38 bits over the
        // first word and the low 6 bits of the second) is filled in here.
        e.has_tsharp = true;
        e.tsharp = t_tsharp;
        const std::uint64_t base = e.memory >> 8;
        e.tsharp[0] = static_cast<std::uint32_t>(base);
        e.tsharp[1] = (e.tsharp[1] & ~0x3fu) | static_cast<std::uint32_t>((base >> 32) & 0x3f);
    }
    g_counts.created[kind].fetch_add(1, std::memory_order_relaxed);
    g_counts.created_bytes[kind].fetch_add(e.bytes, std::memory_order_relaxed);
    if (e.loading) {
        g_counts.created_loading[kind].fetch_add(1, std::memory_order_relaxed);
    }
    std::scoped_lock lock{g_mutex};
    if (const auto old = g_by_holder.find(key); old != g_by_holder.end() && old->second.kind != kBuffer) {
        g_live_texture_bytes.fetch_sub(old->second.bytes, std::memory_order_relaxed); // reused unseen
    }
    if (kind != kBuffer) {
        g_live_texture_bytes.fetch_add(e.bytes, std::memory_order_relaxed);
        if (e.memory) {
            g_texture_at[e.memory] = key;
        }
    } else if (e.memory) {
        g_buffer_at[e.memory] = key;
    }
    // 0.4.6 beta 5: the memory is this resource's now (the texture cache retires images left there).
    if (g_follow && e.memory && e.bytes) {
        if (g_created.size() < kQueueCap) {
            g_created.push_back({e.memory, e.bytes, e.ns});
            g_created_waiting.store(true, std::memory_order_release);
        } else {
            g_counts.created_dropped.fetch_add(1, std::memory_order_relaxed);
        }
    }
    g_by_holder[key] = e;
    return e;
}

// `memory_now`: where the resource's memory is at its destructor's entry (the holder is still whole).
void Unregister(std::uint64_t holder, bool texture, std::uint64_t memory_now) {
    // 0.4.6 beta 5: the game's Map does not move a resource's memory (a rename writes into a copy
    // from GX's own pool, the holder's memory unchanged: NoteMap), so a resource whose memory differs
    // here is not one this registry knows the history of, and is left alone (that range may be
    // another's by now).
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
        if (const auto at = g_texture_at.find(e.memory); at != g_texture_at.end() && at->second == holder) {
            g_texture_at.erase(at);
        }
        if (const auto at = g_buffer_at.find(e.memory); at != g_buffer_at.end() && at->second == holder) {
            g_buffer_at.erase(at);
        }
        if (!e.memory) {
            return;
        }
        if (memory_now != e.memory) {
            g_counts.moved.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!texture || e.kind == kBuffer) {
            // For the buffer cache (research); bounded should it not run.
            if (e.kind == kBuffer && g_released.size() < kQueueCap) {
                g_released.push_back({e.memory, e.bytes, NowNs()});
                g_released_waiting.store(true, std::memory_order_release);
            }
            return;
        }
        // For the texture cache's collector; bounded should it not run (no renderer yet).
        if (g_gone.size() < kQueueCap) {
            g_gone.push_back({e.memory, e.bytes, NowNs()});
            g_gone_waiting.store(true, std::memory_order_release);
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

// A 2D texture created with its data and its T#: its image prepared now, on this thread (0.4.6 beta
// 4; the game's main thread hands it to the worker), for the texture cache to register at its next
// submission.
// 0.4.6 beta 5: also at the first Map of a texture made without its data (`upload` false: the image
// is made now, its upload is its first draw's, once the game has written it).
void HandCreated(const Entry& e, ThreadSlot& slot, bool upload = true) {
    if (!e.has_tsharp) {
        g_counts.ahead_no_tsharp.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (e.memory == 0 || (e.type != 2 && e.type != 3)) {
        return;
    }
    Ahead job{e.tsharp, e.memory, e.bytes, e.ns, e.loading, upload};
    g_counts.ahead_queued.fetch_add(1, std::memory_order_relaxed);
    const CreatedHook hook = g_created_hook.load(std::memory_order_acquire);
    if (!hook) {
        g_counts.ahead_no_renderer.fetch_add(1, std::memory_order_relaxed); // built at its first draw
        return;
    }
    if (slot.frame_critical) {
        if (!HandToWorker(job)) {
            g_counts.ahead_queue_full.fetch_add(1, std::memory_order_relaxed); // built at its first draw
        }
        return;
    }
    const std::uint64_t start = NowNs();
    hook(job);
    slot.prepare_ns.fetch_add(NowNs() - start, std::memory_order_relaxed);
    slot.prepared.fetch_add(1, std::memory_order_relaxed);
}

// The creators' entry: keep the return address and the out pointer (rsi), return through the stub.
template <Kind kind> GUEST_ABI std::int64_t CreatorEntry(std::uint64_t, const std::uint64_t* saved) {
    t_pending.push_back({saved[6], saved[4], kind, saved[2] != 0, false, NowNs(), kOpCreate});
    const_cast<std::uint64_t*>(saved)[6] = reinterpret_cast<std::uint64_t>(g_return_stub);
    return 0;
}

// The T# builder: inside the 2D creator only (rsi = the T# out), read at its return.
GUEST_ABI std::int64_t TsharpEntry(std::uint64_t, const std::uint64_t* saved) {
    if (!t_pending.empty() && !t_pending.back().tsharp && t_pending.back().kind == kTexture) {
        t_pending.push_back({saved[6], saved[4], kTexture, false, true, 0, kOpTsharp});
        const_cast<std::uint64_t*>(saved)[6] = reinterpret_cast<std::uint64_t>(g_return_stub);
    }
    return 0;
}

// 0.4.6 beta 5: Map (rdx = holder, r8 = type, r9 = where it writes the mapped address), the view
// creators (rsi = view**) and the 1D creator (rdi = the resource it fills, rcx = initial data), each
// read at its return.
GUEST_ABI std::int64_t MapEntry(std::uint64_t, const std::uint64_t* saved) {
    t_pending.push_back({saved[6], saved[0], kBuffer, false, false, 0, kOpMap, saved[3], saved[1]});
    const_cast<std::uint64_t*>(saved)[6] = reinterpret_cast<std::uint64_t>(g_return_stub);
    return 0;
}

template <int depth> GUEST_ABI std::int64_t ViewEntry(std::uint64_t, const std::uint64_t* saved) {
    t_pending.push_back({saved[6], saved[4], kTexture, false, false, 0, kOpView, 0, std::uint64_t(depth)});
    const_cast<std::uint64_t*>(saved)[6] = reinterpret_cast<std::uint64_t>(g_return_stub);
    return 0;
}

GUEST_ABI std::int64_t Create1DEntry(std::uint64_t, const std::uint64_t* saved) {
    t_pending.push_back({saved[6], saved[5], kTexture1D, saved[2] != 0, false, NowNs(), kOpCreate1D});
    const_cast<std::uint64_t*>(saved)[6] = reinterpret_cast<std::uint64_t>(g_return_stub);
    return 0;
}

// A Map has returned: the resource is about to be written by the CPU, in place or (renamed) into a
// copy elsewhere, which the game binds in its place until the copy is retired.
void NoteMap(std::uint64_t holder, std::uint64_t type, std::uint64_t mapped_out, ThreadSlot& slot) {
    const std::uint64_t mapped = mapped_out ? Read<std::uint64_t>(mapped_out) : 0;
    Entry prepare{};
    bool hand = false;
    {
        std::scoped_lock lock{g_mutex};
        const auto it = g_by_holder.find(holder);
        if (it == g_by_holder.end()) {
            g_counts.map_unknown.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        Entry& e = it->second;
        g_counts.maps[e.kind][type < 5 ? type : 0].fetch_add(1, std::memory_order_relaxed);
        if (mapped && (mapped < e.memory || mapped >= e.memory + std::max<std::uint64_t>(e.bytes, 1))) {
            g_counts.map_moved.fetch_add(1, std::memory_order_relaxed); // a renamed copy
        }
        // A texture made without its data, written for the first time: its image is made now,
        // ahead of its first draw (the draw uploads it, once the game has written it).
        if (g_follow && e.kind == kTexture && !e.initial && !e.prepared && e.has_tsharp && !e.render_target &&
            !e.depth_target) {
            e.prepared = true;
            prepare = e;
            hand = true;
            g_counts.map_prepared.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (hand) {
        HandCreated(prepare, slot, false);
    }
}

void NoteView(std::uint64_t out, bool depth) {
    const std::uint64_t view = out ? Read<std::uint64_t>(out) : 0;
    const std::uint64_t holder = view ? Read<std::uint64_t>(view + 0x08) : 0;
    std::scoped_lock lock{g_mutex};
    const auto it = holder ? g_by_holder.find(holder) : g_by_holder.end();
    if (it == g_by_holder.end()) {
        g_counts.views_unknown.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    (depth ? it->second.depth_target : it->second.render_target) = true;
    (depth ? g_counts.views_ds : g_counts.views_rt).fetch_add(1, std::memory_order_relaxed);
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
    if (p.op == kOpTsharp) {
        t_tsharp_valid = p.out != 0;
        if (t_tsharp_valid) {
            t_tsharp = Read<std::array<std::uint32_t, 8>>(p.out);
        }
        return p.ret;
    }
    ThreadSlot& slot = CurrentSlot();
    if (p.op == kOpMap) {
        if (static_cast<std::uint32_t>(result) == 0 && p.holder) {
            NoteMap(p.holder, p.type, p.out, slot);
        }
        return p.ret;
    }
    if (p.op == kOpView) {
        if (static_cast<std::uint32_t>(result) == 0) {
            NoteView(p.out, p.type == 2);
        }
        return p.ret;
    }
    if (p.op == kOpCreate1D) {
        slot.textures.fetch_add(1, std::memory_order_relaxed);
        slot.creator_ns.fetch_add(NowNs() - p.entry_ns, std::memory_order_relaxed);
        // The resource it filled sits at a holder's +0x18 (type at +0x20 there: 2 is a texture).
        if (static_cast<std::uint32_t>(result) == 0 && p.out && Read<std::uint8_t>(p.out + 0x20) == 2) {
            const std::uint64_t holder = p.out - 0x18;
            const std::uint64_t memory = Read<std::uint64_t>(holder + 0x20);
            if (memory) {
                Register(kMemoryKey | memory, holder, kTexture1D, p.initial);
            }
        } else if (static_cast<std::uint32_t>(result) != 0) {
            g_counts.failed.fetch_add(1, std::memory_order_relaxed);
        }
        return p.ret;
    }
    if (p.kind == kBuffer) {
        slot.buffers.fetch_add(1, std::memory_order_relaxed);
    } else {
        slot.textures.fetch_add(1, std::memory_order_relaxed);
        slot.creator_ns.fetch_add(NowNs() - p.entry_ns, std::memory_order_relaxed);
    }
    if (static_cast<std::uint32_t>(result) != 0) {
        g_counts.failed.fetch_add(1, std::memory_order_relaxed);
    } else if (p.out) {
        if (const auto holder = Read<std::uint64_t>(p.out)) {
            const Entry e = Register(holder, holder, p.kind, p.initial);
            if (g_follow && p.kind == kTexture && p.initial) {
                HandCreated(e, slot);
            }
        }
    }
    t_tsharp_valid = false;
    return p.ret;
}

GUEST_ABI std::int64_t TextureDestructor(std::uint64_t, const std::uint64_t* saved) {
    Unregister(saved[5], true, Read<std::uint64_t>(saved[5] + 0x20));
    return 0;
}

// 0.4.6 beta 5: a 1D texture's holder, found by its memory.
GUEST_ABI std::int64_t Texture1DDestructor(std::uint64_t, const std::uint64_t* saved) {
    const std::uint64_t memory = Read<std::uint64_t>(saved[5] + 0x20);
    if (memory) {
        Unregister(kMemoryKey | memory, true, memory);
    }
    return 0;
}

GUEST_ABI std::int64_t BufferRelease(std::uint64_t, const std::uint64_t* saved) {
    Unregister(saved[5] - 0x18, false, Read<std::uint64_t>(saved[5] - 0x18 + 0x20));
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
    Hook hooks[19];
    bool ok = Prepare(hooks[0], image, size, kCreateBuffer, kCreatorPrologue, &CreatorEntry<kBuffer>) &&
              Prepare(hooks[1], image, size, kCreateTexture, kCreatorPrologue, &CreatorEntry<kTexture>) &&
              Prepare(hooks[2], image, size, kCreateTexture3D, kCreatorPrologue, &CreatorEntry<kTexture3D>) &&
              Prepare(hooks[3], image, size, kBufferRelease, kReleasePrologue, &BufferRelease) &&
              Prepare(hooks[8], image, size, kTsharpBuild, kTsharpPrologue, &TsharpEntry) &&
              Prepare(hooks[9], image, size, kShaderCreators[0], kShaderPrologue[0], &ShaderCreated<0>) &&
              Prepare(hooks[10], image, size, kShaderCreators[1], kShaderPrologue[1], &ShaderCreated<1>) &&
              Prepare(hooks[11], image, size, kShaderCreators[2], kShaderPrologue[1], &ShaderCreated<2>) &&
              Prepare(hooks[12], image, size, kShaderCreators[3], kShaderPrologue[1], &ShaderCreated<3>) &&
              Prepare(hooks[13], image, size, kMap, kCreatorPrologue, &MapEntry) &&
              Prepare(hooks[14], image, size, kCreateRtv, kCreatorPrologue, &ViewEntry<1>) &&
              Prepare(hooks[15], image, size, kCreateDsv, kDsvPrologue, &ViewEntry<2>) &&
              Prepare(hooks[16], image, size, kCreate1D, kCreate1DPrologue, &Create1DEntry);
    for (std::size_t i = 0; ok && i < std::size(kDestructors) + std::size(k1DDestructors); ++i) {
        const bool one_d = i >= std::size(kDestructors);
        const Destructor& d = one_d ? k1DDestructors[i - std::size(kDestructors)] : kDestructors[i];
        std::uint8_t expected[16] = {0x55, 0x48, 0x89, 0xe5, 0x53, 0x50, 0x48, 0x89, 0xfb, 0x48, 0x8d, 0x05};
        std::memcpy(expected + 12, &d.disp, 4);
        // The same, the lea as movabs rax, <the vtable's address in this image>.
        std::uint8_t displaced[19] = {0x55, 0x48, 0x89, 0xe5, 0x53, 0x50, 0x48, 0x89, 0xfb, 0x48, 0xb8};
        const std::uint64_t vtable = reinterpret_cast<std::uint64_t>(image) + d.vtable;
        std::memcpy(displaced + 11, &vtable, 8);
        ok = d.site + 16 + d.disp == d.vtable && // the lea's target, from its own bytes
             PrepareDisplaced(hooks[one_d ? 17 + (i - std::size(kDestructors)) : 4 + i], image, size, d.site,
                              expected, displaced, one_d ? &Texture1DDestructor : &TextureDestructor);
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
    host_log(g_follow ? "GX resources: the caches follow the game's resources: a texture created with its data "
                        "is prepared in the game's create call and registered at the next submission, one made "
                        "without it at its first Map; an image is freed when the game destroys its texture or "
                        "makes something newer over its memory; the game's data stays in VRAM, as on the PS4, "
                        "until the game unmaps its memory (BB_GX_FOLLOW=0: counted only; BB_GX_RESOURCES=0: off)"
                      : "GX resources: listening to the game's creators, destructors, Maps and views "
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
    g_gone_waiting.store(false, std::memory_order_relaxed);
}

bool DestroyedWaiting() {
    return g_gone_waiting.load(std::memory_order_acquire);
}

void TakeReleased(std::vector<Gone>& out) {
    std::scoped_lock lock{g_mutex};
    out.swap(g_released);
    g_released.clear();
    g_released_waiting.store(false, std::memory_order_relaxed);
}

bool ReleasedWaiting() {
    return g_released_waiting.load(std::memory_order_acquire);
}

void TakeCreated(std::vector<Gone>& out) {
    std::scoped_lock lock{g_mutex};
    out.swap(g_created);
    g_created.clear();
    g_created_waiting.store(false, std::memory_order_relaxed);
}

bool CreatedWaiting() {
    return g_created_waiting.load(std::memory_order_acquire);
}

const char* OwnerName(Owner owner) {
    static constexpr const char* kNames[] = {"no game resource", "a buffer", "a texture made with its data",
                                             "a texture made without", "a render target", "a depth target",
                                             "a 3D texture", "a 1D texture"};
    return std::size_t(owner) < std::size(kNames) ? kNames[std::size_t(owner)] : "?";
}

namespace {
Owner OwnerOf(const Entry& e) {
    switch (e.kind) {
    case kBuffer:
        return Owner::Buffer;
    case kTexture3D:
        return Owner::Texture3D;
    case kTexture1D:
        return Owner::Texture1D;
    default:
        return e.depth_target ? Owner::DepthTarget
               : e.render_target ? Owner::RenderTarget
               : e.initial       ? Owner::TextureData
                                 : Owner::TextureNoData;
    }
}

// The live resource in `index` over [begin, end), or null. Under g_mutex.
const Entry* Covering(const std::map<std::uint64_t, std::uint64_t>& index, std::uint64_t begin, std::uint64_t end,
                      bool* at_start) {
    auto it = index.upper_bound(begin);
    if (it != index.begin()) {
        const auto before = std::prev(it);
        if (const auto e = g_by_holder.find(before->second);
            e != g_by_holder.end() && before->first + std::max<std::uint64_t>(e->second.bytes, 1) > begin) {
            if (at_start) {
                *at_start = before->first == begin;
            }
            return &e->second;
        }
    }
    if (it != index.end() && it->first < end) {
        if (const auto e = g_by_holder.find(it->second); e != g_by_holder.end()) {
            if (at_start) {
                *at_start = false;
            }
            return &e->second;
        }
    }
    return nullptr;
}
} // namespace

Owner Classify(std::uint64_t begin, std::uint64_t end, bool* at_start) {
    if (at_start) {
        *at_start = false;
    }
    if (!g_active || begin >= end) {
        return Owner::None;
    }
    std::scoped_lock lock{g_mutex};
    if (const Entry* e = Covering(g_texture_at, begin, end, at_start)) {
        return OwnerOf(*e);
    }
    if (const Entry* e = Covering(g_buffer_at, begin, end, at_start)) {
        return OwnerOf(*e);
    }
    return Owner::None;
}

void NoteDrawMade(Owner owner, std::uint64_t bytes) {
    const std::size_t i = std::min<std::size_t>(std::size_t(owner), std::size_t(Owner::kCount) - 1);
    g_counts.draw_made[i].fetch_add(1, std::memory_order_relaxed);
    g_counts.draw_made_bytes[i].fetch_add(bytes, std::memory_order_relaxed);
    g_counts.draw_made_total[i].fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t DrawMadeTotal(Owner owner) {
    return g_counts.draw_made_total[std::min<std::size_t>(std::size_t(owner), std::size_t(Owner::kCount) - 1)].load(
        std::memory_order_relaxed);
}

void NoteStaleByOwner(const std::array<std::uint64_t, std::size_t(Owner::kCount)>& bytes,
                      const std::array<std::uint64_t, std::size_t(Owner::kCount)>& images, std::uint64_t gpu_bytes,
                      const char* largest) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        g_counts.stale_bytes[i].store(bytes[i], std::memory_order_relaxed);
        g_counts.stale_images[i].store(images[i], std::memory_order_relaxed);
    }
    g_counts.stale_gpu_bytes.store(gpu_bytes, std::memory_order_relaxed);
    std::scoped_lock lock{g_counts.stale_top_mutex};
    std::snprintf(g_counts.stale_top.data(), g_counts.stale_top.size(), "%s", largest ? largest : "");
}

void NoteReuseDead(std::uint64_t images, std::uint64_t bytes) {
    g_counts.reuse_dead.fetch_add(images, std::memory_order_relaxed);
    g_counts.reuse_dead_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void SetCreatedHook(CreatedHook hook) {
    g_created_hook.store(hook, std::memory_order_release);
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

bool LiveTextureOverlaps(std::uint64_t begin, std::uint64_t end, std::uint64_t skip_at) {
    if (!g_active || begin >= end) {
        return false;
    }
    std::scoped_lock lock{g_mutex};
    auto it = g_texture_at.upper_bound(begin);
    if (it != g_texture_at.begin()) {
        const auto before = std::prev(it);
        if (const auto e = g_by_holder.find(before->second);
            before->first != skip_at && e != g_by_holder.end() &&
            before->first + std::max<std::uint64_t>(e->second.bytes, 1) > begin) {
            return true;
        }
    }
    for (; it != g_texture_at.end() && it->first < end; ++it) {
        if (it->first != skip_at) {
            return true;
        }
    }
    return false;
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
    case AheadResult::Refreshed:
        g_counts.ahead_refreshed.fetch_add(1, std::memory_order_relaxed);
        break;
    case AheadResult::NoMemory:
        g_counts.ahead_no_memory.fetch_add(1, std::memory_order_relaxed);
        break;
    case AheadResult::NoRoom:
        g_counts.ahead_no_room.fetch_add(1, std::memory_order_relaxed);
        break;
    case AheadResult::NoRenderer:
        g_counts.ahead_no_renderer.fetch_add(1, std::memory_order_relaxed);
        break;
    }
}

void NoteOccupant(Occupant why) {
    (why == Occupant::NotDestroyed ? g_counts.occupant_live
     : why == Occupant::UsedSince  ? g_counts.occupant_used
                                   : g_counts.occupant_gpu)
        .fetch_add(1, std::memory_order_relaxed);
}

void NoteBuildPass(std::uint64_t ns, std::uint64_t images) {
    g_counts.build_passes.fetch_add(1, std::memory_order_relaxed);
    g_counts.build_ns.fetch_add(ns, std::memory_order_relaxed);
    g_counts.build_images.fetch_add(images, std::memory_order_relaxed);
    g_counts.build_ns_total.fetch_add(ns, std::memory_order_relaxed);
    g_counts.build_images_total.fetch_add(images, std::memory_order_relaxed);
    std::uint64_t max = g_counts.build_max_ns.load(std::memory_order_relaxed);
    while (ns > max && !g_counts.build_max_ns.compare_exchange_weak(max, ns, std::memory_order_relaxed)) {
    }
}

void BuildPassTotals(std::uint64_t& ns, std::uint64_t& images) {
    ns = g_counts.build_ns_total.load(std::memory_order_relaxed);
    images = g_counts.build_images_total.load(std::memory_order_relaxed);
}

void NoteStale(std::uint64_t with_texture_bytes, std::uint64_t without_texture_bytes) {
    g_counts.stale_live_bytes.store(with_texture_bytes, std::memory_order_relaxed);
    g_counts.stale_orphan_bytes.store(without_texture_bytes, std::memory_order_relaxed);
}

void NoteOrphanFreed(std::uint64_t bytes) {
    g_counts.orphans_freed.fetch_add(1, std::memory_order_relaxed);
    g_counts.orphans_freed_bytes.fetch_add(bytes, std::memory_order_relaxed);
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
             "released %llu (%.0f MiB); %llu failed, %llu destroyed unknown, %llu with their memory moved; live %zu "
             "(%zu textures, %.0f MiB)",
             (unsigned long long)created[kTexture], Mib(created_bytes[kTexture]),
             (unsigned long long)loading[kTexture], (unsigned long long)created[kTexture3D],
             (unsigned long long)destroyed[kTexture], Mib(destroyed_bytes[kTexture]),
             (unsigned long long)destroyed[kTexture3D], (unsigned long long)created[kBuffer],
             Mib(created_bytes[kBuffer]), (unsigned long long)loading[kBuffer],
             (unsigned long long)destroyed[kBuffer], Mib(destroyed_bytes[kBuffer]),
             (unsigned long long)take(g_counts.failed), (unsigned long long)take(g_counts.destroyed_unknown),
             (unsigned long long)take(g_counts.moved), live,
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
        host_log("GX follow this minute: %llu textures created with their data (%llu without a T#, %llu with no "
                 "renderer yet, %llu over the worker's queue), %llu built (%.0f MiB), %llu refreshed in place (the "
                 "same texture again), %llu left to their draw (an image there: %llu of a texture not destroyed, "
                 "%llu used since its texture was destroyed, %llu drawn by the GPU under a live texture), %llu "
                 "destroyed first, %llu too old, %llu not buildable, %llu refused by VRAM, %llu over the VRAM "
                 "manager's comfortable line, %llu waiting; drawn %llu, "
                 "a draw made another image %llu times, freed unused %llu (%.0f MiB); images freed when the game "
                 "destroyed their texture %llu (%.0f MiB)",
                 (unsigned long long)take(g_counts.ahead_queued), (unsigned long long)take(g_counts.ahead_no_tsharp),
                 (unsigned long long)take(g_counts.ahead_no_renderer),
                 (unsigned long long)take(g_counts.ahead_queue_full), (unsigned long long)take(g_counts.ahead_built),
                 Mib(take(g_counts.ahead_built_bytes)), (unsigned long long)take(g_counts.ahead_refreshed),
                 (unsigned long long)take(g_counts.ahead_existed), (unsigned long long)take(g_counts.occupant_live),
                 (unsigned long long)take(g_counts.occupant_used), (unsigned long long)take(g_counts.occupant_gpu),
                 (unsigned long long)take(g_counts.ahead_gone), (unsigned long long)take(g_counts.ahead_old),
                 (unsigned long long)take(g_counts.ahead_invalid), (unsigned long long)take(g_counts.ahead_no_memory),
                 (unsigned long long)take(g_counts.ahead_no_room),
                 (unsigned long long)g_counts.ahead_waiting.load(std::memory_order_relaxed),
                 (unsigned long long)take(g_counts.ahead_hits), (unsigned long long)take(g_counts.ahead_misses),
                 (unsigned long long)take(g_counts.ahead_unused), Mib(take(g_counts.ahead_unused_bytes)),
                 (unsigned long long)take(g_counts.dead_freed), Mib(take(g_counts.dead_freed_bytes)));
        const std::uint64_t passes = take(g_counts.build_passes);
        host_log("GX follow, where the work ran this minute: GPU thread %llu passes registering %llu prepared "
                 "textures (%.1f ms, longest pass %.2f ms); stale images (written over, unused 10 s) %.0f MiB under "
                 "a live game texture, %.0f MiB under none, %llu of those freed (%.0f MiB)",
                 (unsigned long long)passes, (unsigned long long)take(g_counts.build_images),
                 double(take(g_counts.build_ns)) / 1e6, double(take(g_counts.build_max_ns)) / 1e6,
                 Mib(g_counts.stale_live_bytes.load(std::memory_order_relaxed)),
                 Mib(g_counts.stale_orphan_bytes.load(std::memory_order_relaxed)),
                 (unsigned long long)take(g_counts.orphans_freed), Mib(take(g_counts.orphans_freed_bytes)));
        // The game's creating threads, busiest first.
        struct Row {
            const ThreadSlot* slot;
            std::uint64_t textures, buffers, creator_ns, prepared, prepare_ns;
        };
        std::vector<Row> rows;
        std::size_t count = 0;
        {
            std::scoped_lock lock{g_mutex};
            count = g_slot_count;
        }
        for (std::size_t i = 0; i <= count; ++i) {
            ThreadSlot& slot = i < count ? g_slots[i] : g_other_slot;
            Row row{&slot, take(slot.textures), take(slot.buffers), take(slot.creator_ns), take(slot.prepared),
                    take(slot.prepare_ns)};
            if (row.textures + row.buffers + row.prepared != 0) {
                rows.push_back(row);
            }
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            return a.textures + a.prepared > b.textures + b.prepared;
        });
        std::string line;
        for (std::size_t i = 0; i < rows.size() && i < 8; ++i) {
            const Row& r = rows[i];
            char part[256];
            std::snprintf(part, sizeof(part), "%s%s: %llu textures (%.1f ms in the game's creator), %llu buffers, "
                          "%llu prepared (%.1f ms)%s",
                          line.empty() ? "" : "; ", r.slot == &g_other_slot ? "(others)" : r.slot->name,
                          (unsigned long long)r.textures, double(r.creator_ns) / 1e6, (unsigned long long)r.buffers,
                          (unsigned long long)r.prepared, double(r.prepare_ns) / 1e6,
                          r.slot->frame_critical ? ", handed to bb:GxPrepare" : "");
            line += part;
        }
        if (!line.empty()) {
            host_log("GX creates this minute by thread: %s", line.c_str());
        }
    }
    {
        // 0.4.6 beta 5: what the game does with its resources after making them, and what the images
        // made at a draw (not prepared) and the stale ones (written over, unused 10 s) belong to.
        auto maps = [&](int kind) {
            char part[160];
            std::uint64_t m[5];
            for (int t = 0; t < 5; ++t) {
                m[t] = take(g_counts.maps[kind][t]);
            }
            std::snprintf(part, sizeof(part), "%llu in place, %llu renamed, %llu into the copy in flight",
                          (unsigned long long)(m[0] + m[1] + m[2]), (unsigned long long)m[3],
                          (unsigned long long)m[4]);
            return std::string(part);
        };
        const std::string map_buffers = maps(kBuffer), map_textures = maps(kTexture), map_3d = maps(kTexture3D),
                          map_1d = maps(kTexture1D);
        host_log("GX memory this minute: 1D textures created %llu (%.0f MiB), destroyed %llu; maps: buffers %s; "
                 "textures %s; 3D %s; 1D %s; %llu of resources not known, %llu written outside the resource's memory, %llu "
                 "prepared a texture made without its data; views: %llu render targets, %llu depth targets, %llu "
                 "of textures not known; images retired because the game made something newer over them %llu "
                 "(%.0f MiB); %llu created events not queued",
                 (unsigned long long)created[kTexture1D], Mib(created_bytes[kTexture1D]),
                 (unsigned long long)destroyed[kTexture1D], map_buffers.c_str(), map_textures.c_str(), map_3d.c_str(),
                 map_1d.c_str(), (unsigned long long)take(g_counts.map_unknown),
                 (unsigned long long)take(g_counts.map_moved), (unsigned long long)take(g_counts.map_prepared),
                 (unsigned long long)take(g_counts.views_rt), (unsigned long long)take(g_counts.views_ds),
                 (unsigned long long)take(g_counts.views_unknown), (unsigned long long)take(g_counts.reuse_dead),
                 Mib(take(g_counts.reuse_dead_bytes)), (unsigned long long)take(g_counts.created_dropped));
        std::string drawn, stale;
        for (std::size_t i = 0; i < std::size_t(Owner::kCount); ++i) {
            const std::uint64_t n = take(g_counts.draw_made[i]), bytes = take(g_counts.draw_made_bytes[i]);
            if (n != 0) {
                char part[128];
                std::snprintf(part, sizeof(part), "%s%s %llu (%.0f MiB)", drawn.empty() ? "" : ", ",
                              OwnerName(Owner(i)), (unsigned long long)n, Mib(bytes));
                drawn += part;
            }
            const std::uint64_t sn = g_counts.stale_images[i].load(std::memory_order_relaxed),
                                sb = g_counts.stale_bytes[i].load(std::memory_order_relaxed);
            if (sn != 0) {
                char part[128];
                std::snprintf(part, sizeof(part), "%s%s %llu (%.0f MiB)", stale.empty() ? "" : ", ",
                              OwnerName(Owner(i)), (unsigned long long)sn, Mib(sb));
                stale += part;
            }
        }
        std::string top;
        {
            std::scoped_lock lock{g_counts.stale_top_mutex};
            top = g_counts.stale_top.data();
        }
        host_log("GX memory, images by the game resource at their memory: made at a draw this minute: %s; stale "
                 "now: %s (%.0f MiB of them drawn by the GPU)%s%s",
                 drawn.empty() ? "none" : drawn.c_str(), stale.empty() ? "none" : stale.c_str(),
                 Mib(g_counts.stale_gpu_bytes.load(std::memory_order_relaxed)), top.empty() ? "" : "; largest: ",
                 top.c_str());
    }
    host_log("GX research this minute: buffers released with the game's data in VRAM at their memory %llu of %llu "
             "(%.0f MiB); shaders created: vertex %llu + %llu, pixel %llu, compute %llu (%llu during loading screens)",
             (unsigned long long)take(g_counts.released_resident), (unsigned long long)take(g_counts.released_checked),
             Mib(take(g_counts.released_resident_bytes)), (unsigned long long)take(g_counts.shaders[0]),
             (unsigned long long)take(g_counts.shaders[1]), (unsigned long long)take(g_counts.shaders[2]),
             (unsigned long long)take(g_counts.shaders[3]), (unsigned long long)take(g_counts.shaders_loading));
}
} // namespace BbEngine::GxResources

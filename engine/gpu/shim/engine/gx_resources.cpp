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
#include <cstdint>
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
#include <ucontext.h>
#endif
#include "bbport_platform.h"

extern "C" int runtime_memory_region(std::uintptr_t address, std::uintptr_t* start, std::uintptr_t* end,
                                     int* mapped);

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

// 0.4.6 beta 5 (bbhost's gx_resources.cpp, checked against 1.09): the render-target and depth-stencil
// view creators 0x21667a0 / 0x2166610 (ctx, view** out, texture holder, D3D11-shaped description:
// the view keeps the holder at +0x08), and the 1D texture creator 0x2171790 (resource* out = the
// holder's +0x18, ctx, desc, initial data, ...; GX builds the 1D holder inline around it), whose
// holders' destructors are 0x216d030 / 0x216d070 (vtable 0x535f3c0). Beta 5 also followed Map
// 0x2169c30: its log showed only buffers mapped (2,000-10,000 a second, every one into a renamed
// copy from GX's own pool) and never a texture, so beta 6 leaves it alone.
constexpr std::size_t kCreateRtv = 0x21667a0, kCreateDsv = 0x2166610, kCreate1D = 0x2171790;
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

// 0.4.6 beta 6 (bbhost's gx_resources.cpp, checked against 1.09): the game's textures that are not
// GX's. YEBIS, the post-processing middleware, has its own D3D11-shaped device: CreateTexture2D
// 0x11df570 (device, device, initial data, desc, 0, out) makes an object with its memory at +0x98,
// its kind at +0x80 (3: 2D), its size at +0x150 and its D3D11 description from +0x188 (width,
// height, mips, array, format, samples, quality, usage, bind: 0x20 render target, 0x40 depth, ...);
// its 2D texture's deleting destructor is 0x2a4b090 (vtable 0x530bbf0; it runs for far more objects
// than the creator makes: only those it made count). Scaleform, the UI, initializes a texture at
// 0x3a1d30 (texture, ...; true when it made the hardware texture: +0x45 planes in an array at +0x60
// of 0x58-byte records, size at +0, T# at +4, memory at +0x50) and gives the planes' memory back in
// ReleaseHWTextures 0x3a2160 (texture). Most Scaleform textures wrap one of the game's (no memory of
// their own): those stay the game texture's. Beta 5's log: draws made 250-600 images a minute over
// memory no GX resource held, and about 1.1 GB of the GPU's drawings sat there unused for minutes.
constexpr std::size_t kYebisCreate2D = 0x11df570, kScaleformInit = 0x3a1d30, kScaleformRelease = 0x3a2160;
// push rbp; mov rbp, rsp; push r15; push r14; push r13; push r12; push rbx; sub rsp, 0x18
constexpr std::uint8_t kYebisCreatePrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                                 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x18};
// push rbp; mov rbp, rsp; push r15; push r14; push r12; push rbx; sub rsp, 0x60
constexpr std::uint8_t kScaleformReleasePrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
                                                      0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x60};
constexpr Destructor kYebisDestructor = {0x2a4b090, 0x028c0b50, 0x530bbf0};
// Their registry keys: the object (YEBIS), the texture and its plane (Scaleform).
constexpr std::uint64_t kYebisKey = 0x4000000000000000ull, kScaleformKey = 0x2000000000000000ull;
std::uint64_t ScaleformKey(std::uint64_t texture, unsigned plane) {
    return kScaleformKey | (texture << 2) | plane;
}

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
// 0.4.6 beta 8 (research for building shaders ahead): GX's input-layout creator (ctx, object** out,
// elements, element count, the vertex shader's code, ...): where the game gives a vertex shader's vertex
// formats, as D3D11 does (bbhost's 0x25657a0). push rbp; mov rbp, rsp; push r15; push r14; push r12;
// push rbx; sub rsp, 0x70
constexpr std::size_t kCreateInputLayout = 0x21657a0;
// 0.4.6 beta 9: YEBIS's context's UpdateSubresource (ctx, resource, subresource, box, data, ...), Map
// (ctx, resource, subresource, type, flags, mapped*) and Unmap (a thunk: mov rdi, [rdi + 0xaa0]; mov
// esi, 0x38; xor edx, edx; xor ecx, ecx; jmp), bbhost's 0x15d8d70, 0x15d92c0 and 0x15d99c0; Scaleform's
// texture manager's unmapTexture (manager, texture, ...), bbhost's 0x7a3270, where its CPU writes into
// a texture's planes end (Update and Unmap both come there).
constexpr std::size_t kYebisUpdate = 0x11d8d70, kYebisMap = 0x11d92c0, kYebisUnmap = 0x11d99c0,
                      kScaleformUnmap = 0x3a3270;
constexpr std::uint8_t kYebisUpdatePrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
                                                 0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0x58, 0x01, 0x00, 0x00};
constexpr std::uint8_t kYebisMapPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
                                              0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0x98, 0x00, 0x00, 0x00};
constexpr std::uint8_t kYebisUnmapPrologue[] = {0x48, 0x8b, 0xbf, 0xa0, 0x0a, 0x00, 0x00, 0xbe,
                                                0x38, 0x00, 0x00, 0x00, 0x31, 0xd2, 0x31, 0xc9};
// push rbp; mov rbp, rsp; push r14; push rbx; mov r14, rdi; mov rbx, [rsi + 0x50]
constexpr std::uint8_t kScaleformUnmapPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53,
                                                    0x49, 0x89, 0xfe, 0x48, 0x8b, 0x5e, 0x50};
constexpr std::uint8_t kInputLayoutPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
                                                 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x70};

enum Kind : std::uint8_t {
    kBuffer = 0,
    kTexture = 1,
    kTexture3D = 2,
    kTexture1D = 3,
    kYebis = 4,     // 0.4.6 beta 6: a YEBIS 2D texture
    kScaleform = 5, // a Scaleform texture plane with memory of its own
    kKinds = 6
};

struct Entry {
    std::uint64_t memory;
    std::uint32_t bytes;
    Kind kind;
    bool loading;
    std::uint64_t ns;
    std::uint8_t type;
    // 0.4.6 beta 5: whether it was made with its data, has a render-target or depth-stencil view, and
    // the T# its creator built (2D textures).
    bool initial = false, render_target = false, depth_target = false;
    // 0.4.6 beta 7: D3D11's bind flags as GX stored them (+0x3b: 0x8 sampled, 0x20 render target, 0x40
    // depth, 0x80 shader writes; YEBIS's from its description), and a YEBIS texture's shape.
    std::uint8_t bind = 0;
    std::uint32_t width = 0, height = 0;
    std::uint16_t levels = 0, layers = 0;
    bool has_tsharp = false;
    std::array<std::uint32_t, 8> tsharp{};
    // 0.4.6 beta 9: the CPU wrote it through its own calls (YEBIS's update or Unmap): not a surface
    // (SurfaceAt), filled from memory at its first use.
    bool cpu_written = false;
};

// What a return through the stub belongs to.
enum Op : std::uint8_t { kOpCreate, kOpTsharp, kOpView, kOpCreate1D, kOpYebis, kOpScaleform, kOpYebisUpdate };

struct Pending {
    std::uint64_t ret;
    std::uint64_t out; // creators: holder**; the T# builder: the T# out; views: view**; 1D: the resource
    Kind kind;
    bool initial = false; // creators: initial data given
    bool tsharp = false;  // the T# builder
    std::uint64_t entry_ns = 0;
    Op op = kOpCreate;
    std::uint64_t holder = 0, type = 0; // Scaleform: the texture; views (type): 1 render target, 2 depth
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
std::vector<Gone> g_written; // 0.4.6 beta 9, under g_mutex
std::atomic<bool> g_written_waiting{false};
std::uint8_t* g_image_base = nullptr;
std::size_t g_image_size = 0;
thread_local void* t_fault_context = nullptr;
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
    std::atomic<std::uint64_t> views_rt{0}, views_ds{0}, views_unknown{0}, created_dropped{0};
    // 0.4.6 beta 6.
    std::atomic<std::uint64_t> yebis_rt{0}, yebis_ds{0}, sf_wrapped{0}, unmapped{0}, unmapped_bytes{0},
        superseded{0}, superseded_bytes{0}, superseded_hashed{0};
    // 0.4.6 beta 7: GX textures made without their data by binding (sampled only, render target, depth,
    // shader writes, other), and YEBIS's re-created targets that kept their old images.
    std::array<std::atomic<std::uint64_t>, 5> no_data{};
    std::atomic<std::uint64_t> handed_over{0}, handed_over_bytes{0};
    // 0.4.6 beta 8: surfaces' first uses by how they began and by owner; first fills from memory by
    // owner; CPU write faults on images by owner (and kept: a live surface's contents not uploaded), on
    // the game's buffers; the pool; images left unwatched; input layouts; shaders translated for draws.
    std::array<std::array<std::atomic<std::uint64_t>, std::size_t(SurfaceStart::kCount)>, std::size_t(Owner::kCount)>
        surface_start{};
    std::array<std::atomic<std::uint64_t>, std::size_t(SurfaceStart::kCount)> surface_start_bytes{};
    std::array<std::atomic<std::uint64_t>, std::size_t(Owner::kCount)> first_fill{}, first_fill_bytes{},
        cpu_write{}, cpu_write_kept{};
    std::atomic<std::uint64_t> buffer_cpu_write{0}, buffer_cpu_write_bytes{0};
    struct Written {
        std::uint64_t address, bytes;
        std::uint32_t width, height;
        Owner owner;
        bool kept;
    };
    std::array<Written, 5> cpu_write_top{};
    std::mutex cpu_write_mutex;
    std::array<std::atomic<std::uint64_t>, std::size_t(PoolEvent::kCount)> pool{}, pool_bytes{};
    std::atomic<std::uint64_t> unwatched{0}, unwatched_bytes{0};
    std::atomic<std::uint64_t> input_layouts{0}, input_layouts_loading{0}, input_layout_elements{0};
    std::array<std::atomic<std::uint64_t>, 4> translated{}, translated_ns{}, translated_seen{};
    // 0.4.6 beta 9: YEBIS's update, Map and Unmap calls on its registered 2D textures (updates of
    // surfaces made without data for the GPU), Scaleform's unmaps of planes it owns, and the written
    // events queued and dropped; CPU writes on textures made with data logged with their code.
    std::atomic<std::uint64_t> yebis_updates{0}, yebis_update_surfaces{0}, yebis_maps{0}, yebis_unmaps{0},
        sf_unmaps{0}, written_events{0}, written_dropped{0}, data_writes_logged{0};
    // 0.4.6 beta 10: new textures made over a live texture's memory, at its base or partly, by [the live
    // one's owner kind][the new one's]; images marked written by a creation with data; dead images a draw
    // took (after a destroy / after something newer), of them the owner rule's hand-overs and refusals.
    std::array<std::array<std::atomic<std::uint64_t>, std::size_t(Owner::kCount)>, std::size_t(Owner::kCount)>
        overlap_base{}, overlap_part{};
    std::atomic<int> overlap_logged{0};
    std::atomic<std::uint64_t> creation_writes{0}, creation_write_bytes{0}, creation_surfaces_kept{0},
        dead_taken_destroyed{0},
        dead_taken_reused{0}, dead_taken_hand_over{0}, dead_taken_refuse{0}, dead_taken_bytes{0},
        copies_memory{0}, copies_memory_owned_destination{0}, copies_memory_both_owned{0};
    std::array<std::atomic<std::uint64_t>, std::size_t(OwnerGuess::kCount)> guesses{}, guess_bytes{};
    struct Made {
        std::uint64_t address, bytes;
        std::uint32_t width, height, levels, layers, format, tile;
    };
    std::array<Made, 5> none_made{}; // the largest images draws made over no game resource this minute
    std::mutex none_made_mutex;
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

void Insert(std::uint64_t key, const Entry& e);
Owner OwnerOf(const Entry& e);

// `key`: the holder, or kMemoryKey | memory for a 1D texture (its holder is read for the fields).
Entry Register(std::uint64_t key, std::uint64_t holder, Kind kind, bool initial) {
    Entry e{Read<std::uint64_t>(holder + 0x20), Read<std::uint32_t>(holder + 0x2c), kind,
            Loading::ScreenUp(), NowNs(), Read<std::uint8_t>(holder + 0x38)};
    e.initial = initial;
    e.bind = Read<std::uint8_t>(holder + 0x3b);
    if (kind == kTexture && t_tsharp_valid) {
        // The builder runs before the memory is allocated: the base (address >> 8, 38 bits over the
        // first word and the low 6 bits of the second) is filled in here.
        e.has_tsharp = true;
        e.tsharp = t_tsharp;
        const std::uint64_t base = e.memory >> 8;
        e.tsharp[0] = static_cast<std::uint32_t>(base);
        e.tsharp[1] = (e.tsharp[1] & ~0x3fu) | static_cast<std::uint32_t>((base >> 32) & 0x3f);
    }
    Insert(key, e);
    return e;
}

// Counted and indexed (under the lock), and the memory's created event queued.
void Insert(std::uint64_t key, const Entry& e) {
    const Kind kind = e.kind;
    g_counts.created[kind].fetch_add(1, std::memory_order_relaxed);
    g_counts.created_bytes[kind].fetch_add(e.bytes, std::memory_order_relaxed);
    if (e.loading) {
        g_counts.created_loading[kind].fetch_add(1, std::memory_order_relaxed);
    }
    std::scoped_lock lock{g_mutex};
    if (const auto old = g_by_holder.find(key); old != g_by_holder.end() && old->second.kind != kBuffer) {
        g_live_texture_bytes.fetch_sub(old->second.bytes, std::memory_order_relaxed); // reused unseen
    }
    // 0.4.6 beta 10 (census): live textures the new one's memory overlaps, at their base or partly (memory
    // the game gave to something new while the registry still holds the old one: an alias, or a destroy
    // it did not see; bbhost counts the same).
    if (kind != kBuffer && e.memory && e.bytes) {
        auto it = g_texture_at.upper_bound(e.memory);
        if (it != g_texture_at.begin()) {
            --it;
        }
        for (int guard = 0; it != g_texture_at.end() && it->first < e.memory + e.bytes && guard < 64; ++it, ++guard) {
            if (it->second == key) {
                continue;
            }
            const auto live = g_by_holder.find(it->second);
            if (live == g_by_holder.end() || live->second.memory + std::max<std::uint32_t>(live->second.bytes, 1) <= e.memory) {
                continue;
            }
            const Entry& old = live->second;
            const bool base = old.memory == e.memory;
            const std::size_t a = std::size_t(OwnerOf(old)), b = std::size_t(OwnerOf(e));
            (base ? g_counts.overlap_base : g_counts.overlap_part)[a][b].fetch_add(1, std::memory_order_relaxed);
            if (g_counts.overlap_logged.fetch_add(1, std::memory_order_relaxed) < 12) {
                host_log("GX memory: %s [0x%llx, +0x%x) made over the live %s [0x%llx, +0x%x) made %.1f s before (%s)",
                         OwnerName(Owner(b)), (unsigned long long)e.memory, e.bytes, OwnerName(Owner(a)),
                         (unsigned long long)old.memory, old.bytes, double(e.ns - old.ns) / 1e9,
                         base ? "at its base" : "partly");
            }
        }
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
            Gone made{e.memory, e.bytes, e.ns};
            made.with_data = e.initial;
            if (kind == kYebis && !e.initial) {
                made.width = e.width;
                made.height = e.height;
                made.levels = e.levels;
                made.layers = e.layers;
                made.hand_over = e.width != 0 && e.height != 0;
            }
            g_created.push_back(made);
            g_created_waiting.store(true, std::memory_order_release);
        } else {
            g_counts.created_dropped.fetch_add(1, std::memory_order_relaxed);
        }
    }
    g_by_holder[key] = e;
}

// 0.4.6 beta 6: a texture the game's middleware made (YEBIS, Scaleform), as a resource of the game's.
void RegisterOther(std::uint64_t key, std::uint64_t memory, std::uint32_t bytes, Kind kind, bool initial,
                   bool render_target, bool depth_target, std::uint32_t width = 0, std::uint32_t height = 0,
                   std::uint16_t levels = 0, std::uint16_t layers = 0, std::uint8_t bind = 0) {
    Entry e{memory, bytes, kind, Loading::ScreenUp(), NowNs(), 2};
    e.initial = initial;
    e.bind = bind;
    e.render_target = render_target;
    e.depth_target = depth_target;
    e.width = width;
    e.height = height;
    e.levels = levels;
    e.layers = layers;
    Insert(key, e);
}

// `memory_now`: where the resource's memory is at its destructor's entry (the holder is still whole).
namespace Rebuild {
void Destroyed(std::uint64_t memory, const Entry& e);
}

void Unregister(std::uint64_t holder, bool texture, std::uint64_t memory_now) {
    // 0.4.6 beta 5: the game's Map does not move a resource's memory (a rename writes into a copy
    // from GX's own pool, the holder's memory unchanged), so a resource whose memory differs here is
    // not one this registry knows the history of, and is left alone (that range may be another's by
    // now). 0.4.6 beta 6: one whose memory the game unmapped has none (MemoryReleased).
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
        if (e.kind == kYebis) {
            Rebuild::Destroyed(e.memory == memory_now ? e.memory : 0, e);
        }
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
            Gone gone{e.memory, e.bytes, NowNs()};
            gone.grace = e.kind == kYebis; // a rebuild may take its images (beta 7)
            g_gone.push_back(gone);
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

// 0.4.6 beta 5: the view creators (rsi = view**) and the 1D creator (rdi = the resource it fills,
// rcx = initial data), each read at its return.
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

// 0.4.6 beta 6: YEBIS's 2D creator (r9 = the object's out, rdx = initial data) and Scaleform's texture
// Initialize (rdi = the texture), read at their return; the destructors that end them.
GUEST_ABI std::int64_t YebisCreateEntry(std::uint64_t, const std::uint64_t* saved) {
    t_pending.push_back({saved[6], saved[0], kYebis, saved[3] != 0, false, NowNs(), kOpYebis});
    const_cast<std::uint64_t*>(saved)[6] = reinterpret_cast<std::uint64_t>(g_return_stub);
    return 0;
}

GUEST_ABI std::int64_t ScaleformInitEntry(std::uint64_t, const std::uint64_t* saved) {
    t_pending.push_back({saved[6], 0, kScaleform, true, false, NowNs(), kOpScaleform, saved[5]});
    const_cast<std::uint64_t*>(saved)[6] = reinterpret_cast<std::uint64_t>(g_return_stub);
    return 0;
}

// 0.4.6 beta 8 (research): YEBIS's rebuilds. A rebuild is a burst of YEBIS 2D textures made less than
// 250 ms apart; for each, where its textures landed against the YEBIS textures destroyed in the last 10 s
// (the memory of one with the same shape, of one with another shape, inside one's memory, or fresh
// memory), how many YEBIS textures were live when it began, and the destroys around it (the 2 s before,
// during, the 2 s after): whether the old set goes before or after the new one is made.
namespace Rebuild {
struct Dead {
    std::uint64_t bytes, ns;
    std::uint32_t width, height;
    std::uint16_t levels, layers;
};
enum Landed { kSameShape, kOtherShape, kInside, kFresh, kLandedCount };
struct Burst {
    std::uint64_t first_ns = 0, last_ns = 0, made = 0, targets = 0, live_at_start = 0, before = 0, during = 0,
                  after = 0;
    std::array<std::uint64_t, kLandedCount> landed{};
};
constexpr std::uint64_t kGapNs = 250'000'000, kAroundNs = 2'000'000'000, kDeadNs = 10'000'000'000;
std::mutex mutex;
std::map<std::uint64_t, Dead> dead;   // memory -> a YEBIS 2D texture destroyed in the last 10 s
std::deque<std::uint64_t> destroys;   // when YEBIS 2D textures were destroyed (the last 2 s)
std::int64_t live = 0;                // YEBIS 2D textures live
Burst current, ended;                 // `ended` waits for its 2 s after
int reported = 0;

void Report(const Burst& b) {
    if (b.made < 32 || reported >= 40) {
        return; // single textures, or enough reported
    }
    ++reported;
    host_log("YEBIS rebuild: %llu 2D textures made over %.0f ms (%llu render targets); landed on: the memory of a "
             "destroyed one with the same shape %llu, with another shape %llu, inside a destroyed one's memory "
             "%llu, fresh memory %llu; YEBIS 2D textures live when it began %llu; destroyed in the 2 s before it "
             "%llu, during it %llu, in the 2 s after it %llu",
             (unsigned long long)b.made, double(b.last_ns - b.first_ns) / 1e6, (unsigned long long)b.targets,
             (unsigned long long)b.landed[kSameShape], (unsigned long long)b.landed[kOtherShape],
             (unsigned long long)b.landed[kInside], (unsigned long long)b.landed[kFresh],
             (unsigned long long)b.live_at_start, (unsigned long long)b.before, (unsigned long long)b.during,
             (unsigned long long)b.after);
}

// Under `mutex`: the ended burst reported once its 2 s after have passed.
void Settle(std::uint64_t now) {
    if (ended.made != 0 && now - ended.last_ns >= kAroundNs) {
        Report(ended);
        ended = {};
    }
    while (!destroys.empty() && now - destroys.front() > kAroundNs) {
        destroys.pop_front();
    }
    for (auto it = dead.begin(); it != dead.end();) {
        it = now - it->second.ns > kDeadNs ? dead.erase(it) : std::next(it);
    }
}

void Made(std::uint64_t memory, std::uint32_t width, std::uint32_t height, std::uint16_t levels,
          std::uint16_t layers, bool target) {
    const std::uint64_t now = NowNs();
    std::scoped_lock lock{mutex};
    if (current.made != 0 && now - current.last_ns > kGapNs) {
        if (ended.made != 0) {
            Report(ended); // a third burst before the second's 2 s after: report it as it stands
        }
        ended = current;
        current = {};
    }
    Settle(now);
    if (current.made == 0) {
        current.first_ns = now;
        current.live_at_start = std::uint64_t(std::max<std::int64_t>(live, 0));
        current.before = destroys.size();
    }
    current.last_ns = now;
    ++current.made;
    current.targets += target ? 1 : 0;
    Landed landed = kFresh;
    if (const auto at = dead.find(memory); at != dead.end()) {
        const Dead& d = at->second;
        landed = d.width == width && d.height == height && d.levels == levels && d.layers == layers ? kSameShape
                                                                                                    : kOtherShape;
    } else if (auto it = dead.upper_bound(memory); it != dead.begin() && std::prev(it)->first +
                                                                             std::prev(it)->second.bytes > memory) {
        landed = kInside;
    }
    ++current.landed[landed];
    ++live;
}

void Destroyed(std::uint64_t memory, const Entry& e) {
    const std::uint64_t now = NowNs();
    std::scoped_lock lock{mutex};
    Settle(now);
    --live;
    destroys.push_back(now);
    if (current.made != 0 && now - current.last_ns <= kGapNs) {
        ++current.during;
    } else if (ended.made != 0 && now - ended.last_ns < kAroundNs) {
        ++ended.after;
    }
    if (memory && dead.size() < 8192) {
        dead[memory] = Dead{std::max<std::uint64_t>(e.bytes, 1), now, e.width, e.height, e.levels, e.layers};
    }
}

void Minute() {
    std::scoped_lock lock{mutex};
    Settle(NowNs());
}
} // namespace Rebuild

void NoteYebisCreated(std::uint64_t object, bool initial) {
    if (!object || Read<std::uint32_t>(object + 0x80) != 3) {
        return; // not a 2D texture
    }
    const std::uint64_t memory = Read<std::uint64_t>(object + 0x98);
    const std::uint32_t bytes = Read<std::uint32_t>(object + 0x150);
    const std::uint32_t bind = Read<std::uint32_t>(object + 0x188 + 8 * 4); // D3D11 BindFlags
    // Width, height, mip levels and array size, the description's first four words.
    const auto shape = Read<std::array<std::uint32_t, 4>>(object + 0x188);
    if (!memory || !bytes) {
        return;
    }
    const bool rt = (bind & 0x20) != 0, ds = (bind & 0x40) != 0;
    if (rt) {
        g_counts.yebis_rt.fetch_add(1, std::memory_order_relaxed);
    }
    if (ds) {
        g_counts.yebis_ds.fetch_add(1, std::memory_order_relaxed);
    }
    const auto levels = std::uint16_t(std::min<std::uint32_t>(shape[2], 0xffff)),
               layers = std::uint16_t(std::min<std::uint32_t>(shape[3], 0xffff));
    Rebuild::Made(memory, shape[0], shape[1], levels, layers, rt || ds);
    RegisterOther(kYebisKey | object, memory, bytes, kYebis, initial, rt, ds, shape[0], shape[1], levels, layers,
                  std::uint8_t(bind & 0xff));
}

// Scaleform made a texture's hardware planes: each one with memory of its own is a texture of the
// game's; one over memory a live resource of the game's holds (a texture it wraps) stays that one's.
void NoteScaleformTexture(std::uint64_t texture) {
    const std::uint8_t planes = Read<std::uint8_t>(texture + 0x45);
    const std::uint64_t array = Read<std::uint64_t>(texture + 0x60);
    if (!array || planes > 4) {
        return;
    }
    for (unsigned i = 0; i < planes; ++i) {
        const std::uint64_t record = array + i * 0x58ull;
        const std::uint32_t bytes = Read<std::uint32_t>(record);
        const std::uint64_t memory = Read<std::uint64_t>(record + 0x50);
        if (!memory || memory == ~0ull || !bytes) {
            continue;
        }
        bool wrapped = false;
        {
            std::scoped_lock lock{g_mutex};
            for (const auto* index : {&g_texture_at, &g_buffer_at}) {
                auto it = index->upper_bound(memory);
                if (it == index->begin()) {
                    continue;
                }
                --it;
                if (const auto e = g_by_holder.find(it->second);
                    e != g_by_holder.end() && it->first + e->second.bytes >= memory + bytes) {
                    wrapped = true;
                    break;
                }
            }
        }
        if (wrapped) {
            g_counts.sf_wrapped.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        RegisterOther(ScaleformKey(texture, i), memory, bytes, kScaleform, true, false, false);
    }
}

// Unregistered only if registered (YEBIS's destructor runs for far more objects than it made).
void UnregisterIfKnown(std::uint64_t key) {
    std::uint64_t memory = 0;
    {
        std::scoped_lock lock{g_mutex};
        const auto it = g_by_holder.find(key);
        if (it == g_by_holder.end()) {
            return;
        }
        memory = it->second.memory;
    }
    Unregister(key, true, memory);
}

GUEST_ABI std::int64_t YebisDestructor(std::uint64_t, const std::uint64_t* saved) {
    UnregisterIfKnown(kYebisKey | saved[5]);
    return 0;
}

GUEST_ABI std::int64_t ScaleformRelease(std::uint64_t, const std::uint64_t* saved) {
    if (saved[5]) {
        for (unsigned i = 0; i < 4; ++i) {
            UnregisterIfKnown(ScaleformKey(saved[5], i));
        }
    }
    return 0;
}

// 0.4.6 beta 9: a registered texture the CPU wrote through its own call: it is not a surface from now on
// (SurfaceAt), and the texture cache marks the images over it written. Whether it was registered.
bool Written(std::uint64_t key, bool count_surface = false) {
    std::scoped_lock lock{g_mutex};
    const auto it = g_by_holder.find(key);
    if (it == g_by_holder.end() || !it->second.memory || !it->second.bytes) {
        return false;
    }
    Entry& e = it->second;
    if (count_surface && !e.initial && ((e.bind & 0xe0) != 0 || e.render_target || e.depth_target)) {
        g_counts.yebis_update_surfaces.fetch_add(1, std::memory_order_relaxed);
    }
    e.cpu_written = true;
    if (g_follow) {
        if (g_written.size() < kQueueCap) {
            g_written.push_back(Gone{e.memory, e.bytes, NowNs()});
            g_written_waiting.store(true, std::memory_order_release);
            g_counts.written_events.fetch_add(1, std::memory_order_relaxed);
        } else {
            g_counts.written_dropped.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return true;
}

bool YebisRegistered(std::uint64_t object) {
    std::scoped_lock lock{g_mutex};
    return object && g_by_holder.count(kYebisKey | object) != 0;
}

// YEBIS's UpdateSubresource (rsi: the resource): its texture is written when it returns.
GUEST_ABI std::int64_t YebisUpdateEntry(std::uint64_t, const std::uint64_t* saved) {
    if (!YebisRegistered(saved[4])) {
        return 0; // a buffer, or a texture this registry does not hold
    }
    Pending p{saved[6], 0, kYebis, false, false, NowNs(), kOpYebisUpdate};
    p.holder = saved[4];
    t_pending.push_back(p);
    const_cast<std::uint64_t*>(saved)[6] = reinterpret_cast<std::uint64_t>(g_return_stub);
    return 0;
}

// YEBIS's Map (rsi: the resource): counted (bbhost found its 2D textures never mapped).
GUEST_ABI std::int64_t YebisMapEntry(std::uint64_t, const std::uint64_t* saved) {
    if (YebisRegistered(saved[4])) {
        g_counts.yebis_maps.fetch_add(1, std::memory_order_relaxed);
    }
    return 0;
}

// YEBIS's Unmap (rsi: the resource): the writes after its Map went to the memory itself.
GUEST_ABI std::int64_t YebisUnmapEntry(std::uint64_t, const std::uint64_t* saved) {
    if (saved[4] && Written(kYebisKey | saved[4])) {
        g_counts.yebis_unmaps.fetch_add(1, std::memory_order_relaxed);
    }
    return 0;
}

// Scaleform's unmapTexture (rdi: the manager, rsi: the texture): the CPU's writes into its planes are in.
GUEST_ABI std::int64_t ScaleformUnmapEntry(std::uint64_t, const std::uint64_t* saved) {
    if (saved[4]) {
        for (unsigned i = 0; i < 4; ++i) {
            if (Written(ScaleformKey(saved[4], i))) {
                g_counts.sf_unmaps.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    return 0;
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

// 0.4.6 beta 8 (research): an input layout (ecx: its element count).
GUEST_ABI std::int64_t InputLayoutCreated(std::uint64_t, const std::uint64_t* saved) {
    g_counts.input_layouts.fetch_add(1, std::memory_order_relaxed);
    g_counts.input_layout_elements.fetch_add(saved[2] & 0xffffffffu, std::memory_order_relaxed);
    if (Loading::ScreenUp()) {
        g_counts.input_layouts_loading.fetch_add(1, std::memory_order_relaxed);
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
    if (p.op == kOpYebis) {
        if (static_cast<std::uint32_t>(result) == 0 && p.out) {
            NoteYebisCreated(Read<std::uint64_t>(p.out), p.initial);
        } else if (static_cast<std::uint32_t>(result) != 0) {
            g_counts.failed.fetch_add(1, std::memory_order_relaxed);
        }
        return p.ret;
    }
    if (p.op == kOpYebisUpdate) {
        if (Written(kYebisKey | p.holder, true)) {
            g_counts.yebis_updates.fetch_add(1, std::memory_order_relaxed);
        }
        return p.ret;
    }
    if (p.op == kOpScaleform) {
        if ((result & 0xff) != 0 && p.holder) {
            NoteScaleformTexture(p.holder);
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
            } else if (p.kind == kTexture && !p.initial) {
                // 0.4.6 beta 7: made without its data. Sampled only (no render-target, depth or
                // shader-write binding: those images take their shape from the draw that writes
                // them), its image is made now as one made with data is, and registered without an
                // upload: its first draw uploads what the game has put there by then.
                const bool sampled_only = (e.bind & 0x08) != 0 && (e.bind & 0xe0) == 0;
                const int cls = sampled_only ? 0 : (e.bind & 0x20) ? 1 : (e.bind & 0x40) ? 2 : (e.bind & 0x80) ? 3 : 4;
                g_counts.no_data[cls].fetch_add(1, std::memory_order_relaxed);
                if (g_follow && sampled_only) {
                    HandCreated(e, slot, false);
                }
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
    g_image_base = image;
    g_image_size = size;
    g_return_stub = ReturnStub(&CreatorReturn);
    if (!g_return_stub) {
        return false;
    }
    if (const char* env = std::getenv("BB_GX_FOLLOW"); !(env && env[0] == '0')) {
        g_follow = true;
    }
    Hook hooks[27];
    bool ok = Prepare(hooks[0], image, size, kCreateBuffer, kCreatorPrologue, &CreatorEntry<kBuffer>) &&
              Prepare(hooks[1], image, size, kCreateTexture, kCreatorPrologue, &CreatorEntry<kTexture>) &&
              Prepare(hooks[2], image, size, kCreateTexture3D, kCreatorPrologue, &CreatorEntry<kTexture3D>) &&
              Prepare(hooks[3], image, size, kBufferRelease, kReleasePrologue, &BufferRelease) &&
              Prepare(hooks[8], image, size, kTsharpBuild, kTsharpPrologue, &TsharpEntry) &&
              Prepare(hooks[9], image, size, kShaderCreators[0], kShaderPrologue[0], &ShaderCreated<0>) &&
              Prepare(hooks[10], image, size, kShaderCreators[1], kShaderPrologue[1], &ShaderCreated<1>) &&
              Prepare(hooks[11], image, size, kShaderCreators[2], kShaderPrologue[1], &ShaderCreated<2>) &&
              Prepare(hooks[12], image, size, kShaderCreators[3], kShaderPrologue[1], &ShaderCreated<3>) &&
              Prepare(hooks[13], image, size, kYebisCreate2D, kYebisCreatePrologue, &YebisCreateEntry) &&
              Prepare(hooks[20], image, size, kScaleformInit, kCreatorPrologue, &ScaleformInitEntry) &&
              Prepare(hooks[21], image, size, kScaleformRelease, kScaleformReleasePrologue, &ScaleformRelease) &&
              Prepare(hooks[14], image, size, kCreateRtv, kCreatorPrologue, &ViewEntry<1>) &&
              Prepare(hooks[15], image, size, kCreateDsv, kDsvPrologue, &ViewEntry<2>) &&
              Prepare(hooks[16], image, size, kCreate1D, kCreate1DPrologue, &Create1DEntry) &&
              Prepare(hooks[22], image, size, kCreateInputLayout, kInputLayoutPrologue, &InputLayoutCreated) &&
              Prepare(hooks[23], image, size, kYebisUpdate, kYebisUpdatePrologue, &YebisUpdateEntry) &&
              Prepare(hooks[24], image, size, kYebisMap, kYebisMapPrologue, &YebisMapEntry) &&
              Prepare(hooks[25], image, size, kYebisUnmap, kYebisUnmapPrologue, &YebisUnmapEntry) &&
              Prepare(hooks[26], image, size, kScaleformUnmap, kScaleformUnmapPrologue, &ScaleformUnmapEntry);
    // The game's destructors (2D and 3D textures at 4-7, 1D at 17-18) and YEBIS's 2D texture's (19).
    for (std::size_t i = 0; ok && i < std::size(kDestructors) + std::size(k1DDestructors) + 1; ++i) {
        const bool one_d = i >= std::size(kDestructors) && i < std::size(kDestructors) + std::size(k1DDestructors);
        const bool yebis = i == std::size(kDestructors) + std::size(k1DDestructors);
        const Destructor& d = yebis   ? kYebisDestructor
                              : one_d ? k1DDestructors[i - std::size(kDestructors)]
                                      : kDestructors[i];
        std::uint8_t expected[16] = {0x55, 0x48, 0x89, 0xe5, 0x53, 0x50, 0x48, 0x89, 0xfb, 0x48, 0x8d, 0x05};
        std::memcpy(expected + 12, &d.disp, 4);
        // The same, the lea as movabs rax, <the vtable's address in this image>.
        std::uint8_t displaced[19] = {0x55, 0x48, 0x89, 0xe5, 0x53, 0x50, 0x48, 0x89, 0xfb, 0x48, 0xb8};
        const std::uint64_t vtable = reinterpret_cast<std::uint64_t>(image) + d.vtable;
        std::memcpy(displaced + 11, &vtable, 8);
        ok = d.site + 16 + d.disp == d.vtable && // the lea's target, from its own bytes
             PrepareDisplaced(hooks[yebis ? 19 : one_d ? 17 + (i - std::size(kDestructors)) : 4 + i], image, size,
                              d.site, expected, displaced,
                              yebis ? &YebisDestructor : one_d ? &Texture1DDestructor : &TextureDestructor);
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
    host_log(g_follow ? "GX resources: the caches follow the game's resources, YEBIS's and Scaleform's textures "
                        "among them: a texture created with its data is prepared in the game's create call and "
                        "registered at the next submission; an image is freed when the game destroys its texture, "
                        "makes something newer over its memory or unmaps it; the game's data stays in VRAM, as on "
                        "the PS4, until the game unmaps its memory (BB_GX_FOLLOW=0: counted only; "
                        "BB_GX_RESOURCES=0: off)"
                      : "GX resources: listening to the game's creators, destructors and views, YEBIS's and "
                        "Scaleform's textures among them (counted only, BB_GX_FOLLOW=0; BB_GX_RESOURCES=0: off)");
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

void TakeWritten(std::vector<Gone>& out) {
    std::scoped_lock lock{g_mutex};
    out.swap(g_written);
    g_written.clear();
    g_written_waiting.store(false, std::memory_order_relaxed);
}

bool WrittenWaiting() {
    return g_written_waiting.load(std::memory_order_acquire);
}

void SetFaultContext(void* ucontext) {
    t_fault_context = ucontext;
}

void NoteDataTextureWrite(std::uint64_t address, std::uint64_t bytes, std::uint32_t width, std::uint32_t height) {
    if (!t_fault_context || g_counts.data_writes_logged.fetch_add(1, std::memory_order_relaxed) >= 24) {
        return;
    }
    std::uint64_t rip = 0, rbp = 0;
#ifdef _WIN32
    const auto* pointers = static_cast<const EXCEPTION_POINTERS*>(t_fault_context);
    if (pointers && pointers->ContextRecord) {
        rip = pointers->ContextRecord->Rip;
        rbp = pointers->ContextRecord->Rbp;
    }
#else
    const auto* uc = static_cast<const ucontext_t*>(t_fault_context);
    rip = std::uint64_t(uc->uc_mcontext.gregs[REG_RIP]);
    rbp = std::uint64_t(uc->uc_mcontext.gregs[REG_RBP]);
#endif
    const auto base = reinterpret_cast<std::uint64_t>(g_image_base);
    auto describe = [&](std::uint64_t at, char* out, std::size_t size) {
        if (base && at >= base && at < base + g_image_size) {
            std::snprintf(out, size, "+0x%llx", (unsigned long long)(at - base));
        } else {
            std::snprintf(out, size, "0x%llx", (unsigned long long)at);
        }
    };
    char frames[512];
    std::size_t used = 0;
    char part[48];
    describe(rip, part, sizeof(part));
    used += std::snprintf(frames + used, sizeof(frames) - used, "%s", part);
    // Game code keeps frame pointers (push rbp; mov rbp, rsp): its callers from the saved rbp chain.
    for (int i = 0; i < 6 && rbp && used < sizeof(frames) - 64; ++i) {
        std::uint64_t pair[2] = {};
        if (!BbPlatform::ReadProcessMemory(reinterpret_cast<const void*>(rbp), pair, sizeof(pair)) || !pair[1]) {
            break;
        }
        describe(pair[1], part, sizeof(part));
        used += std::snprintf(frames + used, sizeof(frames) - used, " < %s", part);
        if (pair[0] <= rbp) {
            break;
        }
        rbp = pair[0];
    }
    host_log("GX memory: a CPU write landed on a texture made with its data (0x%llx, %.1f KiB, %ux%u): %s",
             (unsigned long long)address, double(bytes) / 1024.0, width, height, frames);
}

const char* OwnerName(Owner owner) {
    static constexpr const char* kNames[] = {"no game resource", "a buffer", "a texture made with its data",
                                             "a texture made without", "a render target", "a depth target",
                                             "a 3D texture", "a 1D texture", "a YEBIS texture",
                                             "a Scaleform texture"};
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
    case kYebis:
        return Owner::Yebis;
    case kScaleform:
        return Owner::Scaleform;
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

Surface SurfaceAt(std::uint64_t begin, std::uint64_t end) {
    Surface s;
    if (!g_active || !g_follow || begin >= end) {
        return s;
    }
    std::scoped_lock lock{g_mutex};
    const auto at = g_texture_at.find(begin);
    if (at == g_texture_at.end()) {
        return s;
    }
    const auto it = g_by_holder.find(at->second);
    if (it == g_by_holder.end()) {
        return s;
    }
    const Entry& e = it->second;
    if (e.initial || e.cpu_written || e.memory != begin || begin + e.bytes < end ||
        (e.kind != kTexture && e.kind != kYebis)) {
        return s; // made with its data, not wholly over the image, or not a GX or YEBIS 2D texture
    }
    if ((e.bind & 0xe0) == 0 && !e.render_target && !e.depth_target) {
        return s; // nothing the GPU writes: the CPU fills it (a texture made without data, sampled only)
    }
    s.found = true;
    s.owner = OwnerOf(e);
    s.ns = e.ns;
    return s;
}

void NoteSurfaceStart(Owner owner, SurfaceStart how, std::uint64_t bytes) {
    const std::size_t o = std::min<std::size_t>(std::size_t(owner), std::size_t(Owner::kCount) - 1);
    const std::size_t h = std::min<std::size_t>(std::size_t(how), std::size_t(SurfaceStart::kCount) - 1);
    g_counts.surface_start[o][h].fetch_add(1, std::memory_order_relaxed);
    g_counts.surface_start_bytes[h].fetch_add(bytes, std::memory_order_relaxed);
}

void NoteFirstFill(Owner owner, std::uint64_t bytes) {
    const std::size_t o = std::min<std::size_t>(std::size_t(owner), std::size_t(Owner::kCount) - 1);
    g_counts.first_fill[o].fetch_add(1, std::memory_order_relaxed);
    g_counts.first_fill_bytes[o].fetch_add(bytes, std::memory_order_relaxed);
}

void NoteCpuWrite(Owner owner, bool kept, std::uint64_t address, std::uint64_t bytes, std::uint32_t width,
                  std::uint32_t height) {
    const std::size_t o = std::min<std::size_t>(std::size_t(owner), std::size_t(Owner::kCount) - 1);
    g_counts.cpu_write[o].fetch_add(1, std::memory_order_relaxed);
    if (kept) {
        g_counts.cpu_write_kept[o].fetch_add(1, std::memory_order_relaxed);
    }
    if (owner == Owner::None) {
        return; // only the game's resources are described
    }
    std::scoped_lock lock{g_counts.cpu_write_mutex};
    auto& top = g_counts.cpu_write_top;
    for (const auto& w : top) {
        if (w.address == address && w.bytes != 0) {
            return; // described already
        }
    }
    auto smallest = std::min_element(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.bytes < b.bytes; });
    if (bytes > smallest->bytes) {
        *smallest = {address, bytes, width, height, owner, kept};
    }
}

void NoteBufferCpuWrite(std::uint64_t address, std::uint64_t bytes) {
    if (!g_active) {
        return;
    }
    {
        std::scoped_lock lock{g_mutex};
        bool at_start = false;
        if (!Covering(g_buffer_at, address, address + std::max<std::uint64_t>(bytes, 1), &at_start)) {
            return; // not a live GX buffer's memory
        }
    }
    g_counts.buffer_cpu_write.fetch_add(1, std::memory_order_relaxed);
    g_counts.buffer_cpu_write_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void NotePool(PoolEvent event, std::uint64_t bytes) {
    const std::size_t i = std::min<std::size_t>(std::size_t(event), std::size_t(PoolEvent::kCount) - 1);
    g_counts.pool[i].fetch_add(1, std::memory_order_relaxed);
    g_counts.pool_bytes[i].fetch_add(bytes, std::memory_order_relaxed);
}

void NoteUnwatched(std::uint64_t images, std::uint64_t bytes) {
    g_counts.unwatched.fetch_add(images, std::memory_order_relaxed);
    g_counts.unwatched_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void NoteShaderTranslated(unsigned stage, std::uint64_t ns, bool program_seen) {
    const unsigned s = std::min(stage, 3u);
    g_counts.translated[s].fetch_add(1, std::memory_order_relaxed);
    g_counts.translated_ns[s].fetch_add(ns, std::memory_order_relaxed);
    if (program_seen) {
        g_counts.translated_seen[s].fetch_add(1, std::memory_order_relaxed);
    }
}

void NoteCreationWrites(std::uint64_t images, std::uint64_t bytes, std::uint64_t surfaces_kept) {
    g_counts.creation_writes.fetch_add(images, std::memory_order_relaxed);
    g_counts.creation_write_bytes.fetch_add(bytes, std::memory_order_relaxed);
    g_counts.creation_surfaces_kept.fetch_add(surfaces_kept, std::memory_order_relaxed);
}

LiveHere LiveAt(std::uint64_t memory) {
    LiveHere here;
    if (!g_active) {
        return here;
    }
    std::scoped_lock lock{g_mutex};
    const auto at = g_texture_at.find(memory);
    if (at == g_texture_at.end()) {
        return here;
    }
    const auto it = g_by_holder.find(at->second);
    if (it == g_by_holder.end()) {
        return here;
    }
    here.found = true;
    here.without_data = !it->second.initial;
    here.bytes = it->second.bytes;
    return here;
}

void NoteDeadImageTaken(bool destroyed, bool hand_over, bool refuse, std::uint64_t bytes) {
    (destroyed ? g_counts.dead_taken_destroyed : g_counts.dead_taken_reused).fetch_add(1, std::memory_order_relaxed);
    if (hand_over) {
        g_counts.dead_taken_hand_over.fetch_add(1, std::memory_order_relaxed);
    }
    if (refuse) {
        g_counts.dead_taken_refuse.fetch_add(1, std::memory_order_relaxed);
    }
    g_counts.dead_taken_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void NoteOwnerGuess(OwnerGuess guess, std::uint64_t bytes) {
    g_counts.guesses[std::size_t(guess)].fetch_add(1, std::memory_order_relaxed);
    g_counts.guess_bytes[std::size_t(guess)].fetch_add(bytes, std::memory_order_relaxed);
}

void NoteCopyThroughMemory(bool source_owned, bool destination_owned) {
    g_counts.copies_memory.fetch_add(1, std::memory_order_relaxed);
    if (destination_owned) {
        g_counts.copies_memory_owned_destination.fetch_add(1, std::memory_order_relaxed);
        if (source_owned) {
            g_counts.copies_memory_both_owned.fetch_add(1, std::memory_order_relaxed);
        }
    }
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

void NoteDrawMadeNone(std::uint64_t address, std::uint64_t bytes, std::uint32_t width, std::uint32_t height,
                      std::uint32_t levels, std::uint32_t layers, std::uint32_t format, std::uint32_t tile) {
    std::scoped_lock lock{g_counts.none_made_mutex};
    auto& list = g_counts.none_made;
    if (bytes <= list.back().bytes) {
        return;
    }
    list.back() = {address, bytes, width, height, levels, layers, format, tile};
    std::sort(list.begin(), list.end(), [](const Counts::Made& a, const Counts::Made& b) { return a.bytes > b.bytes; });
}

void NoteHandedOver(std::uint64_t images, std::uint64_t bytes) {
    g_counts.handed_over.fetch_add(images, std::memory_order_relaxed);
    g_counts.handed_over_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void NoteSuperseded(std::uint64_t bytes) {
    g_counts.superseded.fetch_add(1, std::memory_order_relaxed);
    g_counts.superseded_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void NoteSupersededHashed(std::uint64_t bytes) {
    g_counts.superseded_hashed.fetch_add(bytes, std::memory_order_relaxed);
}

void MemoryReleased(std::uint64_t address, std::uint64_t size) {
    if (!g_active || size == 0) {
        return;
    }
    std::uint64_t n = 0, bytes = 0;
    std::scoped_lock lock{g_mutex};
    for (auto* index : {&g_texture_at, &g_buffer_at}) {
        for (auto it = index->lower_bound(address); it != index->end() && it->first < address + size;) {
            if (const auto e = g_by_holder.find(it->second); e != g_by_holder.end()) {
                if (e->second.kind != kBuffer) {
                    g_live_texture_bytes.fetch_sub(e->second.bytes, std::memory_order_relaxed);
                }
                bytes += e->second.bytes;
                e->second.memory = 0; // its destructor, if it comes, finds nothing to free
                e->second.bytes = 0;
            }
            it = index->erase(it);
            ++n;
        }
    }
    g_counts.unmapped.fetch_add(n, std::memory_order_relaxed);
    g_counts.unmapped_bytes.fetch_add(bytes, std::memory_order_relaxed);
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
        host_log("GX follow this minute: %llu textures prepared at creation (made with their data, or sampled only "
                 "without it: beta 7) (%llu without a T#, %llu with no "
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
        host_log("GX memory this minute: 1D textures created %llu (%.0f MiB), destroyed %llu; textures made without "
                 "their data: %llu sampled only (prepared at creation), %llu render targets, %llu depth, %llu "
                 "written by shaders, %llu other; views: %llu render targets, %llu depth targets, %llu of textures "
                 "not known; images retired because the game made something newer over them %llu (%.0f MiB); "
                 "YEBIS's re-created targets that kept the old ones' images %llu (%.0f MiB); %llu created events "
                 "not queued",
                 (unsigned long long)created[kTexture1D], Mib(created_bytes[kTexture1D]),
                 (unsigned long long)destroyed[kTexture1D], (unsigned long long)take(g_counts.no_data[0]),
                 (unsigned long long)take(g_counts.no_data[1]), (unsigned long long)take(g_counts.no_data[2]),
                 (unsigned long long)take(g_counts.no_data[3]), (unsigned long long)take(g_counts.no_data[4]),
                 (unsigned long long)take(g_counts.views_rt), (unsigned long long)take(g_counts.views_ds),
                 (unsigned long long)take(g_counts.views_unknown), (unsigned long long)take(g_counts.reuse_dead),
                 Mib(take(g_counts.reuse_dead_bytes)), (unsigned long long)take(g_counts.handed_over),
                 Mib(take(g_counts.handed_over_bytes)), (unsigned long long)take(g_counts.created_dropped));
        // 0.4.6 beta 6: the middleware's textures, memory the game unmapped under live resources, and
        // the GPU's stale drawings the game's memory has moved on from.
        host_log("GX memory, the middleware this minute: YEBIS 2D textures made %llu (%.0f MiB; %llu render "
                 "targets, %llu depth), destroyed %llu; Scaleform texture planes with memory of their own %llu "
                 "(%.0f MiB), released %llu, %llu wrapping a game texture; live resources whose memory the game "
                 "unmapped %llu (%.0f MiB); stale images the GPU drew whose memory was written since freed %llu "
                 "(%.0f MiB; %.0f MiB of memory hashed to tell)",
                 (unsigned long long)created[kYebis], Mib(created_bytes[kYebis]),
                 (unsigned long long)take(g_counts.yebis_rt), (unsigned long long)take(g_counts.yebis_ds),
                 (unsigned long long)destroyed[kYebis], (unsigned long long)created[kScaleform],
                 Mib(created_bytes[kScaleform]), (unsigned long long)destroyed[kScaleform],
                 (unsigned long long)take(g_counts.sf_wrapped), (unsigned long long)take(g_counts.unmapped),
                 Mib(take(g_counts.unmapped_bytes)), (unsigned long long)take(g_counts.superseded),
                 Mib(take(g_counts.superseded_bytes)), Mib(take(g_counts.superseded_hashed)));
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
        // 0.4.6 beta 6 (research): the largest images draws made over memory no resource of the game's
        // holds, with the mapping each lies in; and the mappings the game's textures live in.
        std::array<Counts::Made, 5> none{};
        {
            std::scoped_lock lock{g_counts.none_made_mutex};
            none = g_counts.none_made;
            g_counts.none_made = {};
        }
        std::string described;
        for (const Counts::Made& m : none) {
            if (m.bytes == 0) {
                break;
            }
            std::uintptr_t start = 0, end = 0;
            int mapped = 0;
            runtime_memory_region(std::uintptr_t(m.address), &start, &end, &mapped);
            char part[256];
            std::snprintf(part, sizeof(part),
                          "%s0x%llx %.1f MiB %ux%u, %u mips, %u layers, format %u, tile %u, in a mapping at 0x%llx of "
                          "%.0f MiB",
                          described.empty() ? "" : "; ", (unsigned long long)m.address, double(m.bytes) / 1048576.0,
                          m.width, m.height, m.levels, m.layers, m.format, m.tile, (unsigned long long)start,
                          Mib(end - start));
            described += part;
        }
        if (!described.empty()) {
            std::vector<std::pair<std::uint64_t, std::uint32_t>> textures;
            {
                std::scoped_lock lock{g_mutex};
                textures.reserve(g_texture_at.size());
                for (const auto& [memory, key] : g_texture_at) {
                    if (const auto e = g_by_holder.find(key); e != g_by_holder.end()) {
                        textures.emplace_back(memory, e->second.bytes);
                    }
                }
            }
            std::map<std::uintptr_t, std::pair<std::uint64_t, std::uint64_t>> regions; // start -> (end, bytes)
            for (const auto& [memory, bytes] : textures) {
                std::uintptr_t start = 0, end = 0;
                int mapped = 0;
                if (runtime_memory_region(std::uintptr_t(memory), &start, &end, &mapped)) {
                    auto& r = regions[start];
                    r.first = end;
                    r.second += bytes;
                }
            }
            std::vector<std::pair<std::uint64_t, std::uintptr_t>> largest;
            for (const auto& [start, r] : regions) {
                largest.emplace_back(r.second, start);
            }
            std::sort(largest.rbegin(), largest.rend());
            std::string homes;
            for (std::size_t i = 0; i < largest.size() && i < 4; ++i) {
                char part[128];
                std::snprintf(part, sizeof(part), "%s0x%llx of %.0f MiB (%.0f MiB of textures)", homes.empty() ? "" : ", ",
                              (unsigned long long)largest[i].second,
                              Mib(regions[largest[i].second].first - largest[i].second), Mib(largest[i].first));
                homes += part;
            }
            host_log("GX memory, the largest images draws made over no game resource this minute: %s; the game's live "
                     "textures lie in the mappings at %s",
                     described.c_str(), homes.empty() ? "(none)" : homes.c_str());
        }
    }
    host_log("GX research this minute: buffers released with the game's data in VRAM at their memory %llu of %llu "
             "(%.0f MiB); shaders created: vertex %llu + %llu, pixel %llu, compute %llu (%llu during loading screens)",
             (unsigned long long)take(g_counts.released_resident), (unsigned long long)take(g_counts.released_checked),
             Mib(take(g_counts.released_resident_bytes)), (unsigned long long)take(g_counts.shaders[0]),
             (unsigned long long)take(g_counts.shaders[1]), (unsigned long long)take(g_counts.shaders[2]),
             (unsigned long long)take(g_counts.shaders[3]), (unsigned long long)take(g_counts.shaders_loading));
    // 0.4.6 beta 8: surfaces, first fills, CPU writes, the pool and the unwatched images; then shaders.
    Rebuild::Minute();
    static const char* const kStartNames[] = {"empty", "depth cleared", "depth cleared under the game's clear",
                                              "taken from the GPU's writes as a buffer since it was made",
                                              "filled from memory (its resource gone)",
                                              "taken from the GPU's writes from before it was made",
                                              "started empty, the GPU's earlier writes left out"};
    std::string starts, fills, writes;
    for (std::size_t h = 0; h < std::size_t(SurfaceStart::kCount); ++h) {
        std::string owners;
        std::uint64_t total = 0;
        for (std::size_t o = 0; o < std::size_t(Owner::kCount); ++o) {
            const std::uint64_t n = take(g_counts.surface_start[o][h]);
            if (n != 0) {
                char part[96];
                std::snprintf(part, sizeof(part), "%s%s %llu", owners.empty() ? "" : ", ", OwnerName(Owner(o)),
                              (unsigned long long)n);
                owners += part;
                total += n;
            }
        }
        const std::uint64_t bytes = take(g_counts.surface_start_bytes[h]);
        if (total != 0) {
            char part[512];
            std::snprintf(part, sizeof(part), "%s%s %llu (%.0f MiB: %s)", starts.empty() ? "" : "; ", kStartNames[h],
                          (unsigned long long)total, Mib(bytes), owners.c_str());
            starts += part;
        }
    }
    for (std::size_t o = 0; o < std::size_t(Owner::kCount); ++o) {
        const std::uint64_t n = take(g_counts.first_fill[o]), bytes = take(g_counts.first_fill_bytes[o]);
        if (n != 0) {
            char part[128];
            std::snprintf(part, sizeof(part), "%s%s %llu (%.0f MiB)", fills.empty() ? "" : ", ", OwnerName(Owner(o)),
                          (unsigned long long)n, Mib(bytes));
            fills += part;
        }
        const std::uint64_t w = take(g_counts.cpu_write[o]), kept = take(g_counts.cpu_write_kept[o]);
        if (w != 0) {
            char part[128];
            std::snprintf(part, sizeof(part), "%s%s %llu (%llu kept)", writes.empty() ? "" : ", ",
                          OwnerName(Owner(o)), (unsigned long long)w, (unsigned long long)kept);
            writes += part;
        }
    }
    std::string written;
    {
        std::scoped_lock lock{g_counts.cpu_write_mutex};
        auto top = g_counts.cpu_write_top;
        g_counts.cpu_write_top = {};
        std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.bytes > b.bytes; });
        for (const auto& w : top) {
            if (w.bytes == 0) {
                break;
            }
            char part[160];
            std::snprintf(part, sizeof(part), "%s0x%llx %.1f MiB %ux%u (%s%s)", written.empty() ? "" : "; ",
                          (unsigned long long)w.address, double(w.bytes) / 1048576.0, w.width, w.height,
                          OwnerName(w.owner), w.kept ? ", kept" : "");
            written += part;
        }
    }
    host_log("GX memory, surfaces this minute (beta 8): first uses of surfaces the game made without data for the "
             "GPU: %s; first fills from memory, by the resource there: %s; CPU writes that landed on images, by the "
             "live resource there: %s%s%s; on the game's buffers %llu (%.0f MiB); new surfaces that took a dead "
             "target's Vulkan image of their shape %llu (%.0f MiB), found none %llu; images left unwatched (the "
             "calls tell their writes) %llu (%.0f MiB)",
             starts.empty() ? "none" : starts.c_str(), fills.empty() ? "none" : fills.c_str(),
             writes.empty() ? "none" : writes.c_str(), written.empty() ? "" : "; the largest written: ",
             written.c_str(), (unsigned long long)take(g_counts.buffer_cpu_write),
             Mib(take(g_counts.buffer_cpu_write_bytes)), (unsigned long long)take(g_counts.pool[0]),
             Mib(take(g_counts.pool_bytes[0])), (unsigned long long)take(g_counts.pool[1]),
             (unsigned long long)take(g_counts.unwatched), Mib(take(g_counts.unwatched_bytes)));
    static const char* const kStageNames[] = {"vertex", "pixel", "compute", "other"};
    std::string translated;
    for (unsigned s = 0; s < 4; ++s) {
        const std::uint64_t n = take(g_counts.translated[s]), ns = take(g_counts.translated_ns[s]),
                            seen = take(g_counts.translated_seen[s]);
        if (n != 0) {
            char part[160];
            std::snprintf(part, sizeof(part), "%s%s %llu (%.1f ms; %llu of a program translated before)",
                          translated.empty() ? "" : ", ", kStageNames[s], (unsigned long long)n, double(ns) / 1e6,
                          (unsigned long long)seen);
            translated += part;
        }
    }
    host_log("GX memory, write calls this minute (beta 9): YEBIS's updates of its 2D textures %llu (%llu of "
             "surfaces made without data for the GPU), Maps %llu, Unmaps %llu; Scaleform's unmaps of planes "
             "with memory of their own %llu; written textures told to the texture cache %llu (%llu dropped)",
             (unsigned long long)take(g_counts.yebis_updates), (unsigned long long)take(g_counts.yebis_update_surfaces),
             (unsigned long long)take(g_counts.yebis_maps), (unsigned long long)take(g_counts.yebis_unmaps),
             (unsigned long long)take(g_counts.sf_unmaps), (unsigned long long)take(g_counts.written_events),
             (unsigned long long)take(g_counts.written_dropped));
    std::string base_pairs, part_pairs;
    for (std::size_t a = 0; a < std::size_t(Owner::kCount); ++a) {
        for (std::size_t b = 0; b < std::size_t(Owner::kCount); ++b) {
            for (int which = 0; which < 2; ++which) {
                const std::uint64_t n = take((which ? g_counts.overlap_part : g_counts.overlap_base)[a][b]);
                if (n == 0) {
                    continue;
                }
                std::string& out = which ? part_pairs : base_pairs;
                char part[160];
                std::snprintf(part, sizeof(part), "%s%s over %s %llu", out.empty() ? "" : ", ", OwnerName(Owner(b)),
                              OwnerName(Owner(a)), (unsigned long long)n);
                out += part;
            }
        }
    }
    host_log("GX memory, owners this minute (beta 10): new textures over a live texture's memory, at its base: %s; "
             "partly: %s; images marked written by a creation with data over them %llu (%.0f MiB), live surfaces "
             "reaching past one kept %llu; dead images a draw took again: %llu after their texture's destroy, %llu "
             "after something newer was made over them (%.0f MiB in all), of which the owner rule would hand over "
             "%llu (a texture made without data and as large lives there) and refuse %llu; today's guesses it would replace by retiring: dead images kept because another "
             "live texture overlaps %llu (%.0f MiB), because used just before the new resource %llu (%.0f MiB), "
             "stale images freed that had an owner %llu (%.0f MiB); copies run through memory %llu, to an owned "
             "image %llu, owned to owned %llu",
             base_pairs.empty() ? "none" : base_pairs.c_str(), part_pairs.empty() ? "none" : part_pairs.c_str(),
             (unsigned long long)take(g_counts.creation_writes), Mib(take(g_counts.creation_write_bytes)),
             (unsigned long long)take(g_counts.creation_surfaces_kept),
             (unsigned long long)take(g_counts.dead_taken_destroyed), (unsigned long long)take(g_counts.dead_taken_reused),
             Mib(take(g_counts.dead_taken_bytes)), (unsigned long long)take(g_counts.dead_taken_hand_over),
             (unsigned long long)take(g_counts.dead_taken_refuse),
             (unsigned long long)take(g_counts.guesses[std::size_t(OwnerGuess::KeptOverlap)]),
             Mib(take(g_counts.guess_bytes[std::size_t(OwnerGuess::KeptOverlap)])),
             (unsigned long long)take(g_counts.guesses[std::size_t(OwnerGuess::KeptUsedBefore)]),
             Mib(take(g_counts.guess_bytes[std::size_t(OwnerGuess::KeptUsedBefore)])),
             (unsigned long long)take(g_counts.guesses[std::size_t(OwnerGuess::StaleOwned)]),
             Mib(take(g_counts.guess_bytes[std::size_t(OwnerGuess::StaleOwned)])),
             (unsigned long long)take(g_counts.copies_memory),
             (unsigned long long)take(g_counts.copies_memory_owned_destination),
             (unsigned long long)take(g_counts.copies_memory_both_owned));
    host_log("Shaders this minute (beta 8): input layouts the game created %llu (%llu during loading screens, %llu "
             "vertex elements); translated for draws: %s",
             (unsigned long long)take(g_counts.input_layouts), (unsigned long long)take(g_counts.input_layouts_loading),
             (unsigned long long)take(g_counts.input_layout_elements), translated.empty() ? "none" : translated.c_str());
}
} // namespace BbEngine::GxResources

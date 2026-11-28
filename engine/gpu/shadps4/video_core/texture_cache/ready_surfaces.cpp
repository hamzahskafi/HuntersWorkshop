// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 15 part 3: surfaces' memory made during the loading screen (ready_surfaces.h).

#include "video_core/texture_cache/ready_surfaces.h"

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#include "common/thread.h"
#include "engine/loading.h"
#include "video_core/vram_policy.h"

namespace VideoCore::ReadySurfaces {
namespace {

struct Shape {
    vk::ImageCreateInfo ci;
    u32 count = 0;
    u64 bytes = 0; ///< one image's, as allocated
};

std::mutex g_mutex;
std::condition_variable g_cv;
vk::Device g_device{};
VmaAllocator g_allocator = nullptr;
std::vector<Shape> g_learned;   ///< the largest count per shape seen after one load
std::vector<Shape> g_this_load; ///< the surfaces allocated at their draw since the last load ended
std::vector<UniqueImage> g_ready;
u64 g_ready_bytes = 0;
u32 g_requests = 0;
bool g_thread = false;
std::atomic<u64> g_made{0}, g_made_bytes{0}, g_taken{0}, g_freed{0}, g_allocated{0}, g_allocated_ns{0};

u64 CapBytes() {
    static const u64 cap = [] {
        const char* env = std::getenv("BB_READY_SURFACES_MB");
        return (env && env[0] ? std::strtoull(env, nullptr, 10) : 256ull) << 20;
    }();
    return cap;
}

Shape* Find(std::vector<Shape>& shapes, const vk::ImageCreateInfo& ci) {
    for (Shape& s : shapes) {
        if (s.ci == ci) {
            return &s;
        }
    }
    return nullptr;
}

/// Whether more may be made now: VRAM comfortable (or the manager off) and the screen still up.
bool MayMake() {
    return BbEngine::Loading::ScreenUp() && (VramPolicy::Off() || VramPolicy::Comfortable());
}

void MakerMain() {
    Common::SetCurrentThreadName("bb:ReadySurfaces");
    u32 seen = 0;
    for (;;) {
        std::vector<Shape> wanted;
        {
            std::unique_lock lk{g_mutex};
            g_cv.wait(lk, [&] { return g_requests != seen; });
            seen = g_requests;
            for (const Shape& s : g_learned) {
                u32 have = 0;
                for (const UniqueImage& img : g_ready) {
                    have += img.image_ci == s.ci;
                }
                if (s.count > have) {
                    wanted.push_back({s.ci, s.count - have, s.bytes});
                }
            }
        }
        for (const Shape& s : wanted) {
            for (u32 i = 0; i < s.count; ++i) {
                {
                    std::scoped_lock lk{g_mutex};
                    if (g_ready_bytes + s.bytes > CapBytes()) {
                        break;
                    }
                }
                if (!MayMake()) {
                    break;
                }
                UniqueImage img{g_device, g_allocator};
                if (!img.Create(s.ci, true)) {
                    break; // VRAM refused it: nothing more of this shape
                }
                g_made.fetch_add(1, std::memory_order_relaxed);
                g_made_bytes.fetch_add(img.size_bytes, std::memory_order_relaxed);
                std::scoped_lock lk{g_mutex};
                g_ready_bytes += img.size_bytes;
                g_ready.push_back(std::move(img));
            }
        }
    }
}

} // namespace

bool Enabled() {
    static const bool on = [] {
        const char* env = std::getenv("BB_READY_SURFACES");
        return !(env && env[0] == '0');
    }();
    return on;
}

void Init(vk::Device device, VmaAllocator allocator) {
    std::scoped_lock lk{g_mutex};
    g_device = device;
    g_allocator = allocator;
}

bool Take(const vk::ImageCreateInfo& ci, UniqueImage& out) {
    std::scoped_lock lk{g_mutex};
    for (auto it = g_ready.begin(); it != g_ready.end(); ++it) {
        if (it->image_ci == ci) {
            g_ready_bytes -= it->size_bytes;
            out = std::move(*it);
            g_ready.erase(it);
            g_taken.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }
    return false;
}

void NoteAllocated(const vk::ImageCreateInfo& ci, u64 bytes, u64 ns, bool after_load) {
    if (!after_load) {
        return;
    }
    g_allocated.fetch_add(1, std::memory_order_relaxed);
    g_allocated_ns.fetch_add(ns, std::memory_order_relaxed);
    if (!Enabled()) {
        return;
    }
    std::scoped_lock lk{g_mutex};
    if (Shape* s = Find(g_this_load, ci)) {
        ++s->count;
        s->bytes = bytes;
    } else {
        g_this_load.push_back({ci, 1, bytes});
    }
}

void Make() {
    if (!Enabled()) {
        return;
    }
    std::scoped_lock lk{g_mutex};
    if (g_learned.empty() || !g_allocator) {
        return;
    }
    ++g_requests;
    if (!g_thread) {
        g_thread = true;
        std::thread(MakerMain).detach();
    }
    g_cv.notify_one();
}

void EndLearning() {
    std::scoped_lock lk{g_mutex};
    for (const Shape& s : g_this_load) {
        if (Shape* known = Find(g_learned, s.ci)) {
            known->count = std::max(known->count, s.count);
            known->bytes = s.bytes;
        } else {
            g_learned.push_back(s);
        }
    }
    g_this_load.clear();
}

void FreeUntaken() {
    std::vector<UniqueImage> untaken;
    {
        std::scoped_lock lk{g_mutex};
        untaken.swap(g_ready);
        g_ready_bytes = 0;
    }
    // Never used by the GPU: freed at once.
    g_freed.fetch_add(untaken.size(), std::memory_order_relaxed);
}

Totals TotalsNow() {
    return {g_made.load(), g_made_bytes.load(), g_taken.load(), g_freed.load(), g_allocated.load(),
            g_allocated_ns.load()};
}

} // namespace VideoCore::ReadySurfaces

// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 19 (docs/stage-a-cost.md, items 4 and 5): the GPU command thread's decode tasks
// (Liverpool's coroutines, one per submission and constant-engine buffer) take their frames from
// here and give them back when done, instead of the heap: a frame was allocated under the
// submission queue's lock by the game's submit, and freed on the GPU command thread, both through
// the process heap's lock. A few sizes (the coroutines' frames), a few frames each; the rest goes
// to the heap as before. BB_FRAME_POOL=0: the heap always.
#pragma once

#include <array>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <new>

namespace BbFramePool {

namespace Detail {
constexpr std::size_t kSizes = 6, kKept = 64;
struct Pool {
    std::mutex mutex;
    std::array<std::size_t, kSizes> size{};
    std::array<std::array<void*, kKept>, kSizes> frames{};
    std::array<std::size_t, kSizes> count{};
};
inline Pool& Get() {
    static Pool pool;
    return pool;
}
inline bool On() {
    static const bool on = [] {
        const char* env = std::getenv("BB_FRAME_POOL");
        return !(env && env[0] == '0');
    }();
    return on;
}
} // namespace Detail

inline void* Take(std::size_t size) {
    if (Detail::On()) {
        auto& p = Detail::Get();
        std::scoped_lock lock{p.mutex};
        for (std::size_t i = 0; i < Detail::kSizes; ++i) {
            if (p.size[i] == size && p.count[i]) {
                return p.frames[i][--p.count[i]];
            }
        }
    }
    return ::operator new(size);
}

inline void Give(void* frame, std::size_t size) {
    if (frame && Detail::On()) {
        auto& p = Detail::Get();
        std::scoped_lock lock{p.mutex};
        for (std::size_t i = 0; i < Detail::kSizes; ++i) {
            if (p.size[i] == 0) {
                p.size[i] = size; // a size not seen before takes a free class
            }
            if (p.size[i] == size) {
                if (p.count[i] < Detail::kKept) {
                    p.frames[i][p.count[i]++] = frame;
                    return;
                }
                break;
            }
        }
    }
    ::operator delete(frame);
}

} // namespace BbFramePool

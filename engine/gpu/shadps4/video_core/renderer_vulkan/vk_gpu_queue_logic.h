// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 21 (docs/gpu-fences.md): the GPU queue's logic without Vulkan (vk_gpu_progress.h has
// the word itself), so tests/test_gpu_queue.cpp checks it against a model GPU.
#pragma once

#include <cstdint>
#include <mutex>

#include "common/types.h"

namespace Vulkan {

namespace GpuQueueLogic {

/// The word has passed `mark` (a count that wraps: compared as a difference, so about 37 hours at
/// 16,000 marks a second before two compared values could be half the range apart).
[[nodiscard]] constexpr bool Passed(u32 word, u32 mark) noexcept {
    return static_cast<s32>(word - mark) >= 0;
}

/// At a submission: the mark which, once passed, has the GPU on the last unit of the work given to
/// it. `recorded`: marks recorded up to this submission; `before`: those of the submissions before.
/// With a mark of its own, the last unit is the one that mark ends (started when the one before it
/// is done); with none (cut between two marks), the work after the last mark recorded.
[[nodiscard]] constexpr u32 HandOverMark(u32 recorded, u32 before) noexcept {
    return recorded != before ? recorded - 1 : recorded;
}

/// At the end of a game submission with work recorded since the last submission: hold it, or hand
/// it over because the GPU has started the last unit given to it (`word` past `handover`, or no
/// submission before) or has finished everything (`last_done`: the tick it knows).
enum class HandOver { Hold, OnLastUnit, OutOfWork };
[[nodiscard]] constexpr HandOver Decide(bool first, u32 word, u32 handover, bool last_done) noexcept {
    if (first || Passed(word, handover)) {
        return HandOver::OnLastUnit;
    }
    return last_done ? HandOver::OutOfWork : HandOver::Hold;
}

/// How a queued signal is ready: not yet, its engine submission done, or its game submission passed.
enum Ready : int { NotReady = 0, ByTick = 1, ByMark = 2 };

/// Runs the queue's front signals that are ready, in order and each once, by one runner at a time:
/// `wait` false gives way to a runner already at it (returns -1). `ready(front)` under `list_mutex`;
/// the signal runs outside it, still queued (waits are answered from it until it is written), and is
/// popped after; `ran(how)` after each. Producers only push at the back.
template <typename Queue, typename ListMutex, typename RunMutex, typename ReadyFn, typename RanFn>
int RunReady(Queue& queue, ListMutex& list_mutex, RunMutex& run_mutex, bool wait, ReadyFn&& ready,
             RanFn&& ran) {
    std::unique_lock run{run_mutex, std::defer_lock};
    if (wait) {
        run.lock();
    } else if (!run.try_lock()) {
        return -1;
    }
    int count = 0;
    for (;;) {
        decltype(queue.front().signal) signal;
        int how;
        {
            std::scoped_lock lk{list_mutex};
            if (queue.empty()) {
                break;
            }
            how = ready(queue.front());
            if (how == NotReady) {
                break;
            }
            signal = std::move(queue.front().signal);
        }
        signal();
        {
            std::scoped_lock lk{list_mutex};
            queue.pop_front();
        }
        ran(how);
        ++count;
    }
    return count;
}

} // namespace GpuQueueLogic

} // namespace Vulkan

// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 20 (video_core/amdgpu/submit_queue.h, docs/chain-waits.md item 2): the submission queue
// the GPU command thread reads without a lock, with real threads. Three "game" threads push under a
// lock of their own, numbering as they push; one consumer reads the front, then pops it, as the GPU
// command thread does. Checked: every entry arrives once, in the order pushed, across many blocks;
// the queue's size never reads more than was pushed; bursts with pauses (the consumer finds it
// empty) and back to back.
#include "video_core/amdgpu/submit_queue.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

int main() {
    AmdGpu::SubmitQueue<std::uint64_t> queue;
    std::mutex producers;
    std::uint64_t next = 1; // under `producers`
    constexpr std::uint64_t kTotal = 600000;
    std::atomic<bool> done{false};
    int fail = 0;

    std::vector<std::thread> threads;
    for (int t = 0; t < 3; ++t) {
        threads.emplace_back([&, t] {
            std::mt19937 rng{static_cast<unsigned>(t + 1)};
            for (;;) {
                const int burst = 1 + static_cast<int>(rng() % 200);
                {
                    std::scoped_lock lock{producers};
                    for (int i = 0; i < burst && next <= kTotal; ++i) {
                        queue.Push(next++);
                    }
                    if (next > kTotal) {
                        return;
                    }
                }
                if (rng() % 8 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(rng() % 300));
                }
            }
        });
    }
    std::uint64_t expect = 1, empties = 0;
    while (expect <= kTotal) {
        std::uint64_t value = 0;
        if (!queue.Front(value)) {
            ++empties;
            std::this_thread::yield();
            continue;
        }
        if (value != expect) {
            std::printf("FAIL: got %llu, expected %llu\n", static_cast<unsigned long long>(value),
                        static_cast<unsigned long long>(expect));
            ++fail;
            break;
        }
        if (queue.Size() == 0 || queue.Size() > kTotal) {
            std::printf("FAIL: size %zu with an entry at the front\n", queue.Size());
            ++fail;
            break;
        }
        queue.Pop();
        ++expect;
    }
    for (auto& t : threads) {
        t.join();
    }
    done = true;
    std::uint64_t value;
    if (queue.Front(value) || queue.Size() != 0) {
        std::printf("FAIL: entries left over\n");
        ++fail;
    }
    if (fail) {
        std::printf("submit-queue-test: failed\n");
        return 1;
    }
    std::printf("submit-queue-test: ok (%llu entries, found empty %llu times)\n",
                static_cast<unsigned long long>(kTotal), static_cast<unsigned long long>(empties));
    return 0;
}

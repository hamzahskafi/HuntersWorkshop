// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 21 (docs/gpu-fences.md): the GPU queue's logic (vk_gpu_queue_logic.h) against a model.
//  1. The mark comparison across the count's wrap.
//  2. RunReady: in order, each once, two threads at it (one waiting, one giving way).
//  3. A model GPU and game, using the engine's decisions (Passed, HandOverMark, Decide, RunReady)
//     as the rasterizer does: a fence is never written before the model GPU has finished its game
//     submission; every fence is written, in order; with the GPU as the limit a frame goes as one
//     submission; and the GPU never sits idle at a game submission's end while work waits for it.
#include <cstdio>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "video_core/renderer_vulkan/vk_gpu_queue_logic.h"

using namespace Vulkan::GpuQueueLogic;

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_fail;
    }
}

struct Signal {
    unsigned long long tick;
    std::function<void()> signal;
    u32 mark;
    bool early;
};

// The model: a game recording units (its submissions) of fences, the engine handing them over as the
// rasterizer does, and a GPU running them in order with a switch between submissions.
struct Model {
    // Settings.
    int frames = 200;
    int units = 20;
    int fences_per_unit = 30;
    int cpu_unit_us = 100;       ///< the game's time a unit (GPU-bound: 2 ms a frame)
    int gpu_unit_us = 210;       ///< the GPU's time a unit (4.2 ms a frame)
    int pause_every = 0;         ///< a pause of `pause_us` after every n-th unit (0: none)
    int pause_us = 0;
    int switch_us = 70;          ///< the GPU between submissions
    /// Engine id 141: where the game waits for its previous frame: 0 at its frame's end; n after its
    /// n-th submission of a frame (the engine's BB_SUBMIT_AHEAD=2 has it wait at the third).
    int wait_after_unit = 0;
    bool handover_at_wait = true; ///< the work waiting goes when the game starts that wait
    int announce_us = 0;          ///< the GPU out of work to the held work reaching it
    int game_wake_us = 0;         ///< the frame retired to the game's next submission
    unsigned seed = 1;

    // Results.
    unsigned long long submissions = 0, written = 0, by_mark = 0, by_tick = 0;
    unsigned long long idle_with_work_held_us = 0, early_writes = 0, out_of_order = 0;
    unsigned long long frame_end_submissions = 0, held = 0, wait_submissions = 0;

    void Run() {
        std::mt19937 r{seed};
        struct Unit {
            int gpu_us;
            u32 mark; // 0: none
            unsigned long long index;
        };
        struct Submission {
            unsigned long long tick;
            std::vector<Unit> units;
        };
        std::deque<Submission> gpu_queue;
        std::vector<Unit> recording; // since the last submission
        std::deque<Signal> signals;
        std::mutex list_mutex, run_mutex;
        unsigned long long tick = 1, done_tick = 0, units_done = 0, next_fence = 0, last_written = 0;
        u32 recorded_marks = 0, submitted_marks = 0, handover = 0, word = 0;
        bool watch = false;
        int gpu_left = 0, gpu_switch = 0; // the running unit's time left; a switch in progress
        bool switch_due = false;          // the next submission starts with a switch
        unsigned long long unit_index = 0;
        std::vector<unsigned long long> frame_tick(frames + 1, 0);

        const auto flush = [&] {
            if (recording.empty()) {
                return;
            }
            const u32 before = submitted_marks;
            submitted_marks = recorded_marks;
            handover = HandOverMark(recorded_marks, before);
            gpu_queue.push_back({tick, recording});
            recording.clear();
            ++tick;
            ++submissions;
            watch = false;
        };
        const auto run_ready = [&](bool wait) {
            const u32 seen = word;
            RunReady(
                signals, list_mutex, run_mutex, wait,
                [&](const Signal& s) -> int {
                    if (s.tick <= done_tick) {
                        return ByTick;
                    }
                    return s.early && Passed(seen, s.mark) ? ByMark : NotReady;
                },
                [&](int how) { (how == ByTick ? by_tick : by_mark) += 1; });
        };
        const auto unit_end = [&](bool frame_end) {
            // RecordUnitMark: after work of its own.
            if (!recording.empty()) {
                recording.back().mark = ++recorded_marks;
            }
            run_ready(false); // stage A at the end of a game submission
            if (recording.empty()) {
                return;
            }
            if (frame_end) {
                ++frame_end_submissions;
                flush();
                return;
            }
            switch (Decide(tick < 2, word, handover, done_tick >= tick - 1)) {
            case HandOver::OnLastUnit:
            case HandOver::OutOfWork:
                flush();
                break;
            case HandOver::Hold:
                watch = true;
                ++held;
                break;
            }
        };

        int frame = 0, unit = 0, cpu_left = cpu_unit_us, waiting_for = -1;
        int pause_left = 0;
        unsigned long long announce_at = 0;
        for (unsigned long long now = 0; frame < frames || !gpu_queue.empty() || gpu_left ||
                                         !signals.empty();
             ++now) {
            // The GPU.
            if (gpu_switch > 0) {
                --gpu_switch;
            } else if (gpu_left > 0) {
                if (--gpu_left == 0) {
                    const Unit done = gpu_queue.front().units.front();
                    gpu_queue.front().units.erase(gpu_queue.front().units.begin());
                    ++units_done;
                    if (done.mark) {
                        word = done.mark;
                    }
                    if (gpu_queue.front().units.empty()) {
                        done_tick = gpu_queue.front().tick;
                        gpu_queue.pop_front();
                        switch_due = true;
                        run_ready(true); // the signal thread at the submission's end
                        if (watch && done_tick >= tick - 1 && !recording.empty()) {
                            // Announce, SubmitForGpu: the GPU ran out with work waiting, which
                            // reaches it `announce_us` later (the threads' wake-ups between).
                            announce_at = now + static_cast<unsigned long long>(announce_us);
                        }
                    }
                }
            } else if (!gpu_queue.empty()) {
                if (switch_due) {
                    // The switch where the next submission starts: queued or late, it costs it.
                    switch_due = false;
                    gpu_switch = switch_us;
                } else {
                    gpu_left = gpu_queue.front().units.front().gpu_us;
                }
            } else if (!recording.empty()) {
                ++idle_with_work_held_us; // the GPU idle with work recorded and not given to it
            }
            if (announce_at && now >= announce_at) {
                announce_at = 0;
                if (!recording.empty() && done_tick >= tick - 1) {
                    flush();
                }
            }
            if (now > 100000000ull) {
                check(false, "the model ends");
                break;
            }
            // The game.
            if (frame >= frames) {
                if (!recording.empty() && gpu_queue.empty() && gpu_left == 0) {
                    flush();
                }
                continue;
            }
            if (waiting_for >= 0) {
                if (done_tick >= frame_tick[waiting_for]) {
                    waiting_for = -1;
                    pause_left = game_wake_us; // the retirement reaching the game
                } else {
                    continue;
                }
            }
            if (pause_left > 0) {
                --pause_left;
                continue;
            }
            if (--cpu_left > 0) {
                continue;
            }
            // A unit ends: its fences (all in it, before its end) and then its end.
            const unsigned long long index = unit_index++;
            recording.push_back({gpu_unit_us / 2 + int(r() % unsigned(gpu_unit_us)), 0, index});
            for (int f = 0; f < fences_per_unit; ++f) {
                const unsigned long long id = next_fence++;
                std::scoped_lock lk{list_mutex};
                signals.push_back(
                    {tick, [&, id, index] {
                         // Never before the GPU has finished the unit it is in, and in order.
                         early_writes += index >= units_done;
                         out_of_order += id != last_written;
                         last_written = id + 1;
                         ++written;
                     },
                     recorded_marks + 1, true});
            }
            ++unit;
            const bool frame_end = unit == units;
            unit_end(frame_end);
            cpu_left = cpu_unit_us / 2 + int(r() % unsigned(cpu_unit_us));
            if (pause_every && unit % pause_every == 0) {
                pause_left = pause_us;
            }
            const auto wait_for_previous = [&] {
                waiting_for = frame > 0 ? frame - 1 : -1;
                // Liverpool::GuestWaits: nothing more comes until the GPU retires that frame.
                if (waiting_for >= 0 && done_tick < frame_tick[waiting_for] && handover_at_wait &&
                    !recording.empty()) {
                    ++wait_submissions;
                    flush();
                }
            };
            if (wait_after_unit != 0 && unit == wait_after_unit) {
                wait_for_previous();
            }
            if (frame_end) {
                frame_tick[frame] = tick - 1;
                if (wait_after_unit == 0) {
                    wait_for_previous();
                }
                ++frame;
                unit = 0;
            }
        }
    }
};
} // namespace

int main() {
    // 1. The comparison across the wrap.
    check(Passed(5, 5) && Passed(6, 5) && !Passed(4, 5), "passed: equal and after, not before");
    check(Passed(2, 0xFFFFFFF0u) && !Passed(0xFFFFFFF0u, 2), "passed across the wrap");
    check(Passed(0, 0xFFFFFFFFu), "nothing given yet: hand over");
    check(HandOverMark(10, 8) == 9 && HandOverMark(10, 10) == 10, "hand-over mark");
    check(Decide(true, 0, 99, false) == HandOver::OnLastUnit &&
              Decide(false, 9, 9, false) == HandOver::OnLastUnit &&
              Decide(false, 8, 9, true) == HandOver::OutOfWork &&
              Decide(false, 8, 9, false) == HandOver::Hold,
          "the hand-over decision");

    // 2. Two threads at RunReady: in order, each once.
    {
        std::deque<Signal> queue;
        std::mutex list_mutex, run_mutex;
        std::vector<int> order;
        constexpr int n = 200000;
        std::atomic<int> ready_upto{0};
        std::atomic<bool> stop{false};
        std::thread producer{[&] {
            for (int i = 0; i < n; ++i) {
                std::scoped_lock lk{list_mutex};
                queue.push_back({static_cast<unsigned long long>(i), [&order, i] {
                                     order.push_back(i);
                                 },
                                 0, false});
                if (i % 7 == 0) {
                    ready_upto.store(i, std::memory_order_release);
                }
            }
            ready_upto.store(n, std::memory_order_release);
        }};
        const auto runner = [&](bool wait) {
            while (!stop.load(std::memory_order_acquire)) {
                const int upto = ready_upto.load(std::memory_order_acquire);
                RunReady(
                    queue, list_mutex, run_mutex, wait,
                    [&](const Signal& s) { return s.tick < static_cast<unsigned long long>(upto)
                                                      ? ByTick
                                                      : NotReady; },
                    [](int) {});
                std::scoped_lock lk{list_mutex};
                if (upto == n && queue.empty()) {
                    break;
                }
            }
        };
        std::thread a{runner, true}, b{runner, false};
        producer.join();
        a.join();
        b.join();
        stop = true;
        bool in_order = order.size() == static_cast<std::size_t>(n);
        for (int i = 0; in_order && i < n; ++i) {
            in_order = order[i] == i;
        }
        check(in_order, "two runners: every signal once, in order");
    }

    // 3. The model.
    struct Case {
        const char* name;
        int cpu_unit_us, gpu_unit_us, pause_every, pause_us;
        int wait_after_unit = 0;
        bool handover_at_wait = true;
        int announce_us = 0;
        int game_wake_us = 0;
    };
    const Case cases[] = {
        {"GPU the limit", 100, 210, 0, 0},
        {"GPU the limit, the game pausing", 100, 210, 5, 600},
        {"the game the limit", 300, 150, 0, 0},
        {"the game the limit, pausing", 250, 150, 4, 900},
        // Engine id 141: the game as it runs (its wait after the third submission), the work
        // reaching an idle GPU 100 us after it ran out; engine id 140 had no hand-over at the wait.
        {"the game's pacing, no hand-over at its wait (140)", 50, 210, 0, 0, 3, false, 100, 150},
        {"the game's pacing, the hand-over at its wait", 50, 210, 0, 0, 3, true, 100, 150},
        {"the game's pacing, the game pausing", 100, 210, 5, 300, 3, true, 100, 150},
    };
    for (const auto& c : cases) {
        Model m;
        m.cpu_unit_us = c.cpu_unit_us;
        m.gpu_unit_us = c.gpu_unit_us;
        m.pause_every = c.pause_every;
        m.pause_us = c.pause_us;
        m.wait_after_unit = c.wait_after_unit;
        m.handover_at_wait = c.handover_at_wait;
        m.announce_us = c.announce_us;
        m.game_wake_us = c.game_wake_us;
        m.Run();
        const double frames = m.frames;
        std::printf("  %s: %.2f submissions a frame (%.2f at the frame's end, %.2f at the game's "
                    "wait), the GPU idle with work held %.1f us a frame; fences written as the GPU "
                    "passed their unit %.1f%%, at their submission's end %.1f%%\n",
                    c.name, m.submissions / frames, m.frame_end_submissions / frames,
                    m.wait_submissions / frames,
                    m.idle_with_work_held_us / frames, 100.0 * m.by_mark / double(m.written),
                    100.0 * m.by_tick / double(m.written));
        check(m.written == static_cast<unsigned long long>(m.frames * m.units * m.fences_per_unit),
              "every fence is written");
        check(m.early_writes == 0, "no fence before the GPU has finished its unit");
        check(m.out_of_order == 0, "fences written in order");
        if (c.cpu_unit_us * 2 <= c.gpu_unit_us && c.pause_every == 0 && c.wait_after_unit == 0) {
            check(m.submissions / frames < 1.3, "with the GPU as the limit, about one submission a frame");
        }
        if (c.wait_after_unit != 0 && !c.handover_at_wait) {
            check(m.idle_with_work_held_us / frames > 50,
                  "without the hand-over at its wait, the GPU waits for held work each frame");
        }
        // With the game pausing mid-frame for longer than the GPU's remaining work, the work held
        // reaches the GPU when it has run out (the announcement): that wait stays, and is counted.
        if (c.handover_at_wait && c.announce_us != 0 && c.pause_every == 0) {
            check(m.idle_with_work_held_us / frames < 5,
                  "with the hand-over at the game's wait, the GPU never waits for held work");
        }
    }
    if (g_fail) {
        std::printf("gpu-queue-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("gpu-queue-test: ok\n");
    return 0;
}

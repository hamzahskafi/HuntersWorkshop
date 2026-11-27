// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 19 (vk_draw_pipe.h, docs/stage-a-cost.md item 2): the draw pipe's hand-over with the
// fence every 8 packets, with real threads. Stage A commits packets in bursts with pauses long
// enough for stage B to go to sleep between them. Checked: every packet runs, in order; a burst
// ended by Publish (as stage A does before it goes idle or waits) runs at once, with no late wake;
// a burst left without one still runs, within the sleep's check interval, and counts as a late
// wake; Drain and WaitForPacket return with everything before them run.
#include "video_core/renderer_vulkan/vk_draw_pipe.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <thread>

namespace Common {
void SetCurrentThreadName(const char*) {}
} // namespace Common

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

using Clock = std::chrono::steady_clock;
struct Payload {
    u64 number;
    Clock::rep committed;
};
std::atomic<u64> g_run{0};
std::atomic<u64> g_out_of_order{0};
std::atomic<long long> g_worst_us{0};

void Handler(void*, const u8* packet, u32 size) {
    if (size < sizeof(Payload)) {
        return;
    }
    Payload p;
    std::memcpy(&p, packet, sizeof p);
    if (p.number != g_run.load(std::memory_order_relaxed) + 1) {
        g_out_of_order.fetch_add(1, std::memory_order_relaxed);
    }
    g_run.store(p.number, std::memory_order_release);
    const long long us = std::chrono::duration_cast<std::chrono::microseconds>(
                             Clock::now() - Clock::time_point(Clock::duration(p.committed)))
                             .count();
    long long worst = g_worst_us.load(std::memory_order_relaxed);
    while (us > worst && !g_worst_us.compare_exchange_weak(worst, us)) {
    }
}

u64 g_next = 0;
void Send(Vulkan::DrawPipe& pipe) {
    u8* out = pipe.Begin(sizeof(Payload));
    const Payload p{++g_next, Clock::now().time_since_epoch().count()};
    std::memcpy(out, &p, sizeof p);
    pipe.Commit(sizeof p);
}

bool WaitRun(u64 number, int ms) {
    const auto until = Clock::now() + std::chrono::milliseconds(ms);
    while (g_run.load(std::memory_order_acquire) < number) {
        if (Clock::now() > until) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    return true;
}

} // namespace

int main() {
    check(Vulkan::DrawPipe::BatchedHandover(), "the hand-over is batched by default");
    Vulkan::DrawPipe pipe(&Handler, nullptr);
    std::mt19937 rng{12345};

    // Bursts ended by Publish, with pauses past stage B's spin (it sleeps between them).
    for (int burst = 0; burst < 200; ++burst) {
        const int n = 1 + static_cast<int>(rng() % 20);
        g_worst_us = 0;
        for (int i = 0; i < n; ++i) {
            Send(pipe);
        }
        pipe.Publish();
        if (!WaitRun(g_next, 1000)) {
            check(false, "a published burst runs");
            break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(300 + rng() % 700));
    }
    check(pipe.late_wakes.load() == 0, "published bursts need no late wake");
    check(g_out_of_order.load() == 0, "packets run in order");

    // A burst stage A leaves without Publish while stage B sleeps: run by the sleep's own check.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const u64 before = g_next;
    g_worst_us = 0;
    Send(pipe);
    Send(pipe);
    Send(pipe);
    check(WaitRun(before + 3, 100), "an unpublished burst still runs");
    std::printf("  an unpublished burst after stage B slept: run within %lld us, late wakes %llu\n",
                static_cast<long long>(g_worst_us.load()),
                static_cast<unsigned long long>(pipe.late_wakes.load()));
    check(g_worst_us.load() < 20000, "an unpublished burst runs within the sleep's check interval");

    // Drains and waits for a packet, back to back with commits.
    for (int round = 0; round < 2000; ++round) {
        const int n = 1 + static_cast<int>(rng() % 12);
        for (int i = 0; i < n; ++i) {
            Send(pipe);
        }
        if (round & 1) {
            pipe.Drain();
            if (g_run.load(std::memory_order_acquire) != g_next) {
                check(false, "Drain returns with every packet run");
                break;
            }
        } else {
            const u64 number = pipe.NextPacket() - 1;
            pipe.WaitForPacket(number, Vulkan::DrawPipe::ReasonConstRam);
            if (!pipe.ReachedPacket(number)) {
                check(false, "WaitForPacket returns with its packet run");
                break;
            }
        }
        if (round % 97 == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(400));
        }
    }
    check(g_out_of_order.load() == 0, "packets run in order under drains");
    pipe.Drain();
    if (g_fail) {
        std::printf("draw-pipe-handover-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("draw-pipe-handover-test: ok (%llu packets, late wakes %llu)\n",
                static_cast<unsigned long long>(g_next),
                static_cast<unsigned long long>(pipe.late_wakes.load()));
    return 0;
}

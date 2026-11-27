// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 15 part 1 (docs/memory-model.md): compile bursts. The burst threads' count from the
// processor's layout (shim/bbport_cpu_layout.h), and the job queue's backlog rule, raise-on-wait
// and burst accounting (pipeline_compile_queue.h), with real threads. No Vulkan, no system calls.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <thread>
#include <vector>

#include "bbport_cpu_layout.h"
#include "video_core/renderer_vulkan/pipeline_compile_queue.h"

using namespace std::chrono_literals;
using Vulkan::PipelineJobQueue;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

// `cores` cores of `threads` logical processors each, efficiency class `cls`, ids from `first`.
void Add(BbCpu::Layout& l, unsigned cores, unsigned threads, std::uint8_t cls, unsigned& next_core) {
    for (unsigned c = 0; c < cores; ++c, ++next_core) {
        for (unsigned t = 0; t < threads; ++t) {
            l.logical.push_back({static_cast<std::uint32_t>(256 + l.logical.size()), next_core, cls, 0});
        }
    }
    l.known = true;
}

void Layouts() {
    using namespace BbCpu;
    unsigned core = 0;
    // i5-14600KF: 6 performance cores of 2 threads, 8 efficiency cores of 1.
    Layout kf;
    Add(kf, 6, 2, 1, core);
    Add(kf, 8, 1, 0, core);
    const Summary s = Summarize(kf);
    check(s.known && s.hybrid, "a hybrid CPU is seen as one");
    check(s.logical == 20 && s.cores == 14, "20 logical, 14 cores");
    check(s.performance_cores == 6 && s.performance_logical == 12 && s.efficiency_cores == 8, "6 + 8");
    check(s.efficiency_sets.size() == 8 && s.efficiency_sets.front() == 256 + 12, "the efficiency cores' sets");
    BurstPlan p = PlanBursts(s, 4, -1, true, "");
    check(p.threads == 8 && p.on_efficiency, "8 burst threads on the efficiency cores");
    check(Describe(s, 4, p) ==
              "CPU: 14 cores, 20 logical: 6 performance (2 threads each), 8 efficiency; compile threads 4, "
              "burst threads 8 on the efficiency cores, idle priority",
          "the start line");
    // 8 performance, 12 efficiency: at most 8.
    Layout k7;
    core = 0;
    Add(k7, 8, 2, 1, core);
    Add(k7, 12, 1, 0, core);
    check(PlanBursts(Summarize(k7), 4, -1, true, "").threads == 8, "i7-14700K: at most 8");
    // 6 performance, 4 efficiency (i5-12600K): one per efficiency core.
    Layout k12;
    core = 0;
    Add(k12, 6, 2, 1, core);
    Add(k12, 4, 1, 0, core);
    const BurstPlan p12 = PlanBursts(Summarize(k12), 4, -1, true, "");
    check(p12.threads == 4 && p12.on_efficiency, "i5-12600K: 4, on the efficiency cores");

    // Ryzens: physical cores less the standing threads.
    const unsigned rows[][3] = {{4, 2, 2}, {6, 4, 2}, {8, 4, 4}, {16, 4, 8}, {32, 4, 8}};
    for (const auto& r : rows) {
        Layout l;
        core = 0;
        Add(l, r[0], 2, 0, core);
        const Summary sr = Summarize(l);
        const BurstPlan pr = PlanBursts(sr, r[1], -1, true, "");
        char what[64];
        std::snprintf(what, sizeof what, "%u cores x 2: %u burst threads", r[0], r[2]);
        check(!sr.hybrid && sr.cores == r[0] && pr.threads == r[2] && !pr.on_efficiency, what);
    }
    Layout eight;
    core = 0;
    Add(eight, 8, 2, 0, core);
    check(Describe(Summarize(eight), 4, PlanBursts(Summarize(eight), 4, -1, true, "")) ==
              "CPU: 8 cores, 16 logical (2 threads each); compile threads 4, burst threads 4, idle priority",
          "a Ryzen's line");

    // Where an idle thread cannot be raised again: none, unless forced.
    p = PlanBursts(s, 4, -1, false, "under Wine");
    check(p.threads == 0 && p.why == "under Wine; BB_PIPELINE_BURST=N forces them", "none under Wine");
    p = PlanBursts(s, 4, 3, false, "under Wine");
    check(p.threads == 3 && p.on_efficiency && p.why == "BB_PIPELINE_BURST", "forced under Wine");
    p = PlanBursts(s, 4, 0, true, "");
    check(p.threads == 0 && Describe(s, 4, p).ends_with("burst threads none (BB_PIPELINE_BURST=0)"),
          "BB_PIPELINE_BURST=0: the standing threads only");
    check(PlanBursts(s, 4, 99, true, "").threads == kBurstForcedMax, "forced: at most 16");

    // Not read: half the logical processors taken as cores.
    Layout unknown;
    unknown.count = 12;
    const Summary su = Summarize(unknown);
    check(!su.known && su.cores == 6 && PlanBursts(su, 4, -1, true, "").threads == 2, "layout not read");
    check(Describe(su, 4, PlanBursts(su, 4, -1, true, "")).starts_with("CPU: 12 logical processors (layout not read)"),
          "its line");
    // A 4-core CPU with as many standing threads: none.
    Layout four;
    core = 0;
    Add(four, 4, 1, 0, core);
    p = PlanBursts(Summarize(four), 4, -1, true, "");
    check(p.threads == 0 && p.why == "no core beyond the standing compile threads", "no core to spare");
}

std::uint64_t Now() {
    return PipelineJobQueue::SteadyNs();
}

std::atomic<int> g_begins{0}, g_ends{0};
void Edge(PipelineJobQueue::Burst* ended) {
    if (!ended) {
        ++g_begins;
    } else {
        ++g_ends;
        ended->frames = 7; // the caller's own figures go into the burst it returns
    }
}

// Burst threads take nothing while the standing threads asleep would take every job.
void Backlog() {
    PipelineJobQueue q;
    q.edge = &Edge;
    std::atomic<int> standing_ran{0}, burst_ran{0};
    std::atomic<bool> stop{false};
    std::optional<PipelineJobQueue::Burst> ended;
    std::mutex ended_mutex;
    // Two standing threads, each job held until released; one burst thread.
    std::atomic<int> hold{1};
    std::vector<std::thread> standing;
    for (int i = 0; i < 2; ++i) {
        standing.emplace_back([&] {
            for (;;) {
                auto job = q.Pop();
                job();
                if (stop) {
                    return;
                }
                ++standing_ran;
                if (auto b = q.Done(-1, 1000, Now())) {
                    std::scoped_lock lk{ended_mutex};
                    ended = b;
                }
            }
        });
    }
    std::thread burst([&] {
        for (;;) {
            bool started = false;
            auto job = q.PopBurst(0, &started);
            job();
            if (stop) {
                return;
            }
            ++burst_ran;
            if (auto b = q.Done(0, 2000, Now())) {
                std::scoped_lock lk{ended_mutex};
                ended = b;
            }
        }
    });
    std::this_thread::sleep_for(30ms); // all three asleep
    int a = 0, b = 0;
    q.Push(&a, [&] { while (hold) std::this_thread::sleep_for(1ms); });
    q.Push(&b, [&] { while (hold) std::this_thread::sleep_for(1ms); });
    std::this_thread::sleep_for(50ms);
    check(burst_ran == 0 && q.Size() == 0, "two jobs, two standing threads asleep: no burst");
    // Both standing threads busy: the next jobs are a backlog.
    int c = 0, d = 0;
    std::atomic<bool> c_running{false};
    std::atomic<int> release_c{0};
    q.Push(&c, [&] {
        c_running = true;
        while (!release_c) std::this_thread::sleep_for(1ms);
    });
    q.Push(&d, [] {});
    std::this_thread::sleep_for(50ms);
    check(c_running, "a backlog: the burst thread takes the oldest job");
    // A thread waits: the burst thread running c is raised once, to the waiter's priority.
    int raises = 0, to = 0;
    check(q.RaiseRunning(2, [&](unsigned i, int p) { raises += i == 0; to = p; }) == 1,
          "the burst thread running a job is raised");
    check(q.RaiseRunning(2, [&](unsigned, int) { ++raises; }) == 0 && raises == 1 && to == 2, "once");
    check(q.RaisedTo(0) == std::optional<int>{2}, "a thread lowered after the raise sets it again");
    check(!q.RaisedTo(1), "a burst thread with no job: not raised");
    check(!q.Take(&c), "a started job cannot be taken");
    release_c = 1;
    hold = 0;
    std::this_thread::sleep_for(100ms);
    {
        std::scoped_lock lk{ended_mutex};
        check(ended.has_value(), "the burst ends with the queue empty");
        if (ended) {
            check(ended->burst_jobs >= 1 && ended->burst_threads == 1 && ended->raised == 1,
                  "its burst thread, its job and the raise counted");
            check(ended->jobs >= 2 && ended->wall_ns > 0, "the jobs during it and its time");
            check(g_begins == 1 && g_ends == 1 && ended->frames == 7, "its edges, in order, under the lock");
        }
    }
    check(burst_ran >= 1 && standing_ran + burst_ran == 4, "every job ran once");
    stop = true;
    for (int i = 0; i < 3; ++i) {
        q.Push(nullptr, [] {});
    }
    for (int i = 0; i < 3; ++i) {
        q.Push(nullptr, [] {}); // the burst thread's backlog
    }
    for (auto& t : standing) {
        t.join();
    }
    burst.join();
}

// A burst ends only once the queue is empty and no burst thread runs a job: a burst thread done with
// its job while others still wait goes on to them within the same burst.
void OneBurst() {
    PipelineJobQueue q;
    q.edge = &Edge;
    g_begins = g_ends = 0;
    std::atomic<int> release_a{0}, release_b{0}, done{0};
    std::optional<PipelineJobQueue::Burst> ended;
    std::mutex ended_mutex;
    std::atomic<bool> stop{false};
    const auto note = [&](std::optional<PipelineJobQueue::Burst> b) {
        if (b) {
            std::scoped_lock lk{ended_mutex};
            ended = b;
        }
    };
    std::thread standing([&] {
        while (!stop) {
            auto job = q.Pop();
            job();
            note(q.Done(-1, 1, Now()));
        }
    });
    std::thread burst([&] {
        while (!stop) {
            auto job = q.PopBurst(0);
            job();
            note(q.Done(0, 1, Now()));
        }
    });
    std::this_thread::sleep_for(30ms);
    int a = 0, b = 0, c = 0, d = 0;
    q.Push(&a, [&] { while (!release_a) std::this_thread::sleep_for(1ms); ++done; });
    std::this_thread::sleep_for(30ms); // the standing thread holds a
    q.Push(&b, [&] { while (!release_b) std::this_thread::sleep_for(1ms); ++done; });
    q.Push(&c, [&] { ++done; });
    q.Push(&d, [&] { ++done; });
    std::this_thread::sleep_for(30ms); // the burst thread holds b; c and d wait
    check(g_begins == 1 && g_ends == 0, "one burst begun");
    release_b = 1;
    std::this_thread::sleep_for(60ms);
    check(done == 3 && q.Size() == 0, "the burst thread went on to c and d");
    check(g_begins == 1 && g_ends == 1, "within the same burst, ended once the queue was empty");
    {
        std::scoped_lock lk{ended_mutex};
        check(ended && ended->burst_jobs == 3 && ended->jobs == 3 && ended->burst_threads == 1,
              "the burst's three jobs, all the burst thread's");
    }
    release_a = 1;
    stop = true;
    for (int i = 0; i < 4; ++i) {
        q.Push(nullptr, [] {});
    }
    standing.join();
    burst.join();
}

// A burst's wall time starts at its first job, not when its burst thread went to sleep waiting for
// one (beta 15's first runs: 6 short bursts reported as 52 s).
void IdleBeforeBurst() {
    PipelineJobQueue q;
    std::atomic<int> release_a{0}, a_running{0}, c_pushed{0};
    std::optional<PipelineJobQueue::Burst> ended;
    std::mutex ended_mutex;
    std::atomic<bool> stop{false};
    std::thread standing([&] {
        while (!stop) {
            auto job = q.Pop();
            job();
            q.Done(-1, 1, Now());
        }
    });
    // Job a on the standing thread before the burst thread starts (beta 16: with the burst thread
    // asleep first, a was a backlog of its own and went to it; no burst ended in the window).
    int a = 0, b = 0, c = 0;
    q.Push(&a, [&] {
        a_running = 1;
        while (!release_a) std::this_thread::sleep_for(1ms);
    });
    while (!a_running) std::this_thread::sleep_for(1ms);
    std::thread burst([&] {
        while (!stop) {
            auto job = q.PopBurst(0);
            job();
            if (auto b = q.Done(0, 1, Now())) {
                std::scoped_lock lk{ended_mutex};
                ended = b;
            }
        }
    });
    std::this_thread::sleep_for(300ms); // the burst thread asleep all this while
    // b holds the burst open until c is queued: one burst of both, whichever thread is quicker.
    q.Push(&b, [&] { while (!c_pushed) std::this_thread::sleep_for(1ms); });
    q.Push(&c, [] {});
    c_pushed = 1;
    std::this_thread::sleep_for(50ms);
    {
        std::scoped_lock lk{ended_mutex};
        check(ended && ended->burst_jobs == 2, "the burst's two jobs");
        check(ended && ended->wall_ns < 150'000'000ull, "its wall time without the burst thread's sleep before it");
    }
    release_a = 1;
    stop = true;
    for (int i = 0; i < 4; ++i) {
        q.Push(nullptr, [] {});
    }
    standing.join();
    burst.join();
}

// Many jobs at once, many threads: each job runs exactly once and the bursts' counts add up.
void Stress() {
    PipelineJobQueue q;
    constexpr int Jobs = 3000, Standing = 2, Bursts = 4;
    std::vector<int> owners(Jobs);
    std::vector<std::atomic<int>> runs(Jobs);
    std::atomic<int> done{0};
    std::atomic<std::uint32_t> counted_jobs{0};
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;
    const auto note = [&](std::optional<PipelineJobQueue::Burst> b) {
        if (b) {
            counted_jobs += b->jobs;
        }
    };
    for (int i = 0; i < Standing; ++i) {
        threads.emplace_back([&] {
            while (!stop) {
                auto job = q.Pop();
                job();
                note(q.Done(-1, 1, Now()));
            }
        });
    }
    for (int i = 0; i < Bursts; ++i) {
        threads.emplace_back([&, i] {
            while (!stop) {
                auto job = q.PopBurst(static_cast<unsigned>(i));
                (void)q.RaisedTo(static_cast<unsigned>(i));
                job();
                note(q.Done(i, 1, Now()));
            }
        });
    }
    for (int i = 0; i < Jobs; ++i) {
        q.Push(&owners[i], [&, i] {
            runs[i].fetch_add(1);
            ++done;
        });
    }
    // Takers and raisers at once.
    for (int i = Jobs - 1; i >= 0; i -= 7) {
        if (auto job = q.Take(&owners[i])) {
            job();
            note(q.Done(-1, 1, Now()));
        } else {
            q.RaiseRunning(1, [](unsigned, int) {});
        }
    }
    for (int spins = 0; done < Jobs && spins < 5000; ++spins) {
        std::this_thread::sleep_for(1ms);
    }
    bool once = true;
    for (auto& r : runs) {
        once &= r.load() == 1;
    }
    check(done == Jobs && once, "3000 jobs, 6 threads and takers: each ran once");
    check(counted_jobs <= static_cast<std::uint32_t>(Jobs), "bursts count no job twice");
    stop = true;
    for (int i = 0; i < Standing + Bursts * 2 + 8; ++i) {
        q.Push(nullptr, [] {});
    }
    for (auto& t : threads) {
        t.join();
    }
}

} // namespace

// 0.4.6 beta 20 (docs/chain-waits.md, item 4): a standing thread with a slot running a job is
// raised for a waiter when asked (standing_too), once, and told so when its job is done; a standing
// thread between jobs, or without a slot, is not.
void StandingRaise() {
    Vulkan::PipelineJobQueue q;
    std::atomic<int> release{0};
    std::atomic<bool> running{false}, done_raised{false}, second_done{false};
    int owner = 0;
    q.Push(&owner, [&] {
        running = true;
        while (!release) std::this_thread::sleep_for(1ms);
    });
    std::thread standing([&] {
        auto job = q.Pop(3);
        job();
        done_raised = q.StandingDone(3);
        auto next = q.Pop(3); // a quick one: done before anyone asks
        next();
        second_done = !q.StandingDone(3);
    });
    while (!running) std::this_thread::sleep_for(1ms);
    unsigned seen = ~0u;
    check(q.RaiseRunning(1, [&](unsigned i, int) { seen = i; }) == 0,
          "standing threads are raised only when asked");
    check(q.RaiseRunning(1, [&](unsigned i, int) { seen = i; }, true) == 1 &&
              seen == Vulkan::PipelineJobQueue::kMaxBurstThreads + 3,
          "a standing thread running a job is raised, numbered after the burst threads");
    check(q.RaiseRunning(1, [&](unsigned, int) {}, true) == 0, "once");
    release = 1;
    q.Push(nullptr, [] {});
    standing.join();
    check(done_raised, "its job done, it is told it was raised");
    check(second_done, "the next job, not raised, is not");
    check(q.RaiseRunning(1, [&](unsigned, int) {}, true) == 0, "between jobs: none raised");
    check(!q.StandingDone(Vulkan::PipelineJobQueue::kNoSlot), "a thread without a slot is never raised");
}

int main() {
    Layouts();
    Backlog();
    OneBurst();
    IdleBeforeBurst();
    Stress();
    StandingRaise();
    if (g_fail) {
        std::printf("pipeline-burst-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("pipeline-burst-test: ok\n");
    return 0;
}

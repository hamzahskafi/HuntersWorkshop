// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: pipelines first seen during play (docs/pipeline_compiles.md). The compile threads'
// job queue, from which a thread that needs a pipeline right away takes its job, the rule for
// how long draws of a pipeline still compiling may be left out, and the statistics of both
// (BB_FRAME_STATS "Pipelines:" line). No Vulkan here: tests/test_pipeline_compile.cpp.

#pragma once

#include <array>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace Vulkan {

/// Compile jobs, first in, first out (the order the draws need them in), each with the object
/// it creates (`owner`). A thread about to wait for that object takes its job when no compile
/// thread has started it yet (Take) and runs it itself, instead of waiting behind the jobs
/// queued before it (20-90 pipelines at once entering an area).
///
/// 0.4.6 beta 15 part 1: burst threads (docs/memory-model.md, beta 15 part 1). They take a job only
/// while more jobs wait than the standing threads sleeping in Pop will take (a backlog), and run it
/// at idle priority; their turns at this queue's lock are at normal priority, so none is preempted
/// holding it. While a thread waits for a pipeline (or compiles one it took), every burst thread
/// running a job is raised to that thread's priority until its job is done (RaiseRunning): the
/// system does not know the waiter depends on them, directly or through the driver's own locks. A
/// burst lasts from a burst thread's first job until the queue is empty and no burst thread runs one.
class PipelineJobQueue {
public:
    using Job = std::function<void()>;
    static constexpr unsigned kMaxBurstThreads = 16;

    /// One burst, as BurstLine prints it.
    struct Burst {
        std::uint64_t wall_ns = 0;     ///< its first burst job taken to its end
        std::uint64_t compile_ns = 0;  ///< the jobs' own time, summed over every thread
        std::uint32_t jobs = 0;        ///< jobs run during it (standing, burst and waiting threads)
        std::uint32_t burst_jobs = 0;  ///< of which by burst threads
        std::uint32_t taken = 0;       ///< of which by a thread that waited for them (Take)
        std::uint32_t burst_threads = 0; ///< burst threads that ran any
        std::uint32_t raised = 0;      ///< burst threads raised for a waiting thread (each time)
        std::uint32_t frames = 0, frames_over_25 = 0; ///< flips during it (Edge fills them in)
        double worst_ms = 0;
    };
    /// Called under the queue's lock when a burst begins (`ended` null) and when it ends (`ended`:
    /// the burst, for the caller's own figures): the two stay in order whatever the threads do.
    using Edge = void (*)(Burst* ended);
    Edge edge = nullptr;

    void Push(const void* owner, Job job) {
        bool backlog;
        {
            std::scoped_lock lk{mutex};
            jobs.push_back({owner, std::move(job)});
            backlog = idle_burst != 0 && Backlog();
        }
        cv.notify_one();
        if (backlog) {
            burst_cv.notify_one();
        }
    }

    /// A standing compile thread: the oldest job (waits for one).
    Job Pop() {
        std::unique_lock lk{mutex};
        ++idle_standing;
        cv.wait(lk, [this] { return !jobs.empty(); });
        --idle_standing;
        Job job = std::move(jobs.front().job);
        jobs.pop_front();
        if (burst_on) {
            ++burst.jobs;
        }
        // One fewer standing thread asleep: the jobs left may now be a backlog.
        const bool backlog = idle_burst != 0 && Backlog();
        lk.unlock();
        if (backlog) {
            burst_cv.notify_one();
        }
        return job;
    }

    /// The steady clock in ns (Done's `now_ns` is read from it too).
    static std::uint64_t SteadyNs() {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              std::chrono::steady_clock::now().time_since_epoch())
                                              .count());
    }

    /// Burst thread `index` (below kMaxBurstThreads): the oldest job once there is a backlog
    /// (waits for one). `started`: this job began a burst, whose wall time starts now, after the
    /// wait (0.4.6 beta 15's first runs counted the wait in it: 6 bursts of 52 s).
    Job PopBurst(unsigned index, bool* started = nullptr) {
        std::unique_lock lk{mutex};
        ++idle_burst;
        burst_cv.wait(lk, [this] { return Backlog(); });
        --idle_burst;
        Entry e = std::move(jobs.front());
        jobs.pop_front();
        if (!burst_on) {
            burst_on = true;
            burst = {};
            burst_start_ns = SteadyNs();
            burst_threads_used = 0;
            if (started) {
                *started = true;
            }
            if (edge) {
                edge(nullptr);
            }
        }
        ++burst.jobs;
        ++burst.burst_jobs;
        burst_threads_used |= 1u << index;
        ++running_burst;
        slots[index].owner = e.owner;
        slots[index].raised.store(false, std::memory_order_relaxed);
        return std::move(e.job);
    }

    /// A job ended after `ns` (its own time): on burst thread `burst_index` (back at normal priority
    /// by now), or on a standing or a waiting thread (-1). Returns the burst when this was its last
    /// job.
    std::optional<Burst> Done(int burst_index, std::uint64_t ns, std::uint64_t now_ns) {
        std::scoped_lock lk{mutex};
        if (burst_index >= 0) {
            slots[burst_index].owner = nullptr;
            slots[burst_index].raised.store(false, std::memory_order_relaxed);
            --running_burst;
        }
        if (!burst_on) {
            return std::nullopt;
        }
        burst.compile_ns += ns;
        if (!jobs.empty() || running_burst != 0) {
            return std::nullopt;
        }
        burst_on = false;
        burst.wall_ns = now_ns - burst_start_ns;
        burst.burst_threads = static_cast<std::uint32_t>(std::popcount(burst_threads_used));
        if (edge) {
            edge(&burst);
        }
        return burst;
    }

    /// A thread about to wait for a pipeline or compile one itself: each burst thread running a job
    /// and not raised yet is raised to `priority` by `raise(index, priority)` (under the queue's
    /// lock, so it cannot be between jobs). Returns how many were raised now.
    template <typename Raise>
    unsigned RaiseRunning(int priority, Raise&& raise) {
        std::scoped_lock lk{mutex};
        unsigned count = 0;
        for (unsigned i = 0; i < kMaxBurstThreads; ++i) {
            Slot& slot = slots[i];
            if (slot.owner == nullptr || slot.raised.load(std::memory_order_relaxed)) {
                continue;
            }
            slot.priority.store(priority, std::memory_order_relaxed);
            slot.raised.store(true, std::memory_order_seq_cst);
            raise(i, priority);
            ++count;
        }
        if (burst_on) {
            burst.raised += count;
        }
        return count;
    }

    /// Burst thread `index`, just lowered to idle priority for its job: the priority a waiting
    /// thread raised it to meanwhile (between PopBurst and the lowering), to set again; else none.
    [[nodiscard]] std::optional<int> RaisedTo(unsigned index) const {
        const Slot& slot = slots[index];
        if (!slot.raised.load(std::memory_order_seq_cst)) {
            return std::nullopt;
        }
        return slot.priority.load(std::memory_order_relaxed);
    }

    /// `owner`'s job, taken out of the queue, when no thread has started it; else empty.
    Job Take(const void* owner) {
        std::scoped_lock lk{mutex};
        const auto it =
            std::find_if(jobs.begin(), jobs.end(), [owner](const Entry& e) { return e.owner == owner; });
        if (it == jobs.end()) {
            return {};
        }
        Job job = std::move(it->job);
        jobs.erase(it);
        if (burst_on) {
            ++burst.jobs;
            ++burst.taken;
        }
        return job;
    }

    [[nodiscard]] std::size_t Size() {
        std::scoped_lock lk{mutex};
        return jobs.size();
    }

private:
    struct Entry {
        const void* owner;
        Job job;
    };
    struct Slot {
        const void* owner = nullptr;       ///< the job a burst thread runs (under the lock)
        std::atomic<bool> raised{false};   ///< raised for a waiting thread (RaisedTo reads it unlocked)
        std::atomic<int> priority{0};      ///< to what
    };
    /// More jobs wait than the standing threads asleep in Pop will take.
    [[nodiscard]] bool Backlog() const { return jobs.size() > idle_standing; }

    std::mutex mutex;
    std::condition_variable cv, burst_cv;
    std::deque<Entry> jobs;
    std::size_t idle_standing = 0, idle_burst = 0, running_burst = 0;
    Slot slots[kMaxBurstThreads];
    bool burst_on = false;
    Burst burst{};
    std::uint64_t burst_start_ns = 0;
    std::uint32_t burst_threads_used = 0;
};

/// Whether a draw of a pipeline still compiling may be left out now. `first_skip_ns`: when one
/// of its draws was first left out (0: none yet; set here), `budget_ns`: for how long its draws
/// may be left out. Past that they wait for it: a compile that never ends (or a queue that does
/// not move) shows as a stall, not as geometry missing for good.
inline bool WithinSkipBudget(std::uint64_t& first_skip_ns, std::uint64_t now_ns,
                             std::uint64_t budget_ns) noexcept {
    if (first_skip_ns == 0) {
        first_skip_ns = now_ns != 0 ? now_ns : 1;
        return budget_ns != 0;
    }
    return now_ns - first_skip_ns < budget_ns;
}

/// Counted since the last statistics window (BB_FRAME_STATS), exchanged to 0 when printed.
struct PipelineCompileStats {
    std::atomic<std::uint64_t> background{0};        ///< pipelines handed to the compile threads
    std::atomic<std::uint64_t> taken{0};             ///< compiled by the thread that needed them
    std::atomic<std::uint64_t> skipped_draws{0};     ///< draws left out, their pipeline compiling
    std::atomic<std::uint64_t> skipped_pipelines{0}; ///< pipelines whose draws were left out
    std::atomic<std::uint64_t> late_binds{0};        ///< draws bound on the recording threads
    std::atomic<std::uint64_t> waits{0};             ///< threads that waited for a compile
    std::atomic<std::uint64_t> wait_ns{0};
    std::atomic<std::uint64_t> longest_wait_ns{0};
    /// 0.4.6 beta 15 part 5: since start (never exchanged), and the longest since the load line's
    /// last look (it exchanges it).
    std::atomic<std::uint64_t> waits_total{0}, wait_ns_total{0}, longest_wait_mark_ns{0};

    static void Max(std::atomic<std::uint64_t>& at, std::uint64_t ns) noexcept {
        std::uint64_t longest = at.load(std::memory_order_relaxed);
        while (ns > longest && !at.compare_exchange_weak(longest, ns, std::memory_order_relaxed)) {
        }
    }
    /// 0.4.6 beta 17: graphics pipelines new in play by the parts the pipeline library would have
    /// found built when they came (bit 1: the fragment shader's, bit 2: the stages before
    /// rasterization's with slots of their own; vk_graphics_pipeline.cpp NoteParts), and the waits
    /// for them: this window (exchanged) and since the start.
    std::array<std::atomic<std::uint64_t>, 4> parts_new{}, parts_waits{}, parts_wait_ns{};
    std::array<std::atomic<std::uint64_t>, 4> parts_new_total{}, parts_waits_total{}, parts_wait_ns_total{};

    void NoteNewParts(unsigned kind) noexcept {
        parts_new[kind].fetch_add(1, std::memory_order_relaxed);
        parts_new_total[kind].fetch_add(1, std::memory_order_relaxed);
    }
    void NotePartWait(unsigned kind, std::uint64_t ns) noexcept {
        parts_waits[kind].fetch_add(1, std::memory_order_relaxed);
        parts_wait_ns[kind].fetch_add(ns, std::memory_order_relaxed);
        parts_waits_total[kind].fetch_add(1, std::memory_order_relaxed);
        parts_wait_ns_total[kind].fetch_add(ns, std::memory_order_relaxed);
    }
    void NoteWait(std::uint64_t ns) noexcept {
        waits.fetch_add(1, std::memory_order_relaxed);
        wait_ns.fetch_add(ns, std::memory_order_relaxed);
        waits_total.fetch_add(1, std::memory_order_relaxed);
        wait_ns_total.fetch_add(ns, std::memory_order_relaxed);
        Max(longest_wait_ns, ns);
        Max(longest_wait_mark_ns, ns);
    }
};

inline PipelineCompileStats pipeline_compile_stats;

} // namespace Vulkan

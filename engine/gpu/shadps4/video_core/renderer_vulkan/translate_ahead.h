// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.6 beta 17 part 2 (docs/memory-model.md, beta 17): shader translations queued
// ahead of the GPU command thread (stage A). A draw-preparation worker that meets a program not
// translated yet, or a variant of one, queues a job translating it on the compile threads; stage A,
// where it would translate, takes the job's result instead (PipelineCache::GetProgram). This file
// holds the plain parts: a job's state and the registry stage A finds the jobs in. No Vulkan and no
// shader types here: tests/test_translate_ahead.cpp.

#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vulkan::Ahead {

/// A job's life. Queued: no thread has started it (a compile thread pops it, or stage A takes it
/// out of the queue and runs it itself). Running, then Done or Failed (a fault reading the game's
/// memory, or the translator refusing the shader): stage A translates as before.
enum class State : std::uint32_t { Queued, Running, Done, Failed };

/// The part of a job that threads wait on.
class Progress {
public:
    State Get() const noexcept {
        return state.load(std::memory_order_acquire);
    }
    /// The first thread to start the job: true for one caller only.
    bool Start() noexcept {
        State expected = State::Queued;
        return state.compare_exchange_strong(expected, State::Running, std::memory_order_acq_rel);
    }
    void Finish(bool ok) {
        {
            std::scoped_lock lk{mutex};
            state.store(ok ? State::Done : State::Failed, std::memory_order_release);
        }
        cv.notify_all();
    }
    /// Until the job is done or failed (it must have started, or be about to).
    State Wait() {
        std::unique_lock lk{mutex};
        cv.wait(lk, [this] {
            const State s = state.load(std::memory_order_acquire);
            return s == State::Done || s == State::Failed;
        });
        return state.load(std::memory_order_acquire);
    }

private:
    std::atomic<State> state{State::Queued};
    std::mutex mutex;
    std::condition_variable cv;
};

/// The jobs queued ahead, one entry per stage a job translates, by the program's hash and hardware
/// stage. Workers add (several threads), stage A finds and removes. An entry no draw takes (its draw
/// went another way) goes once it is older than `max_age_ns`, or when the registry holds more than
/// `max_entries` (the oldest first), so the registry stays small whatever the workers queue.
template <typename Job>
class Registry {
public:
    struct Entry {
        std::shared_ptr<Job> job;
        std::uint32_t stage = 0;
        std::uint64_t queued_ns = 0;
    };

    explicit Registry(std::uint64_t max_age_ns_ = 30'000'000'000ull, std::size_t max_entries_ = 4096)
        : max_age_ns{max_age_ns_}, max_entries{max_entries_} {}

    void Add(std::uint64_t hash, std::uint32_t hw_stage, std::shared_ptr<Job> job, std::uint32_t stage,
             std::uint64_t now_ns) {
        std::scoped_lock lk{mutex};
        ExpireLocked(now_ns);
        by_key[Key(hash, hw_stage)].push_back({std::move(job), stage, now_ns});
        ++count;
        while (count > max_entries) {
            DropOldestLocked();
        }
    }

    /// The first entry for (hash, hw_stage) whose job and stage satisfy `pred(job, stage)`.
    template <typename Pred>
    std::optional<Entry> Find(std::uint64_t hash, std::uint32_t hw_stage, Pred&& pred) const {
        std::scoped_lock lk{mutex};
        const auto it = by_key.find(Key(hash, hw_stage));
        if (it == by_key.end()) {
            return std::nullopt;
        }
        for (const Entry& e : it->second) {
            if (pred(*e.job, e.stage)) {
                return e;
            }
        }
        return std::nullopt;
    }

    /// Takes out the entry of `job`'s `stage` (used, or found to be of no use).
    void Remove(std::uint64_t hash, std::uint32_t hw_stage, const Job* job, std::uint32_t stage) {
        std::scoped_lock lk{mutex};
        const auto it = by_key.find(Key(hash, hw_stage));
        if (it == by_key.end()) {
            return;
        }
        auto& list = it->second;
        const auto before = list.size();
        std::erase_if(list, [&](const Entry& e) { return e.job.get() == job && e.stage == stage; });
        count -= before - list.size();
        if (list.empty()) {
            by_key.erase(it);
        }
    }

    std::size_t Size() const {
        std::scoped_lock lk{mutex};
        return count;
    }

private:
    static std::uint64_t Key(std::uint64_t hash, std::uint32_t hw_stage) {
        return (hash ^ (std::uint64_t(hw_stage) << 56)) * 0x9E3779B97F4A7C15ull;
    }

    void ExpireLocked(std::uint64_t now_ns) {
        for (auto it = by_key.begin(); it != by_key.end();) {
            auto& list = it->second;
            const auto before = list.size();
            std::erase_if(list, [&](const Entry& e) { return now_ns - e.queued_ns > max_age_ns; });
            count -= before - list.size();
            it = list.empty() ? by_key.erase(it) : std::next(it);
        }
    }

    void DropOldestLocked() {
        auto oldest = by_key.end();
        std::size_t at = 0;
        for (auto it = by_key.begin(); it != by_key.end(); ++it) {
            for (std::size_t i = 0; i < it->second.size(); ++i) {
                if (oldest == by_key.end() || it->second[i].queued_ns < oldest->second[at].queued_ns) {
                    oldest = it;
                    at = i;
                }
            }
        }
        if (oldest == by_key.end()) {
            count = 0;
            return;
        }
        oldest->second.erase(oldest->second.begin() + std::ptrdiff_t(at));
        --count;
        if (oldest->second.empty()) {
            by_key.erase(oldest);
        }
    }

    const std::uint64_t max_age_ns;
    const std::size_t max_entries;
    mutable std::mutex mutex;
    std::unordered_map<std::uint64_t, std::vector<Entry>> by_key;
    std::size_t count = 0;
};

} // namespace Vulkan::Ahead

// SPDX-License-Identifier: GPL-3.0-or-later
// The Hunter's Dream backend, 0.4.6 beta 14 part 7: the event poller's generations and its one
// request at a time, as droogie/bbhost's net::poll_pacing::Gate (src/net/poll_pacing.h, 2e40749).
// Header-only and free of engine headers, so tests/test_thd_poll_gate.cpp runs it with threads.
//
// Each start of the poller is a new generation; a stop ends the current one. A poller of a generation
// that ended while its poll was held (a stop, maybe a start again) must not give what that poll brought
// to the handler, ack it or move a cursor: nothing acked it, so the next generation's poller gets the
// same event. And one request is out at a time: a new poller's first poll waits until an old one's
// held poll has ended (which a stop cuts short). The keepalive sleeps under the same generation, so a
// stop and a start within its 15 s leave one keepalive, not two.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace BbThd {

class PollGate {
public:
    // A new generation, the current one from now on.
    std::uint64_t Start() {
        std::lock_guard lk(mu_);
        current_ = ++counter_;
        cv_.notify_all();
        return current_;
    }
    // No generation is current.
    void Stop() {
        std::lock_guard lk(mu_);
        current_ = 0;
        cv_.notify_all();
    }
    bool Running() const {
        std::lock_guard lk(mu_);
        return current_ != 0;
    }
    bool Live(std::uint64_t gen) const {
        std::lock_guard lk(mu_);
        return gen != 0 && current_ == gen;
    }
    // Waits until no other request is out and takes the turn; false (no turn) once `gen` is over.
    bool Begin(std::uint64_t gen) {
        std::unique_lock lk(mu_);
        cv_.wait(lk, [&] { return !busy_ || current_ != gen; });
        if (gen == 0 || current_ != gen) return false;
        busy_ = true;
        return true;
    }
    void End() {
        std::lock_guard lk(mu_);
        busy_ = false;
        cv_.notify_all();
    }
    // Sleeps up to `ms`; false when `gen` ended meanwhile (at once when it had).
    bool Sleep(std::uint64_t gen, int ms) {
        std::unique_lock lk(mu_);
        return !cv_.wait_for(lk, std::chrono::milliseconds(ms < 0 ? 0 : ms), [&] { return current_ != gen; });
    }

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::uint64_t counter_ = 0, current_ = 0;
    bool busy_ = false;
};

} // namespace BbThd

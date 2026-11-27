// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 19 (docs/stage-a-cost.md, part 2): the engine's threads sleep on a word of their own, as
// the runtime's do since beta 18 (src/host_sync.h, host_park): Windows' WaitOnAddress and
// WakeByAddress (looked up at run time), a private futex on Linux (bbport_park.cpp; <windows.h>
// stays out of the renderer's headers). A wait returns when the word no longer holds `expected`,
// when woken, at the timeout, or spuriously: callers look again. std::atomic's wait and notify went
// through libc++'s global table of locks and condition variables on Windows: every wake took a lock
// shared by every waiting engine thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace BbPark {

constexpr std::uint32_t kForever = 0xffffffffu;

/// Sleeps while `word` holds `expected`, at most `timeout_ms` (kForever: no limit). False at the
/// timeout.
bool Wait(std::atomic<std::uint32_t>& word, std::uint32_t expected, std::uint32_t timeout_ms = kForever);
void WakeOne(std::atomic<std::uint32_t>& word);
void WakeAll(std::atomic<std::uint32_t>& word);

/// BB_ENGINE_PARK=0 (0.4.6 beta 19 only): the recording threads and the GPU command thread wait on
/// condition variables as before.
inline bool EngineWaits() {
    static const bool on = [] {
        const char* env = std::getenv("BB_ENGINE_PARK");
        return !(env && env[0] == '0');
    }();
    return on;
}

/// The usual pairing: a waiter reads the word, checks its condition, and waits on the value it
/// read; a waker changes its condition, bumps the word and wakes. A bump between the waiter's read
/// and its wait makes the wait return at once.
inline void Bump(std::atomic<std::uint32_t>& word, bool all = false) {
    word.fetch_add(1, std::memory_order_seq_cst);
    all ? WakeAll(word) : WakeOne(word);
}

} // namespace BbPark

// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 19 (docs/stage-a-cost.md, part 2): the engine's threads sleep on a word of their own, as
// the runtime's do since beta 18 (src/host_sync.h, host_park): Windows' WaitOnAddress and
// WakeByAddress (looked up at run time), a private futex on Linux. A wait returns when the word no
// longer holds `expected`, when woken, at the timeout, or spuriously: callers look again.
// std::atomic's wait and notify went through libc++'s global table of locks and condition
// variables on Windows: every wake took a lock shared by every waiting engine thread.
#pragma once

#include <atomic>
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <climits>
#include <ctime>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace BbPark {

constexpr std::uint32_t kForever = 0xffffffffu;

#ifdef _WIN32
namespace Detail {
using WaitFn = BOOL(WINAPI*)(volatile VOID*, PVOID, SIZE_T, DWORD);
using WakeFn = VOID(WINAPI*)(PVOID);
inline HMODULE Synch() {
    static const HMODULE module = [] {
        HMODULE m = GetModuleHandleW(L"api-ms-win-core-synch-l1-2-0.dll");
        return m ? m : LoadLibraryW(L"api-ms-win-core-synch-l1-2-0.dll");
    }();
    return module;
}
inline WaitFn Wait() {
    static const auto fn = reinterpret_cast<WaitFn>(
        reinterpret_cast<void*>(GetProcAddress(Synch(), "WaitOnAddress")));
    return fn;
}
inline WakeFn Wake(bool all) {
    static const auto one = reinterpret_cast<WakeFn>(
        reinterpret_cast<void*>(GetProcAddress(Synch(), "WakeByAddressSingle")));
    static const auto every = reinterpret_cast<WakeFn>(
        reinterpret_cast<void*>(GetProcAddress(Synch(), "WakeByAddressAll")));
    return all ? every : one;
}
} // namespace Detail

/// Sleeps while `word` holds `expected`, at most `timeout_ms` (kForever: no limit). False at the
/// timeout.
inline bool Wait(std::atomic<std::uint32_t>& word, std::uint32_t expected,
                 std::uint32_t timeout_ms = kForever) {
    if (word.load(std::memory_order_seq_cst) != expected) {
        return true;
    }
    if (const auto wait = Detail::Wait()) {
        return wait(reinterpret_cast<volatile VOID*>(&word), &expected, sizeof expected,
                    timeout_ms == kForever ? INFINITE : timeout_ms) ||
               GetLastError() != ERROR_TIMEOUT;
    }
    Sleep(1); // no WaitOnAddress (before Windows 8): a short sleep, the caller looks again
    return true;
}
inline void WakeOne(std::atomic<std::uint32_t>& word) {
    if (const auto wake = Detail::Wake(false)) {
        wake(reinterpret_cast<PVOID>(&word));
    }
}
inline void WakeAll(std::atomic<std::uint32_t>& word) {
    if (const auto wake = Detail::Wake(true)) {
        wake(reinterpret_cast<PVOID>(&word));
    }
}
#else
inline bool Wait(std::atomic<std::uint32_t>& word, std::uint32_t expected,
                 std::uint32_t timeout_ms = kForever) {
    if (word.load(std::memory_order_seq_cst) != expected) {
        return true;
    }
    timespec limit{}, *at = nullptr;
    if (timeout_ms != kForever) {
        limit.tv_sec = timeout_ms / 1000;
        limit.tv_nsec = static_cast<long>(timeout_ms % 1000) * 1000000L;
        at = &limit;
    }
    const long r = syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(&word), FUTEX_WAIT_PRIVATE,
                           expected, at, nullptr, 0);
    return !(r == -1 && errno == ETIMEDOUT);
}
inline void WakeOne(std::atomic<std::uint32_t>& word) {
    syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(&word), FUTEX_WAKE_PRIVATE, 1, nullptr,
            nullptr, 0);
}
inline void WakeAll(std::atomic<std::uint32_t>& word) {
    syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(&word), FUTEX_WAKE_PRIVATE, INT_MAX,
            nullptr, nullptr, 0);
}
#endif

/// The usual pairing: a waiter reads the word, checks its condition, and waits on the value it
/// read; a waker changes its condition, bumps the word and wakes. A bump between the waiter's read
/// and its wait makes the wait return at once.
inline void Bump(std::atomic<std::uint32_t>& word, bool all = false) {
    word.fetch_add(1, std::memory_order_seq_cst);
    all ? WakeAll(word) : WakeOne(word);
}

} // namespace BbPark

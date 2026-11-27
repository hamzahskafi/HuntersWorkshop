// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's parking words (bbport_park.h).
#include "bbport_park.h"

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
#include <linux/membarrier.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace BbPark {

#ifdef _WIN32
namespace {
using WaitFn = BOOL(WINAPI*)(volatile VOID*, PVOID, SIZE_T, DWORD);
using WakeFn = VOID(WINAPI*)(PVOID);
HMODULE Synch() {
    static const HMODULE module = [] {
        HMODULE m = GetModuleHandleW(L"api-ms-win-core-synch-l1-2-0.dll");
        return m ? m : LoadLibraryW(L"api-ms-win-core-synch-l1-2-0.dll");
    }();
    return module;
}
WaitFn WaitFunction() {
    static const auto fn = reinterpret_cast<WaitFn>(
        reinterpret_cast<void*>(GetProcAddress(Synch(), "WaitOnAddress")));
    return fn;
}
WakeFn WakeFunction(bool all) {
    static const auto one = reinterpret_cast<WakeFn>(
        reinterpret_cast<void*>(GetProcAddress(Synch(), "WakeByAddressSingle")));
    static const auto every = reinterpret_cast<WakeFn>(
        reinterpret_cast<void*>(GetProcAddress(Synch(), "WakeByAddressAll")));
    return all ? every : one;
}
} // namespace

bool Wait(std::atomic<std::uint32_t>& word, std::uint32_t expected, std::uint32_t timeout_ms) {
    if (word.load(std::memory_order_seq_cst) != expected) {
        return true;
    }
    if (const auto wait = WaitFunction()) {
        return wait(reinterpret_cast<volatile VOID*>(&word), &expected, sizeof expected,
                    timeout_ms == kForever ? INFINITE : timeout_ms) ||
               GetLastError() != ERROR_TIMEOUT;
    }
    Sleep(1); // no WaitOnAddress (before Windows 8): a short sleep, the caller looks again
    return true;
}
void WakeOne(std::atomic<std::uint32_t>& word) {
    if (const auto wake = WakeFunction(false)) {
        wake(reinterpret_cast<PVOID>(&word));
    }
}
void WakeAll(std::atomic<std::uint32_t>& word) {
    if (const auto wake = WakeFunction(true)) {
        wake(reinterpret_cast<PVOID>(&word));
    }
}
bool HeavyFenceAvailable() {
    return true; // since Windows Vista
}
void HeavyFence() {
    FlushProcessWriteBuffers();
}
#else
bool Wait(std::atomic<std::uint32_t>& word, std::uint32_t expected, std::uint32_t timeout_ms) {
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
void WakeOne(std::atomic<std::uint32_t>& word) {
    syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(&word), FUTEX_WAKE_PRIVATE, 1, nullptr,
            nullptr, 0);
}
void WakeAll(std::atomic<std::uint32_t>& word) {
    syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(&word), FUTEX_WAKE_PRIVATE, INT_MAX,
            nullptr, nullptr, 0);
}
bool HeavyFenceAvailable() {
    static const bool registered =
        syscall(SYS_membarrier, MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED, 0, 0) == 0;
    return registered;
}
void HeavyFence() {
    if (!HeavyFenceAvailable() || syscall(SYS_membarrier, MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0, 0) != 0) {
        std::abort(); // registered: it cannot fail; the busy side relies on it
    }
}
#endif

} // namespace BbPark

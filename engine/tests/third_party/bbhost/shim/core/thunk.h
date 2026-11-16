// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only stand-in for bbhost's core/thunk.h: the registry of the game's imports by name, and a call
// into the game, here a host function (tests/test_thd_signaling.cpp defines register_hle_fn).
#pragma once

#include <cstdint>

void register_hle_fn(const char* name, void* fn);

template <typename R, typename... A>
R hle_call_guest(void* fn, A... a) {
    return reinterpret_cast<R (*)(A...)>(fn)(a...);
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only stand-in for bbhost's log.h (tests/test_thd_signaling.cpp defines host_log).
#pragma once

void host_log(const char* format, ...) __attribute__((format(printf, 1, 2)));

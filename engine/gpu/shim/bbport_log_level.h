// SPDX-License-Identifier: GPL-2.0-or-later
// BB_LOG_LEVEL (the launcher's Log level): 0 standard (the default), 1 detailed, 2 debug. Lines that
// repeat through a session are written from detailed on; debug also lowers the GPU log to its debug
// messages (common/logging/log.cpp). The runtime's side is src/runtime_host.c's runtime_log_level.
#pragma once
#include <cstdlib>
#include <cstring>

inline int BbLogLevel() {
    static const int level = [] {
        const char* v = std::getenv("BB_LOG_LEVEL");
        return v && !std::strcmp(v, "debug") ? 2 : v && !std::strcmp(v, "detailed") ? 1 : 0;
    }();
    return level;
}

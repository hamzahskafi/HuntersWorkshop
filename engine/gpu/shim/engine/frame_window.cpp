// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 15: frame windows (frame_window.h). At most a few hundred flips a second: a lock.
#include "frame_window.h"

#include <algorithm>
#include <mutex>

namespace BbEngine::Frames {
namespace {
std::mutex g_mutex;
bool g_open[kSlots]{};
Window g_window[kSlots]{};
} // namespace

void NoteFlip(double ms) {
    std::scoped_lock lock{g_mutex};
    for (unsigned i = 0; i < kSlots; ++i) {
        if (!g_open[i]) {
            continue;
        }
        Window& w = g_window[i];
        ++w.frames;
        w.over_25 += ms > 25.0;
        w.worst_ms = std::max(w.worst_ms, ms);
    }
}

void Open(Slot slot) {
    std::scoped_lock lock{g_mutex};
    g_open[slot] = true;
    g_window[slot] = {};
}

Window Close(Slot slot) {
    std::scoped_lock lock{g_mutex};
    g_open[slot] = false;
    return g_window[slot];
}

} // namespace BbEngine::Frames

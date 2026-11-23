// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 15: frame times over a span of time, from the flips (videoout/driver.cpp), for the
// compile burst lines (part 1) and the line on each load's first frames (part 5).
#pragma once
#include <cstdint>

namespace BbEngine::Frames {

enum Slot : unsigned { kBurst, kLoad, kSlots };

struct Window {
    std::uint32_t frames = 0, over_25 = 0;
    double worst_ms = 0;
};

/// Each flip, with the time since the last one.
void NoteFlip(double ms);
/// Starts counting the flips for `slot` from zero.
void Open(Slot slot);
/// Stops counting for `slot` and returns what it counted.
Window Close(Slot slot);

} // namespace BbEngine::Frames

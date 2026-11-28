// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 15 (docs/memory-model.md, beta 15): work moved into the loading screen. The load clock,
// at each flip (videoout/driver.cpp): at a load's 30th frame the surfaces of the learned shapes are
// made (part 3); 2 s after it ends its line is written (part 5) and the shapes allocated since are
// learned; 3 s after it the ready surfaces not taken are freed. The counters below are the parts'
// own, kept since start; the line takes their differences over the load and the 2 s after it.
#pragma once

#include <atomic>
#include <cstdint>

namespace VideoCore::LoadWork {

struct Counters {
    // Part 2 (BB_LOAD_UPLOAD): the game's buffers made with data during a loading screen.
    std::atomic<std::uint64_t> made_bytes{0};      ///< queued
    std::atomic<std::uint64_t> ahead_bytes{0};     ///< of them uploaded from the queue (their size)
    std::atomic<std::uint64_t> ahead_copied{0};    ///< what those uploads copied
    std::atomic<std::uint64_t> drawn_bytes{0};     ///< found uploaded already (a draw used them first)
    std::atomic<std::uint64_t> released_bytes{0};  ///< released by the game before either
    std::atomic<std::uint64_t> dropped_bytes{0};   ///< left in the queue 2 s after the load
    std::atomic<std::uint64_t> frames_held{0};     ///< frames it waited: VRAM not comfortable
    // Part 4 (BB_COPY_NOTICE): the create calls' copies told before they run.
    std::atomic<std::uint64_t> copies_told{0};
    std::atomic<std::uint64_t> pages_dropped{0};   ///< GPU copies dropped without a readback
    std::atomic<std::uint64_t> pages_writable{0};  ///< pages made writable before the copy
};
inline Counters counters;

/// Each flip (videoout/driver.cpp, the flip thread).
void OnFlip();

} // namespace VideoCore::LoadWork

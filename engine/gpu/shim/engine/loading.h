// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>

namespace BbEngine::Loading {
// The game's image (loader startup, after the frame hook is in place).
void Install(std::uint8_t* image);
// Each frame on the game's main thread, at the start of its frame pacer (SprjFlipper::Update),
// `flipper` being the pacer object.
void Tick(std::uint64_t flipper);
// A loading screen is up (the presenter may lift its frame limit then).
bool ScreenUp();
// 0.4.6 beta 13: whether a loading screen ended less than `window_ns` before `now_ns` (steady clock),
// for the long-frame breakdown from 25 ms after a load.
bool EndedWithin(std::uint64_t now_ns, std::uint64_t window_ns);
// 0.4.6 beta 15: the latest load (0.4.6 beta 15's loading-screen work, video_core/load_work.cpp): its
// number from 1 (0: none yet), whether it is still up, the frames it has shown, and when it ended
// (steady clock, ns; 0 while up). Read from any thread.
struct Status {
    std::uint32_t load = 0;
    bool up = false;
    std::uint64_t frames = 0, ended_ns = 0;
};
Status Current();
// The map folders the latest load opened, as its "Loading:" line names them.
std::string LastMaps();
} // namespace BbEngine::Loading

// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.6 beta 17: the start's splash, a small window in the middle of the game's
// monitor from the engine's start to the game's first frame: the game's own background art (the
// player's sce_sys/pic1), what the start is doing and how far (engine/startup.h). The game window
// stays hidden behind it until then. BB_SPLASH=0: none (the game window shows at once, as before).
// Window thread only (SDL video's thread).

#pragma once

#include <cstdint>

namespace BbSplash {

/// Before the game window: the splash on the game's monitor. False when off or SDL could not make it.
bool Open(const char* title);
/// Each round of the window thread's loop: draws the current step (at most about 30 times a second).
void Draw();
void Close();
bool IsOpen();

} // namespace BbSplash

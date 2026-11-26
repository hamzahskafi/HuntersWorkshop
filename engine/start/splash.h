// SPDX-License-Identifier: GPL-2.0-or-later
// bb-start: the start's splash (bb-splash.exe beside this program: engine/gpu/splash/main.cpp), on the
// screen from the start's first step until the game's first frame, whether the game was started from
// the launcher or from a Steam shortcut (0.4.6 beta 17).
#pragma once
#include "common.h"

#include <windows.h>

#include "../gpu/splash/status_file.h"

namespace start {

class Splash {
public:
    /// Starts bb-splash.exe from `out` with its status file (BB_SPLASH_STATUS, which the engine writes
    /// too; in the temporary folder) and `game` (its art), and writes the first step. Nothing when
    /// BB_SPLASH=0, or when it cannot start.
    void Start(const fs::path& out, const fs::path& game);
    void Step(BbSplashStatus::Step step);
    /// The start over (bb-probe ended): the file removed (the splash closes as this program ends).
    ~Splash();

private:
    BbSplashStatus::Writer writer;
    fs::path file;
};

} // namespace start

// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.6 beta 17: what the engine's start is doing, for the start's splash (bb-splash,
// splash/main.cpp, a program of its own that bb-start or run.sh starts first: its status file,
// splash/status.h), and the helper programs the start runs on the way (bb-shader-lab: the GPU cache
// settled at the start, docs/memory-model.md, "First sight").

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "../../splash/status.h"

namespace BbStartup {

using Step = BbSplashStatus::Step;

/// Whether the start's splash is on the screen beside this engine: bb-start or run.sh started it and
/// named its status file (BB_SPLASH_STATUS), and the file is still there (the splash removes it as it
/// closes, or when it could not show). The game window stays hidden behind it until the game's first
/// frame. A file check: the window thread asks a few times a second.
bool SplashRunning();
/// The step the start is on; its progress starts again at nothing. Any thread.
void SetStep(Step step);
/// How far the step is: `done` of `total` (0 total: no count, the splash's bar moves on its own).
/// Written at most every 50 ms, and when it is complete.
void SetProgress(std::uint64_t done, std::uint64_t total);
/// Window thread, the game window shown: the splash closes. Nothing is written after it.
void CloseSplash();

/// Present thread, before the game's first frame: asks the window thread to show the game window
/// (hidden behind the splash until then), and waits until the window shows, at most `timeout_ms` (a
/// frame put into a window not shown yet: an error on Wayland).
void FirstFrameReady(int timeout_ms);
/// Window thread: whether the first frame is waiting for the game window.
bool FirstFrame();
/// Window thread: the game window shows (or never was hidden).
void NoteGameWindowShown();

/// The folder this program is in (engine/out).
std::filesystem::path ProgramDir();
/// A helper program the engine runs ("bb-shader-lab"): engine\out\tools\<name>.exe on Windows (a
/// folder of their own, with their own DLLs: a DLL put beside the game, as OptiScaler's dxgi.dll or
/// winmm.dll, is never loaded into them), engine/out/<name> elsewhere. Empty when ProgramDir is.
std::filesystem::path ToolPath(const char* name);
/// A path in an environment variable (bb-start's BB_GAME_DIR...), read as wide text on Windows so a
/// folder name outside the system's code page survives; empty when unset.
std::filesystem::path EnvPath(const char* name);
/// A path as UTF-8 (RunTool's arguments).
std::string Utf8(const std::filesystem::path& path);

/// Runs `program` with `args`, without a window, handing each line it prints to `on_line` as it
/// comes. Its exit code; -1 when it could not be started or did not exit by itself.
int RunTool(const std::filesystem::path& program, const std::vector<std::string>& args,
            const std::function<void(const std::string&)>& on_line);

} // namespace BbStartup

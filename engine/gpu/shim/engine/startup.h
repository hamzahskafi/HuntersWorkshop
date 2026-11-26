// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.6 beta 17: what the engine's start is doing, for the splash (shim/splash.cpp),
// and the helper programs it runs on the way (bb-shader-lab: the GPU cache settled at the start,
// docs/memory-model.md, "First sight").

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace BbStartup {

/// The step the start is on (English and Russian, as the menus; BbSettings::MenuText picks one), and
/// an optional second line; the progress starts again at nothing. Any thread.
void SetStep(const char* english, const char* russian, const char* note_english = nullptr,
             const char* note_russian = nullptr);
/// How far the step is: `done` of `total` (0 total: no count, the bar moves on its own).
void SetProgress(std::uint64_t done, std::uint64_t total);

struct Status {
    std::string step, note;
    std::uint64_t done = 0, total = 0;
    std::uint32_t generation = 0; ///< changes with every SetStep
};
Status Get();

/// Present thread, before the game's first frame: asks the window thread to show the game window
/// (hidden behind the splash until then) and close the splash, and waits until the window shows, at
/// most `timeout_ms` (a frame put into a window not shown yet: an error on Wayland).
void FirstFrameReady(int timeout_ms);
/// Window thread: whether the first frame is waiting for the game window.
bool FirstFrame();
/// Window thread: the game window shows (or never was hidden).
void NoteGameWindowShown();

/// The folder this program is in (engine/out).
std::filesystem::path ProgramDir();
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

// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.6 beta 17: the status file of the start's splash (bb-splash, splash/main.cpp).
// bb-start (or run.sh on Linux) makes it before it starts the splash, names it to the splash
// (--status) and to the engine (BB_SPLASH_STATUS), and each writes the step the start is on over the
// last one; the splash shows it in the player's language until the engine writes "close" (the game
// window shown at its first frame). The format here; reading and writing the file: status_file.h.
//
// One record, at the file's start: "<serial> <step> <done> <total> <serial>", padded with spaces to
// 63 characters and a newline. Written over the last, never shortened: a record read half written
// has two different serials and is passed over (the splash keeps the last whole one).

#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace BbSplashStatus {

inline constexpr std::size_t kRecordSize = 64;

/// The start's steps, in their order (bb-start or run.sh: Prepare, Engine; the engine the others).
enum class Step : int {
    Prepare,     ///< mods, the boot image, patches (bb-start, run.sh)
    Engine,      ///< bb-probe started, the game's files loading
    CacheForGpu, ///< the GPU cache translated again for another graphics card or driver
    CacheUpdate, ///< ... for another engine version
    Catalog,     ///< the shipped shader catalog seeded
    Shaders,     ///< the GPU cache's pipelines built (the warm-up)
    Starting,    ///< the game's first frame on its way
    Close,       ///< the game window shows: the splash closes
};
inline constexpr const char* kStepWords[] = {"prepare", "engine",  "gpu",      "update",
                                             "catalog", "shaders", "starting", "close"};
inline constexpr int kStepCount = int(sizeof(kStepWords) / sizeof(kStepWords[0]));
static_assert(kStepCount == int(Step::Close) + 1);

struct Status {
    Step step = Step::Prepare;
    std::uint64_t done = 0, total = 0; ///< total 0: no count
};

inline std::string Record(std::uint32_t serial, Step step, std::uint64_t done, std::uint64_t total) {
    char text[kRecordSize];
    std::snprintf(text, sizeof text, "%u %s %llu %llu %u", unsigned(serial), kStepWords[int(step)],
                  static_cast<unsigned long long>(done), static_cast<unsigned long long>(total), unsigned(serial));
    std::string record(text);
    record.resize(kRecordSize - 1, ' ');
    record += '\n';
    return record;
}

/// The record at the start of `text`; none when it is half written, empty or unknown.
inline std::optional<Status> Parse(std::string_view text) {
    const auto newline = text.find('\n');
    if (newline == std::string_view::npos) return std::nullopt;
    const std::string line(text.substr(0, newline));
    unsigned serial = 0, serial_end = 0;
    char word[16] = {};
    unsigned long long done = 0, total = 0;
    int end = 0;
    if (std::sscanf(line.c_str(), "%u %15s %llu %llu %u%n", &serial, word, &done, &total, &serial_end, &end) != 5 ||
        serial != serial_end || line.find_first_not_of(' ', std::size_t(end)) != std::string::npos) {
        return std::nullopt;
    }
    for (int s = 0; s < kStepCount; ++s) {
        if (std::string_view(word) == kStepWords[s]) return Status{Step(s), done, total};
    }
    return std::nullopt;
}

} // namespace BbSplashStatus

// SPDX-License-Identifier: GPL-3.0-or-later
// Hunter's Workshop 0.4.5 beta 15: where the player is, and what the VRAM manager should keep for it.
//
// The game's areas live in folders of their own (map/m21, map/m34, ...): the file layer reports every
// map file the game opens (runtime_area_hook), so the engine knows which areas are loaded.
//   A visit is a stretch of play in one place: a new one starts at each loading screen, and when the
//   game opens the files of an area that has not been present for 2 minutes (walking on without a
//   loading screen). A visit's areas are every area whose files were opened during it (m34 and m35
//   load together). During a loading screen the destination's files open, so its areas are known
//   before the screen ends.
//   Learned, nothing hard-coded: each area's score is +1 a visit, halving every 10 minutes (areas the
//   player keeps returning to rise; the Hunter's Dream does, by itself), and a table of where the
//   player went next from each area (decaying the same way) names the likely next areas.
// The VRAM manager tags what it holds with the visit it was last used in (CurrentVisit), and orders
// what to free by a view of the visits (Snapshot): protected (the current areas, the destination, the
// likely next areas) and how much each is worth keeping (its areas' scores).
#pragma once
#include <atomic>
#include <cstdint>
#include <vector>

namespace BbEngine::Areas {
constexpr int kAreas = 100;          // m00-m99
constexpr std::uint32_t kRing = 1024; // visits remembered

inline std::atomic<std::uint32_t> current_visit{1};
// The visit now: one relaxed load, for hot paths (an image or a block marked as used).
inline std::uint32_t CurrentVisit() {
    return current_visit.load(std::memory_order_relaxed);
}

// At loader startup: the file layer reports map files from then on. BB_VRAM_AREAS=0: not installed
// (every visit is 1: the VRAM manager falls back to ages).
void Install();
// Once a second (the VRAM manager): loading screens start visits; the current areas stay present.
void Tick();
// A loading screen is up and its destination is known (an area's files opened since it began, or it
// has been up a second).
bool DestinationKnown();

struct View {
    std::uint32_t visit = 0;
    std::vector<std::uint8_t> guard; // [visit % kRing]: 1 if protected
    std::vector<float> keep;         // [visit % kRing]: its areas' learned score
    std::vector<std::uint32_t> id;   // [visit % kRing]: the visit held there
    bool Protected(std::uint32_t v) const {
        return v != 0 && id.size() == kRing && id[v % kRing] == v && guard[v % kRing] != 0;
    }
    float Keep(std::uint32_t v) const {
        return v != 0 && id.size() == kRing && id[v % kRing] == v ? keep[v % kRing] : 0.0f;
    }
};
// The visits as the VRAM manager orders them now (built in well under a millisecond; once a trim).
View Snapshot();

// One line a minute.
void LogMinute();
} // namespace BbEngine::Areas

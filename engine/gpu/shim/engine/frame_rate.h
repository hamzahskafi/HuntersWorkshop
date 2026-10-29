// SPDX-License-Identifier: GPL-3.0-or-later
// Frame-rate corrections at 60 FPS and above, on top of Kyo's "60 FPS++" (frame_rate.cpp; the sites are in
// frame_rate_sites.inc). Adapted from bbhost (src/engine/frame_rate.cpp; licenses/BBHOST-PORTS.md).
#pragma once
#include <cstddef>
#include <cstdint>

namespace BbEngine::FrameRate {
/// How a value tuned for one 30 FPS frame follows a frame of `seconds`.
enum class Kind : std::uint8_t {
    Step,   ///< a step a frame: the frame's seconds
    Amount, ///< an amount a frame: was * 30 * seconds
    Decay,  ///< a weight a frame (value += (target - value) * w): 1 - (1 - was)^(30 * seconds)
    Keep,   ///< a share kept a frame: was^(30 * seconds)
    Rate,   ///< a distance a frame times 30 is a speed: was / (30 * seconds)
    Game,   ///< the game's own value at every rate (a duration the FPS++ list took for a step)
    Cloth,  ///< the cloth's stiffness scale, its solver's rate against the 60 it was made for: (60 * seconds)^2, at most 1
};
float Rated(Kind kind, float was, double seconds);

/// The play time's milliseconds for a frame of `seconds`, the remainder carried to the next frame (the
/// game truncates each frame's, so its clock ran 4% slow at 60).
class PlayTime {
public:
    std::int32_t Add(double seconds);

private:
    double carry = 0;
};

/// Loader start, after the patches and the executable's verification: at 60, 90, 120, 144 FPS or uncapped
/// (BB_FPS; uncapped's limit BB_FPS_LIMIT) with the 60 FPS++ list in place, checks and applies the sites,
/// sets the frame pacer's target and the values for the rate, and points the per-frame loads at slots past
/// the end of the game's data segment, in its last page (`size`: the whole image, the linked modules after
/// the game included). BB_FRAME_FIXES=0 turns all of it off; BB_FRAME_FIXES_SKIP names groups to leave out
/// (a comma list: clips, physics, grass, targeting, jump, sprint, slope, online, hitbox, timers, menus,
/// durations, playtime, pace, sound, cloth, steps, lens, timer, camera, clothstep; without pace the game stays at
/// 60).
/// The frame timer (Step) and the camera's eases with it, unless BB_FRAME_TIMER=0.
bool Install(std::uint8_t* image, std::size_t size);
/// Each frame on the game's main thread, at the start of its frame pacer: the slots from the frame's
/// time (the 60 FPS++ cave's, 0x575a908), not with the frame timer; and the Bloodletter's L2 self-hit
/// once its param table is loaded.
void Tick();
/// The frame timer (on unless BB_FRAME_TIMER=0): each frame from the cave, right after the frame pacer (`pacer`)
/// returns, before the frame's tasks: the step, the task step's and 0x575a908's, and the slots from it.
void Step(std::uint64_t pacer);
} // namespace BbEngine::FrameRate

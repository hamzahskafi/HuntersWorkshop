// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.4 beta 25b: a camera cut between two frames, for the temporal upscalers' history (DLSS, FSR 3.1,
// FSR 4 and TAA keep one). NVIDIA's DLSS Programming Guide (310.6.0, §3.13): after "a complete (or
// significant) change in the scene" the history carries "ghost images from the previous scene", so the
// first frame after it resets it; "improper use of this can result in temporal flickering, heavy
// aliasing", so the thresholds sit far above what play does. The frames before a loading screen are
// cut off already (a frame without the upscaler resets the next); this is for cuts while the scene
// keeps drawing, as a cutscene's shots. Pure arithmetic on CameraMotion's matrices (tests/test_camera_cut.cpp).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace Vulkan::CameraCut {

/// How far the camera went from one frame to the next: the angle it turned (radians, the whole
/// rotation whatever its axis) and how far it moved (the game's units, metres).
struct Change {
    float turn = 0.0f;
    float move = 0.0f;
    float along = 0.0f; ///< the part of `move` along the previous frame's view direction (for the log)
    bool valid = false; ///< both matrices invertible and finite
};

/// The defaults: more than the vertical field of view turned (the two frames see almost nothing in
/// common) or more than 15 m moved in one frame. Not 3 m (25b's first build): the follow camera snaps
/// towards the hunter when something comes between them, measured at 2.0 to 4.0 m in one frame with no
/// turn (the tester's run, about 160 FPS), and a snap is no cut: the motion vectors reproject what stays
/// in view, and a reset there threw that history away. 15 m is about four times the largest snap.
inline constexpr float kMaxMove = 15.0f;
/// When the projection gives no usable field of view: about 57 degrees.
inline constexpr float kFallbackTurn = 1.0f;

namespace Detail {
inline float Dot3(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
} // namespace Detail

/// The camera's position for a world-to-view matrix (3x4 rows, M | t): -M^-1 t, M inverted in
/// general (a scaled or mirrored view as well as a rotation). False when M is singular or not finite.
inline bool Position(const std::array<float, 12>& v, std::array<float, 3>& out) {
    const float a = v[0], b = v[1], c = v[2], d = v[4], e = v[5], f = v[6], g = v[8], h = v[9], i = v[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(det) || std::abs(det) < 1e-12f) {
        return false;
    }
    const float t0 = v[3], t1 = v[7], t2 = v[11];
    // M^-1 = adj(M) / det.
    out[0] = -((e * i - f * h) * t0 + (c * h - b * i) * t1 + (b * f - c * e) * t2) / det;
    out[1] = -((f * g - d * i) * t0 + (a * i - c * g) * t1 + (c * d - a * f) * t2) / det;
    out[2] = -((d * h - e * g) * t0 + (b * g - a * h) * t1 + (a * e - b * d) * t2) / det;
    return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

/// The change from `previous` to `current` (world-to-view, 3x4 rows each). The turn is the angle of
/// the relative rotation, from its trace: cos = (trace(R R'^T) - 1) / 2, the rows normalised first so a
/// scale in the view does not count as a turn.
inline Change Between(const std::array<float, 12>& previous, const std::array<float, 12>& current) {
    Change change;
    std::array<float, 3> p0{}, p1{};
    if (!Position(previous, p0) || !Position(current, p1)) {
        return change;
    }
    float trace = 0.0f;
    for (int row = 0; row < 3; ++row) {
        const float* r0 = &previous[row * 4];
        const float* r1 = &current[row * 4];
        const float n0 = std::sqrt(Detail::Dot3(r0, r0)), n1 = std::sqrt(Detail::Dot3(r1, r1));
        if (!(n0 > 1e-6f) || !(n1 > 1e-6f)) {
            return change;
        }
        trace += Detail::Dot3(r0, r1) / (n0 * n1);
    }
    const float cosine = std::clamp((trace - 1.0f) * 0.5f, -1.0f, 1.0f);
    change.turn = std::acos(cosine);
    const float dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
    change.move = std::sqrt(dx * dx + dy * dy + dz * dz);
    // The previous view direction: the view matrix's third row (its z axis in world space).
    const float* forward = &previous[8];
    const float d = dx * forward[0] + dy * forward[1] + dz * forward[2];
    change.along = std::min(std::abs(d) / std::sqrt(Detail::Dot3(forward, forward)), change.move);
    change.valid = std::isfinite(change.turn) && std::isfinite(change.move) && std::isfinite(change.along);
    return change;
}

/// A cut: more than `max_turn` radians turned (the vertical field of view; kFallbackTurn when that
/// is not a usable angle) or more than `max_move` moved in one frame.
inline bool IsCut(const Change& change, float max_turn, float max_move = kMaxMove) {
    if (!change.valid) {
        return false;
    }
    const float turn_limit = max_turn > 0.1f && max_turn < 3.0f ? max_turn : kFallbackTurn;
    return change.turn > turn_limit || change.move > max_move;
}

} // namespace Vulkan::CameraCut

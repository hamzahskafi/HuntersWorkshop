// SPDX-License-Identifier: GPL-2.0-or-later
// The upscalers' camera-cut check (camera_cut.h): the turn and move between two world-to-view matrices,
// and which of them count as a cut. No game, Vulkan device or window required.
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include "gpu/shadps4/video_core/renderer_vulkan/camera_cut.h"

using namespace Vulkan::CameraCut;

namespace {
constexpr float kDegree = 0.017453292519943295f;

bool Near(float a, float b, float tolerance) {
    return std::fabs(a - b) <= tolerance;
}

// A camera at `position` turned `yaw` about the world's y axis then `pitch` about its x axis, scaled by
// `scale`, as a world-to-view matrix (3x4 rows: M | -M position).
std::array<float, 12> View(float yaw, float pitch, std::array<float, 3> position, float scale = 1.0f) {
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    // Rows: the camera's axes in world space (pitch applied after yaw).
    const float m[3][3] = {{cy, 0.0f, -sy}, {sy * sp, cp, cy * sp}, {sy * cp, -sp, cy * cp}};
    std::array<float, 12> v{};
    for (int r = 0; r < 3; ++r) {
        float t = 0.0f;
        for (int c = 0; c < 3; ++c) {
            v[r * 4 + c] = m[r][c] * scale;
            t += m[r][c] * scale * position[c];
        }
        v[r * 4 + 3] = -t;
    }
    return v;
}
} // namespace

int main() {
    const float fov = 50.0f * kDegree;
    const std::array<float, 3> origin{10.0f, 2.0f, -4.0f};

    // The same camera: nothing turned or moved, no cut; its position recovered.
    {
        const auto v = View(0.3f, 0.1f, origin);
        std::array<float, 3> p{};
        assert(Position(v, p) && Near(p[0], 10.0f, 1e-4f) && Near(p[1], 2.0f, 1e-4f) && Near(p[2], -4.0f, 1e-4f));
        const Change c = Between(v, v);
        assert(c.valid && c.turn < 1e-3f && c.move < 1e-4f && !IsCut(c, fov));
    }
    // Play: a quick turn of 25 degrees and 0.3 m in one frame (a mouse flick at 30 FPS): no cut.
    {
        const Change c = Between(View(0.0f, 0.0f, origin), View(25.0f * kDegree, 0.0f, {10.3f, 2.0f, -4.0f}));
        assert(c.valid && Near(c.turn, 25.0f * kDegree, 1e-3f) && Near(c.move, 0.3f, 1e-4f) && !IsCut(c, fov));
    }
    // A turn about two axes counts as one rotation: 30 yaw and 40 pitch make one of about 49.6 degrees
    // (cos = (cos 30 + cos 40 + cos 30 cos 40 - 1) / 2), more than either alone, less than their sum.
    {
        const Change c = Between(View(0.0f, 0.0f, origin), View(30.0f * kDegree, 40.0f * kDegree, origin));
        const float a = 30.0f * kDegree, b = 40.0f * kDegree;
        const float expected = std::acos((std::cos(a) + std::cos(b) + std::cos(a) * std::cos(b) - 1.0f) * 0.5f);
        assert(c.valid && Near(c.turn, expected, 1e-3f) && c.turn > b);
        assert(IsCut(c, 45.0f * kDegree) && !IsCut(c, fov));
    }
    // A cutscene's next shot: turned 120 degrees, or moved 12 m: cuts. 3 m exactly is not one.
    assert(IsCut(Between(View(0.0f, 0.0f, origin), View(120.0f * kDegree, 0.0f, origin)), fov));
    assert(IsCut(Between(View(0.0f, 0.0f, origin), View(0.0f, 0.0f, {22.0f, 2.0f, -4.0f})), fov));
    {
        const Change c = Between(View(0.0f, 0.0f, origin), View(0.0f, 0.0f, {13.0f, 2.0f, -4.0f}));
        assert(Near(c.move, 3.0f, 1e-4f) && !IsCut(c, fov));
        assert(IsCut(c, fov, 2.5f)); // a lower move limit
    }
    // Turning around (180 degrees): the full angle, a cut.
    {
        const Change c = Between(View(0.0f, 0.0f, origin), View(180.0f * kDegree, 0.0f, origin));
        assert(Near(c.turn, 180.0f * kDegree, 1e-3f) && IsCut(c, fov));
    }
    // A scaled view (a unit change in the matrix) is no turn, and its position is still exact.
    {
        const Change c = Between(View(0.2f, 0.0f, origin), View(0.2f, 0.0f, {10.1f, 2.0f, -4.0f}, 2.0f));
        assert(c.valid && c.turn < 1e-3f && Near(c.move, 0.1f, 1e-4f) && !IsCut(c, fov));
    }
    // The field of view as the limit: 40 degrees turned is a cut under a 35 degree view, not a 50 one.
    {
        const Change c = Between(View(0.0f, 0.0f, origin), View(40.0f * kDegree, 0.0f, origin));
        assert(IsCut(c, 35.0f * kDegree) && !IsCut(c, fov));
        // No usable field of view (0, NaN, over 172 degrees): about 57 degrees instead.
        assert(!IsCut(c, 0.0f) && !IsCut(c, std::numeric_limits<float>::quiet_NaN()) && !IsCut(c, 3.1f));
        assert(IsCut(Between(View(0.0f, 0.0f, origin), View(60.0f * kDegree, 0.0f, origin)), 0.0f));
    }
    // Matrices that are not a camera (all zero, not finite): no change known, never a cut.
    {
        const std::array<float, 12> zero{};
        auto bad = View(0.0f, 0.0f, origin);
        bad[5] = std::numeric_limits<float>::infinity();
        assert(!Between(zero, View(0.0f, 0.0f, origin)).valid);
        assert(!Between(View(0.0f, 0.0f, origin), bad).valid);
        assert(!IsCut(Between(zero, zero), fov));
    }
    std::puts("Camera cut: PASS (turns about any axis, moves, scaled views, the field of view as the limit, "
              "matrices that are not a camera)");
}

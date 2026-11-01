// SPDX-License-Identifier: GPL-3.0-or-later
// The follow camera at every frame rate (0.4.4 beta 24): the weights its update (ChrExFollowCam, patch 0x183ac60)
// takes once a frame, each converted where the game uses it, every frame, from a 30 FPS frame to the frame's own
// length (camera_rate.cpp says what each site is). With the frame timer (frame_rate.cpp), group "camera".
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace BbEngine::CameraRate {
/// How a value tuned for one 30 FPS frame follows a frame of n = 30 x seconds 30 FPS frames.
enum class Convert : std::uint8_t {
    Decay,  ///< a weight a frame (value += (target - value) * w): 1 - (1 - w)^n, for 0 < w < 1; others as they are
    Keep,   ///< a share kept a frame (value = ref + (value - ref) * w): w^n, for 0 < w < 1; others as they are
    Amount, ///< an amount a frame: w * n
};

/// A site in the update: the instructions a cave takes over (at least 14 bytes, no jump into them past the first).
struct Site {
    const char* name;
    std::uint64_t at;    ///< patch address (image offset + 0x400000)
    const char* bytes;   ///< the displaced instructions, as 1.09 has them
};
std::span<const Site> Sites();

/// The constructor's second set of defaults (patch 0x183a8bb on, which always replaces the first): with the FPS++
/// tuning (Kyo's jump at patch 0x183dc96 in) it holds Kyo's 60 FPS values, which become the base's plus only what
/// he changed, as a 30 FPS frame's (the camera then converts them every frame like the game's).
enum class Fpp : std::uint8_t {
    Game,  ///< Kyo halved the game's value for 60: the game's, which the conversion takes to any rate
    Kyo,   ///< his value as it is: an angle, or a share of the frame's movement (the same at every rate)
    Decay, ///< his retuned weight for a 60 FPS frame, as a 30 FPS frame's: 1 - (1 - w)^2
    Keep,  ///< his retuned kept share for a 60 FPS frame, as a 30 FPS frame's: w^2
};
struct Field {
    std::uint64_t at;     ///< the immediate of mov dword [rbx + field], imm32 (patch address)
    std::uint32_t field;  ///< the camera's offset
    std::uint32_t game, kyo;
    Fpp fpp;
};
std::span<const Field> Fields();
std::uint32_t FppValue(const Field& field);

/// Site `index`'s cave for a guest image at `image`: the frame's 30 FPS frames read from `frames`, back to the
/// site's end (or, for the wall turn's build-up, to its branch's target) in the image. `displaced` is what the
/// image holds at the site (checked against Sites() by Check).
std::vector<std::uint8_t> Cave(std::size_t index, const std::uint8_t* image, const std::uint8_t* displaced,
                               const float* frames);

/// Whether every site holds 1.09's bytes and the constructor's second set the tuning's values (`fpp`: Kyo's;
/// otherwise the game's), with the debug menu's camera setup the game's. Nothing is written. `bad`: the first
/// patch address that is not as expected.
bool Check(const std::uint8_t* image, std::size_t size, bool fpp, std::uint64_t* bad);
/// After Check: every site's cave and, with FPS++, the constructor's values. All or none.
bool Install(std::uint8_t* image, std::size_t size, bool fpp);
/// When the start program left the camera to the engine (the frame timer on, camera not skipped) and Install did
/// not happen: the camera converted once for `rate` FPS where its values are still as the game or the list has them
/// (Original: the distance and chase-rate eases and the settings blend from 30; FPS++: Kyo's values from 60, the
/// ease he left and the blend from 30), so it does not run at 30 FPS weights. The number of values written.
int Fallback(std::uint8_t* image, std::size_t size, bool fpp, double rate);
/// Each frame, before the frame's tasks: the frame's length (the frame timer's step).
void Frame(double seconds);
/// The frame in 30 FPS frames, as the caves read it (1 at 1/30 s and longer).
float Frames();
} // namespace BbEngine::CameraRate

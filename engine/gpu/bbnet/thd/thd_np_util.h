// SPDX-License-Identifier: GPL-3.0-or-later
// The Hunter's Dream backend, 0.4.6 beta 14: the NpId compares and the score library's sanitize call,
// as droogie/bbhost a317321 answers them (src/hle/system.cpp hle_np_cmp_online, hle_np_cmp_npid, from
// #217; src/hle/np_score.h sanitize_copy, from f990415). Header-only and free of engine headers, so
// tests/test_thd_np_util.cpp holds them to bbhost's cases.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace BbThd::NpUtil {

constexpr int kErrInvalidArg = static_cast<int>(0x80550003);        // the NP library's
constexpr int kErrNotMatch = static_cast<int>(0x80550609);          // SCE_NP_UTIL_ERROR_NOT_MATCH
constexpr int kScoreErrInvalidArg = static_cast<int>(0x80550704u);  // SCE_NP_COMMUNITY_ERROR_INVALID_ARGUMENT
constexpr std::size_t kSanitizeMaxLen = 255;                        // SCE_NP_SCORE_SANITIZE_COMMENT_MAXLEN

// sceNpCmpOnlineId: the name up to its terminator (16 bytes at most). 0 equal, NOT_MATCH otherwise.
inline int CompareOnlineId(const void* a, const void* b) {
    if (!a || !b) return kErrInvalidArg;
    return std::strncmp(static_cast<const char*>(a), static_cast<const char*>(b), 16) != 0 ? kErrNotMatch : 0;
}

// sceNpCmpNpId: the 36-byte SceNpId by its name, then opt (bytes 20-27) and reserved (28-35); what
// follows the name's terminator is not compared. A memcmp of all 36 bytes called the same player two
// different ones whenever two copies differed past the name, and the game then kept a second record
// and connection for that player (bbhost #217).
inline int CompareNpId(const void* a, const void* b) {
    if (!a || !b) return kErrInvalidArg;
    const auto* x = static_cast<const std::uint8_t*>(a);
    const auto* y = static_cast<const std::uint8_t*>(b);
    if (std::strncmp(reinterpret_cast<const char*>(x), reinterpret_cast<const char*>(y), 16) != 0) return kErrNotMatch;
    if (std::memcmp(x + 20, y + 20, 8) != 0) return kErrNotMatch; // opt
    if (std::memcmp(x + 28, y + 28, 8) != 0) return kErrNotMatch; // reserved
    return 0;
}

// sceNpScoreSanitizeComment's answer: `comment` copied into `out` (kSanitizeMaxLen + 1 bytes) as a
// server with no word filter answers, cut at kSanitizeMaxLen bytes but never inside a UTF-8
// character, and terminated. The game puts its own character's name through it and sends what comes
// back to the other players, who show it under Player Name Display "Name": an answer that left the
// buffer empty showed this player blank to everyone (bbhost f990415). 0, or the score library's
// invalid argument for a missing pointer (out untouched).
inline int SanitizeCopy(const char* comment, char* out) {
    if (!comment || !out) return kScoreErrInvalidArg;
    std::size_t n = 0;
    while (n < kSanitizeMaxLen && comment[n]) ++n;
    if (n == kSanitizeMaxLen && comment[n]) {
        // Cut inside the text: back over the continuation bytes (10xxxxxx) at the cut to the lead
        // byte of the character they belong to, which goes too.
        std::size_t k = n;
        while (k > 0 && (static_cast<unsigned char>(comment[k]) & 0xc0) == 0x80) --k;
        n = k;
    }
    std::memcpy(out, comment, n);
    out[n] = '\0';
    return 0;
}

} // namespace BbThd::NpUtil

// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 14: the NpId compares and the score library's sanitize call (gpu/bbnet/thd/thd_np_util.h)
// as droogie/bbhost answers them. The sanitize cases are bbhost's tests/np_score_test.cpp (f990415):
// the game sends its own character's name through the call and the other players show what comes
// back under Player Name Display "Name", so the text has to come back whole, in UTF-8, cut only at
// the library's 255 bytes and then never inside a character.
#include "gpu/bbnet/thd/thd_np_util.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace BbThd::NpUtil;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

// SceNpId: SceNpOnlineId {char data[16]; char term; char dummy[3]} (20 bytes), opt[8], reserved[8].
struct NpId {
    char data[16];
    char term;
    char dummy[3];
    unsigned char opt[8];
    unsigned char reserved[8];
};
static_assert(sizeof(NpId) == 36);

NpId MakeId(const char* name) {
    NpId id;
    std::memset(&id, 0, sizeof id);
    const std::size_t n = std::strlen(name);
    std::memcpy(id.data, name, n < sizeof id.data ? n : sizeof id.data);
    return id;
}

void compares() {
    const NpId a = MakeId("Hunter"), b = MakeId("Hunter"), c = MakeId("Hunters");
    check(CompareOnlineId(&a, &b) == 0, "the same name: equal");
    check(CompareOnlineId(&a, &c) == kErrNotMatch, "another name: NOT_MATCH");
    check(CompareNpId(&a, &b) == 0, "the same NpId: equal");
    check(CompareNpId(&a, &c) == kErrNotMatch, "another name's NpId: NOT_MATCH");

    // What follows the name's terminator is not compared (bbhost #217): the same player, two copies.
    NpId d = b;
    d.data[10] = 'x'; // past "Hunter"'s terminator
    d.dummy[1] = 7;
    check(CompareNpId(&a, &d) == 0, "bytes past the name's terminator are not compared");
    check(CompareOnlineId(&a, &d) == 0, "nor by the online id compare");

    NpId e = b;
    e.opt[3] = 1;
    check(CompareNpId(&a, &e) == kErrNotMatch, "another opt: NOT_MATCH");
    NpId f = b;
    f.reserved[7] = 1;
    check(CompareNpId(&a, &f) == kErrNotMatch, "another reserved: NOT_MATCH");

    // A name of all 16 characters (no terminator inside data): the 16 bytes decide.
    const NpId g = MakeId("ABCDEFGHIJKLMNOP"), h = MakeId("ABCDEFGHIJKLMNOP");
    NpId i = MakeId("ABCDEFGHIJKLMNOQ");
    check(CompareNpId(&g, &h) == 0, "a 16-character name: equal");
    check(CompareNpId(&g, &i) == kErrNotMatch, "a 16-character name differing in its last: NOT_MATCH");
    i = h;
    i.term = 'Z';
    check(CompareNpId(&g, &i) == 0, "the terminator byte after 16 characters is not compared");

    check(CompareOnlineId(nullptr, &a) == kErrInvalidArg && CompareOnlineId(&a, nullptr) == kErrInvalidArg,
          "a missing online id: invalid argument");
    check(CompareNpId(nullptr, &a) == kErrInvalidArg && CompareNpId(&a, nullptr) == kErrInvalidArg,
          "a missing NpId: invalid argument");
    check(kErrNotMatch == static_cast<int>(0x80550609u), "SCE_NP_UTIL_ERROR_NOT_MATCH");
}

// The game's call: a 256-byte buffer (zeroed by the game; filled with a marker here, so an answer
// that leaves it alone cannot pass by luck).
struct Out {
    char buf[kSanitizeMaxLen + 1 + 16];
    Out() { std::memset(buf, 0x5a, sizeof buf); }
    std::string text() const {
        const void* nul = std::memchr(buf, 0, kSanitizeMaxLen + 1);
        return std::string(buf, nul ? static_cast<const char*>(nul) - buf : kSanitizeMaxLen + 1);
    }
    bool terminated() const { return std::memchr(buf, 0, kSanitizeMaxLen + 1) != nullptr; }
    bool tail_untouched() const {
        for (std::size_t i = kSanitizeMaxLen + 1; i < sizeof buf; ++i)
            if (buf[i] != 0x5a) return false;
        return true;
    }
};

std::string sanitized(const std::string& in, int* rc = nullptr) {
    Out o;
    const int r = SanitizeCopy(in.c_str(), o.buf);
    if (rc) *rc = r;
    check(o.terminated(), "the answer is NUL-terminated inside the 256 bytes");
    check(o.tail_untouched(), "nothing is written past the 256 bytes");
    return o.text();
}

bool whole_utf8(const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!len || i + len > s.size()) return false;
        for (std::size_t k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xc0) != 0x80) return false;
        i += len;
    }
    return true;
}

void names() {
    int rc = -1;
    check(sanitized("Hunter", &rc) == "Hunter", "a character's name comes back as it went");
    check(rc == 0, "and the call succeeds");
    check(sanitized("Lady Maria of the Astral Clocktower") == "Lady Maria of the Astral Clocktower",
          "spaces and length kept");
    // The game converts the UTF-16 name to UTF-8 before the call and back after it.
    const std::string jp = "\xe7\x8b\xa9\xe4\xba\xba"; // a Japanese name
    check(sanitized(jp) == jp, "a name outside ASCII comes back byte for byte");
    const std::string mixed = "Gehrman \xf0\x9f\x8c\x99"; // a 4-byte character
    check(sanitized(mixed) == mixed, "a 4-byte character comes back whole");
    check(sanitized("").empty(), "an empty text comes back empty");
}

void limits() {
    // The game sends at most 254 bytes (it checks strlen + 1 <= 255 first).
    const std::string longest(254, 'a');
    check(sanitized(longest) == longest, "the game's longest text comes back whole");
    const std::string max(kSanitizeMaxLen, 'b');
    check(sanitized(max) == max, "255 bytes, the library's limit, come back whole");
    const std::string over(300, 'c');
    check(sanitized(over) == std::string(kSanitizeMaxLen, 'c'), "a longer text is cut at 255 bytes");
    // 253 ASCII bytes and a 3-byte character: the cut at 255 falls inside it.
    const std::string cut = std::string(253, 'd') + "\xe7\x8b\xa9" + "tail";
    const std::string got = sanitized(cut);
    check(got == std::string(253, 'd'), "a cut inside a character drops the whole character");
    check(whole_utf8(got), "the cut leaves whole UTF-8");
    // 252 bytes and a 3-byte character end exactly at 255: kept.
    const std::string edge = std::string(252, 'e') + "\xe4\xba\xba" + "more";
    check(sanitized(edge) == std::string(252, 'e') + "\xe4\xba\xba", "a character that ends at the limit is kept");
}

void bad_arguments() {
    Out o;
    check(SanitizeCopy(nullptr, o.buf) == kScoreErrInvalidArg, "no comment: invalid argument");
    check(static_cast<unsigned char>(o.buf[0]) == 0x5a, "and the buffer is left alone");
    check(SanitizeCopy("Hunter", nullptr) == kScoreErrInvalidArg, "no buffer: invalid argument");
    check(kScoreErrInvalidArg == static_cast<int>(0x80550704u), "SCE_NP_COMMUNITY_ERROR_INVALID_ARGUMENT");
}

} // namespace

int main() {
    compares();
    names();
    limits();
    bad_arguments();
    if (g_fail) {
        std::printf("thd-np-util-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("thd-np-util-test: ok\n");
    return 0;
}

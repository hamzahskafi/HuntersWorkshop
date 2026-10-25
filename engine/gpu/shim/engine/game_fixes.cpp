// SPDX-License-Identifier: GPL-3.0-or-later
// Fixes for the game's own bugs, in its code, each checked against the 1.09 bytes it changes. The
// switches (BB_GAME_FIXES, BB_GAME_FIXES_SKIP) are read here; the start program reads the same for
// its write (deflate, start/patches.cpp). Image offsets below; Binary Ninja's are 0x400000 higher.
#include "game_fixes.h"
#include "engine_hooks.h"
#include "engine_state.h"
#include "skip_list.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <string>
#include <vector>

namespace BbEngine::GameFixes {
namespace {
#define GUEST_ABI __attribute__((sysv_abi))

enum Fix { kJobLock, kJobNodes, kJobSlots, kBlurSamples, kFmodRing, kDeflate, kFixCount };
constexpr const char* kFixNames[kFixCount] = {"job_lock", "job_nodes", "job_slots", "blur_samples", "fmod_ring",
                                              "deflate"};

// The first few, then at powers of two.
bool Report(unsigned n) {
    return n <= 5 || (n & (n - 1)) == 0;
}

// ---- The blur kernel's base sample count ----------------------------------------------------------
// GPUTexUtil_GetRecursiveSampleParameters (Yebis, 0xbbc3e0): edi the base sample count, esi a mode.
// Its asserts (GPUMath.cpp: nBaseSamples >= 3): mode 0 at least 3; mode 1 at least 3 and odd;
// mode 2 at least 4 and even. Game data now and then gives 2: the game's _Assert ends it (bbport:
// exception 0xa0020007). Kyo's 60 FPS++ entry at 0xbbc40f, in HW's frame-rate table at every rate,
// jumps past the first assert, and with 2 the game then runs on into a loop (bbport 0.6: the guest
// stops submitting, a black screen). The count is raised at the entry to the smallest each mode
// accepts; a valid one is left alone. After deadinside28 (bbport 0.6, InstallKernelSampleClamp),
// which raised every count to 3: the fourth caller's mode 2 asserts on that.
constexpr std::size_t kBlurParameters = 0xbbc3e0;
// push rbp; mov rbp, rsp; push r15; push r14; push r13; push r12; push rbx; sub rsp, 0x38
constexpr std::uint8_t kBlurPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                          0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x38};
std::atomic<unsigned> g_blur_raised{0};

// saved[5] rdi (the count), saved[4] rsi (the mode).
GUEST_ABI std::int64_t BlurSamples(std::uint64_t, const std::uint64_t* saved) {
    const auto count = static_cast<std::int32_t>(saved[5]);
    const auto mode = static_cast<std::int32_t>(saved[4]);
    const std::int32_t least = mode == 2 ? 4 : 3;
    if (count >= least) return 0;
    const_cast<std::uint64_t*>(saved)[5] = static_cast<std::uint32_t>(least);
    const unsigned n = g_blur_raised.fetch_add(1) + 1;
    if (Report(n))
        host_log("Game fixes: a blur kernel's base sample count raised from %d to %d (mode %d; %u so far)", count,
                 least, mode, n);
    return 0;
}
} // namespace

bool Install(std::uint8_t* image, std::size_t size) {
    if (const char* off = std::getenv("BB_GAME_FIXES"); off && off[0] == '0') {
        host_log("Game fixes: off (BB_GAME_FIXES=0)");
        return false;
    }
    std::array<bool, kFixCount> on;
    on.fill(true);
    for (const std::string& name : SkipList("BB_GAME_FIXES_SKIP", kFixNames, on))
        host_log("Game fixes: no fix \"%s\" to skip", name.c_str());

    std::vector<Hook> hooks;
    std::vector<std::string> placed;
    const auto prepare = [&](Fix fix, std::size_t offset, std::span<const std::uint8_t> prologue, Callback callback,
                             const char* what) {
        if (!on[fix]) return;
        Hook hook;
        if (!Prepare(hook, image, size, offset, prologue, callback)) {
            host_log("Game fixes: %s left out: 0x%zx differs from 1.09", kFixNames[fix], offset);
            return;
        }
        hooks.push_back(hook);
        placed.emplace_back(what);
    };
    prepare(kBlurSamples, kBlurParameters, kBlurPrologue, BlurSamples, "blur kernel samples kept valid");

    Commit(hooks);
    std::string list;
    for (const std::string& what : placed) list += (list.empty() ? "" : ", ") + what;
    host_log("Game fixes: %s (BB_GAME_FIXES_SKIP leaves one out)", list.empty() ? "none" : list.c_str());
    return !placed.empty();
}
} // namespace BbEngine::GameFixes

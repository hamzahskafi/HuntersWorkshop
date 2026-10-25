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
#include <cstring>
#include <initializer_list>
#include <span>
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

// ---- Render job slots ------------------------------------------------------------------------------
// The render job runner's step (0x1067f60, runner in rdi) takes a slot from the game's pool into
// [runner + 0x120] and gives it back when the job succeeds (returning 0); its two error exits return
// 1 with the slot still held. Above 30 FPS Laurence's cinematic fails enough of them to empty the
// pool, and every thread needing a slot then sleeps for good. The step runs through Hook::original,
// and a failed one gives its slot back. The runner's two resets (0x1067960, 0x1067a40: a destructor
// and a reset, each clearing [runner + 0x120] in its first instructions) give back one still held
// first. The pool is the global 0x1057350 returns (0x54b8788); 0x1056990 is the game's give-back
// (pool, item), which leaves an item it does not hold alone. After deadinside28 (bbport 0.6,
// JobSlotFix and JobSlotDrop: there at the step's exit and the resets' stores).
constexpr std::size_t kRunnerStep = 0x1067f60, kRunnerDestroy = 0x1067960, kRunnerReset = 0x1067a40;
constexpr std::size_t kSlotPool = 0x54b8788, kGiveBack = 0x1056990;
constexpr std::uint64_t kHeldSlot = 0x120;
// push rbp; mov rbp, rsp; push r15; push r14; push r13; push r12; push rbx; sub rsp, 0x18
constexpr std::uint8_t kStepPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                          0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x18};
// push rbp; mov rbp, rsp; push r15; push r14; push r13; push r12; push rbx; push rax
constexpr std::uint8_t kResetPrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41,
                                           0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50};
std::uint8_t* g_image = nullptr;
std::uint64_t g_step = 0; // the game's step itself (Hook::original)
std::atomic<unsigned> g_slots_back{0};

void GiveSlotBack(std::uint64_t runner, const char* why) {
    auto* const held = reinterpret_cast<std::uint64_t*>(runner + kHeldSlot);
    const std::uint64_t item = *held;
    std::uint64_t pool = 0;
    std::memcpy(&pool, g_image + kSlotPool, sizeof pool);
    if (!item || !pool) return;
    Call(reinterpret_cast<std::uint64_t>(g_image + kGiveBack), pool, item);
    *held = 0;
    const unsigned n = g_slots_back.fetch_add(1) + 1;
    if (Report(n)) host_log("Game fixes: %s gave its render job slot back (%u so far)", why, n);
}

// The step itself, then its slot back after an error exit (result 1, which also comes before a slot
// is taken: nothing is held then). The stub returns the step's result (the return slot).
GUEST_ABI std::int64_t RunnerStep(std::uint64_t, const std::uint64_t* saved) {
    const std::int64_t result = Call(g_step, saved[5], saved[4], saved[3], saved[2], saved[1], saved[0]);
    if (static_cast<std::uint32_t>(result) == 1) GiveSlotBack(saved[5], "a failed render job");
    const_cast<std::uint64_t*>(saved)[-1] = static_cast<std::uint64_t>(result);
    return 1;
}

GUEST_ABI std::int64_t RunnerReset(std::uint64_t, const std::uint64_t* saved) {
    GiveSlotBack(saved[5], "a render job runner destroyed or reset with one");
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

    g_image = image;
    std::vector<Hook> hooks;
    std::vector<std::string> placed;
    struct Site {
        std::size_t offset;
        std::span<const std::uint8_t> prologue;
        Callback callback;
    };
    // A fix's hooks, all of them or none.
    const auto prepare = [&](Fix fix, std::initializer_list<Site> sites, const char* what) {
        if (!on[fix]) return false;
        const std::size_t first = hooks.size();
        for (const Site& site : sites) {
            Hook hook;
            if (!Prepare(hook, image, size, site.offset, site.prologue, site.callback)) {
                host_log("Game fixes: %s left out: 0x%zx differs from 1.09", kFixNames[fix], site.offset);
                for (std::size_t i = first; i < hooks.size(); ++i) Discard(hooks[i]);
                hooks.resize(first);
                return false;
            }
            hooks.push_back(hook);
        }
        placed.emplace_back(what);
        return true;
    };
    prepare(kBlurSamples, {{kBlurParameters, kBlurPrologue, BlurSamples}}, "blur kernel samples kept valid");
    if (prepare(kJobSlots,
                {{kRunnerStep, kStepPrologue, RunnerStep},
                 {kRunnerDestroy, kResetPrologue, RunnerReset},
                 {kRunnerReset, kResetPrologue, RunnerReset}},
                "render job slots given back")) {
        g_step = hooks[hooks.size() - 3].original;
    }

    Commit(hooks);
    std::string list;
    for (const std::string& what : placed) list += (list.empty() ? "" : ", ") + what;
    host_log("Game fixes: %s (BB_GAME_FIXES_SKIP leaves one out)", list.empty() ? "none" : list.c_str());
    return !placed.empty();
}
} // namespace BbEngine::GameFixes

// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 18 (shim/bbport_roles.h): the roles on Windows (or Wine). Threads name themselves as the
// engine's and the game's do; checked: each gets its role's priority, Background work runs at normal
// priority until the first frame and during a loading screen and at idle otherwise, a raise and the
// next lowering, an unknown name left alone, the audio rule by PS4 priority, BB_GPU_THREAD_PRIORITY=0
// keeping the chain at normal, and the Roles: line. Wine stores a thread's priority without applying
// it (no permission), which is what this reads back.
#include "bbport_roles.h"
#include "bbport_threads.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>

extern "C" std::uint64_t runtime_tsc_frequency(void) {
    return 3'000'000'000ull;
}
extern "C" int bbgpu_thread_named(const char* name, int ps4_priority);

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

/// Runs `body` on a thread of its own, named `name` (an engine thread: ps4 < 0).
void OnThread(const char* name, int ps4, const std::function<void()>& body) {
    std::thread([&] {
        if (ps4 < 0) {
            BbRoles::NamedEngine(name);
        } else {
            BbRoles::NamedGame(name, ps4);
        }
        body();
    }).join();
}

int Priority() {
    return GetThreadPriority(GetCurrentThread());
}

} // namespace

int main() {
    check(BbRoles::On(), "roles are on by default");
    OnThread("shadPS4:GpuCommandProcessor", -1, [] {
        check(Priority() == THREAD_PRIORITY_ABOVE_NORMAL, "the render chain runs above normal");
        BbThreads::MakeCritical(); // its old call: the role decides
        check(Priority() == THREAD_PRIORITY_ABOVE_NORMAL, "MakeCritical leaves the chain's role");
    });
    OnThread("bb:VkRecorder1", -1, [] { check(Priority() == THREAD_PRIORITY_ABOVE_NORMAL, "a recorder is chain"); });
    OnThread("bb:Copy2", -1, [] {
        BbThreads::MakeCritical(true);
        check(Priority() == THREAD_PRIORITY_NORMAL, "copy threads are Frame: normal, MakeCritical or not");
    });
    OnThread("bb:Pipeline0", -1, [] { check(Priority() == THREAD_PRIORITY_NORMAL, "compile threads are Frame"); });
    OnThread("bb:DrawPrep3", -1, [] {
        check(Priority() == THREAD_PRIORITY_IDLE, "draw preparation is Assist: idle");
        BbThreads::MakeBackground();
        check(Priority() == THREAD_PRIORITY_IDLE, "and stays so when it lowers itself");
    });
    OnThread("main", 700, [] { check(Priority() == THREAD_PRIORITY_NORMAL, "a game thread is Frame"); });
    OnThread("FMOD mixer", 256, [] {
        check(Priority() == THREAD_PRIORITY_NORMAL, "FMOD's threads: normal without Sound threads first");
    });
    OnThread("bb:SomethingNew", -1, [] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
        BbRoles::NamedEngine("bb:SomethingNew");
        check(Priority() == THREAD_PRIORITY_LOWEST, "a name without a role is left alone");
    });
    OnThread("shadPS4:PresentThread", -1, [] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        BbRoles::NamedEngine("shadPS4:PresentThread");
        check(Priority() == THREAD_PRIORITY_TIME_CRITICAL, "the flip thread keeps its own priority");
    });
    check(bbgpu_thread_named("bb:Prefetch", -1) == 1, "the runtime's bridge answers 1 with roles on");

    // Background work: normal while starting, idle after the first frame, normal again while loading;
    // raised while a frame waits on its job, back at its role at the next lowering.
    std::atomic<int> step{0};
    std::atomic<int> seen[8] = {};
    void* handle = nullptr;
    std::thread burst([&] {
        BbRoles::NamedEngine("bb:PipelineBurst0");
        handle = BbThreads::SelfHandle();
        seen[0] = Priority();
        step = 1;
        for (int at = 1; at < 6; ++at) {
            while (step.load() != at * 2) Sleep(1);
            if (at == 3) BbThreads::MakeBackground(); // the next job: back at its role
            seen[at] = Priority();
            step = at * 2 + 1;
        }
    });
    const auto next = [&](int at) {
        while (step.load() != at * 2 - 1) Sleep(1);
        step = at * 2;
        while (step.load() != at * 2 + 1) Sleep(1);
    };
    while (step.load() != 1) Sleep(1);
    check(seen[0] == THREAD_PRIORITY_NORMAL, "background work runs at normal before the first frame");
    BbRoles::FirstFrame();
    next(1);
    check(seen[1] == THREAD_PRIORITY_IDLE, "background work is idle after the first frame");
    check(BbThreads::SetPriority(handle, THREAD_PRIORITY_ABOVE_NORMAL), "a raise goes through the role");
    next(2);
    check(seen[2] == THREAD_PRIORITY_ABOVE_NORMAL, "raised while a frame waits on its job");
    next(3);
    check(seen[3] == THREAD_PRIORITY_IDLE, "back at its role when it lowers itself for the next job");
    BbRoles::SetLoading(true);
    next(4);
    check(seen[4] == THREAD_PRIORITY_NORMAL, "background work at normal during a loading screen");
    BbRoles::SetLoading(false);
    next(5);
    check(seen[5] == THREAD_PRIORITY_IDLE, "and idle again when play resumes");
    burst.join();

    BbRoles::Report(1.0);
    if (g_fail) {
        std::printf("roles-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("roles-test: ok\n");
    return 0;
}

// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 20 (shim/bbport_role_lock.h, docs/chain-waits.md item 3): the lock that carries
// priority, with real threads. A draw preparation thread (Assist: idle) holds a lock the GPU command
// thread (Chain: above normal) waits for. Checked: the wait is counted for the Roles: line by the
// lock's name; on Windows (or Wine, which stores a priority without applying it) the holder runs at
// the waiter's priority for the rest of its hold and goes back to its role when it lets go, also
// when it held a second lock (it keeps the raise until the last); a holder of a higher role is not
// counted; the shared variant raises a writer for a reader; the runtime's C hooks do the same; many
// threads of mixed roles on one lock keep it exclusive and leave no thread raised.
#include "bbport_role_lock.h"
#include "bbport_roles.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" std::uint64_t runtime_tsc_frequency(void) {
    return 3'000'000'000ull;
}
extern "C" void* bbgpu_role_locked(void);
extern "C" void bbgpu_role_unlocked(void);
extern "C" void bbgpu_role_contended(const char* lock, void* holder);
extern "C" void bbgpu_role_acquired(void);

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

constexpr int kNoPriority = -100;
int Priority() {
#ifdef _WIN32
    return GetThreadPriority(GetCurrentThread());
#else
    return kNoPriority;
#endif
}
#ifdef _WIN32
constexpr int kIdle = THREAD_PRIORITY_IDLE, kAbove = THREAD_PRIORITY_ABOVE_NORMAL;
#else
constexpr int kIdle = kNoPriority, kAbove = kNoPriority;
#endif

/// Waits up to 2 s for the calling thread's priority to read `want` (at once off Windows).
bool SeePriority(int want) {
    for (int i = 0; i < 2000; ++i) {
        if (Priority() == want) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

std::string Inversions() {
    char out[512];
    BbRoles::ReportLockInversions(1.0, out, sizeof(out));
    return out;
}

void Until(const std::atomic<int>& step, int at) {
    while (step.load() < at) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

/// An Assist thread holds `held` (and `first`, taken before it, when given); the chain waits on
/// `held`. Returns what the holder saw: raised while holding, back at its role after.
struct Seen {
    bool raised = false, kept_after_first = true, back = false;
};
template <class Lock, class Wait>
Seen Inversion(Lock& held, BbRoles::Mutex* first, Wait wait) {
    std::atomic<int> step{0};
    Seen seen;
    std::thread holder([&] {
        BbRoles::NamedEngine("bb:DrawPrep5");
        if (first) {
            first->lock();
        }
        held.lock();
        step = 1;
        Until(step, 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(20)); // the waiter is blocked by now
        seen.raised = SeePriority(kAbove); // the waiter raised it
        if (first) {
            first->unlock(); // still holding `held`: keeps the raise
            seen.kept_after_first = SeePriority(kAbove);
        }
        held.unlock();
        seen.back = SeePriority(kIdle);
    });
    std::thread waiter([&] {
        BbRoles::NamedEngine("shadPS4:GpuCommandProcessor");
        Until(step, 1);
        step = 2;
        wait();
    });
    holder.join();
    waiter.join();
    return seen;
}

} // namespace

int main() {
    check(BbRoles::On(), "roles are on by default");
    Inversions(); // a fresh window

    // An uncontended lock costs nothing and counts nothing.
    BbRoles::Mutex quiet{"test quiet"};
    std::thread([&] {
        BbRoles::NamedEngine("shadPS4:GpuCommandProcessor");
        for (int i = 0; i < 1000; ++i) {
            quiet.lock();
            quiet.unlock();
        }
    }).join();
    check(Inversions().empty(), "an uncontended lock counts nothing");

    // The chain waits on an Assist holder: counted, and the holder raised for its hold.
    BbRoles::Mutex lock{"test lock"};
    Seen seen = Inversion(lock, nullptr, [&] {
        lock.lock();
        lock.unlock();
    });
    check(seen.raised, "the holder runs at the waiter's priority while it holds the lock");
    check(seen.back, "the holder goes back to its role when it lets go");
    std::string line = Inversions();
    std::printf("  waited on lower roles:%s\n", line.c_str());
    check(line.find("test lock") != std::string::npos, "the wait is counted by the lock's name");

    // Two locks held: a raise for the second lasts until the holder lets go of it.
    BbRoles::Mutex a{"test first"}, b{"test second"};
    seen = Inversion(b, &a, [&] {
        b.lock();
        b.unlock();
    });
    check(seen.raised && seen.kept_after_first, "a holder keeps the raise until it lets go of the lock waited on");
    check(seen.back, "and goes back after");
    line = Inversions();
    check(line.find("test second") != std::string::npos && line.find("test first") == std::string::npos,
          "only the lock waited on is counted");

    // The shared variant: a reader behind an Assist writer raises it.
    BbRoles::SharedMutex shared{"test shared"};
    seen = Inversion(shared, nullptr, [&] {
        shared.lock_shared();
        shared.unlock_shared();
    });
    check(seen.raised && seen.back, "a reader raises a writer of a lower role");
    check(Inversions().find("test shared") != std::string::npos, "the reader's wait is counted");

    // The runtime's C hooks around a lock of its own.
    struct CLock {
        std::mutex m;
        std::atomic<void*> holder{nullptr};
        void lock() {
            if (!m.try_lock()) {
                bbgpu_role_contended("test runtime", holder.load());
                m.lock();
                bbgpu_role_acquired();
            }
            holder.store(bbgpu_role_locked(), std::memory_order_release);
        }
        void unlock() {
            holder = nullptr;
            m.unlock();
            bbgpu_role_unlocked();
        }
    } c;
    seen = Inversion(c, nullptr, [&] {
        c.lock();
        c.unlock();
    });
    check(seen.raised && seen.back, "the C hooks raise the holder and restore it");
    check(Inversions().find("test runtime") != std::string::npos, "the C hooks count the wait");

    // A holder of a higher role: not counted.
    {
        std::atomic<int> step{0};
        std::thread holder([&] {
            BbRoles::NamedEngine("bb:DrawRec");
            lock.lock();
            step = 1;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            lock.unlock();
        });
        std::thread waiter([&] {
            BbRoles::NamedEngine("bb:DrawPrep6");
            Until(step, 1);
            lock.lock();
            lock.unlock();
        });
        holder.join();
        waiter.join();
        check(Inversions().empty(), "a wait on a higher role's holder is not counted");
    }

    // Many threads of mixed roles on one lock: exclusive, and none left raised.
    {
        BbRoles::Mutex busy{"test busy"};
        long long counter = 0;
        std::atomic<bool> inside{false};
        std::atomic<int> overlaps{0}, left_raised{0};
        const char* names[] = {"shadPS4:GpuCommandProcessor", "bb:DrawPrep0", "bb:DrawPrep1", "bb:Copy0",
                               "bb:DrawRec", "bb:Prefetch0"};
        std::vector<std::thread> threads;
        for (const char* name : names) {
            threads.emplace_back([&, name] {
                BbRoles::NamedEngine(name);
                const int own = Priority();
                for (int i = 0; i < 20000; ++i) {
                    busy.lock();
                    if (inside.exchange(true)) {
                        ++overlaps;
                    }
                    ++counter;
                    inside = false;
                    busy.unlock();
                }
                if (!SeePriority(own)) {
                    ++left_raised;
                }
            });
        }
        for (auto& t : threads) {
            t.join();
        }
        check(counter == 20000LL * 6 && overlaps.load() == 0, "the lock is exclusive under contention");
        check(left_raised.load() == 0, "no thread is left raised");
        std::printf("  mixed roles:%s\n", Inversions().c_str());
    }

    BbRoles::Report(1.0);
    if (g_fail) {
        std::printf("role-lock-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("role-lock-test: ok\n");
    return 0;
}

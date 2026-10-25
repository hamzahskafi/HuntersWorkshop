// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: when the game stops showing frames for BB_FREEZE_REPORT_S seconds (default 10; 0 turns
// it off) after its first one, the log gets every thread's call stack (unwound from the
// executable's and the system DLLs' unwind tables, as module+offset; guest code ends a stack at
// its first frame) and the GPU side's state, once per freeze. A freeze with every thread waiting on
// another (#53) is then named by the log alone. Windows only. Loading screens flip at the frame
// rate, a minimised window too: only a stopped game goes this long without one.
// Hunter's Workshop: BB_HITCH_REPORT_MS=<ms> reports short stalls the same way (HitchWatch below).
#include "bbport_freeze.h"
#include "bbport_platform.h"
#include "bbport_toggles.h"
#include "engine/loading.h"

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" void runtime_blocked_report(const char* prefix);

namespace BbFreeze {
namespace {
std::atomic<long long> last_flip_ms{0};
std::once_flag started;
std::mutex reporter_mutex;
std::function<void()> state_reporter;

long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string Describe(HANDLE thread) {
    PWSTR wide = nullptr;
    std::string name;
    if (SUCCEEDED(GetThreadDescription(thread, &wide)) && wide) {
        char text[128] = {};
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, sizeof(text) - 1, nullptr, nullptr);
        name = text;
        LocalFree(wide);
    }
    return name;
}

// The game's code, where the sampler (bbport_sampler.cpp) and the write fault reports name it as
// +offset too.
constexpr DWORD64 kGuestLo = 0x800000000ull, kGuestHi = 0x810000000ull;
bool Guest(DWORD64 address) {
    return address >= kGuestLo && address < kGuestHi;
}

std::string Module(DWORD64 address) {
    if (Guest(address)) {
        char text[32];
        std::snprintf(text, sizeof(text), "+%#llx", static_cast<unsigned long long>(address - kGuestLo));
        return text;
    }
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &module) ||
        !module) {
        char text[32];
        std::snprintf(text, sizeof(text), "%#llx", static_cast<unsigned long long>(address));
        return text;
    }
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(module, path, MAX_PATH);
    const wchar_t* base = wcsrchr(path, L'\\');
    char name[MAX_PATH] = {};
    WideCharToMultiByte(CP_UTF8, 0, base ? base + 1 : path, -1, name, sizeof(name) - 1, nullptr,
                        nullptr);
    char text[MAX_PATH + 32];
    std::snprintf(text, sizeof(text), "%s+%#llx", name,
                  static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
    return text;
}

constexpr int kMaxFrames = 24;

/// The thread's return addresses, innermost first. Nothing is allocated while it is suspended:
/// it may hold the heap's lock.
int Walk(HANDLE thread, DWORD64 (&frames)[kMaxFrames]) {
    if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
        return 0;
    }
    int count = 0;
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(thread, &context)) {
        while (count < kMaxFrames && context.Rip) {
            frames[count++] = context.Rip;
            if (Guest(context.Rip)) {
                // Game code has no unwind tables but keeps frame pointers (push rbp; mov rbp, rsp):
                // its callers are read from the rbp chain, each frame above the last. The kernel's
                // read, not a plain one: a stale rbp is no fault.
                DWORD64 rbp = context.Rbp;
                while (count < kMaxFrames) {
                    DWORD64 link[2] = {};
                    if ((rbp & 7) || rbp < context.Rsp || rbp - context.Rsp > (1u << 20) ||
                        !BbPlatform::ReadProcessMemory(reinterpret_cast<const void*>(rbp), link, sizeof(link)) ||
                        !Guest(link[1])) {
                        break;
                    }
                    frames[count++] = link[1];
                    if (link[0] <= rbp) {
                        break;
                    }
                    rbp = link[0];
                }
                break;
            }
            DWORD64 image_base = 0;
            const PRUNTIME_FUNCTION function =
                RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
            if (!function) {
                // A leaf function has no entry; anything else without one (guest code) ends it.
                if (count > 1 || !image_base) {
                    break;
                }
                context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
                context.Rsp += 8;
                continue;
            }
            void* handler_data = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context,
                             &handler_data, &establisher, nullptr);
        }
    }
    ResumeThread(thread);
    return count;
}

// Hunter's Workshop: a thread's call chain without unwinding it while it is suspended. A hitch is
// reported in a running game, where a suspended thread could hold a lock the unwinder takes (the
// loader's, while a DLL loads): its registers and the top of its stack are copied, the thread
// resumed at once, and the copy then read for return addresses - the values on the stack that
// follow a call instruction, in the game's code or a module's (now and then a stale one).
constexpr size_t kStackCopy = 16 * 1024;

struct CodeRange {
    DWORD64 lo, hi;
};
std::vector<CodeRange> g_code; // the modules, refreshed at each report, and the game's code

void RefreshCode() {
    g_code.clear();
    g_code.push_back({kGuestLo, kGuestHi});
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Module32FirstW(snapshot, &entry); ok; ok = Module32NextW(snapshot, &entry)) {
        const auto base = reinterpret_cast<DWORD64>(entry.modBaseAddr);
        g_code.push_back({base, base + entry.modBaseSize});
    }
    CloseHandle(snapshot);
}

/// A return address: in code, right after a call (rel32, through memory or a register).
bool AfterCall(DWORD64 address) {
    const bool code = std::any_of(g_code.begin(), g_code.end(),
                                  [&](const CodeRange& r) { return address >= r.lo + 8 && address < r.hi; });
    std::uint8_t b[8] = {};
    if (!code || !BbPlatform::ReadProcessMemory(reinterpret_cast<const void*>(address - 8), b, sizeof(b))) {
        return false;
    }
    // b[8 - n] is the first byte of an n-byte call ending at `address`.
    const auto modrm = [](std::uint8_t m, int mod) { return (m >> 6) == mod && ((m >> 3) & 7) == 2; };
    return b[3] == 0xe8 ||                                               // call rel32
           (b[2] == 0xff && (b[3] == 0x15 || modrm(b[3], 2))) ||         // call [rip/reg + d32]
           (b[6] == 0xff && modrm(b[7], 3)) ||                           // call reg
           (b[6] == 0xff && modrm(b[7], 0)) ||                           // call [reg]
           (b[5] == 0xff && modrm(b[6], 1)) ||                           // call [reg + d8]
           (b[5] == 0x41 && b[6] == 0xff && (modrm(b[7], 3) || modrm(b[7], 0))) ||
           (b[4] == 0xff && modrm(b[5], 1) && (b[6] & 7) == 4) ||        // call [rsp + d8]
           (b[4] == 0x41 && b[5] == 0xff && modrm(b[6], 1)) ||
           (b[1] == 0x41 && b[2] == 0xff && modrm(b[3], 2));
}

int Capture(HANDLE thread, DWORD64 (&frames)[kMaxFrames]) {
    static DWORD64 stack[kStackCopy / 8]; // the watcher's alone
    CONTEXT context{};
    context.ContextFlags = CONTEXT_CONTROL;
    if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
        return 0;
    }
    SIZE_T copied = 0;
    const bool got = GetThreadContext(thread, &context);
    if (got) {
        // Near the stack's base the copy is shorter (ReadProcessMemory: a partial copy).
        for (SIZE_T size = sizeof(stack); size >= 1024 && !copied; size /= 2) {
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(context.Rsp), stack,
                                   size, &copied) &&
                GetLastError() != ERROR_PARTIAL_COPY) {
                copied = 0;
            }
        }
    }
    ResumeThread(thread);
    if (!got) {
        return 0;
    }
    int count = 0;
    frames[count++] = context.Rip;
    for (SIZE_T i = 0; i < copied / 8 && count < kMaxFrames; ++i) {
        if (AfterCall(stack[i])) {
            frames[count++] = stack[i];
        }
    }
    return count;
}

/// One thread at a moment: its name, its stack and the CPU it has used so far, in cycles or (where
/// the system does not count them: Wine) in 100 ns units of CPU time.
struct Shot {
    DWORD id = 0;
    std::string name;
    std::string stack;
    ULONG64 used = 0;
    bool cycles = true;
};

/// Every thread of the process but this one; `running`: the game runs on (Capture, not Walk).
std::vector<Shot> Threads(bool running = false) {
    std::vector<Shot> shots;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return shots;
    }
    const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    std::vector<DWORD> ids;
    for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID == pid && entry.th32ThreadID != self) {
            ids.push_back(entry.th32ThreadID);
        }
    }
    CloseHandle(snapshot);
    for (const DWORD id : ids) {
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                       THREAD_QUERY_LIMITED_INFORMATION,
                                   FALSE, id);
        if (!thread) {
            continue;
        }
        Shot shot;
        shot.id = id;
        shot.name = Describe(thread);
        DWORD64 frames[kMaxFrames] = {};
        const int count = running ? Capture(thread, frames) : Walk(thread, frames);
        if (!QueryThreadCycleTime(thread, &shot.used)) {
            FILETIME created, exited, kernel, user;
            shot.cycles = false;
            if (GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
                shot.used = (ULONG64(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
                            (ULONG64(user.dwHighDateTime) << 32 | user.dwLowDateTime);
            }
        }
        CloseHandle(thread);
        for (int i = 0; i < count; ++i) {
            shot.stack += i ? " < " : "";
            shot.stack += Module(frames[i]);
        }
        shots.push_back(std::move(shot));
    }
    return shots;
}

std::string Label(const Shot& shot) {
    return (shot.name.empty() ? "" : shot.name + " ") + "#" + std::to_string(shot.id);
}

/// Threads with the same stack listed together, one line each, headed by `prefix`.
void PrintGrouped(const char* prefix, const std::vector<const Shot*>& shots) {
    std::map<std::string, std::vector<std::string>> stacks;
    for (const Shot* shot : shots) {
        stacks[shot->stack].push_back(Label(*shot));
    }
    for (const auto& [stack, names] : stacks) {
        std::string list;
        for (size_t i = 0; i < names.size(); ++i) {
            list += (i ? ", " : "") + names[i];
        }
        std::printf("%s: %s: %s\n", prefix, list.c_str(), stack.empty() ? "(no stack)" : stack.c_str());
    }
}

void Report(long long frozen_ms) {
    std::printf("== Freeze: no frame for %.1f s; the threads' stacks and the GPU's state follow "
                "(BB_FREEZE_REPORT_S=0 turns this off)\n",
                frozen_ms / 1000.0);
    {
        std::scoped_lock lock{reporter_mutex};
        if (state_reporter) {
            state_reporter();
        }
    }
    // The guest threads' waits: which mutex (and its owner), semaphore or condition variable.
    runtime_blocked_report("Freeze");
    const std::vector<Shot> shots = Threads();
    std::vector<const Shot*> all;
    for (const Shot& shot : shots) {
        all.push_back(&shot);
    }
    PrintGrouped("Freeze", all);
    std::printf("== Freeze: end of report\n");
    std::fflush(stdout);
}

// Hunter's Workshop: BB_HITCH_REPORT_MS=<ms> (off by default) - a frame that takes longer than
// that (a game that stopped for 50-120 ms, its main thread waiting on another thread) gets what
// every thread waits on and where it runs, twice 20 ms apart: the threads that used the CPU in
// between are the ones doing the work, listed first with their stacks at both moments. At most
// kHitchReports a session, kHitchGapMs apart, and none during the start, while a loading screen
// is up or in the first seconds after one (the world's first frames build what it shows, which
// the frame statistics already account for). The line after the report gives the whole stall,
// from the last frame to the next.
constexpr int kHitchReports = 12;
constexpr long long kHitchGapMs = 3000, kHitchSecondLookMs = 20;
constexpr long long kHitchQuietStartMs = 20000, kHitchQuietAfterLoadMs = 10000;
constexpr ULONG64 kBusyCycles = 2000000; // ~0.5-1 ms of CPU in the 20 ms
constexpr ULONG64 kBusyTime = 10000;      // 1 ms of CPU time, in 100 ns units

void HitchReport(int number, long long frozen_ms, long long limit_ms) {
    RefreshCode();
    const std::vector<Shot> first = Threads(true);
    std::printf("== Hitch %d: no frame for %lld ms (BB_HITCH_REPORT_MS=%lld); the game's waits, "
                "then the threads busy in the next %lld ms, then the rest\n",
                number, frozen_ms, limit_ms, kHitchSecondLookMs);
    runtime_blocked_report("Hitch");
    std::this_thread::sleep_for(std::chrono::milliseconds(kHitchSecondLookMs));
    const std::vector<Shot> second = Threads(true);
    std::map<DWORD, const Shot*> later;
    for (const Shot& shot : second) {
        later[shot.id] = &shot;
    }
    std::vector<std::pair<ULONG64, const Shot*>> busy;
    std::vector<const Shot*> rest;
    for (const Shot& shot : first) {
        const auto it = later.find(shot.id);
        const ULONG64 used = it == later.end() || it->second->cycles != shot.cycles ? 0 : it->second->used - shot.used;
        if (used >= (shot.cycles ? kBusyCycles : kBusyTime)) {
            busy.emplace_back(used, &shot);
        } else {
            rest.push_back(&shot);
        }
    }
    std::sort(busy.begin(), busy.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [used, shot] : busy) {
        const Shot* then = later[shot->id];
        if (shot->cycles) {
            std::printf("Hitch: busy %s (%.1f M cycles): %s\n", Label(*shot).c_str(), used / 1e6,
                        shot->stack.empty() ? "(no stack)" : shot->stack.c_str());
        } else {
            std::printf("Hitch: busy %s (%.1f ms of CPU): %s\n", Label(*shot).c_str(), used / 1e4,
                        shot->stack.empty() ? "(no stack)" : shot->stack.c_str());
        }
        if (then->stack != shot->stack) {
            std::printf("Hitch:   %lld ms later: %s\n", kHitchSecondLookMs,
                        then->stack.empty() ? "(no stack)" : then->stack.c_str());
        }
    }
    PrintGrouped("Hitch", rest);
    std::fflush(stdout);
}

void HitchWatch() {
    const char* env = std::getenv("BB_HITCH_REPORT_MS");
    const long long limit_ms = env ? std::atoll(env) : 0;
    if (limit_ms <= 0) {
        return;
    }
    std::printf("Hitch reports: a frame over %lld ms gets the threads' waits and stacks, at most %d "
                "(BB_HITCH_REPORT_MS)\n",
                limit_ms, kHitchReports);
    std::thread([limit_ms] {
        SetThreadDescription(GetCurrentThread(), L"bb:HitchWatch");
        int reports = 0;
        long long reported_flip = -1, last_report = -kHitchGapMs, quiet_until = NowMs() + kHitchQuietStartMs;
        bool open = false;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const long long flip = last_flip_ms.load(std::memory_order_relaxed), now = NowMs();
            if (BbEngine::Loading::ScreenUp()) {
                quiet_until = std::max(quiet_until, now + kHitchQuietAfterLoadMs);
            }
            if (open && flip != reported_flip) {
                std::printf("== Hitch %d: the next frame came %lld ms after the last one\n", reports,
                            flip - reported_flip);
                std::fflush(stdout);
                open = false;
            }
            if (open || reports >= kHitchReports || flip == reported_flip || now - flip < limit_ms ||
                now - last_report < kHitchGapMs || now < quiet_until) {
                continue;
            }
            reported_flip = flip;
            last_report = now;
            open = true;
            HitchReport(++reports, now - flip, limit_ms);
        }
    }).detach();
}

void Watch() {
    const char* env = std::getenv("BB_FREEZE_REPORT_S");
    const long long limit_ms = (env ? std::atoll(env) : 10) * 1000;
    if (limit_ms <= 0) {
        return;
    }
    std::thread([limit_ms] {
        SetThreadDescription(GetCurrentThread(), L"bb:FreezeWatch");
        // Once per freeze: a long stall that ends (a first start's shader compiles) and a real
        // freeze later both get theirs.
        bool reported = false;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            const long long frozen = NowMs() - last_flip_ms.load(std::memory_order_relaxed);
            if (frozen < limit_ms) {
                reported = false;
            } else if (!reported) {
                Report(frozen);
                reported = true;
            }
        }
    }).detach();
}
} // namespace

void NoteFlip() {
    last_flip_ms.store(NowMs(), std::memory_order_relaxed);
    std::call_once(started, [] {
        Watch();
        HitchWatch();
    });
}

void SetStateReporter(std::function<void()> reporter) {
    {
        std::scoped_lock lock{reporter_mutex};
        state_reporter = std::move(reporter);
    }
    // bbport: a game that never shows its first frame (a black screen at start, beta 9 reports)
    // gets the same report once, BB_FIRST_FRAME_REPORT_S (default 45; 0 off) after the start.
    static std::once_flag first_frame_watch;
    std::call_once(first_frame_watch, [] {
        const char* env = std::getenv("BB_FIRST_FRAME_REPORT_S");
        const long long limit_s = env ? std::atoll(env) : 45;
        if (limit_s <= 0) {
            return;
        }
        std::thread([limit_s] {
            SetThreadDescription(GetCurrentThread(), L"bb:FirstFrameWatch");
            // A stuck start can still flip (black frames, the overlay shows an FPS): "stuck" is
            // also next to nothing drawn (the boot logos alone draw thousands).
            std::this_thread::sleep_for(std::chrono::seconds(limit_s));
            const auto drawn = BbStats::draws.load(std::memory_order_relaxed);
            if (last_flip_ms.load(std::memory_order_relaxed) == 0 || drawn < 200) {
                std::printf("== Startup: %lld s after the start the game has drawn %llu things "
                            "(%s); where its threads are follows\n",
                            limit_s, static_cast<unsigned long long>(drawn),
                            last_flip_ms.load() ? "it flips frames" : "no frame yet");
                Report(limit_s * 1000);
            }
        }).detach();
    });
}
} // namespace BbFreeze

#else
namespace BbFreeze {
void NoteFlip() {}
void SetStateReporter(std::function<void()>) {}
} // namespace BbFreeze
#endif

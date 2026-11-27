// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 18: thread roles (bbport_roles.h; docs/handoffs-and-placement.md, section 1).

#include "bbport_roles.h"
#include "bbport_counter.h"
#include "bbport_role_lock.h"
#include "bbport_cpu_layout.h"
#include "bbport_threads.h"
#include "bbport_toggles.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <x86intrin.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#include <unistd.h>
#endif

extern "C" std::uint64_t runtime_tsc_frequency(void);

namespace BbRoles {
namespace {

constexpr const char* kRoleNames[] = {"unset", "pacing", "chain", "frame", "assist", "background", "audio", "service"};
constexpr unsigned kRoles = sizeof(kRoleNames) / sizeof(kRoleNames[0]);

struct Entry {
    const char* prefix;
    Role role;
};
// The engine's threads by the name each gives itself. Where one name starts another, the longer comes
// first (bb:PipelineBurst before bb:Pipeline).
constexpr Entry kEngine[] = {
    {"shadPS4:GpuCommandProcessor", Role::Chain}, // stage A
    {"bb:DrawRec", Role::Chain},                  // stage B
    {"bb:VkRecorder", Role::Chain},               // the Vulkan recording threads
    {"bb:GpuSignals", Role::Chain},               // fences the game waits on
    {"bb:Present", Role::Chain},                  // the swap thread
    {"shadPS4:PresentThread", Role::Pacing},      // the flip thread: its pacing sets its own timing
    {"bb:PipelineBurst", Role::Background},       // compile backlog (raised while a frame waits)
    {"bb:Pipeline", Role::Frame},                 // compiles a frame may wait on
    {"bb:Copy", Role::Frame},                     // host copies: above the game they took its cores
    {"bb:DrawAhead", Role::Frame},                // stage B's lookahead
    {"bb:GxPrepare", Role::Frame},
    {"bb:ReadySurfaces", Role::Frame},
    {"shadPS4:GpuSchedPriorityPendingOpsRunner", Role::Frame},
    {"bb:DrawPrep", Role::Assist},                // pipelines prepared ahead for stage A
    {"bb:DrawScan", Role::Assist},
    {"shadPS4:PipelineCacheIO", Role::Background},
    {"bb:Prefetch", Role::Background},            // file read-ahead
    {"bb:window", Role::Service},
    {"bb:kernel_service", Role::Service},
    {"bb:pgo", Role::Service},
    {"bb:HitchWatch", Role::Service},
    {"bb:FreezeWatch", Role::Service},
    {"bb:FirstFrameWatch", Role::Service},
    {"shadPS4:Av", Role::Service},                // video playback
};
/// The chain's busy threads (stage A and B, three recorders, signals, swap): the fastest class of
/// cores is its preference only while it has at least this many logical processors.
[[maybe_unused]] constexpr std::size_t kChainDemand = 6;
/// FMOD's threads: the game's PS4 priorities 256-300.
constexpr int kAudioLast = 300;

constexpr int kKeep = 0x7fffffff;
enum Qos : int { kQosKeep = -1, kQosHigh = 0, kQosEco = 1 };
enum Cores : int { kCoresKeep = -1, kCoresAny = 0, kCoresFastest = 1, kCoresEfficient = 2 };
enum CoreClass : unsigned { kFastest, kEfficient, kOther, kClasses };

struct Thread {
    std::mutex mutex; // what was applied, changed by the thread itself or a raise
    std::uint32_t id = 0;
    void* handle = nullptr;
    Role role = Role::Unset;
    std::string name;
    bool game = false;
    bool raised = false;
    int raised_priority = 0;
    int priority = kKeep;     // applied
    int qos = kQosKeep;       // applied
    int cores = kCoresKeep;   // applied
    std::uint64_t cycles = 0; // at the last report
    /// 0.4.6 beta 19: the game's main and render threads, shown on their own in the line.
    bool tracked = false;
    std::atomic<std::uint64_t> wakes[3]{}; // by core class; written by the thread itself
    /// 0.4.6 beta 20 (bbport_role_lock.h): raised by a waiter on a lock it holds; the raise's number.
    std::atomic<bool> lock_raised{false};
    int lock_priority = 0;
    int lock_saved = kKeep; ///< a role without its own priority (service): what it had before the raise
    std::uint64_t lock_generation = 0;
    bool alive = true; ///< records stay after a thread ends (a lock may still name its holder)
};

std::mutex g_mutex;
std::vector<std::unique_ptr<Thread>> g_threads;
thread_local Thread* t_self = nullptr;
std::atomic<bool> g_starting{true}; // the start up to the first frame
std::atomic<bool> g_screen{false};  // a loading screen is up
bool Loading() {
    return g_starting.load(std::memory_order_relaxed) || g_screen.load(std::memory_order_relaxed);
}
std::set<std::string> g_unknown;   // engine names without a role, logged once each
BbStats::Counter g_wakes[kRoles][kClasses];

bool AudioHigh() { // the launcher's "Sound threads first"
    static const bool on = [] {
        const char* env = std::getenv("BB_AUDIO_THREAD_PRIORITY");
        return env && env[0] == '1';
    }();
    return on;
}
[[maybe_unused]] bool ChainAbove() { // BB_GPU_THREAD_PRIORITY=0 (the launcher's experiment, safe mode): normal
    static const bool on = [] {
        const char* env = std::getenv("BB_GPU_THREAD_PRIORITY");
        return !(env && env[0] == '0');
    }();
    return on;
}

#ifdef _WIN32
struct Layout {
    std::vector<ULONG> fastest, efficient; // CPU set ids
    bool hybrid = false;
};
const Layout& Topology() {
    static const Layout layout = [] {
        Layout l;
        const BbCpu::Layout cpu = BbCpu::Read();
        if (!cpu.known || cpu.logical.empty()) {
            return l;
        }
        std::uint8_t lowest = 255, highest = 0;
        for (const auto& p : cpu.logical) {
            lowest = std::min(lowest, p.efficiency);
            highest = std::max(highest, p.efficiency);
        }
        l.hybrid = lowest != highest;
        for (const auto& p : cpu.logical) {
            if (p.efficiency == highest) {
                l.fastest.push_back(p.set_id);
            }
            if (p.efficiency == lowest) {
                l.efficient.push_back(p.set_id);
            }
        }
        return l;
    }();
    return layout;
}
/// The one rule: a role prefers a class of cores only on a CPU with more than one, and the chain only
/// while the fastest class holds its busy threads. On a CPU of one kind it changes nothing.
int ChainCores() {
    const Layout& l = Topology();
    return l.hybrid && l.fastest.size() >= kChainDemand ? kCoresFastest : kCoresKeep;
}
int EfficientCores() {
    return Topology().hybrid ? kCoresEfficient : kCoresKeep;
}

struct Want {
    int priority, qos, cores;
};
Want Mapping(const Thread& t) {
    const bool by_lock = t.lock_raised.load(std::memory_order_relaxed);
    if (t.raised || by_lock) {
        // A burst raise (Raise) and a lock's (bbport_role_lock.h) are kept apart: either can end first.
        const int burst = t.raised ? t.raised_priority : THREAD_PRIORITY_IDLE;
        return {by_lock ? std::max(burst, t.lock_priority) : burst, kQosHigh, kCoresAny};
    }
    switch (t.role) {
    case Role::Pacing:
        return {kKeep, kQosHigh, kCoresKeep};
    case Role::Chain:
        return {ChainAbove() ? THREAD_PRIORITY_ABOVE_NORMAL : THREAD_PRIORITY_NORMAL, kQosHigh, ChainCores()};
    case Role::Frame:
        return {THREAD_PRIORITY_NORMAL, kQosHigh, kCoresKeep};
    case Role::Assist:
        return {THREAD_PRIORITY_IDLE, kQosHigh, kCoresKeep};
    case Role::Background:
        if (Loading()) {
            return {THREAD_PRIORITY_NORMAL, kQosHigh, kCoresAny};
        }
        return {THREAD_PRIORITY_IDLE, kQosEco, EfficientCores()};
    case Role::Audio:
        return {AudioHigh() ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_NORMAL, kQosHigh, kCoresKeep};
    default:
        return {kKeep, kQosKeep, kCoresKeep};
    }
}

void SetQos(HANDLE h, int qos) {
    // THREAD_POWER_THROTTLING_STATE through SetThreadInformation(ThreadPowerThrottling), looked up at
    // run time. EXECUTION_SPEED in the control mask: the system's choice is overridden; in the state
    // mask: eco (efficiency cores, low clocks), clear: high.
    struct PowerThrottling {
        ULONG version, control_mask, state_mask;
    };
    using SetInfo = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
    static const auto set = reinterpret_cast<SetInfo>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadInformation")));
    constexpr int kThreadPowerThrottling = 3;
    constexpr ULONG kExecutionSpeed = 1;
    if (set) {
        PowerThrottling state{1, kExecutionSpeed, qos == kQosEco ? kExecutionSpeed : 0};
        set(h, kThreadPowerThrottling, &state, sizeof(state));
    }
}
void SetCores(HANDLE h, int cores) {
    using SetSelected = BOOL(WINAPI*)(HANDLE, const ULONG*, ULONG);
    static const auto set = reinterpret_cast<SetSelected>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadSelectedCpuSets")));
    if (!set) {
        return;
    }
    const Layout& l = Topology();
    const std::vector<ULONG>* ids = cores == kCoresFastest ? &l.fastest : cores == kCoresEfficient ? &l.efficient : nullptr;
    set(h, ids ? ids->data() : nullptr, ids ? static_cast<ULONG>(ids->size()) : 0);
}
/// The role's flags on thread `t` where they differ from what it has (t.mutex held).
void Apply(Thread& t) {
    const Want w = Mapping(t);
    HANDLE h = static_cast<HANDLE>(t.handle);
    if (w.priority != kKeep && w.priority != t.priority && SetThreadPriority(h, w.priority)) {
        t.priority = w.priority;
    }
    if (w.qos != kQosKeep && w.qos != t.qos) {
        SetQos(h, w.qos);
        t.qos = w.qos;
    }
    // "Any" undoes only a preference this module set.
    const bool clear = w.cores == kCoresAny && t.cores > kCoresAny;
    if ((w.cores > kCoresAny && w.cores != t.cores) || clear) {
        SetCores(h, w.cores);
        t.cores = w.cores;
    }
}
unsigned ClassOfCurrent() {
    if (!Topology().hybrid) {
        return kOther;
    }
    PROCESSOR_NUMBER n{};
    GetCurrentProcessorNumberEx(&n);
    return BbCpu::OnEfficiencyCore(static_cast<std::uint16_t>((unsigned(n.Group) << 8) | n.Number)) ? kEfficient
                                                                                                     : kFastest;
}
void* DuplicateSelf() {
    HANDLE handle = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &handle,
                         THREAD_QUERY_LIMITED_INFORMATION | THREAD_SET_INFORMATION |
                             THREAD_SET_LIMITED_INFORMATION,
                         FALSE, 0)) {
        return nullptr;
    }
    return handle;
}
std::uint32_t CurrentId() {
    return GetCurrentThreadId();
}
#else
// Linux: the threads keep what they set themselves (raising needs privileges here, and a thread
// lowered to SCHED_IDLE could not be raised again); roles name and count them.
void Apply(Thread&) {}
unsigned ClassOfCurrent() {
    return kOther;
}
void* DuplicateSelf() {
    return nullptr;
}
std::uint32_t CurrentId() {
    return static_cast<std::uint32_t>(gettid());
}
#endif

void Assign(const char* name, Role role, bool game) {
    Thread* t = t_self;
    if (!t) {
        auto made = std::make_unique<Thread>();
        made->id = CurrentId();
        made->handle = DuplicateSelf();
        t = made.get();
        std::scoped_lock lock{g_mutex};
        g_threads.push_back(std::move(made));
        t_self = t;
        LockDetail::self = t;
        LockDetail::raised_flag = &t->lock_raised;
    }
    std::scoped_lock lock{t->mutex};
    t->name = name;
    t->role = role;
    t->game = game;
    t->tracked = game && (t->name == "main" || t->name.rfind("GXRenderThread", 0) == 0);
    if (t->handle) {
        Apply(*t);
    }
}

} // namespace

bool On() {
    static const bool on = [] {
        const char* env = std::getenv("BB_THREAD_ROLES");
        const bool roles = !(env && env[0] == '0');
        if (!roles) {
            std::printf("Roles: off (BB_THREAD_ROLES=0): the thread priorities before 0.4.6 beta 18\n");
            return false;
        }
#ifdef _WIN32
        const Layout& l = Topology();
        if (!l.hybrid) {
            std::printf("Roles: on; one kind of core: no core preferences\n");
        } else if (ChainCores() == kCoresFastest) {
            std::printf("Roles: on; two kinds of core: the render chain prefers the fastest (%zu logical "
                        "processors), background work the efficient (%zu)\n",
                        l.fastest.size(), l.efficient.size());
        } else {
            std::printf("Roles: on; two kinds of core: the fastest hold %zu logical processors, fewer than the "
                        "render chain's %zu threads, so it runs on any; background work prefers the efficient "
                        "(%zu)\n",
                        l.fastest.size(), kChainDemand, l.efficient.size());
        }
#else
        std::printf("Roles: on (Linux: the threads keep the priorities they set themselves)\n");
#endif
        return true;
    }();
    return on;
}

void NamedEngine(const char* name) {
    if (!On() || !name) {
        return;
    }
    for (const Entry& e : kEngine) {
        if (std::strncmp(name, e.prefix, std::strlen(e.prefix)) == 0) {
            Assign(name, e.role, false);
            return;
        }
    }
    std::scoped_lock lock{g_mutex};
    if (g_unknown.insert(name).second) {
        std::printf("Roles: engine thread '%s' has no role (left as the system has it)\n", name);
    }
}

bool NamedGame(const char* name, int ps4_priority) {
    if (!On()) {
        return false;
    }
    Assign(name ? name : "", ps4_priority >= 0 && ps4_priority <= kAudioLast ? Role::Audio : Role::Frame, true);
    return true;
}

namespace {
void LoadingChanged() {
    std::scoped_lock lock{g_mutex};
    for (auto& t : g_threads) {
        std::scoped_lock thread_lock{t->mutex};
        if (t->role == Role::Background && t->handle) {
            Apply(*t);
        }
    }
}
} // namespace

void SetLoading(bool loading) {
    if (On() && g_screen.exchange(loading) != loading && !g_starting.load(std::memory_order_relaxed)) {
        LoadingChanged();
    }
}

void FirstFrame() {
    if (On() && g_starting.exchange(false) && !g_screen.load(std::memory_order_relaxed)) {
        LoadingChanged();
    }
}

bool LowerSelf() {
    Thread* t = t_self;
    if (!On() || !t || !t->handle || (t->role != Role::Background && t->role != Role::Assist)) {
        return false;
    }
    std::scoped_lock lock{t->mutex};
    t->raised = false;
    t->priority = kKeep; // it may have set its own since (normal at a queue's lock): applied anew
    Apply(*t);
    return true;
}

bool Raise(void* handle, int priority) {
#ifdef _WIN32
    if (!On() || !handle) {
        return false;
    }
    const DWORD id = GetThreadId(static_cast<HANDLE>(handle));
    std::scoped_lock lock{g_mutex};
    for (auto& t : g_threads) {
        if (t->id == id && t->handle) {
            std::scoped_lock thread_lock{t->mutex};
            t->raised = true;
            t->raised_priority = priority;
            t->priority = kKeep; // the thread may have set its own since: applied anew
            Apply(*t);
            return true;
        }
    }
#else
    (void)handle;
    (void)priority;
#endif
    return false;
}

bool EndRaise() {
    Thread* t = t_self;
    if (!On() || !t || !t->handle) {
        return false;
    }
    std::scoped_lock lock{t->mutex};
    if (t->raised) {
        t->raised = false;
        t->priority = kKeep; // applied anew
        Apply(*t);
    }
    return true;
}

bool RaiseOnWait() {
    static const bool on = [] {
        const char* env = std::getenv("BB_RAISE_ON_WAIT");
        return On() && !(env && env[0] == '0'); // without roles nothing puts a thread back
    }();
    return on;
}

// ---- 0.4.6 beta 20: the lock that carries priority (bbport_role_lock.h) ----------------------------

namespace LockDetail {
thread_local constinit void* self = nullptr;
thread_local constinit std::atomic<bool>* raised_flag = nullptr;
thread_local constinit unsigned depth = 0;

struct Stats {
    const char* name = nullptr;
    std::atomic<std::uint64_t> contended{0}, inversions{0}, ticks{0}, raises{0};
};

namespace {
constexpr std::size_t kMaxLocks = 96;
Stats g_locks[kMaxLocks];
std::atomic<std::size_t> g_lock_count{0};
std::mutex g_register;

bool InheritOn() {
    static const bool on = [] {
        const char* env = std::getenv("BB_LOCK_INHERIT");
        return !(env && env[0] == '0');
    }();
    return on;
}

/// How important a role's work is, for "a lower role holds what a higher one waits for" (t.mutex
/// held). A waiter that is raised (a burst thread a frame waits on, a holder of a lock the chain
/// waits on) works for the chain: it passes its raise on to whoever holds what it waits for.
int Rank(const Thread& t, bool as_waiter) {
    if (as_waiter && (t.raised || t.lock_raised.load(std::memory_order_relaxed))) {
        return 5;
    }
    switch (t.role) {
    case Role::Pacing:
        return 6;
    case Role::Chain:
        return 5;
    case Role::Audio:
        return AudioHigh() ? 4 : 3;
    case Role::Frame:
        return 3;
    case Role::Assist:
        return 2;
    case Role::Background:
        return Loading() ? 3 : 1;
    default:
        return 0;
    }
}

#ifdef _WIN32
/// The priority a holder is raised to for a waiter: the waiter's own (Pacing keeps the one it set
/// itself, so its holder gets the highest). (waiter.mutex held.)
int RaisePriorityFor(const Thread& waiter) {
    const int p = Mapping(waiter).priority;
    return p == kKeep ? THREAD_PRIORITY_HIGHEST : p;
}
#endif

/// Back from a lock's raise (t.mutex held).
void Restore(Thread& t) {
    t.lock_raised.store(false, std::memory_order_relaxed);
    t.priority = kKeep; // applied anew
    if (!t.handle) {
        return;
    }
#ifdef _WIN32
    if (Mapping(t).priority == kKeep && t.lock_saved != kKeep &&
        SetThreadPriority(static_cast<HANDLE>(t.handle), t.lock_saved)) {
        t.priority = t.lock_saved;
    }
#endif
    Apply(t);
}
} // namespace

Stats* Register(const char* name) {
    std::scoped_lock lock{g_register};
    const std::size_t n = g_lock_count.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < n; ++i) {
        if (g_locks[i].name == name || std::strcmp(g_locks[i].name, name) == 0) {
            return &g_locks[i]; // several locks of one kind (per object) count together
        }
    }
    if (n == kMaxLocks) {
        return &g_locks[kMaxLocks - 1];
    }
    g_locks[n].name = name;
    g_lock_count.store(n + 1, std::memory_order_release);
    return &g_locks[n];
}

std::uint64_t Contended(Stats* stats, void* holder, std::uint64_t& start) {
    stats->contended.fetch_add(1, std::memory_order_relaxed);
    auto* const waiter = static_cast<Thread*>(self);
    auto* const held = static_cast<Thread*>(holder);
    if (!waiter || !held || held == waiter) {
        return 0;
    }
    // One record's mutex at a time: the waiter's, then the holder's (a raise of the waiter by
    // another lock in between only makes it rank higher).
    int rank;
#ifdef _WIN32
    int priority;
#endif
    {
        std::scoped_lock lock{waiter->mutex};
        rank = Rank(*waiter, true);
#ifdef _WIN32
        priority = RaisePriorityFor(*waiter);
#endif
    }
    std::scoped_lock lock{held->mutex};
    if (Rank(*held, false) >= rank) {
        return 0;
    }
    stats->inversions.fetch_add(1, std::memory_order_relaxed);
    start = __rdtsc();
    if (!InheritOn() || !On()) {
        return 0;
    }
#ifdef _WIN32
    if (!held->handle || !held->alive) {
        return 0;
    }
    if (held->raised && held->raised_priority >= priority) {
        return 0; // a burst thread raised as high already
    }
    if (!held->lock_raised.load(std::memory_order_relaxed)) {
        const int had = GetThreadPriority(static_cast<HANDLE>(held->handle));
        held->lock_saved = had == THREAD_PRIORITY_ERROR_RETURN ? kKeep : had;
    } else if (held->lock_priority >= priority) {
        return 0; // raised as high already, by another waiter
    }
    held->lock_priority = priority;
    held->lock_raised.store(true, std::memory_order_relaxed);
    held->priority = kKeep; // applied anew
    Apply(*held);
    stats->raises.fetch_add(1, std::memory_order_relaxed);
    return ++held->lock_generation;
#else
    // Linux: threads keep their own priorities (Apply does nothing); counted only.
    return 0;
#endif
}

void Acquired(Stats* stats, void* holder, std::uint64_t generation, std::uint64_t start) {
    if (start) {
        stats->ticks.fetch_add(__rdtsc() - start, std::memory_order_relaxed);
    }
    if (generation) {
        // The holder has let go: if it hasn't gone back by itself yet, back now.
        auto* const held = static_cast<Thread*>(holder);
        std::scoped_lock lock{held->mutex};
        if (held->lock_raised.load(std::memory_order_relaxed) && held->lock_generation == generation) {
            Restore(*held);
        }
    }
}

void RestoreSelf() {
    auto* const t = static_cast<Thread*>(self);
    if (!t) {
        return;
    }
    std::scoped_lock lock{t->mutex};
    if (t->lock_raised.load(std::memory_order_relaxed)) {
        Restore(*t);
    }
}
} // namespace LockDetail

void ReportLockInversions(double seconds, char* out, std::size_t size) {
    out[0] = 0;
    if (seconds <= 0 || size == 0) {
        return;
    }
    const double hz = static_cast<double>(runtime_tsc_frequency());
    std::size_t used = 0;
    const std::size_t n = LockDetail::g_lock_count.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < n && used + 1 < size; ++i) {
        auto& l = LockDetail::g_locks[i];
        const std::uint64_t inversions = l.inversions.exchange(0, std::memory_order_relaxed);
        const std::uint64_t ticks = l.ticks.exchange(0, std::memory_order_relaxed);
        const std::uint64_t raises = l.raises.exchange(0, std::memory_order_relaxed);
        l.contended.exchange(0, std::memory_order_relaxed);
        if (!inversions) {
            continue;
        }
        const int w = std::snprintf(out + used, size - used, "%s %s %.0f/s %.2f ms/s (raised %llu)",
                                    used ? "," : "", l.name, inversions / seconds,
                                    hz > 0 ? ticks * 1e3 / hz / seconds : 0.0,
                                    static_cast<unsigned long long>(raises));
        if (w < 0) {
            break;
        }
        used = std::min(size - 1, used + static_cast<std::size_t>(w));
    }
}

void NoteWake() {
    if (!BbStats::enabled) {
        return;
    }
    Thread* t = t_self;
    const unsigned role = t ? static_cast<unsigned>(t->role) : 0;
    const unsigned where = ClassOfCurrent();
    g_wakes[role][where].Add();
    if (t && t->tracked) {
        t->wakes[where].fetch_add(1, std::memory_order_relaxed);
    }
}

void Report(double seconds) {
    if (!On() || seconds <= 0) {
        return;
    }
    struct Row {
        unsigned threads = 0;
        double cores = 0;
    } rows[kRoles];
    static bool listed = false;
    std::string game_names;
    std::string tracked; // " main 0.95 cores (woke on the fastest cores 41%);" ...
#ifdef _WIN32
    const double hz = static_cast<double>(runtime_tsc_frequency());
#endif
    {
        std::scoped_lock lock{g_mutex};
        for (auto it = g_threads.begin(); it != g_threads.end();) {
            Thread& t = **it;
            std::unique_lock thread_lock{t.mutex};
            const unsigned role = static_cast<unsigned>(t.role);
            bool alive = true;
#ifdef _WIN32
            if (t.handle) {
                ULONG64 cycles = 0;
                if (QueryThreadCycleTime(static_cast<HANDLE>(t.handle), &cycles) && hz > 0) {
                    const double cores =
                        static_cast<double>(cycles - std::min<ULONG64>(cycles, t.cycles)) / (hz * seconds);
                    rows[role].cores += cores;
                    t.cycles = cycles;
                    if (t.tracked) {
                        std::uint64_t w[3], all = 0;
                        for (unsigned c = 0; c < 3; ++c) {
                            all += w[c] = t.wakes[c].exchange(0, std::memory_order_relaxed);
                        }
                        char part[160];
                        std::snprintf(part, sizeof(part), " %s %.2f cores", t.name.c_str(), cores);
                        tracked += part;
                        if (all && (w[kFastest] || w[kEfficient])) {
                            std::snprintf(part, sizeof(part), " (woke on the fastest cores %.0f%%)",
                                          100.0 * w[kFastest] / all);
                            tracked += part;
                        }
                        tracked += ";";
                    }
                }
                DWORD code = 0;
                alive = !GetExitCodeThread(static_cast<HANDLE>(t.handle), &code) || code == STILL_ACTIVE;
            }
#endif
            if (!listed && t.game && game_names.find(t.name) == std::string::npos) {
                game_names += (game_names.empty() ? "" : ", ") + t.name;
            }
            if (!alive || !t.alive) {
                // Kept: a lock may still name it as its holder (bbport_role_lock.h).
                t.alive = false;
                ++it;
                continue;
            }
            ++rows[role].threads;
            ++it;
        }
    }
    if (!listed) {
        listed = true;
        std::printf("Roles: the game's threads (frame, FMOD's audio): %s\n", game_names.c_str());
    }
    std::string line = "Roles:";
    char part[160];
    for (unsigned r = 1; r < kRoles; ++r) {
        if (!rows[r].threads) {
            continue;
        }
        std::uint64_t wakes[kClasses];
        std::uint64_t total = 0;
        for (unsigned c = 0; c < kClasses; ++c) {
            total += wakes[c] = g_wakes[r][c].Exchange();
        }
        std::snprintf(part, sizeof(part), " %s %u thread%s %.2f cores", kRoleNames[r], rows[r].threads,
                      rows[r].threads == 1 ? "" : "s", rows[r].cores);
        line += part;
        if (total && (wakes[kFastest] || wakes[kEfficient])) {
            std::snprintf(part, sizeof(part), " (woke on the fastest cores %.0f%%)", 100.0 * wakes[kFastest] / total);
            line += part;
        }
        line += ";";
    }
    if (!tracked.empty()) {
        line += " of them" + tracked;
    }
    // 0.4.6 beta 20: waits on a lock held by a thread of a lower role (expected none on the chain).
    char inversions[512];
    ReportLockInversions(seconds, inversions, sizeof(inversions));
    line += inversions[0] ? std::string(" waited on lower roles:") + inversions + ";" : std::string(" waited on lower roles: none;");
    if (line.back() != ';') {
        return;
    }
    line.back() = '\n';
    std::fputs(line.c_str(), stdout);
}

unsigned PoolSize(Pool pool) {
    const unsigned available = BbThreads::Available();
    const auto env = [](const char* name, int lo, int hi, unsigned fallback) {
        if (const char* v = std::getenv(name); v && v[0]) {
            return static_cast<unsigned>(std::clamp(std::atoi(v), lo, hi));
        }
        return fallback;
    };
    switch (pool) {
    case Pool::Recording: // serial work cut in segments: 1 to 3
        return env("BB_VK_RECORD_THREADS", 1, 8, available >= 12 ? 3 : available >= 8 ? 2 : 1);
    case Pool::Copy: // a quarter of the hardware threads, 1 to 4
        return env("BB_COPY_THREADS", 0, 16, std::clamp(available / 4, 1u, 4u));
    case Pool::Compile: // a third, 1 to 4
        return env("BB_PIPELINE_THREADS", 1, 16, std::clamp(available / 3, 1u, 4u));
    case Pool::DrawPrep:
        // Idle-priority helpers that claim whole buffers. Below 16 hardware threads they still share
        // cores with the render chain and the game's threads: a quarter (8 threads 2, 12 threads 3).
        // From 16 on, 3/8, 6 to 8.
        return env("BB_PREP_WORKERS", 0, 16,
                   available < 16 ? std::max(available / 4, 1u) : std::clamp(available * 3 / 8, 6u, 8u));
    }
    return 1;
}

} // namespace BbRoles

/// The runtime (src/): a thread was named. ps4_priority >= 0: a game thread; < 0: one of the
/// runtime's own (by the engine's table). 1 when its role set its priority.
extern "C" int bbgpu_thread_named(const char* name, int ps4_priority) {
    if (ps4_priority >= 0) {
        return BbRoles::NamedGame(name, ps4_priority) ? 1 : 0;
    }
    BbRoles::NamedEngine(name);
    return BbRoles::On() ? 1 : 0;
}

/// BB_TEST_CPUS=<n> (beta 18, a measurement tool): the process keeps the first n logical processors,
/// fastest class first, so it lays itself out and schedules as a CPU with n hardware threads would.
/// Called at the start, before any thread or count is made.
extern "C" void bbgpu_test_cpus(void) {
    const char* env = std::getenv("BB_TEST_CPUS");
    const int n = env && env[0] ? std::atoi(env) : 0;
    if (n <= 0) {
        return;
    }
#ifdef _WIN32
    BbCpu::Layout cpu = BbCpu::Read();
    std::vector<BbCpu::Logical> order = cpu.logical;
    std::stable_sort(order.begin(), order.end(),
                     [](const BbCpu::Logical& a, const BbCpu::Logical& b) { return a.efficiency > b.efficiency; });
    DWORD_PTR mask = 0;
    int kept = 0;
    for (const auto& p : order) {
        if (kept == n) {
            break;
        }
        if ((p.processor >> 8) == 0 && (p.processor & 0xff) < sizeof(DWORD_PTR) * 8) {
            mask |= DWORD_PTR(1) << (p.processor & 0xff);
            ++kept;
        }
    }
    if (mask && SetProcessAffinityMask(GetCurrentProcess(), mask)) {
        std::printf("Roles: BB_TEST_CPUS=%d: the process runs on %d logical processors (a test)\n", n, kept);
    }
#else
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int i = 0; i < n && i < CPU_SETSIZE; ++i) {
        CPU_SET(i, &set);
    }
    if (sched_setaffinity(0, sizeof(set), &set) == 0) {
        std::printf("Roles: BB_TEST_CPUS=%d: the process runs on %d logical processors (a test)\n", n, n);
    }
#endif
}

// 0.4.6 beta 20: the runtime's own locks (src/, C) tell the roles who holds them.
extern "C" void* bbgpu_role_locked(void) {
    return BbRoles::LockDetail::Locked();
}
extern "C" void bbgpu_role_unlocked(void) {
    BbRoles::LockDetail::AfterUnlock();
}
namespace {
struct CWait {
    BbRoles::LockDetail::Stats* stats = nullptr;
    void* holder = nullptr;
    std::uint64_t generation = 0, start = 0;
};
thread_local CWait t_c_wait;
} // namespace
extern "C" void bbgpu_role_contended(const char* lock, void* holder) {
    auto& w = t_c_wait;
    w.stats = BbRoles::LockDetail::Register(lock);
    w.holder = holder;
    w.start = 0;
    w.generation = BbRoles::LockDetail::Contended(w.stats, holder, w.start);
}
extern "C" void bbgpu_role_acquired(void) {
    auto& w = t_c_wait;
    if (w.stats) {
        BbRoles::LockDetail::Acquired(w.stats, w.holder, w.generation, w.start);
        w = {};
    }
}

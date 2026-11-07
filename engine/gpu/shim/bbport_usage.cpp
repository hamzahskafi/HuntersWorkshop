// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_usage.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <tlhelp32.h>
#endif

namespace BbUsage {

#ifdef _WIN32
namespace {

std::uint64_t Ticks(const FILETIME& t) {
    return (std::uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}

/// The whole PC's CPU use since the last call (GetSystemTimes: kernel time includes idle).
float CpuPercent() {
    static std::uint64_t last_idle = 0, last_total = 0;
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) {
        return -1.0f;
    }
    const std::uint64_t i = Ticks(idle), total = Ticks(kernel) + Ticks(user);
    const std::uint64_t di = i - last_idle, dt = total - last_total;
    const bool first = last_total == 0;
    last_idle = i;
    last_total = total;
    if (first || dt == 0) {
        return -1.0f;
    }
    return std::clamp(100.0f * float(dt - std::min(di, dt)) / float(dt), 0.0f, 100.0f);
}

/// The GPU's 3D engine use: the sum of "GPU Engine(*engtype_3D)\Utilization Percentage" over
/// every process, as Task Manager's GPU column adds them. pdh.dll is loaded at run time (no link
/// dependency); without it, or without GPU counters (older drivers), -1.
class GpuCounter {
public:
    /// Task Manager's CPU figure ("Processor Information(_Total)\% Processor Utility", which also
    /// counts the clock speed), read by the last Percent(); -1 without it.
    float cpu = -1.0f;

    float Percent() {
        cpu = -1.0f;
        if (!Open()) {
            return -1.0f;
        }
        if (collect(query) != ERROR_SUCCESS) {
            return -1.0f;
        }
        PDH_FMT_COUNTERVALUE one{};
        if (cpu_counter && value && value(cpu_counter, PDH_FMT_DOUBLE, nullptr, &one) == ERROR_SUCCESS &&
            one.CStatus == ERROR_SUCCESS) {
            cpu = float(std::clamp(one.doubleValue, 0.0, 100.0));
        }
        DWORD bytes = 0, count = 0;
        PDH_STATUS status = array(counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr);
        if (status != PDH_MORE_DATA || bytes == 0) {
            return -1.0f; // the first collection has no rate yet
        }
        buffer.resize(bytes);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if (array(counter, PDH_FMT_DOUBLE, &bytes, &count, items) != ERROR_SUCCESS) {
            return -1.0f;
        }
        double sum = 0.0;
        for (DWORD k = 0; k < count; ++k) {
            if (items[k].FmtValue.CStatus == ERROR_SUCCESS) {
                sum += items[k].FmtValue.doubleValue;
            }
        }
        return float(std::clamp(sum, 0.0, 100.0));
    }

private:
    bool Open() {
        if (tried) {
            return query != nullptr;
        }
        tried = true;
        HMODULE pdh = LoadLibraryW(L"pdh.dll");
        if (!pdh) {
            return false;
        }
        const auto open = reinterpret_cast<decltype(&PdhOpenQueryW)>(GetProcAddress(pdh, "PdhOpenQueryW"));
        const auto add = reinterpret_cast<decltype(&PdhAddEnglishCounterW)>(
            GetProcAddress(pdh, "PdhAddEnglishCounterW"));
        collect = reinterpret_cast<decltype(&PdhCollectQueryData)>(GetProcAddress(pdh, "PdhCollectQueryData"));
        array = reinterpret_cast<decltype(&PdhGetFormattedCounterArrayW)>(
            GetProcAddress(pdh, "PdhGetFormattedCounterArrayW"));
        value = reinterpret_cast<decltype(&PdhGetFormattedCounterValue)>(
            GetProcAddress(pdh, "PdhGetFormattedCounterValue"));
        if (!open || !add || !collect || !array) {
            return false;
        }
        PDH_HQUERY q = nullptr;
        if (open(nullptr, 0, &q) != ERROR_SUCCESS) {
            return false;
        }
        if (add(q, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &counter) != ERROR_SUCCESS) {
            return false;
        }
        // Optional: without it the CPU figure comes from GetSystemTimes.
        if (add(q, L"\\Processor Information(_Total)\\% Processor Utility", 0, &cpu_counter) != ERROR_SUCCESS) {
            cpu_counter = nullptr;
        }
        query = q;
        collect(query); // the first sample: rates start with the next one
        return true;
    }

    bool tried = false;
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr, cpu_counter = nullptr;
    decltype(&PdhCollectQueryData) collect = nullptr;
    decltype(&PdhGetFormattedCounterArrayW) array = nullptr;
    decltype(&PdhGetFormattedCounterValue) value = nullptr;
    std::vector<unsigned char> buffer;
};

/// BB_THREAD_CPU=1: every 5 s, the game's busiest threads by CPU time (a core is 100%), in the log
/// - to see which waits spin. Started with the program; nothing without the variable.
void ThreadReportLoop() {
    using Describe = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    const auto describe = reinterpret_cast<Describe>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
    std::vector<std::pair<DWORD, std::uint64_t>> last;
    auto previous = std::chrono::steady_clock::now();
    for (;;) {
        Sleep(5000);
        const auto now = std::chrono::steady_clock::now();
        const double wall = std::chrono::duration<double>(now - previous).count() * 1e7; // 100 ns
        previous = now;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) continue;
        struct Row { double percent; std::wstring name; DWORD id; };
        std::vector<Row> rows;
        std::vector<std::pair<DWORD, std::uint64_t>> current;
        double total = 0.0;
        THREADENTRY32 entry{sizeof(entry)};
        for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
            if (entry.th32OwnerProcessID != GetCurrentProcessId()) continue;
            HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
            if (!thread) continue;
            FILETIME created, exited, kernel, user;
            if (GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
                const std::uint64_t time = Ticks(kernel) + Ticks(user);
                current.emplace_back(entry.th32ThreadID, time);
                const auto it = std::find_if(last.begin(), last.end(),
                                             [&](const auto& p) { return p.first == entry.th32ThreadID; });
                if (it != last.end() && wall > 0) {
                    const double percent = 100.0 * double(time - it->second) / wall;
                    total += percent;
                    std::wstring name;
                    PWSTR text = nullptr;
                    if (describe && SUCCEEDED(describe(thread, &text)) && text) {
                        name = text;
                        LocalFree(text);
                    }
                    rows.push_back({percent, name, entry.th32ThreadID});
                }
            }
            CloseHandle(thread);
        }
        CloseHandle(snapshot);
        last = std::move(current);
        if (rows.empty()) continue;
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.percent > b.percent; });
        std::printf("Thread CPU (5 s, 100%% = one core): all %.0f%%;", total);
        for (std::size_t k = 0; k < rows.size() && k < 12; ++k) {
            std::printf(" %ls %.0f%%%s", rows[k].name.empty() ? L"(unnamed)" : rows[k].name.c_str(),
                        rows[k].percent, k + 1 < rows.size() && k < 11 ? "," : "");
        }
        std::printf("\n");
        std::fflush(stdout);
    }
}

const bool thread_report = [] {
    const char* env = std::getenv("BB_THREAD_CPU");
    if (env && env[0] == '1') {
        std::thread(ThreadReportLoop).detach();
    }
    return true;
}();

/// 0.4.6 beta 5: each process's dedicated GPU memory on each adapter (the counter "GPU Process
/// Memory(*)/Dedicated Usage", instances "pid_<pid>_luid_<high>_<low>_phys_<n>"), raw byte counts.
class GpuMemoryCounter {
public:
    struct Item {
        DWORD pid;
        std::wstring luid;
        std::uint64_t bytes;
    };

    bool Read(std::vector<Item>& out) {
        out.clear();
        if (!Open() || collect(query) != ERROR_SUCCESS) {
            return false;
        }
        DWORD bytes = 0, count = 0;
        if (array(counter, PDH_FMT_LARGE, &bytes, &count, nullptr) != PDH_MORE_DATA || bytes == 0) {
            return false;
        }
        buffer.resize(bytes);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if (array(counter, PDH_FMT_LARGE, &bytes, &count, items) != ERROR_SUCCESS) {
            return false;
        }
        for (DWORD k = 0; k < count; ++k) {
            if (items[k].FmtValue.CStatus != ERROR_SUCCESS || !items[k].szName) {
                continue;
            }
            const wchar_t* name = items[k].szName;
            const wchar_t* pid = std::wcsstr(name, L"pid_");
            const wchar_t* luid = std::wcsstr(name, L"luid_");
            const wchar_t* phys = luid ? std::wcsstr(luid, L"_phys") : nullptr;
            if (!pid || !luid) {
                continue;
            }
            out.push_back({DWORD(std::wcstoul(pid + 4, nullptr, 10)),
                           std::wstring(luid + 5, phys ? phys : luid + std::wcslen(luid)),
                           std::uint64_t(std::max<LONGLONG>(items[k].FmtValue.largeValue, 0))});
        }
        return true;
    }

private:
    bool Open() {
        if (tried) {
            return query != nullptr;
        }
        tried = true;
        HMODULE pdh = LoadLibraryW(L"pdh.dll");
        if (!pdh) {
            return false;
        }
        const auto open = reinterpret_cast<decltype(&PdhOpenQueryW)>(GetProcAddress(pdh, "PdhOpenQueryW"));
        const auto add = reinterpret_cast<decltype(&PdhAddEnglishCounterW)>(
            GetProcAddress(pdh, "PdhAddEnglishCounterW"));
        collect = reinterpret_cast<decltype(&PdhCollectQueryData)>(GetProcAddress(pdh, "PdhCollectQueryData"));
        array = reinterpret_cast<decltype(&PdhGetFormattedCounterArrayW)>(
            GetProcAddress(pdh, "PdhGetFormattedCounterArrayW"));
        PDH_HQUERY q = nullptr;
        if (!open || !add || !collect || !array || open(nullptr, 0, &q) != ERROR_SUCCESS) {
            return false;
        }
        if (add(q, L"\\GPU Process Memory(*)\\Dedicated Usage", 0, &counter) != ERROR_SUCCESS) {
            return false;
        }
        query = q;
        return true;
    }

    bool tried = false;
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    decltype(&PdhCollectQueryData) collect = nullptr;
    decltype(&PdhGetFormattedCounterArrayW) array = nullptr;
    std::vector<unsigned char> buffer;
};

struct VramWho {
    std::mutex mutex;
    std::condition_variable wake;
    bool started = false, triggered = false;
    std::uint64_t budget = 0, used = 0, reported_budget = 0;
    std::chrono::steady_clock::time_point last_report{};
    const char* why = "";
};
VramWho vram_who;

std::string ProcessName(DWORD pid, const std::map<DWORD, std::string>& names) {
    const auto it = names.find(pid);
    return it != names.end() ? it->second : "pid " + std::to_string(pid);
}

void VramWhoLoop() {
    using Describe = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    if (const auto describe = reinterpret_cast<Describe>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"))) {
        describe(GetCurrentThread(), L"bb:VramWho");
    }
    GpuMemoryCounter counter;
    std::vector<GpuMemoryCounter::Item> items;
    std::map<DWORD, std::uint64_t> last;
    const DWORD self = GetCurrentProcessId();
    bool unavailable_logged = false;
    for (;;) {
        std::uint64_t budget = 0, used = 0;
        const char* why = "";
        {
            std::unique_lock lock{vram_who.mutex};
            vram_who.wake.wait_for(lock, std::chrono::seconds(60), [] { return vram_who.triggered; });
            why = vram_who.triggered ? vram_who.why : "once a minute";
            vram_who.triggered = false;
            budget = vram_who.budget;
            used = vram_who.used;
            vram_who.reported_budget = budget;
            vram_who.last_report = std::chrono::steady_clock::now();
        }
        if (!counter.Read(items) || items.empty()) {
            if (!unavailable_logged) {
                unavailable_logged = true;
                std::printf("VRAM by process: Windows' GPU memory counters are not available here\n");
                std::fflush(stdout);
            }
            continue;
        }
        // The adapter this game uses most (the one its Vulkan device is on).
        std::map<std::wstring, std::uint64_t> own_by_luid, all_by_luid;
        for (const auto& item : items) {
            all_by_luid[item.luid] += item.bytes;
            if (item.pid == self) {
                own_by_luid[item.luid] += item.bytes;
            }
        }
        std::wstring luid;
        std::uint64_t best = 0;
        for (const auto& [l, bytes] : own_by_luid) {
            if (bytes >= best) {
                best = bytes;
                luid = l;
            }
        }
        if (luid.empty()) {
            continue; // the game holds nothing yet
        }
        std::map<DWORD, std::uint64_t> by_pid;
        for (const auto& item : items) {
            if (item.luid == luid) {
                by_pid[item.pid] += item.bytes;
            }
        }
        std::map<DWORD, std::string> names;
        if (HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{sizeof(entry)};
            for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
                if (!by_pid.contains(entry.th32ProcessID)) {
                    continue;
                }
                char name[260];
                const int n = WideCharToMultiByte(CP_UTF8, 0, entry.szExeFile, -1, name, sizeof(name), nullptr, nullptr);
                names[entry.th32ProcessID] = n > 0 ? name : "?";
            }
            CloseHandle(snapshot);
        }
        std::vector<std::pair<std::uint64_t, DWORD>> others;
        std::vector<std::pair<std::int64_t, DWORD>> grown;
        std::uint64_t others_total = 0;
        for (const auto& [pid, bytes] : by_pid) {
            if (pid != self) {
                others_total += bytes;
                if (bytes >= (1ull << 20)) {
                    others.emplace_back(bytes, pid);
                }
            }
            const auto it = last.find(pid);
            const std::int64_t delta = std::int64_t(bytes) - std::int64_t(it != last.end() ? it->second : 0);
            if (!last.empty() && (delta >= std::int64_t(32ull << 20) || delta <= -std::int64_t(32ull << 20))) {
                grown.emplace_back(delta, pid);
            }
        }
        for (const auto& [pid, bytes] : last) {
            if (!by_pid.contains(pid) && bytes >= (32ull << 20)) {
                grown.emplace_back(-std::int64_t(bytes), pid); // ended
            }
        }
        std::sort(others.rbegin(), others.rend());
        std::sort(grown.begin(), grown.end(), [](const auto& a, const auto& b) {
            return (a.first < 0 ? -a.first : a.first) > (b.first < 0 ? -b.first : b.first);
        });
        auto mib = [](std::uint64_t bytes) { return (unsigned long long)(bytes >> 20); };
        std::string line;
        for (std::size_t k = 0; k < others.size() && k < 8; ++k) {
            line += (k ? ", " : "") + ProcessName(others[k].second, names) + " " +
                    std::to_string(mib(others[k].first)) + " MiB";
        }
        std::string change;
        for (std::size_t k = 0; k < grown.size() && k < 6; ++k) {
            const std::int64_t d = grown[k].first;
            change += (k ? ", " : "") + (grown[k].second == self ? std::string("this game") : ProcessName(grown[k].second, names)) +
                      (d >= 0 ? " +" : " -") + std::to_string((d >= 0 ? d : -d) >> 20) + " MiB";
        }
        const auto own = by_pid.find(self);
        std::printf("VRAM by process (%s; budget %llu MiB, the driver's count of this game's Vulkan memory %llu "
                    "MiB): this game %llu MiB, others %llu MiB%s%s%s%s\n",
                    why, mib(budget), mib(used), mib(own != by_pid.end() ? own->second : 0), mib(others_total),
                    line.empty() ? "" : " (", line.c_str(), line.empty() ? "" : ")",
                    change.empty() ? "" : ("; since the last report: " + change).c_str());
        std::fflush(stdout);
        last = std::move(by_pid);
    }
}

} // namespace

void NoteVramBudget(unsigned long long budget, unsigned long long used) {
    std::scoped_lock lock{vram_who.mutex};
    vram_who.budget = budget;
    vram_who.used = used;
    if (!vram_who.started) {
        vram_who.started = true;
        vram_who.reported_budget = budget;
        vram_who.why = "at the start";
        vram_who.triggered = true;
        std::thread(VramWhoLoop).detach();
        return;
    }
    const std::uint64_t moved = budget > vram_who.reported_budget ? budget - vram_who.reported_budget
                                                                  : vram_who.reported_budget - budget;
    if (!vram_who.triggered && moved >= (256ull << 20) &&
        std::chrono::steady_clock::now() - vram_who.last_report >= std::chrono::seconds(10)) {
        vram_who.why = budget < vram_who.reported_budget ? "the budget fell 256 MiB or more"
                                                         : "the budget rose 256 MiB or more";
        vram_who.triggered = true;
        vram_who.wake.notify_one();
    }
}

Sample Get(bool cpu, bool gpu) {
    static Sample last;
    static auto next = std::chrono::steady_clock::time_point{};
    static GpuCounter counter;
    const auto now = std::chrono::steady_clock::now();
    if (now < next) {
        return last;
    }
    next = now + std::chrono::milliseconds(500);
    // The CPU figure is the share of time the processors were busy (GetSystemTimes), as Task
    // Manager shows it now. "% Processor Utility" also scales by the clock against the base
    // clock: a boosting CPU read 100% at 70% busy. BB_CPU_UTILITY=1: that counter instead.
    static const bool utility = [] {
        const char* env = std::getenv("BB_CPU_UTILITY");
        return env && env[0] == '1';
    }();
    const float gpu_now = counter.Percent();
    const float system_cpu = CpuPercent();
    if (cpu) {
        last.cpu = utility && counter.cpu >= 0.0f ? counter.cpu : system_cpu;
    }
    if (gpu) {
        last.gpu = gpu_now;
    }
    return last;
}
#else
Sample Get(bool, bool) {
    return {};
}

void NoteVramBudget(unsigned long long, unsigned long long) {}
#endif

} // namespace BbUsage

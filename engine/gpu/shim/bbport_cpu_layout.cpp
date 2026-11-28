// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 15 part 1: reading the processor's layout and steering a thread (bbport_cpu_layout.h).
// The CPU set calls are Windows 10's, looked up at run time (MinGW's headers do not declare them).

#include "bbport_cpu_layout.h"
#include "bbport_threads.h"

#ifdef _WIN32
#include <windows.h>

namespace BbCpu {

Layout Read() {
    Layout layout;
    layout.count = BbThreads::Available();
    using GetSets = BOOL(WINAPI*)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
    const auto get = reinterpret_cast<GetSets>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetSystemCpuSetInformation")));
    if (!get) {
        return layout;
    }
    ULONG length = 0;
    get(nullptr, 0, &length, GetCurrentProcess(), 0);
    if (length == 0) {
        return layout;
    }
    std::vector<unsigned char> buffer(length);
    if (!get(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()), length, &length, GetCurrentProcess(), 0)) {
        return layout;
    }
    DWORD_PTR process = 0, system = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &process, &system);
    std::vector<Logical> all, allowed;
    for (ULONG at = 0; at + sizeof(DWORD) * 2 <= length;) {
        const auto* info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + at);
        if (info->Size == 0) {
            break;
        }
        if (info->Type == CpuSetInformation) {
            const auto& set = info->CpuSet;
            const Logical l{set.Id, (std::uint32_t(set.Group) << 8) | set.CoreIndex, set.EfficiencyClass,
                            std::uint16_t((std::uint32_t(set.Group) << 8) | set.LogicalProcessorIndex)};
            all.push_back(l);
            // The affinity mask is the process's group's (one group up to 64 logical processors).
            if (set.Group == 0 && set.LogicalProcessorIndex < sizeof(DWORD_PTR) * 8 &&
                (process >> set.LogicalProcessorIndex) & 1) {
                allowed.push_back(l);
            }
        }
        at += info->Size;
    }
    layout.logical = allowed.empty() ? all : allowed;
    layout.known = !layout.logical.empty();
    return layout;
}

bool UnderWine() {
    static const bool wine =
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    return wine;
}

bool PrioritiesReversible(const char** why) {
    if (UnderWine()) {
        *why = "under Wine thread priorities apply only with a Linux limit or permission most installs lack";
        return false;
    }
    return true;
}

bool OnEfficiencyCore(std::uint16_t processor) {
    static const std::vector<std::uint16_t> efficient = [] {
        std::vector<std::uint16_t> out;
        const Layout layout = Read();
        if (!layout.known) {
            return out;
        }
        std::uint8_t lowest = 255, highest = 0;
        for (const auto& l : layout.logical) {
            lowest = std::min(lowest, l.efficiency);
            highest = std::max(highest, l.efficiency);
        }
        for (const auto& l : layout.logical) {
            if (lowest != highest && l.efficiency == lowest) {
                out.push_back(l.processor);
            }
        }
        return out;
    }();
    return std::find(efficient.begin(), efficient.end(), processor) != efficient.end();
}

bool SteerCurrentThread(const std::vector<std::uint32_t>& sets) {
    using SetSelected = BOOL(WINAPI*)(HANDLE, const ULONG*, ULONG);
    static const auto set_selected = reinterpret_cast<SetSelected>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadSelectedCpuSets")));
    if (!set_selected || sets.empty()) {
        return false;
    }
    std::vector<ULONG> ids(sets.begin(), sets.end());
    return set_selected(GetCurrentThread(), ids.data(), static_cast<ULONG>(ids.size())) != 0;
}

} // namespace BbCpu

#else

namespace BbCpu {

Layout Read() {
    Layout layout;
    layout.count = BbThreads::Available();
    return layout;
}

bool UnderWine() { return false; }

bool PrioritiesReversible(const char** why) {
    // SCHED_IDLE cannot be left without CAP_SYS_NICE or a raised RLIMIT_NICE.
    *why = "on Linux an idle thread cannot be raised again without privileges";
    return false;
}

bool SteerCurrentThread(const std::vector<std::uint32_t>&) { return false; }

bool OnEfficiencyCore(std::uint16_t) { return false; }

} // namespace BbCpu

#endif

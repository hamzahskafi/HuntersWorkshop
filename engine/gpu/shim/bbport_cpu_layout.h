// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 15 part 1 (docs/memory-model.md, beta 15 part 1): the processor's layout, read once at
// start, and how many burst compile threads it allows. Nothing is measured while the game runs:
// the system's scheduler hands free time to the burst threads (idle priority) and takes it back
// itself. What it cannot know is told: that they are spare work (their priority), that a frame
// waits on them (raised meanwhile, PipelineJobQueue::RaiseRunning), and that a thread beside a busy
// one on the same core slows it (at most one compile thread per core; on a CPU with efficiency
// cores, which have one thread each, the burst threads are kept there).
// The counting below is plain code (tests/test_pipeline_burst.cpp); reading the system and steering
// a thread are bbport_cpu_layout.cpp's.

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace BbCpu {

/// One logical processor this process may run on.
struct Logical {
    std::uint32_t set_id = 0;     ///< its CPU set (Windows), to steer a thread to it
    std::uint32_t core = 0;       ///< its physical core: logical processors sharing one share this
    std::uint8_t efficiency = 0;  ///< efficiency class: the higher, the faster (all equal on one kind)
};

struct Layout {
    std::vector<Logical> logical;
    bool known = false; ///< read from the system; else only the count (cores taken as half of it)
    unsigned count = 0; ///< logical processors when not known
};

struct Summary {
    unsigned logical = 0, cores = 0;
    bool known = false, hybrid = false;
    unsigned performance_cores = 0, performance_logical = 0; ///< on a hybrid CPU
    unsigned efficiency_cores = 0;                            ///< on a hybrid CPU
    std::vector<std::uint32_t> efficiency_sets;               ///< their CPU sets
};

inline Summary Summarize(const Layout& layout) {
    Summary s;
    s.known = layout.known && !layout.logical.empty();
    if (!s.known) {
        s.logical = std::max(1u, layout.count);
        s.cores = std::max(1u, (s.logical + 1) / 2);
        return s;
    }
    s.logical = static_cast<unsigned>(layout.logical.size());
    std::uint8_t lowest = 255, highest = 0;
    for (const Logical& l : layout.logical) {
        lowest = std::min(lowest, l.efficiency);
        highest = std::max(highest, l.efficiency);
    }
    s.hybrid = lowest != highest;
    std::vector<std::uint32_t> seen;
    for (const Logical& l : layout.logical) {
        const bool efficient = s.hybrid && l.efficiency == lowest;
        if (efficient) {
            s.efficiency_sets.push_back(l.set_id);
        } else if (s.hybrid) {
            ++s.performance_logical;
        }
        if (std::find(seen.begin(), seen.end(), l.core) != seen.end()) {
            continue;
        }
        seen.push_back(l.core);
        ++s.cores;
        if (s.hybrid) {
            ++(efficient ? s.efficiency_cores : s.performance_cores);
        }
    }
    return s;
}

struct BurstPlan {
    unsigned threads = 0;
    bool on_efficiency = false; ///< kept on the efficiency cores
    std::string why;            ///< why none, or that the count was forced
};

constexpr unsigned kBurstMax = 8, kBurstForcedMax = 16;

/// `standing`: the standing compile threads. `forced`: BB_PIPELINE_BURST (-1: not set).
/// `reversible`: a lowered thread priority can be raised again here (Windows itself; not under
/// Wine, where priorities apply only with a system limit or permission most installs lack, nor on
/// Linux without privileges). On a hybrid CPU they run on its efficiency cores (0.4.6: BB_CPU_SETS=0,
/// anywhere, went with the betas).
inline BurstPlan PlanBursts(const Summary& s, unsigned standing, int forced, bool reversible,
                            const char* not_reversible_why) {
    BurstPlan p;
    if (forced >= 0) {
        p.threads = std::min<unsigned>(static_cast<unsigned>(forced), kBurstForcedMax);
        p.why = forced == 0 ? "BB_PIPELINE_BURST=0" : "BB_PIPELINE_BURST";
    } else if (!reversible) {
        p.why = std::string{not_reversible_why} + "; BB_PIPELINE_BURST=N forces them";
        return p;
    } else if (s.hybrid) {
        p.threads = std::min(s.efficiency_cores, kBurstMax);
    } else {
        p.threads = s.cores > standing ? std::min(s.cores - standing, kBurstMax) : 0;
        if (p.threads == 0) {
            p.why = "no core beyond the standing compile threads";
        }
    }
    p.on_efficiency = p.threads != 0 && s.hybrid && !s.efficiency_sets.empty();
    return p;
}

/// The start line, e.g. "CPU: 14 cores, 20 logical: 6 performance (2 threads each), 8 efficiency;
/// compile threads 4, burst threads 8 on the efficiency cores, idle priority".
inline std::string Describe(const Summary& s, unsigned standing, const BurstPlan& p) {
    char head[160];
    if (!s.known) {
        std::snprintf(head, sizeof head, "CPU: %u logical processors (layout not read)", s.logical);
    } else if (s.hybrid) {
        const unsigned each = s.performance_cores ? s.performance_logical / s.performance_cores : 1;
        std::snprintf(head, sizeof head, "CPU: %u cores, %u logical: %u performance (%u thread%s each), %u efficiency",
                      s.cores, s.logical, s.performance_cores, each, each == 1 ? "" : "s", s.efficiency_cores);
    } else {
        const unsigned each = s.cores ? s.logical / s.cores : 1;
        std::snprintf(head, sizeof head, "CPU: %u cores, %u logical (%u thread%s each)", s.cores, s.logical, each,
                      each == 1 ? "" : "s");
    }
    std::string line = head;
    char tail[96];
    std::snprintf(tail, sizeof tail, "; compile threads %u, burst threads ", standing);
    line += tail;
    if (p.threads == 0) {
        line += "none (" + p.why + ")";
        return line;
    }
    line += std::to_string(p.threads);
    if (p.on_efficiency) {
        line += " on the efficiency cores";
    }
    line += ", idle priority";
    if (!p.why.empty()) {
        line += " (" + p.why + ")";
    }
    return line;
}

// bbport_cpu_layout.cpp:
/// The logical processors this process may run on (Windows: GetSystemCpuSetInformation within
/// the process's affinity; elsewhere the count only).
Layout Read();
/// Running under Wine (ntdll exports wine_get_version).
bool UnderWine();
/// Whether a thread lowered to idle priority can be raised again here, and if not, why.
bool PrioritiesReversible(const char** why);
/// The calling thread runs on these CPU sets only (Windows SetThreadSelectedCpuSets).
bool SteerCurrentThread(const std::vector<std::uint32_t>& sets);

} // namespace BbCpu

// SPDX-License-Identifier: GPL-2.0-or-later
// bbport BB_TIMELINE (bbport_timeline.h), 0.4.6 beta 18, engine id 132: the calling thread's tag
// (which thread, on which kind of core) and the runtime's guest waits (runtime_wait_note).

#include "bbport_timeline.h"
#include "bbport_cpu_layout.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace BbTimeline {

std::uint64_t ThreadTag() {
#ifdef _WIN32
    PROCESSOR_NUMBER number{};
    GetCurrentProcessorNumberEx(&number);
    const auto processor = std::uint16_t((std::uint32_t(number.Group) << 8) | number.Number);
    const std::uint64_t thread = GetCurrentThreadId();
#else
    const int cpu = sched_getcpu();
    const auto processor = std::uint16_t(cpu < 0 ? 0 : cpu);
    const std::uint64_t thread = std::uint64_t(syscall(SYS_gettid));
#endif
    const std::uint64_t efficient = BbCpu::OnEfficiencyCore(processor) ? 1 : 0;
    return thread << 32 | efficient << 16 | processor;
}

} // namespace BbTimeline

/// The runtime's guest waits (src/runtime_kernel.c, runtime_wait_note): kind 0 a condition variable,
/// 1 a mutex, 2 a semaphore, 3 a sleep; `ns` how long it took. Waits under 10 us are left out.
extern "C" void bbgpu_timeline_wait(int kind, std::uint64_t ns) {
    static const bool on = BbTimeline::On();
    if (!on || ns < 10000) {
        return;
    }
    const std::uint64_t tag = BbTimeline::ThreadTag() | (std::uint64_t(kind & 0xff) << 24);
    BbTimeline::Note(BbTimeline::GuestBlocked, ns / 1000, tag);
}

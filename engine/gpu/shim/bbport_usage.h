// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: CPU and GPU usage for the FPS counter (config.ini show_fps_cpu / show_fps_gpu).
#pragma once

namespace BbUsage {

/// The PC's CPU and GPU (3D engine) use in percent, as Task Manager shows them, measured over
/// about the last half second; -1 where it is not known (another OS, or no GPU counters).
/// Cheap to call every frame: it samples at most twice a second.
struct Sample {
    float cpu = -1.0f;
    float gpu = -1.0f;
};
Sample Get(bool cpu, bool gpu);

/// 0.4.6 beta 5: the VRAM budget (bytes, VK_EXT_memory_budget: the card less what others use) and
/// the game's use by the driver's count, once a second (the VRAM manager's measure). On Windows, when
/// the budget moves by 256 MiB or more since the last report (at most every 10 s) and once a minute,
/// a thread of its own logs who holds the card's memory: each process's dedicated GPU memory
/// ("GPU Process Memory" counters, Task Manager's), this game's against the driver's count of its
/// Vulkan memory, and who grew since. Elsewhere nothing.
void NoteVramBudget(unsigned long long budget, unsigned long long used);

} // namespace BbUsage

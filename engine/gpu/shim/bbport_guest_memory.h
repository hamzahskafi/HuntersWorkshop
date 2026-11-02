// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the game's memory imported for the GPU as it is: the game's GX region read in place
// (BB_GX_IN_PLACE, BufferCache::StartGxImport). Windows imports it (bbport_guest_memory_win.cpp);
// Linux does not yet (bbport_guest_memory.cpp).
#pragma once

#include <cstdint>
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class Instance;
}

namespace BbGuestMemory {
/// One range of direct memory imported for the GPU as it is: a buffer with `usage` and memory
/// over [phys, phys + size) (the game's GX region as an upload heap, BufferCache). With
/// eShaderDeviceAddress in `usage` the memory is allocated for device addresses and `address` is
/// the buffer's. Pins its pages: slow (about a second for 256 MiB), so not on a thread the game
/// waits for. False, with the step that failed, where the driver cannot (or on Linux, not yet).
bool ImportRange(const Vulkan::Instance& instance, std::uint64_t phys, std::uint64_t size,
                 vk::BufferUsageFlags usage, vk::Buffer& buffer, vk::DeviceMemory& memory,
                 vk::DeviceAddress& address, const char** failed_step);
/// Whether the arena (a sparse buffer) lists a memory type a host import may use: binding the
/// import into it (BufferCache::GxArenaBound) is then within the Vulkan spec. NVIDIA's lists VRAM
/// only and binds it anyway. Known after the first ImportRange; true where nothing was checked.
bool ArenaTakesImports();
} // namespace BbGuestMemory

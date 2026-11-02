// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_guest_memory.h"

namespace BbGuestMemory {
bool ImportRange(const Vulkan::Instance&, std::uint64_t, std::uint64_t, vk::BufferUsageFlags,
                 vk::Buffer&, vk::DeviceMemory&, vk::DeviceAddress&, const char** failed_step) {
    *failed_step = "Linux (the game's memory is not imported there yet)";
    return false;
}

bool ArenaTakesImports() {
    return true; // nothing imported into the arena here
}
} // namespace BbGuestMemory

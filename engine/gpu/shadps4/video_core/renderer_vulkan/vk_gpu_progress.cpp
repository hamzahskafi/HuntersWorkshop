// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 21: the GPU's progress word (vk_gpu_progress.h).

#include "video_core/renderer_vulkan/vk_gpu_progress.h"
#include "video_core/renderer_vulkan/vk_instance.h"

#include <vk_mem_alloc.h>

namespace Vulkan {

bool GpuProgress::Init(const Instance& instance) {
    if (word) {
        return true;
    }
    if (!instance.IsBufferMarkerSupported()) {
        return false;
    }
    // As the breadcrumbs' buffer: in host memory, which the CPU reads as plain memory. Kept for
    // the life of the process (the rasterizer's), as theirs.
    const VkBufferCreateInfo buffer_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 64,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VmaAllocationCreateInfo alloc_ci = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };
    VkBuffer handle{};
    VmaAllocation allocation{};
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(instance.GetAllocator(), &buffer_ci, &alloc_ci, &handle, &allocation,
                        &info) != VK_SUCCESS ||
        !info.pMappedData) {
        return false;
    }
    buffer = vk::Buffer{handle};
    word = static_cast<volatile u32*>(info.pMappedData);
    *word = 0;
    return true;
}

void GpuProgress::Write(vk::CommandBuffer cmdbuf, vk::Buffer buffer, u32 value) {
    cmdbuf.writeBufferMarkerAMD(vk::PipelineStageFlagBits::eBottomOfPipe, buffer, 0, value);
}

} // namespace Vulkan

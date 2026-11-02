// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the game's GX region imported for the GPU on Windows (BB_GX_IN_PLACE,
// BufferCache::StartGxImport). The game's memory is a pagefile section (src/win32_memory.c) the
// runtime also maps whole as its backing view; direct memory phys lives at backing + phys in every
// view of it. The GPU imports the region's part of that view (VK_EXT_external_memory_host) and
// then reads and writes the game's memory there in place, with no page protection, write faults
// or uploads.
#ifdef _WIN32
#include "bbport_guest_memory.h"

#include <cstdio>
#include <windows.h>
#include "video_core/renderer_vulkan/vk_instance.h"

extern "C" unsigned char* runtime_memory_backing(uint64_t* direct_size);
// src/win32_memory.c: BB_POOL_LAZY commits the pool's pages as they are mapped; an import pins
// pages, so the range is committed whole first (a no-op on a committed pool).
extern "C" int win_mem_commit(uint64_t phys, uint64_t size);

namespace BbGuestMemory {
namespace {
using u64 = std::uint64_t;
/// The usage of the self-test's buffers (its import and the arena it asks about).
constexpr vk::BufferUsageFlags SelfTestUsage =
    vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eUniformBuffer |
    vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
    vk::BufferUsageFlagBits::eIndirectBuffer;

const Vulkan::Instance* vk_instance = nullptr;
std::uint32_t import_types = 0; // memory types a host import may use (from the self-test)
std::uint32_t arena_types = 0;  // memory types the arena lists (from the self-test)

std::uint32_t PickType(std::uint32_t bits) {
    const auto props = vk_instance->GetPhysicalDevice().getMemoryProperties();
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)) {
            return i;
        }
    }
    return ~0u;
}

/// A buffer over `size` bytes of host memory at `pointer`, imported; false (nothing kept) if not.
/// With eShaderDeviceAddress in `usage` the memory is allocated for device addresses and `address`
/// is the buffer's.
bool Import(void* pointer, u64 size, vk::Buffer& buffer, vk::DeviceMemory& memory,
            const char** failed_step, vk::BufferUsageFlags usage,
            vk::DeviceAddress* address = nullptr) {
    const vk::Device dev = vk_instance->GetDevice();
    const auto [props_result, props] = dev.getMemoryHostPointerPropertiesEXT(
        vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT, pointer);
    const std::uint32_t type = props_result == vk::Result::eSuccess
                                   ? PickType(props.memoryTypeBits & (import_types ? import_types : ~0u))
                                   : ~0u;
    if (type == ~0u) {
        *failed_step = "the driver's host pointer check";
        return false;
    }
    const vk::ExternalMemoryBufferCreateInfo external{
        .handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT,
    };
    const auto [buffer_result, made] = dev.createBuffer({
        .pNext = &external,
        .size = size,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive,
    });
    if (buffer_result != vk::Result::eSuccess) {
        *failed_step = "buffer creation";
        return false;
    }
    const bool device_address = bool(usage & vk::BufferUsageFlagBits::eShaderDeviceAddress);
    const vk::MemoryAllocateFlagsInfo address_info{
        .flags = vk::MemoryAllocateFlagBits::eDeviceAddress,
    };
    const vk::ImportMemoryHostPointerInfoEXT import_info{
        .pNext = device_address ? &address_info : nullptr,
        .handleType = vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT,
        .pHostPointer = pointer,
    };
    const auto [memory_result, imported] = dev.allocateMemory({
        .pNext = &import_info,
        .allocationSize = size,
        .memoryTypeIndex = type,
    });
    if (memory_result != vk::Result::eSuccess) {
        dev.destroyBuffer(made);
        *failed_step = device_address ? "the import (with device addresses)" : "the import";
        return false;
    }
    if (dev.bindBufferMemory(made, imported, 0) != vk::Result::eSuccess) {
        dev.freeMemory(imported);
        dev.destroyBuffer(made);
        *failed_step = "binding";
        return false;
    }
    if (device_address && address) {
        *address = dev.getBufferAddress({.buffer = made});
        if (*address == 0) {
            dev.freeMemory(imported);
            dev.destroyBuffer(made);
            *failed_step = "its device address";
            return false;
        }
    }
    buffer = made;
    memory = imported;
    return true;
}

/// Once: whether the driver imports a section view, as the game's memory is, and which memory
/// types an import and the arena may use. Sets vk_instance.
bool SelfTest(const Vulkan::Instance& instance) {
    static const bool usable = [&] {
        if (!instance.IsGuestMemoryImportSupported()) {
            std::printf("Guest memory: the driver cannot import system memory "
                        "(VK_EXT_external_memory_host)\n");
            return false;
        }
        vk_instance = &instance;
        // A section view, as the game's memory is: imported, then dropped.
        constexpr u64 Size = 2 << 20;
        HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                            DWORD(Size), nullptr);
        void* view = section ? MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, Size) : nullptr;
        vk::Buffer buffer;
        vk::DeviceMemory memory;
        const char* step = "mapping a test section";
        const bool imported = view && Import(view, Size, buffer, memory, &step, SelfTestUsage);
        if (imported) {
            const vk::Device dev = instance.GetDevice();
            // The arena (a sparse buffer) binds the GX region's import at its blocks where it is
            // needed there (GxArenaBound; BB_GX_OWN_BUFFER=0: all of it). Outside the types the
            // arena lists the binding is outside the spec; NVIDIA's driver binds it all the same,
            // so it is reported (ArenaTakesImports) rather than refused.
            const auto [arena_result, arena] = dev.createBuffer({
                .flags = vk::BufferCreateFlagBits::eSparseBinding |
                         vk::BufferCreateFlagBits::eSparseResidency,
                .size = u64(1) << 30,
                .usage = SelfTestUsage,
                .sharingMode = vk::SharingMode::eExclusive,
            });
            if (arena_result == vk::Result::eSuccess) {
                arena_types = dev.getBufferMemoryRequirements(arena).memoryTypeBits;
                const auto [props_result, props] = dev.getMemoryHostPointerPropertiesEXT(
                    vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT, view);
                import_types = props_result == vk::Result::eSuccess ? props.memoryTypeBits : 0;
                dev.destroyBuffer(arena);
            }
            dev.destroyBuffer(buffer);
            dev.freeMemory(memory);
        } else {
            std::printf("Guest memory: importing a section view failed at %s\n", step);
            vk_instance = nullptr;
        }
        if (view) {
            UnmapViewOfFile(view);
        }
        if (section) {
            CloseHandle(section);
        }
        return imported;
    }();
    return usable;
}
} // namespace

bool ArenaTakesImports() {
    return arena_types == 0 || import_types == 0 || (arena_types & import_types) != 0;
}

bool ImportRange(const Vulkan::Instance& instance, std::uint64_t phys, std::uint64_t size,
                 vk::BufferUsageFlags usage, vk::Buffer& buffer, vk::DeviceMemory& memory,
                 vk::DeviceAddress& address, const char** failed_step) {
    if (!SelfTest(instance)) { // sets vk_instance and the types an import may use
        *failed_step = "the driver's import of system memory (see above)";
        return false;
    }
    u64 direct_size = 0;
    unsigned char* backing = runtime_memory_backing(&direct_size);
    if (!backing || phys + size > direct_size) {
        *failed_step = "finding it in the game's memory pool";
        return false;
    }
    if (win_mem_commit(phys, size) != 0) {
        *failed_step = "committing its pages";
        return false;
    }
    address = 0;
    return Import(backing + phys, size, buffer, memory, failed_step, usage, &address);
}
} // namespace BbGuestMemory
#endif

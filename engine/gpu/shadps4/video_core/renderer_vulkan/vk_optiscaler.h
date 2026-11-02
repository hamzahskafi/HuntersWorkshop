// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: OptiScaler (a DLL players drop next to the executable, often as winmm.dll) hooks
// vkCreateDevice, vkGetDeviceProcAddr, vkQueueSubmit and takes over the NGX entry points: our
// DLSS runs through its own feature, which may call vkDeviceWaitIdle from the thread recording
// the evaluate (a release, or a backend change from its menu). It is allowed; while it is loaded
// a compatibility mode keeps the engine on the paths it handles: NGX calls under the queue lock,
// the Vulkan instance and device made on a thread of their own, and one submit thread while its
// frame generation is on. One switch for all of it (0.4.4 beta 28: its parts' own switches went):
// optiscaler_compat in config.ini (the in-game menu, read at start), or BB_OPTISCALER_COMPAT=0
// (the launcher's Engine switch) for a run.
#pragma once

#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace Vulkan::OptiScaler {

struct Info {
    bool loaded = false;
    std::string module;  ///< the file name it was loaded as (winmm.dll, dxgi.dll, ...)
    std::string version; ///< from its version resource, empty when it has none
    std::string frame_generation; ///< its frame generation output from OptiScaler.ini, empty when off
};

/// Looks once for a loaded OptiScaler and prints one line about it and the compatibility mode.
const Info& Detect();
/// OptiScaler is loaded and its compatibility mode is on.
bool Compat();
/// One submit thread: Compat() with its frame generation on.
bool SyncSubmit();

/// OptiScaler keeps a per-thread "a D3D device is being created" flag and skips its vkCreateInstance
/// and vkCreateDevice hooks while it is set (for DXVK/vkd3d-proton). It was found set on the
/// thread that creates our Vulkan objects (2026-10-21 log: both hooks "Skipping because DXVK/VKD3D
/// is creating a D3D device"), so its frame generation never prepared our device or hooked its
/// swapchain, yet started its D3D12 side and lost the GPU. With OptiScaler loaded the instance and
/// the device are created on a thread of their own, where that flag starts clear.
template <typename F>
auto OnCleanThread(F&& create) {
    if (!Compat()) {
        return create();
    }
    std::optional<decltype(create())> result;
    std::thread([&] { result.emplace(create()); }).join();
    return std::move(*result);
}

} // namespace Vulkan::OptiScaler

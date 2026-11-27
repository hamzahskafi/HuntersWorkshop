// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: OptiScaler (a DLL players put beside the engine, as dxgi.dll or winmm.dll, with its
// OptiScaler.ini) takes a game's DLSS calls and runs the upscaler chosen in its own menu (FSR, XeSS,
// DLSS; tools such as d4r run NVIDIA's DLSS on AMD through it). 0.4.6 beta 17: with it loaded, the
// engine's DLSS goes to its NGX entry points on any GPU (vk_dlss.cpp), not to NVIDIA's driver.
// Its hooks (vkCreateDevice, vkGetDeviceProcAddr, vkQueueSubmit, the NGX calls) are allowed; while it
// is loaded a compatibility mode keeps the engine on the paths they handle: NGX calls and every queue
// under the queue lock (its calls may wait for the device), the Vulkan instance and device made on a
// thread of their own, and one submit thread while its frame generation is set. Always, with it loaded
// (0.4.6 beta 17: the switch went, the tester's call: off took the protections away and bought nothing).
#pragma once

#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "video_core/renderer_vulkan/optiscaler_ini.h"

namespace Vulkan::OptiScaler {

struct Info {
    bool loaded = false;
    std::string module;  ///< the file name it was loaded as (winmm.dll, dxgi.dll, ...)
    std::string version; ///< from its version resource, empty when it has none
    void* handle = nullptr; ///< its module (HMODULE): its NGX entry points
    IniSettings ini;        ///< from OptiScaler.ini beside it
    std::string frame_generation; ///< ini.frame_generation: its frame generation, empty when off
    bool d4r = false; ///< d4r beside it (its boot DLL loaded, or its folder): NVIDIA's DLSS through it
};

/// Looks once for a loaded OptiScaler and prints what was found and how the engine works with it.
/// Called at the Vulkan instance's creation, after SDL's video start (which loads dxgi.dll).
const Info& Detect();
/// The engine's DLSS calls go to OptiScaler's NGX entry points: it is loaded and its DLSS inputs are
/// not turned off in OptiScaler.ini (EnableDlssInputs=false leaves DLSS to NVIDIA's driver).
bool DlssThrough();
/// The compatibility mode: OptiScaler is loaded.
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

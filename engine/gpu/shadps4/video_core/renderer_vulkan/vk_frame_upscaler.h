// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: one frame's inputs to an upscaler that writes its own output image: AMD FSR through AMD's
// FSR SDK (vk_amd_fsr.h) and NVIDIA DLSS (vk_dlss.h). TemporalUpscaler::RecordFrameUpscaler fills it
// from the HDR scene colour (1920x1080 output) or the game's tonemapped frame (other outputs), the
// scene depth, the motion vectors in render pixels (previous - current) and the jitter offset.
// 0.4.4 beta 27: was Fsr4Upscaler's (vk_fsr4.h, the built-in FSR 4 model, removed).
#pragma once

#include <array>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

struct UpscaleInput {
    /// An image in layout General, written/read by compute shaders.
    struct Image {
        vk::Image image;
        vk::ImageView view;
        u32 width, height;
        vk::Format format = vk::Format::eUndefined;
    };
    struct Frame {
        vk::CommandBuffer cmdbuf;
        Image color, depth, motion, output;
        /// The reactive mask (R8, render size) when made this frame: AMD FSR only; image null otherwise.
        Image reactive{};
        u32 render_width, render_height;
        int preset; ///< BbSettings::Preset
        std::array<float, 2> jitter;
        float frame_ms, near_plane, far_plane, vertical_fov;
        float sharpness;
        bool sharpen, reset;
        /// The colour is the HDR scene (1920x1080 output); otherwise the game's frame in linear light.
        bool hdr = true;
    };
};

} // namespace Vulkan

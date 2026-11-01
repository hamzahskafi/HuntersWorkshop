// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: frame generation, FSR 3.1 frame interpolation (vk_frame_generation.h).

#include "video_core/renderer_vulkan/vk_frame_generation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <vk_mem_alloc.h>

#include "bbport_settings.h"
#include "common/thread.h"
#include "precise_pacing.h"
#include "core/emulator_settings.h"
#include "video_core/host_shaders/frame_gen_compose_comp.h"
#include "video_core/host_shaders/frame_gen_depth_comp.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/vk_streamline.h"

namespace Vulkan {

namespace {

// bbport: the scene's far plane: the scene constants give the near plane and the field of view
// (CameraMotion), not the far one; the upscaler passes the same.
constexpr float FarPlane = 3000.0f;
// A pixel is UI where the final frame differs from its HUD-less copy by more than this (the
// copies are bit-identical where no UI is drawn; this only ignores rounding).
constexpr float UiThreshold = 1.5f / 255.0f;

bool FrameStats() {
    static const bool enabled = [] {
        const char* env = std::getenv("BB_FRAME_STATS");
        return env && env[0] == '1';
    }();
    return enabled;
}


void GlobalBarrier(vk::CommandBuffer cmdbuf) {
    const vk::MemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
    };
    cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});
}

constexpr vk::ImageUsageFlags ColorUsage = vk::ImageUsageFlagBits::eSampled |
                                           vk::ImageUsageFlagBits::eStorage |
                                           vk::ImageUsageFlagBits::eTransferDst |
                                           vk::ImageUsageFlagBits::eTransferSrc;

} // namespace

FrameGenerator::FrameGenerator(const Instance& instance_, Scheduler& draw_scheduler_)
    : instance{instance_}, draw_scheduler{draw_scheduler_} {}

FrameGenerator::~FrameGenerator() {
    // The presenter waited for the GPU before its members go.
    DestroyResources();
}

bool FrameGenerator::Enabled() const {
    // 0.4.4 beta 27: FSR 3.1 frame interpolation (FireBurn's) went with FireBurn's FSR; AMD's own
    // needs AMD's swapchain and comes after 0.4.4 (community-fixes-plan.md, Decisions).
    return false;
}

bool FrameGenerator::DlssWanted() {
    const auto& s = BbSettings::Get();
    return s.frame_gen.load(std::memory_order_relaxed) == BbSettings::FrameGenDlss &&
           s.dlss_fg_supported.load(std::memory_order_relaxed);
}

void FrameGenerator::OnFrameStart() {
    // The display pass of the frame just finished: its inputs go to its PrepareFrame (the flip
    // that follows in the command stream).
    inputs_ready = inputs_frame;
    hudless_ready = hudless_frame;
    inputs_frame = false;
    hudless_frame = false;
}

void FrameGenerator::CaptureInputs(Scheduler& scheduler, vk::ImageView source_depth,
                                   vk::Image motion, u32 w, u32 h, std::array<float, 2> jitter,
                                   float near_plane, float vertical_fov,
                                   const CameraMotion::Matrices& camera) {
    if (!Capturing()) {
        return;
    }
    // The render size the resources are made for (Prepare makes them again on a change): this
    // frame then has none, the next one does.
    input_width = w;
    input_height = h;
    if (render_width == 0 || w != render_width || h != render_height || !source_depth || !motion) {
        return;
    }
    input_jitter = jitter;
    input_near = near_plane;
    input_fov = vertical_fov;
    input_camera = camera;
    inputs_frame = true;
    scheduler.Record([source_depth, motion, w, h, target_depth = *depth_view,
                      target_motion = vk::Image(motion_image), sampler = *sampler,
                      pipeline = *depth_pipeline,
                      layout = *depth_pipeline_layout](vk::CommandBuffer cmdbuf) {
        GlobalBarrier(cmdbuf);
        const vk::DescriptorImageInfo in{.sampler = sampler, .imageView = source_depth,
                                         .imageLayout = vk::ImageLayout::eGeneral};
        const vk::DescriptorImageInfo out{.imageView = target_depth,
                                          .imageLayout = vk::ImageLayout::eGeneral};
        const std::array<vk::WriteDescriptorSet, 2> writes{{
            {.dstBinding = 0, .descriptorCount = 1,
             .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &in},
            {.dstBinding = 1, .descriptorCount = 1,
             .descriptorType = vk::DescriptorType::eStorageImage, .pImageInfo = &out},
        }};
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
        cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, layout, 0, writes);
        cmdbuf.dispatch((w + 7) / 8, (h + 7) / 8, 1);
        const vk::ImageCopy region{
            .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .extent = {w, h, 1},
        };
        cmdbuf.copyImage(motion, vk::ImageLayout::eGeneral, target_motion,
                         vk::ImageLayout::eGeneral, region);
        GlobalBarrier(cmdbuf);
    });
}

void FrameGenerator::CaptureHudless(Scheduler& scheduler, vk::Image image, vk::Format format,
                                    u32 w, u32 h) {
    if (!WantsHudless() || !image ||
        (format != vk::Format::eR8G8B8A8Unorm && format != vk::Format::eR8G8B8A8Srgb)) {
        return;
    }
    hudless_width = w;
    hudless_height = h;
    if (w != width || h != height) {
        return; // Prepare reports the mismatch
    }
    hudless_frame = true;
    scheduler.Record([image, w, h, target = vk::Image(hudless_image)](vk::CommandBuffer cmdbuf) {
        GlobalBarrier(cmdbuf);
        const vk::ImageCopy region{
            .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .extent = {w, h, 1},
        };
        cmdbuf.copyImage(image, vk::ImageLayout::eGeneral, target, vk::ImageLayout::eGeneral,
                         region);
        GlobalBarrier(cmdbuf);
    });
}

void FrameGenerator::Idle(const char* reason) {
    if (running || (reason != idle_reason && !started_logged)) {
        // Once per stop (and, before the first start, once per reason): the reasons a frame
        // has none change every frame in a menu.
        std::printf("Frame generation: no generated frames: %s\n", reason);
        std::fflush(stdout);
    }
    running = false;
    idle_reason = reason;
    BbSettings::Get().frame_gen_active.store(false, std::memory_order_relaxed);
}

std::optional<FrameGenerator::Job> FrameGenerator::Prepare(vk::Extent2D display,
                                                           vk::Format view_format, bool hdr,
                                                           bool other_scaler, u64 tick) {
    const int setting = BbSettings::Get().frame_gen.load(std::memory_order_relaxed);
    const bool dlss = setting == BbSettings::FrameGenDlss;
    if (setting != last_setting) {
        if (last_setting >= 0 || setting != BbSettings::FrameGenOff) {
            std::printf("Frame generation: %s\n",
                        setting == BbSettings::FrameGenFsr3 ? "FSR 3.1 is no longer offered: off"
                        : dlss                              ? "DLSS selected"
                                                            : "off");
        }
        last_setting = setting;
        need_reset = true;
        if (setting == BbSettings::FrameGenOff) {
            running = false;
            idle_reason = nullptr;
            BbSettings::Get().frame_gen_active.store(false, std::memory_order_relaxed);
        }
    }
    const auto now = Clock::now();
    const float ms = std::chrono::duration<float, std::milli>(now - last_prepare).count();
    frame_ms = ms > 0.0f && ms < 200.0f ? ms : 16.6f;
    last_prepare = now;
    const bool inputs = inputs_ready, hudless = hudless_ready;
    inputs_ready = hudless_ready = false;
    if (!dlss) {
        need_reset = true;
        return std::nullopt;
    }
    const char* why = nullptr;
    if (!DlssWanted()) {
        // bbport: Streamline not loaded at start, or DLSS-G not supported here (the menu says why).
        const char* problem = BbSettings::Get().dlss_fg_problem.load(std::memory_order_relaxed);
        why = problem ? problem : "DLSS Frame Generation is not loaded (restart with it selected)";
    } else if (hdr && !DlssFrameGenHdr()) {
        why = "HDR output is on (BB_DLSSG_HDR=0)";
    } else if (other_scaler) {
        why = "the presenter's FSR 1 scaling is on";
    } else if (view_format != vk::Format::eR8G8B8A8Unorm &&
               view_format != vk::Format::eR8G8B8A8Srgb &&
               view_format != vk::Format::eB8G8R8A8Unorm &&
               view_format != vk::Format::eB8G8R8A8Srgb) {
        why = "the display buffer is not 8 bits per channel";
    } else if (input_width != 0 &&
               (resources_fsr || render_width == 0 || display.width != width ||
                display.height != height || input_width != render_width ||
                input_height != render_height)) {
        // New sizes (or the first upscaled frame): made again once the GPU is idle; generation
        // starts over from the next frame.
        const u32 rw = input_width, rh = input_height;
        input_width = input_height = 0;
        need_reset = true;
        EnsureResources(display.width, display.height, rw, rh, false);
        return std::nullopt;
    } else if (!inputs) {
        why = "no upscaled game frame (menus, movies, loading screens, or no upscaler)";
    } else if (!hudless) {
        why = hudless_width != width || hudless_height != height
                  ? "the frame without the UI is not the display's size"
                  : "no copy of the frame without the UI";
    }
    if (why) {
        Idle(why);
        need_reset = true;
        return std::nullopt;
    }
    Job job;
    job.frame_id = next_frame_id++;
    job.tick = tick;
    job.reset = need_reset;
    job.present = !need_reset;
    need_reset = false;
    job.width = width;
    job.height = height;
    job.render_width = render_width;
    job.render_height = render_height;
    job.jitter = input_jitter;
    job.near_plane = std::max(input_near, 0.001f);
    job.vertical_fov = input_fov > 0.01f && input_fov < 3.1f ? input_fov : 1.0f;
    job.frame_ms = frame_ms;
    job.view_format = view_format;
    job.dlss = dlss;
    job.camera = input_camera;
    if (dlss) {
        job.present = false; // DLSS-G presents its own frames: no generated presentation frame
    }
    if (!running) {
        running = true;
        idle_reason = nullptr;
        if (!started_logged) {
            started_logged = true;
            std::printf("Frame generation: %s, %ux%u interpolated from %ux%u motion and "
                        "depth, UI from the real frame (HUD-less copy), present mode %s\n",
                        "DLSS (Streamline)", width, height, render_width,
                        render_height, std::string(EmulatorSettings.GetPresentMode()).c_str());
        } else {
            std::printf("Frame generation: running again\n");
        }
        std::fflush(stdout);
    }
    // DLSS-G says itself when its frames are shown (DlssFrameGen::AfterPresent).
    return job;
}

void FrameGenerator::Retire(u64 completed_tick) {
    while (!in_flight.empty() && in_flight.front().second <= completed_tick) {
        in_flight.pop_front();
    }
}

bool FrameGenerator::Record(vk::CommandBuffer, const Job&, vk::ImageView) {
    // The presenter hands DLSS-G its inputs (vk_streamline.h). 0.4.4 beta 27: the FSR 3.1 frame
    // interpolation recorded here went with FireBurn's FSR.
    return false;
}

void FrameGenerator::CreatePipelines() {
    if (compose_pipeline) {
        return;
    }
    const auto device = instance.GetDevice();
    sampler = Check(device.createSamplerUnique({
        .magFilter = vk::Filter::eNearest, .minFilter = vk::Filter::eNearest,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge}));
    const auto make = [&](u32 samplers, vk::UniqueDescriptorSetLayout& desc_layout,
                          vk::UniquePipelineLayout& pipeline_layout, u32 push_size,
                          const auto& code) {
        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};
        for (u32 i = 0; i <= samplers; ++i) {
            bindings[i] = {.binding = i,
                           .descriptorType = i < samplers ? vk::DescriptorType::eCombinedImageSampler
                                                          : vk::DescriptorType::eStorageImage,
                           .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute};
        }
        desc_layout = Check(device.createDescriptorSetLayoutUnique({
            .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
            .bindingCount = samplers + 1, .pBindings = bindings.data()}));
        const vk::PushConstantRange push{vk::ShaderStageFlagBits::eCompute, 0, push_size};
        pipeline_layout = Check(device.createPipelineLayoutUnique({
            .setLayoutCount = 1, .pSetLayouts = &*desc_layout,
            .pushConstantRangeCount = push_size ? 1u : 0u, .pPushConstantRanges = &push}));
        const auto module = CompileSPV(code, device);
        auto pipeline = Check(device.createComputePipelineUnique(
            {}, vk::ComputePipelineCreateInfo{
                    .stage = {.stage = vk::ShaderStageFlagBits::eCompute,
                              .module = module,
                              .pName = "main"},
                    .layout = *pipeline_layout,
                }));
        device.destroyShaderModule(module);
        return pipeline;
    };
    compose_pipeline = make(3, compose_desc_layout, compose_pipeline_layout, sizeof(float),
                            FRAME_GEN_COMPOSE_COMP);
    depth_pipeline = make(1, depth_desc_layout, depth_pipeline_layout, 0, FRAME_GEN_DEPTH_COMP);
}

void FrameGenerator::DestroyResources() {
    std::scoped_lock lock{record_mutex};
    in_flight.clear();
    hudless_view_unorm.reset();
    hudless_view_srgb.reset();
    generated_view_unorm.reset();
    generated_view_srgb.reset();
    composed_view.reset();
    depth_view.reset();
    hudless_image = VideoCore::UniqueImage();
    generated_image = VideoCore::UniqueImage();
    composed_image = VideoCore::UniqueImage();
    depth_image = VideoCore::UniqueImage();
    motion_image = VideoCore::UniqueImage();
    width = height = render_width = render_height = 0;
    resources_fsr = false;
}

bool FrameGenerator::EnsureResources(u32 w, u32 h, u32 rw, u32 rh, bool fsr) {
    // Recorded commands of earlier frames use the old ones.
    draw_scheduler.Finish();
    DestroyResources();
    CreatePipelines();
    const auto device = instance.GetDevice();
    const auto allocator = instance.GetAllocator();
    const auto image = [&](VideoCore::UniqueImage& target, vk::Format format, u32 iw, u32 ih,
                           vk::ImageUsageFlags usage, bool srgb_view) {
        target = VideoCore::UniqueImage(device, allocator);
        target.Create(vk::ImageCreateInfo{
            .flags = srgb_view ? vk::ImageCreateFlagBits::eMutableFormat |
                                     vk::ImageCreateFlagBits::eExtendedUsage
                               : vk::ImageCreateFlags{},
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = {iw, ih, 1},
            .mipLevels = 1, .arrayLayers = 1, .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = usage,
            .initialLayout = vk::ImageLayout::eUndefined,
        });
    };
    const auto view = [&](const VideoCore::UniqueImage& source, vk::Format format,
                          bool sampled_only) {
        const vk::ImageViewUsageCreateInfo usage{.usage = vk::ImageUsageFlagBits::eSampled};
        return Check(device.createImageViewUnique({
            .pNext = sampled_only ? &usage : nullptr,
            .image = vk::Image(source), .viewType = vk::ImageViewType::e2D, .format = format,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}));
    };
    image(hudless_image, vk::Format::eR8G8B8A8Unorm, w, h, ColorUsage, true);
    image(generated_image, vk::Format::eR8G8B8A8Unorm, w, h, ColorUsage, true);
    image(composed_image, vk::Format::eR16G16B16A16Sfloat, w, h,
          vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage, false);
    image(depth_image, vk::Format::eR32Sfloat, rw, rh,
          vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage, false);
    image(motion_image, vk::Format::eR16G16Sfloat, rw, rh,
          vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, false);
    hudless_view_unorm = view(hudless_image, vk::Format::eR8G8B8A8Unorm, true);
    hudless_view_srgb = view(hudless_image, vk::Format::eR8G8B8A8Srgb, true);
    generated_view_unorm = view(generated_image, vk::Format::eR8G8B8A8Unorm, true);
    generated_view_srgb = view(generated_image, vk::Format::eR8G8B8A8Srgb, true);
    composed_view = view(composed_image, vk::Format::eR16G16B16A16Sfloat, false);
    depth_view = view(depth_image, vk::Format::eR32Sfloat, false);

    // bbport: DLSS Frame Generation: the captures only (0.4.4 beta 27: FSR 3.1's context went).
    resources_fsr = fsr;
    width = w;
    height = h;
    render_width = rw;
    render_height = rh;
    ToGeneral();
    std::printf("Frame generation: DLSS inputs %ux%u (render %ux%u)\n", w, h, rw, rh);
    return true;
}

void FrameGenerator::ToGeneral() {
    // The images live in General from here on.
    draw_scheduler.Record([images = std::array<vk::Image, 5>{
                               vk::Image(hudless_image), vk::Image(generated_image),
                               vk::Image(composed_image), vk::Image(depth_image),
                               vk::Image(motion_image)}](vk::CommandBuffer cmdbuf) {
        std::array<vk::ImageMemoryBarrier2, 5> barriers{};
        for (size_t i = 0; i < images.size(); ++i) {
            barriers[i] = {
                .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                .srcAccessMask = vk::AccessFlagBits2::eNone,
                .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                .dstAccessMask = vk::AccessFlagBits2::eMemoryRead |
                                 vk::AccessFlagBits2::eMemoryWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eGeneral,
                .image = images[i],
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
        }
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = u32(barriers.size()),
                                 .pImageMemoryBarriers = barriers.data()});
    });
}

bool FrameGenerator::ShouldPresentGenerated() {
    if (queued_flips.load(std::memory_order_relaxed) > 0) {
        // The next real frame is already waiting: showing this one would delay it further.
        ++stats_dropped_backlog;
        return false;
    }
    return true;
}

void FrameGenerator::WaitForRealSlot(Clock::time_point shown) {
    if (interval_ms <= 0.0) {
        return;
    }
    const auto half = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double, std::milli>(std::min(interval_ms, 50.0) * 0.5));
    const auto deadline = shown + half;
    PrecisePacing::WaitUntil(deadline, true);
}

void FrameGenerator::OnRealPresented(Clock::time_point start, bool generated_shown,
                                     bool generated_made) {
    const auto now = Clock::now();
    if (last_real != Clock::time_point{}) {
        const double dt = std::chrono::duration<double, std::milli>(start - last_real).count();
        // A hitch (loading, a stall) is not the game's rhythm.
        if (dt > 1.0 && dt < 100.0) {
            interval_ms = interval_ms > 0.0 ? interval_ms * 0.9 + dt * 0.1 : dt;
        }
    }
    last_real = start;
    if (interval_ms > 0.0) {
        BbSettings::Get().frame_gen_game_fps.store(float(1000.0 / interval_ms),
                                                   std::memory_order_relaxed);
    }
    ++stats_real;
    if (!generated_made) {
        ++stats_not_made;
    }
    if (generated_shown && interval_ms > 0.0) {
        ++stats_shown;
        const double cycle = std::chrono::duration<double, std::milli>(now - start).count();
        stats_cycle_ms += cycle;
        // Overload guard: both presents (with the half-interval wait between them) should fit
        // in the game's interval. One slow frame weighs at most 2x, so hitches alone do not
        // trip it; a steady overload does within about half a second.
        load_avg = load_avg * 0.9 + std::min(cycle / interval_ms, 2.0) * 0.1;
        over_frames = load_avg > 1.15 ? over_frames + 1 : 0;
        if (over_frames >= 30) {
            const auto until = now + std::chrono::seconds(3);
            paused_until_ns.store(
                std::chrono::duration_cast<std::chrono::nanoseconds>(until.time_since_epoch())
                    .count(),
                std::memory_order_relaxed);
            std::printf("Frame generation: paused for 3 s: presenting both frames took %.1f ms "
                        "of the game's %.1f ms frame interval\n",
                        cycle, interval_ms);
            std::fflush(stdout);
            load_avg = 0.0;
            over_frames = 0;
        }
    }
    if (!FrameStats()) {
        return;
    }
    if (stats_start == Clock::time_point{}) {
        stats_start = now;
    }
    const double window = std::chrono::duration<double>(now - stats_start).count();
    if (window >= 5.0) {
        std::printf("Frame generation: %.1f s: game %.1f FPS, shown %.1f FPS, %u generated shown, "
                    "dropped %u (next frame queued), %u real frames without one; present cycle "
                    "%.2f ms of %.2f ms, load %.2f; VRAM %llu MiB in use\n",
                    window, stats_real / window, (stats_real + stats_shown) / window,
                    stats_shown, stats_dropped_backlog, stats_not_made,
                    stats_shown ? stats_cycle_ms / stats_shown : 0.0, interval_ms, load_avg,
                    // VK_EXT_memory_budget: every allocation, the interpolation's own included.
                    static_cast<unsigned long long>(
                        instance.CanReportMemoryUsage() ? instance.GetDeviceMemoryUsage() >> 20
                                                        : 0));
        std::fflush(stdout);
        stats_start = now;
        stats_real = stats_shown = stats_dropped_backlog = stats_not_made = 0;
        stats_cycle_ms = 0.0;
    }
}

} // namespace Vulkan

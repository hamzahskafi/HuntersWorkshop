// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: GPU work left out or merged, and its BB_FRAME_STATS report.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>

namespace Vulkan::GpuSavings {

/// What ended a render pass (Scheduler::EndRendering's reason; beta 19: named where it ends;
/// beta 20: transfers by operation).
enum class PassBreak : std::uint8_t {
    Other,
    CopyShader,
    ImageCopy,
    Dispatch,
    BufferBarrier,
    ImageBarrier,
    Transfer,
    TextureCache,
    SceneProxy,
    Motion,
    Upscaler,
    Profiler,
    Submission,
    BufferCopy,
    BufferUpload,
    BufferUpdate,
    BufferFill,
    ImageUpload,
    ImageDownload,
    ImageTransferCopy,
    ImageClear,
    DepthColor,
    BackingSamples,
    Count
};
inline constexpr const char* PassBreakNames[] = {
    "other",          "copy shader",        "image copy (copy shader)", "other dispatch",
    "buffer barrier", "image barrier",      "transfer",                 "texture cache",
    "scene proxy",    "motion",             "upscaler or presentation", "profiler",
    "submission",     "buffer copy",        "buffer upload from the game's memory",
    "buffer update",  "buffer fill",        "image upload",             "image download",
    "image copy (transfer)",                "image clear",              "depth/colour copy",
    "sample count change",
};
static_assert(std::size(PassBreakNames) == static_cast<std::size_t>(PassBreak::Count));
/// What a barrier that ended a pass waits for (its source stages); detail bits 0 to 5.
enum class BarrierWait : std::uint8_t {
    GraphicsShaders,
    Attachments,
    Compute,
    Transfer,
    Host,
    Other,
    Count
};
inline constexpr const char* BarrierWaitNames[] = {"graphics shaders", "attachments", "compute",
                                                   "transfers", "host", "all commands or other"};
/// 0.4.4 beta 20: the layouts an image barrier that ended a pass moves images to; detail bits 8
/// to 14.
enum class BarrierLayout : std::uint8_t {
    DepthAttachment,
    DepthReadOnly,
    ShaderRead,
    General,
    ColorAttachment,
    Transfer,
    Other,
    Count
};
inline constexpr const char* BarrierLayoutNames[] = {
    "depth attachment", "depth read-only", "shader read", "general",
    "colour attachment", "transfer", "other"};

struct Counters {
    // Texel buffer fills from images (BufferCache::SynchronizeMemoryFromImage).
    std::atomic<std::uint64_t> tile_fills{0}, tile_skips{0}, tile_bytes_skipped{0};
    // Why a fill was not skipped although a memo for the range existed.
    std::atomic<std::uint64_t> tile_miss_image{0}, tile_miss_buffer{0}, tile_miss_proxy{0},
        tile_miss_other{0};
    // HLE copy shader: batches; those copied by a dispatch (MultiCopy; the others, of fewer than
    // 8 ranges or not whole dwords, by vkCmdCopyBuffer); those dispatches that flushed barriers
    // first.
    std::atomic<std::uint64_t> hle_batches{0}, hle_dispatches{0}, hle_barriers{0};
    // Lookahead thread: sleeps instead of yields while stage B is stuck.
    std::atomic<std::uint64_t> lookahead_sleeps{0};
    // Scene proxy resolves by the native access that needed them: transfer (copies, downloads,
    // texel buffer fills), sampled read, storage write, native attachment, other.
    std::array<std::atomic<std::uint64_t>, 5> scene_resolves{};
    // 0.4.4 beta 18: render passes begun again with the same targets after something ended them
    // (a split), by what ended them (PassBreak; beta 19: every place that ends a pass named).
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(PassBreak::Count)>
        pass_splits{};
    // 0.4.4 beta 19: barriers that ended a pass, by what they wait for (BarrierWait); beta 20:
    // only those of a pass begun again with the same targets, and also by the layouts their
    // images move to (BarrierLayout).
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(BarrierWait::Count)>
        pass_barrier_waits{};
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(BarrierLayout::Count)>
        pass_barrier_layouts{};
    // 0.4.4 beta 20: passes begun again after a recording cut closed them (Scheduler::MaybeSplit,
    // EnterDirectMode); passes begun again on the same colour targets with another depth view, or
    // with the same views and other layouts or clears.
    std::atomic<std::uint64_t> pass_resumes_after_cut{0}, pass_restarts_depth_view{0},
        pass_restarts_layout_or_clear{0};
    // 0.4.4 beta 20: draws and dispatches bound by a descriptor set, not pushed (pipelines with
    // more descriptors than the device pushes: Pipeline::BindResources).
    std::atomic<std::uint64_t> descriptor_set_binds{0};
    // 0.4.4 beta 20: draws and dispatches left out because the resource-table walker faulted on
    // a table pointer (Shader::Info::srt_walk_faults; the engine map's shader-1).
    std::atomic<std::uint64_t> table_fault_drops{0};
    // 0.4.4 beta 18: copy shader dispatches recorded inside a render pass: with no barrier needed
    // before them (nothing since the pass began wrote their source or touched their destination)
    // and their source a snapshot in the stream buffer; the same with their source the game's
    // memory; needing a barrier.
    std::array<std::atomic<std::uint64_t>, 3> hle_in_pass{};
    // 0.4.4 beta 20: copy shader game dispatches that interrupted a render pass (counted at their
    // start; hle_in_pass splits those whose first batch went through MultiCopy).
    std::atomic<std::uint64_t> hle_dispatches_in_pass{0};
    // 0.4.4 beta 18: Runtime::CopyColorAndDepth by kind (ColorDepthCopy), why a depth->colour
    // copy was not done by the shader (DepthCopyRefusal), and the texture cache's conversions of
    // an image into one of the other kind (colour->depth, depth->colour).
    std::array<std::atomic<std::uint64_t>, 7> color_depth_copies{};
    std::array<std::atomic<std::uint64_t>, 7> depth_copy_refusals{};
    // 0.4.4 beta 19: why Runtime::CopyColorToDepth left a colour->depth copy to the copy before
    // it (DepthCopyRefusal: storage meaning the depth image's attachment use here).
    std::array<std::atomic<std::uint64_t>, 7> color_copy_refusals{};
    std::array<std::atomic<std::uint64_t>, 2> overlap_conversions{};
    // 0.4.4 beta 18: uploads of the game's memory into depth images (Runtime::UploadImage).
    std::atomic<std::uint64_t> depth_uploads{0}, depth_upload_bytes{0};
    // 0.4.5 beta 1: draws with no target and no memory writes, left out
    // (Rasterizer::SkipEmptyDraw).
    std::atomic<std::uint64_t> empty_draws_skipped{0};
    // 0.4.5 beta 1: the game's rectangle copies (a040875f), all of them; beta 4 leaves the lamp
    // shadow faces' out (ShadowRedirectDispatch in vk_rasterizer.cpp).
    std::atomic<std::uint64_t> rect_copies{0};
    // 0.4.5 beta 4: lamp shadow faces drawn into their blur's scratch image, their copies left
    // out, and faces put back into the cube's layer before or after the copy (ShadowRedirect).
    std::atomic<std::uint64_t> shadow_faces_redirected{0}, shadow_copies_skipped{0},
        shadow_restores_before{0}, shadow_restores_after{0};
    // 0.4.5 beta 5: their clears done as the face's render pass began, or on their own (nothing
    // drawn into the face: where the copy was left out; beta 6: before a pass smaller than it).
    std::atomic<std::uint64_t> shadow_clears_folded{0}, shadow_clears_late{0};
};

/// Runtime::CopyColorAndDepth's kinds: depth->colour by the shader (0.4.4 beta 17), by a copy;
/// colour->depth by a copy; a single-sample image into MSAA depth by a draw; both of one kind;
/// not done (other MSAA copies).
enum class ColorDepthCopy : std::uint8_t {
    DepthToColorShader,
    DepthToColorCopy,
    ColorToDepth,
    ColorToMsDepth,
    SameKind,
    NotDone,
    ColorToDepthShader ///< 0.4.4 beta 19: by a draw (Runtime::CopyColorToDepth)
};
/// Why Runtime::CopyDepthToColor left a depth->colour copy to the copy before it (beta 19:
/// LevelsOrLayers only for volume images, every level and layer being copied now).
enum class DepthCopyRefusal : std::uint8_t {
    SwitchedOff,
    Samples,
    LevelsOrLayers,
    Size,
    NoImage,
    NoStorage,
    Formats
};
inline void Count(std::atomic<std::uint64_t>& counter) {
    counter.fetch_add(1, std::memory_order_relaxed);
}
inline Counters& Get() {
    static Counters counters;
    return counters;
}

/// BB_FRAME_STATS: one line every 5 s (called once a submission).
inline void Report() {
    static auto window = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now - window < std::chrono::seconds(5)) {
        return;
    }
    const double s = std::chrono::duration<double>(now - window).count();
    window = now;
    auto& c = Get();
    const auto take = [](std::atomic<std::uint64_t>& v) { return double(v.exchange(0)); };
    const double fills = take(c.tile_fills), skips = take(c.tile_skips);
    const double skipped_mb = take(c.tile_bytes_skipped) / (1024.0 * 1024.0);
    const double mi = take(c.tile_miss_image), mb = take(c.tile_miss_buffer),
                 mp = take(c.tile_miss_proxy), mo = take(c.tile_miss_other);
    const double batches = take(c.hle_batches), dispatches = take(c.hle_dispatches),
                 barriers = take(c.hle_barriers);
    const double sleeps = take(c.lookahead_sleeps);
    double resolves[5];
    for (int i = 0; i < 5; ++i) {
        resolves[i] = take(c.scene_resolves[i]);
    }
    double in_pass[3], kinds[7], refusals[7], color_refusals[7], overlaps[2];
    // 0.4.4 beta 20: the pass reasons named, those above 0.05 a second.
    std::string splits, waits, layouts;
    const auto add = [&](std::string& out, const char* name, double value) {
        if (value >= 0.05) {
            char text[96];
            std::snprintf(text, sizeof(text), "%s%s %.1f", out.empty() ? "" : ", ", name, value);
            out += text;
        }
    };
    for (std::size_t i = 0; i < c.pass_splits.size(); ++i) {
        add(splits, PassBreakNames[i], take(c.pass_splits[i]) / s);
    }
    for (std::size_t i = 0; i < c.pass_barrier_waits.size(); ++i) {
        add(waits, BarrierWaitNames[i], take(c.pass_barrier_waits[i]) / s);
    }
    for (std::size_t i = 0; i < c.pass_barrier_layouts.size(); ++i) {
        add(layouts, BarrierLayoutNames[i], take(c.pass_barrier_layouts[i]) / s);
    }
    const double resumes = take(c.pass_resumes_after_cut) / s;
    const double depth_view = take(c.pass_restarts_depth_view) / s;
    const double layout_or_clear = take(c.pass_restarts_layout_or_clear) / s;
    const double dispatches_in_pass = take(c.hle_dispatches_in_pass) / s;
    const double set_binds = take(c.descriptor_set_binds) / s;
    const double table_drops = take(c.table_fault_drops) / s;
    for (int i = 0; i < 3; ++i) {
        in_pass[i] = take(c.hle_in_pass[i]) / s;
    }
    for (int i = 0; i < 7; ++i) {
        kinds[i] = take(c.color_depth_copies[i]) / s;
        refusals[i] = take(c.depth_copy_refusals[i]) / s;
        color_refusals[i] = take(c.color_copy_refusals[i]) / s;
    }
    for (int i = 0; i < 2; ++i) {
        overlaps[i] = take(c.overlap_conversions[i]) / s;
    }
    const double depth_uploads = take(c.depth_uploads) / s;
    const double depth_upload_mb = take(c.depth_upload_bytes) / s / (1024.0 * 1024.0);
    std::printf("GPU savings per second: image->texel buffer fills %.1f, skipped unchanged %.1f "
                "(%.1f MB; refilled after image write %.1f, buffer write %.1f, proxy %.1f, other "
                "%.1f); HLE copy %.0f batches, %.0f of them as dispatches (%.0f after a barrier "
                "flush; the rest vkCmdCopyBuffer); lookahead sleeps %.0f\n",
                fills / s, skips / s, skipped_mb / s, mi / s, mb / s, mp / s, mo / s, batches / s,
                dispatches / s, barriers / s, sleeps / s);
    std::printf("  scene proxy resolves per second by native access: transfer %.1f, sampled %.1f, "
                "storage %.1f, attachment %.1f, other %.1f\n",
                resolves[0] / s, resolves[1] / s, resolves[2] / s, resolves[3] / s, resolves[4] / s);
    std::printf("  render passes per second begun again with the same targets, by what ended them: "
                "%s; their barriers waiting for: %s; moving images to: %s; begun again after a "
                "recording cut %.1f; on the same colour targets with another depth view %.1f; with "
                "the same views and other layouts or clears %.1f; copy shader dispatches that "
                "interrupted a pass %.1f (first batch: no barrier needed and the source a snapshot "
                "%.1f, the source the game's memory %.1f; a barrier needed %.1f)\n",
                splits.empty() ? "none" : splits.c_str(), waits.empty() ? "none" : waits.c_str(),
                layouts.empty() ? "none" : layouts.c_str(), resumes, depth_view, layout_or_clear,
                dispatches_in_pass, in_pass[0], in_pass[1], in_pass[2]);
    std::printf("  draws and dispatches per second bound by a descriptor set, not pushed (more "
                "descriptors than the device pushes): %.1f; left out because the resource-table "
                "walker faulted: %.1f\n",
                set_binds, table_drops);
    std::printf("  depth and colour copies per second (Runtime::CopyColorAndDepth): depth->colour "
                "by the shader %.1f, by a copy %.1f (switched off %.1f, MSAA %.1f, volume %.1f, "
                "sizes %.1f, no image %.1f, no storage %.1f, formats %.1f); "
                "colour->depth by a draw %.1f, by a copy %.1f (switched off %.1f, MSAA %.1f, "
                "volume %.1f, sizes %.1f, no image %.1f, no attachment use %.1f, formats %.1f), "
                "into MSAA depth %.1f; same kind %.1f; not done (MSAA) %.1f; texture cache "
                "conversions colour->depth %.1f, depth->colour %.1f; uploads of the game's memory "
                "into depth images %.1f (%.1f MB)\n",
                kinds[0], kinds[1], refusals[0], refusals[1], refusals[2], refusals[3],
                refusals[4], refusals[5], refusals[6], kinds[6], kinds[2], color_refusals[0],
                color_refusals[1], color_refusals[2], color_refusals[3], color_refusals[4],
                color_refusals[5], color_refusals[6], kinds[3], kinds[4], kinds[5], overlaps[0],
                overlaps[1], depth_uploads, depth_upload_mb);
    const double empty_draws = take(c.empty_draws_skipped) / s;
    const double rect_copies = take(c.rect_copies) / s;
    const double faces = take(c.shadow_faces_redirected) / s;
    const double copies_left_out = take(c.shadow_copies_skipped) / s;
    const double restores_before = take(c.shadow_restores_before) / s;
    const double restores_after = take(c.shadow_restores_after) / s;
    const double clears_folded = take(c.shadow_clears_folded) / s;
    const double clears_late = take(c.shadow_clears_late) / s;
    std::printf("  GPU steps per second (0.4.5): empty draws skipped %.1f; rectangle copies "
                "(a040875f) %.1f; lamp shadow faces drawn into the scratch image %.1f, their "
                "copies left out %.1f, faces put back %.1f before the copy and %.1f after; their "
                "clears in the face's pass %.1f, on their own %.1f\n",
                empty_draws, rect_copies, faces, copies_left_out, restores_before,
                restores_after, clears_folded, clears_late);
}

} // namespace Vulkan::GpuSavings

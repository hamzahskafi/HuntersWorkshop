// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: GPU work left out or merged, with kill switches, and its BB_FRAME_STATS report.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace Vulkan::GpuSavings {

/// An environment switch on by default: BB_x=0 turns it off.
inline bool EnvOn(const char* name) {
    const char* env = std::getenv(name);
    return !(env && env[0] == '0');
}

/// BB_IMAGE_TILE_MEMO=0: a texel buffer aliasing an image is filled from the image (download +
/// tiling) at every binding, as before, also when neither changed since the last fill.
inline bool TileMemoOn() {
    static const bool on = EnvOn("BB_IMAGE_TILE_MEMO");
    return on;
}
/// BB_HLE_COPY_MERGE=1: the copy shader's batches merged into one dispatch per guest dispatch.
/// Off by default: rainbow noise in some ground textures was seen with it on (beta 8 test).
inline bool HleMergeOn() {
    static const bool on = [] {
        const char* env = std::getenv("BB_HLE_COPY_MERGE");
        return env && env[0] == '1';
    }();
    return on;
}

struct Counters {
    // Texel buffer fills from images (BufferCache::SynchronizeMemoryFromImage).
    std::atomic<std::uint64_t> tile_fills{0}, tile_skips{0}, tile_bytes_skipped{0};
    // Why a fill was not skipped although a memo for the range existed.
    std::atomic<std::uint64_t> tile_miss_image{0}, tile_miss_buffer{0}, tile_miss_proxy{0},
        tile_miss_other{0};
    // HLE copy shader: batches, and dispatches/barrier checks recorded for them.
    std::atomic<std::uint64_t> hle_batches{0}, hle_dispatches{0}, hle_barriers{0};
    // Lookahead thread: sleeps instead of yields while stage B is stuck (BB_LOOKAHEAD_SLEEP).
    std::atomic<std::uint64_t> lookahead_sleeps{0};
    // Scene proxy resolves by the native access that needed them: transfer (copies, downloads,
    // texel buffer fills), sampled read, storage write, native attachment, other.
    std::array<std::atomic<std::uint64_t>, 5> scene_resolves{};
    // 0.4.4 beta 18: render passes begun again with the same targets after something ended them
    // (a split), by what ended them (PassBreak; beta 19: every place that ends a pass named).
    std::array<std::atomic<std::uint64_t>, 13> pass_splits{};
    // 0.4.4 beta 19: barriers that ended an open pass, by what they wait for (BarrierWait): the
    // work that wrote what the next commands read.
    std::array<std::atomic<std::uint64_t>, 6> pass_barrier_waits{};
    // 0.4.4 beta 18: copy shader dispatches recorded inside a render pass: with no barrier needed
    // before them (nothing since the pass began wrote their source or touched their destination)
    // and their source a snapshot in the stream buffer; the same with their source the game's
    // memory; needing a barrier.
    std::array<std::atomic<std::uint64_t>, 3> hle_in_pass{};
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
};

/// What ended a render pass (Scheduler::EndRendering's reason; beta 19: named where it ends).
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
    Submission
};
/// What a barrier that ended a pass waits for (its source stages).
enum class BarrierWait : std::uint8_t {
    GraphicsShaders,
    Attachments,
    Compute,
    Transfer,
    Host,
    Other
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
    double splits[13], waits[6], in_pass[3], kinds[7], refusals[7], color_refusals[7],
        overlaps[2];
    for (int i = 0; i < 13; ++i) {
        splits[i] = take(c.pass_splits[i]) / s;
    }
    for (int i = 0; i < 6; ++i) {
        waits[i] = take(c.pass_barrier_waits[i]) / s;
    }
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
                "%.1f) [BB_IMAGE_TILE_MEMO]; HLE copy %.0f batches in %.0f dispatches, %.0f "
                "barrier flushes [BB_HLE_COPY_MERGE]; lookahead sleeps %.0f "
                "[BB_LOOKAHEAD_SLEEP]\n",
                fills / s, skips / s, skipped_mb / s, mi / s, mb / s, mp / s, mo / s, batches / s,
                dispatches / s, barriers / s, sleeps / s);
    std::printf("  scene proxy resolves per second by native access: transfer %.1f, sampled %.1f, "
                "storage %.1f, attachment %.1f, other %.1f\n",
                resolves[0] / s, resolves[1] / s, resolves[2] / s, resolves[3] / s, resolves[4] / s);
    std::printf("  render passes per second begun again with the same targets, by what ended them: "
                "buffer barrier %.1f, image barrier %.1f, transfer %.1f, texture cache %.1f, scene "
                "proxy %.1f, motion %.1f, upscaler or presentation %.1f, profiler %.1f, submission "
                "%.1f, copy shader %.1f, image copy %.1f, other dispatch %.1f, other %.1f; "
                "barriers per second that ended a pass, waiting for graphics shaders %.1f, "
                "attachments %.1f, compute %.1f, transfers %.1f, host %.1f, other %.1f; copy "
                "shader dispatches inside a pass %.1f (no barrier needed and the source a snapshot "
                "%.1f, the source the game's memory %.1f; a barrier needed %.1f)\n",
                splits[4], splits[5], splits[6], splits[7], splits[8], splits[9], splits[10],
                splits[11], splits[12], splits[1], splits[2], splits[3], splits[0], waits[0],
                waits[1], waits[2], waits[3], waits[4], waits[5],
                in_pass[0] + in_pass[1] + in_pass[2], in_pass[0], in_pass[1], in_pass[2]);
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
}

} // namespace Vulkan::GpuSavings

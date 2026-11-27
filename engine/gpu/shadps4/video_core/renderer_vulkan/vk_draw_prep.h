// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: speculative draw preparation on worker threads (docs/parallel_gpu.md, steps 1-2).
//
// Every submitted graphics command buffer is copied. A scanner thread replays the register
// writes of the whole stream in order (ApplyGraphicsRegisterPacket, the same code the GPU
// thread runs) and records, per buffer, its starting checksum and a delta of the register
// blocks it wrote. Any number of workers (one per spare hardware thread) claim the nearest
// buffers ahead of the GPU thread, reach their starting state by applying deltas, and select
// the graphics pipeline and resource sharps of each direct draw. All helpers run as
// SCHED_IDLE: they only use idle cores. The GPU thread uses a prepared draw only when the
// running register checksums match and the flattened user data it computes itself equals the
// worker's; otherwise it takes the regular path. Workers never create or compile anything; since
// 0.4.6 beta 17 they queue a draw's untranslated shaders on the compile threads (translate_ahead.h).

#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

#include "bbport_role_lock.h"
#include "common/types.h"
#include "video_core/amdgpu/regs.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"

namespace Vulkan {

/// FilterDraw's checks without its side effects, on any register state: false when the GPU thread
/// skips the draw or runs a pass of its own for it (fast clear elimination, FMASK decompression,
/// resolve, no primitive type, depth/stencil copy), so it never looks up a pipeline for it
/// (Rasterizer::Draw). 0.4.6 beta 17: the draw-preparation workers leave those draws too.
bool DrawSelectsPipeline(const AmdGpu::Regs& regs);

struct PreparedStage {
    const Program* program;
    u64 hash;
    Shader::HwStage hw_stage;
    VAddr pgm_base;
    const u32* flat; ///< flattened user data the worker computed, owned by the submission
    u32 flat_size;
    // Resource sharps read from `flat` in the order of the program's resource lists, and the
    // texture description hash of each image (docs/parallel_gpu.md, step 2). Valid for the
    // GPU thread exactly when `flat` matched: sharps depend on nothing else.
    const AmdGpu::Image* image_sharps;
    const u64* image_hashes;
    const AmdGpu::Sampler* sampler_sharps;
    const AmdGpu::Buffer* buffer_sharps;
    u32 num_images, num_samplers, num_buffers;
};

/// Key of Rasterizer's texture description cache for a T# bound through `res`.
inline u64 ImageDescHash(const AmdGpu::Image& sharp, const Shader::ImageResource& res) {
    std::array<u64, 4> key;
    std::memcpy(key.data(), &sharp, sizeof(key));
    const u32 flags = u32(res.is_written) | u32(res.is_depth) << 1 | u32(res.is_array) << 2;
    u64 hash = flags * 0x9E3779B97F4A7C15ull;
    for (const u64 word : key) {
        hash = (hash ^ word) * 0xFF51AFD7ED558CCDull;
        hash ^= hash >> 32;
    }
    return hash;
}

/// Vertex inputs of a draw (the dynamic vertex input path): the attribute and binding
/// descriptions, the V# of each stream, and the stream memory merged into ranges.
struct PreparedVertexInputs {
    struct Range {
        VAddr base, end;
    };
    const vk::VertexInputAttributeDescription2EXT* attributes;
    const vk::VertexInputBindingDescription2EXT* bindings;
    const AmdGpu::Buffer* buffers;
    const u8* range_index; ///< per stream with memory: its entry in `ranges`
    const Range* ranges;
    u32 count, num_ranges;
    u64 buffers_hash; ///< XXH3 of `buffers` (object motion stream identity)
    bool valid;
};

struct PreparedDraw {
    enum : u32 { Pending = 0, Ready = 1, Unavailable = 2 };
    std::atomic<u32> state{Pending};
    u64 reg_checksum{};
    GraphicsPipelineKey key{};
    u32 num_stages{};
    std::array<PreparedStage, MaxShaderStages> stages{};
    PreparedVertexInputs vertex{};
};

/// One top-level graphics command buffer, copied for the workers. Outside DrawPreparation so the
/// decode task (liverpool.h) can carry it by a forward declaration.
struct PrepSubmission {
    u64 seq{};
    std::vector<u32> commands;
    std::unique_ptr<PreparedDraw[]> draws;
    u32 num_draws{};
    std::vector<std::unique_ptr<u32[]>> flat_chunks; ///< from the claiming worker
    u64 start_checksum{}, end_checksum{}; ///< set by the scanner
    AmdGpu::RegDelta delta;                ///< registers written, values at the end
    std::atomic<bool> scanned{false};
    std::atomic<bool> claimed{false};
    std::atomic<bool> gpu_done{false};
    /// 0.4.6 beta 20: the hand-over from the game's submit to the scanner (a lock-free list).
    PrepSubmission* next_incoming{};
    std::shared_ptr<PrepSubmission> incoming_ref;
};

class DrawPreparation {
public:
    explicit DrawPreparation(PipelineCache& pipeline_cache);
    ~DrawPreparation();

    DrawPreparation(const DrawPreparation&) = delete;
    DrawPreparation& operator=(const DrawPreparation&) = delete;

    [[nodiscard]] bool Enabled() const noexcept {
        return worker_count != 0;
    }

    using Submission = PrepSubmission;
    /// Game submit thread, outside the queue lock: copies and scans a top-level graphics
    /// command buffer. Null when disabled.
    std::shared_ptr<Submission> Build(std::span<const u32> commands);
    /// Game submit thread, under the queue lock (the submitting threads' own): hands it to the
    /// scanner in submission order. 0.4.6 beta 20 (docs/chain-waits.md, item 1): through a lock-free
    /// list, which the scanner moves into the queue; the game's threads take none of the draw
    /// preparation's locks.
    void Enqueue(u64 seq, std::shared_ptr<Submission> submission, bool notify = true);
    /// The scanner told of buffers enqueued with `notify` false (0.4.6 beta 19: the game's submit
    /// wakes it after letting the submission queue's lock go). A system call only when it sleeps.
    void NotifyWorkers();

    /// GPU thread: brackets the processing of submission `seq`, whose record (`prepared`: from
    /// Build, through the decode task) it is handed. The first call hands the workers the exact
    /// register state and checksum they start replaying from. 0.4.6 beta 20 (docs/chain-waits.md,
    /// item 1): no lock waited for: the record comes with the task, a finished submission is only
    /// marked (the scanner collects it), and a rebase is tried without waiting.
    void BeginSubmission(u64 seq, const AmdGpu::Regs& regs, u64 reg_checksum,
                         std::shared_ptr<Submission> prepared);
    void EndSubmission();

    /// GPU thread: the prepared state for the next direct draw of the current submission.
    const PreparedDraw* NextDraw();

    /// GPU thread: counts whether a prepared draw was used; prints every 5 s (BB_FRAME_STATS).
    void Count(bool used);

private:
    void ScannerLoop(std::stop_token stop);
    void WorkerLoop(std::stop_token stop, u32 index);
    /// The scanner, under `mutex`: the game's new submissions into the queue; finished ones off
    /// its front, moving the restart state.
    void DrainIncoming();
    void Collect();
    /// Wakes the scanner when it sleeps (a change it must see is made before).
    void WakeScanner();

    PipelineCache& pipeline_cache;
    std::unique_ptr<AmdGpu::Regs> initial_regs; ///< state at the start of `baseline_seq`
    u64 initial_checksum{};
    u64 baseline_seq{};
    /// State at the start of the oldest queued buffer (`tail_seq`): a worker whose position
    /// was collected restarts from here. Guarded by `mutex`.
    std::unique_ptr<AmdGpu::Regs> tail_regs;
    u64 tail_seq{};
    u64 tail_checksum{};
    u64 rebases = 0; ///< scanner restarts from the GPU thread state (BB_FRAME_STATS)
    bool baseline_ready{}; ///< guarded by `mutex`
    /// Taken by the scanner and the workers (the Assist role); the GPU thread only tries it, for a
    /// rebase (and takes it once, for the baseline).
    BbRoles::Mutex mutex{"draw preparation"}; ///< role-aware: the GPU thread's rare takes counted
    std::condition_variable_any cv; ///< the workers' wait: the scanner wakes one a buffer scanned
    std::deque<std::shared_ptr<Submission>> submissions; ///< ordered by seq; the scanner adds
    std::atomic<Submission*> incoming{nullptr}; ///< newest first; the game's submit pushes
    std::atomic<u32> scan_word{0};              ///< bumped for the scanner (bbport_park.h)
    std::atomic<bool> scan_sleeping{false};
    std::atomic<u64> scan_next{0}; ///< the scanner's next buffer, for the GPU thread's lag check
    std::atomic<u64> gpu_seq{0};
    std::shared_ptr<Submission> current;
    bool gpu_baseline_set = false; ///< GPU thread only

public:
    /// bbport: the current submission (prepared draws), for the draw recording thread to keep
    /// it alive while it still records its draws.
    [[nodiscard]] std::shared_ptr<const void> KeepCurrent() const {
        return current;
    }

private:
    u32 current_draw = 0;
    u64 used = 0, unused = 0;
    /// Draws the workers left because the GPU thread looks up no pipeline for them
    /// (DrawSelectsPipeline; 0.4.6 beta 17), since the last report.
    std::atomic<u64> passes_left{0};
    u32 worker_count = 0; ///< fixed before the worker threads start
    std::jthread scanner;
    std::vector<std::jthread> workers;
};

} // namespace Vulkan

// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: speculative draw preparation on worker threads (see vk_draw_prep.h). 0.4.6 beta 17 part 2: a
// draw whose shader or variant is not translated yet has it queued on the compile threads
// (PipelineCache::QueueAhead, translate_ahead.h); the draw itself stays the GPU thread's.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bbport_native_calls.h"
#include "bbport_park.h"
#include "bbport_roles.h"
#include "bbport_threads.h"
#include "bbport_toggles.h"
#include "common/thread.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/amdgpu/pm4_cmds.h"
#include "video_core/renderer_vulkan/vk_draw_prep.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include <xxhash.h>

namespace Vulkan {

namespace {

bool IsDirectDraw(const AmdGpu::PM4Header* header) {
    using AmdGpu::PM4ItOpcode;
    const PM4ItOpcode opcode = header->type3.opcode;
    // bbport BB_NATIVE_CALLS=1: a draw call's record NOP (bbport_native_calls.h) is a direct
    // draw; ApplyGraphicsRegisterPacket applies it as the DRAW_INDEX_* packet it stands for.
    return opcode == PM4ItOpcode::DrawIndex2 || opcode == PM4ItOpcode::DrawIndexOffset2 ||
           opcode == PM4ItOpcode::DrawIndexAuto ||
           (opcode == PM4ItOpcode::Nop &&
            BbNativeCalls::IsRecordNop(reinterpret_cast<const u32*>(header)));
}

/// Walks the type-3 packets of a command buffer (type-2 padding skipped).
template <typename Func>
void ForEachPacket(std::span<const u32> commands, Func&& func) {
    for (size_t at = 0; at < commands.size();) {
        const auto* header = reinterpret_cast<const AmdGpu::PM4Header*>(commands.data() + at);
        if (header->type == 2) {
            ++at;
            continue;
        }
        if (header->type != 3) {
            return;
        }
        const size_t words = header->type3.NumWords() + 1;
        if (at + words > commands.size()) {
            return;
        }
        func(header);
        at += words;
    }
}

/// BB_PREP_HANDOVER=old (0.4.6 beta 20 only): the GPU thread takes the draw preparation's lock at
/// each submission's start (waiting for a rebase) and end (collecting) as before.
bool OldHandover() {
    static const bool old = [] {
        const char* env = std::getenv("BB_PREP_HANDOVER");
        return env && std::strcmp(env, "old") == 0;
    }();
    return old;
}

u32 DefaultWorkerCount() {
    // BB_PREP_WORKERS. Workers are idle priority (the Assist role) and claim whole buffers.
    return BbRoles::PoolSize(BbRoles::Pool::DrawPrep);
}

/// Flattened user data of one submission: chunks never move, so the GPU thread can read a
/// prepared draw's data while the worker keeps appending.
struct FlatArena {
    static constexpr size_t ChunkWords = 16 * 1024;
    std::vector<std::unique_ptr<u32[]>> chunks;
    size_t used = ChunkWords;

    const u32* Append(const std::vector<u32>& data) {
        return AppendArray(data.data(), data.size());
    }

    /// Trivially copyable elements, 8-byte aligned (sharps hold u64 bit fields).
    template <typename T>
    const T* AppendArray(const T* data, size_t count) {
        static_assert(std::is_trivially_copyable_v<T> && alignof(T) <= 8);
        const size_t words = (count * sizeof(T) + sizeof(u32) - 1) / sizeof(u32);
        used = (used + 1) & ~size_t(1);
        if (chunks.empty() || words > ChunkWords - std::min(used, ChunkWords)) {
            chunks.push_back(std::make_unique<u32[]>(std::max(ChunkWords, words)));
            used = 0;
        }
        u32* dst = chunks.back().get() + used;
        if (count) {
            std::memcpy(dst, data, count * sizeof(T));
        }
        used += words;
        return reinterpret_cast<const T*>(dst);
    }
};

/// Reads the stage's resource sharps from the worker's flattened user data, so the GPU thread
/// only binds them (the sharp fetches and texture-description hashing were ~7% of it).
void PrepareResources(const Shader::Info& info, PreparedStage& stage, FlatArena& arena) {
    thread_local std::vector<AmdGpu::Image> images;
    thread_local std::vector<u64> hashes;
    thread_local std::vector<AmdGpu::Sampler> samplers;
    thread_local std::vector<AmdGpu::Buffer> buffers;
    images.clear();
    hashes.clear();
    samplers.clear();
    buffers.clear();
    for (const auto& res : info.images) {
        images.push_back(res.GetSharp(info));
        hashes.push_back(ImageDescHash(images.back(), res));
    }
    for (const auto& res : info.samplers) {
        samplers.push_back(res.GetSharp(info));
    }
    for (const auto& res : info.buffers) {
        buffers.push_back(res.IsSpecial() ? AmdGpu::Buffer::Null() : res.GetSharp(info));
    }
    stage.image_sharps = arena.AppendArray(images.data(), images.size());
    stage.image_hashes = arena.AppendArray(hashes.data(), hashes.size());
    stage.sampler_sharps = arena.AppendArray(samplers.data(), samplers.size());
    stage.buffer_sharps = arena.AppendArray(buffers.data(), buffers.size());
    stage.num_images = static_cast<u32>(images.size());
    stage.num_samplers = static_cast<u32>(samplers.size());
    stage.num_buffers = static_cast<u32>(buffers.size());
}

/// Rasterizer::BindVertexBuffers' pure part (GraphicsPipeline::GetVertexInputs for the dynamic
/// vertex input path, and the merge of the stream memory into ranges).
void PrepareVertexInputs(const Shader::Info& vs, const Shader::Gcn::FetchShaderData& fetch,
                         const AmdGpu::Regs& regs, PreparedVertexInputs& out, FlatArena& arena) {
    using InstanceIdType = Shader::Gcn::VertexAttribute::InstanceIdType;
    thread_local std::vector<vk::VertexInputAttributeDescription2EXT> attributes;
    thread_local std::vector<vk::VertexInputBindingDescription2EXT> bindings;
    thread_local std::vector<AmdGpu::Buffer> buffers;
    thread_local std::vector<PreparedVertexInputs::Range> ranges, merged;
    thread_local std::vector<u8> range_index;
    attributes.clear();
    bindings.clear();
    buffers.clear();
    ranges.clear();
    merged.clear();
    range_index.clear();
    for (const auto& attrib : fetch.attributes) {
        const auto step_rate = attrib.GetStepRate();
        const auto buffer = attrib.GetSharp(vs);
        // The V# may come from user data the guest is still writing: an unknown format leaves
        // the inputs to the GPU thread, which reads them once the draw is submitted (the
        // assertion in SurfaceFormat stopped the process here).
        const vk::Format format =
            LiverpoolToVK::TrySurfaceFormat(buffer.GetDataFmt(), buffer.GetNumberFmt());
        if (format == vk::Format::eUndefined &&
            buffer.GetDataFmt() != AmdGpu::DataFormat::FormatInvalid) {
            out.valid = false;
            return;
        }
        attributes.push_back({
            .location = attrib.semantic,
            .binding = attrib.semantic,
            .format = format,
            .offset = 0,
        });
        bindings.push_back({
            .binding = attrib.semantic,
            .stride = buffer.GetStride(),
            .inputRate = step_rate == InstanceIdType::None ? vk::VertexInputRate::eVertex
                                                           : vk::VertexInputRate::eInstance,
            .divisor = step_rate == InstanceIdType::OverStepRate0 ? regs.vgt_instance_step_rate_0
                       : step_rate == InstanceIdType::OverStepRate1
                           ? regs.vgt_instance_step_rate_1
                           : 1u,
        });
        buffers.push_back(buffer);
        if (buffer.base_address != 0 && buffer.GetSize() > 0) {
            ranges.push_back({buffer.base_address, buffer.base_address + buffer.GetSize()});
        }
    }
    std::ranges::sort(ranges, {}, &PreparedVertexInputs::Range::base);
    for (const auto& range : ranges) {
        if (merged.empty() || merged.back().end < range.base) {
            merged.push_back(range);
        } else {
            merged.back().end = std::max(merged.back().end, range.end);
        }
    }
    for (const auto& buffer : buffers) {
        if (buffer.base_address != 0 && buffer.GetSize() > 0) {
            const auto it = std::ranges::find_if(merged, [&](const auto& range) {
                return buffer.base_address >= range.base && buffer.base_address < range.end;
            });
            range_index.push_back(static_cast<u8>(it - merged.begin()));
        }
    }
    out.attributes = arena.AppendArray(attributes.data(), attributes.size());
    out.bindings = arena.AppendArray(bindings.data(), bindings.size());
    out.buffers = arena.AppendArray(buffers.data(), buffers.size());
    out.range_index = arena.AppendArray(range_index.data(), range_index.size());
    out.ranges = arena.AppendArray(merged.data(), merged.size());
    out.count = static_cast<u32>(attributes.size());
    out.num_ranges = static_cast<u32>(merged.size());
    out.buffers_hash =
        buffers.empty() ? 0 : XXH3_64bits(buffers.data(), buffers.size() * sizeof(AmdGpu::Buffer));
    out.valid = true;
}

} // namespace

DrawPreparation::DrawPreparation(PipelineCache& pipeline_cache_)
    : pipeline_cache{pipeline_cache_} {
    worker_count = DefaultWorkerCount();
    if (worker_count) {
        scanner = std::jthread([this](std::stop_token stop) { ScannerLoop(stop); });
    }
    for (u32 i = 0; i < worker_count; ++i) {
        workers.emplace_back([this, i](std::stop_token stop) { WorkerLoop(stop, i); });
    }
    std::printf("GPU: draw preparation workers: %u (+1 scanner, %u hardware threads)\n",
                worker_count, BbThreads::Available());
}

DrawPreparation::~DrawPreparation() {
    for (auto& worker : workers) {
        worker.request_stop();
    }
    if (scanner.joinable()) {
        scanner.request_stop();
        BbPark::Bump(scan_word, true);
    }
    cv.notify_all();
    workers.clear();
    scanner = {};
    // Buffers handed over but never moved into the queue.
    for (Submission* p = incoming.exchange(nullptr); p;) {
        Submission* next = p->next_incoming;
        p->incoming_ref.reset();
        p = next;
    }
}

std::shared_ptr<DrawPreparation::Submission> DrawPreparation::Build(
    std::span<const u32> commands) {
    if (!Enabled()) {
        return {};
    }
    auto submission = std::make_shared<Submission>();
    submission->commands.assign(commands.begin(), commands.end());
    ForEachPacket(submission->commands, [&](const AmdGpu::PM4Header* header) {
        submission->num_draws += IsDirectDraw(header);
    });
    submission->draws = std::make_unique<PreparedDraw[]>(submission->num_draws);
    return submission;
}

void DrawPreparation::Enqueue(u64 seq, std::shared_ptr<Submission> submission, bool notify) {
    if (!submission) {
        return;
    }
    Submission* const p = submission.get();
    p->seq = seq;
    p->incoming_ref = std::move(submission); // the list's reference, moved into the queue later
    Submission* head = incoming.load(std::memory_order_relaxed);
    do {
        p->next_incoming = head;
    } while (!incoming.compare_exchange_weak(head, p, std::memory_order_release,
                                             std::memory_order_relaxed));
    if (notify) {
        WakeScanner();
    }
}

void DrawPreparation::NotifyWorkers() {
    WakeScanner();
}

void DrawPreparation::WakeScanner() {
    // With the scanner's sleep (ScannerLoop): it announces it, then looks at the word again, so
    // either it sees this bump or this sees it asleep.
    scan_word.fetch_add(1, std::memory_order_seq_cst);
    if (scan_sleeping.load(std::memory_order_seq_cst)) {
        BbPark::WakeOne(scan_word);
    }
}

void DrawPreparation::BeginSubmission(u64 seq, const AmdGpu::Regs& regs, u64 reg_checksum,
                                      std::shared_ptr<Submission> prepared) {
    current_draw = 0;
    current = std::move(prepared);
    if (!Enabled()) {
        current.reset();
        return;
    }
    gpu_seq.store(seq, std::memory_order_release);
    if (!gpu_baseline_set) {
        // Once, at the first submission: the state everyone starts from.
        gpu_baseline_set = true;
        {
            std::scoped_lock lk{mutex};
            initial_regs = std::make_unique<AmdGpu::Regs>(regs);
            tail_regs = std::make_unique<AmdGpu::Regs>(regs);
            initial_checksum = tail_checksum = reg_checksum;
            baseline_seq = tail_seq = seq;
            baseline_ready = true;
        }
        WakeScanner();
        return;
    }
    // Idle helpers can starve on a busy CPU. Rather than queueing without bound, restart
    // them from the GPU thread's own state (one register copy) once the scanner lags. 0.4.6 beta
    // 20: only if the lock is free now; else at a later submission (the helpers hold it).
    constexpr u64 MaxScanLag = 64;
    if (seq <= scan_next.load(std::memory_order_acquire) + MaxScanLag && !OldHandover()) {
        return;
    }
    std::unique_lock lk{mutex, std::defer_lock};
    if (OldHandover()) {
        lk.lock();
    } else if (!lk.try_lock()) {
        return;
    }
    if (seq > tail_seq + MaxScanLag && !submissions.empty() && submissions.front()->seq < seq &&
        !submissions.front()->scanned.load()) {
        *tail_regs = regs;
        tail_seq = seq;
        tail_checksum = reg_checksum;
        while (!submissions.empty() && submissions.front()->seq < seq) {
            submissions.pop_front();
        }
        ++rebases;
        lk.unlock();
        WakeScanner();
    }
}

void DrawPreparation::EndSubmission() {
    if (current) {
        // Its reference let go before it is marked: the queue's keeps it until the scanner
        // collects it (after the mark), so it is freed there, not on this thread.
        Submission* const done = current.get();
        current.reset();
        done->gpu_done.store(true, std::memory_order_release);
    }
    if (Enabled() && OldHandover()) {
        std::scoped_lock lk{mutex};
        Collect();
    }
}

void DrawPreparation::DrainIncoming() {
    Submission* list = incoming.exchange(nullptr, std::memory_order_acquire);
    // Newest first: reversed into submission order.
    Submission* ordered = nullptr;
    while (list) {
        Submission* next = list->next_incoming;
        list->next_incoming = ordered;
        ordered = list;
        list = next;
    }
    while (ordered) {
        Submission* next = ordered->next_incoming;
        ordered->next_incoming = nullptr;
        auto ref = std::move(ordered->incoming_ref);
        // A rebase passed it (its draws are the GPU thread's): never queued.
        if (!baseline_ready || ref->seq >= tail_seq) {
            submissions.push_back(std::move(ref));
        }
        ordered = next;
    }
}

const PreparedDraw* DrawPreparation::NextDraw() {
    if (!current || current_draw >= current->num_draws) {
        return nullptr;
    }
    const PreparedDraw* draw = &current->draws[current_draw++];
    if (BbToggle::Disabled(BbToggle::DrawPreparation)) {
        return nullptr;
    }
    return draw;
}

void DrawPreparation::Collect() {
    while (!submissions.empty()) {
        const auto& front = submissions.front();
        if (!baseline_ready || !front->gpu_done.load(std::memory_order_acquire)) {
            break;
        }
        if (front->seq >= baseline_seq) {
            // Its delta moves the restart state to the start of the next buffer.
            if (!front->scanned.load(std::memory_order_acquire)) {
                break;
            }
            front->delta.Apply(*tail_regs);
            tail_seq = front->seq + 1;
            tail_checksum = front->end_checksum;
        }
        submissions.pop_front();
    }
}

void DrawPreparation::ScannerLoop(std::stop_token stop) {
    Common::SetCurrentThreadName("bb:DrawScan");
    BbThreads::MakeBackground();
    std::unique_ptr<AmdGpu::Regs> regs;
    u64 checksum = 0;
    u64 next_seq = 0;
    AmdGpu::RegDirty dirty;
    while (!stop.stop_requested()) {
        // 0.4.6 beta 20 (docs/chain-waits.md, item 1): it sleeps on a word of its own; the game's
        // submit and the GPU thread bump it after their change, without the lock.
        const u32 word = scan_word.load(std::memory_order_seq_cst);
        std::shared_ptr<Submission> submission;
        {
            std::scoped_lock lk{mutex};
            DrainIncoming();
            if (baseline_ready) {
                Collect();
                if (!regs) {
                    regs = std::make_unique<AmdGpu::Regs>(*initial_regs);
                    checksum = initial_checksum;
                    next_seq = baseline_seq;
                    scan_next.store(next_seq, std::memory_order_release);
                }
                if (next_seq < tail_seq) {
                    // Rebased by the GPU thread: continue from its state.
                    *regs = *tail_regs;
                    checksum = tail_checksum;
                    next_seq = tail_seq;
                    scan_next.store(next_seq, std::memory_order_release);
                }
                // Unscanned buffers are only collected by a rebase, so `next_seq` is still queued.
                if (!submissions.empty() && next_seq >= submissions.front()->seq &&
                    next_seq - submissions.front()->seq < submissions.size()) {
                    submission = submissions[next_seq - submissions.front()->seq];
                }
            }
        }
        if (!submission) {
            scan_sleeping.store(true, std::memory_order_seq_cst);
            if (scan_word.load(std::memory_order_seq_cst) == word && !stop.stop_requested()) {
                BbPark::Wait(scan_word, word);
            }
            scan_sleeping.store(false, std::memory_order_relaxed);
            continue;
        }
        submission->start_checksum = checksum;
        dirty.Clear();
        ForEachPacket(submission->commands, [&](const AmdGpu::PM4Header* header) {
            AmdGpu::Liverpool::ApplyGraphicsRegisterPacket(*regs, header, checksum, &dirty);
        });
        submission->delta.Capture(*regs, dirty);
        submission->end_checksum = checksum;
        {
            std::scoped_lock lk{mutex};
            submission->scanned.store(true, std::memory_order_release);
            Collect();
        }
        ++next_seq;
        scan_next.store(next_seq, std::memory_order_release);
        // One buffer to claim: one worker (a busy one looks again when it finishes its own).
        cv.notify_one();
    }
}

void DrawPreparation::WorkerLoop(std::stop_token stop, u32 index) {
    Common::SetCurrentThreadName(("bb:DrawPrep" + std::to_string(index)).c_str());
    BbThreads::MakeBackground();
    bb_fault_recovers_only = true; // its faults leave the draw to the GPU thread
    auto regs = std::make_unique<AmdGpu::Regs>();
    constexpr u64 NoPosition = ~0ull;
    u64 position = NoPosition; ///< `regs` holds the state at the start of this buffer
    PipelineSelection sel{};
    sel.regs = regs.get();
    PrepWorker prep_worker{};
    sel.worker = &prep_worker;
    FlatArena arena;
    std::vector<std::shared_ptr<Submission>> path;

    // The nearest scanned buffer the GPU thread has not reached and no worker has claimed.
    const auto claimable = [&]() -> std::shared_ptr<Submission> {
        const u64 gpu = gpu_seq.load(std::memory_order_acquire);
        for (const auto& submission : submissions) {
            if (submission->seq >= gpu && submission->seq >= tail_seq &&
                submission->scanned.load(std::memory_order_acquire) &&
                !submission->claimed.load(std::memory_order_relaxed)) {
                return submission;
            }
        }
        return {};
    };

    while (!stop.stop_requested()) {
        std::shared_ptr<Submission> target;
        {
            std::unique_lock lk{mutex};
            // gpu_seq moves without the lock: keep the candidate the predicate found.
            cv.wait(lk, stop, [&] {
                target = baseline_ready ? claimable() : nullptr;
                return target != nullptr;
            });
            if (stop.stop_requested() || !target) {
                return;
            }
            target->claimed.store(true, std::memory_order_relaxed);
            // Buffers between this worker's state and the target: their deltas. A position
            // already collected (or past the target) restarts from the queue's tail state.
            if (position == NoPosition || position < tail_seq || position > target->seq) {
                *regs = *tail_regs;
                position = tail_seq;
            }
            const u64 front = submissions.front()->seq;
            path.clear();
            for (u64 seq = position; seq < target->seq; ++seq) {
                path.push_back(submissions[seq - front]);
            }
        }
        for (const auto& step : path) {
            step->delta.Apply(*regs);
        }
        path.clear();
        u64 checksum = target->start_checksum;
        u32 ordinal = 0;
        arena = FlatArena{};
        ForEachPacket(target->commands, [&](const AmdGpu::PM4Header* header) {
            AmdGpu::Liverpool::ApplyGraphicsRegisterPacket(*regs, header, checksum);
            if (!IsDirectDraw(header)) {
                return;
            }
            // Past the GPU thread the draws are no longer worth preparing.
            if (ordinal < target->num_draws &&
                target->seq >= gpu_seq.load(std::memory_order_acquire)) {
                auto& draw = target->draws[ordinal];
                // bbport: the draw's other guest reads (shader headers in GetParams, the vertex
                // buffer table in PrepareVertexInputs) recover too. Handled, such a fault sent a
                // readback to the GPU command thread and waited for it; on memory the guest has
                // protected or freed meanwhile it faulted again forever, or ended the game.
                // GetProgram's own recovery point (inside programs_mutex) hands back to this one.
                BbRecoverBuf recover;
                if (BB_RECOVER_SET(recover)) {
                    runtime_fault_recover = nullptr;
                    draw.state.store(PreparedDraw::Unavailable, std::memory_order_release);
                    ++ordinal;
                    return;
                }
                runtime_fault_recover = &recover;
                prep_worker.stages.clear();
                prep_worker.failed = false;
                prep_worker.ahead.clear();
                prep_worker.ahead_closed = false;
                sel.draw_indirect_params = {};
                // 0.4.6 beta 17: a draw the GPU thread looks up no pipeline for (a pass of its own,
                // or skipped) is not prepared: its stages, never translated there, would be queued
                // ahead at every frame and never taken (the "never taken" of the shader report).
                if (!DrawSelectsPipeline(*regs)) {
                    passes_left.fetch_add(1, std::memory_order_relaxed);
                    runtime_fault_recover = nullptr;
                    draw.state.store(PreparedDraw::Unavailable, std::memory_order_release);
                    ++ordinal;
                    return;
                }
                if (pipeline_cache.PrepareGraphicsPipeline(sel)) {
                    draw.reg_checksum = checksum;
                    draw.key = sel.graphics_key;
                    draw.num_stages = static_cast<u32>(prep_worker.stages.size());
                    for (u32 i = 0; i < draw.num_stages; ++i) {
                        const auto& stage = prep_worker.stages[i];
                        draw.stages[i] = {
                            .program = stage.program,
                            .hash = stage.hash,
                            .hw_stage = stage.hw_stage,
                            .pgm_base = stage.pgm_base,
                            .flat = arena.Append(*stage.flat),
                            .flat_size = static_cast<u32>(stage.flat->size()),
                        };
                        PrepareResources(prep_worker.infos.at(stage.program), draw.stages[i],
                                         arena);
                    }
                    draw.vertex.valid = false;
                    const auto* vs = sel.infos[static_cast<u32>(Shader::SwStage::Vertex)];
                    if (vs && sel.fetch_shader) {
                        PrepareVertexInputs(*vs, *sel.fetch_shader, *regs, draw.vertex, arena);
                    }
                    draw.state.store(PreparedDraw::Ready, std::memory_order_release);
                } else {
                    draw.state.store(PreparedDraw::Unavailable, std::memory_order_release);
                    // 0.4.6 beta 17 part 2: its stages not translated yet go to the compile threads,
                    // so the GPU thread finds them translated, or under way, when it gets here.
                    if (!prep_worker.ahead.empty()) {
                        runtime_fault_recover = nullptr;
                        pipeline_cache.QueueAhead(std::move(prep_worker.ahead));
                        prep_worker.ahead.clear();
                    }
                }
                runtime_fault_recover = nullptr;
            }
            ++ordinal;
        });
        // The arena's chunks live as long as the submission.
        target->flat_chunks = std::move(arena.chunks);
        position = target->seq + 1;
    }
}

void DrawPreparation::Count(bool was_used) {
    ++(was_used ? used : unused);
    static const bool stats = std::getenv("BB_FRAME_STATS") != nullptr;
    // The clock is read every 1024 draws: per draw it was 8% of the GPU thread.
    if (!stats || ((used + unused) & 1023) != 0) {
        return;
    }
    static auto window = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now - window < std::chrono::seconds(5)) {
        return;
    }
    window = now;
    std::printf("Draw preparation: %llu prepared pipelines used, %llu not (%u workers, "
                "%llu scanner rebases); left to the GPU thread's own passes %llu\n",
                static_cast<unsigned long long>(used), static_cast<unsigned long long>(unused),
                worker_count, static_cast<unsigned long long>(rebases),
                static_cast<unsigned long long>(passes_left.exchange(0, std::memory_order_relaxed)));
    used = unused = 0;
}

} // namespace Vulkan

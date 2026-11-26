// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include "shader_recompiler/native/tone_curve.h"
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>

#include "bbport_cpu_layout.h"
#include "bbport_log_level.h"
#include "bbport_threads.h"
#include "engine/frame_window.h"
#include "engine/startup.h"
#include "common/thread.h"
#include "common/hash.h"
#include "common/serdes.h"
#include "common/assert.h"
#include "shader_recompiler/backend/spirv/emit_spirv_discard_frag.h"
#include "shader_recompiler/backend/spirv/emit_spirv_quad_rect.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/pipeline_compile_queue.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_object_motion.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

using Shader::Backend::SPIRV::AuxShaderType;

/// bbport: pipelines first created during play go to compile threads (BB_ASYNC_PIPELINES=0:
/// on the GPU command thread, as before). Pipelines loaded from the cache at startup are
/// always created at once.
bool AsyncPipelinesEnabled() {
    static const bool enabled = [] {
        const char* env = std::getenv("BB_ASYNC_PIPELINES");
        return !(env && env[0] == '0');
    }();
    return enabled;
}

namespace {
/// A graphics pipeline's create info with everything it points to owned by the job.
struct PendingPipeline {
    vk::GraphicsPipelineCreateInfo info;
    boost::container::static_vector<vk::PipelineShaderStageCreateInfo, MaxShaderStages> stages;
    vk::PipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup_size;
    VertexInputs<vk::VertexInputBindingDescription> bindings;
    VertexInputs<vk::VertexInputAttributeDescription> attributes;
    VertexInputs<vk::VertexInputBindingDivisorDescriptionEXT> divisors;
    vk::PipelineVertexInputDivisorStateCreateInfo divisor_state;
    vk::PipelineVertexInputStateCreateInfo vertex_input;
    vk::PipelineInputAssemblyStateCreateInfo input_assembly;
    vk::PipelineTessellationStateCreateInfo tessellation;
    vk::PipelineViewportDepthClipControlCreateInfoEXT clip_control;
    vk::PipelineViewportStateCreateInfo viewport;
    vk::PipelineRasterizationStateCreateInfo raster;
    vk::PipelineRasterizationProvokingVertexStateCreateInfoEXT provoking;
    vk::PipelineRasterizationDepthClipStateCreateInfoEXT depth_clip;
    vk::PipelineMultisampleStateCreateInfo multisample;
    vk::PipelineDepthStencilStateCreateInfo depth_stencil;
    std::array<vk::PipelineColorBlendAttachmentState, AmdGpu::NUM_COLOR_BUFFERS> attachments;
    vk::PipelineColorBlendStateCreateInfo color_blend;
    boost::container::static_vector<vk::DynamicState, 32> dynamic_states;
    vk::PipelineDynamicStateCreateInfo dynamic;
    std::array<vk::Format, Shader::IR::NumRenderTargets> color_formats;
    std::array<vk::SampleCountFlagBits, AmdGpu::NUM_COLOR_BUFFERS> color_samples;
    vk::AttachmentSampleCountInfoAMD mixed_samples;
    vk::PipelineRenderingCreateInfo rendering;
};

/// Compile threads (BB_PIPELINE_THREADS, default a third of the hardware threads, 1-4), first
/// in, first out: the draw that needs a pipeline first gets it first. Never stopped: pipelines
/// destroyed at exit wait for their job (Pipeline::WaitReady).
/// 0.4.6 beta 15 part 1: burst threads beside them (BB_PIPELINE_BURST, bbport_cpu_layout.h): at
/// idle priority, on the efficiency cores where there are any (BB_CPU_SETS=0: anywhere), taking
/// jobs only in a backlog, raised while a thread waits for theirs.
class PipelineCompiler {
public:
    static PipelineCompiler& Get() {
        static auto* compiler = new PipelineCompiler();
        return *compiler;
    }

    PipelineJobQueue queue;
    u32 threads = 0;
    u32 burst_threads = 0;
    /// Burst thread i's handle (written by it before its first job, read under the queue's lock).
    void* burst_handles[PipelineJobQueue::kMaxBurstThreads]{};
    /// Since start, for the line on each load's first frames (part 5).
    std::atomic<u64> jobs_total{0}, compile_ns_total{0}, bursts_total{0}, burst_wall_ns_total{0};
    std::atomic<u32> burst_threads_most{0};

    static u64 NowNs() {
        return PipelineJobQueue::SteadyNs();
    }

    /// A job ended after `ns` on burst thread `burst_index` (-1: another thread).
    void Finish(int burst_index, u64 ns) {
        jobs_total.fetch_add(1, std::memory_order_relaxed);
        compile_ns_total.fetch_add(ns, std::memory_order_relaxed);
        if (const auto burst = queue.Done(burst_index, ns, NowNs())) {
            Report(*burst);
        }
    }

private:
    PipelineCompiler() {
        threads = std::clamp<u32>(BbThreads::Available() / 3, 1, 4);
        if (const char* env = std::getenv("BB_PIPELINE_THREADS"); env && env[0]) {
            threads = std::clamp(std::atoi(env), 1, 16);
        }
        for (u32 i = 0; i < threads; ++i) {
            std::thread([this, i] {
                Common::SetCurrentThreadName(("bb:Pipeline" + std::to_string(i)).c_str());
                for (;;) {
                    auto job = queue.Pop();
                    const u64 start = NowNs();
                    job();
                    Finish(-1, NowNs() - start);
                }
            }).detach();
        }
        // A burst's flips counted from its first job to its end (under the queue's lock).
        queue.edge = [](PipelineJobQueue::Burst* ended) {
            if (!ended) {
                BbEngine::Frames::Open(BbEngine::Frames::kBurst);
                return;
            }
            const BbEngine::Frames::Window w = BbEngine::Frames::Close(BbEngine::Frames::kBurst);
            ended->frames = w.frames;
            ended->frames_over_25 = w.over_25;
            ended->worst_ms = w.worst_ms;
        };
        StartBursts();
    }

    void StartBursts() {
        const BbCpu::Summary cpu = BbCpu::Summarize(BbCpu::Read());
        int forced = -1;
        if (const char* env = std::getenv("BB_PIPELINE_BURST"); env && env[0]) {
            forced = std::max(0, std::atoi(env));
        }
        const char* why = "";
        const bool reversible = BbCpu::PrioritiesReversible(&why);
        const char* sets_env = std::getenv("BB_CPU_SETS");
        const bool steer = !(sets_env && sets_env[0] == '0');
        const BbCpu::BurstPlan plan = BbCpu::PlanBursts(cpu, threads, forced, reversible, steer, why);
        std::printf("%s\n", BbCpu::Describe(cpu, threads, plan).c_str());
        std::fflush(stdout);
        burst_threads = std::min<u32>(plan.threads, PipelineJobQueue::kMaxBurstThreads);
        const std::vector<u32> sets = plan.on_efficiency ? cpu.efficiency_sets : std::vector<u32>{};
        for (u32 i = 0; i < burst_threads; ++i) {
            std::thread([this, i, sets] {
                Common::SetCurrentThreadName(("bb:PipelineBurst" + std::to_string(i)).c_str());
                if (!sets.empty() && !BbCpu::SteerCurrentThread(sets) && i == 0) {
                    std::printf("Pipelines: burst threads could not be kept on the efficiency cores\n");
                }
                burst_handles[i] = BbThreads::SelfHandle();
                for (;;) {
                    // At normal priority while at the queue (its lock), idle for the compile.
                    auto job = queue.PopBurst(i);
                    BbThreads::MakeBackground();
                    if (const auto to = queue.RaisedTo(i)) {
                        BbThreads::SetPriority(burst_handles[i], *to); // raised before the lowering
                    }
                    const u64 start = NowNs();
                    job();
                    const u64 ns = NowNs() - start;
                    BbThreads::MakeNormal();
                    Finish(static_cast<int>(i), ns);
                }
            }).detach();
        }
    }

    /// A burst's line: from detailed logs on, and at the standard level for bursts of 8 or more.
    void Report(const PipelineJobQueue::Burst& b) {
        bursts_total.fetch_add(1, std::memory_order_relaxed);
        burst_wall_ns_total.fetch_add(b.wall_ns, std::memory_order_relaxed);
        const u32 used = threads + b.burst_threads;
        for (u32 most = burst_threads_most.load(); used > most &&
                                                   !burst_threads_most.compare_exchange_weak(most, used);) {
        }
        if (b.jobs < 8 && BbLogLevel() < 1) {
            return;
        }
        std::printf("Pipelines: a backlog of %u compiled in %.2f s (%.0f ms of compiling): burst threads %u "
                    "of %u took %u, waiting draws took %u, burst threads raised for a waiting draw %u times; "
                    "frames over 25 ms during it %u of %u (worst %.1f ms)\n",
                    b.jobs, b.wall_ns / 1e9, b.compile_ns / 1e6, b.burst_threads, burst_threads, b.burst_jobs,
                    b.taken, b.raised, b.frames_over_25, b.frames, b.worst_ms);
        std::fflush(stdout);
    }
};

} // namespace

void SubmitPipelineJob(const void* owner, std::function<void()> job) {
    pipeline_compile_stats.background.fetch_add(1, std::memory_order_relaxed);
    PipelineCompiler::Get().queue.Push(owner, std::move(job));
}

void SubmitTranslationJob(const void* owner, std::function<void()> job) {
    PipelineCompiler::Get().queue.Push(owner, std::move(job));
}

std::function<void()> TakeTranslationJob(const void* owner) {
    auto job = PipelineCompiler::Get().queue.Take(owner);
    if (!job) {
        return {};
    }
    return [job = std::move(job)] {
        const u64 start = PipelineCompiler::NowNs();
        job();
        PipelineCompiler::Get().Finish(-1, PipelineCompiler::NowNs() - start);
    };
}

std::function<void()> TakePipelineJob(const void* owner) {
    // bbport: BB_PIPELINE_TAKE=0: a thread that needs a queued pipeline waits for a compile
    // thread to get to it, as before.
    static const bool enabled = [] {
        const char* env = std::getenv("BB_PIPELINE_TAKE");
        return !(env && env[0] == '0');
    }();
    if (!enabled) {
        return {};
    }
    auto job = PipelineCompiler::Get().queue.Take(owner);
    if (!job) {
        return {};
    }
    return [job = std::move(job)] {
        const u64 start = PipelineCompiler::NowNs();
        job();
        PipelineCompiler::Get().Finish(-1, PipelineCompiler::NowNs() - start);
    };
}

unsigned RaisePipelineBursts() {
    auto& compiler = PipelineCompiler::Get();
    if (compiler.burst_threads == 0) {
        return 0;
    }
    // The waiting thread's priority, at least normal (the render chain runs above normal).
    const int priority = std::max(BbThreads::CurrentPriority(), 0);
    return compiler.queue.RaiseRunning(priority, [&](unsigned i, int to) {
        BbThreads::SetPriority(compiler.burst_handles[i], to);
    });
}

PipelineBurstTotals PipelineBurstTotalsNow(bool reset_most) {
    auto& c = PipelineCompiler::Get();
    return {c.jobs_total.load(), c.compile_ns_total.load(), c.bursts_total.load(),
            c.burst_wall_ns_total.load(),
            reset_most ? c.burst_threads_most.exchange(0) : c.burst_threads_most.load()};
}

u32 PipelineBurstThreads() {
    return PipelineCompiler::Get().burst_threads;
}

u32 PipelineCompileThreads() {
    return PipelineCompiler::Get().threads;
}

// --- 0.4.6 beta 17: a pipeline's two parts, as the graphics pipeline library would make them
// (docs/memory-model.md, "The pipeline measurement") -------------------------------------------------
namespace {

/// The fragment shader's part (its permutation; 0: none), and the stages before rasterization's: by
/// permutation (`pre_today`: their slots after the fragment shader's, as compiled today) and by
/// program and runtime state (`pre_own`: with slots of their own, the rework's; an upper bound, the
/// rasterization state and vertex inputs left out).
struct PartKeys {
    u64 fragment = 0, pre_today = 0, pre_own = 0;
};

/// Every part of the graphics pipelines built so far (the warm-up's and play's).
struct PartRegistry {
    std::mutex mutex;
    /// Program -> its distinct runtime states (`pre_own` names them by index).
    std::unordered_map<u64, std::vector<Shader::RuntimeInfo>> runtimes;
    std::unordered_set<u64> fragments, pre_own;
};

PartRegistry& Parts() {
    static auto* registry = new PartRegistry; // used by compile threads until the process ends
    return *registry;
}

/// Under the registry's lock.
PartKeys ComputePartKeys(PartRegistry& r, const GraphicsPipelineKey& key,
                         std::span<const Shader::Info* const> infos,
                         std::span<const Shader::RuntimeInfo, MaxShaderStages> runtime_infos) {
    PartKeys k{.fragment = key.stage_hashes[u32(Shader::SwStage::Fragment)]};
    for (const auto stage : {Shader::SwStage::TessellationControl, Shader::SwStage::TessellationEval,
                             Shader::SwStage::Vertex, Shader::SwStage::Geometry}) {
        const u32 i = u32(stage);
        if (!key.stage_hashes[i] || !infos[i]) {
            continue;
        }
        k.pre_today = HashCombine(k.pre_today, u64(key.stage_hashes[i]));
        auto& seen = r.runtimes[infos[i]->pgm_hash];
        const auto it = std::ranges::find(seen, runtime_infos[i]);
        const u64 index = u64(it - seen.begin());
        if (it == seen.end()) {
            seen.push_back(runtime_infos[i]);
        }
        k.pre_own = HashCombine(k.pre_own, HashCombine(infos[i]->pgm_hash, index + 1));
    }
    return k;
}

/// Every graphics pipeline made: its parts kept; one new in play (not `preloading`) counted first by
/// the parts already built (the kind it returns; 0xFF for the warm-up's). A pipeline without a
/// fragment shader needs no fragment-shader part of its own.
std::pair<PartKeys, std::uint8_t> NoteParts(const GraphicsPipelineKey& key,
                                             std::span<const Shader::Info* const> infos,
                                             std::span<const Shader::RuntimeInfo, MaxShaderStages> runtime_infos,
                                             bool preloading) {
    auto& r = Parts();
    std::scoped_lock lock{r.mutex};
    const PartKeys k = ComputePartKeys(r, key, infos, runtime_infos);
    std::uint8_t kind = 0xFF;
    if (!preloading) {
        const bool fragment = !k.fragment || r.fragments.contains(k.fragment);
        const bool pre = r.pre_own.contains(k.pre_own);
        kind = std::uint8_t((fragment ? 1 : 0) | (pre ? 2 : 0));
        pipeline_compile_stats.NoteNewParts(kind);
    }
    if (k.fragment) {
        r.fragments.insert(k.fragment);
    }
    r.pre_own.insert(k.pre_own);
    return {k, kind};
}

} // namespace

// --- 0.4.6 beta 17: the pipeline measurement (BB_PIPELINE_BENCH; docs/memory-model.md, "The pipeline
// measurement") --------------------------------------------------------------------------------------
// At the warm-up's end, a sample of the cache's graphics pipelines is built again one at a time, each
// three ways: whole as today, whole without the driver's optimizations, and in the pipeline library's
// four parts with a fast link. The driver's own cache must not answer from an earlier build: every
// stage gets a specialization constant no shader declares, with a new value each time (it changes
// nothing the GPU runs, but the build's identity), and where the driver lets it be asked
// (pipelineCreationCacheControl) whether it has the pipeline already, the answer is counted. Then,
// over the whole cache, how many pipelines share their parts with another one: what a pipeline built
// from parts would find already built.
namespace {

struct BenchEntry {
    std::shared_ptr<PendingPipeline> job;
    u64 fragment = 0, pre_today = 0, pre_own = 0; ///< PartKeys
};

struct Bench {
    std::mutex mutex;
    std::vector<BenchEntry> entries;
    std::atomic<u32> pushed{0}, done{0};
    vk::Device device;
    bool parts = false, cache_control = false;
    u32 salt = 0;
};

Bench& TheBench() {
    static Bench bench;
    return bench;
}

constexpr u32 kSaltConstant = 0x7FFFF00Du; // a specialization constant ID no shader declares

struct Built {
    vk::Pipeline pipeline{};
    double ms = -1.0;    ///< < 0: the driver refused it
    bool cached = false; ///< the driver had it already (the salt did not hide it)
};

/// `info` built and timed. With shaders in it (`probe`), the driver is asked first whether it has it
/// already (a part without shaders, or a link, needs no compile, and the answer would say nothing).
Built Create(Bench& b, vk::GraphicsPipelineCreateInfo info, bool probe) {
    Built built;
    if (b.cache_control && probe) {
        auto probe = info;
        probe.flags |= vk::PipelineCreateFlagBits::eFailOnPipelineCompileRequired;
        auto [result, pipeline] = b.device.createGraphicsPipeline({}, probe);
        if (result == vk::Result::eSuccess) {
            built.cached = true;
            b.device.destroyPipeline(pipeline);
        }
    }
    const auto start = std::chrono::steady_clock::now();
    auto [result, pipeline] = b.device.createGraphicsPipeline({}, info);
    const auto end = std::chrono::steady_clock::now();
    if (result == vk::Result::eSuccess) {
        built.pipeline = pipeline;
        built.ms = std::chrono::duration<double, std::milli>(end - start).count();
    }
    return built;
}

using BenchStages = boost::container::static_vector<vk::PipelineShaderStageCreateInfo, MaxShaderStages>;

/// The job's stages with `salt` as the unused specialization (stages with one of their own: as they are).
BenchStages Salted(const PendingPipeline& j, const vk::SpecializationInfo& salt) {
    BenchStages stages(j.stages.begin(), j.stages.end());
    for (auto& stage : stages) {
        if (!stage.pSpecializationInfo) {
            stage.pSpecializationInfo = &salt;
        }
    }
    return stages;
}

struct WholeTimes {
    double optimized = -1.0, unoptimized = -1.0;
    bool cached = false;
};

WholeTimes BuildWhole(Bench& b, const PendingPipeline& j) {
    WholeTimes t;
    for (const bool optimized : {true, false}) {
        u32 value = ++b.salt;
        const vk::SpecializationMapEntry entry{kSaltConstant, 0, sizeof(u32)};
        const vk::SpecializationInfo salt{1, &entry, sizeof(u32), &value};
        auto stages = Salted(j, salt);
        auto info = j.info;
        info.pStages = stages.data();
        if (!optimized) {
            info.flags |= vk::PipelineCreateFlagBits::eDisableOptimization;
        }
        const Built built = Create(b, info, true);
        if (built.pipeline) {
            b.device.destroyPipeline(built.pipeline);
        }
        (optimized ? t.optimized : t.unoptimized) = built.ms;
        t.cached |= built.cached;
    }
    return t;
}

struct PartTimes {
    double vertex_input = -1.0, pre_raster = -1.0, fragment = -1.0, output = -1.0, link = -1.0;
    bool cached = false, skipped = false;
};

/// The pipeline library's four parts (one layout for all, as today's pipeline has), then the fast
/// link (no link-time optimization).
PartTimes BuildParts(Bench& b, const PendingPipeline& j) {
    PartTimes t;
    u32 value = ++b.salt;
    const vk::SpecializationMapEntry entry{kSaltConstant, 0, sizeof(u32)};
    const vk::SpecializationInfo salt{1, &entry, sizeof(u32), &value};
    const auto stages = Salted(j, salt);
    BenchStages pre, fragment;
    for (const auto& stage : stages) {
        (stage.stage == vk::ShaderStageFlagBits::eFragment ? fragment : pre).push_back(stage);
    }
    if (fragment.empty() || pre.empty()) { // depth only: no fragment shader to make a part of
        t.skipped = true;
        return t;
    }
    vk::PipelineRenderingCreateInfo rendering = j.rendering; // its chain: the job's own
    std::array<Built, 4> parts{};
    const auto part = [&](size_t index, vk::GraphicsPipelineLibraryFlagsEXT subset, vk::GraphicsPipelineCreateInfo info) {
        vk::GraphicsPipelineLibraryCreateInfoEXT library{.pNext = &rendering, .flags = subset};
        info.pNext = &library;
        info.flags = j.info.flags | vk::PipelineCreateFlagBits::eLibraryKHR;
        info.pDynamicState = j.info.pDynamicState;
        parts[index] = Create(b, info, info.stageCount != 0);
        t.cached |= parts[index].cached;
        return parts[index].ms;
    };
    using Lib = vk::GraphicsPipelineLibraryFlagBitsEXT;
    t.vertex_input = part(0, Lib::eVertexInputInterface,
                          {.pVertexInputState = j.info.pVertexInputState,
                           .pInputAssemblyState = j.info.pInputAssemblyState});
    t.pre_raster = part(1, Lib::ePreRasterizationShaders,
                        {.stageCount = u32(pre.size()),
                         .pStages = pre.data(),
                         .pTessellationState = j.info.pTessellationState,
                         .pViewportState = j.info.pViewportState,
                         .pRasterizationState = j.info.pRasterizationState,
                         .layout = j.info.layout});
    t.fragment = part(2, Lib::eFragmentShader,
                      {.stageCount = u32(fragment.size()),
                       .pStages = fragment.data(),
                       .pMultisampleState = j.info.pMultisampleState,
                       .pDepthStencilState = j.info.pDepthStencilState,
                       .layout = j.info.layout});
    t.output = part(3, Lib::eFragmentOutputInterface,
                    {.pMultisampleState = j.info.pMultisampleState, .pColorBlendState = j.info.pColorBlendState});
    if (std::ranges::all_of(parts, [](const Built& p) { return bool(p.pipeline); })) {
        std::array<vk::Pipeline, 4> handles{};
        for (size_t i = 0; i < parts.size(); ++i) {
            handles[i] = parts[i].pipeline;
        }
        const vk::PipelineLibraryCreateInfoKHR libraries{.libraryCount = u32(handles.size()),
                                                         .pLibraries = handles.data()};
        const Built linked =
            Create(b, {.pNext = &libraries, .flags = j.info.flags, .layout = j.info.layout}, false);
        t.link = linked.ms;
        t.cached |= linked.cached;
        if (linked.pipeline) {
            b.device.destroyPipeline(linked.pipeline);
        }
    }
    for (const auto& p : parts) {
        if (p.pipeline) {
            b.device.destroyPipeline(p.pipeline);
        }
    }
    return t;
}

/// "median 14.2 ms, 90% under 31.0, longest 88.0; 4.6 s in all" of the times measured (< 0: left out).
std::string Spread(std::vector<double> v) {
    std::erase_if(v, [](double x) { return x < 0.0; });
    if (v.empty()) {
        return "none measured";
    }
    std::sort(v.begin(), v.end());
    double total = 0.0;
    for (const double x : v) {
        total += x;
    }
    return fmt::format("median {:.1f} ms, 90% under {:.1f}, longest {:.1f}; {:.1f} s in all ({})",
                       v[v.size() / 2], v[std::min(v.size() - 1, v.size() * 9 / 10)], v.back(),
                       total / 1000.0, v.size());
}

double Median(std::vector<double> v) {
    std::erase_if(v, [](double x) { return x < 0.0; });
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

void RunBench() {
    auto& b = TheBench();
    std::scoped_lock lock{b.mutex};
    const size_t total = b.entries.size();
    const size_t n = std::min<size_t>(PipelineBenchCount(), total);
    if (!n) {
        return;
    }
    b.salt = u32(std::chrono::steady_clock::now().time_since_epoch().count());
    BbStartup::SetStep(BbStartup::Step::Shaders);
    const auto start = std::chrono::steady_clock::now();
    std::vector<double> optimized, unoptimized, vertex_input, pre_raster, fragment, output, link, parts_sum;
    u32 cached = 0, refused = 0, part_refused = 0, skipped = 0;
    for (size_t k = 0; k < n; ++k) {
        const BenchEntry& e = b.entries[k * total / n];
        const WholeTimes whole = BuildWhole(b, *e.job);
        optimized.push_back(whole.optimized);
        unoptimized.push_back(whole.unoptimized);
        refused += whole.optimized < 0.0 || whole.unoptimized < 0.0;
        bool was_cached = whole.cached;
        if (b.parts) {
            const PartTimes p = BuildParts(b, *e.job);
            was_cached |= p.cached;
            if (p.skipped) {
                ++skipped;
            } else {
                vertex_input.push_back(p.vertex_input);
                pre_raster.push_back(p.pre_raster);
                fragment.push_back(p.fragment);
                output.push_back(p.output);
                link.push_back(p.link);
                const bool all = p.vertex_input >= 0 && p.pre_raster >= 0 && p.fragment >= 0 && p.output >= 0 &&
                                 p.link >= 0;
                part_refused += !all;
                parts_sum.push_back(all ? p.vertex_input + p.pre_raster + p.fragment + p.output + p.link : -1.0);
            }
        }
        cached += was_cached;
        BbStartup::SetProgress(k + 1, n);
    }
    // Parts shared across the whole cache: a pipeline whose part another pipeline has too.
    std::unordered_map<u64, u32> fragment_uses, today_uses, own_uses;
    for (const auto& e : b.entries) {
        ++fragment_uses[e.fragment];
        ++today_uses[e.pre_today];
        ++own_uses[e.pre_own];
    }
    u32 with_fragment = 0, fragment_shared = 0, today_shared = 0, own_shared = 0, both_today = 0, both_own = 0;
    for (const auto& e : b.entries) {
        const bool frag = e.fragment && fragment_uses[e.fragment] > 1;
        const bool today = today_uses[e.pre_today] > 1, own = own_uses[e.pre_own] > 1;
        with_fragment += e.fragment != 0;
        fragment_shared += frag;
        today_shared += today;
        own_shared += own;
        both_today += frag && today;
        both_own += frag && own;
    }
    const auto pct = [&](u32 part, size_t of) { return of ? 100.0 * part / double(of) : 0.0; };
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("Pipeline measurement (BB_PIPELINE_BENCH): %zu of the cache's %zu graphics pipelines built "
                "again one at a time in %.1f s, the driver's own cache kept out (%s: %u answered from it); "
                "the warm-up's time above includes it\n",
                n, total, seconds,
                b.cache_control ? "asked first" : "it could not be asked", cached);
    std::printf("  whole, as today: %s\n", Spread(optimized).c_str());
    std::printf("  whole, without the driver's optimizations: %s; the median %.0f%% of the optimized one\n",
                Spread(unoptimized).c_str(),
                Median(optimized) > 0.0 ? 100.0 * Median(unoptimized) / Median(optimized) : 0.0);
    if (b.parts) {
        std::printf("  in parts (the pipeline library, one layout): vertex input %.1f ms, before "
                    "rasterization %.1f ms, fragment shader %.1f ms, fragment output %.1f ms (medians); "
                    "%u without a fragment shader not made in parts\n",
                    Median(vertex_input), Median(pre_raster), Median(fragment), Median(output), skipped);
        std::printf("  the fast link of built parts: %s\n", Spread(link).c_str());
        std::printf("  parts and link together: %s\n", Spread(parts_sum).c_str());
    } else {
        std::printf("  in parts: not measured (the driver has no graphics pipeline library)\n");
    }
    std::printf("  refused by the driver: %u whole, %u in parts\n", refused, part_refused);
    std::printf("  parts shared with another of the cache's %zu pipelines: the fragment shader's %.0f%% (of "
                "%u with one); the stages before rasterization %.0f%% as compiled today (slots after the "
                "fragment shader's), at most %.0f%% with slots of their own; both %.0f%% today, at most "
                "%.0f%%\n",
                total, pct(fragment_shared, with_fragment), with_fragment, pct(today_shared, total),
                pct(own_shared, total), pct(both_today, total), pct(both_own, total));
    std::fflush(stdout);
}

/// Warm-up, while the cache is read (one thread): `job` kept for the measurement.
void BenchRegister(const Instance& instance, const std::shared_ptr<PendingPipeline>& job, const PartKeys& parts) {
    auto& b = TheBench();
    std::scoped_lock lock{b.mutex};
    b.device = instance.GetDevice();
    b.parts = instance.IsGraphicsPipelineLibraryEnabled();
    b.cache_control = instance.IsPipelineCreationCacheControlEnabled();
    b.entries.push_back({.job = job, .fragment = parts.fragment, .pre_today = parts.pre_today, .pre_own = parts.pre_own});
    b.pushed.fetch_add(1, std::memory_order_relaxed);
}

/// Warm-up job ended: after the last one (the cache read whole first, then built), the measurement.
void BenchJobDone() {
    auto& b = TheBench();
    if (b.done.fetch_add(1, std::memory_order_acq_rel) + 1 == b.pushed.load(std::memory_order_acquire)) {
        RunBench();
    }
}

} // namespace

static constexpr std::array LogicalStageToStageBit = {
    vk::ShaderStageFlagBits::eFragment,
    vk::ShaderStageFlagBits::eTessellationControl,
    vk::ShaderStageFlagBits::eTessellationEvaluation,
    vk::ShaderStageFlagBits::eVertex,
    vk::ShaderStageFlagBits::eGeometry,
    vk::ShaderStageFlagBits::eCompute,
};

GraphicsPipeline::GraphicsPipeline(
    const Instance& instance, Scheduler& scheduler, DescriptorHeap& desc_heap,
    const Shader::Profile& profile, const GraphicsPipelineKey& key_,
    vk::PipelineCache pipeline_cache, std::span<const Shader::Info*, MaxShaderStages> infos,
    std::span<const Shader::RuntimeInfo, MaxShaderStages> runtime_infos,
    std::optional<const Shader::Gcn::FetchShaderData> fetch_shader_,
    std::span<const vk::ShaderModule> modules, SerializationSupport& sdata, bool preloading)
    : Pipeline{instance, scheduler, desc_heap, profile, pipeline_cache}, key{key_},
      fetch_shader{std::move(fetch_shader_)} {
    const vk::Device device = instance.GetDevice();
    std::ranges::copy(infos, stages.begin());
    BuildDescSetLayout(preloading);
    const auto debug_str = GetDebugString();
    // 0.4.6 beta 17: its parts kept, and one new in play counted by those already built.
    const auto [part_keys, part_kind] = NoteParts(key, stages, runtime_infos, preloading);
    part_reuse = part_kind;

    // bbport: a composite with the tone curve extension reads its parameters after the push
    // data (native/tone_curve.h).
    uses_tone_curve = std::ranges::any_of(stages, [](const Shader::Info* info) {
        return info && info->uses_tone_curve;
    });
    const vk::PushConstantRange push_constants = {
        .stageFlags = AllGraphicsStageBits,
        .offset = 0,
        .size = u32(uses_tone_curve ? Shader::Native::ToneCurvePushOffset +
                                          sizeof(Shader::Native::ToneCurvePush)
                                    : sizeof(Shader::PushData)),
    };

    std::array<vk::DescriptorSetLayout, 2> set_layouts{*desc_layout, {}};
    if (key.motion_vectors) {
        motion_address_layout = ObjectMotion::CreateAddressLayout(device);
        set_layouts[Shader::MotionVectors::DescriptorSet] = *motion_address_layout;
    }
    const vk::PipelineLayoutCreateInfo layout_info = {
        .setLayoutCount = key.motion_vectors ? 2U : 1U,
        .pSetLayouts = set_layouts.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constants,
    };
    auto [layout_result, layout] = instance.GetDevice().createPipelineLayoutUnique(layout_info);
    ASSERT_MSG(layout_result == vk::Result::eSuccess,
               "Failed to create graphics pipeline layout: {}", vk::to_string(layout_result));
    pipeline_layout = std::move(layout);
    SetObjectName(device, *pipeline_layout, "Graphics PipelineLayout {}", debug_str);

    if (!preloading) {
        VertexInputs<AmdGpu::Buffer> guest_buffers;
        if (!instance.IsVertexInputDynamicState()) {
            const auto& vs_info = runtime_infos[u32(Shader::SwStage::Vertex)].sw.vs;
            GetVertexInputs(sdata.vertex_attributes, sdata.vertex_bindings, sdata.divisors,
                            guest_buffers, vs_info.step_rate_0, vs_info.step_rate_1);
        }
    }

    const vk::PipelineVertexInputDivisorStateCreateInfo divisor_state = {
        .vertexBindingDivisorCount = static_cast<u32>(sdata.divisors.size()),
        .pVertexBindingDivisors = sdata.divisors.data(),
    };

    const vk::PipelineVertexInputStateCreateInfo vertex_input_info = {
        .pNext = sdata.divisors.empty() ? nullptr : &divisor_state,
        .vertexBindingDescriptionCount = static_cast<u32>(sdata.vertex_bindings.size()),
        .pVertexBindingDescriptions = sdata.vertex_bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<u32>(sdata.vertex_attributes.size()),
        .pVertexAttributeDescriptions = sdata.vertex_attributes.data(),
    };

    const auto topology = LiverpoolToVK::PrimitiveType(key.prim_type);
    const vk::PipelineInputAssemblyStateCreateInfo input_assembly = {
        .topology = topology,
    };

    const bool is_rect_list = key.prim_type == AmdGpu::PrimitiveType::RectList;
    const bool is_quad_list = key.prim_type == AmdGpu::PrimitiveType::QuadList;
    const vk::PipelineTessellationStateCreateInfo tessellation_state = {
        .patchControlPoints = is_rect_list ? 3U : (is_quad_list ? 4U : key.patch_control_points),
    };

    vk::StructureChain raster_chain = {
        vk::PipelineRasterizationStateCreateInfo{
            .depthClampEnable = key.depth_clamp_enable &&
                                (!key.depth_clip_enable || instance.IsDepthClipEnableSupported()),
            .rasterizerDiscardEnable = false,
            .polygonMode = LiverpoolToVK::PolygonMode(key.polygon_mode),
            .lineWidth = 1.0f,
        },
        vk::PipelineRasterizationProvokingVertexStateCreateInfoEXT{
            .provokingVertexMode = key.provoking_vtx_last == AmdGpu::ProvokingVtxLast::First
                                       ? vk::ProvokingVertexModeEXT::eFirstVertex
                                       : vk::ProvokingVertexModeEXT::eLastVertex,
        },
        vk::PipelineRasterizationDepthClipStateCreateInfoEXT{
            .depthClipEnable = key.depth_clip_enable,
        },
    };

    if (!instance.IsProvokingVertexSupported()) {
        raster_chain.unlink<vk::PipelineRasterizationProvokingVertexStateCreateInfoEXT>();
    }
    if (!instance.IsDepthClipEnableSupported()) {
        raster_chain.unlink<vk::PipelineRasterizationDepthClipStateCreateInfoEXT>();
    }

    if (!preloading) {
        const auto& fs_info = runtime_infos[u32(Shader::SwStage::Fragment)].hw.fs;
        sdata.multisampling = {
            .rasterizationSamples = LiverpoolToVK::NumSamples(
                key.num_samples, instance.GetColorSampleCounts() & instance.GetDepthSampleCounts()),
            .sampleShadingEnable =
                fs_info.addr_flags.persp_sample_ena || fs_info.addr_flags.linear_sample_ena,
        };
    }

    const vk::PipelineViewportDepthClipControlCreateInfoEXT clip_control = {
        .negativeOneToOne = key.clip_space == AmdGpu::ClipSpace::MinusWToW,
    };

    const vk::PipelineViewportStateCreateInfo viewport_info = {
        .pNext = instance.IsDepthClipControlSupported() ? &clip_control : nullptr,
    };

    boost::container::static_vector<vk::DynamicState, 32> dynamic_states = {
        vk::DynamicState::eViewportWithCount,  vk::DynamicState::eScissorWithCount,
        vk::DynamicState::eBlendConstants,     vk::DynamicState::eDepthTestEnable,
        vk::DynamicState::eDepthWriteEnable,   vk::DynamicState::eDepthCompareOp,
        vk::DynamicState::eDepthBiasEnable,    vk::DynamicState::eDepthBias,
        vk::DynamicState::eStencilTestEnable,  vk::DynamicState::eStencilReference,
        vk::DynamicState::eStencilCompareMask, vk::DynamicState::eStencilWriteMask,
        vk::DynamicState::eStencilOp,          vk::DynamicState::eCullMode,
        vk::DynamicState::eFrontFace,          vk::DynamicState::eRasterizerDiscardEnable,
        vk::DynamicState::eLineWidth,          vk::DynamicState::ePrimitiveRestartEnable,
    };

    if (instance.IsDepthBoundsSupported()) {
        dynamic_states.push_back(vk::DynamicState::eDepthBoundsTestEnable);
        dynamic_states.push_back(vk::DynamicState::eDepthBounds);
    }
    if (instance.IsDynamicColorWriteMaskSupported()) {
        dynamic_states.push_back(vk::DynamicState::eColorWriteMaskEXT);
    }
    if (instance.IsVertexInputDynamicState()) {
        dynamic_states.push_back(vk::DynamicState::eVertexInputEXT);
    } else if (!sdata.vertex_bindings.empty()) {
        dynamic_states.push_back(vk::DynamicState::eVertexInputBindingStride);
    }

    const vk::PipelineDynamicStateCreateInfo dynamic_info = {
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    boost::container::static_vector<vk::PipelineShaderStageCreateInfo, MaxShaderStages>
        shader_stages;
    auto stage = u32(Shader::SwStage::Vertex);
    if (infos[stage]) {
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = modules[stage],
            .pName = "main",
        });
    }
    stage = u32(Shader::SwStage::Geometry);
    if (infos[stage]) {
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eGeometry,
            .module = modules[stage],
            .pName = "main",
        });
    }
    stage = u32(Shader::SwStage::TessellationControl);
    if (infos[stage]) {
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eTessellationControl,
            .module = modules[stage],
            .pName = "main",
        });
    } else if (is_rect_list || is_quad_list) {
        const auto type = is_quad_list ? AuxShaderType::QuadListTCS : AuxShaderType::RectListTCS;
        if (!preloading) {
            const auto& fs_info = runtime_infos[u32(Shader::SwStage::Fragment)].hw.fs;
            sdata.tcs = Shader::Backend::SPIRV::EmitAuxilaryTessShader(type, fs_info);
        }
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eTessellationControl,
            .module = CompileSPV(sdata.tcs, instance.GetDevice()),
            .pName = "main",
        });
    }
    stage = u32(Shader::SwStage::TessellationEval);
    if (infos[stage]) {
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eTessellationEvaluation,
            .module = modules[stage],
            .pName = "main",
        });
    } else if (is_rect_list || is_quad_list) {
        if (!preloading) {
            const auto& fs_info = runtime_infos[u32(Shader::SwStage::Fragment)].hw.fs;
            sdata.tes = Shader::Backend::SPIRV::EmitAuxilaryTessShader(
                AuxShaderType::PassthroughTES, fs_info);
        }
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eTessellationEvaluation,
            .module = CompileSPV(sdata.tes, instance.GetDevice()),
            .pName = "main",
        });
    }
    stage = u32(Shader::SwStage::Fragment);
    if (infos[stage]) {
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = modules[stage],
            .pName = "main",
        });
    } else if (runtime_infos[u32(Shader::SwStage::Fragment)].hw.fs.clip_distance_emulation ||
               !sdata.fragment.empty()) {
        if (!preloading) {
            const auto& vs = runtime_infos[static_cast<u32>(Shader::SwStage::Vertex)].hw.vs;

            sdata.fragment = Shader::Backend::SPIRV::EmitDiscardFragmentShader(vs.outputs);
        }
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = CompileSPV(sdata.fragment, instance.GetDevice()),
            .pName = "main",
        });
    }

    const vk::PipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup_size_ci = {
        .requiredSubgroupSize = 64,
    };
    for (auto& stage : shader_stages) {
        stage.pNext = instance.IsSubgroupSize64Supported() ? &subgroup_size_ci : nullptr;
    }

    const auto depth_format =
        instance.GetSupportedFormat(LiverpoolToVK::DepthFormat(key.z_format, key.stencil_format),
                                    vk::FormatFeatureFlagBits2::eDepthStencilAttachment);
    std::array<vk::Format, Shader::IR::NumRenderTargets> color_formats;
    for (s32 i = 0; i < key.num_color_attachments; ++i) {
        const auto& col_buf = key.color_buffers[i];
        const auto format = LiverpoolToVK::SurfaceFormat(col_buf.data_format, col_buf.num_format);
        const auto color_format =
            instance.GetSupportedFormat(format, vk::FormatFeatureFlagBits2::eColorAttachment);
        if (!instance.IsFormatSupported(color_format,
                                        vk::FormatFeatureFlagBits2::eColorAttachment)) {
            LOG_WARNING(Render_Vulkan,
                        "color buffer format {} does not support COLOR_ATTACHMENT_BIT",
                        vk::to_string(color_format));
        }
        color_formats[i] = color_format;
    }

    std::array<vk::SampleCountFlagBits, AmdGpu::NUM_COLOR_BUFFERS> color_samples;
    std::ranges::transform(key.color_samples, color_samples.begin(), [&instance](u8 num_samples) {
        return num_samples ? LiverpoolToVK::NumSamples(num_samples, instance.GetColorSampleCounts())
                           : vk::SampleCountFlagBits::e1;
    });
    const vk::AttachmentSampleCountInfoAMD mixed_samples = {
        .colorAttachmentCount = key.num_color_attachments,
        .pColorAttachmentSamples = color_samples.data(),
        .depthStencilAttachmentSamples =
            LiverpoolToVK::NumSamples(key.depth_samples, instance.GetDepthSampleCounts()),
    };

    const vk::PipelineRenderingCreateInfo pipeline_rendering_ci = {
        .pNext = instance.IsMixedDepthSamplesSupported() ? &mixed_samples : nullptr,
        .colorAttachmentCount = key.num_color_attachments,
        .pColorAttachmentFormats = color_formats.data(),
        .depthAttachmentFormat = key.z_format != AmdGpu::DepthBuffer::ZFormat::Invalid
                                     ? depth_format
                                     : vk::Format::eUndefined,
        .stencilAttachmentFormat = key.stencil_format != AmdGpu::DepthBuffer::StencilFormat::Invalid
                                       ? depth_format
                                       : vk::Format::eUndefined,
    };

    std::array<vk::PipelineColorBlendAttachmentState, AmdGpu::NUM_COLOR_BUFFERS> attachments;
    for (u32 i = 0; i < key.num_color_attachments; i++) {
        const auto& control = key.blend_controls[i];

        const auto src_color = LiverpoolToVK::BlendFactor(control.color_src_factor);
        const auto dst_color = LiverpoolToVK::BlendFactor(control.color_dst_factor);
        const auto color_blend = LiverpoolToVK::BlendOp(control.color_func);

        const auto src_alpha = control.separate_alpha_blend
                                   ? LiverpoolToVK::BlendFactor(control.alpha_src_factor)
                                   : src_color;
        const auto dst_alpha = control.separate_alpha_blend
                                   ? LiverpoolToVK::BlendFactor(control.alpha_dst_factor)
                                   : dst_color;
        const auto alpha_blend =
            control.separate_alpha_blend ? LiverpoolToVK::BlendOp(control.alpha_func) : color_blend;

        // Vulkan ignores blend factors for min/max, but a factor that zeroes one operand
        // makes the operation collapse to a plain selection: min(s, 0) is 0 and max(s, 0)
        // is s for normalized alpha. Rewrite those to the equivalent add so the result is
        // exact instead of leaving the other operand to survive.
        auto eff_src_alpha = src_alpha;
        auto eff_dst_alpha = dst_alpha;
        auto eff_alpha_blend = alpha_blend;
        if (alpha_blend == vk::BlendOp::eMin || alpha_blend == vk::BlendOp::eMax) {
            const bool takes_max = alpha_blend == vk::BlendOp::eMax;
            if (src_alpha == vk::BlendFactor::eOne && dst_alpha == vk::BlendFactor::eZero) {
                eff_alpha_blend = vk::BlendOp::eAdd;
                eff_src_alpha = takes_max ? vk::BlendFactor::eOne : vk::BlendFactor::eZero;
                eff_dst_alpha = vk::BlendFactor::eZero;
            } else if (src_alpha == vk::BlendFactor::eZero && dst_alpha == vk::BlendFactor::eOne) {
                eff_alpha_blend = vk::BlendOp::eAdd;
                eff_src_alpha = vk::BlendFactor::eZero;
                eff_dst_alpha = takes_max ? vk::BlendFactor::eOne : vk::BlendFactor::eZero;
            }
        }

        const auto color_scaled_min_max =
            (color_blend == vk::BlendOp::eMin || color_blend == vk::BlendOp::eMax) &&
            (src_color != vk::BlendFactor::eOne || dst_color != vk::BlendFactor::eOne) &&
            !key.color_buffers[i].blend_self_scale;
        const auto alpha_scaled_min_max =
            (eff_alpha_blend == vk::BlendOp::eMin || eff_alpha_blend == vk::BlendOp::eMax) &&
            (eff_src_alpha != vk::BlendFactor::eOne || eff_dst_alpha != vk::BlendFactor::eOne);
        if (color_scaled_min_max || alpha_scaled_min_max) {
            LOG_WARNING(
                Render_Vulkan,
                "Unimplemented use of min/max blend op with blend factor not equal to one.");
        }

        attachments[i] = vk::PipelineColorBlendAttachmentState{
            .blendEnable = control.enable,
            .srcColorBlendFactor = src_color,
            .dstColorBlendFactor = dst_color,
            .colorBlendOp = color_blend,
            .srcAlphaBlendFactor = eff_src_alpha,
            .dstAlphaBlendFactor = eff_dst_alpha,
            .alphaBlendOp = eff_alpha_blend,
            .colorWriteMask =
                instance.IsDynamicColorWriteMaskSupported()
                    ? vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
                    : key.write_masks[i],
        };

        // The shader squares its color output for this attachment (see PsColorBuffer), so the
        // factors must not scale the operands again.
        if (key.color_buffers[i].blend_self_scale) {
            LOG_WARNING(
                Render_Vulkan,
                "Emulating scaled min/max blend with squared shader output on attachment {}", i);
            attachments[i].srcColorBlendFactor = vk::BlendFactor::eOne;
            attachments[i].dstColorBlendFactor = vk::BlendFactor::eOne;
        }

        // On GCN GPU there is an additional mask which allows to control color components exported
        // from a pixel shader. A situation possible, when the game may mask out the alpha channel,
        // while it is still need to be used in blending ops. For such cases, HW will default alpha
        // to 1 and perform the blending, while shader normally outputs 0 in the last component.
        // Unfortunatelly, Vulkan doesn't provide any control on blend inputs, so below we detecting
        // such cases and override alpha value in order to emulate HW behaviour.
        const auto has_alpha_masked_out =
            (key.cb_shader_mask.GetMask(i) & AmdGpu::ColorBufferMask::ComponentA) == 0;
        const auto has_src_alpha_in_src_blend = src_color == vk::BlendFactor::eSrcAlpha ||
                                                src_color == vk::BlendFactor::eOneMinusSrcAlpha;
        const auto has_src_alpha_in_dst_blend = dst_color == vk::BlendFactor::eSrcAlpha ||
                                                dst_color == vk::BlendFactor::eOneMinusSrcAlpha;
        if (has_alpha_masked_out && has_src_alpha_in_src_blend) {
            attachments[i].srcColorBlendFactor = src_color == vk::BlendFactor::eSrcAlpha
                                                     ? vk::BlendFactor::eOne
                                                     : vk::BlendFactor::eZero; // 1-A
        }
        if (has_alpha_masked_out && has_src_alpha_in_dst_blend) {
            attachments[i].dstColorBlendFactor = dst_color == vk::BlendFactor::eSrcAlpha
                                                     ? vk::BlendFactor::eOne
                                                     : vk::BlendFactor::eZero; // 1-A
        }
    }

    const vk::PipelineColorBlendStateCreateInfo color_blending = {
        .logicOpEnable =
            instance.IsLogicOpSupported() && key.logic_op != AmdGpu::ColorControl::LogicOp::Copy,
        .logicOp = LiverpoolToVK::LogicOp(key.logic_op),
        .attachmentCount = key.num_color_attachments,
        .pAttachments = attachments.data(),
        .blendConstants = std::array{1.0f, 1.0f, 1.0f, 1.0f},
    };

    // Required by spec unless VK_EXT_extended_dynamic_state3 is supported.
    // In practice, we use dynamic state for all of it.
    constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_info = {};

    const vk::GraphicsPipelineCreateInfo pipeline_info = {
        .pNext = &pipeline_rendering_ci,
        .stageCount = static_cast<u32>(shader_stages.size()),
        .pStages = shader_stages.data(),
        .pVertexInputState = !instance.IsVertexInputDynamicState() ? &vertex_input_info : nullptr,
        .pInputAssemblyState = &input_assembly,
        .pTessellationState = &tessellation_state,
        .pViewportState = &viewport_info,
        .pRasterizationState = &raster_chain.get(),
        .pMultisampleState = &sdata.multisampling,
        .pDepthStencilState =
            !instance.IsExtendedDynamicState3Supported() ? &depth_stencil_info : nullptr,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_info,
        .layout = *pipeline_layout,
    };

    // bbport (BB_ASYNC_PIPELINES): a pipeline first seen during play is created by a compile
    // thread; the draw recording thread waits for it only when it records the bind (Handle).
    // Meanwhile the GPU command thread decodes on and starts the next new pipelines, so a burst
    // of compiles (area loads: 50-90 at once, 0.5-1.3 s on one thread) runs in parallel.
    // bbport: the start's cached pipelines are created in parallel (WarmupJobs).
    WarmupJobs* const warmup = preloading ? warmup_jobs : nullptr;
    if ((!preloading && AsyncPipelinesEnabled()) || warmup) {
        auto job = std::make_shared<PendingPipeline>();
        auto& j = *job;
        bool ok = true;
        j.stages.assign(shader_stages.begin(), shader_stages.end());
        j.subgroup_size = subgroup_size_ci;
        ok &= subgroup_size_ci.pNext == nullptr;
        for (auto& s : j.stages) {
            ok &= s.pNext == nullptr || s.pNext == &subgroup_size_ci;
            s.pNext = s.pNext ? &j.subgroup_size : nullptr;
        }
        j.bindings = sdata.vertex_bindings;
        j.attributes = sdata.vertex_attributes;
        j.divisors = sdata.divisors;
        j.divisor_state = divisor_state;
        ok &= divisor_state.pNext == nullptr &&
              divisor_state.pVertexBindingDivisors == sdata.divisors.data();
        j.divisor_state.pVertexBindingDivisors = j.divisors.data();
        j.vertex_input = vertex_input_info;
        ok &= vertex_input_info.pNext == nullptr || vertex_input_info.pNext == &divisor_state;
        j.vertex_input.pNext = vertex_input_info.pNext ? &j.divisor_state : nullptr;
        j.vertex_input.pVertexBindingDescriptions = j.bindings.data();
        j.vertex_input.pVertexAttributeDescriptions = j.attributes.data();
        j.input_assembly = input_assembly;
        j.tessellation = tessellation_state;
        j.clip_control = clip_control;
        j.viewport = viewport_info;
        ok &= viewport_info.pNext == nullptr || viewport_info.pNext == &clip_control;
        j.viewport.pNext = viewport_info.pNext ? &j.clip_control : nullptr;
        // The rasterization chain, linked again in the same order without unlinked parts.
        j.raster = raster_chain.get<vk::PipelineRasterizationStateCreateInfo>();
        j.provoking = raster_chain.get<vk::PipelineRasterizationProvokingVertexStateCreateInfoEXT>();
        j.depth_clip = raster_chain.get<vk::PipelineRasterizationDepthClipStateCreateInfoEXT>();
        {
            const void** next = &j.raster.pNext;
            if (instance.IsProvokingVertexSupported()) {
                *next = &j.provoking;
                next = &j.provoking.pNext;
            }
            if (instance.IsDepthClipEnableSupported()) {
                *next = &j.depth_clip;
                next = &j.depth_clip.pNext;
            }
            *next = nullptr;
        }
        j.multisample = sdata.multisampling;
        ok &= sdata.multisampling.pNext == nullptr && sdata.multisampling.pSampleMask == nullptr;
        j.depth_stencil = depth_stencil_info;
        j.attachments = attachments;
        j.color_blend = color_blending;
        ok &= color_blending.pNext == nullptr;
        j.color_blend.pAttachments = j.attachments.data();
        j.dynamic_states.assign(dynamic_states.begin(), dynamic_states.end());
        j.dynamic = dynamic_info;
        j.dynamic.pDynamicStates = j.dynamic_states.data();
        j.color_formats = color_formats;
        j.color_samples = color_samples;
        j.mixed_samples = mixed_samples;
        j.mixed_samples.pColorAttachmentSamples = j.color_samples.data();
        j.rendering = pipeline_rendering_ci;
        ok &= pipeline_rendering_ci.pNext == nullptr || pipeline_rendering_ci.pNext == &mixed_samples;
        j.rendering.pNext = pipeline_rendering_ci.pNext ? &j.mixed_samples : nullptr;
        j.rendering.pColorAttachmentFormats = j.color_formats.data();
        j.info = pipeline_info;
        ok &= input_assembly.pNext == nullptr && tessellation_state.pNext == nullptr &&
              clip_control.pNext == nullptr && dynamic_info.pNext == nullptr &&
              mixed_samples.pNext == nullptr && pipeline_info.pRasterizationState == &raster_chain.get() &&
              pipeline_info.pMultisampleState == &sdata.multisampling;
        j.info.pNext = &j.rendering;
        j.info.stageCount = static_cast<u32>(j.stages.size());
        j.info.pStages = j.stages.data();
        j.info.pVertexInputState = pipeline_info.pVertexInputState ? &j.vertex_input : nullptr;
        j.info.pInputAssemblyState = &j.input_assembly;
        j.info.pTessellationState = &j.tessellation;
        j.info.pViewportState = &j.viewport;
        j.info.pRasterizationState = &j.raster;
        j.info.pMultisampleState = &j.multisample;
        j.info.pDepthStencilState = pipeline_info.pDepthStencilState ? &j.depth_stencil : nullptr;
        j.info.pColorBlendState = &j.color_blend;
        j.info.pDynamicState = &j.dynamic;
        // Anything this copy does not know how to own (a chain added later): created here.
        if (ok && warmup) {
            ready.store(false, std::memory_order_relaxed);
            const bool bench = PipelineBenchCount() != 0;
            if (bench) {
                BenchRegister(instance, job, part_keys);
            }
            warmup->jobs.push_back([this, job, device, cache = pipeline_cache, name = debug_str, bench] {
                auto [result, created] = device.createGraphicsPipelineUnique(cache, job->info);
                if (result == vk::Result::eSuccess) {
                    pipeline = std::move(created);
                    SetObjectName(device, *pipeline, "Graphics Pipeline {}", name);
                }
                MarkReady();
                if (bench) {
                    BenchJobDone();
                }
                return result == vk::Result::eSuccess;
            });
            return;
        }
        if (ok) {
            ready.store(false, std::memory_order_relaxed);
            SubmitPipelineJob(this, [this, job, device, cache = pipeline_cache, name = debug_str] {
                auto [result, created] = device.createGraphicsPipelineUnique(cache, job->info);
                if (result == vk::Result::eSuccess) {
                    pipeline = std::move(created);
                    SetObjectName(device, *pipeline, "Graphics Pipeline {}", name);
                } else {
                    // bbport: left without a pipeline (IsFailed): its draws are skipped.
                    LOG_ERROR(Render_Vulkan, "Failed to create graphics pipeline {}: {}; its draws are skipped",
                              name, vk::to_string(result));
                }
                MarkReady();
            });
            return;
        }
    }

    auto [pipeline_result, pipe] =
        device.createGraphicsPipelineUnique(pipeline_cache, pipeline_info);
    if (preloading && pipeline_result != vk::Result::eSuccess) {
        // bbport: a pipeline from the cache the driver rejects is a damaged cache entry: the
        // cache is rebuilt (PipelineCache::WarmUp) instead of stopping the game at every start.
        throw Serialization::CorruptData{"cached graphics pipeline rejected by the driver: " +
                                         vk::to_string(pipeline_result)};
    }
    if (pipeline_result != vk::Result::eSuccess) {
        // bbport: a driver that cannot build one pipeline stopped the game here (code 23); the
        // pipeline stays empty instead (IsFailed), its draws are skipped and it is not saved.
        LOG_ERROR(Render_Vulkan, "Failed to create graphics pipeline {}: {}; its draws are skipped",
                  debug_str, vk::to_string(pipeline_result));
        return;
    }
    pipeline = std::move(pipe);
    SetObjectName(device, *pipeline, "Graphics Pipeline {}", debug_str);
}

GraphicsPipeline::~GraphicsPipeline() {
    // bbport: the compile thread may still be creating it (its job points at this object).
    WaitReady();
}

template <typename Attribute, typename Binding>
void GraphicsPipeline::GetVertexInputs(
    VertexInputs<Attribute>& attributes, VertexInputs<Binding>& bindings,
    VertexInputs<vk::VertexInputBindingDivisorDescriptionEXT>& divisors,
    VertexInputs<AmdGpu::Buffer>& guest_buffers, u32 step_rate_0, u32 step_rate_1,
    std::span<const AmdGpu::Buffer> sharps) const {
    using InstanceIdType = Shader::Gcn::VertexAttribute::InstanceIdType;
    if (!fetch_shader || fetch_shader->attributes.empty()) {
        return;
    }
    const auto& vs_info = GetStage(Shader::SwStage::Vertex);
    const bool given = sharps.size() == fetch_shader->attributes.size();
    for (u32 index = 0; const auto& attrib : fetch_shader->attributes) {
        const auto step_rate = attrib.GetStepRate();
        const auto buffer = given ? sharps[index] : attrib.GetSharp(vs_info);
        ++index;
        attributes.push_back(Attribute{
            .location = attrib.semantic,
            .binding = attrib.semantic,
            .format = LiverpoolToVK::SurfaceFormat(buffer.GetDataFmt(), buffer.GetNumberFmt()),
            .offset = 0,
        });
        bindings.push_back(Binding{
            .binding = attrib.semantic,
            .stride = buffer.GetStride(),
            .inputRate = step_rate == InstanceIdType::None ? vk::VertexInputRate::eVertex
                                                           : vk::VertexInputRate::eInstance,
        });
        const u32 divisor = step_rate == InstanceIdType::OverStepRate0
                                ? step_rate_0
                                : (step_rate == InstanceIdType::OverStepRate1 ? step_rate_1 : 1);
        if constexpr (std::is_same_v<Binding, vk::VertexInputBindingDescription2EXT>) {
            bindings.back().divisor = divisor;
        } else if (step_rate != InstanceIdType::None) {
            divisors.push_back(vk::VertexInputBindingDivisorDescriptionEXT{
                .binding = attrib.semantic,
                .divisor = divisor,
            });
        }
        guest_buffers.emplace_back(buffer);
    }
}

// Declare templated GetVertexInputs for necessary types.
template void GraphicsPipeline::GetVertexInputs(
    VertexInputs<vk::VertexInputAttributeDescription>& attributes,
    VertexInputs<vk::VertexInputBindingDescription>& bindings,
    VertexInputs<vk::VertexInputBindingDivisorDescriptionEXT>& divisors,
    VertexInputs<AmdGpu::Buffer>& guest_buffers, u32 step_rate_0, u32 step_rate_1,
    std::span<const AmdGpu::Buffer> sharps) const;
template void GraphicsPipeline::GetVertexInputs(
    VertexInputs<vk::VertexInputAttributeDescription2EXT>& attributes,
    VertexInputs<vk::VertexInputBindingDescription2EXT>& bindings,
    VertexInputs<vk::VertexInputBindingDivisorDescriptionEXT>& divisors,
    VertexInputs<AmdGpu::Buffer>& guest_buffers, u32 step_rate_0, u32 step_rate_1,
    std::span<const AmdGpu::Buffer> sharps) const;

void GraphicsPipeline::BuildDescSetLayout(bool preloading) {
    boost::container::small_vector<vk::DescriptorSetLayoutBinding, 32> bindings;
    u32 binding{};

    for (const auto* stage : stages) {
        if (!stage) {
            continue;
        }
        const auto stage_bit = LogicalStageToStageBit[u32(stage->sw_stage)];
        for (const auto& buffer : stage->buffers) {
            const auto sharp =
                preloading ? AmdGpu::Buffer{}
                           : buffer.GetSharp(*stage); // See for the comment in compute PL creation
            bindings.push_back({
                .binding = binding++,
                .descriptorType = buffer.is_uniform ? vk::DescriptorType::eUniformBuffer
                                                    : vk::DescriptorType::eStorageBuffer,
                .descriptorCount = 1,
                .stageFlags = stage_bit,
            });
        }
        for (const auto& image : stage->images) {
            const u32 num_bindings = image.NumBindings(*stage);
            bindings.push_back({
                .binding = binding,
                .descriptorType = image.is_written ? vk::DescriptorType::eStorageImage
                                                   : vk::DescriptorType::eSampledImage,
                .descriptorCount = num_bindings,
                .stageFlags = stage_bit,
            });
            binding += num_bindings;
        }
        for (const auto& sampler : stage->samplers) {
            bindings.push_back({
                .binding = binding++,
                .descriptorType = vk::DescriptorType::eSampler,
                .descriptorCount = 1,
                .stageFlags = stage_bit,
            });
        }
    }
    // 0.4.4 beta 20: up to and including the limit, as Vulkan allows (it said below it: a
    // pipeline of exactly the limit fell back to descriptor sets; the engine map's shader-9).
    uses_push_descriptors = binding <= instance.MaxPushDescriptors();
    const auto flags = uses_push_descriptors
                           ? vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR
                           : vk::DescriptorSetLayoutCreateFlagBits{};
    const vk::DescriptorSetLayoutCreateInfo desc_layout_ci = {
        .flags = flags,
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    };
    auto [layout_result, layout] =
        instance.GetDevice().createDescriptorSetLayoutUnique(desc_layout_ci);
    ASSERT_MSG(layout_result == vk::Result::eSuccess,
               "Failed to create graphics descriptor set layout: {}", vk::to_string(layout_result));
    desc_layout = std::move(layout);
}

} // namespace Vulkan

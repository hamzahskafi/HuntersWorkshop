// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <shared_mutex>
#include <variant>
#include <boost/container/static_vector.hpp>
#include <tsl/robin_map.h>
#include "shader_recompiler/profile.h"
#include "shader_recompiler/recompiler.h"
#include "shader_recompiler/specialization.h"
#include "video_core/renderer_vulkan/vk_compute_pipeline.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"
#include "video_core/renderer_vulkan/translate_ahead.h"

template <>
struct std::hash<vk::ShaderModule> {
    std::size_t operator()(const vk::ShaderModule& module) const noexcept {
        return std::hash<size_t>{}(reinterpret_cast<size_t>((VkShaderModule)module));
    }
};

namespace AmdGpu {
class Liverpool;
}

namespace Serialization {
struct Archive;
}

namespace Shader {
struct Info;
}

namespace Vulkan {

class Instance;
class Scheduler;
class ShaderCache;

struct Program {
    struct Module {
        vk::ShaderModule module;
        Shader::StageSpecialization spec;
        /// bbport: its shader state is in the cache folder (Shader::Native::StoreState), so draws
        /// skip the store's lookup. Set through std::atomic_ref (draw-preparation workers).
        mutable bool state_stored = false;
    };
    static constexpr size_t MaxPermutations = 8;
    using ModuleList = boost::container::small_vector<Module, MaxPermutations>;

    Shader::Info info;
    ModuleList modules{};
    size_t last_used = 0; ///< bbport: permutation of the previous lookup, compared first
    /// bbport: `info` as translated, for draw-preparation workers (they must not read `info`,
    /// whose user data the GPU thread rewrites every draw). Guarded by programs_mutex.
    std::unique_ptr<Shader::Info> info_template;
    /// 0.4.5 beta 17: a vertex program with an object-motion permutation (set when one is added;
    /// read through std::atomic_ref): its draws ask for that permutation first
    /// (PipelineCache::RefreshGraphicsKey), not for the ordinary one only to look at its buffers.
    bool has_motion = false;

    Program() = default;
    Program(Shader::HwStage stage, Shader::SwStage l_stage, Shader::ShaderParams params)
        : info{stage, l_stage, params} {}

    void AddPermut(vk::ShaderModule module, Shader::StageSpecialization&& spec) {
        NoteMotion(spec);
        modules.emplace_back(module, std::move(spec));
    }

    void InsertPermut(vk::ShaderModule module, Shader::StageSpecialization&& spec,
                      size_t perm_idx) {
        NoteMotion(spec);
        modules.resize(std::max(modules.size(), perm_idx + 1)); // <-- beware of realloc
        modules[perm_idx] = {module, std::move(spec)};
    }

    void NoteMotion(const Shader::StageSpecialization& spec) {
        if (spec.runtime_info.hw_stage == Shader::HwStage::Vertex && spec.runtime_info.hw.vs.motion_vectors) {
            std::atomic_ref<bool>(has_motion).store(true, std::memory_order_relaxed);
        }
    }
};

struct DrawIndirectParams {
    u16 vertex_sgpr_offset;
    u32 instance_sgpr_offset;
};

} // namespace Vulkan

namespace AmdGpu {
union Regs;
}

namespace Vulkan {

/// bbport: state of one graphics/compute pipeline selection. The GPU thread owns one
/// (PipelineCache::sel); draw-preparation workers use their own with their register copies.
struct PipelineSelection {
    const AmdGpu::Regs* regs{};
    std::array<Shader::RuntimeInfo, MaxShaderStages> runtime_infos{};
    std::array<const Shader::Info*, MaxShaderStages> infos{};
    std::array<vk::ShaderModule, MaxShaderStages> modules{};
    std::optional<Shader::Gcn::FetchShaderData> fetch_shader{};
    GraphicsPipelineKey graphics_key{};
    bool motion = false;
    DrawIndirectParams draw_indirect_params{};
    struct PrepWorker* worker{}; ///< set: read-only selection for a draw-preparation worker
};

/// 0.4.6 beta 17 part 2 (translate_ahead.h): a stage a draw-preparation worker found not translated,
/// queued ahead of the GPU command thread: a program not translated yet, or a variant of a known one
/// (`variant_spec`, its specialization as the worker computed it). Its inputs are the worker's,
/// copied (the user data live in the worker's register copy); the job fills in the rest.
struct AheadStage {
    u64 hash{};
    Shader::HwStage hw_stage{};
    Shader::SwStage sw_stage{};
    std::array<u32, Shader::ShaderParams::NumShaderUserData> user_data{};
    std::span<const u32> code;
    /// The code's words as the worker read them: the job translates `code` in place (its address is
    /// the program's base, which inline constant buffers are found by) and fails if the memory differs
    /// from this before or after; the GPU thread uses the result only for code equal to it. A shader's
    /// memory the game rewrote meanwhile is never published under the hash taken before.
    std::vector<u32> code_copy;
    Shader::RuntimeInfo runtime{};
    Shader::Backend::Bindings start{};
    bool start_known = false; ///< false: where the job's previous stage ends (a chain of programs)
    std::optional<Shader::StageSpecialization> variant_spec;
    // The job's results.
    std::unique_ptr<Shader::Info> info;
    Shader::RuntimeInfo runtime_after{};  ///< as the translation left it (CompileModule's)
    Shader::Backend::Bindings actual_start{}, end{};
    std::vector<u32> spv;
    std::optional<Shader::StageSpecialization> spec; ///< a program's, from its translated Info
    u64 ns = 0;
    bool ok = false;

    /// Whether `words` are the code this stage was queued with.
    bool SameCode(std::span<const u32> words) const {
        return words.size() == code_copy.size() &&
               std::equal(words.begin(), words.end(), code_copy.begin());
    }

    /// The same request: one job serves both.
    bool SameInputs(const AheadStage& other) const {
        return hash == other.hash && hw_stage == other.hw_stage && runtime == other.runtime &&
               start_known == other.start_known && (!start_known || start == other.start) &&
               variant_spec.has_value() == other.variant_spec.has_value() &&
               (!variant_spec || *variant_spec == *other.variant_spec);
    }
};

/// A job: one draw's stages not translated yet, in the order the draw binds them. Self-contained (its
/// own profile copy): a compile thread runs it without the pipeline cache.
struct TranslationJob {
    std::vector<AheadStage> stages;
    Shader::Profile profile{};
    Ahead::Progress progress;
};

/// bbport: a draw-preparation worker's own program state (see vk_draw_prep.h).
struct PrepWorker {
    struct Stage {
        const Program* program;
        u64 hash;
        Shader::HwStage hw_stage;
        VAddr pgm_base;
        const std::vector<u32>* flat;
    };
    // Node-based: stages keep pointers to these Infos while later stages are inserted.
    std::unordered_map<const Program*, Shader::Info> infos;
    boost::container::static_vector<Stage, MaxShaderStages> stages;
    bool failed = false;
    /// 0.4.6 beta 17 part 2: this draw's stages not translated yet, to queue (cleared per draw);
    /// `ahead_closed`: a later stage cannot join (its bindings would start where an untranslated
    /// stage ends, or it is a variant after one).
    std::vector<AheadStage> ahead;
    bool ahead_closed = false;
};

struct PreparedDraw;

struct PrecacheProgress {
    u32 total{};
    u32 loaded{};
    u32 rejected{};
    bool complete{};

    float Fraction() const {
        return total ? float(loaded + rejected) / total : (complete ? 1.0f : 0.0f);
    }
};

class PipelineCache {
public:
    explicit PipelineCache(const Instance& instance, Scheduler& scheduler,
                           AmdGpu::Liverpool* liverpool, u32 sparse_page_shift);
    ~PipelineCache();

    void WarmUp();
    void Sync();

    // Available even while the renderer constructor is running WarmUp().
    static PrecacheProgress GetPrecacheProgress();
    // The port's window-close path uses _Exit, so it cannot rely on destructors.
    static void SaveAllForShutdown();
    // Set once before Liverpool can submit draws or preparation work.
    void SetObjectMotionEnabled(bool enabled) { object_motion_enabled = enabled; }

    bool LoadComputePipeline(Serialization::Archive& ar);
    bool LoadGraphicsPipeline(Serialization::Archive& ar, u32 version);
    bool LoadPipelineStage(Serialization::Archive& ar, size_t stage, u64 expected_hash);

    const GraphicsPipeline* GetGraphicsPipeline(const DrawIndirectParams params = {},
                                                const PreparedDraw* prepared = nullptr);

    /// bbport: worker side of draw preparation: selects the pipeline key for `sel.regs` without
    /// creating anything. False when a program or permutation does not exist yet.
    bool PrepareGraphicsPipeline(PipelineSelection& sel);

    /// bbport: GPU-thread side: the pipeline for a prepared draw after checking that registers
    /// and flattened user data match; null to take the regular path.
    const GraphicsPipeline* TryPreparedPipeline(const PreparedDraw& prepared);

    /// bbport: the prepared draw the last GetGraphicsPipeline used, or null (regular path).
    [[nodiscard]] const PreparedDraw* UsedPrepared() const noexcept {
        return used_prepared;
    }

    /// 0.4.6 beta 17 part 2 (BB_TRANSLATE_AHEAD=0: off): a draw-preparation worker's stages not
    /// translated yet (PrepWorker::ahead), queued on the compile threads unless an equal job is.
    void QueueAhead(std::vector<AheadStage>&& stages);
    static bool TranslateAheadEnabled();

    const ComputePipeline* GetComputePipeline();

    using Result = std::tuple<const Shader::Info*, vk::ShaderModule,
                              std::optional<Shader::Gcn::FetchShaderData>, u64>;
    Result GetProgram(PipelineSelection& sel, Shader::HwStage stage, Shader::SwStage l_stage,
                      const Shader::ShaderParams& params, Shader::Backend::Bindings& binding);

    std::optional<vk::ShaderModule> ReplaceShader(vk::ShaderModule module,
                                                  std::span<const u32> spv_code);

    static std::string GetShaderName(Shader::HwStage stage, u64 hash,
                                     std::optional<size_t> perm = {});

    auto& GetProfile() const {
        return profile;
    }

private:
    void CreateNativeCache();
    void SaveNativeCache();
    // Only pipeline creation and shutdown snapshots take this lock, never cache hits.
    std::mutex native_cache_mutex;
    static std::mutex progress_mutex;
    static PrecacheProgress precache_progress;

    bool RefreshGraphicsKey(PipelineSelection& sel);
    bool RefreshGraphicsStages(PipelineSelection& sel);
    bool RefreshComputeKey();

    void DumpShader(std::span<const u32> code, u64 hash, Shader::HwStage stage, size_t perm_idx,
                    std::string_view ext);
    std::optional<std::vector<u32>> GetShaderPatch(u64 hash, Shader::HwStage stage, size_t perm_idx,
                                                   std::string_view ext);
    vk::ShaderModule CompileModule(Shader::Info& info, Shader::RuntimeInfo& runtime_info,
                                   const std::span<const u32>& code, size_t perm_idx,
                                   Shader::Backend::Bindings& binding);
    /// The part of CompileModule after the translation (it needs the variant's index): the module
    /// (or the shader patch's), the stored binary and state, the dumps and names.
    vk::ShaderModule FinishModule(const Shader::Info& info, const Shader::RuntimeInfo& runtime_before,
                                  const std::span<const u32>& code, size_t perm_idx, std::vector<u32>&& spv,
                                  bool ahead);
    /// 0.4.6 beta 17 part 2: where the GPU command thread would translate, the queued job's stage
    /// for it (found by `pred`), run here if no compile thread has started it, waited for if one has;
    /// none when there is no such job or it failed.
    template <typename Pred>
    std::optional<std::pair<std::shared_ptr<TranslationJob>, u32>> AcquireAhead(u64 hash, Shader::HwStage stage,
                                                                                std::span<const u32> code,
                                                                                Pred&& pred);
    static void RunAhead(TranslationJob& job);
    static void TranslateAheadStages(TranslationJob& job, Shader::Pools& pools);
    std::optional<Result> PublishAheadProgram(Shader::HwStage hw_stage, Shader::SwStage sw_stage,
                                              const Shader::ShaderParams& params,
                                              const Shader::RuntimeInfo& runtime_info,
                                              Shader::Backend::Bindings& binding);
    const Shader::RuntimeInfo& BuildRuntimeInfo(PipelineSelection& sel, Shader::HwStage stage,
                                                Shader::SwStage l_stage);

    [[nodiscard]] bool IsPipelineCacheDirty() const {
        return num_new_pipelines > 0;
    }

private:
    const Instance& instance;
    Scheduler& scheduler;
    AmdGpu::Liverpool* liverpool;
    DescriptorHeap desc_heap;
    vk::UniquePipelineCache pipeline_cache;
    vk::UniquePipelineLayout pipeline_layout;
    Shader::Profile profile{};
    Shader::Pools pools;
    tsl::robin_map<size_t, std::unique_ptr<Program>> program_cache;
    /// bbport: exclusive for program/permutation insertions, shared for worker lookups.
    std::shared_mutex programs_mutex;
    /// 0.4.6 beta 17 part 2: the translations queued ahead (workers add, the GPU thread takes; an
    /// entry no draw took goes after 10 s).
    Ahead::Registry<TranslationJob> ahead_registry{10'000'000'000ull, 4096};
    /// Requests the GPU thread never took (a job done and unused 2 s after it was queued): a worker's
    /// view of that draw differs from the GPU thread's, so the request is not queued again (the
    /// engine id 115 run: 33 such stages asked for 13,748 times a minute). By program, stage and kind.
    std::mutex ahead_phantom_mutex;
    std::unordered_set<u64> ahead_phantoms;
    u64 prepared_hits = 0, prepared_misses = 0;
    const PreparedDraw* used_prepared = nullptr;
    tsl::robin_map<ComputePipelineKey, std::unique_ptr<ComputePipeline>> compute_pipelines;
    tsl::robin_map<GraphicsPipelineKey, std::unique_ptr<GraphicsPipeline>> graphics_pipelines;
    PipelineSelection sel{}; ///< GPU thread selection state
    /// bbport: the warm-up's loaded stages: where each one's bindings start (its specialization)
    /// and how many it has, and whether it has none at all (StageSpecialization::HasNoBindings: its
    /// start does not matter); `warmup_mip_bindings`: one has an image bound per mip level, whose
    /// count needs the draw's descriptor.
    std::array<Shader::Backend::Bindings, MaxShaderStages> warmup_start{}, warmup_count{};
    std::array<bool, MaxShaderStages> warmup_no_bindings{};
    bool warmup_mip_bindings = false;
    ComputePipelineKey compute_key{};
    u32 num_new_pipelines{}; // new pipelines added to the cache since the game start
    bool object_motion_enabled{};

    // Only if Config::collectShadersForDebug()
    tsl::robin_map<vk::ShaderModule,
                   std::vector<std::variant<GraphicsPipelineKey, ComputePipelineKey>>>
        module_related_pipelines;
};

} // namespace Vulkan

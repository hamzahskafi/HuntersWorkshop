// SPDX-License-Identifier: GPL-2.0-or-later
// bbport 0.4.4 beta 27: AMD FSR through AMD's FSR SDK 2.4 (FidelityFX-SDK v2.4.0): AMD's signed
// amd_fidelityfx_loader_vk.dll and amd_fidelityfx_upscaler_vk.dll, loaded at run time, driven through
// the AMD FSR API (third_party/amd-fsr-sdk, MIT headers). AMD's SDK picks the FSR version this GPU
// runs: FSR 4.1.2 on RX 9000 (FP8), RX 7000, RX 6000 and Ryzen AI Max (INT8), Ryzen AI 300/400 and
// Z2 (lightweight INT8); FSR 3.1.6 elsewhere (NVIDIA, Intel, older Radeon). fsr_version=3.1 asks for
// FSR 3.1 instead. AMD's DLLs are used as AMD ships them: not reverse engineered, decompiled or
// disassembled (AMD's licence, third_party/amd-fsr-sdk/LICENSE.md). Windows only: AMD's SDK has no
// Linux build.
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "video_core/renderer_vulkan/vk_frame_upscaler.h"

namespace Vulkan {

class Instance;
class Scheduler;

class AmdFsr {
public:
    static constexpr const char* LoaderName = "amd_fidelityfx_loader_vk.dll";
    static constexpr const char* UpscalerName = "amd_fidelityfx_upscaler_vk.dll";

    /// AMD's DLLs' folder: BB_AMD_FSR_DIR, else the package's amd-fsr folder next to the engine.
    static std::filesystem::path Directory();
    /// AMD's loader and upscaler DLLs are there (Windows only; BB_AMD_FSR=0 hides them, for tests).
    static bool FilesPresent();

    AmdFsr(const Instance& instance, Scheduler& scheduler);
    ~AmdFsr();

    /// Loads AMD's DLLs and asks which FSR versions run on this GPU (logged once). False when AMD FSR
    /// cannot run here; Problem() says why.
    bool Probe();

    /// Starts making the context for an output size and its inputs on a worker thread (making it
    /// builds the upscaler's shaders: seconds the first time); nothing when one is made or being made
    /// for them. `version`: "auto" (the newest AMD's SDK runs here) or a version AMD lists for this GPU
    /// by its name (BbSettings::FsrVersionMatch; 0.4.4 beta 27c, was FSR 3.1 or not).
    void Prepare(u32 output_width, u32 output_height, bool hdr, bool auto_exposure, const std::string& version);
    /// A context for these is made: Record can run.
    [[nodiscard]] bool Ready(u32 output_width, u32 output_height, bool hdr, bool auto_exposure,
                             const std::string& version) const;
    /// A context is being made on the worker thread.
    [[nodiscard]] bool Preparing() const noexcept;

    /// Records AMD FSR into `frame.cmdbuf`; the output stays in General. False when it cannot run
    /// (Problem() says why).
    bool Record(const UpscaleInput::Frame& frame);

    /// FSR 3.1's tuning constants (fsr3_* in config.ini): AMD's FSR 3.1 takes them through its
    /// key-value configuration (FFX_API_CONFIGURE_UPSCALE_KEY_*); FSR 4 has none.
    void Tune(float velocity_factor, float reactiveness_scale, float shading_change_scale,
              float accumulation, float min_disocclusion);

    /// Why the last call failed, or null.
    [[nodiscard]] const char* Problem() const noexcept;
    /// AMD FSR will not run in this session (its DLLs, this GPU, or a failed dispatch).
    [[nodiscard]] bool Fatal() const noexcept;
    /// AMD's name of the version that runs ("FSR 4.1.2", "FSR 3.1.6"), or "FSR" before one runs.
    [[nodiscard]] std::string VersionName() const;
    /// The version that runs is FSR 3.1 (it takes the reactive mask and the tuning constants).
    [[nodiscard]] bool RunsFsr31() const noexcept;
    /// The version that runs is one AMD's driver provides (" *" in AMD's list).
    [[nodiscard]] bool RunsDriverVersion() const noexcept;
    /// A user retry (a menu change): failures forgotten, the context made again when next needed.
    void ClearFailure() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Vulkan

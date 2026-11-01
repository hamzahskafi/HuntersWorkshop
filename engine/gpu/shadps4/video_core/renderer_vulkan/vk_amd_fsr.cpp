// SPDX-License-Identifier: GPL-2.0-or-later
// bbport 0.4.4 beta 27: AMD FSR through AMD's FSR SDK 2.4 (vk_amd_fsr.h). Written for HW from AMD's
// documentation (Kits/FidelityFX/docs: getting-started/ffx-api.md, techniques/super-resolution-ml.md)
// and its MIT headers; the structure (the context made on a worker thread, the frames in flight
// bounded, the inputs' layouts) follows the fsr4vk integration this replaces (0xCydral's
// bloodborne_pc, GPL-2.0-or-later).

#include "video_core/renderer_vulkan/vk_amd_fsr.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "api/include/ffx_api.h"
#include "api/include/ffx_api_types.h"
#include "api/include/vk/ffx_api_vk.h"
#include "upscalers/include/ffx_upscale.h"

#include "bbport_toggles.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#endif

namespace Vulkan {

namespace {

std::filesystem::path ExecutableDirectory() {
#ifdef _WIN32
    std::wstring path(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    path.resize(length);
    return std::filesystem::path{path}.parent_path();
#else
    return {};
#endif
}

std::string Utf8(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    return {reinterpret_cast<const char*>(u8.data()), u8.size()};
}

} // namespace

std::filesystem::path AmdFsr::Directory() {
#ifdef _WIN32
    // The wide environment: a folder with a non-ASCII name reads the same whatever the code page.
    if (const wchar_t* dir = _wgetenv(L"BB_AMD_FSR_DIR"); dir && dir[0]) {
        return dir;
    }
#else
    if (const char* dir = std::getenv("BB_AMD_FSR_DIR"); dir && dir[0]) {
        return dir;
    }
#endif
    return ExecutableDirectory() / "amd-fsr";
}

bool AmdFsr::FilesPresent() {
#ifdef _WIN32
    if (const char* setting = std::getenv("BB_AMD_FSR"); setting && setting[0] == '0') {
        return false;
    }
    std::error_code error;
    return std::filesystem::is_regular_file(Directory() / LoaderName, error) &&
           std::filesystem::is_regular_file(Directory() / UpscalerName, error);
#else
    return false;
#endif
}

#ifdef _WIN32

namespace {

void Message(uint32_t type, const wchar_t* text) {
    std::printf("Upscaler: AMD FSR %s: %ls\n", type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning",
                text ? text : L"");
}

const char* ReturnName(ffxReturnCode_t code) {
    switch (code) {
    case FFX_API_RETURN_OK: return "ok";
    case FFX_API_RETURN_ERROR: return "error";
    case FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE: return "unknown descriptor type";
    case FFX_API_RETURN_ERROR_RUNTIME_ERROR: return "runtime error";
    case FFX_API_RETURN_NO_PROVIDER: return "no provider";
    case FFX_API_RETURN_ERROR_MEMORY: return "out of memory";
    case FFX_API_RETURN_ERROR_PARAMETER: return "invalid parameter";
    case FFX_API_RETURN_PROVIDER_NO_SUPPORT_NEW_DESCTYPE: return "descriptor type too new for the provider";
    default: return "unknown";
    }
}

/// AMD's names may carry a trailing " *" for a version the driver provides (ffx-api.md, "Driver-provided
/// versions").
bool DriverProvided(const std::string& name) {
    return name.size() >= 2 && name.compare(name.size() - 2, 2, " *") == 0;
}

/// An FSR 3.1 version by AMD's name ("3.1.6", "FSR 3.1.6", ...): AMD asks for names over ids, whose
/// encoding may change (ffx-api.md).
bool Fsr31Name(const std::string& name) {
    for (size_t at = name.find("3.1"); at != std::string::npos; at = name.find("3.1", at + 1)) {
        const bool start = at == 0 || !std::isdigit(static_cast<unsigned char>(name[at - 1]));
        const size_t end = at + 3;
        const bool finish = end == name.size() || !std::isdigit(static_cast<unsigned char>(name[end]));
        if (start && finish) {
            return true;
        }
    }
    return false;
}

/// AMD's backend keeps a few frames' descriptors and constants in rings (FFX_MAX_QUEUED_FRAMES, 4, in
/// its internal types): a dispatch never gets more than three frames ahead of the GPU.
constexpr size_t FramesInFlight = 3;

} // namespace

struct AmdFsr::Impl {
    const Instance& instance;
    Scheduler& scheduler;
    HMODULE module{};
    PfnFfxCreateContext create{};
    PfnFfxDestroyContext destroy{};
    PfnFfxConfigure configure{};
    PfnFfxQuery query{};
    PfnFfxDispatch dispatch{};
    VkDeviceContext device{};
    bool probed{}, probe_ok{};
    std::vector<uint64_t> version_ids;
    std::vector<std::string> version_names;

    ffxContext context{};
    struct Key {
        u32 width{}, height{};
        bool hdr{}, auto_exposure{}, fsr31{};
        bool operator==(const Key&) const = default;
    } made{}, wanted{};
    std::string provider_name;
    bool runs_fsr31{};
    std::array<float, 5> tuning{};
    bool tuned{};
    std::deque<u64> ticks; ///< scheduler ticks of the frames in flight
    std::string problem;
    bool fatal{};

    // The context is made on a worker thread: making it builds the upscaler's shaders. While
    // `preparing`, only the worker touches the fields above (bar `problem` read by the menu, which
    // copies it); the render thread reads them again after joining it.
    std::thread prep;
    std::atomic<bool> preparing{false};
    bool created_ok{};

    Impl(const Instance& instance_, Scheduler& scheduler_) : instance{instance_}, scheduler{scheduler_} {
        device.vkDevice = VkDevice(instance.GetDevice());
        device.vkPhysicalDevice = VkPhysicalDevice(instance.GetPhysicalDevice());
        device.vkDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;
    }

    ~Impl() {
        if (prep.joinable()) {
            prep.join();
        }
        DestroyContextNow(false);
        // The DLLs stay loaded until the process ends: AMD's loader may keep threads or state.
    }

    void Fail(std::string reason, bool permanent) {
        problem = std::move(reason);
        fatal |= permanent;
    }

    /// `recording`: from Record, on the command buffer being recorded: only earlier submissions wait.
    void DestroyContextNow(bool recording) {
        if (!context) {
            return;
        }
        if (recording) {
            scheduler.WaitSubmittedWork();
        } else {
            scheduler.Finish();
        }
        destroy(&context, nullptr);
        context = nullptr;
        made = {};
        ticks.clear();
        tuned = false;
        provider_name.clear();
        runs_fsr31 = false;
    }

    bool Load() {
        if (module) {
            return true;
        }
        if (!FilesPresent()) {
            Fail("AMD's FSR files are missing (" + Utf8(Directory()) + ": " + LoaderName + ", " +
                     UpscalerName + ")",
                 true);
            return false;
        }
        const auto path = Directory() / LoaderName;
        // AMD's loader finds the upscaler DLL next to itself (SDK 2.4) or next to the executable.
        module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) {
            Fail("cannot load " + Utf8(path) + " (error " + std::to_string(GetLastError()) + ")", true);
            return false;
        }
        bool complete = true;
        const auto load = [&](auto& function, const char* name) {
            function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
                reinterpret_cast<void*>(GetProcAddress(module, name)));
            complete = complete && function;
        };
        // AMD's ffxLoadFunctions (ffx_api_loader.h), written out: it is defined for MSVC builds only.
        load(create, "ffxCreateContext");
        load(destroy, "ffxDestroyContext");
        load(configure, "ffxConfigure");
        load(query, "ffxQuery");
        load(dispatch, "ffxDispatch");
        if (!complete) {
            Fail(Utf8(path) + " is not AMD's FSR loader (an entry point is missing)", true);
            return false;
        }
        return true;
    }

    bool Probe() {
        if (probed) {
            return probe_ok;
        }
        probed = true;
        if (!Load()) {
            return false;
        }
        ffxQueryDescGetVersions versions{};
        versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        versions.device = &device;
        uint64_t count = 0;
        versions.outputCount = &count;
        if (const auto result = query(nullptr, &versions.header); result != FFX_API_RETURN_OK || count == 0) {
            Fail(std::string{"AMD's FSR SDK offers no upscaler for this GPU ("} + ReturnName(result) + ")",
                 true);
            return false;
        }
        // Ids and names in one query (AMD: their order may differ between calls), names copied at once
        // (AMD: some are kept in global memory a later query may overwrite).
        std::vector<uint64_t> ids(count);
        std::vector<const char*> names(count);
        versions.versionIds = ids.data();
        versions.versionNames = names.data();
        if (const auto result = query(nullptr, &versions.header); result != FFX_API_RETURN_OK) {
            Fail(std::string{"AMD's FSR version list failed ("} + ReturnName(result) + ")", true);
            return false;
        }
        ids.resize(count);
        version_ids = ids;
        version_names.clear();
        std::string listed;
        for (size_t i = 0; i < count; ++i) {
            version_names.emplace_back(names[i] ? names[i] : "?");
            listed += (i ? ", " : "") + version_names.back();
        }
        std::printf("Upscaler: AMD FSR SDK loaded (%s); FSR versions for this GPU: %s (Automatic: AMD's "
                    "choice, the newest; * = provided by the driver)\n",
                    Utf8(Directory()).c_str(), listed.c_str());
        probe_ok = true;
        return true;
    }

    /// The FSR 3.1 version to ask for: one shipped with the game first (AMD lists the driver's too).
    std::optional<uint64_t> Fsr31Id() const {
        std::optional<uint64_t> driver;
        for (size_t i = 0; i < version_ids.size(); ++i) {
            if (!Fsr31Name(version_names[i])) {
                continue;
            }
            if (!DriverProvided(version_names[i])) {
                return version_ids[i];
            }
            driver = driver ? driver : std::optional<uint64_t>{version_ids[i]};
        }
        return driver;
    }

    bool CreateContextNow(Key key) {
        if (key.width < 64 || key.height < 64 || key.width > 7680 || key.height > 4320) {
            Fail("output size " + std::to_string(key.width) + "x" + std::to_string(key.height) +
                     " is outside 64x64 to 7680x4320",
                 true);
            return false;
        }
        ffxCreateContextDescUpscale desc{};
        desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        // The scene's depth is standard, 0 at the near plane (CameraMotion::Near), as DLSS is told;
        // AMD supports it, though it recommends inverted depth. The motion vectors are at the render
        // size, without jitter. The render size moves within the output with the preset (live):
        // dynamic resolution. HDR: the scene colour at a 1920x1080 output; elsewhere the game's
        // tonemapped frame, decoded to linear light first (TemporalUpscaler::RunScaled).
        desc.flags = FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION |
                     (key.hdr ? FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE : 0u) |
                     (key.auto_exposure ? FFX_UPSCALE_ENABLE_AUTO_EXPOSURE : 0u);
        static const bool debug = [] {
            const char* env = std::getenv("BB_AMD_FSR_DEBUG");
            return env && env[0] == '1';
        }();
        if (debug) {
            desc.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
        }
        desc.maxRenderSize = {key.width, key.height};
        desc.maxUpscaleSize = {key.width, key.height};
        desc.fpMessage = Message;
        ffxCreateContextDescUpscaleVersion api{};
        api.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
        api.version = FFX_UPSCALER_VERSION; // AMD: required from SDK 2.1
        ffxCreateBackendVKDesc backend{};
        backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        backend.deviceContext = device;
        ffxOverrideVersion override_version{};
        override_version.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
        desc.header.pNext = &api.header;
        api.header.pNext = &backend.header;
        if (key.fsr31) {
            if (const auto id = Fsr31Id()) {
                override_version.versionId = *id;
                backend.header.pNext = &override_version.header;
            } else {
                std::printf("Upscaler: AMD FSR: no FSR 3.1 in AMD's list for this GPU; AMD's choice instead\n");
            }
        }
        const auto result = create(&context, &desc.header, nullptr);
        if (result != FFX_API_RETURN_OK) {
            context = nullptr;
            Fail(std::string{"AMD FSR could not start: "} + ReturnName(result) + " (" +
                     std::to_string(result) + ")",
                 true);
            return false;
        }
        made = key;
        ffxQueryGetProviderVersion version{};
        version.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        std::string name = query(&context, &version.header) == FFX_API_RETURN_OK && version.versionName
                               ? version.versionName
                               : "";
        name.erase(0, name.find_first_not_of(' '));
        runs_fsr31 = Fsr31Name(name);
        provider_name = name.empty() ? "FSR" : name.rfind("FSR", 0) == 0 ? name : "FSR " + name;
        return true;
    }

    void Prepare(Key key) {
        if (fatal || preparing || (context && made == key)) {
            return;
        }
        if (prep.joinable()) {
            prep.join();
        }
        DestroyContextNow(false);
        wanted = key;
        preparing = true;
        std::printf("Upscaler: AMD FSR building its shaders in the background (output %ux%u, %s%s); "
                    "the scene at full size meanwhile\n",
                    key.width, key.height, key.hdr ? "HDR scene" : "linear frame",
                    key.fsr31 ? ", FSR 3.1 asked for" : "");
        prep = std::thread([this, key] {
            const auto start = std::chrono::steady_clock::now();
            created_ok = CreateContextNow(key);
            if (created_ok) {
                std::printf("Upscaler: AMD FSR ready after %.1f s: %s\n",
                            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
                            provider_name.c_str());
            }
            preparing = false;
        });
    }

    void Settle() {
        if (!preparing && prep.joinable()) {
            prep.join();
        }
    }

    bool Ready(Key key) {
        Settle();
        return !preparing && !fatal && context && made == key;
    }

    void Tune(const std::array<float, 5>& values) {
        Settle();
        if (preparing || !context || !runs_fsr31 || (tuned && values == tuning)) {
            return;
        }
        constexpr std::array<uint64_t, 5> keys{
            FFX_API_CONFIGURE_UPSCALE_KEY_FVELOCITYFACTOR, FFX_API_CONFIGURE_UPSCALE_KEY_FREACTIVENESSSCALE,
            FFX_API_CONFIGURE_UPSCALE_KEY_FSHADINGCHANGESCALE,
            FFX_API_CONFIGURE_UPSCALE_KEY_FACCUMULATIONADDEDPERFRAME,
            FFX_API_CONFIGURE_UPSCALE_KEY_FMINDISOCCLUSIONACCUMULATION};
        for (size_t i = 0; i < keys.size(); ++i) {
            // As AMD's sample sets them (fsrapirendermodule.cpp, SetUpscaleConstantBuffer): a pointer to
            // the float. Applied to the dispatches recorded after it.
            float value = values[i];
            ffxConfigureDescUpscaleKeyValue kv{};
            kv.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
            kv.key = keys[i];
            kv.ptr = &value;
            configure(&context, &kv.header);
        }
        if (tuned) {
            std::printf("Upscaler: FSR 3.1 tuning: velocity %.2f, reactiveness %.2f, shading %.2f, "
                        "accumulation %.3f, disocclusion %.3f\n",
                        values[0], values[1], values[2], values[3], values[4]);
        }
        tuning = values;
        tuned = true;
    }

    /// The inputs in shader-read layout, as AMD's state COMPUTE_READ says (AMD's sample hands them in
    /// that state); the output stays General (state UNORDERED_ACCESS). Back to General after.
    void Transition(vk::CommandBuffer cmdbuf, const UpscaleInput::Frame& f, bool to_read_only) {
        std::array<std::pair<const UpscaleInput::Image*, vk::ImageAspectFlags>, 4> inputs{{
            {&f.color, vk::ImageAspectFlagBits::eColor},
            {&f.depth, vk::ImageAspectFlagBits::eDepth},
            {&f.motion, vk::ImageAspectFlagBits::eColor},
            {&f.reactive, vk::ImageAspectFlagBits::eColor},
        }};
        if (f.depth.format == vk::Format::eD32SfloatS8Uint || f.depth.format == vk::Format::eD24UnormS8Uint ||
            f.depth.format == vk::Format::eD16UnormS8Uint) {
            inputs[1].second |= vk::ImageAspectFlagBits::eStencil;
        }
        std::array<vk::ImageMemoryBarrier2, 4> barriers;
        u32 count = 0;
        for (const auto& [image, aspect] : inputs) {
            if (!image->image) {
                continue;
            }
            barriers[count++] = vk::ImageMemoryBarrier2{
                .srcStageMask = to_read_only ? vk::PipelineStageFlagBits2::eAllCommands
                                             : vk::PipelineStageFlagBits2::eComputeShader,
                .srcAccessMask = to_read_only ? vk::AccessFlagBits2::eMemoryWrite : vk::AccessFlagBits2::eShaderRead,
                .dstStageMask = to_read_only ? vk::PipelineStageFlagBits2::eComputeShader
                                             : vk::PipelineStageFlagBits2::eAllCommands,
                .dstAccessMask = to_read_only ? vk::AccessFlagBits2::eShaderRead
                                              : vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                .oldLayout = to_read_only ? vk::ImageLayout::eGeneral : vk::ImageLayout::eShaderReadOnlyOptimal,
                .newLayout = to_read_only ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eGeneral,
                .image = image->image,
                .subresourceRange = {aspect, 0, 1, 0, 1},
            };
        }
        // The output: earlier users done before AMD FSR writes it, its writes visible to all after.
        const vk::MemoryBarrier2 memory{
            .srcStageMask = to_read_only ? vk::PipelineStageFlagBits2::eAllCommands
                                         : vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = to_read_only ? vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite
                                          : vk::AccessFlagBits2::eShaderStorageWrite,
            .dstStageMask = to_read_only ? vk::PipelineStageFlagBits2::eComputeShader
                                         : vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = to_read_only
                                 ? vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
                                 : vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        };
        cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1,
                                 .pMemoryBarriers = &memory,
                                 .imageMemoryBarrierCount = count,
                                 .pImageMemoryBarriers = barriers.data()});
    }

    static FfxApiResource Resource(const UpscaleInput::Image& image, uint32_t usage, uint32_t state) {
        FfxApiResource r{};
        if (!image.image) {
            r.state = state; // an optional input left out: a null resource
            return r;
        }
        r.resource = reinterpret_cast<void*>(VkImage(image.image));
        r.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
        r.description.format = ffxApiGetSurfaceFormatVK(static_cast<VkFormat>(image.format));
        r.description.width = image.width;
        r.description.height = image.height;
        r.description.depth = 1;
        r.description.mipCount = 1;
        r.description.flags = FFX_API_RESOURCE_FLAGS_NONE;
        r.description.usage = usage;
        r.state = state;
        return r;
    }

    bool Record(const UpscaleInput::Frame& f) {
        Settle();
        if (preparing) {
            return false; // Problem(): still building
        }
        if (fatal) {
            return false;
        }
        if (!context) {
            Fail("no AMD FSR context yet", false);
            return false;
        }
        if (f.output.width != made.width || f.output.height != made.height || f.hdr != made.hdr) {
            Fail("the frame does not match the AMD FSR context (made for another output)", false);
            return false;
        }
        while (ticks.size() >= FramesInFlight) {
            scheduler.Wait(ticks.front());
            ticks.pop_front();
        }
        ffxDispatchDescUpscale d{};
        d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        d.commandList = reinterpret_cast<void*>(VkCommandBuffer(f.cmdbuf));
        const uint32_t read = FFX_API_RESOURCE_STATE_COMPUTE_READ;
        d.color = Resource(f.color, FFX_API_RESOURCE_USAGE_READ_ONLY, read);
        uint32_t depth_usage = FFX_API_RESOURCE_USAGE_DEPTHTARGET;
        if (f.depth.format == vk::Format::eD32SfloatS8Uint || f.depth.format == vk::Format::eD24UnormS8Uint ||
            f.depth.format == vk::Format::eD16UnormS8Uint) {
            depth_usage |= FFX_API_RESOURCE_USAGE_STENCILTARGET;
        }
        d.depth = Resource(f.depth, depth_usage, read);
        d.motionVectors = Resource(f.motion, FFX_API_RESOURCE_USAGE_READ_ONLY, read);
        d.exposure = Resource({}, FFX_API_RESOURCE_USAGE_READ_ONLY, read);
        d.reactive = Resource(f.reactive, FFX_API_RESOURCE_USAGE_READ_ONLY, read);
        d.transparencyAndComposition = Resource({}, FFX_API_RESOURCE_USAGE_READ_ONLY, read);
        d.output = Resource(f.output, FFX_API_RESOURCE_USAGE_UAV, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        d.jitterOffset = {f.jitter[0], f.jitter[1]};
        // Render pixels, previous - current: AMD's convention (super-resolution-ml.md, "Providing
        // motion vectors"). Fsr4MotionYFlip set (bbport_toggles.h): y negated, for tests.
        const bool flip_y = BbToggle::Disabled(BbToggle::Fsr4MotionYFlip);
        d.motionVectorScale = {1.0f, flip_y ? -1.0f : 1.0f};
        d.renderSize = {f.render_width, f.render_height};
        d.upscaleSize = {f.output.width, f.output.height};
        d.enableSharpening = f.sharpen;
        d.sharpness = std::clamp(f.sharpness, 0.0f, 1.0f);
        d.frameTimeDelta = f.frame_ms;
        d.preExposure = 1.0f;
        d.reset = f.reset;
        d.cameraNear = f.near_plane;
        d.cameraFar = f.far_plane;
        d.cameraFovAngleVertical = f.vertical_fov;
        d.viewSpaceToMetersFactor = 1.0f;
        static const bool debug_view = [] {
            const char* env = std::getenv("BB_AMD_FSR_DEBUG_VIEW");
            return env && env[0] == '1';
        }();
        d.flags = debug_view ? FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW : 0u;
        Transition(f.cmdbuf, f, true);
        const auto result = dispatch(&context, &d.header);
        Transition(f.cmdbuf, f, false);
        if (result != FFX_API_RETURN_OK) {
            Fail(std::string{"AMD FSR dispatch failed: "} + ReturnName(result) + " (" + std::to_string(result) + ")",
                 true);
            return false;
        }
        ticks.push_back(scheduler.CurrentTick());
        problem.clear();
        return true;
    }
};

AmdFsr::AmdFsr(const Instance& instance, Scheduler& scheduler)
    : impl{std::make_unique<Impl>(instance, scheduler)} {}

AmdFsr::~AmdFsr() = default;

bool AmdFsr::Probe() {
    return impl->Probe();
}

void AmdFsr::Prepare(u32 ow, u32 oh, bool hdr, bool auto_exposure, bool fsr31) {
    impl->Settle();
    impl->Prepare({ow, oh, hdr, auto_exposure, fsr31});
}

bool AmdFsr::Ready(u32 ow, u32 oh, bool hdr, bool auto_exposure, bool fsr31) const {
    return impl->Ready({ow, oh, hdr, auto_exposure, fsr31});
}

bool AmdFsr::Preparing() const noexcept {
    return impl->preparing;
}

bool AmdFsr::Record(const UpscaleInput::Frame& frame) {
    return impl->Record(frame);
}

void AmdFsr::Tune(float velocity_factor, float reactiveness_scale, float shading_change_scale,
                  float accumulation, float min_disocclusion) {
    impl->Tune({velocity_factor, reactiveness_scale, shading_change_scale, accumulation, min_disocclusion});
}

const char* AmdFsr::Problem() const noexcept {
    if (impl->preparing) {
        return "building its shaders in the background; the scene at full size until they are ready";
    }
    return impl->problem.empty() ? nullptr : impl->problem.c_str();
}

bool AmdFsr::Fatal() const noexcept {
    return !impl->preparing && impl->fatal;
}

std::string AmdFsr::VersionName() const {
    return impl->preparing || impl->provider_name.empty() ? "FSR" : impl->provider_name;
}

bool AmdFsr::RunsFsr31() const noexcept {
    return !impl->preparing && impl->runs_fsr31;
}

void AmdFsr::ClearFailure() noexcept {
    impl->Settle();
    if (impl->preparing) {
        return;
    }
    // A failed probe (missing files, no version for this GPU) stays: nothing a retry would change.
    if (impl->probed && !impl->probe_ok) {
        return;
    }
    impl->fatal = false;
    impl->problem.clear();
}

#else

struct AmdFsr::Impl {};
AmdFsr::AmdFsr(const Instance&, Scheduler&) {}
AmdFsr::~AmdFsr() = default;
bool AmdFsr::Probe() {
    return false;
}
void AmdFsr::Prepare(u32, u32, bool, bool, bool) {}
bool AmdFsr::Ready(u32, u32, bool, bool, bool) const {
    return false;
}
bool AmdFsr::Preparing() const noexcept {
    return false;
}
bool AmdFsr::Record(const UpscaleInput::Frame&) {
    return false;
}
void AmdFsr::Tune(float, float, float, float, float) {}
const char* AmdFsr::Problem() const noexcept {
    return "AMD FSR runs on Windows only (AMD's FSR SDK has no Linux build)";
}
bool AmdFsr::Fatal() const noexcept {
    return true;
}
std::string AmdFsr::VersionName() const {
    return "FSR";
}
bool AmdFsr::RunsFsr31() const noexcept {
    return false;
}
void AmdFsr::ClearFailure() noexcept {}

#endif

} // namespace Vulkan

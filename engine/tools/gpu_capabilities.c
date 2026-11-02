/* Check whether native-size depth/stencil images can be blitted to reduced
 * renderer targets. The renderer needs both directions for live presets.
 * --gamepads: the connected gamepads, "GUID<tab>name" per line (the launcher's controller list,
 * BB_GAMEPAD). --read-input: one key or button for the launcher's controls (below).
 * --fsr-versions: the AMD FSR versions AMD's FSR SDK offers on the game's GPU (below). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#ifdef _WIN32
#include <wchar.h>
#include <windows.h>
#include "api/include/ffx_api.h"
#include "api/include/vk/ffx_api_vk.h"
#include "upscalers/include/ffx_upscale.h"
#endif

static VkDeviceSize largest_local_heap(VkPhysicalDevice device) {
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(device, &memory);
    VkDeviceSize largest = 0;
    for (uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
        if ((memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) &&
            memory.memoryHeaps[i].size > largest) {
            largest = memory.memoryHeaps[i].size;
        }
    }
    return largest;
}

static int better_device(VkPhysicalDevice candidate, VkPhysicalDevice current) {
    VkPhysicalDeviceProperties next, old;
    vkGetPhysicalDeviceProperties(candidate, &next);
    vkGetPhysicalDeviceProperties(current, &old);
    const int next_api = next.apiVersion >= VK_API_VERSION_1_3;
    const int old_api = old.apiVersion >= VK_API_VERSION_1_3;
    if (next_api != old_api) return next_api;
    const int next_discrete = next.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    const int old_discrete = old.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    if (next_discrete != old_discrete) return next_discrete;
    const int next_cpu = next.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
    const int old_cpu = old.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
    if (next_cpu != old_cpu) return !next_cpu;
    return largest_local_heap(candidate) > largest_local_heap(current);
}

static int list_gamepads(void) {
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "gamepads: %s\n", SDL_GetError());
        return 1;
    }
    // Some devices (HIDAPI) show up only after events are pumped.
    for (int i = 0; i < 5; ++i) {
        SDL_PumpEvents();
        SDL_Delay(40);
    }
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; ++i) {
        char guid[33];
        SDL_GUIDToString(SDL_GetGamepadGUIDForID(ids[i]), guid, sizeof guid);
        const char *name = SDL_GetGamepadNameForID(ids[i]);
        printf("%s\t%s\n", guid, name ? name : "?");
    }
    SDL_free(ids);
    SDL_Quit();
    return 0;
}

/* --read-input key|pad: a small window; prints "key <SDL key name>" or "pad <SDL button name>"
 * (lefttrigger/righttrigger for the triggers) for the first key or gamepad button pressed, the
 * names config.ini's key.* and pad.* lines take. Escape, closing it or 15 s: nothing. */
static int read_input(const char *kind) {
    const int want_key = strcmp(kind, "pad") != 0, want_pad = strcmp(kind, "key") != 0;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "read-input: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    const char *prompt = want_key && want_pad ? "Press a key or a gamepad button"
                         : want_key           ? "Press a key"
                                              : "Press a gamepad button";
    if (!SDL_CreateWindowAndRenderer("bbport", 520, 90, 0, &window, &renderer)) {
        fprintf(stderr, "read-input: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; ++i) SDL_OpenGamepad(ids[i]);
    SDL_free(ids);
    const Uint64 end = SDL_GetTicks() + 15000;
    int done = 0;
    while (!done && SDL_GetTicks() < end) {
        SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
        SDL_RenderDebugText(renderer, 16, 30, prompt);
        SDL_RenderDebugText(renderer, 16, 50, "Escape: cancel");
        SDL_RenderPresent(renderer);
        SDL_Event e;
        while (!done && SDL_WaitEventTimeout(&e, 50)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                done = 1;
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                SDL_OpenGamepad(e.gdevice.which);
                break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
                    done = 1;
                } else if (want_key) {
                    printf("key %s\n", SDL_GetScancodeName(e.key.scancode));
                    done = 1;
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (want_pad) {
                    printf("pad %s\n", SDL_GetGamepadStringForButton((SDL_GamepadButton)e.gbutton.button));
                    done = 1;
                }
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (want_pad && e.gaxis.value > 16000 &&
                    (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
                    printf("pad %s\n", e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? "lefttrigger" : "righttrigger");
                    done = 1;
                }
                break;
            default:
                break;
            }
        }
    }
    fflush(stdout);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

/* --fsr-versions (0.4.4 beta 27c, the launcher's Graphics page): the AMD FSR versions AMD's FSR SDK
 * offers on the GPU the game uses, asked as the engine asks (vk_amd_fsr.cpp, a device of that GPU):
 * "gpu<tab>name<tab>driver version" and then "version<tab>name" per version, as AMD names it (" *":
 * provided by the driver). AMD's loader from BB_AMD_FSR_DIR, else the package's engine\amd-fsr (this
 * program is in engine\out). AMD's DLLs are only loaded and asked, as AMD's API documents. Windows only:
 * AMD's SDK has no Linux build. Exit 1 when it cannot tell. */
static int fsr_versions(VkPhysicalDevice gpu) {
#ifdef _WIN32
    wchar_t dir[MAX_PATH] = L"", path[MAX_PATH * 2];
    const wchar_t *env = _wgetenv(L"BB_AMD_FSR_DIR");
    if (env && env[0]) {
        wcsncpy(dir, env, MAX_PATH - 1);
    } else {
        GetModuleFileNameW(NULL, dir, MAX_PATH);
        wchar_t *slash = wcsrchr(dir, L'\\');
        if (slash) *slash = 0;
        swprintf(path, MAX_PATH * 2, L"%ls\\amd-fsr\\amd_fidelityfx_loader_vk.dll", dir);
        wcsncat(dir, GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES ? L"\\amd-fsr" : L"\\..\\amd-fsr",
                MAX_PATH - wcslen(dir) - 1);
    }
    swprintf(path, MAX_PATH * 2, L"%ls\\amd_fidelityfx_loader_vk.dll", dir);
    HMODULE loader = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!loader) {
        fprintf(stderr, "AMD FSR: cannot load AMD's loader (error %lu)\n", GetLastError());
        return 1;
    }
    PfnFfxQuery query = (PfnFfxQuery)(void (*)(void))GetProcAddress(loader, "ffxQuery");
    if (!query) {
        fputs("AMD FSR: not AMD's FSR loader (no ffxQuery)\n", stderr);
        return 1;
    }
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    const VkDeviceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue,
    };
    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(gpu, &info, NULL, &device) != VK_SUCCESS) {
        fputs("AMD FSR: cannot create a Vulkan device\n", stderr);
        return 1;
    }
    VkDeviceContext context = {device, gpu, vkGetDeviceProcAddr};
    struct ffxQueryDescGetVersions versions;
    memset(&versions, 0, sizeof(versions));
    versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    versions.device = &context;
    uint64_t count = 0;
    versions.outputCount = &count;
    int result = 1;
    if (query(NULL, &versions.header) == FFX_API_RETURN_OK && count) {
        uint64_t *ids = calloc((size_t)count, sizeof(*ids));
        const char **names = calloc((size_t)count, sizeof(*names));
        versions.versionIds = ids;
        versions.versionNames = names;
        if (ids && names && query(NULL, &versions.header) == FFX_API_RETURN_OK) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(gpu, &props);
            printf("gpu\t%s\t%u\n", props.deviceName, props.driverVersion);
            for (uint64_t i = 0; i < count; ++i) printf("version\t%s\n", names[i] ? names[i] : "?");
            result = 0;
        }
        free(ids);
        free(names);
    } else {
        fputs("AMD FSR: AMD's FSR SDK offers no upscaler for this GPU\n", stderr);
    }
    vkDestroyDevice(device, NULL);
    return result; /* AMD's loader stays loaded until the process ends, as in the engine */
#else
    (void)gpu;
    fputs("AMD FSR: Windows only (AMD's FSR SDK has no Linux build)\n", stderr);
    return 1;
#endif
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--read-input")) {
        return read_input(argc > 2 ? argv[2] : "any");
    }
    if (argc > 1 && !strcmp(argv[1], "--gamepads")) {
        return list_gamepads();
    }
    const int fsr_mode = argc > 1 && !strcmp(argv[1], "--fsr-versions");
    const VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "bbport scene scaling probe",
        .apiVersion = VK_API_VERSION_1_3,
    };
    const VkInstanceCreateInfo create = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&create, NULL, &instance) != VK_SUCCESS) {
        fputs("GPU scene scaling: cannot create Vulkan instance\n", stderr);
        return 1;
    }
    uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(instance, &count, NULL) != VK_SUCCESS || !count) {
        fputs("GPU scene scaling: no Vulkan device\n", stderr);
        vkDestroyInstance(instance, NULL);
        return 1;
    }
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices || vkEnumeratePhysicalDevices(instance, &count, devices) != VK_SUCCESS) {
        fputs("GPU scene scaling: cannot enumerate Vulkan devices\n", stderr);
        free(devices);
        vkDestroyInstance(instance, NULL);
        return 1;
    }
    /* Match vk_instance.cpp's default ranking or its explicit BB_GPU_ID index. */
    VkPhysicalDevice selected = devices[0];
    const char *gpu_id = getenv("BB_GPU_ID");
    if (gpu_id && atoi(gpu_id) >= 0) {
        const unsigned long index = strtoul(gpu_id, NULL, 10);
        if (index >= count) {
            fputs("GPU scene scaling: BB_GPU_ID is outside the device list\n", stderr);
            free(devices);
            vkDestroyInstance(instance, NULL);
            return 1;
        }
        selected = devices[index];
    } else {
        for (uint32_t i = 1; i < count; ++i)
            if (better_device(devices[i], selected)) selected = devices[i];
    }
    if (fsr_mode) {
        const int result = fsr_versions(selected);
        free(devices);
        vkDestroyInstance(instance, NULL);
        return result;
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(selected, &props);
    const struct { VkFormat format; const char *name; } formats[] = {
        {VK_FORMAT_R8G8B8A8_UNORM, "RGBA8"},
        {VK_FORMAT_R8G8B8A8_SRGB, "RGBA8 sRGB"},
        {VK_FORMAT_B10G11R11_UFLOAT_PACK32, "B10G11R11"},
        {VK_FORMAT_R16G16B16A16_SFLOAT, "RGBA16F"},
        {VK_FORMAT_D32_SFLOAT_S8_UINT, "D32S8"},
    };
    int supported = 1;
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        VkFormatProperties features;
        vkGetPhysicalDeviceFormatProperties(selected, formats[i].format, &features);
        const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                              VK_FORMAT_FEATURE_BLIT_DST_BIT;
        if ((features.optimalTilingFeatures & required) != required) {
            fprintf(stderr, "GPU scene scaling: %s lacks blit support for %s\n",
                    props.deviceName, formats[i].name);
            supported = 0;
        }
    }
    if (supported) fprintf(stderr, "GPU scene scaling: %s supports live presets\n", props.deviceName);
    free(devices);
    vkDestroyInstance(instance, NULL);
    return supported ? 0 : 1;
}

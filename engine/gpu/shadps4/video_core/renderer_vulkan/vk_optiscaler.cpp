// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <cwctype>
#include <optional>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif

#include "video_core/renderer_vulkan/vk_optiscaler.h"

namespace Vulkan::OptiScaler {

namespace {

#ifdef _WIN32
std::string Narrow(const std::wstring& text) {
    std::string out;
    for (const wchar_t c : text) {
        out.push_back(c < 128 ? char(c) : '?');
    }
    return out;
}

/// "x.y.z.w" from the file's version resource (version.dll is loaded here, not linked).
std::string FileVersion(const wchar_t* path) {
    const HMODULE dll = LoadLibraryW(L"version.dll");
    if (!dll) {
        return {};
    }
    using SizeFn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    using InfoFn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    using QueryFn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    const auto size_fn = reinterpret_cast<SizeFn>(GetProcAddress(dll, "GetFileVersionInfoSizeW"));
    const auto info_fn = reinterpret_cast<InfoFn>(GetProcAddress(dll, "GetFileVersionInfoW"));
    const auto query_fn = reinterpret_cast<QueryFn>(GetProcAddress(dll, "VerQueryValueW"));
    std::string version;
    DWORD handle = 0;
    const DWORD size = size_fn && info_fn && query_fn ? size_fn(path, &handle) : 0;
    if (size) {
        std::vector<char> data(size);
        VS_FIXEDFILEINFO* fixed = nullptr;
        UINT length = 0;
        if (info_fn(path, 0, size, data.data()) &&
            query_fn(data.data(), L"\\", reinterpret_cast<LPVOID*>(&fixed), &length) && fixed &&
            fixed->dwFileVersionMS) {
            char text[64];
            std::snprintf(text, sizeof(text), "%u.%u.%u.%u", HIWORD(fixed->dwFileVersionMS),
                          LOWORD(fixed->dwFileVersionMS), HIWORD(fixed->dwFileVersionLS),
                          LOWORD(fixed->dwFileVersionLS));
            version = text;
        }
    }
    FreeLibrary(dll);
    return version;
}

/// OptiScaler exports the NGX entry points under a proxy name (winmm.dll, dxgi.dll, version.dll,
/// ...) and keeps OptiScaler.ini beside itself. The driver's own NGX core (_nvngx.dll / nvngx.dll)
/// exports them too and counts only with that ini.
Info Find() {
    Info info;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        return info;
    }
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Module32FirstW(snapshot, &entry); more && !info.loaded;
         more = Module32NextW(snapshot, &entry)) {
        std::wstring name = entry.szModule;
        for (auto& c : name) {
            c = wchar_t(std::towlower(c));
        }
        if (!GetProcAddress(entry.hModule, "NVSDK_NGX_VULKAN_Init_Ext2")) {
            continue;
        }
        std::error_code ec;
        const auto ini_path = std::filesystem::path(entry.szExePath).parent_path() / L"OptiScaler.ini";
        const bool ini = std::filesystem::exists(ini_path, ec);
        const bool ngx_core = name == L"_nvngx.dll" || name == L"nvngx.dll";
        if (ngx_core && !ini) {
            continue;
        }
        info.loaded = true;
        info.module = Narrow(entry.szModule);
        info.version = FileVersion(entry.szExePath);
        info.handle = entry.hModule;
        if (ini) {
            std::ifstream file(ini_path);
            info.ini = ParseIni(file);
            info.frame_generation = info.ini.frame_generation;
        }
        // d4r (NVIDIA's DLSS on AMD through OptiScaler): its boot DLL, which its dxgi.dll loads first,
        // or its folder beside OptiScaler.
        info.d4r = GetModuleHandleW(L"d4r_boot.dll") != nullptr ||
                   std::filesystem::is_directory(std::filesystem::path(entry.szExePath).parent_path() / L"d4r", ec);
    }
    CloseHandle(snapshot);
    return info;
}
#else
Info Find() {
    return {};
}
#endif

/// A spoofing key's value: "on", "off" or "its default" (auto).
const char* Said(const std::optional<bool>& flag) {
    return !flag ? "its default" : *flag ? "on" : "off";
}

} // namespace

const Info& Detect() {
    static const Info info = [] {
        Info found = Find();
        if (found.loaded) {
            const bool dlss = found.ini.dlss_inputs.value_or(true);
            std::printf("OptiScaler: %s%s loaded as %s; the engine's DLSS goes %s; compatibility mode: on - NGX "
                        "calls and every queue under the queue lock, the Vulkan device made on a thread of its "
                        "own, one submit thread while its frame generation is set\n",
                        found.version.empty() ? "" : "v",
                        found.version.empty() ? "(version unknown)" : found.version.c_str(), found.module.c_str(),
                        dlss ? "through it" : "to NVIDIA's driver (EnableDlssInputs=false in OptiScaler.ini)");
            if (dlss) {
                std::printf("OptiScaler: its upscaler for Vulkan: %s (VulkanUpscaler in OptiScaler.ini, or "
                            "its menu)%s\n",
                            found.ini.vulkan_upscaler.empty() ? "its default" : found.ini.vulkan_upscaler.c_str(),
                            found.d4r ? "; d4r beside it: NVIDIA's DLSS on this GPU through it" : "");
            }
            std::printf("OptiScaler: GPU spoofing: Vulkan %s, Vulkan extensions %s, DXGI %s (OptiScaler.ini "
                        "[Spoofing]); the engine's DLSS needs none (the GPU lines say what the engine sees)\n",
                        Said(found.ini.vulkan_spoofing), Said(found.ini.vulkan_extension_spoofing),
                        Said(found.ini.dxgi_spoofing));
            if (!found.frame_generation.empty()) {
                std::printf("OptiScaler: its frame generation is set (%s in OptiScaler.ini): not supported "
                            "by the engine yet; one submit thread while it is set\n",
                            found.frame_generation.c_str());
            }
        }
        return found;
    }();
    return info;
}

bool DlssThrough() {
    const Info& info = Detect();
    return info.loaded && info.handle && info.ini.dlss_inputs.value_or(true);
}

bool Compat() {
    return Detect().loaded;
}

bool SyncSubmit() {
    return Compat() && !Detect().frame_generation.empty();
}

} // namespace Vulkan::OptiScaler

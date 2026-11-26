// SPDX-License-Identifier: GPL-2.0-or-later
// What the engine reads from OptiScaler.ini (renderer_vulkan/optiscaler_ini.h): the keys its log and
// its DLSS path go by, as OptiScaler's own ini writes them (sections, "auto", comments, blank values),
// both frame-generation keys (v10's FGOutput, 0.9's FGType), and keys with the same names in other
// sections left alone.

#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>

#include "video_core/renderer_vulkan/optiscaler_ini.h"

using namespace Vulkan::OptiScaler;

static IniSettings Parse(const std::string& text) {
    std::istringstream in(text);
    return ParseIni(in);
}

int main() {
    // OptiScaler's shipped ini: every key at "auto", comments above each.
    {
        const auto ini = Parse("[Upscalers]\r\n"
                               "; fsr21, fsr22, ffx, xess, dlss - Default (auto) is fsr21\r\n"
                               "Dx11Upscaler=auto\r\n"
                               "Dx12Upscaler=auto\r\n"
                               "VulkanUpscaler=auto\r\n"
                               "\r\n"
                               "[FrameGen]\r\n"
                               "; nofg, optifg, nukems - Default (auto) is nofg\r\n"
                               "FGType=auto\r\n"
                               "[Inputs]\r\n"
                               "EnableDlssInputs=auto\r\n"
                               "[Spoofing]\r\n"
                               "Dxgi=auto\r\n"
                               "Vulkan=auto\r\n"
                               "VulkanExtensionSpoofing=auto\r\n");
        assert(ini.vulkan_upscaler.empty());
        assert(ini.frame_generation.empty());
        assert(!ini.dlss_inputs && !ini.vulkan_spoofing && !ini.vulkan_extension_spoofing && !ini.dxgi_spoofing);
    }
    // Set by a player: the Vulkan upscaler, spoofing, frame generation; case, spaces and inline
    // comments as people write them.
    {
        const auto ini = Parse("[ upscalers ]\n"
                               "  VulkanUpscaler = XeSS   ; the player's choice\n"
                               "Dx12Upscaler=fsr31\n"
                               "[FrameGen]\n"
                               "FGType=optifg\n"
                               "[INPUTS]\n"
                               "enabledlssinputs=TRUE\n"
                               "[Spoofing]\n"
                               "Vulkan=true\n"
                               "VulkanExtensionSpoofing=1\n"
                               "Dxgi=false\n");
        assert(ini.vulkan_upscaler == "xess");
        assert(ini.frame_generation == "optifg");
        assert(ini.dlss_inputs == true);
        assert(ini.vulkan_spoofing == true && ini.vulkan_extension_spoofing == true && ini.dxgi_spoofing == false);
    }
    // v10's FGOutput wins over 0.9's FGType; "nofg" is off; DLSS inputs turned off.
    {
        auto ini = Parse("[FrameGen]\nFGType=optifg\nFGOutput=xefg\n[Inputs]\nEnableDlssInputs=false\n");
        assert(ini.frame_generation == "xefg");
        assert(ini.dlss_inputs == false);
        ini = Parse("[FrameGen]\nFGOutput=nofg\nFGType=nukems\n");
        assert(ini.frame_generation.empty());
        ini = Parse("[FrameGen]\nFGType=nukems\n");
        assert(ini.frame_generation == "nukems");
    }
    // The same key names elsewhere are not these; blank values, odd flags and lines without "="
    // leave a key to OptiScaler's default.
    {
        const auto ini = Parse("Vulkan=true\n"
                               "[Hotfix]\n"
                               "Vulkan=true\n"
                               "VulkanUpscaler=dlss\n"
                               "[Spoofing]\n"
                               "Vulkan=\n"
                               "Dxgi=yes\n"
                               "VulkanExtensionSpoofing\n"
                               "# a comment\n"
                               "[Inputs]\n"
                               "EnableDlssInputs=0\n");
        assert(ini.vulkan_upscaler.empty());
        assert(!ini.vulkan_spoofing && !ini.dxgi_spoofing && !ini.vulkan_extension_spoofing);
        assert(ini.dlss_inputs == false);
    }
    // No file (an empty stream): nothing set.
    {
        const auto ini = Parse("");
        assert(ini.vulkan_upscaler.empty() && ini.frame_generation.empty() && !ini.dlss_inputs);
    }
    std::puts("optiscaler ini: ok");
    return 0;
}

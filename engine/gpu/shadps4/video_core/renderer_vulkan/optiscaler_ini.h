// SPDX-License-Identifier: GPL-2.0-or-later
// bbport (0.4.6 beta 17): what the engine reads from OptiScaler.ini (vk_optiscaler.h): the upscaler it
// runs for a Vulkan game, whether it takes the game's DLSS calls, its GPU spoofing and its frame
// generation. OptiScaler's keys (its own OptiScaler.ini): "auto" leaves a key to its default.

#pragma once

#include <algorithm>
#include <cctype>
#include <istream>
#include <optional>
#include <string>

namespace Vulkan::OptiScaler {

struct IniSettings {
    std::string vulkan_upscaler;  ///< [Upscalers] VulkanUpscaler; empty: its default
    std::string frame_generation; ///< [FrameGen] FGOutput (v10) or FGType (0.9); empty: off or default
    std::optional<bool> dlss_inputs;               ///< [Inputs] EnableDlssInputs
    std::optional<bool> vulkan_spoofing;           ///< [Spoofing] Vulkan (an NVIDIA GPU shown)
    std::optional<bool> vulkan_extension_spoofing; ///< [Spoofing] VulkanExtensionSpoofing
    std::optional<bool> dxgi_spoofing;             ///< [Spoofing] Dxgi
};

namespace IniDetail {
inline std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}
inline std::string Trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    const auto last = text.find_last_not_of(" \t\r");
    return first == std::string::npos ? std::string{} : text.substr(first, last - first + 1);
}
/// true, false, 1, 0; anything else (auto) leaves it to OptiScaler's default.
inline std::optional<bool> Flag(const std::string& value) {
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    return std::nullopt;
}
} // namespace IniDetail

inline IniSettings ParseIni(std::istream& in) {
    using namespace IniDetail;
    IniSettings ini;
    std::string line, section, fg_output, fg_type;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (line[0] == '[') {
            section = Lower(Trim(line.substr(1, line.find(']') - 1)));
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = Lower(Trim(line.substr(0, eq)));
        std::string value = line.substr(eq + 1);
        if (const auto comment = value.find(';'); comment != std::string::npos) {
            value.erase(comment);
        }
        value = Lower(Trim(value));
        if (section == "upscalers" && key == "vulkanupscaler") {
            ini.vulkan_upscaler = value == "auto" ? std::string{} : value;
        } else if (section == "framegen" && key == "fgoutput") {
            fg_output = value;
        } else if (section == "framegen" && key == "fgtype") {
            fg_type = value;
        } else if (section == "inputs" && key == "enabledlssinputs") {
            ini.dlss_inputs = Flag(value);
        } else if (section == "spoofing" && key == "vulkan") {
            ini.vulkan_spoofing = Flag(value);
        } else if (section == "spoofing" && key == "vulkanextensionspoofing") {
            ini.vulkan_extension_spoofing = Flag(value);
        } else if (section == "spoofing" && key == "dxgi") {
            ini.dxgi_spoofing = Flag(value);
        }
    }
    const std::string fg = fg_output.empty() ? fg_type : fg_output;
    ini.frame_generation = fg == "nofg" || fg == "auto" ? std::string{} : fg;
    return ini;
}

} // namespace Vulkan::OptiScaler

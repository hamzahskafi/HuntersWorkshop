// SPDX-License-Identifier: GPL-3.0-or-later
// A comma list of names in an environment variable (BB_FRAME_FIXES_SKIP, BB_GAME_FIXES_SKIP), case
// and spaces ignored: each known name turns its entry off. Returns the names it did not know.
#pragma once
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

namespace BbEngine {
inline std::vector<std::string> SkipList(const char* variable, std::span<const char* const> names,
                                         std::span<bool> on) {
    std::vector<std::string> unknown;
    const char* list = std::getenv(variable);
    if (!list) return unknown;
    std::string name;
    for (const char* c = list;; ++c) {
        if (*c && *c != ',') {
            if (!std::isspace(static_cast<unsigned char>(*c))) name += char(std::tolower(static_cast<unsigned char>(*c)));
            continue;
        }
        bool known = name.empty();
        for (std::size_t i = 0; i < names.size() && i < on.size(); ++i) {
            if (name == names[i]) {
                on[i] = false;
                known = true;
            }
        }
        if (!known) unknown.push_back(name);
        name.clear();
        if (!*c) break;
    }
    return unknown;
}
} // namespace BbEngine

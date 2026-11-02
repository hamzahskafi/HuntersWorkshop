// SPDX-License-Identifier: GPL-2.0-or-later
// Private settings for the online backend's HTTP (sceHttp). Renderer settings stay separate.
#pragma once
#include <cstdlib>
#include <string>

class BbNetSettingsImpl {
public:
    static BbNetSettingsImpl* GetInstance() { static BbNetSettingsImpl s; return &s; }
    static bool Flag(const char* name, bool fallback) {
        const char* v = std::getenv(name);
        return v ? v[0] == '1' : fallback;
    }
    bool IsConnectedToNetwork() const { return Flag("BB_ONLINE", false); }
    /// shadPS4's disable_https: HTTPS requests the game makes go out as plain HTTP.
    bool IsForcedHttpsDisabled() const { return Flag("BB_DISABLE_HTTPS", false); }
};
#define EmulatorSettings (*BbNetSettingsImpl::GetInstance())

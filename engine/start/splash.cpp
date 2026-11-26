// SPDX-License-Identifier: GPL-2.0-or-later
// bb-start: the start's splash (splash.h).
#include "splash.h"

#include "assets.h"

#include <string>
#include <vector>

namespace start {

namespace {

/// Starts `line` with only `output` inherited (the session log, as its standard output): the splash
/// keeps nothing else of this program's open, the memory pool's handle that bb-probe inherits later
/// included. Without a usable `output`, nothing inherited.
bool Spawn(const fs::path& program, std::wstring line, HANDLE output) {
    STARTUPINFOEXW info{};
    info.StartupInfo.cb = sizeof info;
    std::vector<unsigned char> storage;
    bool inherit = false;
    if (output && output != INVALID_HANDLE_VALUE) {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        storage.resize(size);
        auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (InitializeProcThreadAttributeList(list, 1, 0, &size)) {
            if (UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &output, sizeof output, nullptr,
                                          nullptr)) {
                info.lpAttributeList = list;
                info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
                info.StartupInfo.hStdOutput = output;
                info.StartupInfo.hStdError = output;
                inherit = true;
            } else {
                DeleteProcThreadAttributeList(list);
            }
        }
    }
    PROCESS_INFORMATION process{};
    BOOL ok = CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, inherit,
                             inherit ? EXTENDED_STARTUPINFO_PRESENT : 0, nullptr, nullptr, &info.StartupInfo, &process);
    if (info.lpAttributeList) DeleteProcThreadAttributeList(info.lpAttributeList);
    if (!ok && inherit) { // an output Windows would not hand on: the splash without its log lines
        STARTUPINFOW plain{sizeof plain};
        ok = CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &plain, &process);
    }
    if (!ok) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

} // namespace

void Splash::Start(const fs::path& out, const fs::path& game) {
    // A restart's start (probe.c runtime_restart) has the last one's: that splash closed long ago.
    UnsetEnv("BB_SPLASH_STATUS");
    if (Env("BB_SPLASH", "1") == "0") return;
    // Among the helpers in out\tools (their own folder: a DLL put beside the game, OptiScaler's
    // dxgi.dll or winmm.dll, is never loaded into them).
    const fs::path program = out / "tools" / "bb-splash.exe";
    std::error_code error;
    if (!fs::is_regular_file(program, error)) return;
    wchar_t temp[MAX_PATH + 1]{};
    const DWORD n = GetTempPathW(MAX_PATH + 1, temp);
    if (!n || n > MAX_PATH) return;
    const DWORD self = GetCurrentProcessId();
    const fs::path path = fs::path(temp) / (L"hunters-workshop-splash-" + std::to_wstring(self) + L".txt");
    if (!writer.Open(path, true)) return;
    writer.Write(BbSplashStatus::Step::Prepare);
    writer.Close();
    const std::wstring line = CommandLine(program, {L"--status", path.native(), L"--parent", std::to_wstring(self),
                                                    L"--game-dir", game.native()});
    HANDLE output = static_cast<HANDLE>(LogHandle());
    if (!output) output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!Spawn(program, line, output)) {
        Log("Splash: bb-splash.exe did not start (error %lu)", GetLastError());
        fs::remove(path, error);
        return;
    }
    file = path;
    SetEnv("BB_SPLASH_STATUS", Utf8(path));
}

void Splash::Step(BbSplashStatus::Step step) {
    // Opened for each write (OPEN_EXISTING): a splash that closed removed it, and it stays removed.
    if (file.empty() || !writer.Open(file, false)) return;
    writer.Write(step);
    writer.Close();
}

Splash::~Splash() {
    // The splash closes as this program ends (it watches it), at its first frame's "close" or not.
    std::error_code error;
    if (!file.empty()) fs::remove(file, error);
}

} // namespace start

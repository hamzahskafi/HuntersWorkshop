// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.6 beta 17: the start's step for its splash, and helper programs run without a
// window (engine/startup.h).

#include "engine/startup.h"
#include "bbport_roles.h"

#include "../../splash/status_file.h"

#include <atomic>
#include <chrono>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <limits.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace BbStartup {

namespace {
std::mutex g_mutex; // the status below and its file
BbSplashStatus::Writer g_writer;
bool g_closed = false;
Step g_step = Step::Engine;
std::uint64_t g_done = 0, g_total = 0;
std::chrono::steady_clock::time_point g_written{};
std::atomic<bool> g_first_frame{false};
std::mutex g_shown_mutex;
std::condition_variable g_shown_cv;
bool g_shown = false;

const std::filesystem::path& StatusPath() {
    static const std::filesystem::path path = EnvPath("BB_SPLASH_STATUS");
    return path;
}

/// Under g_mutex: the status into the splash's file, at once (`now`) or when 50 ms have passed since
/// the last write (the warm-up counts each of its pipelines, on every compile thread). The file is
/// opened for each write: a splash that closes removes it at once, and nothing makes it again.
void Write(bool now) {
    if (g_closed || StatusPath().empty()) return;
    const auto at = std::chrono::steady_clock::now();
    if (!now && at - g_written < std::chrono::milliseconds(50)) return;
    g_written = at;
    if (g_writer.Open(StatusPath(), false)) {
        g_writer.Write(g_step, g_done, g_total);
        g_writer.Close();
    }
}

/// Hands each complete line of `pending` to `on_line`, keeping the rest.
void SplitLines(std::string& pending, const std::function<void(const std::string&)>& on_line) {
    size_t at = 0;
    for (size_t end; (end = pending.find('\n', at)) != std::string::npos; at = end + 1) {
        std::string row = pending.substr(at, end - at);
        if (!row.empty() && row.back() == '\r') row.pop_back();
        on_line(row);
    }
    pending.erase(0, at);
}

#ifdef _WIN32
std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
    return out;
}

/// One argument for CommandLineToArgvW's rules: quoted, its quotes and the backslashes before them
/// (and before the closing quote) escaped.
void AppendArgument(std::wstring& line, const std::wstring& arg) {
    line += L'"';
    for (size_t i = 0;; ++i) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++i;
            ++backslashes;
        }
        if (i == arg.size()) {
            line.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            line.append(backslashes * 2 + 1, L'\\');
        } else {
            line.append(backslashes, L'\\');
        }
        line += arg[i];
    }
    line += L'"';
}
#endif
} // namespace

bool SplashRunning() {
    std::error_code error;
    return !StatusPath().empty() && std::filesystem::exists(StatusPath(), error);
}

void SetStep(Step step) {
    std::scoped_lock lock{g_mutex};
    g_step = step;
    g_done = 0;
    g_total = 0;
    Write(true);
}

void SetProgress(std::uint64_t done, std::uint64_t total) {
    std::scoped_lock lock{g_mutex};
    g_done = done;
    g_total = total;
    Write(done >= total);
}

void CloseSplash() {
    std::scoped_lock lock{g_mutex};
    g_step = Step::Close;
    g_done = 0;
    g_total = 0;
    Write(true);
    g_closed = true;
}

void FirstFrameReady(int timeout_ms) {
    g_first_frame.store(true, std::memory_order_release);
    BbRoles::FirstFrame(); // 0.4.6 beta 18: the start's loading phase ends
    std::unique_lock lock{g_shown_mutex};
    g_shown_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [] { return g_shown; });
}

bool FirstFrame() {
    return g_first_frame.load(std::memory_order_acquire);
}

void NoteGameWindowShown() {
    {
        std::scoped_lock lock{g_shown_mutex};
        g_shown = true;
    }
    g_shown_cv.notify_all();
}

std::filesystem::path ProgramDir() {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (n == 0 || n >= path.size()) return {};
    path.resize(n);
    return std::filesystem::path(path).parent_path();
#else
    char path[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n <= 0) return {};
    path[n] = 0;
    return std::filesystem::path(path).parent_path();
#endif
}

std::filesystem::path ToolPath(const char* name) {
    const std::filesystem::path out = ProgramDir();
    if (out.empty()) return {};
#ifdef _WIN32
    return out / (std::string(name) + ".exe");
#else
    return out / name;
#endif
}

std::filesystem::path EnvPath(const char* name) {
#ifdef _WIN32
    const std::wstring wide = Wide(name);
    const DWORD n = GetEnvironmentVariableW(wide.c_str(), nullptr, 0);
    if (n == 0) return {};
    std::wstring value(n, L'\0');
    const DWORD got = GetEnvironmentVariableW(wide.c_str(), value.data(), n);
    value.resize(got);
    return value;
#else
    const char* value = std::getenv(name);
    return value && *value ? std::filesystem::path(value) : std::filesystem::path{};
#endif
}

std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

int RunTool(const std::filesystem::path& program, const std::vector<std::string>& args,
            const std::function<void(const std::string&)>& on_line) {
#ifdef _WIN32
    std::wstring line;
    AppendArgument(line, program.wstring());
    for (const auto& arg : args) {
        line += L' ';
        AppendArgument(line, Wide(arg));
    }
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &sa, 0)) return -1;
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    int code = -1;
    if (CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                       nullptr, &si, &pi)) {
        CloseHandle(write);
        write = nullptr;
        char buffer[4096];
        DWORD got = 0;
        std::string pending;
        while (ReadFile(read, buffer, sizeof buffer, &got, nullptr) && got) {
            pending.append(buffer, got);
            SplitLines(pending, on_line);
        }
        if (!pending.empty()) on_line(pending);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exit_code = ~0u;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        // A crash ends it with an exception code (0xC0000005 ...), not an exit code of its own.
        code = exit_code >= 0xC0000000u ? -1 : int(exit_code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    if (write) CloseHandle(write);
    CloseHandle(read);
    return code;
#else
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) return -1;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);
    const std::string program_text = program.string();
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(program_text.c_str()));
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, program_text.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipe_fds[1]);
    if (spawned != 0) {
        close(pipe_fds[0]);
        return -1;
    }
    char buffer[4096];
    std::string pending;
    for (ssize_t got; (got = read(pipe_fds[0], buffer, sizeof buffer)) != 0;) {
        if (got < 0) {
            if (errno == EINTR) continue;
            break;
        }
        pending.append(buffer, size_t(got));
        SplitLines(pending, on_line);
    }
    if (!pending.empty()) on_line(pending);
    close(pipe_fds[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

} // namespace BbStartup

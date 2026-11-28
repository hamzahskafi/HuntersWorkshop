// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.6 beta 17: the start's splash's status file (status.h) written and read: by
// bb-start (start/splash.cpp), the engine (shim/engine/startup.cpp) and bb-splash (splash/main.cpp).
// Apart from status.h, which the engine's other files include, so that the system's headers stay here.

#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "status.h"

namespace BbSplashStatus {

/// The file, written in place. bb-start makes it (`create`); the engine only writes over it, opening
/// it for each write, so a file the splash removed as it closed stays removed.
class Writer {
public:
    Writer() = default;
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    ~Writer() { Close(); }

    bool Open(const std::filesystem::path& path, bool create) {
        Close();
#ifdef _WIN32
        file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, create ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        return file != INVALID_HANDLE_VALUE;
#else
        fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC | (create ? O_CREAT | O_TRUNC : 0), 0600);
        return fd >= 0;
#endif
    }
    bool IsOpen() const {
#ifdef _WIN32
        return file != INVALID_HANDLE_VALUE;
#else
        return fd >= 0;
#endif
    }
    void Write(Step step, std::uint64_t done = 0, std::uint64_t total = 0) {
        if (!IsOpen()) return;
        const std::string record = Record(++serial, step, done, total);
#ifdef _WIN32
        OVERLAPPED at{}; // offset 0
        DWORD written = 0;
        WriteFile(file, record.data(), DWORD(record.size()), &written, &at);
#else
        [[maybe_unused]] const ssize_t written = ::pwrite(fd, record.data(), record.size(), 0);
#endif
    }
    void Close() {
#ifdef _WIN32
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
#else
        if (fd >= 0) ::close(fd);
        fd = -1;
#endif
    }

private:
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
#endif
    std::uint32_t serial = 0;
};

/// The file read in place while the start writes it (the splash).
class Reader {
public:
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    ~Reader() { Close(); }
    explicit Reader(std::filesystem::path path_) : path(std::move(path_)) {
#ifdef _WIN32
        file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
        fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
#endif
    }
    /// The record now in the file; none while it is half written.
    std::optional<Status> Read() const {
        char text[kRecordSize * 2] = {};
#ifdef _WIN32
        if (file == INVALID_HANDLE_VALUE) return std::nullopt;
        OVERLAPPED at{}; // offset 0
        DWORD got = 0;
        if (!ReadFile(file, text, DWORD(sizeof text), &got, &at)) return std::nullopt;
#else
        if (fd < 0) return std::nullopt;
        const ssize_t got = ::pread(fd, text, sizeof text, 0);
        if (got <= 0) return std::nullopt;
#endif
        return Parse(std::string_view(text, size_t(got)));
    }
    /// Closed and removed: the start is over (bb-start removes it too, on Windows).
    void Remove() {
        Close();
#ifdef _WIN32
        DeleteFileW(path.c_str());
#else
        ::unlink(path.c_str());
#endif
    }

private:
    void Close() {
#ifdef _WIN32
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
#else
        if (fd >= 0) ::close(fd);
        fd = -1;
#endif
    }

    std::filesystem::path path;
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
#endif
};

} // namespace BbSplashStatus

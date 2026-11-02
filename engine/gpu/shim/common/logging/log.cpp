// bbport: see log.h.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include "common/logging/log.h"

namespace Common::Log {
static Level threshold() {
    static const Level level = [] {
        // The launcher's Log level (BB_LOG_LEVEL): info at detailed, debug at debug, else warnings
        // (0.4.4 beta 28: BB_GPU_LOG, a second name for it, went).
        const char* log_level = std::getenv("BB_LOG_LEVEL");
        if (log_level && !std::strcmp(log_level, "debug")) return Level::debug;
        if (log_level && !std::strcmp(log_level, "detailed")) return Level::info;
        return Level::warn;
    }();
    return level;
}
bool ShouldLog(Level level) { return level >= threshold(); }
void Write(std::string_view log_class, Level level, const char* file, int line, const char* function,
           std::string&& message) {
    static std::mutex mutex;
    static const char* names[] = {"Trace", "Debug", "Info", "Warning", "Error", "Critical"};
    const char* base = std::strrchr(file, '/');
    std::scoped_lock lock{mutex};
    std::fprintf(stderr, "GPU [%.*s] <%s> %s:%d %s: %s\n", int(log_class.size()), log_class.data(),
                 names[int(level)], base ? base + 1 : file, line, function, message.c_str());
}
} // namespace Common::Log

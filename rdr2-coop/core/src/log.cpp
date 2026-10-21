#include "coop/log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace coop {
namespace {

std::mutex g_mutex;
std::FILE* g_file = nullptr;
bool g_console = false;
bool g_verbose = false;

const char* levelName(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO ";
        case LogLevel::Warn: return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

}  // namespace

bool logOpenFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file != nullptr) std::fclose(g_file);
    g_file = std::fopen(path.c_str(), "a");
    return g_file != nullptr;
}

void logToConsole(bool enabled) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_console = enabled;
}

void logSetVerbose(bool enabled) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_verbose = enabled;
}

void logf(LogLevel level, const char* fmt, ...) {
    char message[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    const auto now = std::chrono::system_clock::now();
    const std::time_t secs = std::chrono::system_clock::to_time_t(now);
    const int ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (level == LogLevel::Debug && !g_verbose) return;
    if (g_file != nullptr) {
        std::fprintf(g_file, "%s.%03d %s %s\n", stamp, ms, levelName(level), message);
        std::fflush(g_file);
    }
    if (g_console) {
        std::fprintf(stdout, "%s.%03d %s %s\n", stamp, ms, levelName(level), message);
        std::fflush(stdout);
    }
}

}  // namespace coop

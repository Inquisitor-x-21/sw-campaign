#pragma once

#include <string>

#if defined(__GNUC__) || defined(__clang__)
#define COOP_PRINTF(fmtIndex, argIndex) __attribute__((format(printf, fmtIndex, argIndex)))
#else
#define COOP_PRINTF(fmtIndex, argIndex)
#endif

namespace coop {

enum class LogLevel { Debug, Info, Warn, Error };

bool logOpenFile(const std::string& path);  // appends; returns false if it can't be opened
void logToConsole(bool enabled);
void logSetVerbose(bool enabled);           // Debug lines are dropped unless verbose
void logf(LogLevel level, const char* fmt, ...) COOP_PRINTF(2, 3);

}  // namespace coop

#define LOG_DEBUG(...) ::coop::logf(::coop::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...) ::coop::logf(::coop::LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...) ::coop::logf(::coop::LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::coop::logf(::coop::LogLevel::Error, __VA_ARGS__)

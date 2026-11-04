// Minimal levelled logger: stderr (picked up by journald) or syslog.
#pragma once

#include <optional>
#include <string_view>

namespace wifimgr {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

void setLogLevel(LogLevel level);
LogLevel logLevel();
std::optional<LogLevel> logLevelFromString(std::string_view s);

// Route output to syslog(3) instead of stderr.
void setLogSyslog(bool enabled, const char* ident = "wifimgrd");

void logf(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

}  // namespace wifimgr

#define LOGD(...) ::wifimgr::logf(::wifimgr::LogLevel::Debug, __VA_ARGS__)
#define LOGI(...) ::wifimgr::logf(::wifimgr::LogLevel::Info, __VA_ARGS__)
#define LOGW(...) ::wifimgr::logf(::wifimgr::LogLevel::Warn, __VA_ARGS__)
#define LOGE(...) ::wifimgr::logf(::wifimgr::LogLevel::Error, __VA_ARGS__)

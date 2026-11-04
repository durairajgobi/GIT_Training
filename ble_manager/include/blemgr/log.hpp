// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <string>

namespace blemgr {

enum class LogLevel { Error = 0, Warn, Info, Debug };

void setLogLevel(LogLevel level);
LogLevel logLevel();
/// Replace the default sink (stderr, journald-friendly "<N>" prefixes).
void setLogSink(std::function<void(LogLevel, const std::string&)> sink);
void logf(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

}  // namespace blemgr

#define BLE_LOGE(...) ::blemgr::logf(::blemgr::LogLevel::Error, __VA_ARGS__)
#define BLE_LOGW(...) ::blemgr::logf(::blemgr::LogLevel::Warn, __VA_ARGS__)
#define BLE_LOGI(...) ::blemgr::logf(::blemgr::LogLevel::Info, __VA_ARGS__)
#define BLE_LOGD(...) ::blemgr::logf(::blemgr::LogLevel::Debug, __VA_ARGS__)

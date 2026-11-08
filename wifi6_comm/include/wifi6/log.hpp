// SPDX-License-Identifier: MIT
// Minimal printf-style logger (stderr; journald picks it up under systemd).
#pragma once

#include <cstdint>
#include <ctime>

namespace wifi6 {

enum class LogLevel : int { Error = 0, Warn = 1, Info = 2, Debug = 3 };

void setLogLevel(LogLevel lvl) noexcept;
LogLevel logLevel() noexcept;
void logf(LogLevel lvl, const char* tag, const char* fmt, ...) noexcept __attribute__((format(printf, 3, 4)));

inline uint64_t monotonicMs() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}

}  // namespace wifi6

#define W6_LOGE(tag, ...) ::wifi6::logf(::wifi6::LogLevel::Error, tag, __VA_ARGS__)
#define W6_LOGW(tag, ...) ::wifi6::logf(::wifi6::LogLevel::Warn, tag, __VA_ARGS__)
#define W6_LOGI(tag, ...) ::wifi6::logf(::wifi6::LogLevel::Info, tag, __VA_ARGS__)
#define W6_LOGD(tag, ...) ::wifi6::logf(::wifi6::LogLevel::Debug, tag, __VA_ARGS__)

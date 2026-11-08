// SPDX-License-Identifier: MIT
#include "wifi6/log.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace wifi6 {
namespace {
std::atomic<int> g_level{static_cast<int>(LogLevel::Info)};
}

void setLogLevel(LogLevel lvl) noexcept { g_level.store(static_cast<int>(lvl), std::memory_order_relaxed); }
LogLevel logLevel() noexcept { return static_cast<LogLevel>(g_level.load(std::memory_order_relaxed)); }

void logf(LogLevel lvl, const char* tag, const char* fmt, ...) noexcept {
    if (static_cast<int>(lvl) > g_level.load(std::memory_order_relaxed)) return;
    static const char kLetters[] = {'E', 'W', 'I', 'D'};
    const uint64_t ms = monotonicMs();
    char line[512];
    int n = std::snprintf(line, sizeof line, "[%7llu.%03llu] %c %-6s ",
                          static_cast<unsigned long long>(ms / 1000), static_cast<unsigned long long>(ms % 1000),
                          kLetters[static_cast<int>(lvl) & 3], tag);
    if (n < 0) return;
    va_list ap;
    va_start(ap, fmt);
    if (static_cast<size_t>(n) < sizeof line) std::vsnprintf(line + n, sizeof line - static_cast<size_t>(n), fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "%s\n", line);  // one write per line keeps output unscrambled
}

}  // namespace wifi6

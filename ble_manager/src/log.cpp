// SPDX-License-Identifier: MIT
#include "blemgr/log.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace blemgr {
namespace {
std::atomic<LogLevel> g_level{LogLevel::Info};
std::mutex g_mu;
std::function<void(LogLevel, const std::string&)> g_sink;

void defaultSink(LogLevel l, const std::string& msg) {
    // sd-daemon(3) priority prefixes: journald maps them, a TTY just shows them.
    static const int prio[] = {3, 4, 6, 7};
    std::fprintf(stderr, "<%d>%s\n", prio[static_cast<int>(l)], msg.c_str());
}
}  // namespace

void setLogLevel(LogLevel level) { g_level = level; }
LogLevel logLevel() { return g_level; }

void setLogSink(std::function<void(LogLevel, const std::string&)> sink) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_sink = std::move(sink);
}

void logf(LogLevel level, const char* fmt, ...) {
    if (level > g_level.load()) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_sink)
        g_sink(level, buf);
    else
        defaultSink(level, buf);
}

}  // namespace blemgr

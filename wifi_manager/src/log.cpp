#include "wifimgr/log.hpp"

#include <syslog.h>

#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace wifimgr {
namespace {

LogLevel g_level = LogLevel::Info;
bool g_syslog = false;

const char* levelTag(LogLevel l) {
  switch (l) {
    case LogLevel::Debug: return "D";
    case LogLevel::Info: return "I";
    case LogLevel::Warn: return "W";
    case LogLevel::Error: return "E";
  }
  return "?";
}

int syslogPriority(LogLevel l) {
  switch (l) {
    case LogLevel::Debug: return LOG_DEBUG;
    case LogLevel::Info: return LOG_INFO;
    case LogLevel::Warn: return LOG_WARNING;
    case LogLevel::Error: return LOG_ERR;
  }
  return LOG_INFO;
}

}  // namespace

void setLogLevel(LogLevel level) { g_level = level; }
LogLevel logLevel() { return g_level; }

std::optional<LogLevel> logLevelFromString(std::string_view s) {
  if (s == "debug") return LogLevel::Debug;
  if (s == "info") return LogLevel::Info;
  if (s == "warn" || s == "warning") return LogLevel::Warn;
  if (s == "error") return LogLevel::Error;
  return std::nullopt;
}

void setLogSyslog(bool enabled, const char* ident) {
  if (enabled && !g_syslog) openlog(ident, LOG_PID, LOG_DAEMON);
  if (!enabled && g_syslog) closelog();
  g_syslog = enabled;
}

void logf(LogLevel level, const char* fmt, ...) {
  if (level < g_level) return;
  va_list ap;
  va_start(ap, fmt);
  if (g_syslog) {
    vsyslog(syslogPriority(level), fmt, ap);
  } else {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    std::fprintf(stderr, "[%5ld.%03ld] %s ", static_cast<long>(ts.tv_sec), ts.tv_nsec / 1000000L,
                 levelTag(level));
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
  }
  va_end(ap);
}

}  // namespace wifimgr

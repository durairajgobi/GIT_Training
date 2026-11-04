#include <cstring>
#include <exception>

#include "test.hpp"
#include "wifimgr/log.hpp"

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  wifimgr::setLogLevel(getenv("WIFIMGR_TEST_LOG") ? wifimgr::LogLevel::Debug
                                                  : wifimgr::LogLevel::Error);
  int run = 0, failedCases = 0;
  for (const auto& c : t::registry()) {
    if (filter && !std::strstr(c.name, filter)) continue;
    int before = t::failures();
    ++run;
    try {
      c.fn();
    } catch (const t::Abort&) {
    } catch (const std::exception& e) {
      std::fprintf(stderr, "  unexpected exception: %s\n", e.what());
      ++t::failures();
    }
    bool ok = t::failures() == before;
    if (!ok) ++failedCases;
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name);
  }
  std::printf("\n%d test(s), %d failed\n", run, failedCases);
  return failedCases == 0 ? 0 : 1;
}

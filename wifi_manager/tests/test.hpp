// Tiny self-registering test harness (no external dependencies, so the
// tests also run inside a Yocto SDK / on target).
#pragma once

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace t {

struct Case {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}
inline int& failures() {
  static int f = 0;
  return f;
}

struct Register {
  Register(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct Abort {};

template <typename A, typename B>
std::string describe(const A& a, const B& b) {
  std::ostringstream o;
  o << "'" << a << "' vs '" << b << "'";
  return o.str();
}

}  // namespace t

#define T_CAT2(a, b) a##b
#define T_CAT(a, b) T_CAT2(a, b)
#define TEST(name)                                                   \
  static void name();                                                \
  static ::t::Register T_CAT(reg_, name)(#name, name);               \
  static void name()

#define CHECK(cond)                                                                  \
  do {                                                                               \
    if (!(cond)) {                                                                   \
      std::fprintf(stderr, "  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++::t::failures();                                                             \
    }                                                                                \
  } while (0)

#define CHECK_EQ(a, b)                                                                 \
  do {                                                                                 \
    const auto _a = (a);                                                                \
    const auto _b = (b);                                                                \
    if (!(_a == _b)) {                                                                 \
      std::fprintf(stderr, "  %s:%d: CHECK_EQ(%s, %s) failed: %s\n", __FILE__, __LINE__, \
                   #a, #b, ::t::describe(_a, _b).c_str());                             \
      ++::t::failures();                                                               \
    }                                                                                  \
  } while (0)

#define REQUIRE(cond)                                                                    \
  do {                                                                                   \
    if (!(cond)) {                                                                       \
      std::fprintf(stderr, "  %s:%d: REQUIRE(%s) failed\n", __FILE__, __LINE__, #cond);   \
      ++::t::failures();                                                                 \
      throw ::t::Abort{};                                                                \
    }                                                                                    \
  } while (0)

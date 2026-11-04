// SPDX-License-Identifier: MIT
// Minimal self-registering test harness (no external dependencies).
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
inline std::vector<Case>& cases() {
    static std::vector<Case> c;
    return c;
}
inline int& failures() {
    static int f = 0;
    return f;
}
struct Reg {
    Reg(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); }
};

inline int runAll() {
    int failedCases = 0;
    for (auto& c : cases()) {
        int before = failures();
        c.fn();
        bool ok = failures() == before;
        failedCases += !ok;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", c.name);
    }
    std::printf("%zu cases, %d failed\n", cases().size(), failedCases);
    return failedCases ? 1 : 0;
}

template <class A, class B>
void checkEq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
    if (a == b) return;
    std::ostringstream os;
    if constexpr (std::is_enum_v<A>)
        os << static_cast<long long>(a) << " vs " << static_cast<long long>(b);
    else
        os << a << " vs " << b;
    std::printf("  %s:%d: CHECK_EQ(%s, %s) failed: %s\n", file, line, ea, eb, os.str().c_str());
    ++failures();
}

}  // namespace t

#define T_CAT2(a, b) a##b
#define T_CAT(a, b) T_CAT2(a, b)
#define TEST(name)                                                   \
    static void name();                                              \
    static ::t::Reg T_CAT(reg_, name)(#name, &name);                 \
    static void name()
#define CHECK(c)                                                                     \
    do {                                                                             \
        if (!(c)) {                                                                  \
            std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);      \
            ++::t::failures();                                                       \
        }                                                                            \
    } while (0)
#define CHECK_EQ(a, b) ::t::checkEq((a), (b), #a, #b, __FILE__, __LINE__)
#define TEST_MAIN() \
    int main() { return ::t::runAll(); }

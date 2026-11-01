// Tiny self-contained test framework (no external dependencies).
#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace wt {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline int runAll(int argc, char** argv) {
    std::string filter = argc > 1 ? argv[1] : "";
    int passed = 0, failed = 0;
    for (const auto& t : registry()) {
        if (!filter.empty() && t.name.find(filter) == std::string::npos) continue;
        try {
            t.fn();
            ++passed;
            std::cout << "[ PASS ] " << t.name << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[ FAIL ] " << t.name << "\n         " << e.what() << "\n";
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    return failed == 0 && passed > 0 ? 0 : 1;
}

}  // namespace wt

#define WT_CONCAT2(a, b) a##b
#define WT_CONCAT(a, b) WT_CONCAT2(a, b)

#define TEST(name)                                                              \
    static void WT_CONCAT(wt_test_, name)();                                    \
    static wt::Registrar WT_CONCAT(wt_reg_, name)(#name, WT_CONCAT(wt_test_, name)); \
    static void WT_CONCAT(wt_test_, name)()

#define WT_FAIL(msg)                                                         \
    do {                                                                     \
        std::ostringstream wt_os;                                            \
        wt_os << __FILE__ << ":" << __LINE__ << ": " << msg;                 \
        throw wt::Failure(wt_os.str());                                      \
    } while (0)

#define EXPECT_TRUE(cond) \
    do { if (!(cond)) WT_FAIL("expected true: " #cond); } while (0)

#define EXPECT_FALSE(cond) \
    do { if (cond) WT_FAIL("expected false: " #cond); } while (0)

#define EXPECT_EQ(a, b)                                                                     \
    do {                                                                                    \
        const auto wt_a = (a);                                                              \
        const auto wt_b = (b);                                                              \
        if (!(wt_a == wt_b)) WT_FAIL("expected " #a " == " #b "\n           got: " << wt_a  \
                                     << "\n      expected: " << wt_b);                      \
    } while (0)

#define EXPECT_CONTAINS(haystack, needle)                                                    \
    do {                                                                                     \
        std::string wt_h = (haystack), wt_n = (needle);                                      \
        if (wt_h.find(wt_n) == std::string::npos)                                            \
            WT_FAIL("expected to find \"" << wt_n << "\" in:\n" << wt_h);                    \
    } while (0)

#define EXPECT_THROWS(expr, ExType)                                                        \
    do {                                                                                   \
        bool wt_thrown = false;                                                            \
        try { (void)(expr); } catch (const ExType&) { wt_thrown = true; }                  \
        if (!wt_thrown) WT_FAIL("expected " #expr " to throw " #ExType);                   \
    } while (0)

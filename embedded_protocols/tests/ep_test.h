/* Minimal unit-test harness usable from C and C++ (no exceptions). */
#ifndef EP_TEST_H
#define EP_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ep_test_failures;
static int ep_test_checks;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ep_test_checks++;                                                        \
        if (!(cond)) {                                                           \
            ep_test_failures++;                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        long long _a = (long long)(a), _b = (long long)(b);                      \
        ep_test_checks++;                                                        \
        if (_a != _b) {                                                          \
            ep_test_failures++;                                                  \
            fprintf(stderr, "%s:%d: CHECK_EQ failed: %s (%lld) != %s (%lld)\n",  \
                    __FILE__, __LINE__, #a, _a, #b, _b);                         \
        }                                                                        \
    } while (0)

#define CHECK_MEM(a, b, n) CHECK(memcmp((a), (b), (n)) == 0)

#define RUN(fn)                                                                  \
    do {                                                                         \
        int _before = ep_test_failures;                                          \
        fn();                                                                    \
        printf("%-40s %s\n", #fn, ep_test_failures == _before ? "ok" : "FAIL");  \
    } while (0)

#define TEST_MAIN_END()                                                          \
    do {                                                                         \
        printf("%d checks, %d failures\n", ep_test_checks, ep_test_failures);    \
        return ep_test_failures ? EXIT_FAILURE : EXIT_SUCCESS;                   \
    } while (0)

#endif

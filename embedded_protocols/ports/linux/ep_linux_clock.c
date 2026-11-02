#include "ep_linux.h"

#include <errno.h>
#include <time.h>

static uint64_t mono_now(void *ctx)
{
    (void)ctx;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void mono_delay(void *ctx, uint32_t us)
{
    (void)ctx;
    struct timespec ts = {.tv_sec = us / 1000000u, .tv_nsec = (long)(us % 1000000u) * 1000L};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
    }
}

static const ep_clock_t linux_clock = {mono_now, mono_delay, NULL};

const ep_clock_t *ep_linux_clock(void)
{
    return &linux_clock;
}

// gt2_sysclock host tests: combine vectors (scripted HW counters).

#include "gt2/sysclock.h"

#include <stdio.h>

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
        failures++; \
    } \
} while (0)

int main(void) {
    // Emulated run (timers 1111/2222/3333/4444).
    CHECK(gt2_sysclock_combine(0x1111, 0x2222, 0x3333, 0x4444) ==
          0x04754031u, "scripted");
    CHECK(gt2_sysclock_combine(0, 0, 0, 0) == 0u, "zeros");
    CHECK(gt2_sysclock_combine(0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF) ==
          (0xFFFFu ^ (0xFFFFu << 4) ^ (0xFFFFu << 8) ^ (0xFFFFu << 12)),
          "ones");
    CHECK(GT2_TIMER2_MODE == 0x248u, "timer mode");

    if (failures == 0)
        printf("PASS test_sysclock (combine ok)\n");
    else
        printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}

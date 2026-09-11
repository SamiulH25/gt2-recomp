// gt2_host tests: vsync spin predicate + timer ticks (self-contained,
// no disc needed). Contracts from gt2_sysinit (see gt2/host.h).

#include "gt2/host.h"
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
    gt2_host_vsync_t v = { 0 };
    CHECK(!gt2_host_vsync_ready(&v), "not ready at 0");
    CHECK(!gt2_host_vsync_ready(NULL), "null not ready");
    CHECK(gt2_host_vsync_tick(NULL) == GT2_HOST_ERR_INVAL, "tick null");
    for (int i = 0; i < 3; i++)
        CHECK(gt2_host_vsync_tick(&v) == GT2_HOST_OK, "tick %d", i);
    CHECK(!gt2_host_vsync_ready(&v), "not ready at 3");
    CHECK(gt2_host_vsync_tick(&v) == GT2_HOST_OK, "tick 4");
    CHECK(gt2_host_vsync_ready(&v), "ready at 4 (setup spins to >= 4)");
    CHECK(v.count == 4, "count %u", v.count);

    gt2_host_timer_t t = { 0x1111, 0x2222, 0x3333, 0x4444 };
    CHECK(gt2_host_timer_tick(&t) == GT2_HOST_OK, "timer tick");
    CHECK(t.t0 == 0x1112 && t.t3 == 0x4445, "timer advanced");
    CHECK(gt2_host_timer_tick(NULL) == GT2_HOST_ERR_INVAL, "timer null");
    // Ticks feed the already-ported sysclock combine (u16 lanes).
    u32 c = gt2_sysclock_combine((u16)t.t0, (u16)t.t1, (u16)t.t2,
                                 (u16)t.t3);
    CHECK(c == (0x1112u ^ (0x2223u << 4) ^ (0x3334u << 8) ^
                (0x4445u << 12)),
          "combine over host ticks");

    if (failures == 0)
        printf("PASS test_host (vsync + timer ok)\n");
    return failures != 0;
}

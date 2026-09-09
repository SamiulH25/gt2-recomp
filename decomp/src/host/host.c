// GT2 native port: host backends (vsync counter + timer ticks).
// See gt2/host.h for the exact game contracts.

#include "gt2/host.h"

const char *gt2_host_strerror(gt2_host_status_t st) {
    switch (st) {
    case GT2_HOST_OK: return "ok";
    case GT2_HOST_ERR_INVAL: return "invalid argument";
    default: return "unknown";
    }
}

gt2_host_status_t gt2_host_vsync_tick(gt2_host_vsync_t *v) {
    if (!v)
        return GT2_HOST_ERR_INVAL;
    v->count++;
    return GT2_HOST_OK;
}

int gt2_host_vsync_ready(const gt2_host_vsync_t *v) {
    if (!v)
        return 0;
    // Setup spins until RAM 0x80011DF4 >= 4.
    return v->count >= GT2_HOST_VSYNC_SPIN_TARGET;
}

gt2_host_status_t gt2_host_timer_tick(gt2_host_timer_t *t) {
    if (!t)
        return GT2_HOST_ERR_INVAL;
    // Unit ticks (free-running counters; feed gt2_sysclock_combine).
    t->t0++;
    t->t1++;
    t->t2++;
    t->t3++;
    return GT2_HOST_OK;
}

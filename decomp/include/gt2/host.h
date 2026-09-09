#pragma once
// GT2 native port: host backends for the HW-coupled side of sysinit
// (SCUS gt2_sysinit 0x80010998) and boot. Only what has exact,
// standalone semantics is functional; the rest is rendezvous addresses
// plus the documented gap. See decomp/docs/boot_notes.md.
//
// - vsync (0x80010954 setup + 0x80010928 handler): the handler bumps a
//   RAM counter (game: u32 at 0x80011DF4); setup spins until it reaches
//   4 (four vsyncs). Ported exactly: tick() + ready() (>= 4). The
//   handler install itself (via 0x8008BD08) is host wiring, not modeled.
// - timer (sysclock 0x800108C0): Timer2 mode 0x248 programs hardware;
//   the combine (t0^(t1<<4)^(t2<<8)^(t3<<12) -> 0x801C93D4) is already
//   ported in gt2/sysclock.h. This module only holds injectable tick
//   values for it (same pattern as gt2_spu wait_idle injection).
// - pad (0x80087148 PadInitDirect, buffers 0x801F0C98/0x801F0CBA): full
//   SIO driver init (port setup, buffer registration, IRQ) — HW-coupled,
//   NOT ported. Addresses kept as the rendezvous for future SIO work.
// - card/FS init (0x80086CE8/0x80086D78/0x80086DE8): BIOS A/B-table
//   trampolines (A 0x70/0xAB, B 0x4F/0x4E/0x4A) — docs-only, not ported.
//   The data path is the 0x80073978 state machine (see docs/save_notes.md)
//   over gt2_mcd images.
// - GPU (0x8007FE34) / GTE (0x8008BB10, B-table 0x56) setup: HW/BIOS-only,
//   not ported.

#include "gt2/types.h"

typedef enum {
    GT2_HOST_OK = 0,
    GT2_HOST_ERR_INVAL,
} gt2_host_status_t;

const char *gt2_host_strerror(gt2_host_status_t st);

// Game-RAM rendezvous (informational; the native buffers live with the
// caller, cf. gt2_boot_state_t).
#define GT2_HOST_VSYNC_CELL 0x80011DF4u
#define GT2_HOST_VSYNC_SPIN_TARGET 4u
#define GT2_HOST_PAD_BUF0 0x801F0C98u
#define GT2_HOST_PAD_BUF1 0x801F0CBAu
#define GT2_HOST_TIMER_MODE 0x248u

// vsync counter: tick() models one handler firing; ready() is the setup
// spin predicate (count >= 4).
typedef struct {
    u32 count;
} gt2_host_vsync_t;

gt2_host_status_t gt2_host_vsync_tick(gt2_host_vsync_t *v);
int gt2_host_vsync_ready(const gt2_host_vsync_t *v);

// Timer tick values feeding gt2_sysclock_combine (unit ticks; the rate
// is host time, exact hardware cadence not modeled).
typedef struct {
    u32 t0, t1, t2, t3;
} gt2_host_timer_t;

gt2_host_status_t gt2_host_timer_tick(gt2_host_timer_t *t);

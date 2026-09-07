#pragma once
// GT2 native port: system clock/counter setup.
//
// Behavior reference: SCUS gt2_sysinit_sysclock_setup (0x800108C0),
// confirmed by emulation: writes 0 then GT2_TIMER2_MODE to Timer2
// control (0x1F801124), reads four HW counters, combines and stores:
//   sys = t0 ^ (t1 << 4) ^ (t2 << 8) ^ (t3 << 12)   (to 0x801C93D4)
// The port takes counter values from the host (HLE) and returns the
// combined value; timer programming is the host backend's job.

#include "gt2/types.h"

#define GT2_TIMER2_MODE 0x248u

static inline u32 gt2_sysclock_combine(u16 t0, u16 t1, u16 t2, u16 t3) {
    return (u32)t0 ^ ((u32)t1 << 4) ^ ((u32)t2 << 8) ^ ((u32)t3 << 12);
}

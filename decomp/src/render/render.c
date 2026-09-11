// GT2 native port: GPU packet emitters (SCUS 0x8007DA44/0x80081478).
// Clean-room C from emulated behavior; never pasted. Unaligned lwl/swl
// mirror tools/mips_emu.py bit-for-bit (little-endian pair). See
// gt2/render.h + docs/menu_notes.md.

#include "gt2/render.h"

#include <string.h>

const char *gt2_render_strerror(gt2_render_status_t st) {
    switch (st) {
    case GT2_RENDER_OK: return "ok";
    case GT2_RENDER_ERR_INVAL: return "invalid argument";
    case GT2_RENDER_ERR_OOB: return "packet outside arena window";
    default: return "unknown";
    }
}

// lwl Rt,[addr]: load bytes [align..addr] into Rt's top, keep the low
// bytes (little-endian; mirrors the emulator exactly). Alignment comes
// from the REAL pointer: callers preserve mod-4 alignment (as the test
// does by sliding), so native low bits equal the game's.
static u32 vlwl(const u8 *at, u32 rt) {
    uintptr_t addr = (uintptr_t)at;
    u32 n = addr & 3u, k = n + 1u;
    u32 w;
    memcpy(&w, (const void *)(addr & ~(uintptr_t)3u), 4);
    u32 keep = (k < 4u) ? ((1u << (32u - 8u * k)) - 1u) : 0u;
    return (rt & keep) | (w << (32u - 8u * k));
}

// swl Rt,[addr]: store Rt's top bytes down to addr (little-endian).
static void vswl(u8 *at, u32 v) {
    uintptr_t addr = (uintptr_t)at;
    u32 n = addr & 3u;
    for (u32 j = 0; j <= n; j++)
        *(at - (ptrdiff_t)j) = (u8)(v >> (24u - 8u * j));
}

// Shared body: tag selects A/B shape. Needs [top,top+8) in-arena and
// the [(fp+2)&~3, +4) word in-fp (8 B of fp covers every alignment).
static gt2_render_status_t emit(u8 *arena, u32 arena_len, u32 arena_addr,
                                u32 *top_io, u8 *fp, u32 fp_len, u32 flags,
                                u32 a2in, u8 **pkt_out, int is_a) {
    u32 top, abs_top, a2, v1, ret_off, bump;
    u8 *ap, *fpw;
    if (!arena || !top_io || !fp || !pkt_out)
        return GT2_RENDER_ERR_INVAL;
    top = *top_io;
    // Exact touched spans (game touches RAM unconditionally; native
    // validates): arena [top, top+8) plus the swl word at top+2
    // (reaches top-1 when (top+2)%4 == 3); fp [(fp+2)&~3, +4).
    ap = arena + top;
    if ((u64)top + 8u > arena_len)
        return GT2_RENDER_ERR_OOB;
    if ((((uintptr_t)(ap + 2u)) & 3u) == 3u) {
        if (top == 0)
            return GT2_RENDER_ERR_OOB;
    }
    fpw = fp + 2u;
    // Upper bound only: for fp%4 == 1 the aligned word starts 1 byte
    // BELOW fp (game reads caller frame/stack there — always mapped
    // in-game; native callers must likewise map a scratch byte, as the
    // test does with its slid block).
    {
        uintptr_t fa = (uintptr_t)fpw & ~(uintptr_t)3u;
        if (fa + 4u > (uintptr_t)fp + fp_len)
            return GT2_RENDER_ERR_INVAL;
    }
    // lwl a2,2(fp) — stale low bytes survive only as far as the swl
    // drops them (alignment-dependent; emu-proven).
    a2 = vlwl(fpw, a2in);
    // Tag byte.
    arena[top + 3u] = is_a ? 4u : 1u;
    // swl a2,2(top).
    vswl(ap + 2u, a2);
    // Link update: swl ((abs_top)<<8),2(fp).
    abs_top = arena_addr + top;
    v1 = abs_top << 8u;
    vswl(fpw, v1);
    // Flags word at top+4.
    {
        u32 f = is_a ? (flags ^ 0x64000000u) : (flags | 0xE1000000u);
        memcpy(arena + top + 4u, &f, 4);
    }
    ret_off = top + (is_a ? 8u : 4u);
    bump = is_a ? 0x14u : 8u;
    *pkt_out = arena + ret_off;
    *top_io = top + bump;
    return GT2_RENDER_OK;
}

gt2_render_status_t gt2_render_emit_b(u8 *arena, u32 arena_len, u32 arena_addr,
                                      u32 *top_io, u8 *fp, u32 fp_len,
                                      u32 flags, u32 a2in, u8 **pkt_out) {
    return emit(arena, arena_len, arena_addr, top_io, fp, fp_len, flags,
                a2in, pkt_out, 0);
}

gt2_render_status_t gt2_render_emit_a(u8 *arena, u32 arena_len, u32 arena_addr,
                                      u32 *top_io, u8 *fp, u32 fp_len,
                                      u32 flags, u32 a2in, u8 **pkt_out) {
    return emit(arena, arena_len, arena_addr, top_io, fp, fp_len, flags,
                a2in, pkt_out, 1);
}

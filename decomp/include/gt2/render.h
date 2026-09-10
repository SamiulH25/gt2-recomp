#pragma once
// GT2 native port: GPU packet emitters (SCUS render helpers).
//
// emit_B (`0x8007DA44`) / emit_A (`0x80081478`) append fixed packets to
// the arena whose top cell is game RAM `0x801C93EC` (behavior reference
// confirmed by emulation, see docs/menu_notes.md "Emitter protocol" and
// tools/render_arena.py):
//
// - Header: unaligned link word (lwl from fp+2, swl to top+2), tag byte
//   1 (B) / 4 (A) at top+3, flags (|0xE1000000 B / ^0x64000000 A) at
//   top+4. The caller's stale a2 survives lwl only to be dropped by the
//   swl whenever the load is at least as wide as the store (emu-proven
//   identical packets across a2 values at fp+2; leaks at fp+4 — hence
//   the explicit a2in below).
// - Link update: swl ((abs_top)<<8) back to fp+2 (partial store; the
//   untouched fp bytes are preserved exactly).
// - Bump: top += 8 (B, returns old+4) / += 0x14 (A, returns old+8). The
//   menu tick fills 12 B at A-returns with the next top exactly there
//   (perfect tiling); B-returns are never filled.
//
// The port takes the arena as a caller window + the game-visible base
// address cookie (link math needs absolute addresses); pass 0 for a
// relocatable native arena. fp alignment matters mod 4 only (callers
// preserve it, as the test does by sliding); for fp%4 == 1 the aligned
// word starts 1 byte below fp (game frame interior — native callers
// must map a scratch byte there too).

#include "gt2/types.h"

typedef enum {
    GT2_RENDER_OK = 0,
    GT2_RENDER_ERR_INVAL,
    GT2_RENDER_ERR_OOB,
} gt2_render_status_t;

const char *gt2_render_strerror(gt2_render_status_t st);

// Append one B packet (8 B). top_io is the bump offset; *pkt_out is the
// returned fill pointer (old+4). Needs 8 B at top and 8 B at fp.
gt2_render_status_t gt2_render_emit_b(u8 *arena, u32 arena_len, u32 arena_addr,
                                      u32 *top_io, u8 *fp, u32 fp_len,
                                      u32 flags, u32 a2in, u8 **pkt_out);

// Append one A packet (8-B header, 0x14 stride). *pkt_out = old+8.
gt2_render_status_t gt2_render_emit_a(u8 *arena, u32 arena_len, u32 arena_addr,
                                      u32 *top_io, u8 *fp, u32 fp_len,
                                      u32 flags, u32 a2in, u8 **pkt_out);

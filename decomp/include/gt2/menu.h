#pragma once
// GT2 native port: menu-record lifecycle (gt2_02 overlay, VRAM 0x80016xxx).
//
// The garage/menu tick drives per-frame records through three routines
// (behavior reference confirmed by emulation, see docs/menu_notes.md and
// tools/menu_tick.py):
//
// - record_init (0x80016394, 12 insns): copies 4 fields from a source
//   template (byte +0, half +2, byte +1 -> +8), parks the counter at -1
//   (idle) and the scale at 0x80. All other bytes are left untouched.
// - counter_bump (0x800163C8, ~1/frame in menu flows): advances the
//   saturating 0..12 frame counter at +0x10 (-1 sticks = idle; values
//   below -1 climb toward -1; above 12 clamps to 12; 32767 wraps to
//   -32768 which then sticks below range... rather climbs: -32768 < -1
//   so it increments; all verified against emulation).
// - emit_tick (0x80016410, 0x59C bytes): fixed-point ease head (signed
//   divide-by-3 via 0x2AAAAAAB mult-high magic) + packet-emit stanzas.
//   Flag byte +0 bits select stanzas (bit2: first pair; bit1: full vs
//   shortcut stanza; bit0: continue vs tail). The three SCUS callees are
//   injected: emit_A (0x80081478) / emit_B (0x8007DA44) GPU-packet appends
//   (each returns a 12-byte-fillable packet pointer) and the short-path
//   pair caller emit_S (0x8006B61C, receives a stack-built cell).
//
// The wrapper 0x8001636C (inflate-then-index pair via gzSetup 0x80082FAC)
// is NOT ported: needs SCUS gzip bytes + 0x7250/0x3D60 stack frames;
// documented gap, same class as the b2 async DMA (data in, timing out).

#include "gt2/types.h"

typedef enum {
    GT2_MENU_OK = 0,
    GT2_MENU_ERR_INVAL,
} gt2_menu_status_t;

const char *gt2_menu_strerror(gt2_menu_status_t st);

// Menu record (0x14 bytes; game layout, holes preserved by init).
typedef struct {
    u8 b0;        // +0x00 flag byte (bit6 set in practice; bits 0..2 select)
    u8 b1;        // +0x01 untouched by init
    s16 h2;       // +0x02 blend source (from template +2)
    s16 h4;       // +0x04 ease base (untouched by init)
    s16 h6;       // +0x06 ease base (untouched by init)
    u8 b8;        // +0x08 blend base (from template +1)
    u8 _p09;      // +0x09 hole
    u16 _p0A;     // +0x0A hole
    u32 list;     // +0x0C list-block guest address (opaque cookie: the tick
                  //   compares the counter against it for the idle check,
                  //   but dereferences the native copy below)
    s16 cnt;      // +0x10 frame counter (-1 idle, 0..12 active)
    u16 x12;      // +0x12 scale (0x80 from init)
} gt2_menu_rec_t;

// List block behind +0x0C (10 bytes the tick reads).
typedef struct {
    u32 addr;     // guest address cookie (compared with cnt, never deref'd)
    u32 w0;       // +0x00 copied into every packet fill
    u32 w4;       // +0x04 copied into every packet fill
    u16 h4;       // +0x04 half view (ease math, zero-extended)
    u16 h6;       // +0x06 half view (ease math, zero-extended)
    u16 h8;       // +0x08 half view (emit_B flag base)
} gt2_menu_list_t;

// Short-path cell (stack block at sp+0x10 handed to emit_S).
typedef struct {
    s16 h10, h12, h14, h16;
    u32 w18, w1c;
} gt2_menu_cell_t;

// Packet emitters: fp is the caller's opaque frame block, flags is the
// composed flag word; returns a 12-byte-fillable packet pointer.
typedef u8 *(*gt2_menu_emit_fn)(u8 *fp, u32 flags, void *ctx);
// Short-path pair caller: cell is the stack-built block (valid for the
// call only).
typedef void (*gt2_menu_short_fn)(u8 *fp, const gt2_menu_cell_t *cell,
                                  void *ctx);

gt2_menu_status_t gt2_menu_record_init(gt2_menu_rec_t *dst,
                                       const u8 tpl[16]);
gt2_menu_status_t gt2_menu_counter_bump(gt2_menu_rec_t *rec);

// One tick. rec is read-only; list carries the dereferenceable block;
// t4gate is the +0x60(sp) gate (game: incoming a2). Returns the last
// emitter's packet pointer, or NULL when nothing emitted. All arithmetic
// is bit-exact (wrapping u32 + signed-mult-high magic, no float).
u8 *gt2_menu_emit_tick(const gt2_menu_rec_t *rec, u8 *fp, u32 t4gate,
                       const gt2_menu_list_t *list,
                       gt2_menu_emit_fn emit_a, gt2_menu_emit_fn emit_b,
                       gt2_menu_short_fn emit_s, void *ctx);

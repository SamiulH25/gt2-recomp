#pragma once
// GT2 native port: PSX TIM image + logo container + TXD string dictionary.
//
// TIM (standard Sony format, all LE; verified on arc_topmenu's 12 chained
// 16-bit images, US 1.2 sim):
//   u32 magic 0x10; u32 flag (bits0-2: 0=4bit 1=8bit 2=16bit 3=24bit,
//   bit3 = CLUT present); optional CLUT block {u32 len; u16 x,y,w,h;
//   colors}; image block {u32 len; u16 x,y,w,h; pixels}. For 4/8-bit modes
//   w is in 16-bit units (pixels/4 and pixels/2); h is always pixels.
//   15-bit pixels expand r=(v&31)*255/31 etc. (STP bit dropped).
//
// Logo containers (/carlogo/*.tim, e.g. a-a7rl--.tim): an opaque head
// (5184 B for a-a7rl — possibly 72x72 indexed pixels, UNVERIFIED) plus a
// trailing CLUT TIM whose declared image length can exceed the file size
// (a-a7rl: 62x56 declared vs 1128 B available). Pixel decoding waits for
// the overlay consumer (carlogo 0x80011820 annotates, upload path is
// overlay-side); gt2_logo_find_tim only LOCATES the TIM strictly.
// champtim.tim (16532 B: 32 zero bytes + 16500 B payload, no TIM magic
// anywhere) is likewise documented-not-decoded (dims need consumer RE).
//
// TXD (/.text/data-race.txd, 44983 B, 2293 strings): flat NUL-separated
// C strings ("\%dLaps", "Wrong Way", ...) with variable NUL padding
// between them (no alignment, no header). The game must index by runtime
// scan; gt2_txd_count/get do the same over the non-empty runs.
//
// Provenance: bytes measured from the US 1.2 sim disc; no game
// disassembly was needed (format-level work).

#include "gt2/types.h"

typedef enum {
    GT2_TIM_OK = 0,
    GT2_TIM_ERR_IO,
    GT2_TIM_ERR_MAGIC,
    GT2_TIM_ERR_MODE,
    GT2_TIM_ERR_TRUNCATED,
    GT2_TIM_ERR_NO_MEM,
    GT2_TIM_ERR_NOT_FOUND,
    GT2_TIM_ERR_INVAL,
} gt2_tim_status_t;

const char *gt2_tim_strerror(gt2_tim_status_t st);

// One TIM image inside a (possibly chained) slice.
typedef struct {
    u32 mode;        // 0=4bit 1=8bit 2=16bit 3=24bit
    u32 has_clut;
    u16 clut_x, clut_y, clut_w, clut_h;   // valid iff has_clut
    u32 clut_colors;      // (clut_len-12)/2
    u16 orgx, orgy, w, h; // w: pixels for 16/24-bit, 16-bit units for 4/8-bit
    u32 pix_off;     // slice-relative offset of pixel bytes
    u32 pix_len;
    u32 clut_off;    // slice-relative offset of CLUT colors (0 if none)
    u32 total_len;   // whole TIM block incl. headers
} gt2_tim_info_t;

// Count chained TIMs in [data, data+len). Stops at the first invalid
// header (that tail is not an error — it means "no more TIMs").
gt2_tim_status_t gt2_tim_count(const u8 *data, u32 len, u32 *n_out);

// Describe the index-th TIM (0-based).
gt2_tim_status_t gt2_tim_info(const u8 *data, u32 len, u32 index,
                              gt2_tim_info_t *out);

// Pixel width in real pixels (w/4 for 4-bit, w/2 for 8-bit, w else).
u32 gt2_tim_pix_w(const gt2_tim_info_t *info);

// Decode to 24-bit RGB (malloc'd w*h*3, row-major top-down as stored).
// 4/8-bit go through the TIM's own CLUT; 15-bit STP is dropped.
gt2_tim_status_t gt2_tim_decode(const u8 *data, u32 len, u32 index,
                                u8 **rgb_out, u32 *w_out, u32 *h_out);

// Re-emit a TIM block from parsed pieces (round-trip: encode(info as
// parsed, clut+pixel bytes) == original slice). Caller frees.
gt2_tim_status_t gt2_tim_encode(const gt2_tim_info_t *info,
                                const u8 *clut, const u8 *pix,
                                u8 **out, u32 *len_out);

// Strict TIM-header validation at data+off (for chain walking AND logo
// locating): magic, flag<=0xF, mode<=3, headers in bounds, dims sane
// (w,h <= 1024, nonzero), CLUT colors 1..256. Image end MAY exceed len
// (logo containers declare past EOF); pass allow_overshoot=0 for strict
// chain walking, 1 for locating.
gt2_tim_status_t gt2_tim_validate(const u8 *data, u32 len, u32 off,
                                  int allow_overshoot,
                                  gt2_tim_info_t *out);

// Inflate concatenated gzip members with inter-member padding (VOL files
// like arc_topmenu chain one gzip member per TIM). Members are located by
// 1f8b magic at 2048-byte-aligned slots; the walk stops at the first slot
// without magic. Concatenated output is malloc'd. Fails closed on a bad
// member (a payload magic colliding at a slot boundary would error rather
// than desync — callers pin counts/sizes).
gt2_tim_status_t gt2_gunzip_join(const u8 *data, u32 len, u8 **out,
                                 u32 *len_out);

// Locate a trailing CLUT TIM in a logo container: first 4-aligned offset
// passing gt2_tim_validate(allow_overshoot=1) with has_clut set.
gt2_tim_status_t gt2_logo_find_tim(const u8 *data, u32 len, u32 *off_out);

// --- TXD string dictionary -------------------------------------------
// Flat NUL-separated C strings with NUL padding runs between them
// (/.text/data-race.txd: 44983 B, 2293 non-empty strings — "%dLaps",
// "Wrong Way", ...). Entries are the non-empty runs; replacements must
// fit inside the original string's span (NUL-pad) so later offsets stay
// stable. Whether the game's reader counts padding as empty strings is
// overlay-consumer knowledge (open).
u32 gt2_txd_count(const u8 *data, u32 len);   // non-empty entries
// i-th entry (0-based): pointer into data + length in bytes (no NUL).
gt2_tim_status_t gt2_txd_get(const u8 *data, u32 len, u32 i,
                             const u8 **str_out, u32 *len_out);

#pragma once
// GT2 native port: GT2.OVL container (overlay members).
//
// On-disc layout (GT2.OVL;1, extent 331), all LE:
//   +0x00 u32 header_size (0x30: first member starts here)
//   per member i in 0..5: u32 comp_size[i]
//   for i in 1..5: u32 member_off[i] (member 0 starts at header_size;
//   member 5 runs to end of file). Each member is one raw gzip stream.
// Behavior reference: SCUS gt2_load_overlay_default (0x8005DA3C) /
// gt2_load_overlay (0x8005DA7C) feed staging at 0x800A8D5C into the
// libpress gunzip chain (setup 0x80082FAC, decompress 0x800847D0) with
// dst VRAM 0x80010000 (boot ovr0 region is reused); overlay entry is the
// task0a stub (0x800100C0) calling 0x80010000. Confirmed by emulation +
// byte-exact member splits (see decomp/docs/overlay_notes.md).

#include "gt2/cd.h"

#define GT2_OVL_MEMBERS 6u
// Runtime load base shared by all members (boot code region is reused)
// and the task0a entry offset inside each decompressed member.
#define GT2_OVL_VRAM_BASE 0x80010000u
#define GT2_OVL_ENTRY_OFF 0xC0u

typedef struct gt2_ovl gt2_ovl_t;

typedef enum {
    GT2_OVL_OK = 0,
    GT2_OVL_ERR_IO,
    GT2_OVL_ERR_NO_MEM,
    GT2_OVL_ERR_NOT_FOUND,  // GT2.OVL;1 missing / bad header
    GT2_OVL_ERR_INVAL,
    GT2_OVL_ERR_CORRUPT,    // inflate failure
} gt2_ovl_status_t;

// Locate GT2.OVL;1 on the image and parse its header.
gt2_ovl_status_t gt2_ovl_open(gt2_cd_t *cd, gt2_ovl_t **out);
void gt2_ovl_close(gt2_ovl_t *ovl);

u32 gt2_ovl_member_count(const gt2_ovl_t *ovl);   // always 6 on retail
// Compressed range of member `idx` (offsets relative to GT2.OVL start).
gt2_ovl_status_t gt2_ovl_member_range(const gt2_ovl_t *ovl, u32 idx,
                                      u32 *comp_off_out, u32 *comp_size_out);
// Inflate member `idx` (malloc'd, caller frees; needs zlib).
gt2_ovl_status_t gt2_ovl_read_member(gt2_ovl_t *ovl, u32 idx,
                                     u8 **data_out, u32 *size_out);

const char *gt2_ovl_strerror(gt2_ovl_status_t st);

#pragma once
// GT2 native port: boot orchestration (storage → volume → overlay).
//
// Native analog of the game's boot path (start → gt2_main → ovr0_task0
// → load_overlay): opens the image once and chains cd/iso/vol/ovl.
// Overlay *execution* stays in the recomp/runtime (see docs); this
// module delivers bytes + load addresses like the staging/VRAM flow.

#include "gt2/cd.h"
#include "gt2/iso.h"
#include "gt2/ovl.h"
#include "gt2/vol.h"

typedef struct gt2_boot gt2_boot_t;

typedef enum {
    GT2_BOOT_OK = 0,
    GT2_BOOT_ERR_IO,
    GT2_BOOT_ERR_NO_MEM,
    GT2_BOOT_ERR_INVAL,
    GT2_BOOT_ERR_BAD_IMAGE,  // ISO/VOL/OVL layer rejected the image
} gt2_boot_status_t;

gt2_boot_status_t gt2_boot_open(const char *image_path, gt2_boot_t **out);
void gt2_boot_close(gt2_boot_t *b);

gt2_cd_t *gt2_boot_cd(gt2_boot_t *b);
gt2_vol_t *gt2_boot_vol(gt2_boot_t *b);
gt2_ovl_t *gt2_boot_ovl(gt2_boot_t *b);

// Inflate overlay member `idx` (malloc'd, caller frees). Native analog
// of staging → VRAM (load base GT2_OVL_VRAM_BASE).
gt2_boot_status_t gt2_boot_load_overlay(gt2_boot_t *b, u32 idx,
                                        u8 **data_out, u32 *size_out);

const char *gt2_boot_strerror(gt2_boot_status_t st);

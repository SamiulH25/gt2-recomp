// GT2 native port: boot orchestration. Chains the storage modules in
// the game's boot order (ISO -> VOL table -> OVL container).

#include "gt2/boot.h"

#include <stdlib.h>

struct gt2_boot {
    gt2_cd_t *cd;
    gt2_vol_t *vol;
    gt2_ovl_t *ovl;
};

const char *gt2_boot_strerror(gt2_boot_status_t st) {
    switch (st) {
    case GT2_BOOT_OK: return "ok";
    case GT2_BOOT_ERR_IO: return "i/o error";
    case GT2_BOOT_ERR_NO_MEM: return "out of memory";
    case GT2_BOOT_ERR_INVAL: return "invalid argument";
    case GT2_BOOT_ERR_BAD_IMAGE: return "bad image";
    default: return "unknown";
    }
}

gt2_boot_status_t gt2_boot_open(const char *image_path, gt2_boot_t **out) {
    if (!image_path || !out)
        return GT2_BOOT_ERR_INVAL;
    *out = NULL;
    gt2_boot_t *b = calloc(1, sizeof *b);
    if (!b)
        return GT2_BOOT_ERR_NO_MEM;
    if (gt2_cd_open(image_path, &b->cd) != GT2_CD_OK) {
        free(b);
        return GT2_BOOT_ERR_IO;
    }
    if (gt2_vol_open(image_path, &b->vol) != GT2_VOL_OK) {
        gt2_boot_close(b);
        return GT2_BOOT_ERR_BAD_IMAGE;
    }
    if (gt2_ovl_open(b->cd, &b->ovl) != GT2_OVL_OK) {
        gt2_boot_close(b);
        return GT2_BOOT_ERR_BAD_IMAGE;
    }
    *out = b;
    return GT2_BOOT_OK;
}

void gt2_boot_close(gt2_boot_t *b) {
    if (!b)
        return;
    // ovl borrows cd; vol owns its own cd handle.
    gt2_ovl_close(b->ovl);
    gt2_vol_close(b->vol);
    gt2_cd_close(b->cd);
    free(b);
}

gt2_cd_t *gt2_boot_cd(gt2_boot_t *b) {
    return b ? b->cd : NULL;
}

gt2_vol_t *gt2_boot_vol(gt2_boot_t *b) {
    return b ? b->vol : NULL;
}

gt2_ovl_t *gt2_boot_ovl(gt2_boot_t *b) {
    return b ? b->ovl : NULL;
}

gt2_boot_status_t gt2_boot_load_overlay(gt2_boot_t *b, u32 idx,
                                        u8 **data_out, u32 *size_out) {
    if (!b || !data_out || idx >= GT2_OVL_MEMBERS)
        return GT2_BOOT_ERR_INVAL;
    *data_out = NULL;
    if (gt2_ovl_read_member(b->ovl, idx, data_out, size_out) != GT2_OVL_OK)
        return GT2_BOOT_ERR_IO;
    return GT2_BOOT_OK;
}

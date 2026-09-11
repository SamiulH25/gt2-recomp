#pragma once
// GT2 native port: ISO9660 file resolution (ISO-level files, not VOL).
//
// Behavior reference: SCUS task082 (0x80011154, path in a0, handle in a1)
// + descent worker task3 (0x8001124C) + strncasecmp matcher task30
// (0x8008D060), all confirmed by emulation (tools/vol_walk.py): records
// are `{u8 len@0; extent@2; size@10; flags@25; u8 namelen@32; name@33}`,
// matched case-insensitively over the full record name (version `;1`
// included), leading '/' consumed. Returns a pointer to the record on
// success, NULL + status flag 3 on miss.

#include "gt2/cd.h"

typedef struct {
    u32 extent_lba;     // data start sector
    u32 size;           // data length in bytes
    u8 flags;           // ISO flags (bit1 = directory)
    u8 namelen;
    char name[256];     // record name as stored (e.g. "GT2.OVL;1")
} gt2_iso_entry_t;

typedef enum {
    GT2_ISO_OK = 0,
    GT2_ISO_ERR_IO,
    GT2_ISO_ERR_NO_MEM,
    GT2_ISO_ERR_NOT_FOUND,
    GT2_ISO_ERR_INVAL,
    GT2_ISO_ERR_IS_DIR,
} gt2_iso_status_t;

// Resolve 'GT2.OVL;1' / '/GT2.VOL;1' (version suffix required, case
// does not matter). Only the root directory is searched: the GT2 disc
// is flat (., .., FAULTY.PSX;1, GT2.OVL;1, GT2.VOL;1).
gt2_iso_status_t gt2_iso_stat(gt2_cd_t *cd, const char *path,
                              gt2_iso_entry_t *out);

// Read a whole ISO-level file (malloc'd, caller frees).
gt2_iso_status_t gt2_iso_read(gt2_cd_t *cd, const char *path,
                              u8 **data_out, u32 *size_out);

const char *gt2_iso_strerror(gt2_iso_status_t st);

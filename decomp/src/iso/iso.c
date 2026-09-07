// GT2 native port: ISO9660 root-directory resolution.
// Written from the measured disc layout; game behavior (task082/task3 +
// strncasecmp matcher, ';1' required, leading '/' skipped) confirmed by
// emulation — see decomp/docs/gtfs_notes.md Q6.

#include "gt2/iso.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GT2_ISO_PVD_LBA 16u
#define GT2_ISO_ROOT_OFF 156u
#define GT2_ISO_MAX_NAME 255u

const char *gt2_iso_strerror(gt2_iso_status_t st) {
    switch (st) {
    case GT2_ISO_OK: return "ok";
    case GT2_ISO_ERR_IO: return "i/o error";
    case GT2_ISO_ERR_NO_MEM: return "out of memory";
    case GT2_ISO_ERR_NOT_FOUND: return "not found";
    case GT2_ISO_ERR_INVAL: return "invalid argument";
    case GT2_ISO_ERR_IS_DIR: return "is a directory";
    default: return "unknown";
    }
}

static int name_equal_ci(const u8 *rec_name, u32 reclen, const char *want) {
    size_t n = strlen(want);
    if (n != reclen)
        return 0;
    for (size_t i = 0; i < n; i++) {
        u8 a = rec_name[i], b = (u8)want[i];
        if (a >= 'a' && a <= 'z')
            a -= 'a' - 'A';
        if (b >= 'a' && b <= 'z')
            b -= 'a' - 'A';
        if (a != b)
            return 0;
    }
    return 1;
}

static gt2_iso_status_t root_dir(gt2_cd_t *cd, u32 *lba_out, u32 *len_out) {
    u8 pvd[GT2_SECTOR_SIZE];
    gt2_cd_status_t st = gt2_cd_read_sector(cd, GT2_ISO_PVD_LBA, pvd);
    if (st != GT2_CD_OK)
        return GT2_ISO_ERR_IO;
    if (memcmp(pvd + 1, "CD001", 5) != 0)
        return GT2_ISO_ERR_IO;
    const u8 *r = pvd + GT2_ISO_ROOT_OFF;
    memcpy(lba_out, r + 2, 4);
    memcpy(len_out, r + 10, 4);
    return GT2_ISO_OK;
}

gt2_iso_status_t gt2_iso_stat(gt2_cd_t *cd, const char *path,
                              gt2_iso_entry_t *out) {
    if (!cd || !path || !out)
        return GT2_ISO_ERR_INVAL;
    while (*path == '/')
        path++;
    size_t want = strlen(path);
    if (want == 0 || want > GT2_ISO_MAX_NAME)
        return GT2_ISO_ERR_INVAL;
    if (strchr(path, '/') != NULL)
        return GT2_ISO_ERR_NOT_FOUND;   // flat root only (measured)

    u32 lba, len;
    gt2_iso_status_t st = root_dir(cd, &lba, &len);
    if (st != GT2_ISO_OK)
        return st;

    u8 sec[GT2_SECTOR_SIZE];
    u32 left = len;
    u32 cur = lba;
    while (left > 0) {
        if (gt2_cd_read_sector(cd, cur, sec) != GT2_CD_OK)
            return GT2_ISO_ERR_IO;
        u32 off = 0;
        while (off < GT2_SECTOR_SIZE) {
            u8 reclen = sec[off];
            if (reclen == 0)
                break;  // padding to sector end
            if (off + reclen > GT2_SECTOR_SIZE || off + 33 > GT2_SECTOR_SIZE)
                return GT2_ISO_ERR_IO;
            u8 nl = sec[off + 32];
            if (nl > 0 && off + 33u + nl <= off + reclen &&
                name_equal_ci(sec + off + 33, nl, path)) {
                memcpy(&out->extent_lba, sec + off + 2, 4);
                memcpy(&out->size, sec + off + 10, 4);
                out->flags = sec[off + 25];
                out->namelen = nl;
                memcpy(out->name, sec + off + 33, nl);
                out->name[nl] = '\0';
                return GT2_ISO_OK;
            }
            off += reclen;
        }
        cur++;
        left = left > GT2_SECTOR_SIZE ? left - GT2_SECTOR_SIZE : 0;
    }
    return GT2_ISO_ERR_NOT_FOUND;
}

gt2_iso_status_t gt2_iso_read(gt2_cd_t *cd, const char *path,
                              u8 **data_out, u32 *size_out) {
    if (!cd || !path || !data_out)
        return GT2_ISO_ERR_INVAL;
    *data_out = NULL;
    gt2_iso_entry_t e;
    gt2_iso_status_t st = gt2_iso_stat(cd, path, &e);
    if (st != GT2_ISO_OK)
        return st;
    if (e.flags & 0x02)
        return GT2_ISO_ERR_IS_DIR;
    u8 *buf = malloc(e.size ? e.size : 1);
    if (!buf)
        return GT2_ISO_ERR_NO_MEM;
    if (e.size > 0 &&
        gt2_cd_pread(cd, e.extent_lba * GT2_SECTOR_SIZE, buf, e.size) != GT2_CD_OK) {
        free(buf);
        return GT2_ISO_ERR_IO;
    }
    *data_out = buf;
    if (size_out)
        *size_out = e.size;
    return GT2_ISO_OK;
}

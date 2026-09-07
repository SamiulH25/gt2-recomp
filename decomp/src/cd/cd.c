// GT2 native port: CD image sector I/O for cooked and raw dumps.
// Raw rule matches tools/extract.py: MODE2 sector, payload at +24.
// Detection rule matches tools/vol_dump.py: a 2352-multiple size that is
// not a 2048 multiple means raw (our discs are single-track starting at
// LBA 0, so no pregap adjustment is needed).

#include "gt2/cd.h"

#include <stdio.h>
#include <stdlib.h>

#define GT2_RAW_SECTOR 2352u
#define GT2_RAW_PAYLOAD_OFF 24u

struct gt2_cd {
    FILE *fp;
    int raw;
};

const char *gt2_cd_strerror(gt2_cd_status_t st) {
    switch (st) {
    case GT2_CD_OK: return "ok";
    case GT2_CD_ERR_IO: return "i/o error";
    case GT2_CD_ERR_NO_MEM: return "out of memory";
    case GT2_CD_ERR_INVAL: return "invalid argument";
    default: return "unknown";
    }
}

gt2_cd_status_t gt2_cd_open(const char *path, gt2_cd_t **out) {
    if (!path || !out)
        return GT2_CD_ERR_INVAL;
    *out = NULL;
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return GT2_CD_ERR_IO;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return GT2_CD_ERR_IO;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return GT2_CD_ERR_IO;
    }
    gt2_cd_t *cd = malloc(sizeof *cd);
    if (!cd) {
        fclose(fp);
        return GT2_CD_ERR_NO_MEM;
    }
    cd->fp = fp;
    cd->raw = (size % GT2_RAW_SECTOR == 0 && size % GT2_SECTOR_SIZE != 0);
    *out = cd;
    return GT2_CD_OK;
}

void gt2_cd_close(gt2_cd_t *cd) {
    if (!cd)
        return;
    if (cd->fp)
        fclose(cd->fp);
    free(cd);
}

int gt2_cd_is_raw(const gt2_cd_t *cd) {
    return cd ? cd->raw : 0;
}

gt2_cd_status_t gt2_cd_read_sector(gt2_cd_t *cd, u32 lba, u8 out[GT2_SECTOR_SIZE]) {
    if (!cd || !out)
        return GT2_CD_ERR_INVAL;
    long file_off = cd->raw
        ? (long)lba * GT2_RAW_SECTOR + GT2_RAW_PAYLOAD_OFF
        : (long)lba * GT2_SECTOR_SIZE;
    if (fseek(cd->fp, file_off, SEEK_SET) != 0)
        return GT2_CD_ERR_IO;
    if (fread(out, 1, GT2_SECTOR_SIZE, cd->fp) != GT2_SECTOR_SIZE)
        return GT2_CD_ERR_IO;
    return GT2_CD_OK;
}

gt2_cd_status_t gt2_cd_pread(gt2_cd_t *cd, u32 off, void *buf, u32 len) {
    if (!cd || (!buf && len))
        return GT2_CD_ERR_INVAL;
    u8 *dst = buf;
    while (len > 0) {
        u32 lba = off / GT2_SECTOR_SIZE;
        u32 intra = off % GT2_SECTOR_SIZE;
        u8 sec[GT2_SECTOR_SIZE];
        gt2_cd_status_t st = gt2_cd_read_sector(cd, lba, sec);
        if (st != GT2_CD_OK)
            return st;
        u32 take = GT2_SECTOR_SIZE - intra;
        if (take > len)
            take = len;
        for (u32 i = 0; i < take; i++)
            dst[i] = sec[intra + i];
        dst += take;
        off += take;
        len -= take;
    }
    return GT2_CD_OK;
}

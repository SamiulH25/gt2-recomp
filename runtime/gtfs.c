#include "gtfs.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// GTFS: BASE = LBA 473*2048 inside ISO
// tbl at BASE+0x10, dir at BASE+0xBE7C, 32-byte fixed entries
// Verified with tools/gtfs_extract.py

#define GTFS_MAGIC 0x53465447 // "GTFS" LE
static uint32_t *tbl = NULL;
static size_t tbl_count = 0;
static FILE *vol = NULL;
static long vol_base = 0; // offset of GT2.VOL start inside ISO

typedef struct {
    uint32_t hash;
    uint16_t idx;
    char name[24];
} GtfsEntry;
static GtfsEntry *dir = NULL;
static size_t dir_count = 0;

int gtfs_init(const char *iso_path) {
    vol = fopen(iso_path, "rb");
    if (!vol) { perror("fopen iso"); return -1; }
    // find GTFS magic at LBA 473
    long base = 473L*2048L;
    vol_base = base;
    fseek(vol, base, SEEK_SET);
    uint32_t magic;
    fread(&magic, 4, 1, vol);
    if (magic != GTFS_MAGIC) { fprintf(stderr,"bad magic %08x\n",magic); return -1; }
    // load tbl - goes from BASE+0x10 to 0xBE7C (~0xBC6C bytes = ~12000 entries)
    fseek(vol, base+0x10, SEEK_SET);
    size_t tbl_bytes = 0xBE7C - 0x10; // 48236
    tbl = calloc(tbl_bytes/4 + 16, 4);
    size_t cap = tbl_bytes/4;
    for (size_t i=0;i<cap;i++) {
        uint32_t v;
        if (fread(&v,4,1,vol)!=1) break;
        tbl[i]=v;
        tbl_count=i+1;
        if (v > 600*1024*1024) { tbl_count=i; break; }
    }
    printf("[gtfs] tbl_count %zu tbl[0]=%08x tbl[36]=%08x tbl[2047]=%08x\n", tbl_count, tbl[0], tbl[36], tbl_count>2047?tbl[2047]:0);

    // load dir at 0xBE7C - 11568 entries observed, 32-byte fixed
    fseek(vol, base+0xBE7C, SEEK_SET);
    dir = calloc(13000, sizeof(GtfsEntry));
    for (size_t i=0;i<13000;i++) {
        uint8_t rec[32];
        if (fread(rec,32,1,vol)!=1) break;
        // check for all zeros terminator
        int allzero=1; for(int k=0;k<32;k++) if(rec[k]!=0) {allzero=0; break;}
        if(allzero) break;
        uint32_t h; memcpy(&h, rec+4,4);
        uint16_t idx; memcpy(&idx, rec+8,2);
        // filename at 11, up to 21 bytes
        char fname[24]={0};
        memcpy(fname, rec+11, 21);
        fname[21]=0;
        // find null
        size_t flen=strnlen(fname,21);
        if(flen==0) continue;
        if(idx >= tbl_count) continue;
        dir[dir_count].hash=h;
        dir[dir_count].idx=idx;
        strncpy(dir[dir_count].name, fname, 23);
        dir_count++;
    }
    printf("[gtfs] dir_count %zu e.g. %s idx %u, last %s idx %u\n", dir_count, dir_count?dir[0].name:"-", dir_count?dir[0].idx:0, dir_count?dir[dir_count-1].name:"-", dir_count?dir[dir_count-1].idx:0);
    return 0;
}

const char* gtfs_find(const char *name, uint32_t *out_off, uint32_t *out_size) {
    for (size_t i=0;i<dir_count;i++) if (strcmp(dir[i].name, name)==0) {
        uint16_t idx=dir[i].idx;
        uint32_t start=tbl[idx];
        uint32_t end=(idx+1 < tbl_count) ? tbl[idx+1] : 0;
        if (out_off) *out_off = start;
        if (out_size) *out_size = end ? end-start : 0;
        return dir[i].name;
    }
    return NULL;
}

size_t gtfs_read(const char *name, uint8_t **out) {
    uint32_t off, sz;
    if (!gtfs_find(name, &off, &sz)) return 0;
    fseek(vol, vol_base+off, SEEK_SET);
    uint8_t *buf=malloc(sz);
    fread(buf,1,sz,vol);
    *out=buf;
    return sz;
}

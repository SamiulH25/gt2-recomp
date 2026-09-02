// GT2 Recomp - hybrid runtime smoke test
#include <stdio.h>
#include <stdint.h>
#include "gtfs.h"
#include <stdlib.h>
uint8_t* overlay_decompress(const char *path, size_t *out_size);
void overlay_load(const char *path, uint32_t va);

int main(int argc, char **argv) {
    const char *iso = "/tmp/opencode/gt2.iso";
    const char *ovl = "/tmp/opencode/GT2.OVL";
    if (argc>1) iso=argv[1];
    printf("GT2 Recomp - hybrid runtime\n");
    if (gtfs_init(iso)==0) {
        uint32_t off, sz;
        if (gtfs_find("arc_topmenu", &off, &sz))
            printf(" gtfs: arc_topmenu off %08x size %u\n", off, sz);
        uint8_t *buf=NULL;
        size_t n=gtfs_read("champtim.tim", &buf);
        printf(" gtfs_read champtim.tim %zu bytes head %02x%02x%02x%02x\n", n, buf?buf[0]:0,buf?buf[1]:0,buf?buf[2]:0,buf?buf[3]:0);
        if(buf) free(buf);
    }
    size_t osz; uint8_t *ovl_data=overlay_decompress(ovl, &osz);
    if(ovl_data) {
        printf(" ovl decompressed %zu at 0x80100000? first ins %02x%02x%02x%02x\n", osz, ovl_data[0],ovl_data[1],ovl_data[2],ovl_data[3]);
        free(ovl_data);
    }
    overlay_load(ovl, 0x80100000);
    return 0;
}

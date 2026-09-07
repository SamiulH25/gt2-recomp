// path_probe: resolve hierarchical paths with gt2_vol, print machine-readable.
// Usage: path_probe <iso> <path>...  ->  one line per path:
//   OK <slot> <next> <flags> <date> <off> <size> <name>
//   ERR <code> <path>
#include "gt2/vol.h"

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: path_probe <iso> <path>...\n");
        return 2;
    }
    gt2_vol_t *vol = NULL;
    gt2_vol_status_t st = gt2_vol_open(argv[1], &vol);
    if (st != GT2_VOL_OK) {
        fprintf(stderr, "open: %s\n", gt2_vol_strerror(st));
        return 1;
    }
    for (int i = 2; i < argc; i++) {
        gt2_vol_entry_t e;
        st = gt2_vol_stat_path(vol, argv[i], &e);
        if (st != GT2_VOL_OK) {
            printf("ERR %d %s\n", st, argv[i]);
            continue;
        }
        u32 off = 0, size = 0;
        if (!(e.flags & GT2_VOL_FLAG_DIR))
            gt2_vol_file_range(vol, e.next, &off, &size);
        printf("OK %u %u %u %u %u %u %s\n", e.slot, e.next, e.flags,
               e.date, off, size, e.name);
    }
    gt2_vol_close(vol);
    return 0;
}

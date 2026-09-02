// GT2 Recomp - entry stub
#include <stdio.h>

// Recompiled SCUS entry: void scus_entry(void) @ 8005D600
// Generated files will declare: void recomp_8005D600(void);

int main(int argc, char **argv) {
    printf("GT2 Recomp - hybrid runtime\n");
    printf("  Run: python3 tools/extract.py && python3 tools/disasm.py\n");
    printf("  Next: resolve OVL load VA, run psx-recomp, implement runtime/gtfs.c\n");
    // TODO: init HLE, map GT2.VOL via gtfs_init("/tmp/opencode/gt2.iso"), load OVL, call entry
    return 0;
}

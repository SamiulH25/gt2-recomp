// gt2_vol host tests. Needs a cooked 2048-B/sector ISO via GT2_ISO
// (default /tmp/opencode/gt2.iso). Prints SKIP and exits 0 when the image
// is absent so plain builds never break. All expectations below were
// measured from the US 1.2 sim disc (see decomp/docs/gtfs_notes.md).

#include "gt2/vol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
        failures++; \
    } \
} while (0)

static void count_names(const char *name, u32 index, void *ctx) {
    (void)name;
    (void)index;
    (*(u32 *)ctx)++;
}

struct dir_count {
    u32 n;
    u32 ends;
    u8 saw_dotdot;
};

static void count_dir(const gt2_vol_entry_t *en, void *ctx) {
    struct dir_count *c = ctx;
    c->n++;
    if (en->flags & GT2_VOL_FLAG_END)
        c->ends++;
    if (!strcmp(en->name, ".."))
        c->saw_dotdot = 1;
}

int main(void) {
    const char *iso = getenv("GT2_ISO");
    if (!iso || !iso[0])
        iso = "/tmp/opencode/gt2.iso";

    gt2_vol_t *vol = NULL;
    gt2_vol_status_t st = gt2_vol_open(iso, &vol);
    if (st == GT2_VOL_ERR_IO) {
        printf("SKIP: cannot open '%s' (%s)\n", iso, gt2_vol_strerror(st));
        return 0;
    }
    CHECK(st == GT2_VOL_OK, "open: %s", gt2_vol_strerror(st));
    if (!vol)
        return 1;

    CHECK(gt2_vol_file_count(vol) == 11581, "file_count=%u want 11581",
          gt2_vol_file_count(vol));
    CHECK(gt2_vol_name_count(vol) == 11568, "name_count=%u want 11568",
          gt2_vol_name_count(vol));

    // arc_topmenu: known gzip, 118784 bytes (docs/GTFS.md).
    u32 off = 0, size = 0;
    CHECK(gt2_vol_find(vol, "arc_topmenu", &off, &size) == GT2_VOL_OK,
          "find arc_topmenu");
    CHECK(size == 118784, "arc_topmenu size=%u want 118784", size);
    u8 *data = NULL;
    u32 n = 0;
    CHECK(gt2_vol_read(vol, "arc_topmenu", &data, &n) == GT2_VOL_OK,
          "read arc_topmenu");
    CHECK(n == 118784 && data && data[0] == 0x1F && data[1] == 0x8B,
          "arc_topmenu head=%02x%02x want 1f8b",
          data ? data[0] : 0, data ? data[1] : 0);
    free(data);
    data = NULL;

    // file_range(36) must agree with the name lookup.
    u32 off2 = 0, size2 = 0;
    CHECK(gt2_vol_file_range(vol, 36, &off2, &size2) == GT2_VOL_OK,
          "file_range(36)");
    CHECK(off2 == off && size2 == size, "range mismatch %08x/%u vs %08x/%u",
          off2, size2, off, size);

    // Other spot checks measured from the disc.
    CHECK(gt2_vol_find(vol, "champtim.tim", NULL, &size) == GT2_VOL_OK &&
          size == 16532, "champtim.tim size=%u want 16532", size);
    CHECK(gt2_vol_find(vol, "course_mapinfo", NULL, &size) == GT2_VOL_OK &&
          size == 2063, "course_mapinfo size=%u want 2063", size);

    // Missing names and bad indices.
    CHECK(gt2_vol_find(vol, "no_such_file_xyz", NULL, NULL) ==
          GT2_VOL_ERR_NOT_FOUND, "missing name must be NOT_FOUND");
    CHECK(gt2_vol_file_range(vol, 11581, NULL, NULL) == GT2_VOL_ERR_INVAL,
          "index == count must be INVAL");
    CHECK(gt2_vol_find(vol, "", NULL, NULL) == GT2_VOL_ERR_INVAL,
          "empty name must be INVAL");

    u32 visited = 0;
    gt2_vol_visit_names(vol, count_names, &visited);
    CHECK(visited == gt2_vol_name_count(vol), "visit count %u vs %u",
          visited, gt2_vol_name_count(vol));

    // --- hierarchical paths (unified slot table) ---
    CHECK(gt2_vol_slot_count(vol) == 11620, "slot_count=%u want 11620",
          gt2_vol_slot_count(vol));

    gt2_vol_entry_t e;
    CHECK(gt2_vol_stat_path(vol, "/arcade/arc_carlogo", &e) == GT2_VOL_OK,
          "stat arc_carlogo");
    CHECK(e.next == 15 && e.slot == 31 && !(e.flags & GT2_VOL_FLAG_DIR),
          "arc_carlogo slot=%u next=%u flags=%02x", e.slot, e.next, e.flags);
    CHECK(gt2_vol_find_path(vol, "/arcade/arc_carlogo", &off, &size) ==
          GT2_VOL_OK && off == 473u * 2048u + 0xEF824u && size == 345948,
          "arc_carlogo range %08x/%u", off, size);

    // leading '/' optional; root dot-files resolve
    CHECK(gt2_vol_find_path(vol, ".carcolor", &off, &size) == GT2_VOL_OK &&
          off == 473u * 2048u + 0x66B22u && size == 12342,
          ".carcolor range %08x/%u", off, size);
    // deep + non-RAM-resident in the game, plain disc read here
    CHECK(gt2_vol_find_path(vol, "/gtmenu/usa/solodata.dat.gz", &off,
                            &size) == GT2_VOL_OK && size == 3616,
          "solodata size=%u want 3616", size);
    CHECK(gt2_vol_find_path(vol, "/sound/spu_10.seq", &off, &size) ==
          GT2_VOL_OK && size == 5486, "spu_10 size=%u want 5486", size);
    CHECK(gt2_vol_find_path(vol, "/.text/data-race.txd", &off, &size) ==
          GT2_VOL_OK && size == 44983, "data-race size=%u want 44983",
          size);
    // dual reachability: tree path and flat name agree
    u32 foff = 0, fsize = 0;
    CHECK(gt2_vol_find_path(vol, "/arcade/champtim.tim", &off, &size) ==
          GT2_VOL_OK &&
          gt2_vol_find(vol, "champtim.tim", &foff, &fsize) == GT2_VOL_OK &&
          off == foff && size == fsize, "dual lookup mismatch");

    // errors
    CHECK(gt2_vol_stat_path(vol, "/arcade", &e) == GT2_VOL_OK &&
          (e.flags & GT2_VOL_FLAG_DIR), "arcade must stat as dir");
    CHECK(gt2_vol_find_path(vol, "/arcade", NULL, NULL) ==
          GT2_VOL_ERR_IS_DIR, "dir data read must be IS_DIR");
    CHECK(gt2_vol_find_path(vol, "/arcade/nope", NULL, NULL) ==
          GT2_VOL_ERR_NOT_FOUND, "bad leaf must be NOT_FOUND");
    CHECK(gt2_vol_find_path(vol, "/nope", NULL, NULL) ==
          GT2_VOL_ERR_NOT_FOUND, "bad root must be NOT_FOUND");
    CHECK(gt2_vol_find_path(vol, "", NULL, NULL) == GT2_VOL_ERR_INVAL,
          "empty path must be INVAL");
    CHECK(gt2_vol_find_path(vol, "/", NULL, NULL) == GT2_VOL_ERR_INVAL,
          "bare slash must be INVAL");
    CHECK(gt2_vol_find_path(vol, "/arcade/", NULL, NULL) ==
          GT2_VOL_ERR_INVAL, "trailing slash must be INVAL");
    CHECK(gt2_vol_find_path(vol, "/arc_carlogo/more", NULL, NULL) ==
          GT2_VOL_ERR_NOT_FOUND, "descend-into-file must be NOT_FOUND");

    // directory listings
    struct dir_count arc = { 0, 0, 0 }, root = { 0, 0, 0 };
    CHECK(gt2_vol_list_dir(vol, "/arcade", count_dir, &arc) == GT2_VOL_OK &&
          arc.n == 67 && arc.ends == 1 && arc.saw_dotdot,
          "arcade list n=%u ends=%u dotdot=%u", arc.n, arc.ends,
          arc.saw_dotdot);
    CHECK(gt2_vol_list_dir(vol, "/", count_dir, &root) == GT2_VOL_OK &&
          root.n == 28 && root.ends == 1, "root list n=%u ends=%u",
          root.n, root.ends);
    CHECK(gt2_vol_list_dir(vol, "/arcade/arc_carlogo", count_dir, &arc) ==
          GT2_VOL_ERR_IS_DIR, "list file must be IS_DIR");

    // index spans (task0b7 mechanism: last - first - 1)
    u32 span = 0;
    CHECK(gt2_vol_span(vol, "/replay/scea.000", "/replay/scea.999",
                       &span) == GT2_VOL_OK && span == 0,
          "replay span=%u want 0", span);
    CHECK(gt2_vol_span(vol, "/arcade/arc_carlogo", "/arcade/arc_other.tim",
                       &span) == GT2_VOL_OK && span == 11,
          "arcade span=%u want 11", span);
    CHECK(gt2_vol_span(vol, "/arcade/arc_other.tim", "/arcade/arc_carlogo",
                       &span) == GT2_VOL_OK && span == 0xFFFFFFF3u,
          "reversed span=%u", span);
    CHECK(gt2_vol_span(vol, "/arcade/arc_carlogo", "/nope", NULL) ==
          GT2_VOL_ERR_NOT_FOUND, "span missing must be NOT_FOUND");

    gt2_vol_close(vol);
    gt2_vol_close(NULL);    // must be safe

    // Raw 2352 dump must open identically (gt2_vol uses gt2_cd inside).
    const char *raw = getenv("GT2_RAW_BIN");
    if (!raw || !raw[0])
        raw = "Gran Turismo 2 (USA) (Simulation Mode) (v1.2)/"
              "Gran Turismo 2 (USA) (Simulation Mode) (v1.2).bin";
    gt2_vol_t *rvol = NULL;
    if (gt2_vol_open(raw, &rvol) == GT2_VOL_OK) {
        CHECK(gt2_vol_file_count(rvol) == 11581, "raw file_count=%u",
              gt2_vol_file_count(rvol));
        CHECK(gt2_vol_slot_count(rvol) == 11620, "raw slot_count=%u",
              gt2_vol_slot_count(rvol));
        u8 *rdata = NULL;
        u32 rn = 0;
        CHECK(gt2_vol_read_path(rvol, "/arcade/arc_carlogo", &rdata, &rn) ==
              GT2_VOL_OK && rn == 345948, "raw arc_carlogo n=%u", rn);
        free(rdata);
        gt2_vol_close(rvol);
    } else {
        printf("SKIP: cannot open raw '%s'\n", raw);
    }

    if (failures == 0)
        printf("PASS test_vol (files=11581 names=11568 slots=11620 paths ok)\n");
    else
        printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}

// gt2_task host tests: b3 gather/marks/slots/blocks/tails + b4/b5/b6.
// Static gather sources come from disc/SCUS_944.88 (SKIP if absent).
// All expectations measured from the emulated tasks (13115 steps, no
// traps; see decomp/docs/task_notes.md, tools/task_tail.py).

#include "gt2/task.h"

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

// SCUS file offset of a VRAM address (load 0x80010000, file_off 0x800).
#define SCUS_OFF(vram) ((u32)(vram) - 0x80010000u + 0x800u)

static const u8 rec0_head[44] = {
    0x02, 0x03, 0x09, 0x0A, 0x0B, 0x08, 0x0D, 0x05, 0x0C, 0x04, 0x00,
    0x82, 0x82, 0x09, 0x0A, 0x0B, 0x08, 0x0D, 0x05, 0x0C, 0x04, 0x00,
    0x80, 0x80, 0x81, 0x82, 0x0B, 0x08, 0x00, 0x01, 0x0C, 0x04, 0x00,
    0x80, 0x80, 0x09, 0x0A, 0x0B, 0x08, 0x0D, 0x05, 0x0C, 0x04, 0x01,
};

static const u8 rec0_tail[20] = {
    0x40, 0x00, 0x78, 0x00, 0x88, 0x00, 0xC0, 0x00, 0x10, 0x00,
    0xF0, 0x00, 0x10, 0x00, 0xF0, 0x00, 0x10, 0x00, 0xF0, 0x00,
};

int main(void) {
    // Helper-level vectors first (no fixtures needed).
    u8 slot[0x24];
    memset(slot, 0xAA, sizeof slot);
    CHECK(gt2_task_slot_init(slot) == GT2_TASK_OK, "slot_init");
    for (u32 i = 0; i < 4; i++) {
        u32 w;
        memcpy(&w, slot + i * 4, 4);
        CHECK(w == 0xFFFFFFFFu, "slot word %u=%08x", i, w);
    }
    u32 w4;
    memcpy(&w4, slot + 16, 4);
    CHECK(w4 == 0xFFFF0000u, "slot word 4=%08x", w4);
    CHECK(slot[0x10] == 0 && slot[0x11] == 0 && slot[0x12] == 0xFF &&
          slot[0x13] == 0xFF && slot[0x14] == 0xAA, "slot tail");
    CHECK(gt2_task_slot_init(NULL) == GT2_TASK_ERR_INVAL, "slot null");

    u8 blk[0xA4];
    memset(blk, 0xAA, sizeof blk);
    CHECK(gt2_task_block_init(blk) == GT2_TASK_OK, "block_init");
    CHECK(blk[0] == 0 && blk[1] == 0 && blk[2] == 0 && blk[3] == 0xAA,
          "block head");
    CHECK(blk[0x68] == 0 && blk[0x74] == 0 && blk[0x80] == 0 &&
          blk[0x8C] == 0 && blk[0x98] == 0 && blk[0x99] == 0xAA,
          "block tags");

    u8 lst[0xB0];
    memset(lst, 0xAA, sizeof lst);
    CHECK(gt2_task_list_init(lst) == GT2_TASK_OK, "list_init");
    CHECK(lst[0] == 0 && lst[0xA3] == 0 && lst[7] == 0 && lst[8] == 0xFF &&
          lst[0x97] == 0xFF && lst[0x98] == 0 && lst[0xA4] == 0xAA,
          "list words");

    u8 big[0x160];
    memset(big, 0xAA, sizeof big);
    CHECK(gt2_task_big_init(big) == GT2_TASK_OK, "big_init");
    CHECK(big[0] == 0 && big[0x40] == 1 && big[0x41] == 0 &&
          big[0x44] == 0 && big[0x15F] == 0 && big[0x160 - 1] == 0,
          "big body");
    CHECK(big[0x40] == 1, "big flag");

    u8 mk[0x4020];
    memset(mk, 0xAA, sizeof mk);
    CHECK(gt2_task_mark_init(mk) == GT2_TASK_OK, "mark_init");
    CHECK(mk[0] == 0 && mk[1] == 0 && mk[0x4014] == 0x10 &&
          mk[0x4015] == 0x27 && mk[0x4018] == 0xFF &&
          mk[0x4019] == 0xFF && mk[0x401B] == 0 && mk[0x401A] == 0xAA,
          "mark words");

    // b4/b5/b6 vectors.
    u8 dst[0x40];
    s16 bounds[2] = { 0, 0 };
    memset(dst, 0xAA, sizeof dst);
    CHECK(gt2_task_b4(dst, bounds) == GT2_TASK_OK, "b4");
    CHECK(dst[0] == 0 && dst[0x3F] == 0 && bounds[0] == -0x40 &&
          bounds[1] == 0x40, "b4 body");
    CHECK(gt2_task_b4(NULL, bounds) == GT2_TASK_ERR_INVAL, "b4 null");
    u8 cell = 0;
    CHECK(gt2_task_b5(&cell) == GT2_TASK_OK && cell == 0x60, "b5");
    CHECK(gt2_task_b5(NULL) == GT2_TASK_ERR_INVAL, "b5 null");
    u8 s6[0x14];
    memset(s6, 0xAA, sizeof s6);
    CHECK(gt2_task_b6(s6) == GT2_TASK_OK, "b6");
    CHECK(s6[0] == 0 && s6[1] == 0 && s6[2] == 0 && s6[3] == 0xAA &&
          s6[7] == 0xAA && s6[8] == 0 && s6[0xB] == 0 &&
          s6[0xC] == 0xFF && s6[0xD] == 0xFF && s6[0xE] == 0 &&
          s6[0xF] == 0 && s6[0x10] == 0 && s6[0x11] == 0xAA, "b6 body");

    // Full b3 sequence on one game-ordered buffer (0xAA prefill, like the
    // padding around the real RAM region). Needs the SCUS static tables.
    FILE *f = fopen("disc/SCUS_944.88", "rb");
    if (!f) {
        printf("SKIP: no disc/SCUS_944.88\n");
        if (failures == 0)
            printf("PASS test_task (helpers ok, b3 skipped)\n");
        return failures ? 1 : 0;
    }
    u8 srcs[12 + 12 + 12 + 12 + 24];
    const u32 offs[5] = { SCUS_OFF(0x80091570), SCUS_OFF(0x8009157C),
                          SCUS_OFF(0x80091588), SCUS_OFF(0x80091594),
                          SCUS_OFF(0x800A6ED8) };
    const u32 lens[5] = { 11, 11, 11, 11, 20 };
    u32 pos = 0;
    for (u32 i = 0; i < 5; i++) {
        fseek(f, offs[i], SEEK_SET);
        if (fread(srcs + pos, 1, lens[i], f) != lens[i]) {
            fclose(f);
            printf("SKIP: short SCUS read\n");
            return 1;
        }
        pos += lens[i];
    }
    fclose(f);
    gt2_task_gather_src_t gs = { srcs, srcs + 11, srcs + 22, srcs + 33,
                                 srcs + 44 };

    u8 *base = malloc(0xC000);
    CHECK(base != NULL, "oom");
    if (!base)
        return 1;
    memset(base, 0xAA, 0xC000);
    CHECK(gt2_task_b3_gather(base, &gs) == GT2_TASK_OK, "gather");
    CHECK(gt2_task_b3_gather(NULL, &gs) == GT2_TASK_ERR_INVAL,
          "gather null");
    CHECK(gt2_task_b3_gather(base, NULL) == GT2_TASK_ERR_INVAL,
          "gather null src");
    // Record shape: 10 zero bytes, 44 source bytes, 18 zero gap, 20 tail.
    CHECK(!memcmp(base + 0xA, rec0_head, 44), "rec0 head");
    CHECK(!memcmp(base + 0xA + 0x3E, rec0_tail, 20), "rec0 tail");
    for (u32 i = 0; i < 18; i++)
        CHECK(base[0xA + 0x2C + i] == 0, "rec0 gap %u", i);
    CHECK(!memcmp(base + 0xA, base + 0x5C, 0x52), "rec1 == rec0");
    for (u32 i = 0; i < 10; i++)
        CHECK(base[i] == 0, "pre-record %u", i);
    CHECK(base[0xB6] == 0xAA, "memset bound");

    CHECK(gt2_task_b3_marks(base) == GT2_TASK_OK, "marks");
    const u8 markpat[9] = { 1, 0, 0, 2, 1, 0, 2, 0, 1 };
    CHECK(!memcmp(base, markpat, 9), "mark bytes");
    const u8 b2pat[8] = { 0, 0, 0, 1, 1, 0xF0, 0xC0, 1 };
    CHECK(!memcmp(base + 0xAE, b2pat, 8), "mark bytes2");
    for (u32 k = 0; k < 2; k++) {
        u32 o = 0x3C74 + k * 0x4028;
        u32 w;
        memcpy(&w, base + o + 0x4014, 4);
        CHECK(w == 0x2710u, "mark%u word=%08x", k, w);
        CHECK(base[o] == 0 && base[o + 0x4018] == 0xFF &&
              base[o + 0x4019] == 0xFF && base[o + 0x401B] == 0,
              "mark%u halves", k);
    }

    CHECK(gt2_task_b3_slots(base, 126) == GT2_TASK_OK, "slots");
    for (u32 k = 0; k < 126; k += 125) {
        u8 *s = base + 0x218 + k * 0x24;
        u32 w;
        memcpy(&w, s, 4);
        CHECK(w == 0xFFFFFFFFu && s[0x10] == 0 && s[0x11] == 0 &&
              s[0x12] == 0xFF && s[0x14] == 0xAA, "slot%u", k);
    }

    CHECK(gt2_task_b3_blocks(base) == GT2_TASK_OK, "blocks");
    for (u32 o = 0; o < 6; o += 5) {
        for (u32 i = 0; i < 10; i += 9) {
            u8 *p = base + 0x1418 + o * 0x668 + i * 0xA4;
            CHECK(p[0] == 0 && p[1] == 0 && p[2] == 0 &&
                  p[0x68] == 0, "block%u.%u", o, i);
        }
    }

    CHECK(gt2_task_b3_tails(base) == GT2_TASK_OK, "tails");
    for (u32 k = 0; k < 3; k++) {
        u8 *p = base + 0x3A88 + k * 0xA4;
        u32 w;
        memcpy(&w, p + 8, 4);
        CHECK(p[0] == 0 && w == 0xFFFFFFFFu, "tail%u", k);
    }
    u32 flag;
    memcpy(&flag, base + 0xB8 + 0x40, 4);
    CHECK(flag == 1 && base[0xB8] == 0, "big flag");
    free(base);

    if (failures == 0)
        printf("PASS test_task (b3 full + b4/b5/b6 ok)\n");
    else
        printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}

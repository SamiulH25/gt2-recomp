// gt2_spu host tests: init pattern + unlink mark, matching the emulated
// SCUS 0x80078408 output byte-for-byte.

#include "gt2/spu.h"

#include <stdio.h>
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

// Fake guest RAM for link cookies: cookie = base + offset.
static u8 ram[0x100];
#define RAM_BASE 0x801C0000u
static u32 marked = 0;

static void mark_ram(u32 cookie, void *ctx) {
    (void)ctx;
    marked++;
    ram[cookie - RAM_BASE] = 0xFF;
}

static int spin_count;

static int busy_twice(u32 voice, void *ctx) {
    (void)voice;
    (void)ctx;
    return spin_count++ < 2 * 24;
}

static int busy_always(u32 voice, void *ctx) {
    (void)voice;
    (void)ctx;
    return 1;
}

int main(void) {
    static gt2_spu_voice_t v[GT2_SPU_VOICES];
    memset(v, 0, sizeof v);
    // Poison non-init-managed zones to prove they are preserved.
    for (u32 i = 0; i < GT2_SPU_VOICES; i++) {
        u8 *b = (u8 *)&v[i];
        memset(b + 1, 0xAA, 3);
        memset(b + 0x0C, 0xAA, 8);
        memset(b + 0x18, 0xAA, 16);
    }
    memset(ram, 0, sizeof ram);

    // Stale link on voice 5 (as in the emulation probe).
    v[5].link = RAM_BASE + 0x100 - 0x100 + 0x00; // == RAM_BASE
    ram[0] = 0x07;
    gt2_spu_init_ex(v, mark_ram, NULL);
    CHECK(marked == 1, "marks=%u", marked);
    CHECK(ram[0] == 0xFF, "owner byte=%02x", ram[0]);
    CHECK(v[5].link == 0, "link cleared");

    // Exact init pattern on every voice (emulated output):
    // 00 xx xx xx 00 02 00 01 00*4, rest preserved (0xAA here).
    for (u32 i = 0; i < GT2_SPU_VOICES; i++) {
        u8 *b = (u8 *)&v[i];
        CHECK(b[0] == 0 && b[4] == 0 && b[5] == 2 && b[6] == 0 &&
              b[7] == 1 && b[8] == 0 && b[9] == 0 && b[10] == 0 &&
              b[11] == 0, "voice%u pattern %02x%02x%02x%02x %02x%02x%02x%02x",
              i, b[0], b[4], b[5], b[6], b[7], b[8], b[9], b[10]);
        CHECK(b[1] == 0xAA && b[2] == 0xAA && b[3] == 0xAA,
              "voice%u pad preserved", i);
        for (int k = 0x0C; k < 0x14; k++)
            CHECK(b[k] == 0xAA, "voice%u data+%x preserved", i, k);
        for (int k = 0x18; k < 0x28; k++)
            CHECK(b[k] == 0xAA, "voice%u tail+%x preserved", i, k);
    }
    CHECK(sizeof(gt2_spu_voice_t) == 0x28, "stride=%zu",
          sizeof(gt2_spu_voice_t));

    // NULL-safe + plain init.
    gt2_spu_init(NULL);
    memset(v, 0, sizeof v);
    v[2].link = 0x1234;
    gt2_spu_init(v);
    CHECK(v[2].link == 0 && marked == 1, "plain init clears, no mark");

    // wait_idle: busy twice then idle -> 1; always busy -> timeout 0.
    CHECK(gt2_spu_wait_idle(busy_twice, NULL) == 1, "wait ok");
    CHECK(spin_count == 2 * 24 + 24, "spins=%d", spin_count);
    CHECK(gt2_spu_wait_idle(busy_always, NULL) == 0, "wait timeout");
    CHECK(gt2_spu_wait_idle(NULL, NULL) == 1, "wait null");

    if (failures == 0)
        printf("PASS test_spu (24 voices ok)\n");
    else
        printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}

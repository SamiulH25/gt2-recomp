// gt2_render tests: emit_A/emit_B xcheck vs tools/render_arena.py
// emulation vectors (docs/menu_notes.md "Emitter protocol"). Covers the
// full 4x4 fp/top alignment matrix, flag/link extremes, and the stale-a2
// cases (a2 leaks into the header only when the lwl is narrower than the
// swl, e.g. fp+4). Expected bytes are byte-exact emu ground truth.

#include "gt2/render.h"

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

static int hexeq(const u8 *got, int n, const char *hex) {
    for (int i = 0; i < n; i++) {
        unsigned v = 0;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1)
            return 0;
        if (got[i] != (u8)v)
            return 0;
    }
    return 1;
}

typedef struct {
    char kind;          // 'A' / 'B'
    int fp_off;         // game fp+2 alignment source (2..5)
    int top_off;        // game top alignment source (0..3)
    u32 flags, a2in;
    const char *link;   // 4 link bytes hex
    int ret_off, top1;  // expected ret/top1 relative offsets
    const char *pkt;    // 8 header bytes hex
    const char *fpw;    // 6 fp bytes hex
} vec_t;

static const vec_t VECS[] = {

    { 'B', 2, 0, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0x8, "0000de01333300e1", "00701fadbeef" },
    { 'B', 2, 1, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0x9, "0000de01333300e1", "01701fadbeef" },
    { 'B', 2, 2, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0xa, "0000de01333300e1", "02701fadbeef" },
    { 'B', 2, 3, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0xb, "0000de01333300e1", "03701fadbeef" },
    { 'B', 3, 0, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0x8, "0000de01333300e1", "00701fadbeef" },
    { 'B', 3, 1, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0x9, "0000de01333300e1", "01701fadbeef" },
    { 'B', 3, 2, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0xa, "0000de01333300e1", "02701fadbeef" },
    { 'B', 3, 3, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0xb, "0000de01333300e1", "03701fadbeef" },
    { 'B', 4, 0, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0x8, "aaaade01333300e1", "00001fadbeef" },
    { 'B', 4, 1, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0x9, "aaaade01333300e1", "00001fadbeef" },
    { 'B', 4, 2, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0xa, "0000de01333300e1", "00001fadbeef" },
    { 'B', 4, 3, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0xb, "00aade01333300e1", "00001fadbeef" },
    { 'B', 5, 0, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0x8, "aa00de01333300e1", "00701fadbeef" },
    { 'B', 5, 1, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0x9, "aa00de01333300e1", "00701fadbeef" },
    { 'B', 5, 2, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0xa, "0000de01333300e1", "00701fadbeef" },
    { 'B', 5, 3, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x4, 0xb, "0000de01333300e1", "00701fadbeef" },
    { 'B', 2, 0, 0x00000000u, 0x00000000u, "00000000", 0x4, 0x8, "00000001000000e1", "00701f000000" },
    { 'B', 2, 0, 0xffffffffu, 0x12345678u, "11223344", 0x4, 0x8, "00001101ffffffff", "00701f223344" },
    { 'B', 2, 0, 0x00003333u, 0xffffff00u, "deadbeef", 0x4, 0x8, "0000de01333300e1", "00701fadbeef" },
    { 'B', 2, 0, 0x00003333u, 0xffffff7fu, "deadbeef", 0x4, 0x8, "0000de01333300e1", "00701fadbeef" },
    { 'A', 2, 0, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x14, "0000de0433330064", "00701fadbeef" },
    { 'A', 2, 1, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x15, "0000de0433330064", "01701fadbeef" },
    { 'A', 2, 2, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x16, "0000de0433330064", "02701fadbeef" },
    { 'A', 2, 3, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x17, "0000de0433330064", "03701fadbeef" },
    { 'A', 3, 0, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x14, "0000de0433330064", "00701fadbeef" },
    { 'A', 3, 1, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x15, "0000de0433330064", "01701fadbeef" },
    { 'A', 3, 2, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x16, "0000de0433330064", "02701fadbeef" },
    { 'A', 3, 3, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x17, "0000de0433330064", "03701fadbeef" },
    { 'A', 4, 0, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x14, "aaaade0433330064", "00001fadbeef" },
    { 'A', 4, 1, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x15, "aaaade0433330064", "00001fadbeef" },
    { 'A', 4, 2, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x16, "0000de0433330064", "00001fadbeef" },
    { 'A', 4, 3, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x17, "00aade0433330064", "00001fadbeef" },
    { 'A', 5, 0, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x14, "aa00de0433330064", "00701fadbeef" },
    { 'A', 5, 1, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x15, "aa00de0433330064", "00701fadbeef" },
    { 'A', 5, 2, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x16, "0000de0433330064", "00701fadbeef" },
    { 'A', 5, 3, 0x00003333u, 0xaaaaaaaau, "deadbeef", 0x8, 0x17, "0000de0433330064", "00701fadbeef" },
    { 'A', 2, 0, 0x00000000u, 0x00000000u, "00000000", 0x8, 0x14, "0000000400000064", "00701f000000" },
    { 'A', 2, 0, 0xffffffffu, 0x12345678u, "11223344", 0x8, 0x14, "00001104ffffff9b", "00701f223344" },
};

#define NARENA 0x100u
#define ARENA_ADDR 0x801F7000u   // mirrors the emu driver setup

int main(void) {
    static union {
        u32 w[16];
        u8 b[64];
    } fpbig;
    static union {
        u32 w[64];
        u8 b[256];
    } arenabig;
    for (unsigned vi = 0; vi < sizeof VECS / sizeof VECS[0]; vi++) {
        const vec_t *v = &VECS[vi];
        // Replicate the game's mod-4 alignments natively: fp base slide
        // k with (k+2)%4 == fp_off%4, arena top = top_off.
        int want = v->fp_off & 3;
        int k = (want - 2) & 3;
        u8 *fp = fpbig.b + k;
        u8 *arena = arenabig.b;
        memset(fpbig.b, 0, sizeof fpbig.b);
        memset(arenabig.b, 0, sizeof arenabig.b);
        u8 link[4];
        for (int b = 0; b < 4; b++) {
            unsigned x = 0;
            sscanf(v->link + 2 * b, "%2x", &x);
            link[b] = (u8)x;
        }
        memcpy(fp + 2, link, 4);
        u32 top = (u32)v->top_off;
        u8 *pkt = NULL;
        gt2_render_status_t st = v->kind == 'A' ?
            gt2_render_emit_a(arena, NARENA, ARENA_ADDR, &top, fp, 8,
                              v->flags, v->a2in, &pkt) :
            gt2_render_emit_b(arena, NARENA, ARENA_ADDR, &top, fp, 8,
                              v->flags, v->a2in, &pkt);
        CHECK(st == GT2_RENDER_OK, "vec %u status %d", vi, st);
        CHECK(top == (u32)v->top1, "vec %u top %u (want %d)", vi, top,
              v->top1);
        CHECK(pkt == arena + v->top_off + (v->kind == 'A' ? 8 : 4),
              "vec %u ret", vi);
        CHECK(hexeq(arena + v->top_off, 8, v->pkt), "vec %u pkt", vi);
        CHECK(hexeq(fp, 6, v->fpw), "vec %u fp", vi);
    }

    // OOB + null guards.
    {
        u8 arena[8];
        u8 fp[8];
        u32 top = 4;
        u8 *pkt = NULL;
        memset(arena, 0, sizeof arena);
        memset(fp, 0, sizeof fp);
        CHECK(gt2_render_emit_b(arena, sizeof arena, 0, &top, fp,
                                sizeof fp, 0, 0,
                                &pkt) == GT2_RENDER_ERR_OOB,
              "oob top");
        top = 0;
        CHECK(gt2_render_emit_a(NULL, 8, 0, &top, fp, sizeof fp, 0, 0,
                                &pkt) == GT2_RENDER_ERR_INVAL,
              "null arena");
        CHECK(gt2_render_emit_b(arena, sizeof arena, 0, NULL, fp,
                                sizeof fp, 0, 0,
                                &pkt) == GT2_RENDER_ERR_INVAL,
              "null top");
    }

    if (failures == 0)
        printf("PASS test_render (emit_A/B xcheck ok)\n");
    return failures != 0;
}

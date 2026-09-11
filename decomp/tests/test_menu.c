// gt2_menu tests: record init + counter bump + emit-tick xcheck vs
// tools/menu_tick.py emulation vectors (docs/menu_notes.md). The tick
// vectors below are byte-exact emu ground truth (call sequence, emitter
// flags, 12-B packet fills, short-path cells); any port deviation fails.

#include "gt2/menu.h"

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

// --- native arena-backed emitter stand-ins (mirror the emu hooks) ---

typedef struct {
    char kind;          // 'A' / 'B' / 'S'
    u32 flags;
    u8 pkt[12];
    gt2_menu_cell_t cell;
    int has_cell;
} logged_t;

static logged_t g_log[16];
static int g_nlog;
static u8 g_arena[16 * 0x40];
static int g_aslot;

static void log_reset(void) {
    g_nlog = 0;
    g_aslot = 0;
    memset(g_arena, 0, sizeof g_arena);
    memset(g_log, 0, sizeof g_log);
}

static u8 *t_emit_a(u8 *fp, u32 flags, void *ctx) {
    (void)fp;
    (void)ctx;
    u8 *p = g_arena + g_aslot * 0x40;
    g_aslot++;
    g_log[g_nlog].kind = 'A';
    g_log[g_nlog].flags = flags;
    memcpy(g_log[g_nlog].pkt, p, 12);
    g_log[g_nlog].has_cell = 0;
    g_nlog++;
    return p;
}

static u8 *t_emit_b(u8 *fp, u32 flags, void *ctx) {
    (void)fp;
    (void)ctx;
    u8 *p = g_arena + g_aslot * 0x40;
    g_aslot++;
    g_log[g_nlog].kind = 'B';
    g_log[g_nlog].flags = flags;
    memcpy(g_log[g_nlog].pkt, p, 12);
    g_log[g_nlog].has_cell = 0;
    g_nlog++;
    return p;
}

static void t_emit_s(u8 *fp, const gt2_menu_cell_t *cell, void *ctx) {
    (void)fp;
    (void)ctx;
    g_log[g_nlog].kind = 'S';
    g_log[g_nlog].flags = 0;
    g_log[g_nlog].cell = *cell;
    g_log[g_nlog].has_cell = 1;
    g_nlog++;
}

// hex ("22ef...") -> bytes compare against a packet/cell dump.
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

static gt2_menu_rec_t mkrec(s16 cnt, u8 fl) {
    gt2_menu_rec_t r;
    memset(&r, 0, sizeof r);
    r.b0 = (u8)(0x40u | fl);
    r.h2 = 100;
    r.h4 = 50;
    r.h6 = 60;
    r.b8 = 0x33;
    r.list = 0x801F6000u;
    r.cnt = cnt;
    r.x12 = 0x80u;
    return r;
}

static gt2_menu_list_t mklist(void) {
    gt2_menu_list_t l;
    l.addr = 0x801F6000u;
    l.w0 = 0xAABBCCDDu;
    l.w4 = 0x22221111u;
    l.h4 = 0x1111u;
    l.h6 = 0x2222u;
    l.h8 = 0x3333u;
    return l;
}

// One sweep vector: expected sequences/flags/packets from menu_tick.py.
typedef struct {
    s16 cnt;
    u8 fl;
    u32 t4;
    const char *seq;            // e.g. "ABABB"
    const u32 *flags;           // per call (S: ignored)
    const char **pkts;          // per A/B call, 12-B hex (B fills: zeros)
    const char **cells;         // per S call, 16-B hex (NULL if none)
} tickvec_t;

static const u32 F_00[] = { 0x000c0c0cu, 0x00003333u };
static const char *P_00[] = {
    "22ef2aefddccbbaa11112222", "000000000000000000000000",
};
static const u32 F_33[] = { 0x020c0c0cu, 0x00003333u, 0x02060606u,
                            0x00003373u, 0x00003333u };
static const char *P_33[] = {
    "22ef2aefddccbbaa11112222", "000000000000000000000000",
    "22ef2aefddccbbaa11112222", "000000000000000000000000",
    "000000000000000000000000",
};
static const u32 F_37[] = { 0x02272727u, 0x02272727u, 0x00003333u,
                            0x020c0c0cu, 0x00003333u, 0x02060606u,
                            0x00003373u, 0x00003333u };
static const char *P_37[] = {
    "d7ee2aefddccbbaa11112222", "6def2aefddccbbaa11112222",
    "000000000000000000000000", "22ef2aefddccbbaa11112222",
    "000000000000000000000000", "22ef2aefddccbbaa11112222",
    "000000000000000000000000", "000000000000000000000000",
};
static const u32 F_C0[] = { 0x00333333u, 0x00003333u };
static const char *P_C0[] = {
    "22ef2aefddccbbaa11112222", "000000000000000000000000",
};
static const u32 F_C7[] = { 0x02333333u, 0x00003333u, 0x02191919u,
                            0x00003373u, 0x00003333u };
static const char *P_C7[] = {
    "22ef2aefddccbbaa11112222", "000000000000000000000000",
    "22ef2aefddccbbaa11112222", "000000000000000000000000",
    "000000000000000000000000",
};
static const u32 F_3799[] = { 0x02272727u, 0x02272727u, 0x00003333u,
                              0x020c0c0cu, 0x00003333u, 0x02000000u,
                              0x00003333u, 0x020c0c0cu, 0x00003373u,
                              0x00003333u };
static const char *P_3799[] = {
    "d7ee2aefddccbbaa11112222", "6def2aefddccbbaa11112222",
    "000000000000000000000000", "22ef2aefddccbbaa11112222",
    "000000000000000000000000", "22ef2aefddccbbaa11112222",
    "000000000000000000000000", "22ef2aefddccbbaa11112222",
    "000000000000000000000000", "000000000000000000000000",
};
static const u32 F_05[] = { 0x02333333u, 0x02333333u, 0x00003333u,
                            0x00000000u, 0x00003333u };
static const char *P_05[] = {
    "beee2aefddccbbaa11112222", "86ef2aefddccbbaa11112222",
    "000000000000000000000000", "22ef2aefddccbbaa11112222",
    "000000000000000000000000",
};
static const u32 F_B4[] = { 0x02050505u, 0x02050505u, 0x00003333u,
                            0x002e2e2eu, 0x00003333u };
static const char *P_B4[] = {
    "1aef2aefddccbbaa11112222", "2aef2aefddccbbaa11112222",
    "000000000000000000000000", "22ef2aefddccbbaa11112222",
    "000000000000000000000000",
};
static const char *C_M5[] = {
    "82fa8bfaf205620b0000000022000000",
    "e0ee8bfaf205620b2200000000000000",
};
static const u32 F_M5[] = { 0, 0, 0x20u };
static const char *P_M5[] = { "000000000000000000000000" };

static const u32 F_C0Z[] = { 0x00000000u, 0x00003333u };

static const tickvec_t VECS[] = {
    { -1, 0, 0, "", NULL, NULL, NULL },
    { 0, 0, 0, "AB", F_C0Z, P_00, NULL },
    { 3, 0, 0, "AB", F_00, P_00, NULL },
    { 3, 1, 0, "AB", F_00, P_00, NULL },
    { 3, 3, 0, "ABABB", F_33, P_33, NULL },
    { 3, 7, 0, "AABABABB", F_37, P_37, NULL },
    { 12, 0, 0, "AB", F_C0, P_C0, NULL },
    { -5, 0, 0, "SSB", F_M5, P_M5, C_M5 },
    { 12, 1, 0, "AB", F_C0, P_C0, NULL },
    { 12, 5, 0, "AB", F_C0, P_C0, NULL },
    { 12, 7, 0, "ABABB", F_C7, P_C7, NULL },
    { 3, 7, 0x99, "AABABABABB", F_3799, P_3799, NULL },
    { 3, 1, 0x99, "AB", F_00, P_00, NULL },
    { 0, 5, 0, "AABAB", F_05, P_05, NULL },
    { 11, 4, 0, "AABAB", F_B4, P_B4, NULL },
};

int main(void) {
    // record_init: fields + hole preservation.
    {
        gt2_menu_rec_t r;
        memset(&r, 0xAA, sizeof r);
        const u8 tpl[16] = { 0x11, 0x22, 0x34, 0x12, 0x78, 0x56,
                             0x34, 0x12, 0xAA, 0xBB, 0xCC, 0xDD,
                             0xEE, 0xFF, 0x01, 0x02 };
        CHECK(gt2_menu_record_init(&r, tpl) == GT2_MENU_OK, "init ok");
        CHECK(r.b0 == 0x11 && r.b8 == 0x22, "init bytes %02x %02x",
              r.b0, r.b8);
        CHECK(r.h2 == 0x1234, "init half %d", r.h2);
        CHECK(r.cnt == -1 && r.x12 == 0x80, "init cnt/x12 %d %u",
              r.cnt, r.x12);
        CHECK(r.b1 == 0xAA && r.h4 == (s16)0xAAAA &&
              r.h6 == (s16)0xAAAA, "init holes kept");
        CHECK(r.list == 0xAAAAAAAAu, "init list kept %08x", r.list);
        CHECK(gt2_menu_record_init(NULL, tpl) == GT2_MENU_ERR_INVAL,
              "init null dst");
        CHECK(gt2_menu_record_init(&r, NULL) == GT2_MENU_ERR_INVAL,
              "init null tpl");
    }

    // counter_bump: emu-verified table incl. wrap edges.
    {
        static const s16 in[] = { -3, -2, -1, 0, 1, 5, 11, 12, 13, 100,
                                  32766, 32767, -32768, -32767 };
        static const s16 want[] = { -2, -1, -1, 1, 2, 6, 12, 12, 12, 12,
                                    12, -32768, -32767, -32766 };
        for (unsigned i = 0; i < sizeof in / sizeof in[0]; i++) {
            gt2_menu_rec_t r;
            memset(&r, 0, sizeof r);
            r.cnt = in[i];
            CHECK(gt2_menu_counter_bump(&r) == GT2_MENU_OK, "bump ok");
            CHECK(r.cnt == want[i], "bump %d -> %d (want %d)", in[i],
                  r.cnt, want[i]);
        }
        CHECK(gt2_menu_counter_bump(NULL) == GT2_MENU_ERR_INVAL,
              "bump null");
    }

    // emit_tick xcheck over the whole sweep.
    {
        u8 fp[64];
        memset(fp, 0x55, sizeof fp);
        for (unsigned vi = 0; vi < sizeof VECS / sizeof VECS[0]; vi++) {
            const tickvec_t *v = &VECS[vi];
            gt2_menu_rec_t r = mkrec(v->cnt, v->fl);
            gt2_menu_list_t l = mklist();
            log_reset();
            u8 *ret = gt2_menu_emit_tick(&r, fp, v->t4, &l, t_emit_a,
                                         t_emit_b, t_emit_s, NULL);
            size_t want_n = strlen(v->seq);
            CHECK((size_t)g_nlog == want_n,
                  "vec %d (cnt=%d fl=%u): %d calls, want %zu (%s)", vi,
                  v->cnt, v->fl, g_nlog, want_n, v->seq);
            int si = 0, nAB = 0;
            for (size_t k = 0; k < want_n && (int)k < g_nlog; k++) {
                CHECK(g_log[k].kind == v->seq[k],
                      "vec %d call %zu: %c, want %c", vi, k,
                      g_log[k].kind, v->seq[k]);
                if (g_log[k].kind == 'S') {
                    CHECK(v->cells && v->cells[si] &&
                          hexeq((const u8 *)&g_log[k].cell, 16,
                                v->cells[si]),
                          "vec %d cell %d", vi, si);
                    si++;
                } else {
                    CHECK(v->flags && g_log[k].flags == v->flags[k],
                          "vec %d call %zu: flags %08x, want %08x", vi,
                          k, g_log[k].flags,
                          v->flags ? v->flags[k] : 0);
                    // Arena slot k holds the post-fill bytes (the port,
                    // like the game, fills after the emitter returns).
                    CHECK(v->pkts && hexeq(g_arena + nAB * 0x40, 12,
                                           v->pkts[nAB]),
                          "vec %d arena slot %d", vi, nAB);
                    nAB++;
                }
            }
            // Return = last emitter packet (NULL when idle).
            if (want_n == 0) {
                CHECK(ret == NULL, "vec %d idle ret", vi);
            } else {
                int total = 0;
                for (size_t k = 0; k < want_n; k++)
                    if (v->seq[k] != 'S')
                        total++;
                CHECK(ret == g_arena + (total - 1) * 0x40,
                      "vec %d ret %p", vi, (void *)ret);
            }
        }
    }

    if (failures == 0)
        printf("PASS test_menu (record + bump + tick xcheck ok)\n");
    return failures != 0;
}

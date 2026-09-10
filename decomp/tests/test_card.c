// gt2_card tests: status driver xcheck vs tools/save_state.py emulation
// vectors (docs/save_notes.md). Covers entry counters (+0x14/+0x16/+0x18
// incl. saturate edges), the 0x500 countdown lane, the 0xA00
// memmove/table lane (incl. byte-shift proof), the bit maze, all return
// codes (-3/-2/-1/0), scripted c8fc4/ad3c steering, and OOB refusals.
// The aux const table is read from disc/SCUS_944.88 at runtime (SKIP the
// aux vectors if missing); table[0]==0x20 cross-checks the disc read
// against emu ground truth.

#include "gt2/card.h"

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

// --- scripted card backend (mirror the emu hooks) ---

typedef struct {
    char kinds[20];
    int n;
    u32 op0840_codes[8];
    int nop0840;
    u8 op3524_v[8];
    int nop3524;
    const int *script8;
    int script8_len, script8_i;
    const int *scriptA;
    int scriptA_len, scriptA_i;
} tlog_t;

static tlog_t G;

static void log_reset(const int *s8, int n8, const int *sA, int nA) {
    memset(&G, 0, sizeof G);
    G.script8 = s8;
    G.script8_len = n8;
    G.scriptA = sA;
    G.scriptA_len = nA;
}

static void note(char k) {
    if (G.n < 20)
        G.kinds[G.n++] = k;
}

static void t_cfc4(u8 *blk, void *ctx) {
    (void)blk;
    (void)ctx;
    note('C');
}

static void t_be64(u8 *blk, void *ctx) {
    (void)blk;
    (void)ctx;
    note('B');
}

static void t_da80(u32 w, void *ctx) {
    (void)w;
    (void)ctx;
    note('D');
}

static int t_c8fc4(u8 *data, void *ctx) {
    (void)data;
    (void)ctx;
    note('8');
    int v = G.script8[G.script8_i < G.script8_len ? G.script8_i :
                      G.script8_len - 1];
    G.script8_i++;
    return v;
}

static int t_ad3c(u8 *data, s16 n, void *ctx) {
    (void)data;
    (void)n;
    (void)ctx;
    note('A');
    int v = G.scriptA[G.scriptA_i < G.scriptA_len ? G.scriptA_i :
                      G.scriptA_len - 1];
    G.scriptA_i++;
    return v;
}

static void t_d400(u8 *blk, void *ctx) {
    (void)blk;
    (void)ctx;
    note('4');
}

static void t_op3524(u8 *data, s16 v, void *ctx) {
    (void)data;
    (void)ctx;
    note('2');
    if (G.nop3524 < 8)
        G.op3524_v[G.nop3524++] = (u8)v;
}

static void t_op0840(u32 v, void *ctx) {
    (void)ctx;
    note('0');
    if (G.nop0840 < 8)
        G.op0840_codes[G.nop0840++] = v;
}

static const gt2_card_cb_t CB = { t_cfc4, t_be64, t_da80, t_c8fc4,
                                  t_ad3c, t_d400, t_op3524, t_op0840 };
static const int S0[] = { 0 };

static u8 *mkstate(void) {
    static u8 s[0x100];
    memset(s, 0, sizeof s);
    return s;
}

static u8 *mkdata(void) {
    static u8 d[0x100];
    memset(d, 0, sizeof d);
    return d;
}

static void mkev(u8 *ev, u32 b4, u32 bC) {
    memset(ev, 0, 0x10);
    memcpy(ev + 4, &b4, 4);
    memcpy(ev + 0xC, &bC, 4);
}

static s16 get16(const u8 *p, u32 off) {
    s16 v;
    memcpy(&v, p + off, 2);
    return v;
}

int main(void) {
    // Aux const table (SCUS 0x8009226C window); SKIP aux vectors if the
    // SCUS file is absent (same convention as test_task).
    static u8 aux[0x1000];
    int have_aux = 0;
    {
        FILE *f = fopen("disc/SCUS_944.88", "rb");
        if (f) {
            fseek(f, 0x800 + (0x8009226Cu - 0x80010000u), SEEK_SET);
            have_aux = fread(aux, 1, sizeof aux, f) == sizeof aux;
            fclose(f);
            if (have_aux)
                CHECK(aux[0] == 0x20, "aux[0] %02x (want 20)", aux[0]);
        }
        if (!have_aux)
            printf("SKIP: no SCUS aux table (ad3c/table vectors)\n");
    }

    // Status-bit sweep (zeroed state; ev struct).
    {
        static const struct {
            u32 s1, ev4, evC;
            int rc;
            const char *seq;
            int c0840;   // expected op0840 code, -1 if none
        } vecs[] = {
            { 0, 0, 0, -2, "CBD", -1 },
            { 0, 1, 0, -3, "CBD0", 5 },
            { 0, 0x10, 0, -3, "CBD0", 5 },
            { 0, 0x1000, 0, -3, "CBD80", 5 },
            { 0, 4, 0, -3, "CBD0", 5 },
            { 0, 8, 0, -3, "CBD0", 5 },
            { 0, 0x500, 0, -2, "CBD0", 0 },
            { 0, 0xA00, 0, -2, "CBD80", 2 },
            { 0x101F, 0x101F, 0x101F, -3, "CBD80", 5 },
        };
        // NOTE: nonzero s1-init is NOT natively reachable (entry regs
        // zero, same as emu.call); the emu st1 vector (s1=1, == idle)
        // documents it without a runtime case here.
        for (unsigned i = 0; i < sizeof vecs / sizeof vecs[0]; i++) {
            u8 *st = mkstate();
            u8 *dt = mkdata();
            u8 ev[0x10];
            mkev(ev, vecs[i].ev4, vecs[i].evC);
            int rc = 99;
            log_reset(S0, 1, S0, 1);
            gt2_card_status_t r = gt2_card_run(st, ev, dt, 0x100, aux,
                                               have_aux ? 0x1000u : 0u,
                                               &CB, NULL, &rc);
            CHECK(r == GT2_CARD_OK && rc == vecs[i].rc,
                  "vec %u rc %d (want %d, st %d)", i, rc, vecs[i].rc,
                  r);
            CHECK((int)strlen(vecs[i].seq) == G.n &&
                  memcmp(G.kinds, vecs[i].seq, G.n) == 0,
                  "vec %u seq", i);
            if (vecs[i].c0840 >= 0)
                CHECK(G.nop0840 == 1 &&
                      G.op0840_codes[0] == (u32)vecs[i].c0840,
                      "vec %u op %u", i, G.op0840_codes[0]);
            CHECK(get16(st, 0x14) == 1, "vec %u cnt14", i);
        }
    }

    // State-field sweep (ev 0x101F/0x101F).
    {
        static const struct {
            u8 off;
            u16 val;
            int rc;
            s16 c14;
            u8 b1a, b1b;
            s16 c1c;
        } vecs[] = {
            { 0x14, 5, -3, 6, 0, 0, 0 },       // cnt5
            { 0x1C, 7, -3, 1, 0, 0, 0 },       // h1c (min-clamp to 0)
            { 0x1B, 3, -3, 1, 0, 3, 0 },       // b1b (kept)
            { 0x1A, 0xC, -3, 1, 0, 0, 0 },     // b1a (R7 overwrites)
            { 0x4A, 9, -3, 1, 9, 0, 0 },       // h4a (R7 copies)
        };
        for (unsigned i = 0; i < sizeof vecs / sizeof vecs[0]; i++) {
            u8 *st = mkstate();
            u8 *dt = mkdata();
            u8 ev[0x10];
            mkev(ev, 0x101F, 0x101F);
            st[vecs[i].off] = (u8)vecs[i].val;
            st[vecs[i].off + 1] = (u8)(vecs[i].val >> 8);
            int rc = 99;
            log_reset(S0, 1, S0, 1);
            gt2_card_status_t r = gt2_card_run(st, ev, dt, 0x100, aux,
                                               have_aux ? 0x1000u : 0u,
                                               &CB, NULL, &rc);
            CHECK(r == GT2_CARD_OK && rc == vecs[i].rc,
                  "field vec %u rc %d", i, rc);
            CHECK(G.n == 5 && memcmp(G.kinds, "CBD80", 5) == 0,
                  "field vec %u seq", i);
            CHECK(get16(st, 0x14) == vecs[i].c14 &&
                  st[0x1A] == vecs[i].b1a && st[0x1B] == vecs[i].b1b &&
                  get16(st, 0x1C) == vecs[i].c1c,
                  "field vec %u state", i);
        }
    }

    // a00 +0x1A==0xC lanes (returns -1 / 0, no aux traffic).
    {
        static const struct {
            u8 b1b;
            int rc;
        } vecs[] = { { 3, -1 }, { 9, 0 } };
        for (unsigned i = 0; i < 2; i++) {
            u8 *st = mkstate();
            u8 *dt = mkdata();
            u8 ev[0x10];
            mkev(ev, 0xA00, 0);
            st[0x1A] = 0xC;
            st[0x1B] = vecs[i].b1b;
            int rc = 99;
            log_reset(S0, 1, S0, 1);
            gt2_card_status_t r = gt2_card_run(st, ev, dt, 0x100, aux,
                                               have_aux ? 0x1000u : 0u,
                                               &CB, NULL, &rc);
            CHECK(r == GT2_CARD_OK && rc == vecs[i].rc && G.n == 3 &&
                  memcmp(G.kinds, "CBD", 3) == 0,
                  "a00 lane %u", i);
        }
    }

    // R2 countdown lanes (ev 0x500).
    {
        u8 *st = mkstate();
        u8 *dt = mkdata();
        u8 ev[0x10];
        mkev(ev, 0x500, 0);
        st[0x1C] = 3;
        int rc = 99;
        log_reset(S0, 1, S0, 1);
        CHECK(gt2_card_run(st, ev, dt, 0x100, aux,
                           have_aux ? 0x1000u : 0u, &CB, NULL,
                           &rc) == GT2_CARD_OK && rc == -2 &&
              get16(st, 0x1C) == 2 && G.n == 5 &&
              G.op3524_v[0] == 2 &&
              G.op0840_codes[0] == 2,
              "r2count");
    }
    // Counter edges incl. saturations.
    {
        static const s16 in14[] = { 40, 11, 0 };
        static const s16 want14[] = { 12, 12, 1 };
        for (unsigned i = 0; i < 3; i++) {
            u8 *st = mkstate();
            u8 *dt = mkdata();
            u8 ev[0x10];
            mkev(ev, 0, 0);
            st[0x14] = (u8)in14[i];
            st[0x15] = (u8)((u16)in14[i] >> 8);
            int rc = 99;
            log_reset(S0, 1, S0, 1);
            CHECK(gt2_card_run(st, ev, dt, 0x100, aux,
                               have_aux ? 0x1000u : 0u, &CB, NULL,
                               &rc) == GT2_CARD_OK && rc == -2,
                  "c14 %d", in14[i]);
            CHECK(get16(st, 0x14) == want14[i], "c14 %d -> %d",
                  in14[i], get16(st, 0x14));
        }
        static const s16 in16[] = { 59, 60, 61 };
        static const s16 want16[] = { 60, 0, 0 };
        for (unsigned i = 0; i < 3; i++) {
            u8 *st = mkstate();
            u8 *dt = mkdata();
            u8 ev[0x10];
            mkev(ev, 0, 0);
            st[0x16] = (u8)in16[i];
            st[0x17] = (u8)((u16)in16[i] >> 8);
            int rc = 99;
            log_reset(S0, 1, S0, 1);
            gt2_card_run(st, ev, dt, 0x100, aux,
                         have_aux ? 0x1000u : 0u, &CB, NULL, &rc);
            CHECK(get16(st, 0x16) == want16[i], "c16 %d -> %d",
                  in16[i], get16(st, 0x16));
        }
        static const s16 in18[] = { 16, 40 };
        static const s16 want18[] = { 17, 0 };
        for (unsigned i = 0; i < 2; i++) {
            u8 *st = mkstate();
            u8 *dt = mkdata();
            u8 ev[0x10];
            mkev(ev, 0, 0);
            st[0x18] = (u8)in18[i];
            st[0x19] = (u8)((u16)in18[i] >> 8);
            int rc = 99;
            log_reset(S0, 1, S0, 1);
            gt2_card_run(st, ev, dt, 0x100, aux,
                         have_aux ? 0x1000u : 0u, &CB, NULL, &rc);
            CHECK(get16(st, 0x18) == want18[i], "c18 %d -> %d",
                  in18[i], get16(st, 0x18));
        }
        // cntNeg (-3 -> -2, early exit, 2 calls).
        {
            u8 *st = mkstate();
            u8 *dt = mkdata();
            u8 ev[0x10];
            mkev(ev, 0, 0);
            st[0x14] = 0xFD;
            st[0x15] = 0xFF;
            int rc = 99;
            log_reset(S0, 1, S0, 1);
            CHECK(gt2_card_run(st, ev, dt, 0x100, aux,
                               have_aux ? 0x1000u : 0u, &CB, NULL,
                               &rc) == GT2_CARD_OK && rc == -2 &&
                  G.n == 2 && get16(st, 0x14) == -2,
                  "cntNeg");
        }
        // ev NULL -> early -2 after entry counters.
        {
            u8 *st = mkstate();
            u8 *dt = mkdata();
            int rc = 99;
            log_reset(S0, 1, S0, 1);
            CHECK(gt2_card_run(st, NULL, dt, 0x100, aux,
                               have_aux ? 0x1000u : 0u, &CB, NULL,
                               &rc) == GT2_CARD_OK && rc == -2 &&
                  G.n == 3 && get16(st, 0x14) == 1,
                  "ev null");
        }
    }

    // Card regions (need the aux table for ad3c vectors).
    if (have_aux) {
        // bit10000: D400 block (+0x1A forced 0x0C by the delay slot).
        {
            u8 *st = mkstate();
            u8 *dt = mkdata();
            u8 ev[0x10];
            mkev(ev, 0x10000, 0);
            int rc = 99;
            log_reset(S0, 1, S0, 1);
            CHECK(gt2_card_run(st, ev, dt, 0x100, aux, 0x1000u, &CB,
                               NULL, &rc) == GT2_CARD_OK && rc == -3 &&
                  G.n == 5 && st[0x1A] == 0x0C && st[0x1B] == 8 &&
                  G.op0840_codes[0] == 5,
                  "bit10000");
        }
        // R2 countdown (+0x1C 3->2, op3524(data,2)).
        {
            u8 *st = mkstate();
            u8 *dt = mkdata();
            u8 ev[0x10];
            mkev(ev, 0x500, 0);
            st[0x1C] = 3;
            int rc = 99;
            log_reset(S0, 1, S0, 1);
            CHECK(gt2_card_run(st, ev, dt, 0x100, aux, 0x1000u, &CB,
                               NULL, &rc) == GT2_CARD_OK && rc == -2 &&
                  get16(st, 0x1C) == 2 && G.nop3524 == 1 &&
                  G.op3524_v[0] == 2,
                  "r2count");
        }
        // ad3c-low: memmove + table + R5-entry (bump to 5).
        {
            u8 *st = mkstate();
            u8 *dt = mkdata();
            u8 ev[0x10];
            mkev(ev, 0xA00, 0);
            for (int k = 0; k < 0x20; k++)
                dt[k] = (u8)k;
            st[0x1C] = 4;
            st[0x1E] = 20;
            st[0x20] = 10;
            static const int s8[] = { 9 };
            static const int sA[] = { 3 };
            int rc = 99;
            log_reset(s8, 1, sA, 1);
            gt2_card_status_t r = gt2_card_run(st, ev, dt, 0x100, aux,
                                               0x1000u, &CB, NULL, &rc);
            CHECK(r == GT2_CARD_OK && rc == -2 && G.n == 6,
                  "ad3c-low run");
            CHECK(get16(st, 0x1C) == 5, "ad3c-low bump");
            CHECK(dt[4] == 0x20 && dt[5] == 4 && dt[10] == 9,
                  "ad3c-low shift %02x %02x %02x", dt[4], dt[5],
                  dt[10]);
            CHECK(G.op0840_codes[G.nop0840 - 1] == 1, "ad3c-low op");
        }
        // ad3c-high: 73524 path (no bump).
        {
            u8 *st = mkstate();
            u8 *dt = mkdata();
            u8 ev[0x10];
            mkev(ev, 0xA00, 0);
            for (int k = 0; k < 0x20; k++)
                dt[k] = (u8)k;
            st[0x1C] = 4;
            st[0x1E] = 20;
            st[0x20] = 2;
            static const int s8[] = { 9 };
            static const int sA[] = { 7 };
            int rc = 99;
            log_reset(s8, 1, sA, 1);
            gt2_card_status_t r = gt2_card_run(st, ev, dt, 0x100, aux,
                                               0x1000u, &CB, NULL, &rc);
            CHECK(r == GT2_CARD_OK && rc == -2 && G.n == 7,
                  "ad3c-high run");
            CHECK(get16(st, 0x1C) == 4 && G.op3524_v[0] == 4 &&
                  G.op0840_codes[G.nop0840 - 1] == 0,
                  "ad3c-high vals");
        }
        // +0x24 guest pointer is unobserved (native window substituted):
        // garbage pointer behaves identically on a non-memmove path.
        {
            u8 *st = mkstate();
            u8 *dt = mkdata();
            u8 ev[0x10];
            mkev(ev, 0x101F, 0x101F);
            int rc = 99;
            log_reset(S0, 1, S0, 1);
            CHECK(gt2_card_run(st, ev, dt, 0x100, aux, 0x1000u, &CB,
                               NULL, &rc) == GT2_CARD_OK && rc == -3 &&
                  G.n == 5,
                  "ptr independence");
        }
    }

    // OOB refusals (reachable states; game reads wild RAM).
    {
        // memmove past a tiny window (ret8CFC4=9, cnt=4, len 8).
        u8 *st = mkstate();
        u8 *dt = mkdata();
        u8 ev[0x10];
        mkev(ev, 0xA00, 0);
        st[0x1C] = 4;
        st[0x1E] = 20;
        static const int s8[] = { 9 };
        static const int sA[] = { 3 };
        int rc = 99;
        log_reset(s8, 1, sA, 1);
        CHECK(gt2_card_run(st, ev, dt, 8, aux, have_aux ? 0x1000u : 0u,
                           &CB, NULL,
                           &rc) == GT2_CARD_ERR_OOB,
              "oob memmove");
        // table index past a tiny aux window (+0x1A=1,+0x1B=0 -> 0x10).
        st = mkstate();
        dt = mkdata();
        mkev(ev, 0xA00, 0);
        st[0x1A] = 1;
        st[0x1C] = 4;
        st[0x1E] = 20;
        log_reset(s8, 1, sA, 1);
        CHECK(gt2_card_run(st, ev, dt, 0x100, aux, 1, &CB, NULL,
                           &rc) == GT2_CARD_ERR_OOB,
              "oob table");
        // null guards.
        CHECK(gt2_card_run(NULL, ev, dt, 0x100, aux, 0x1000u, &CB,
                           NULL, &rc) == GT2_CARD_ERR_INVAL,
              "null state");
    }

    if (failures == 0)
        printf("PASS test_card (status driver xcheck ok)\n");
    return failures != 0;
}

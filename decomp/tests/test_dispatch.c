// gt2_dispatch tests: select/guards + arms + tail + helpers, xcheck vs
// tools/menu_dispatch.py emulation vectors (docs/menu_notes.md). The
// TABLE1 hole TRAPs in emulation (jr into garbage); the native runner
// reports UNKNOWN_ARM instead (documented deviation — native cannot
// execute arbitrary guest bytes).

#include "gt2/dispatch.h"

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

// --- scripted poll + logging worker (mirror the emu hooks) ---

typedef struct {
    char kinds[16];
    int n;
    u32 worker_data[8];
    int nworker;
    const int *script;
    int script_len;
    int script_i;
    int polls;
} tlog_t;

static tlog_t G;

static void log_reset(const int *script, int n) {
    memset(&G, 0, sizeof G);
    G.script = script;
    G.script_len = n;
}

static int t_poll(u8 *blk, void *ctx) {
    (void)blk;
    (void)ctx;
    int v = G.script[G.script_i < G.script_len ? G.script_i :
                     G.script_len - 1];
    G.script_i++;
    G.polls++;
    if (G.n < 16)
        G.kinds[G.n++] = 'P';
    return v;
}

static void t_worker(u8 *blk, u32 data, void *ctx) {
    (void)blk;
    (void)ctx;
    if (G.nworker < 8)
        G.worker_data[G.nworker++] = data;
    if (G.n < 16)
        G.kinds[G.n++] = 'W';
}

static const gt2_dispatch_cb_t CB = { t_poll, t_worker };
static const int POLL0[] = { 0 };

static u8 *mkobj(void) {
    static u8 obj[0x800];
    memset(obj, 0, sizeof obj);
    return obj;
}

static u8 *mktask(u32 len) {
    static u8 task[0x20000];
    memset(task, 0, len < sizeof task ? len : sizeof task);
    return task;
}

int main(void) {
    // locate: 4 known arms + unknown.
    {
        gt2_dispatch_arm_t arm;
        CHECK(gt2_dispatch_arm_locate(0x800177E8u, &arm) ==
                  GT2_DISPATCH_OK && arm == GT2_DISPATCH_ARM_A,
              "locate A");
        CHECK(gt2_dispatch_arm_locate(0x8001782Cu, &arm) ==
                  GT2_DISPATCH_OK && arm == GT2_DISPATCH_ARM_B,
              "locate B");
        CHECK(gt2_dispatch_arm_locate(0x80017860u, &arm) ==
                  GT2_DISPATCH_OK && arm == GT2_DISPATCH_ARM_C,
              "locate C");
        CHECK(gt2_dispatch_arm_locate(0x800178C8u, &arm) ==
                  GT2_DISPATCH_OK && arm == GT2_DISPATCH_ARM_D,
              "locate D");
        CHECK(gt2_dispatch_arm_locate(0xBAADF00Du, &arm) ==
                  GT2_DISPATCH_ERR_UNKNOWN_ARM,
              "locate unknown");
        CHECK(gt2_dispatch_arm_locate(0x800177E8u, NULL) ==
                  GT2_DISPATCH_ERR_INVAL,
              "locate null");
    }

    // select guards.
    {
        u8 *obj = mkobj();
        u8 *task = mktask(GT2_DISPATCH_TASK_MIN);
        u32 tab[10];
        for (int i = 0; i < 10; i++)
            tab[i] = 0x800177E8u;
        obj[0x5D0] = 0;
        task[GT2_DISPATCH_T2 + 0xA] = 3;
        CHECK(gt2_dispatch_select(obj, task, tab) == -1, "sel flag0");
        obj[0x5D0] = 1;
        task[GT2_DISPATCH_T2 + 0xA] = 0;
        CHECK(gt2_dispatch_select(obj, task, tab) == -1, "sel mode0");
        task[GT2_DISPATCH_T2 + 0xA] = 11;
        CHECK(gt2_dispatch_select(obj, task, tab) == -1, "sel mode11");
        task[GT2_DISPATCH_T2 + 0xA] = 3;
        CHECK(gt2_dispatch_select(obj, task, tab) == 2, "sel mode3");
        CHECK(gt2_dispatch_select(NULL, task, tab) == -1, "sel null");
    }

    // sub_select guards.
    {
        u8 *task = mktask(GT2_DISPATCH_TASK_MIN);
        u32 tab[11];
        task[GT2_DISPATCH_T2 + 0xA] = 1;
        CHECK(gt2_dispatch_sub_select(task, tab) == 0, "subsel mode1");
        task[GT2_DISPATCH_T2 + 0xA] = 12;
        CHECK(gt2_dispatch_sub_select(task, tab) == -1, "subsel mode12");
        CHECK(gt2_dispatch_sub_select(NULL, tab) == -1, "subsel null");
    }

    // Full runs: guards-fail -> tail only (rc 9, no calls, s5d1 = 1).
    {
        static const u8 modes[] = { 0, 11 };
        for (unsigned i = 0; i < 2; i++) {
            u8 *obj = mkobj();
            u8 *task = mktask(GT2_DISPATCH_TASK_MIN);
            u32 tab[10];
            obj[0x5D0] = 1;
            task[GT2_DISPATCH_T2 + 0xA] = modes[i];
            int rc = -1;
            log_reset(POLL0, 1);
            CHECK(gt2_dispatch_run(obj, task, GT2_DISPATCH_TASK_MIN,
                                   tab, &CB, NULL, &rc) ==
                      GT2_DISPATCH_OK && rc == 9,
                  "guard run mode %u rc %d", modes[i], rc);
            CHECK(G.n == 0 && obj[0x5D1] == 1, "guard no calls");
        }
        // Unknown table address -> UNKNOWN_ARM (emu: jr TRAP).
        {
            u8 *obj = mkobj();
            u8 *task = mktask(GT2_DISPATCH_TASK_MIN);
            u32 tab[10];
            for (int i = 0; i < 10; i++)
                tab[i] = 0xBAADF00Du;
            obj[0x5D0] = 1;
            task[GT2_DISPATCH_T2 + 0xA] = 5;
            int rc = -1;
            log_reset(POLL0, 1);
            CHECK(gt2_dispatch_run(obj, task, GT2_DISPATCH_TASK_MIN,
                                   tab, &CB, NULL, &rc) ==
                      GT2_DISPATCH_ERR_UNKNOWN_ARM,
                  "hole unknown");
        }
    }

    // Arms x flags (mode fixed 3; table = arm everywhere).
    {
        static const struct {
            gt2_dispatch_arm_t arm;
            u32 addr;
            u8 flag;
            int rc;
            const char *seq;
            u8 s5d1;
        } vecs[] = {
            { GT2_DISPATCH_ARM_A, 0x800177E8u, 0, 11, "PWPP", 0 },
            { GT2_DISPATCH_ARM_A, 0x800177E8u, 2, 7, "PWPP", 0 },
            { GT2_DISPATCH_ARM_A, 0x800177E8u, 5, 9, "PWPP", 1 },
            { GT2_DISPATCH_ARM_B, 0x8001782Cu, 0, 7, "PWP", 0 },
            { GT2_DISPATCH_ARM_B, 0x8001782Cu, 2, 7, "PWP", 0 },
            { GT2_DISPATCH_ARM_B, 0x8001782Cu, 5, 9, "PWPP", 1 },
            { GT2_DISPATCH_ARM_C, 0x80017860u, 0, 4, "PWP", 0 },
            { GT2_DISPATCH_ARM_C, 0x80017860u, 2, 7, "PWP", 0 },
            { GT2_DISPATCH_ARM_C, 0x80017860u, 5, 9, "PWPP", 1 },
            { GT2_DISPATCH_ARM_D, 0x800178C8u, 0, 9, "P", 1 },
            { GT2_DISPATCH_ARM_D, 0x800178C8u, 2, 9, "P", 1 },
            { GT2_DISPATCH_ARM_D, 0x800178C8u, 5, 9, "P", 1 },
        };
        for (unsigned i = 0; i < sizeof vecs / sizeof vecs[0]; i++) {
            u8 *obj = mkobj();
            u8 *task = mktask(GT2_DISPATCH_TASK_MIN);
            u32 tab[10];
            for (int k = 0; k < 10; k++)
                tab[k] = vecs[i].addr;
            obj[0x5D0] = 1;
            obj[0x408] = vecs[i].flag;
            task[GT2_DISPATCH_T2 + 0xA] = 3;
            int rc = -1;
            log_reset(POLL0, 1);
            gt2_dispatch_status_t st = gt2_dispatch_run(
                obj, task, GT2_DISPATCH_TASK_MIN, tab, &CB, NULL, &rc);
            CHECK(st == GT2_DISPATCH_OK && rc == vecs[i].rc,
                  "arm vec %u rc %d (want %d, st %d)", i, rc,
                  vecs[i].rc, st);
            CHECK((int)strlen(vecs[i].seq) == G.n &&
                  memcmp(G.kinds, vecs[i].seq, G.n) == 0,
                  "arm vec %u seq", i);
            CHECK(obj[0x5D1] == vecs[i].s5d1, "arm vec %u s5d1", i);
            if (vecs[i].arm == GT2_DISPATCH_ARM_C &&
                vecs[i].flag == 0) {
                // sync0(obj, mode=3, 11): obj5d8 = mode, T2+A = 11.
                CHECK(obj[0x5D8] == 3 && task[GT2_DISPATCH_T2 + 0xA] == 11,
                      "arm C sync0 routing");
            }
            if (G.nworker > 0)
                CHECK(G.worker_data[0] == GT2_DISPATCH_SUB_DATA0,
                      "arm vec %u worker data %08x", i,
                      G.worker_data[0]);
        }
    }

    // sync0 effects (v=0x77, n=0x99; obj halves 11/N/33).
    {
        static const int ns[] = { 0, 1, 3 };
        static const int nstamps[] = { 0, 1, 3 };
        for (unsigned i = 0; i < 3; i++) {
            // Stamp area (T2+0xE8 stride 0xD0) extends past TASK_MIN;
            // caller windows it (game: larger task RAM).
            u8 *obj = mkobj();
            u8 *task = mktask(0xD000);
            obj[0x5D2] = 0x11;
            obj[0x5D4] = (u8)ns[i];
            obj[0x5D6] = 0x33;
            CHECK(gt2_dispatch_sync0(obj, task, 0xD000, 0x77, 0x99) ==
                      GT2_DISPATCH_OK,
                  "sync0 %d", ns[i]);
            CHECK(obj[0x5D8] == 0x77, "sync0 obj5d8");
            CHECK(task[GT2_DISPATCH_T2 + 0xA] == 0x99 &&
                  task[GT2_DISPATCH_T2 + 0xD] == 0x33 &&
                  task[GT2_DISPATCH_T2 + 0x5A] == (u8)ns[i] &&
                  task[GT2_DISPATCH_T2 + 0xF] == 0x11,
                  "sync0 task win %d", ns[i]);
            for (int k = 0; k < 3; k++) {
                u8 want = k < nstamps[i] ? 1 : 0;
                CHECK(task[GT2_DISPATCH_T2 + 0xE8 + k * 0xD0] == want,
                      "sync0 stamp %d/%d", ns[i], k);
            }
            CHECK(gt2_dispatch_sync0(NULL, task, GT2_DISPATCH_TASK_MIN,
                                     0, 0) == GT2_DISPATCH_ERR_INVAL,
                  "sync0 null");
        }
    }

    // wait scripts.
    {
        static const int s1[] = { 1 };
        static const int s3[] = { -1, 5, 0 };
        u8 *obj = mkobj();
        log_reset(s1, 1);
        CHECK(gt2_dispatch_wait(obj, 0, &CB, NULL) == GT2_DISPATCH_OK &&
              obj[0x38B] == 0 && G.polls == 1,
              "wait0 immediate");
        log_reset(s3, 3);
        CHECK(gt2_dispatch_wait(obj, 0, &CB, NULL) == GT2_DISPATCH_OK &&
              G.polls == 3,
              "wait0 retry");
        log_reset(s1, 1);
        CHECK(gt2_dispatch_wait(obj, 1, &CB, NULL) == GT2_DISPATCH_OK &&
              obj[0x38B] == 1,
              "wait1 flag");
        CHECK(gt2_dispatch_wait(NULL, 0, &CB, NULL) ==
                  GT2_DISPATCH_ERR_INVAL,
              "wait null");
    }

    // sub fallthrough stanza.
    {
        u8 *obj = mkobj();
        int rc = -1;
        log_reset(POLL0, 1);
        CHECK(gt2_dispatch_sub_entry(obj, GT2_DISPATCH_SUB_DATA0, &CB,
                                     NULL, &rc) == GT2_DISPATCH_OK &&
              rc == 1 && G.n == 2 && G.kinds[0] == 'W' &&
              G.kinds[1] == 'P' &&
              G.worker_data[0] == GT2_DISPATCH_SUB_DATA0,
              "sub entry");
    }

    // tail halves incl. OOB (min window) + big-window copies.
    {
        u8 *obj = mkobj();
        u8 *task = mktask(0x20000);
        // (0,0) -> copy from +0x3D1A; (2,7) -> +0xC1E6; (5,5) -> +0x18116.
        task[0x3D1A] = 0xED;
        task[0x3D1B] = 0x5E;
        task[0xC1E6] = 0xED;
        task[0xC1E7] = 0x5E;
        task[0x18116] = 0xED;
        task[0x18117] = 0x5E;
        static const s16 hs[][2] = { { 0, 0 }, { 2, 7 }, { 5, 5 },
                                     { -1, -1 }, { -5, -3 } };
        static const u16 want[] = { 0x5EED, 0x5EED, 0x5EED, 0, 0 };
        for (unsigned i = 0; i < 5; i++) {
            task[GT2_DISPATCH_T2 + 0x582] = (u8)hs[i][0];
            task[GT2_DISPATCH_T2 + 0x582 + 1] =
                (u8)((u16)hs[i][0] >> 8);
            task[GT2_DISPATCH_T2 + 0x584] = (u8)hs[i][1];
            task[GT2_DISPATCH_T2 + 0x584 + 1] =
                (u8)((u16)hs[i][1] >> 8);
            task[GT2_DISPATCH_T2 + 0x58] = 0;
            task[GT2_DISPATCH_T2 + 0x59] = 0;
            int rc = -1;
            CHECK(gt2_dispatch_tail(obj, task, 0x20000, &rc) ==
                      GT2_DISPATCH_OK && rc == 9,
                  "tail %u", i);
            u16 got = (u16)(task[GT2_DISPATCH_T2 + 0x58] |
                            (task[GT2_DISPATCH_T2 + 0x59] << 8));
            CHECK(got == want[i], "tail %u w58 %04x (want %04x)", i,
                  got, want[i]);
            CHECK(obj[0x5D1] == 1, "tail %u s5d1", i);
        }
        // (5,5) with the min window -> OOB (game reads wild RAM).
        // NOTE: poke full halves — the T2 area sits above the min
        // memset window, so stale high bytes would corrupt the sign.
        {
            u8 *t2 = mktask(GT2_DISPATCH_TASK_MIN);
            t2[GT2_DISPATCH_T2 + 0x582] = 5;
            t2[GT2_DISPATCH_T2 + 0x582 + 1] = 0;
            t2[GT2_DISPATCH_T2 + 0x584] = 5;
            t2[GT2_DISPATCH_T2 + 0x584 + 1] = 0;
            int rc = -1;
            CHECK(gt2_dispatch_tail(obj, t2, GT2_DISPATCH_TASK_MIN,
                                    &rc) == GT2_DISPATCH_ERR_OOB,
                  "tail oob");
        }
    }

    if (failures == 0)
        printf("PASS test_dispatch (select+arms+tail+helpers ok)\n");
    return failures != 0;
}

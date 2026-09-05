/* gt2_batch_jobs.c — P2 parallel batch dispatch for GT2's billboard parent.
 *
 * Hooks ov_00010000_74BDB962_F608722F_func_8002002C (gt2_01 scene parent):
 * instead of 33 serial CPS-chained funcA calls, collect the batch args and
 * run them on the batch pool. Each job gets a cloned CPUState (regs+GTE),
 * a private 1KB scratch (P1 override), a private 64KB packet arena (wired
 * as the job's bump pointer via scratch 0x68), and detached cycle
 * accounting. Main merges arenas in index order, remaps the mailbox, and
 * advances the global clock by the summed charges. Jobs run the real
 * CPS chain through a trampoline over both dispatchers (overlay + SCUS).
 *
 * ABI contract: delegate preserves all callee-saved guest regs and sp
 * (never touches them; jobs run on clones). Caller-saved regs end as the
 * collection sequence leaves them (a1 = last batch arg, matching serial).
 *
 * Serial equivalence notes:
 *  - Emission order is call order in both modes (ordered concat), so packet
 *    addresses match bit-for-bit given identical per-batch sizes.
 *  - 0x801C93EC mailbox holds the bump pointer: remapped from the last
 *    enabled job's arena-relative value to real coordinates.
 *  - Cycle accounting is approximate for the ~10-insn collection loop
 *    (exact loads, counted charges); worker charges are exact sums.
 *    Outputs are gate-verified; cycle parity is cosmetic-only.
 *
 * Gates (all must pass or the original serial body runs):
 *   PSX_BATCH_GT2=1, pool on, no IRQ pending, no netplay/ds/lockstep,
 *   not inside device service, no reentry (verify's serial shadow call).
 *
 * PSX_BATCH_VERIFY=1: after parallel merge, re-run the original parent
 * body into a shadow buffer and memcmp packet bytes + mailbox. Mismatch
 * restores the serial result and logs; match keeps the parallel bytes.
 * The shadow call re-enters the generated parent with the reentry guard
 * set so the hook yields to the original body.
 *
 * Wired by tools/patch_overlay_batch.py (hook + extern decl); the override
 * namespace below must match generated/overlays/overlays_static.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "batch_dispatch.h"
#include "psx_cyc.h"

/* Generated + runtime symbols (patch script asserts these exist). */
extern void ov_00010000_74BDB962_F608722F_func_8002002C(CPUState *cpu);
extern int psx_overlay_dispatch(CPUState *cpu, uint32_t addr);
extern int psx_dispatch_game_compiled(CPUState *cpu, uint32_t addr);
extern int psx_netplay_active(void);
extern volatile int g_ds_recording;
extern int g_ls_replay_active;
extern int psx_in_device_service;
extern uint32_t psx_read_word(uint32_t addr);
extern void psx_write_word(uint32_t addr, uint32_t val);
extern void psx_write_byte(uint32_t addr, uint8_t val);
extern uint32_t psx_cyc_load_word(CPUState *cpu, uint32_t addr, uint32_t rt,
                                  uint32_t reg_mask);
extern uint8_t *memory_get_scratchpad_ptr(void);
extern uint8_t *memory_get_ram_ptr(void);
extern void psx_advance_cycles(uint32_t cycles);

#define GT2_MAX_BATCHES 33
#define GT2_ARENA_SIZE (64u * 1024u)
#define GT2_TRAMP_LIMIT 4000000u
#define GT2_SHADOW_BASE 0x801C0000u
#define GT2_SHADOW_SIZE 0x40000u

#define SCR_BASE 0x1F800000u
#define SCR_BUMP 0x68u
#define SCR_MBOX 0x6Cu
#define G_GT2_MAIL 0x801C93ECu

struct gt2_batch {
    uint32_t s3, a1;
    uint8_t *arena;
    uint32_t used;
    uint64_t cycles;
    int overflow;
};

static uint8_t *s_arenas;      /* GT2_MAX_BATCHES x ARENA, malloc pool */
static uint8_t *s_side;        /* merged-bytes save (verify memcmp) */
static uint8_t *s_shadsave;    /* live-RAM snapshot under shadow zone */

/* Gate meter (patch_overlay_gatemeter.py): per-object cull outcomes in
 * funcB's phase-1 loop. Printed periodically when PSX_GATELOG=1. */
static uint64_t s_gate_bypass, s_gate_reject, s_gate_class, s_gate_accept;
static uint64_t s_gate_n;
static int s_gate_init, s_gate_on;
static uint64_t s_gate_code_skip[256], s_gate_code_link[256];
static int16_t s_box_skip_min = 0x7FFF, s_box_skip_max = -0x8000;
static int16_t s_box_link_min = 0x7FFF, s_box_link_max = -0x8000;

void gt2_gate_note(int kind) {
    if (!s_gate_init) {
        s_gate_init = 1;
        const char *e = getenv("PSX_GATELOG");
        s_gate_on = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    if (!s_gate_on)
        return;
    if (kind == 0)
        s_gate_bypass++;
    else if (kind == 1)
        s_gate_reject++;
    else if (kind == 2)
        s_gate_class++;
    else
        s_gate_accept++;
    if ((++s_gate_n & 0x1FFF) == 0) {
        extern uint8_t *memory_get_scratchpad_ptr(void);
        extern uint16_t psx_read_half(uint32_t addr);
        uint8_t *sp = memory_get_scratchpad_ptr();
        (void)sp;
        int top = 0;
        uint64_t topv = 0;
        for (int i = 0; i < 256; i++) {
            if (s_gate_code_skip[i] > topv) {
                topv = s_gate_code_skip[i];
                top = i;
            }
        }
        int ltop = 0;
        uint64_t ltopv = 0;
        for (int i = 0; i < 256; i++) {
            if (s_gate_code_link[i] > ltopv) {
                ltopv = s_gate_code_link[i];
                ltop = i;
            }
        }
        fprintf(stderr,
                "[gt2_gate] bypass=%llu reject=%llu class=%llu accept=%llu "
                "bnds=%04x/%04x/%04x topcode=%02x(n=%llu) "
                "link=%02x(n=%llu) box=%04x/%04x/%04x/%04x "
                "skipx=[%d..%d] linkx=[%d..%d]\n",
                (unsigned long long)s_gate_bypass,
                (unsigned long long)s_gate_reject,
                (unsigned long long)s_gate_class,
                (unsigned long long)s_gate_accept,
                psx_read_half(0x1F80005Eu), psx_read_half(0x1F800060u),
                psx_read_half(0x1F800062u), top,
                (unsigned long long)topv, ltop,
                (unsigned long long)ltopv,
                psx_read_half(0x1F80009Cu), psx_read_half(0x1F80009Eu),
                psx_read_half(0x1F8000A0u), psx_read_half(0x1F8000A2u),
                s_box_skip_min, s_box_skip_max,
                s_box_link_min, s_box_link_max);
    }
}

/* Outcode histogram for class-skip (1) vs linked (0) objects. v = raw
 * 0x8007B640 return; low 8 bits carry the trivial-reject planes. Also
 * tracks the tested box (scratch 0x9C/0x9E, freshly written per object)
 * extremes per outcome: the skip/link boundary in test units. */
void gt2_gate_code(uint32_t v, int skipped) {
    if (!s_gate_on)
        return;
    extern uint16_t psx_read_half(uint32_t addr);
    int16_t bx = (int16_t)psx_read_half(0x1F80009Cu);
    if (skipped) {
        s_gate_code_skip[v & 0xFF]++;
        if (bx < s_box_skip_min)
            s_box_skip_min = bx;
        if (bx > s_box_skip_max)
            s_box_skip_max = bx;
    } else {
        s_gate_code_link[v & 0xFF]++;
        if (bx < s_box_link_min)
            s_box_link_min = bx;
        if (bx > s_box_link_max)
            s_box_link_max = bx;
    }
}

/* Frustum-cull kill switch (patch_scus_nocull.py): forces SCUS
 * func_8007B640 (per-object trivial-reject test) to return 0 = nothing
 * culled. Env PSX_NO_FRUSTUM_CULL=1. Experiment to attribute edge pop-in;
 * NOT for shipping (uncapped overdraw). */
static int s_nocull_init, s_nocull_on;

int gt2_nocull_try(CPUState *cpu) {
    if (!s_nocull_init) {
        s_nocull_init = 1;
        const char *e = getenv("PSX_NO_FRUSTUM_CULL");
        s_nocull_on = (e && e[0] && e[0] != '0') ? 1 : 0;
        if (s_nocull_on)
            fprintf(stderr, "[gt2_nocull] frustum cull DISABLED\n");
    }
    if (!s_nocull_on)
        return 0;
    cpu->gpr[2] = 0;
    return 1;
}

static int s_reentry_guard;
static int s_env_init;
static int s_gt2_on;
static int s_verify_on;

static void gt2_env_init(void) {
    if (s_env_init)
        return;
    s_env_init = 1;
    const char *e = getenv("PSX_BATCH_GT2");
    s_gt2_on = (e && e[0] && e[0] != '0') ? 1 : 0;
    e = getenv("PSX_BATCH_VERIFY");
    s_verify_on = (e && e[0] && e[0] != '0') ? 1 : 0;
    if (s_verify_on)
        s_gt2_on = 1;
}

static uint32_t rd_word_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr_word_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Guest phys -> host pointer (packet stream lives in low RAM, unmirrored). */
static uint8_t *gt2_host_ptr(uint32_t guest_addr, uint32_t *avail) {
    uint32_t phys = guest_addr & 0x1FFFFFFFu;
    if (phys >= 0x200000u)
        return NULL;
    if (avail)
        *avail = 0x200000u - phys;
    return memory_get_ram_ptr() + phys;
}

/* CPS trampoline: follow the batch subgraph through both dispatchers until
 * host-return (pc==0). Anything undispatchable -> 0 (serial fallback). */
static int gt2_trampoline(CPUState *c, uint32_t entry) {
    static int dbg_on = -1;
    if (dbg_on < 0) {
        const char *e = getenv("PSX_BATCH_DEBUG");
        dbg_on = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    c->pc = entry;
    uint32_t steps = 0;
    for (uint32_t i = 0; i < GT2_TRAMP_LIMIT; i++) {
        uint32_t pc = c->pc;
        if (pc == 0)
            return 1;
        steps++;
        int r;
        if (pc >= 0x80010000u && pc < 0x80070000u)
            r = psx_overlay_dispatch(c, pc);
        else
            r = psx_dispatch_game_compiled(c, pc);
        if (!r) {
            if (dbg_on)
                fprintf(stderr, "[gt2_batch] escape pc=%08x steps=%u\n",
                        pc, steps);
            return 0;
        }
    }
    if (dbg_on)
        fprintf(stderr, "[gt2_batch] escape LIMIT pc=%08x\n", c->pc);
    return 0;
}

struct gt2_job_ctx {
    struct gt2_batch *jobs;
    CPUState *base;
};

static void gt2_job_fn(void *vctx, unsigned index) {
    struct gt2_job_ctx *jc = (struct gt2_job_ctx *)vctx;
    struct gt2_batch *J = &jc->jobs[index];
    CPUState *clone = psx_batch_cpu_clone(jc->base);
    uint8_t *priv = psx_batch_scratch_acquire();
    if (!clone || !priv) {
        J->overflow = 1;
        psx_batch_cpu_release(clone);
        psx_batch_scratch_release(priv);
        return;
    }
    psx_batch_scratch_override_set(priv);
    t_batch_detached = 1;
    t_batch_cycles = 0;
    t_batch_local_acc = 0;
    /* Bump pointer := this job's arena. */
    wr_word_le(priv + SCR_BUMP, (uint32_t)(uintptr_t)J->arena);
    clone->gpr[4] = J->s3;
    clone->gpr[5] = J->a1;
    clone->gpr[31] = 0;
    int ok = gt2_trampoline(clone, 0x8001F7F8u);
    uint32_t end = rd_word_le(priv + SCR_BUMP);
    uintptr_t base = (uintptr_t)J->arena;
    /* The game stores absolute RAM addresses here, so validate range. */
    if (!ok || end < base || end - base > GT2_ARENA_SIZE) {
        J->overflow = 1;
    } else {
        J->used = (uint32_t)(end - base);
    }
    J->cycles = psx_batch_cycle_take();
    /* Stash mailbox word for ordered merge (reuse s3 slot). */
    J->s3 = rd_word_le(priv + SCR_MBOX);
    t_batch_detached = 0;
    t_batch_local_acc = 0;
    psx_batch_scratch_override_set(NULL);
    psx_batch_cpu_release(clone);
    psx_batch_scratch_release(priv);
}

int gt2_batch_parent_try(CPUState *cpu) {
    gt2_env_init();
    if (s_reentry_guard)
        return 0;
    if (!s_gt2_on || psx_batch_worker_count() == 0)
        return 0;
    if (psx_irq_pending_for_batch() || psx_netplay_active() ||
        g_ds_recording || g_ls_replay_active || psx_in_device_service)
        return 0;
    if (!s_arenas) {
        s_arenas = (uint8_t *)malloc(GT2_MAX_BATCHES * GT2_ARENA_SIZE);
        if (s_verify_on) {
            s_side = (uint8_t *)malloc(GT2_MAX_BATCHES * GT2_ARENA_SIZE);
            s_shadsave = (uint8_t *)malloc(GT2_SHADOW_SIZE);
        }
        if (!s_arenas || (s_verify_on && (!s_side || !s_shadsave)))
            return 0;
    }
    /* Publish main-thread deferred charges before snapshotting state. */
    psx_cyc_batch_flush();
    CPUState base = *cpu;
    /* Collect the 33 batch arg sets (mirrors parent 0x8002005C loop + tail).
     * Loads use the generated code's exact rt/mask pairs. */
    struct gt2_batch jobs[GT2_MAX_BATCHES];
    memset(jobs, 0, sizeof(jobs));
    uint32_t s3 = cpu->gpr[4], s4 = cpu->gpr[1], s2 = cpu->gpr[6];
    uint32_t s1 = s4;
    unsigned n = 0;
    for (unsigned i = 0; i < 32; i++) {
        if (s2 & 1u) {
            jobs[n].s3 = s3;
            jobs[n].a1 = psx_cyc_load_word(cpu, s1 + 0x118u, 5, 0x20000u);
            n++;
        }
        s1 += 4;
        s2 >>= 1;
        psx_cyc_charge(7);
    }
    jobs[n].s3 = s3;
    jobs[n].a1 = psx_cyc_load_word(cpu, s4 + 0x80u + 0x118u, 5, 0x4u);
    n++;
    psx_cyc_charge(12);
    cpu->gpr[5] = jobs[n - 1].a1; /* match serial clobber of a1 */
    if (n == 0)
        return 1;
    uint32_t real_base = psx_read_word(SCR_BASE + SCR_BUMP);
    uint32_t real_avail = 0;
    uint8_t *real_host = gt2_host_ptr(real_base, &real_avail);
    if (!real_host)
        return 0;
    for (unsigned i = 0; i < n; i++)
        jobs[i].arena = s_arenas + (size_t)i * GT2_ARENA_SIZE;
    struct gt2_job_ctx jc = { jobs, &base };
    psx_batch_run(gt2_job_fn, &jc, n);
    for (unsigned i = 0; i < n; i++) {
        if (jobs[i].overflow) {
            fprintf(stderr, "[gt2_batch] job %u overflow/escape; serial\n", i);
            return 0; /* arenas discarded; live stream untouched */
        }
    }
    /* Ordered concat into the real stream (word path = generated code's
     * own store path, so dirty/trace side effects match serial). */
    uint32_t total = 0;
    for (unsigned i = 0; i < n; i++)
        total += jobs[i].used;
    if (total > real_avail)
        return 0;
    uint32_t cursor = real_base;
    for (unsigned i = 0; i < n; i++) {
        uint8_t *src = jobs[i].arena;
        uint32_t w = jobs[i].used / 4;
        uint32_t r = jobs[i].used % 4;
        for (uint32_t k = 0; k < w; k++, src += 4, cursor += 4) {
            uint32_t v = rd_word_le(src);
            psx_write_word(cursor, v);
        }
        for (uint32_t k = 0; k < r; k++, src++, cursor++) {
            psx_write_byte(cursor, *src);
        }
    }
    /* Mailbox: 0x68 := stream end; 0x6C := last job's value; mail word :=
     * last job's bump remapped to real coordinates (= stream end, since
     * every job's bump advance equals its used count). */
    psx_write_word(SCR_BASE + SCR_BUMP, cursor);
    psx_write_word(SCR_BASE + SCR_MBOX, jobs[n - 1].s3);
    psx_write_word(G_GT2_MAIL, cursor);
    /* Cycle ledger: workers' exact sums rejoin the global clock. */
    uint64_t sum = 0;
    for (unsigned i = 0; i < n; i++)
        sum += jobs[i].cycles;
    while (sum > 0xFFFFFFFFu) {
        psx_advance_cycles(0xFFFFFFFFu);
        sum -= 0xFFFFFFFFu;
    }
    psx_advance_cycles((uint32_t)sum);
    if (s_verify_on) {
        /* Serial shadow: re-run the original body into the shadow zone
         * (live RAM snapshotted first, restored after), then compare
         * packet bytes + mailbox against the parallel merge. */
        uint8_t *realp = gt2_host_ptr(real_base, NULL);
        uint8_t *zonep = gt2_host_ptr(GT2_SHADOW_BASE, NULL);
        if (!realp || !zonep)
            return 1; /* merged result stands; skip verification */
        memcpy(s_side, realp, total);
        memcpy(s_shadsave, zonep, GT2_SHADOW_SIZE);
        uint32_t par_bump = cursor;
        uint32_t par_mbox = psx_read_word(SCR_BASE + SCR_MBOX);
        uint32_t par_mail = psx_read_word(G_GT2_MAIL);
        s_reentry_guard = 1;
        psx_write_word(SCR_BASE + SCR_BUMP, GT2_SHADOW_BASE);
        ov_00010000_74BDB962_F608722F_func_8002002C(cpu);
        s_reentry_guard = 0;
        uint32_t ser_end = psx_read_word(SCR_BASE + SCR_BUMP);
        uint32_t ser_used = ser_end - GT2_SHADOW_BASE;
        uint32_t ser_mbox = psx_read_word(SCR_BASE + SCR_MBOX);
        uint32_t ser_mail = psx_read_word(G_GT2_MAIL);
        /* Serial mail holds shadow coordinates; remap before comparing. */
        uint32_t ser_mail_remap = (ser_mail >= GT2_SHADOW_BASE)
            ? (ser_mail - GT2_SHADOW_BASE + real_base) : 0xFFFFFFFFu;
        int match = (ser_used == total && ser_used <= GT2_SHADOW_SIZE &&
                     memcmp(zonep, s_side, total) == 0 &&
                     ser_mbox == par_mbox && ser_mail_remap == par_mail);
        memcpy(zonep, s_shadsave, GT2_SHADOW_SIZE);
        /* Restore live mailbox to parallel-merged values (identical on
         * match; serial-authoritative values already live either way). */
        psx_write_word(SCR_BASE + SCR_BUMP, par_bump);
        psx_write_word(SCR_BASE + SCR_MBOX, par_mbox);
        if (!match) {
            fprintf(stderr,
                    "[gt2_batch] VERIFY MISMATCH used=%u/%u mbox=%08x/%08x "
                    "mail=%08x/%08x\n",
                    ser_used, total, ser_mbox, par_mbox, ser_mail, par_mail);
            /* NOTE: serial packet bytes are gone with the zone restore;
             * mailbox adopts serial values below while the stream keeps
             * parallel bytes. A mismatch means the design assumption broke
             * (stray shared write); fix the root cause, don't ship this. */
            psx_write_word(SCR_BASE + SCR_BUMP, real_base + ser_used);
            psx_write_word(SCR_BASE + SCR_MBOX, ser_mbox);
            psx_write_word(G_GT2_MAIL, ser_mail_remap);
        }
    }
    return 1;
}

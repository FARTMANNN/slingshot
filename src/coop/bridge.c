// Slingshot's bridge to Squadala!, the co-op mod for this game (github.com/daurosk/oot-remake-multiplayer, GPL-3.0): its co-op module,
// built for macOS from that repository (hookshot_coop.dylib), is the same one the PC kit's Longshot loads, and talks to the game through
// one small C table (that repository's hookshot/src/hos_coop_api.h, version 1). This file fills the table for nxnative, where the game's
// own AArch64 code runs natively, so a Mac player plays in the same co-op games as the PC players (its docs/crossplay.md).
//
// - The module: ~/Library/Application Support/Slingshot/coop/hookshot_coop.dylib (SLINGSHOT_COOP_MODULE: another file), loaded at the
//   game's first frame hook, as Longshot's bridge does; its folder is the module's game folder (coop.ini, coop-options.ini, and
//   mods/contents/0400486024DEC000/coop-puppet, the puppet pack). No module there, or SLINGSHOT_COOP=0: nothing changes.
// - The hooks (observation gates, dm/games/0400486024DEC000/patches.py OBS, OBS_COOP with the site in the upper bits): the two calls of
//   the frame function in moduleCalc_ (sites 0 and 1, just before their mov x0, x20, which Longshot's hook follows: the same point for
//   the module), the AI hold (site 8: after the blr x8 that answers a BSA component its calc bits in w0, which the module may change),
//   the world events of co-op's Longshot patches 0003/0005/0007 (sites 9..14: hos_coop_world events, the same numbers and values), and the title
//   screen's three points (sites 15..17, patch 0004: hos_coop_title, the Multiplayer rows on it), and the game's menus and camera (sites
//   18..20, patches 0018 and 0019: menu screens, the pause menu, the gameplay view; slingshot/src/coop/menus.c).
//   The puppet guard is a stub in the same file (STUBS), and so are the wrapped game functions (patches 0005, 0013, 0014, 0017, 0024):
//   once the module is on, the bridge writes slingshot/src/coop/call.S's wrap entry into the frame step state page (+0xc00), where the wrap stubs look,
//   and the module's hos_coop_wrap sees every call of those functions before and after (phase 0 may skip it), as on Longshot.
// - The world hello: once the module is on, the bridge tells it the hooks' level (hos_coop_world.h version 4) and which wraps this build
//   has (each wrap's first instruction a branch to its stub), as Longshot's bridge does at its first hooked event.
// - The present (nvn_mtl.m dm_present_hook): the module's frame at HOS_COOP_SITE_PRESENT (2), once per picture, loads and pause too. That is co-op's
//   main work (its OnFrame: the players, the actors, the network), so it runs on a thread of its own, woken by each present: nvn_mtl.m calls the hook on
//   the display link's thread, just before the picture goes up, where any time it takes makes the picture late (Longshot calls it in the game's own
//   nvnQueuePresentTexture, which has no such deadline). Presents that come while it still works are done by its next run.
// - The cost: what co-op's calls take, by kind, logged every 10 s ("coop: cost"), for frame-rate reports.
// - Game calls (the table's call): a gate's callback runs on the thread's gate stack, where nxnative never runs the game's code: an SVC
//   the game makes would enter the gate again over the frame below it (entry.S). So a call runs on the game's own stack, below the gate
//   block's frame, and while it runs the thread's gate stack top (dm_gate_top) moves below this frame, so the call's own SVCs get a gate
//   frame of their own under it.
// - Memory: read, write and the 32-bit swap go through slingshot/src/coop/call.S's probes: a fault there is a failed read, not a crash.
// - Null reads: the module's puppets are stripped copies of Link whose code reads through pointers of components they lack; on the
//   emulator such a read gives 0, on Longshot a mapped null page does. macOS cannot map page zero, so a load from the game's code below
//   1 MB gives 0 here (the fault hook decodes it, writes 0 to its registers and steps over it), from the module's start on
//   (HOS_COOP_NULL_READS=0: they fault as before). A write still faults.
#include "../../../host/dm.h"
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ucontext.h>
#include <unistd.h>

// The module's table, as hos_coop_api.h lays it out (version 1, the fields up to scratch)
typedef struct { uint64_t function, x[8], stack[4]; float s0; } coop_call_t;
typedef struct {
    uint32_t version, size;
    uint64_t main_base;
    const char *main_build_id, *game_dir;
    int (*read)(uint64_t address, void *out, size_t size);
    int (*write)(uint64_t address, const void *in, size_t size);
    int (*cas32)(uint64_t address, uint32_t expected, uint32_t desired, uint32_t *seen);
    int (*call)(void *ctx, const coop_call_t *call, uint64_t *x0);
    void (*log)(const char *line);
    uint64_t (*pad_buttons)(void);
    int (*key_down)(uint32_t vk);
    uint64_t (*scratch)(void *ctx, const void *data, size_t n);
} coop_api_t;
_Static_assert(sizeof(coop_call_t) == 112, "HosCoopCall");
_Static_assert(sizeof(coop_api_t) == 96, "HosCoopApi (version 1, up to scratch)");

enum { SITE_RARE = 0, SITE_FRAME = 1, SITE_PRESENT = 2, SITE_HOLD = 8,   // patches.py's OBS parameters (0..2 are hos_coop_api.h's sites)
       SITE_COUNTER = 9, SITE_CREATE = 10, SITE_DELETE = 11, SITE_LIFE = 12, SITE_LIFE_STOPS = 13, SITE_SHOOT = 14,
       SITE_TITLE_SETUP = 15, SITE_TITLE_STEP = 16, SITE_TITLE_GONE = 17,     // the title screen (Longshot's patch 0004): hos_coop_title's events 1, 2, 0
       SITE_MENU_TICK = 18, SITE_PAUSE_FOCUS = 19, SITE_VIEW = 20 };          // the game's menus and camera (Longshot's 0018 and 0019): menus.c
// hos_coop_world.h / hos_coop_actors.h: the events and the hooks' level
enum { W_HELLO = 0, W_COUNTER_START = 1, W_CREATE_REQUEST = 2, W_DELETE_REQUEST = 3, W_LIFE_STEP = 4, W_CREATE_FROM = 5, W_CREATE_STOP = 6,
       W_SHOOT_FROM = 7, W_LIFE_STOPS = 8, W_VERSION = 4 };
// The wrapped functions (patches.py COOP_WRAPS: id, main offset) and where their stubs find the wrap entry
static const struct { uint32_t id, off; } k_wraps[] = {{1, 0xb99980}, {2, 0xeee640}, {3, 0xd7a820}, {4, 0x6733b0}, {5, 0xc08f0}, {6, 0x32d8690},
    {7, 0x32c1db0}, {8, 0x32c2080}, {9, 0x32bbce0}, {16, 0x279ce80}, {17, 0x279e2c0}, {18, 0x3025850}, {19, 0x3b25e00}, {24, 0x440630}};
enum { STATE_WRAP = 0xc00 };
enum { CALL_AREA = 0x400 };   // below the gate block's sp: the call's 4 stack words, then 128 bytes for x8, then 256 of scratch
typedef struct { uint64_t *regs; } coop_ctx_t;

extern uint64_t dm_gate_top[];                 // entry.S / thread.c: each gate slot's stack top
extern const uint8_t dm_gate_stubs[];          // entry.S: gate slot k's stub at + 16 k (a thread's KGATE slot points to its own)
extern const uint8_t coop_probe_start[], coop_probe_end[], coop_probe_fault[];
uint64_t coop_guest_call(uint64_t fn, const uint64_t x[9], uint64_t sp, const float *s0);   // slingshot/src/coop/call.S
int coop_probe_copy(void *dst, const void *src, size_t n);
int coop_probe_cas32(uint32_t *a, uint32_t expected, uint32_t desired, uint32_t *seen);
void coop_wrap_entry(void);
extern void (*dm_present_hook)(void);   // nvn_mtl.m
extern int (*dm_coop_camera_wanted)(uint64_t frame, float width, float height);   // nvn_mtl.m: the game's camera for the module's name tags
extern void (*dm_coop_camera)(uint64_t frame, const float *block, float width, float height);

static _Atomic int g_state;                    // 0: not tried yet, 1: on, -1: off
static _Atomic int g_null_reads;
static _Atomic long g_null_hits;
static int (*m_start)(const coop_api_t *);
static void (*m_frame)(void *ctx, int site);
static uint32_t (*m_hold)(uint64_t component, uint32_t bits);
static void (*m_stop)(void);
static int (*m_world)(uint32_t event, uint64_t a, uint64_t b);
static int (*m_wrap)(uint32_t id, int phase, uint64_t *x, void *ctx);
static void *g_lib;                            // the module, once loaded (dlopen)
static _Atomic int g_world;                    // the module answered the world hello: its events and wraps are on
static uint64_t g_wrap_mask;                   // the wraps this build has (bit id)
static coop_api_t g_api;
static char g_dir[1024], g_build[33];
static uint64_t g_text_lo, g_text_hi;          // main's code: what call() may call

// ---- The cost: time in co-op's calls, by kind, summed in 16 slots (a thread's own, by its id: the game's worker threads call at once) ----
enum { COST_FRAME, COST_HOLD, COST_WORLD, COST_WRAP, COST_MENUS, COST_TITLE, COST_KINDS };
static const char *const k_cost_name[COST_KINDS] = {"frame hook", "held components", "world events", "wrapped game functions", "menus and view", "title"};
typedef struct { _Atomic uint64_t t[COST_KINDS], n[COST_KINDS]; } __attribute__((aligned(128))) cost_slot_t;
static cost_slot_t g_cost[16];
static __thread int t_cost_slot = -1;
static inline uint64_t cost_now(void) { return mach_absolute_time(); }
static inline void cost_add(int kind, uint64_t t0) {
    if (t_cost_slot < 0) { uint64_t id = 0; pthread_threadid_np(NULL, &id); t_cost_slot = (int)(id % 16); }
    cost_slot_t *c = &g_cost[t_cost_slot];
    atomic_fetch_add_explicit(&c->t[kind], cost_now() - t0, memory_order_relaxed);
    atomic_fetch_add_explicit(&c->n[kind], 1, memory_order_relaxed);
}
static double cost_ms(uint64_t ticks) { static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb); return (double)ticks * tb.numer / tb.denom / 1e6; }

// ---- The table ----
static int coop_read(uint64_t a, void *out, size_t n) { return !n || (a >= 0x1000 && a + n > a && coop_probe_copy(out, (const void *)(uintptr_t)a, n)); }
static int coop_write(uint64_t a, const void *in, size_t n) { return !n || (a >= 0x1000 && a + n > a && coop_probe_copy((void *)(uintptr_t)a, in, n)); }
static int coop_cas32(uint64_t a, uint32_t expected, uint32_t desired, uint32_t *seen) {
    uint32_t was = 0;
    if (a < 0x1000 || (a & 3) || !coop_probe_cas32((uint32_t *)(uintptr_t)a, expected, desired, &was)) return 0;
    if (seen) *seen = was;
    return 1;
}
static void coop_log(const char *line) { LOG("%s", line); }
// Player 1's pad (Longshot's patch 0004): the buttons word the game was last given (hle_hid.c keeps it), and the module's hos_coop_pad first in line on
// that read (its title menu, down but not out: a downed Link crawls and A beside one revives), on the game's input thread
extern uint64_t (*dm_coop_pad_filter)(uint64_t buttons, int32_t stick[4]);
extern _Atomic uint64_t dm_coop_pad_word;
static uint64_t (*m_pad)(uint64_t buttons, int32_t stick[4]);
static void (*m_title)(void *ctx, uint64_t screen, int event);   // the title screen's Multiplayer rows (on the game's calc thread, with a ctx call() may use)
static uint64_t coop_pad(void) { return atomic_load_explicit(&dm_coop_pad_word, memory_order_relaxed); }
static uint64_t coop_pad_filter(uint64_t buttons, int32_t stick[4]) {
    return atomic_load_explicit(&g_state, memory_order_acquire) == 1 && m_pad ? m_pad(buttons, stick) : buttons;
}
// The keys held in the game's window, by Windows key code (the front end's key monitor, slingshot/src/squadala.h; cleared when the window loses the
// keyboard): the module's key_down, as GetAsyncKeyState on Windows and the SDL window's keys on Linux (F6 held: the player list)
static _Atomic uint8_t g_keys[256];
void coop_bridge_key(uint32_t vk, int down) { if (vk < 256) atomic_store_explicit(&g_keys[vk], down ? 1 : 0, memory_order_relaxed); }
void coop_bridge_keys_clear(void) { for (int i = 0; i < 256; i++) atomic_store_explicit(&g_keys[i], 0, memory_order_relaxed); }
static int coop_key(uint32_t vk) { return vk < 256 && atomic_load_explicit(&g_keys[vk], memory_order_relaxed); }
static uint64_t call_base(const coop_ctx_t *c) { return (c->regs[99] - CALL_AREA) & ~15ull; }   // regs[99]: the gate block's sp (entry.S frame +792)
// scratch: one fixed place, the same address each time (Longshot's bridge_scratch: the module asks once where its bytes go, then writes them with
// pointers into themselves; coop_title.cpp SetRowTexts). Above the call's stack words and x8's 128 bytes, so the calls made with it never touch it.
static uint64_t coop_scratch(void *ctx, const void *data, size_t n) {
    coop_ctx_t *c = ctx;
    if (!c || !c->regs || n > 256) return 0;
    uint64_t at = call_base(c) + 160;
    if (n && data) memcpy((void *)(uintptr_t)at, data, n);
    return at;
}
static int coop_call(void *ctx, const coop_call_t *call, uint64_t *x0) {
    coop_ctx_t *c = ctx;
    if (!c || !c->regs || !call || call->function < g_text_lo || call->function >= g_text_hi || (call->function & 3)) return 0;
    const uint8_t *stub = pthread_getspecific(g_keygate);
    if (!stub) return 0;
    const size_t k = (size_t)(stub - dm_gate_stubs) / 16;
    const uint64_t base = call_base(c);
    memcpy((void *)(uintptr_t)base, call->stack, sizeof call->stack);
    uint64_t r[9]; memcpy(r, call->x, sizeof call->x); r[8] = base + 32;   // x8: where a function puts a result it returns in memory
    const float s0 = call->s0;
    const uint64_t top = dm_gate_top[k];
    dm_gate_top[k] = ((uint64_t)(uintptr_t)__builtin_frame_address(0) - 1024) & ~15ull;   // the call's SVCs: their gate frames below this one
    const uint64_t ret = coop_guest_call(call->function, r, base, &s0);
    dm_gate_top[k] = top;
    if (x0) *x0 = ret;
    return 1;
}

// ---- Null reads and the probes' faults ----
static void set_x(_STRUCT_ARM_THREAD_STATE64 *ss, unsigned r, uint64_t v) {
    if (r < 29) ss->__x[r] = v; else if (r == 29) ss->__fp = v; else if (r == 30) ss->__lr = v;   // 31: xzr (the load's own sp is never null)
}
static uint64_t get_x(_STRUCT_ARM_THREAD_STATE64 *ss, unsigned r) { return r < 29 ? ss->__x[r] : r == 29 ? ss->__fp : r == 30 ? ss->__lr : ss->__sp; }
static int64_t sext(uint64_t v, int bits) { return (int64_t)(v << (64 - bits)) >> (64 - bits); }
// A load the game's code made: its registers get 0 (with a writeback form, its base register moves on as the load would have made it).
// 0: not a load this knows (it faults as before)
static int null_load(ucontext_t *uc, uint32_t i) {
    _STRUCT_ARM_THREAD_STATE64 *ss = &uc->uc_mcontext->__ss;
    _STRUCT_ARM_NEON_STATE64 *ns = &uc->uc_mcontext->__ns;
    const unsigned rt = i & 31, rn = (i >> 5) & 31, v = (i >> 26) & 1, opc = (i >> 22) & 3, size = i >> 30;
    if ((i & 0x3B000000) == 0x39000000 || (i & 0x3B000000) == 0x38000000) {   // a single register: unsigned offset, or unscaled / indexed / register offset
        int writeback = 0; int64_t imm = 0;
        if ((i & 0x3B000000) == 0x38000000) {
            const unsigned kind = (i >> 10) & 3, b21 = (i >> 21) & 1;
            if (b21 && kind != 2) return 0;                         // atomics and the like
            if (!b21 && (kind == 1 || kind == 3)) { writeback = 1; imm = sext((i >> 12) & 0x1ff, 9); }   // post- / pre-index
        }
        if (v ? !(opc & 1) : opc == 0) return 0;                    // a store
        if (!v && size == 3 && opc == 2) return 1;                  // prfm: nothing to load
        if (v) ns->__v[rt] = 0; else set_x(ss, rt, 0);
        if (writeback && rn != rt) set_x(ss, rn, get_x(ss, rn) + (uint64_t)imm);
        return 1;
    }
    if ((i & 0x3A000000) == 0x28000000) {                           // a pair
        if (!((i >> 22) & 1)) return 0;                             // a store
        const unsigned rt2 = (i >> 10) & 31, kind = (i >> 23) & 3, o = i >> 30;
        const int scale = v ? 4 << o : (o == 2 ? 8 : 4);
        if (v) { ns->__v[rt] = 0; ns->__v[rt2] = 0; } else { set_x(ss, rt, 0); set_x(ss, rt2, 0); }
        if ((kind == 1 || kind == 3) && rn != rt && rn != rt2) set_x(ss, rn, get_x(ss, rn) + (uint64_t)(sext((i >> 15) & 0x7f, 7) * scale));
        return 1;
    }
    if ((i & 0x3F000000) == 0x08000000 && ((i >> 22) & 1)) {        // exclusive or acquire loads
        if (((i >> 23) & 1) && ((i >> 21) & 1)) return 0;          // a compare and swap (it stores)
        set_x(ss, rt, 0);
        if ((i >> 21) & 1) set_x(ss, (i >> 10) & 31, 0);            // a pair
        return 1;
    }
    return 0;
}
static int coop_fault(void *addr, int is_write, void *uc_) {
    ucontext_t *uc = uc_;
    const uint64_t pc = uc->uc_mcontext->__ss.__pc;
    if (pc >= (uint64_t)(uintptr_t)coop_probe_start && pc < (uint64_t)(uintptr_t)coop_probe_end) { uc->uc_mcontext->__ss.__pc = (uint64_t)(uintptr_t)coop_probe_fault; return 1; }
    if (!atomic_load_explicit(&g_null_reads, memory_order_relaxed) || is_write || (uintptr_t)addr >= 0x100000) return 0;
    module_t *m = module_of((const void *)(uintptr_t)pc);
    if (!m || !null_load(uc, *(const uint32_t *)(uintptr_t)pc)) return 0;
    uc->uc_mcontext->__ss.__pc = pc + 4;
    const long n = atomic_fetch_add(&g_null_hits, 1);
    if (n < 32) LOG("coop: null read: %#llx from %s+%#llx: reads 0, as on the emulator", (unsigned long long)(uintptr_t)addr, m->name, (unsigned long long)(pc - (uint64_t)(uintptr_t)m->base));
    else if (n == 32) LOG("coop: null reads: more than 32, no more lines");
    return 1;
}

// ---- The world: events and wraps ----
static uint64_t strip(uint64_t p) { __asm__("xpaci %0" : "+r"(p)); return p; }   // a return address without its pointer signature (pacibsp)
static uint8_t g_no_creates[0x110] = {[0x109] = 2};   // an actor manager that takes no creates now (bit 1 of +0x109): CREATE_STOP's way out
static void world_event(uint64_t *regs, int site) {
    if (!atomic_load_explicit(&g_world, memory_order_acquire)) return;
    switch (site) {
    case SITE_COUNTER: m_world(W_COUNTER_START, regs[0], 0); break;
    case SITE_CREATE: {   // requestCreateActorAsync, its arguments and return still the caller's; the gate is before its ldrb w8, [x0, #0x109]
        const uint64_t from = strip(*(const uint64_t *)(uintptr_t)(regs[99] + 8));   // the guest's x30, which the gate block pushed above its sp
        uint32_t stop = 0;
        m_world(W_CREATE_FROM, from, regs[2]);
        m_world(W_CREATE_REQUEST, regs[1], regs[3]);
        m_world(W_CREATE_STOP, (uint64_t)(uintptr_t)&stop, 0);
        if (stop) regs[0] = (uint64_t)(uintptr_t)g_no_creates;   // the ldrb then reads "no creates": the function's own exit (main+0x372c88), x0 unused there
        break;
    }
    case SITE_DELETE: m_world(W_DELETE_REQUEST, regs[19], regs[20]); break;
    case SITE_LIFE: {
        int32_t sum = (int32_t)(uint32_t)regs[9];
        m_world(W_LIFE_STEP, regs[19], (uint64_t)(uintptr_t)&sum);
        regs[9] = (uint32_t)sum;
        break;
    }
    case SITE_LIFE_STOPS: {
        uint32_t pass = 0;
        m_world(W_LIFE_STOPS, regs[19], (uint64_t)(uintptr_t)&pass);
        if (pass) regs[10] = 0;
        break;
    }
    case SITE_SHOOT: m_world(W_SHOOT_FROM, regs[1], regs[0]); break;
    }
}
// slingshot/src/coop/call.S's wrap entry: phase 0 (1 = the module did the call's work) and phase 1 (the caller's return back). The return and the id wait
// on a per-thread stack while the function runs, since it runs with the caller's sp
typedef struct { uint64_t lr; uint32_t id; } wrap_rec_t;
enum { WRAP_DEPTH = 256 };
static __thread wrap_rec_t t_wrap[WRAP_DEPTH];
static __thread int t_wraps;
int coop_wrap_before(uint32_t id, uint64_t *x) {
    if (t_wraps >= WRAP_DEPTH) die("co-op: wrapped game functions nested more than %d deep", WRAP_DEPTH);
    t_wrap[t_wraps++] = (wrap_rec_t){x[30], id};
    if (!atomic_load_explicit(&g_world, memory_order_acquire) || !m_wrap) return 0;
    const uint64_t t0 = cost_now();
    const int r = m_wrap(id, 0, x, NULL);
    cost_add(COST_WRAP, t0);
    return r;
}
uint64_t coop_wrap_after(uint64_t *x) {
    if (t_wraps <= 0) die("co-op: a wrapped game function returned without its entry");
    const wrap_rec_t r = t_wrap[--t_wraps];
    x[30] = r.lr;
    if (atomic_load_explicit(&g_world, memory_order_acquire) && m_wrap) { const uint64_t t0 = cost_now(); m_wrap(r.id, 1, x, NULL); cost_add(COST_WRAP, t0); }
    return r.lr;
}
// The present's work on its own thread (see the top): each present wakes it, and one run does every present that came since the last
static dispatch_semaphore_t g_present_wake;
static _Atomic uint64_t g_presents;            // presents so far (the hook's count)
static pthread_mutex_t g_present_mu = PTHREAD_MUTEX_INITIALIZER;   // held while the module's present work runs (the stop waits for it)
static void coop_present(void) { atomic_fetch_add_explicit(&g_presents, 1, memory_order_relaxed); dispatch_semaphore_signal(g_present_wake); }
static int cmp_u64(const void *a, const void *b) { const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
// Every 10 s: the present work's time a run (p50, p95, max) and the other kinds' time a present, from the slots
static uint64_t cost_ticks(double ms) { return (uint64_t)(ms * 1e6 / cost_ms(1000000)); }
static void cost_report(const uint64_t *runs, int nruns, uint64_t presents, uint64_t skipped) {
    static uint64_t was_t[COST_KINDS], was_n[COST_KINDS];
    uint64_t sorted[600]; memcpy(sorted, runs, (size_t)nruns * sizeof *runs); qsort(sorted, (size_t)nruns, sizeof *sorted, cmp_u64);
    if (!presents) presents = 1;
    char line[1024]; int k = snprintf(line, sizeof line, "coop: cost over %llu presents: present work (its own thread) p50 %.2f ms, p95 %.2f, max %.2f",
                                      (unsigned long long)presents, cost_ms(sorted[nruns / 2]), cost_ms(sorted[nruns * 95 / 100]), cost_ms(sorted[nruns - 1]));
    if (skipped) k += snprintf(line + k, sizeof line - (size_t)k, " (%llu presents came while it ran)", (unsigned long long)skipped);
    k += snprintf(line + k, sizeof line - (size_t)k, "; a present on the game's threads:");
    const int head = k;
    for (int i = 0; i < COST_KINDS; i++) {
        uint64_t t = 0, n = 0;
        for (int j = 0; j < 16; j++) t += atomic_load_explicit(&g_cost[j].t[i], memory_order_relaxed), n += atomic_load_explicit(&g_cost[j].n[i], memory_order_relaxed);
        if (n != was_n[i] && k < (int)sizeof line - 100)
            k += snprintf(line + k, sizeof line - (size_t)k, "%s %s %.3f ms (%.1f calls)", k > head ? "," : "", k_cost_name[i],
                          cost_ms(t - was_t[i]) / (double)presents, (double)(n - was_n[i]) / (double)presents);
        was_t[i] = t, was_n[i] = n;
    }
    if (k == head) snprintf(line + k, sizeof line - (size_t)k, " none");
    LOG("%s", line);
}
static void *present_worker(void *arg) {
    (void)arg;
    pthread_setname_np("slingshot-coop-present");
    uint64_t done = 0, skipped = 0, from = 0, runs[600]; int nruns = 0;
    uint64_t next = cost_now() + cost_ticks(10000);
    for (;;) {
        dispatch_semaphore_wait(g_present_wake, DISPATCH_TIME_FOREVER);
        const uint64_t want = atomic_load_explicit(&g_presents, memory_order_relaxed);
        if (want == done) continue;   // (a wake for presents an earlier run did)
        if (done && want - done > 1) skipped += want - done - 1;
        done = want;
        pthread_mutex_lock(&g_present_mu);
        if (atomic_load_explicit(&g_state, memory_order_acquire) != 1) { pthread_mutex_unlock(&g_present_mu); continue; }
        const uint64_t t0 = cost_now();
        m_frame(NULL, SITE_PRESENT);
        const uint64_t t1 = cost_now();
        pthread_mutex_unlock(&g_present_mu);
        if (!from) from = done - 1;
        if (nruns < 600) runs[nruns++] = t1 - t0;
        if (t1 >= next) { cost_report(runs, nruns, done - from, skipped); nruns = 0, skipped = 0, from = done; next = t1 + cost_ticks(10000); }
    }
    return NULL;
}
static void present_worker_start(void) {
    g_present_wake = dispatch_semaphore_create(0);
    pthread_attr_t a; pthread_attr_init(&a);
    pthread_attr_set_qos_class_np(&a, QOS_CLASS_USER_INITIATED, 0);   // (below the game's own threads, which are user-interactive)
    pthread_t t;
    if (pthread_create(&t, &a, present_worker, NULL)) die("co-op: the present work's thread did not start");
    pthread_attr_destroy(&a);
}

// ---- The module ----
// The module's own stop (it leaves the session and ends its threads), once, from the front end when the app quits (front_mac.m: the
// window closing, before exit). Never from atexit: exit runs the destructors of the module's static objects too, function-local ones
// made after an atexit handler was registered before it, and its stop then found its mutexes gone (std::system_error, abort at quit)
void coop_bridge_stop(void) {
    int on = 1;
    if (atomic_compare_exchange_strong(&g_state, &on, -1)) {
        atomic_store(&g_world, 0);   // (the wraps then pass straight through the module's hooks)
        dm_coop_camera_wanted = NULL; dm_coop_pad_filter = NULL;
        pthread_mutex_lock(&g_present_mu); pthread_mutex_unlock(&g_present_mu);   // (the present work running now ends first; none starts after)
        if (m_stop) { LOG("coop: stopping the co-op module"); m_stop(); }
    }
}
void coop_voice_init(void);   // voice.c
void coop_menus_obs(uint64_t *regs, int which);   // menus.c
static void coop_load(void) {
    atomic_store(&g_state, -1);
    const char *off = getenv("SLINGSHOT_COOP");
    if (off && !strcmp(off, "0")) return;
    module_t *main = NULL;
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) main = &g_mod[i];
    const char *home = getenv("HOME");
    snprintf(g_dir, sizeof g_dir, "%s/Library/Application Support/Slingshot/coop", home ? home : "");
    char path[1200]; const char *want = getenv("SLINGSHOT_COOP_MODULE");
    snprintf(path, sizeof path, "%s", want && *want ? want : "");
    if (!path[0]) snprintf(path, sizeof path, "%s/hookshot_coop.dylib", g_dir);
    if (access(path, R_OK)) { LOG("coop: no co-op module at %s: co-op off", path); return; }
    if (!main || main->patch_off < 0) { LOG("coop: main's image is not laid out as this bridge expects: co-op off"); return; }
    void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) { LOG("coop: the co-op module did not load: %s", dlerror()); return; }
    g_lib = lib;
    m_start = (int (*)(const coop_api_t *))dlsym(lib, "hos_coop_start");
    m_frame = (void (*)(void *, int))dlsym(lib, "hos_coop_frame");
    m_hold = (uint32_t (*)(uint64_t, uint32_t))dlsym(lib, "hos_coop_hold");
    m_stop = (void (*)(void))dlsym(lib, "hos_coop_stop");
    m_world = (int (*)(uint32_t, uint64_t, uint64_t))dlsym(lib, "hos_coop_world");
    m_wrap = (int (*)(uint32_t, int, uint64_t *, void *))dlsym(lib, "hos_coop_wrap");
    m_pad = (uint64_t (*)(uint64_t, int32_t *))dlsym(lib, "hos_coop_pad");
    m_title = (void (*)(void *, uint64_t, int))dlsym(lib, "hos_coop_title");
    if (!m_start || !m_frame) { LOG("coop: %s has no hos_coop_start / hos_coop_frame: not the co-op module", path); return; }
    snprintf(g_build, sizeof g_build, "%s", main->build_id);
    g_text_lo = (uint64_t)(uintptr_t)main->base; g_text_hi = g_text_lo + main->text_end;
    g_api = (coop_api_t){.version = 1, .size = sizeof g_api, .main_base = g_text_lo, .main_build_id = g_build, .game_dir = g_dir,
                         .read = coop_read, .write = coop_write, .cas32 = coop_cas32, .call = coop_call, .log = coop_log,
                         .pad_buttons = coop_pad, .key_down = coop_key, .scratch = coop_scratch};
    dm_fault_hook_register(coop_fault);   // (now, after the host's own: a fault the renderer's write watch takes is never ours)
    // The wraps this build has: each one's first instruction a branch (patches.py COOP_WRAPS), and the state page they read the entry from
    g_wrap_mask = 0;
    if (main->fps_state_off)
        for (size_t i = 0; i < sizeof k_wraps / sizeof k_wraps[0]; i++)
            if ((*(const uint32_t *)(main->base + k_wraps[i].off) >> 26) == 0x05) g_wrap_mask |= 1ull << k_wraps[i].id;
    // Without the wrap of the game's flag applier (1) the module learns the game's own flag writes by reading the flag tables, as on the emulator
    if (!(g_wrap_mask >> 1 & 1)) setenv("LICHII_COOP_FLAG_EVENTS", "0", 0);
    const char *nr = getenv("HOS_COOP_NULL_READS");
    atomic_store(&g_null_reads, !(nr && !strcmp(nr, "0")));
    LOG("coop: the co-op module from %s, main at %#llx, build %s; its folder %s", path, (unsigned long long)g_text_lo, g_build, g_dir);
    if (!m_start(&g_api)) { atomic_store(&g_null_reads, 0); LOG("coop: the co-op module stays off (its lines above say why)"); return; }
    atomic_store_explicit(&g_state, 1, memory_order_release);
    LOG("coop: on%s", atomic_load(&g_null_reads) ? " (null reads from the game's code give 0)" : "");
    if (m_world && m_world(W_HELLO, W_VERSION, g_wrap_mask) == 1) {
        atomic_store_explicit(&g_world, 1, memory_order_release);
        if (g_wrap_mask) *(uint64_t *)(main->base + main->fps_state_off + STATE_WRAP) = (uint64_t)(uintptr_t)coop_wrap_entry;   // the wrap stubs: on
        int n = 0; for (uint64_t m = g_wrap_mask; m; m &= m - 1) n++;
        LOG("coop: world hooks on (level %d): 6 events, %d wrapped game functions", W_VERSION, n);
    } else LOG("coop: the module did not answer the world hello: no world events or wraps");
    present_worker_start();
    dm_present_hook = coop_present;
    coop_voice_init();   // voice chat, once the module asks for it (voice.c)
    if (m_pad) dm_coop_pad_filter = coop_pad_filter;   // player 1's pad: the module first
    {   // the game's camera from its scene draws, for the name tags (both, or neither)
        void *cw = dlsym(lib, "hos_coop_camera_wanted"), *cc = dlsym(lib, "hos_coop_camera");
        if (cw && cc) { dm_coop_camera = (void (*)(uint64_t, const float *, float, float))cc; dm_coop_camera_wanted = (int (*)(uint64_t, float, float))cw; }
    }
}
static void coop_obs(uint64_t *regs, int param) {
    if (param == SITE_HOLD) {
        if (atomic_load_explicit(&g_state, memory_order_acquire) != 1 || !m_hold) return;
        const uint64_t t0 = cost_now();
        const uint32_t was = (uint32_t)regs[0], now = m_hold(regs[21], was);
        if (now != was) regs[0] = now;
        cost_add(COST_HOLD, t0);
        return;
    }
    if (param >= SITE_TITLE_SETUP && param <= SITE_TITLE_GONE) {   // the title screen: the screen object (x19 at its set-up, x0 at the others)
        if (atomic_load_explicit(&g_state, memory_order_acquire) != 1 || !m_title) return;
        static const int event[] = {1, 2, 0}, reg[] = {19, 0, 0};
        coop_ctx_t c = {.regs = regs};
        const uint64_t t0 = cost_now();
        m_title(&c, regs[reg[param - SITE_TITLE_SETUP]], event[param - SITE_TITLE_SETUP]);
        cost_add(COST_TITLE, t0);
        return;
    }
    if (param >= SITE_MENU_TICK && param <= SITE_VIEW) {   // the menus' screens, the pause menu, the gameplay view (menus.c), once the module is on
        if (atomic_load_explicit(&g_state, memory_order_acquire) != 1) return;
        const uint64_t t0 = cost_now();
        coop_menus_obs(regs, param - SITE_MENU_TICK);
        cost_add(COST_MENUS, t0);
        return;
    }
    if (param >= SITE_COUNTER) {
        if (!atomic_load_explicit(&g_world, memory_order_acquire)) return;
        const uint64_t t0 = cost_now();
        world_event(regs, param);
        cost_add(COST_WORLD, t0);
        return;
    }
    if (atomic_load_explicit(&g_state, memory_order_acquire) == 0) coop_load();   // the first frame hook, on the game's calc thread
    if (atomic_load_explicit(&g_state, memory_order_acquire) != 1) return;
    coop_ctx_t c = {.regs = regs};
    const uint64_t t0 = cost_now();
    m_frame(&c, param);
    cost_add(COST_FRAME, t0);
}
void coop_bridge_init(void) { gate_obs_set(OBS_COOP, coop_obs); }
// For menus.c (the game's menus and camera): main's base, a fault-safe read, and a game function called from a gate's registers (x0 one argument)
uint64_t coop_bridge_main(void) { return g_text_lo; }
int coop_bridge_peek(uint64_t a, void *out, size_t n) { return coop_read(a, out, n); }
int coop_bridge_game_call(uint64_t *regs, uint64_t fn, uint64_t x0) {
    coop_ctx_t c = {.regs = regs}; coop_call_t k = {0}; uint64_t r = 0;
    k.function = fn, k.x[0] = x0;
    return coop_call(&c, &k, &r);
}
// For the front end (slingshot/src/squadala.h: the Squadala! pages in Slingshot's menu, which draw the module's own tabs): the module's state
// (0 not loaded yet, 1 on, -1 off: not there, refused or stopped) and its exports, found by name, only while it is on
int coop_bridge_state(void) { return atomic_load_explicit(&g_state, memory_order_acquire); }
void *coop_bridge_sym(const char *name) { return coop_bridge_state() == 1 && g_lib ? dlsym(g_lib, name) : NULL; }

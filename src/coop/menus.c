// The game's own menus and camera for Squadala! (Longshot's patches 0018 and 0019), ported from Longshot's host/oot_menu.c (hos_oot_menu_tick: the
// screens as they run) and host/hos_main.c (hos_oot_free_view: the gameplay view), with the addresses they use (Longshot's, main at 0x80020000).
//
// Three observation gates (dm/games/0400486024DEC000/patches.py, OBS_COOP sites 18 to 20), on the game's threads, only while the module is on:
//   - 18, every menu screen's update (f_802f1380, x0 the screen; Longshot calls hos_oot_menu_tick there): the screens are tracked as Longshot tracks
//     them. Which runs, which has a focused button shown (looked at every 6th update), the HUD screen (vtable 0x84c08300: the minimap and the Map
//     page's map), the panel's frame (0x84c08aa0: Key Items, Threads of Time, Map), whether the Map page is shown (the frame running alone), and
//     whether a game menu is in front (a screen with a focused button shown, as the one Longshot gives the mouse). The HUD's own refresh
//     (f_833e4200) runs here, at the HUD screen's own update, when the module asked for it.
//   - 19, the pause menu's handler of a pane getting the focus (f_802ed660, x0 the screen): which screen is the pause menu.
//   - 20, the head of the view routine's loop (f_804e48c0 at 0x804e5068, x20 the view): the gameplay view (camera zero's, vtable 0x84c18a88) goes
//     to the module's hos_coop_view first (the Customize page's camera around your Link), and its matrix is built from the eye and target it gives.
// Exported for the module (found by name, dlsym RTLD_DEFAULT, as on Linux): hos_coop_menu_state and hos_coop_hud_refresh.
#include <math.h>
#include <os/lock.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include "../../../host/dm.h"

uint64_t coop_bridge_main(void);                                   // bridge.c: main's base
int coop_bridge_state(void);                                        // 1 while the module is on
void *coop_bridge_sym(const char *name);                            // an export of the module while it is on
int coop_bridge_peek(uint64_t a, void *out, size_t n);              // a fault-safe read
int coop_bridge_game_call(uint64_t *regs, uint64_t fn, uint64_t x0); // a game function called from a gate's registers

#define L(a) ((a) - 0x80020000ull)   // a Longshot address as a main offset
enum { MENU_TICK = 0, PAUSE_FOCUS = 1, VIEW = 2 };   // bridge.c's sites 18, 19, 20 less 18
static const uint64_t HUD_VT = L(0x84c08300), PANEL_FRAME_VT = L(0x84c08aa0), KEY_ITEMS_VT = L(0x84c07798), THREADS_VT = L(0x84c04a18),
                      VIEW_VT = L(0x84c18a88), CAMERAS = L(0x84ca6bc0), HUD_REFRESH = L(0x833e4200);

static int64_t mono_ns(void) { return (int64_t)clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static int peek(uint64_t a, void *dst, size_t n) { return a >= 0x100000 && coop_bridge_peek(a, dst, n); }   // (below 1 MB: a null pointer's)
static uint64_t peek64(uint64_t a) { uint64_t v = 0; return peek(a, &v, 8) ? v : 0; }

// ---- The screens (oot_menu.c) ----
// A button's hit box (button+0x58): its global matrix at +0x28 (3 x 4 by column, the translation at +0x4c, +0x50) and its rectangle at +0x84. Shown
// when it has a size on screen (all 0 while its page is not shown). Longshot's button_rect, without the picture's mirrored box the mouse needs
static int button_shown(uint64_t button) {
    float m[12], b[4];
    const uint64_t box = peek64(button + 0x58);
    return peek(box + 0x28, m, sizeof m) && peek(box + 0x84, b, sizeof b) && m[0] > 1e-3f && m[4] > 1e-3f && b[2] > 0 && b[3] > 0;
}
// Whether the screen shows a focused button: its list of entries (+136 the head, +144 the first, +156 the entry's offset from its node; entry+24 the
// button; a button's +56 its focus state), at most 400, when its focus manager (*(screen+32)+96) is there
static int has_focus(uint64_t screen) {
    if (!peek64(peek64(screen + 32) + 96)) return 0;
    const uint64_t head = screen + 136;
    int32_t off = 0; peek(screen + 156, &off, 4);
    uint64_t node = peek64(screen + 144);
    for (int i = 0; node && node != head && i < 400; i++, node = peek64(node + 8)) {
        const uint64_t button = peek64(node - (uint64_t)(int64_t)off + 24);
        uint8_t state = 0;
        if (peek(button + 56, &state, 1) && state && button_shown(button)) return 1;
    }
    return 0;
}
typedef struct { uint64_t screen, vt; int64_t first_ns, seen_ns, lost_ns; int focus, running; unsigned n; } seen_t;
static seen_t g_seen[24];
static os_unfair_lock g_seen_lock = OS_UNFAIR_LOCK_INIT;   // (the screens' updates run on several of the game's threads at once)
static _Atomic uint64_t g_pause, g_hud, g_panel;              // the pause menu's screen; the HUD screen and the panel's frame, as seen
static _Atomic int g_map_page, g_refresh_wanted;
static _Atomic int64_t g_front_ns, g_hud_seen_ns;             // when a game menu was last in front; when the HUD screen last ran (0: not since seen)
static _Atomic uint32_t g_refreshes;
static seen_t *screen_seen(uint64_t screen, int64_t now, uint64_t main) {
    seen_t *free_one = NULL;
    for (int i = 0; i < 24; i++) {
        if (g_seen[i].screen == screen) return &g_seen[i];
        if (!free_one || g_seen[i].seen_ns < free_one->seen_ns) free_one = &g_seen[i];   // (the one gone longest)
    }
    // (a slot reused is a screen gone longest: the HUD or panel it held is no longer known to run, so it is forgotten, never handed out)
    if (free_one->screen && free_one->screen == atomic_load(&g_hud)) atomic_store(&g_hud, 0), atomic_store(&g_hud_seen_ns, 0);
    if (free_one->screen && free_one->screen == atomic_load(&g_panel)) atomic_store(&g_panel, 0);
    memset(free_one, 0, sizeof *free_one);
    free_one->screen = screen, free_one->first_ns = now, free_one->vt = peek64(screen);
    if (free_one->vt == main + HUD_VT) atomic_store(&g_hud, screen), atomic_store(&g_hud_seen_ns, 0);
    if (free_one->vt == main + PANEL_FRAME_VT) atomic_store(&g_panel, screen);
    return free_one;
}
static void menu_tick(uint64_t *regs) {
    const uint64_t screen = regs[0], main = coop_bridge_main();
    if (!screen) return;
    const int64_t now = mono_ns();
    os_unfair_lock_lock(&g_seen_lock);
    seen_t *me = screen_seen(screen, now, main);
    me->running = 1, me->seen_ns = now;
    if (screen == atomic_load(&g_hud)) atomic_store(&g_hud_seen_ns, now);
    for (int i = 0; i < 24; i++) if (g_seen[i].running && now - g_seen[i].seen_ns > 200000000) g_seen[i].running = 0;   // (no update for 200 ms: stopped)
    if (me->n++ % 6 == 0) {
        const int had = me->focus;
        me->focus = has_focus(screen);
        if (had && !me->focus) me->lost_ns = now;
    }
    int frame = 0, other = 0;   // the Map page: the panel's frame running alone (not under Key Items, Threads of Time or the pause menu)
    const uint64_t pause = atomic_load(&g_pause);
    for (int i = 0; i < 24; i++) {
        if (!g_seen[i].screen || !g_seen[i].running) continue;
        if (g_seen[i].vt == main + PANEL_FRAME_VT) frame = 1;
        if (g_seen[i].vt == main + KEY_ITEMS_VT || g_seen[i].vt == main + THREADS_VT || g_seen[i].screen == pause) other = 1;
    }
    const int map_page = frame && !other;
    atomic_store(&g_map_page, map_page);
    const seen_t *front = NULL;   // the screen in front: of those that ran in the last 100 ms with a focused button shown (or a panel page), the last to come
    for (int i = 0; i < 24; i++) {
        const uint64_t vt = g_seen[i].vt;
        const int panel = vt == main + KEY_ITEMS_VT || vt == main + THREADS_VT || (vt == main + PANEL_FRAME_VT && map_page);
        if (g_seen[i].screen && (g_seen[i].focus || now - g_seen[i].lost_ns < 500000000 || panel) && g_seen[i].running &&
            now - g_seen[i].seen_ns < 100000000 && (!front || g_seen[i].first_ns > front->first_ns))
            front = &g_seen[i];
    }
    if (front) atomic_store(&g_front_ns, now);
    os_unfair_lock_unlock(&g_seen_lock);
    // The HUD screen's own refresh (it reads the dungeon's map, compass, boss key and floors into what the minimap and the Map page draw), when the
    // module asked: here, at that screen's own update on the game's thread, so the screen is alive by definition
    if (atomic_load(&g_refresh_wanted) && peek64(screen) == main + HUD_VT && atomic_exchange(&g_refresh_wanted, 0)) {
        if (coop_bridge_game_call(regs, main + HUD_REFRESH, screen)) atomic_fetch_add(&g_refreshes, 1);
    }
}
typedef struct {
    uint32_t size;
    int32_t map_page, menu_open, reserved;
    uint64_t hud_screen, panel_screen;
    int64_t hud_ran_ns_ago;   // how long ago the HUD screen last ran (-1: not since it was seen); filled when size covers it
    uint32_t hud_refreshes, reserved2;
} menu_state_t;   // Squadala!'s HosCoopMenuState (Longshot's patch 0018)
__attribute__((visibility("default"))) int hos_coop_menu_state(menu_state_t *out) {
    if (!out || out->size < offsetof(menu_state_t, hud_ran_ns_ago)) return 0;   // (an older module's smaller one too)
    const int long_one = out->size >= sizeof *out;
    out->size = long_one ? (uint32_t)sizeof *out : (uint32_t)offsetof(menu_state_t, hud_ran_ns_ago);
    out->map_page = atomic_load(&g_map_page);
    const int64_t at = atomic_load(&g_front_ns), now = mono_ns();
    out->menu_open = at && now - at < 150000000;
    out->reserved = 0;
    out->hud_screen = atomic_load(&g_hud), out->panel_screen = atomic_load(&g_panel);
    if (long_one) {
        const int64_t ran = atomic_load(&g_hud_seen_ns);
        out->hud_ran_ns_ago = out->hud_screen && ran ? now - ran : -1;
        out->hud_refreshes = atomic_load(&g_refreshes), out->reserved2 = 0;
    }
    return 1;
}
__attribute__((visibility("default"))) int hos_coop_hud_refresh(void) { atomic_store(&g_refresh_wanted, 1); return 1; }   // runs at the HUD's next update

// ---- The gameplay view (hos_main.c hos_oot_free_view and Longshot's patch 0019 oot_coop_view) ----
// Camera zero's view: *(*(*(*CAMERAS)+40))+120 while the manager holds cameras (+32)
static uint64_t gameplay_view(uint64_t main) {
    const uint64_t singleton = peek64(main + CAMERAS), manager = singleton ? peek64(singleton) : 0;
    uint32_t n = 0;
    const uint64_t cameras = manager && peek(manager + 32, &n, 4) && n ? peek64(manager + 40) : 0, camera = cameras ? peek64(cameras) : 0;
    return camera ? peek64(camera + 120) : 0;
}
static void view_tick(uint64_t *regs) {
    static int (*view)(double eye[3], double target[3]);
    static int looked;
    if (!looked) { view = (int (*)(double *, double *))coop_bridge_sym("hos_coop_view"); looked = 1; }   // (once: the module is on here)
    const uint64_t v = regs[20], main = coop_bridge_main();
    if (!view || !v || peek64(v) != main + VIEW_VT || gameplay_view(main) != v) return;
    double eye[3], target[3];   // (metres, the world's axes, y up)
    if (!peek(v + 96, eye, sizeof eye) || !peek(v + 120, target, sizeof target) || !view(eye, target)) return;
    const double x = eye[0] - target[0], y = eye[1] - target[1], z = eye[2] - target[2];
    if (!(hypot(x, z) > 1e-6) || !isfinite(x + y + z)) return;   // (straight down, or not a number: the game's own)
    const double yaw = atan2(x, z), pitch = atan2(y, hypot(x, z)), sy = sin(yaw), cy = cos(yaw), sp = sin(pitch), cp = cos(pitch);
    const double axes[3][3] = {{cy, 0, -sy}, {-sp * sy, cp, -sp * cy}, {cp * sy, sp, cp * cy}};   // right, up, back; the eye's offset in the fourth column
    float m[12];
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 4; col++)
            m[row * 4 + col] = col < 3 ? (float)axes[row][col] : (float)-(axes[row][0] * eye[0] + axes[row][1] * eye[1] + axes[row][2] * eye[2]);
    memcpy((void *)(uintptr_t)(v + 8), m, sizeof m);   // (the view's matrix; the object was just read through, so it is there)
}

// bridge.c, sites 18 to 20, while the module is on
void coop_menus_obs(uint64_t *regs, int which) {
    if (which == MENU_TICK) menu_tick(regs);
    else if (which == PAUSE_FOCUS) { if (regs[0]) atomic_store(&g_pause, regs[0]); }
    else if (which == VIEW) view_tick(regs);
}

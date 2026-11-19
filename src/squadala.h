// Squadala! in Slingshot's menu (slingshot/src/menu.h includes this after the mods). Squadala! is the co-op mod for The Legend of Zelda: Ocarina of Time's
// remake (daurosk/oot-remake-multiplayer); on Slingshot it is a mod like the others: its module (hookshot_coop.dylib, built from Squadala!'s own source by
// dm/slingshot/tools/squadala_setup.sh) loads with the game when it is installed (slingshot/src/coop/bridge.c), and without it Slingshot is as it was.
// Its pages are the module's own, as on Windows and Linux: the module names its tabs (Multiplayer, Mods with the randomizer, …) and says what each page's
// rows are, and Slingshot draws them in its own look and hands the presses on (the same table Longshot's menu reads, Squadala!'s hookshot/src/hos_coop_menu.h,
// its patch 0012; typing as its patch 0022). Nothing of a page is Slingshot's: a new Squadala! release brings its new rows with it.
// The Mods page has a Squadala! row (its state: solo, players, hosting …); selecting it opens the module's tabs in the menu's column, the first one shown. Back
// with nothing left to go back to on a tab goes to the column, and from the column to Slingshot's Mods page. The module may also open the menu on a tab or close
// it (hos_coop_menu_wanted), which Slingshot asks ten times a second.
// Keyboard on a page: the arrows, Return and Esc as on Slingshot's pages, Q / E the previous / next tab. While a page types (a name, an address, a password),
// every key goes to the module (hos_coop_key with Windows' key codes, as Longshot gives them, then hos_coop_char with the characters), ⌘V pastes.
// In play: the module draws over the picture (chat, the player list, its panels: hos_coop_draw2, drawn here with Core Graphics), has each key press first
// (its chat box opens with T) and the keyboard while it types; player 1's pad goes through it first (slingshot/src/coop/bridge.c, hle_hid.c).
#pragma once
#import <Cocoa/Cocoa.h>

int coop_bridge_state(void);              // slingshot/src/coop/bridge.c: 0 not loaded yet, 1 on, -1 off
void *coop_bridge_sym(const char *name);  // an export of the module while it is on

// ---- The module's table (hos_coop_menu.h version 1 and hos_coop_host.h's status: their layouts, declared here) -----------------------------------------------
typedef struct { const char *name, *value; uint8_t kind, on, left, right, fixed, pad_[3]; } smc_row;
typedef struct {
    uint32_t size; int32_t rows_n; const smc_row *rows; int32_t selected, typing;
    const char *help_title, *help, *hint, *gold, *change, *confirm, *back;
} smc_page;
enum { SMC_ACTION, SMC_CHOICE, SMC_TOGGLE, SMC_HEADING, SMC_NOTE };
enum { SMC_UP, SMC_DOWN, SMC_LEFT, SMC_RIGHT, SMC_CONFIRM, SMC_BACK, SMC_SHOW, SMC_HIDE, SMC_SELECT, SMC_CLICK };
#define SMC_WANT_NOTHING (-1)
#define SMC_WANT_CLOSE (-2)
typedef struct {
    uint32_t size; int32_t state; uint16_t port, players; uint8_t invite_done, upnp_open, reserved[2];
    char invite[160], relay_code[160], answer[160], password[132], message[256]; int32_t problem; uint32_t stream_kbps;
} smc_status;
enum { SMC_IDLE, SMC_STARTING, SMC_HOSTING, SMC_JOINING, SMC_WAIT_HOST, SMC_CONNECTED, SMC_FAILED };

static struct {
    int (*tabs)(void); const char *(*tab_name)(int); int (*page)(int, smc_page *); int (*input)(int, int, int, int); int (*wanted)(void);
    int (*key)(uint32_t, int); int (*chr)(uint32_t); void (*status)(smc_status *); int (*line)(char *, size_t);
    int (*picture)(int, void *); int (*mouse)(float, float, int);
} g_smc_m;
static int g_smc_found;
// The module's exports, once it is on: the menu's four (all four, or no pages), and the optional others
static int smc_find(void) {
    if (g_smc_found) return g_smc_m.tabs != NULL;
    if (coop_bridge_state() != 1) return 0;
    g_smc_found = 1;
    g_smc_m.tabs = (int (*)(void))coop_bridge_sym("hos_coop_menu_tabs");
    g_smc_m.tab_name = (const char *(*)(int))coop_bridge_sym("hos_coop_menu_tab_name");
    g_smc_m.page = (int (*)(int, smc_page *))coop_bridge_sym("hos_coop_menu_page");
    g_smc_m.input = (int (*)(int, int, int, int))coop_bridge_sym("hos_coop_menu_input");
    g_smc_m.wanted = (int (*)(void))coop_bridge_sym("hos_coop_menu_wanted");
    g_smc_m.key = (int (*)(uint32_t, int))coop_bridge_sym("hos_coop_key");
    g_smc_m.chr = (int (*)(uint32_t))coop_bridge_sym("hos_coop_char");
    g_smc_m.status = (void (*)(smc_status *))coop_bridge_sym("hos_coop_status");
    g_smc_m.line = (int (*)(char *, size_t))coop_bridge_sym("hos_coop_discord_state");
    g_smc_m.picture = (int (*)(int, void *))coop_bridge_sym("hos_coop_menu_picture");
    g_smc_m.mouse = (int (*)(float, float, int))coop_bridge_sym("hos_coop_mouse");
    if (!g_smc_m.tabs || !g_smc_m.tab_name || !g_smc_m.page || !g_smc_m.input) { g_smc_m.tabs = NULL; LOG("slingshot: Squadala!: this module has no menu tabs"); }
    else LOG("slingshot: Squadala!: %d tab(s) in the menu", g_smc_m.tabs());
    return g_smc_m.tabs != NULL;
}
static int smc_ntabs(void) { int n = smc_find() ? g_smc_m.tabs() : 0; return n < 0 ? 0 : n > 6 ? 6 : n; }   // (the column holds six)
static NSString *smc_str(const char *s) { return s && *s ? [NSString stringWithUTF8String:s] ?: @"" : @""; }

// ---- The Mods page's row ---------------------------------------------------------------------------------------------------------------------------------
static NSString *smc_module_path(void) {
    NSString *env = NSProcessInfo.processInfo.environment[@"SLINGSHOT_COOP_MODULE"];
    return env.length ? env : [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"Slingshot/coop/hookshot_coop.dylib"];
}
static int smc_installed(void) { return [NSFileManager.defaultManager fileExistsAtPath:smc_module_path()]; }
static NSString *smc_row_value(void) {
    int st = coop_bridge_state();
    if (st == 0) return @"Starting…";
    if (st < 0) return @"Off";
    smc_find();
    if (g_smc_m.status) {
        smc_status s = {.size = sizeof s};
        g_smc_m.status(&s);
        switch (s.state) {
        case SMC_STARTING: return @"Starting…";
        case SMC_HOSTING: return s.players > 1 ? [NSString stringWithFormat:@"Hosting · %d players", s.players] : @"Hosting";
        case SMC_JOINING: case SMC_WAIT_HOST: return @"Joining…";
        case SMC_FAILED: return @"Problem";
        }
    }
    char line[64] = "";
    if (g_smc_m.line && g_smc_m.line(line, sizeof line) && line[0]) return smc_str(line);   // the module's own words: "Solo", "Co-op: 2 players"
    return @"On";
}
static NSString *smc_row_about(void) {
    int st = coop_bridge_state();
    if (st < 0) return @"Squadala! did not start this time: the log says why (Slingshot › Show Log, or ~/Library/Logs/nxnative).";
    if (st == 0) return @"Squadala! starts with the game. Play with Squadala! players on Windows, Linux and Mac.";
    return @"Play with Squadala! players on Windows, Linux and Mac: host or join a game, your Link's look, mods and the randomizer.";
}

// ---- Its pages in the menu ----------------------------------------------------------------------------------------------------------------------------------
static int g_smc_on;             // the menu shows Squadala!'s pages: the column holds its tabs, the page is the module's
static int g_smc_tab;            // the tab shown
static int g_smc_told = -1;      // the tab the module was last told shows (HOS_COOP_MENU_SHOW / _HIDE), -1 none
static int g_smc_first;          // the page's first row drawn
static int g_smc_typing, g_smc_rows, g_smc_sel;   // from the page's last draw: it types, how many rows, the one chosen
typedef struct { NSRect r; int row, kind; } smc_hit;
static smc_hit g_smc_hit[8]; static int g_smc_nhit;   // the rows drawn, for the mouse
static void smc_tell(int now) {
    if (now == g_smc_told || !smc_find()) return;
    if (g_smc_told >= 0) g_smc_m.input(g_smc_told, SMC_HIDE, 0, 0);
    if (now >= 0) g_smc_m.input(now, SMC_SHOW, 0, 0);
    g_smc_told = now;
}
static void smc_set_tab(int t) {
    int n = smc_ntabs(); if (!n) return;
    t = (t + n) % n;
    if (t != g_smc_tab) g_smc_first = 0;
    g_smc_tab = t; smc_tell(t); sm_redraw();
}
static void smc_enter(int tab) {
    if (!smc_ntabs()) { sm_redraw(); return; }   // not on (yet): the row's line says why
    g_smc_on = 1; g_smc_tab = -1; g_sm_focus = 1; smc_set_tab(tab);
    LOG("slingshot: Squadala!'s pages open");
}
static void smc_leave(void) {   // back to Slingshot's Mods page, on the Squadala! row
    smc_tell(-1); g_smc_on = 0; g_sm_focus = 1; sm_redraw();
}
static void smc_row_set(int v) { (void)v; smc_enter(0); }
static void smc_tab_step(int d) { if (g_smc_on && smc_ntabs()) smc_set_tab(g_smc_tab + d); }   // Q / E
// The Squadala! row, first on the Mods page, when its module is installed
static void smc_add_row(void) {
    if (!smc_installed()) return;
    sm_add(SM_MODS, (sm_row){"Squadala!", NULL, 0, {0}, NULL, smc_row_set, smc_row_value, smc_row_about});
}

// A press of the menu's (SM_GO_*) while Squadala!'s pages show
static void smc_input(int what) {
    if (!smc_ntabs()) { smc_leave(); return; }   // (the module stopped)
    if (!g_sm_focus) {
        switch (what) {
        case SM_GO_UP: smc_set_tab(g_smc_tab - 1); break;
        case SM_GO_DOWN: smc_set_tab(g_smc_tab + 1); break;
        case SM_GO_RIGHT: case SM_GO_OK: g_sm_focus = 1; sm_redraw(); break;
        case SM_GO_LEFT: case SM_GO_BACK: smc_leave(); break;
        }
        return;
    }
    static const int ev[] = {[SM_GO_UP] = SMC_UP, [SM_GO_DOWN] = SMC_DOWN, [SM_GO_LEFT] = SMC_LEFT, [SM_GO_RIGHT] = SMC_RIGHT, [SM_GO_OK] = SMC_CONFIRM, [SM_GO_BACK] = SMC_BACK};
    if (what > SM_GO_BACK) return;
    int took = g_smc_m.input(g_smc_tab, ev[what], 0, 0);
    if (!took && (what == SM_GO_BACK || (what == SM_GO_LEFT && !g_smc_typing))) g_sm_focus = 0;   // nothing to go back to on the tab: the column
    sm_redraw();
}

// ---- Drawing ----------------------------------------------------------------------------------------------------------------------------------------------
#define SMC_TOP 184
#define SMC_PITCH 50
#define SMC_BAR 42
#define SMC_VIS 6
#define SMC_GOLD hq_color(0.478, 0.341, 0.106, 1)
static void smc_draw_header(void) {
    if (!sm_image(@"tab", NSMakeRect(530, 6, 220, 70), 1, 0)) sm_fill(NSMakeRect(545, 14, 190, 54), hq_color(0.40, 0.31, 0.19, 1), 4);
    sm_text(@"Squadala!", SM_MEDIUM, 30, SM_CREAM, 640, 41, 1, 1, 180);
    sm_image(@"flourish", NSMakeRect(330, 30, 180, 32), 1, 0);
    sm_image(@"flourish", NSMakeRect(770, 30, 180, 32), 1, 1);
}
static void smc_draw_column(void) {
    NSRect col = NSMakeRect(60, 96, 330, 560);
    if (!sm_image(@"column", col, 1, 0)) sm_fill(col, hq_color(0.141, 0.102, 0.067, 0.95), 4);
    int n = smc_ntabs();
    for (int i = 0; i < n; i++) {
        NSRect r = sm_cat_rect(i);
        if (!sm_image(@"btn", r, 1, 0)) sm_fill(NSInsetRect(r, 8, 8), hq_color(0.404, 0.322, 0.188, 1), 4);
        if (i == g_smc_tab && !sm_image(@"btn_sel", r, g_sm_focus ? 0.45 : 1, 0)) {
            NSBezierPath *b = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, 6, 6) xRadius:4 yRadius:4]; b.lineWidth = 3;
            [[NSColor colorWithWhite:1 alpha:g_sm_focus ? 0.45 : 1] setStroke]; [b stroke];
        }
        sm_text(smc_str(g_smc_m.tab_name(i)), SM_REGULAR, 27, SM_CREAM, NSMidX(r), NSMidY(r), 1, 1, 230);
    }
    sm_text(@"‹ Slingshot", SM_REGULAR, 19, [SM_CREAM colorWithAlphaComponent:0.7], 225, 628, 1, 1, 0);   // (Back from the column)
}
// One line of text that ends with … when it is wider than room, even at the smallest size sm_text takes
static NSString *smc_fit(NSString *s, int font, CGFloat size, CGFloat room) {
    NSDictionary *a = @{NSFontAttributeName: sm_font(font, size * 0.75)};
    if ([s sizeWithAttributes:a].width <= room) return s;
    while (s.length > 1 && [[s stringByAppendingString:@"…"] sizeWithAttributes:a].width > room) s = [s substringToIndex:s.length - 1];
    return [s stringByAppendingString:@"…"];
}
static void smc_draw_rows(const smc_page *p) {
    int n = p->rows_n > 0 && p->rows ? p->rows_n : 0, sel = p->selected;
    if (sel >= 0 && sel < n) {
        if (sel < g_smc_first) g_smc_first = sel;
        if (sel >= g_smc_first + SMC_VIS) g_smc_first = sel - SMC_VIS + 1;
        if (sel == g_smc_first && sel > 0 && p->rows[sel - 1].kind == SMC_HEADING) g_smc_first--;   // its heading too
    }
    if (g_smc_first > n - SMC_VIS) g_smc_first = n - SMC_VIS;
    if (g_smc_first < 0) g_smc_first = 0;
    g_smc_nhit = 0;
    for (int k = 0; k < SMC_VIS && g_smc_first + k < n; k++) {
        int i = g_smc_first + k;
        const smc_row *w = &p->rows[i];
        NSRect b = NSMakeRect(455, SMC_TOP + k * SMC_PITCH, 560, SMC_BAR); CGFloat y = NSMidY(b);
        NSString *name = smc_str(w->name);
        if (w->kind == SMC_HEADING) {   // a section's title: gold on the parchment, a rule after it
            CGFloat tw = sm_text(smc_fit(name, SM_MEDIUM, 24, 600), SM_MEDIUM, 24, SMC_GOLD, 462, y + 4, 0, 0, 600);
            [[SMC_GOLD colorWithAlphaComponent:0.6] setFill]; NSRectFillUsingOperation(NSMakeRect(462 + tw + 14, y + 4, 1190 - (462 + tw + 14), 1.5), NSCompositingOperationSourceOver);
            continue;
        }
        if (w->kind == SMC_NOTE) { sm_text(smc_fit(name, SM_REGULAR, 19, 720), SM_REGULAR, 19, SM_INK_SOFT, 466, y, 0, 0, 720); continue; }
        int chosen = i == sel && g_sm_focus, fixed = w->fixed != 0;
        if (g_smc_nhit < 8) g_smc_hit[g_smc_nhit++] = (smc_hit){NSMakeRect(455, b.origin.y, 1210 - 455, SMC_BAR), i, w->kind};
        if (!sm_image(chosen ? @"bar_sel" : @"bar", b, fixed ? 0.6 : 1, 0)) sm_fill(b, chosen ? hq_color(0.588, 0.471, 0.282, 0.95) : hq_color(0.173, 0.129, 0.082, fixed ? 0.5 : 0.85), 2);
        sm_text(smc_fit(name, SM_REGULAR, 24, 470), SM_REGULAR, 24, [SM_CREAM colorWithAlphaComponent:fixed ? 0.55 : 1], 478, y, 0, 1, 470);
        NSColor *ink = [SM_INK colorWithAlphaComponent:fixed ? 0.45 : 1];
        if (w->kind == SMC_TOGGLE) {   // Off / On halves: the one it is in ink, the other faint
            sm_text(@"Off", w->on ? SM_REGULAR : SM_MEDIUM, 24, w->on ? [SM_INK_SOFT colorWithAlphaComponent:0.35] : ink, SM_VALUE_X - 34, y, 1, 0, 0);
            sm_text(@"On", w->on ? SM_MEDIUM : SM_REGULAR, 24, w->on ? ink : [SM_INK_SOFT colorWithAlphaComponent:0.35], SM_VALUE_X + 34, y, 1, 0, 0);
            [[ink colorWithAlphaComponent:0.7] setFill]; NSRectFillUsingOperation(NSMakeRect(SM_VALUE_X + (w->on ? 34 : -34) - 16, y + 15, 32, 2), NSCompositingOperationSourceOver);
            continue;
        }
        NSString *v = smc_str(w->value);
        if (chosen && p->typing) v = [v stringByAppendingString:fmod(CACurrentMediaTime(), 1.0) < 0.55 ? @"|" : @" "];   // the caret, while it types
        if (w->kind == SMC_ACTION && !v.length) {   // an action without a value opens a page: a small arrow at the value's end
            sm_text(@"›", SM_MEDIUM, 30, ink, 1190, y, 2, 0, 0);
            continue;
        }
        CGFloat vw = sm_text(smc_fit(v, SM_REGULAR, 24, 200), SM_REGULAR, 24, ink, SM_VALUE_X, y, 1, 0, 200), half = fmax(vw / 2, 40);
        if (chosen && w->kind == SMC_CHOICE && !fixed) {
            if (w->left && !sm_image(@"arrow_left", NSMakeRect(SM_VALUE_X - half - 38, y - 16, 32, 32), 1, 0)) sm_text(@"‹", SM_MEDIUM, 30, SM_INK, SM_VALUE_X - half - 12, y, 2, 0, 0);
            if (w->right && !sm_image(@"arrow_right", NSMakeRect(SM_VALUE_X + half + 6, y - 16, 32, 32), 1, 0)) sm_text(@"›", SM_MEDIUM, 30, SM_INK, SM_VALUE_X + half + 12, y, 0, 0, 0);
        }
    }
    if (g_smc_first > 0) sm_text(@"▲", SM_REGULAR, 14, SM_INK_SOFT, 735, SMC_TOP - 12, 1, 0, 0);
    if (g_smc_first + SMC_VIS < n) sm_text(@"▼", SM_REGULAR, 14, SM_INK_SOFT, 735, SMC_TOP + SMC_VIS * SMC_PITCH - 2, 1, 0, 0);
}
// The help box under the rows: its title, its paragraph, a line set off under it, one gold line
static void smc_draw_help(const smc_page *p) {
    NSString *title = smc_str(p->help_title), *help = smc_str(p->help), *hint = smc_str(p->hint), *gold = smc_str(p->gold);
    CGFloat top = SMC_TOP + SMC_VIS * SMC_PITCH + 8, bottom = 652;
    [[SM_INK_SOFT colorWithAlphaComponent:0.35] setFill]; NSRectFillUsingOperation(NSMakeRect(470, top - 4, 700, 1), NSCompositingOperationSourceOver);
    if (title.length) { sm_text(smc_fit(title, SM_MEDIUM, 22, 720), SM_MEDIUM, 22, SM_INK, 820, top + 16, 1, 0, 720); top += 32; }
    if (gold.length) { sm_text(smc_fit(gold, SM_REGULAR, 18, 720), SM_MEDIUM, 18, SMC_GOLD, 820, bottom - 10, 1, 0, 720); bottom -= 24; }
    if (hint.length) { sm_text(smc_fit(hint, SM_REGULAR, 17, 720), SM_REGULAR, 17, [SM_INK_SOFT colorWithAlphaComponent:0.8], 820, bottom - 10, 1, 0, 720); bottom -= 24; }
    if (help.length && bottom > top) {
        NSRect r = NSMakeRect(450, top, 740, bottom - top);
        [NSGraphicsContext saveGraphicsState]; NSRectClip(r);
        sm_para(help, r, 18, SM_INK_SOFT);
        [NSGraphicsContext restoreGraphicsState];
    }
}
static NSString *g_smc_change, *g_smc_confirm, *g_smc_back;   // the prompts of the page last drawn, for the guide (the module's words)
// A picture beside the page (its hos_coop_menu_picture.h, version 2; Longshot's patch 0019): convex shapes the module draws in a square, here in the column
// under its tabs (Longshot: over the help column), or the square `live`, a clear window onto the game behind the menu with the shapes over it (the
// Customize page shows your own Link there). Asked right after the page, on the same thread
typedef struct { uint32_t rgba; uint8_t n, outline, pad_[2]; float xy[16]; } smc_shape;
typedef struct { uint32_t size; int32_t shapes_n; const smc_shape *shapes; int32_t live; float window[4]; float aspect; } smc_picture;
static void smc_draw_picture(void) {
    if (!g_smc_m.picture || !g_sm_view) return;
    CGFloat top = 130 + smc_ntabs() * 90 + 6, side = fmin(250, 600 - top);
    if (side < 120) return;
    NSRect sq = NSMakeRect(225 - side / 2, top, side, side);
    NSSize z = g_sm_view.bounds.size; CGFloat s, ox, oy; sm_fit(z, &s, &ox, &oy);
    smc_picture pic = {.size = sizeof pic};
    pic.window[0] = (float)((ox + NSMinX(sq) * s) / z.width); pic.window[1] = (float)((oy + NSMinY(sq) * s) / z.height);
    pic.window[2] = (float)((ox + NSMaxX(sq) * s) / z.width); pic.window[3] = (float)((oy + NSMaxY(sq) * s) / z.height);
    pic.aspect = (float)(z.width / fmax(z.height, 1));
    if (!g_smc_m.picture(g_smc_tab, &pic)) return;
    if (pic.live) {   // a clear window onto the game: the menu's dim and column taken out there, a thin frame round it
        [[NSColor clearColor] set]; NSRectFillUsingOperation(sq, NSCompositingOperationCopy);
        NSBezierPath *f = [NSBezierPath bezierPathWithRect:NSInsetRect(sq, 0.75, 0.75)]; f.lineWidth = 1.5; [[SM_CREAM colorWithAlphaComponent:0.7] setStroke]; [f stroke];
    }
    int n = pic.shapes_n < 0 ? 0 : pic.shapes_n > 256 ? 256 : pic.shapes_n;
    for (int i = 0; i < n && pic.shapes; i++) {
        const smc_shape *sh = &pic.shapes[i];
        if (sh->n < 3 || sh->n > 8) continue;
        NSBezierPath *p = [NSBezierPath bezierPath];
        for (int k = 0; k < sh->n; k++) {
            NSPoint q = NSMakePoint(NSMinX(sq) + sh->xy[2 * k] * side, NSMinY(sq) + sh->xy[2 * k + 1] * side);
            if (k) [p lineToPoint:q]; else [p moveToPoint:q];
        }
        [p closePath];
        NSColor *c = [NSColor colorWithSRGBRed:(sh->rgba >> 24) / 255.0 green:((sh->rgba >> 16) & 0xff) / 255.0 blue:((sh->rgba >> 8) & 0xff) / 255.0 alpha:(sh->rgba & 0xff) / 255.0];
        if (sh->outline) { p.lineWidth = 1.5; [c setStroke]; [p stroke]; } else { [c setFill]; [p fill]; }
    }
}
static void smc_draw_page(void) {
    NSRect paper = NSMakeRect(420, 96, 800, 560);
    if (!sm_image(@"parchment", paper, 1, 0)) sm_fill(paper, hq_color(0.478, 0.380, 0.220, 1), 4);
    int n = smc_ntabs();
    if (!n) { sm_text(@"Squadala! is not running", SM_MEDIUM, 30, SM_INK, 820, 330, 1, 0, 700); return; }
    if (g_smc_tab >= n) g_smc_tab = n - 1;
    CGFloat hw = sm_text(smc_str(g_smc_m.tab_name(g_smc_tab)), SM_MEDIUM, 38, SM_INK, 820, 150, 1, 0, 400);
    sm_image(@"flourish_dark", NSMakeRect(820 - hw / 2 - 134, 139, 120, 22), 1, 0);
    sm_image(@"flourish_dark", NSMakeRect(820 + hw / 2 + 14, 139, 120, 22), 1, 1);
    smc_page p = {.size = sizeof p, .selected = -1};
    if (!g_smc_m.page(g_smc_tab, &p)) { p = (smc_page){.size = sizeof p, .selected = -1}; }
    g_smc_typing = p.typing != 0; g_smc_rows = p.rows_n; g_smc_sel = p.selected;
    g_smc_change = smc_str(p.change); g_smc_confirm = smc_str(p.confirm); g_smc_back = smc_str(p.back);   // (copied at once: the module's strings hold until its next page call)
    smc_draw_rows(&p);
    smc_draw_help(&p);
    smc_draw_picture();
}
static void smc_draw_guide(CGFloat right, CGFloat bottom) {
    NSRect g = NSMakeRect(700, 664, right - 700, bottom - 664);
    if (!sm_image(@"guide", g, 1, 0)) sm_fill(g, [NSColor colorWithWhite:0 alpha:0.45], 0);
    int which[3], n = 0; NSString *label[3];
    if (!g_sm_focus) { which[n] = SM_GO_OK; label[n++] = @"Open"; which[n] = SM_GO_BACK; label[n++] = @"Slingshot"; }
    else {
        if (g_smc_change.length) { which[n] = SM_GO_CHANGE; label[n++] = g_smc_change; }
        if (g_smc_confirm.length) { which[n] = SM_GO_OK; label[n++] = g_smc_confirm; }
        which[n] = SM_GO_BACK; label[n++] = g_smc_back.length ? g_smc_back : @"Back";
    }
    CGFloat x = 1250, y = 692;
    for (int i = n - 1; i >= 0; i--) { x -= sm_button(which[i], x, y) + 10; x -= sm_text(label[i], SM_REGULAR, 23, SM_CREAM, x, y, 2, 1, 0) + 30; }
}

// ---- The mouse --------------------------------------------------------------------------------------------------------------------------------------------
static void smc_mouse_at(NSPoint p, int click) {
    int n = smc_ntabs();
    for (int i = 0; i < n; i++) if (NSPointInRect(p, sm_cat_rect(i))) {
        if (g_sm_focus || g_smc_tab != i) { g_sm_focus = 0; smc_set_tab(i); }
        if (click) { g_sm_focus = 1; sm_redraw(); }
        return;
    }
    if (click && NSPointInRect(p, NSMakeRect(85, 610, 280, 36))) { smc_leave(); return; }   // ‹ Slingshot
    for (int k = 0; k < g_smc_nhit; k++) {
        const smc_hit *h = &g_smc_hit[k];
        if (!NSPointInRect(p, h->r)) continue;
        if (!g_sm_focus || h->row != g_smc_sel) { g_sm_focus = 1; g_smc_m.input(g_smc_tab, SMC_SELECT, h->row, 0); }
        if (click) g_smc_m.input(g_smc_tab, SMC_CLICK, h->row, p.x < 1015 ? 0 : p.x < SM_VALUE_X ? -1 : 1);   // the name, or the value's left / right half
        sm_redraw();
        return;
    }
}

// ---- Typing -----------------------------------------------------------------------------------------------------------------------------------------------
// A Mac key as Windows' virtual-key code, which co-op's code reads (Longshot hands it Windows' own); 0 none
static uint32_t smc_vk(NSEvent *e) {
    switch (e.keyCode) {
    case 36: case 76: return 0x0D;   // Return, Enter
    case 53: return 0x1B;            // Esc
    case 51: return 0x08;            // Delete (backspace)
    case 117: return 0x2E;           // Forward Delete
    case 48: return 0x09;            // Tab
    case 49: return 0x20;            // Space
    case 123: return 0x25; case 126: return 0x26; case 124: return 0x27; case 125: return 0x28;   // the arrows
    case 115: return 0x24; case 119: return 0x23; case 116: return 0x21; case 121: return 0x22;   // Home, End, Page Up, Page Down
    case 114: return 0x2D;           // Help (Insert)
    case 122: return 0x70; case 120: return 0x71; case 99: return 0x72; case 118: return 0x73; case 96: return 0x74; case 97: return 0x75;   // F1 … F6
    case 98: return 0x76; case 100: return 0x77; case 101: return 0x78; case 109: return 0x79; case 103: return 0x7A; case 111: return 0x7B;   // F7 … F12
    }
    NSString *s = e.charactersIgnoringModifiers.uppercaseString;
    if (s.length == 1) { unichar c = [s characterAtIndex:0]; if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c; }   // letters and digits: their ASCII
    return 0;
}
// A key in the game's window (front_mac.m's key monitor, before anything else): 1 when Squadala!'s page took it. While the page types, every key is the
// module's (a repeat too); ⌘V (or Ctrl+V) pastes on a page whether it types or not, as on Windows; ⌘ with another key stays the Mac's
static int smc_type(NSEvent *e) {
    if (!g_sm_open || !g_smc_on || !g_sm_focus || !smc_ntabs() || !g_smc_m.key) return 0;
    NSEventModifierFlags mods = e.modifierFlags;
    int command = (mods & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0;
    uint32_t vk = smc_vk(e);
    if (e.type == NSEventTypeKeyDown && command && vk == 'V') { g_smc_m.key('V', e.isARepeat); sm_redraw(); return 1; }
    if (!g_smc_typing) return 0;
    if (command && (mods & NSEventModifierFlagCommand)) return 0;   // ⌘Q, ⌘Tab …: the Mac's
    if (e.type != NSEventTypeKeyDown) return 1;
    if (vk == 0x70) return 0;   // F1: the menu's (it closes, the typing with it)
    if (vk) g_smc_m.key(vk, e.isARepeat);
    if (g_smc_m.chr && !command) {
        NSString *t = e.characters;
        for (NSUInteger i = 0; i < t.length; i++) {
            uint32_t cp = [t characterAtIndex:i];
            if (CFStringIsSurrogateHighCharacter((UniChar)cp) && i + 1 < t.length) cp = CFStringGetLongCharacterForSurrogatePair((UniChar)cp, [t characterAtIndex:++i]);
            if (cp >= 0x20 && cp != 0x7F && !(cp >= 0xF700 && cp <= 0xF8FF)) g_smc_m.chr(cp);   // (not the function keys' private characters)
        }
    }
    sm_redraw();
    return 1;
}
// The few SDL calls co-op's Linux and Mac code asks the game's SDL for (its CoopClipboard::GameSdl, which on macOS looks in this program:
// dm/slingshot/patches/squadala-macos.patch): the modifiers held (Ctrl+V pastes; ⌘ counts as Ctrl) and the clipboard's text
__attribute__((visibility("default"))) unsigned SDL_GetModState(void) {
    NSEventModifierFlags f = NSEvent.modifierFlags;
    return ((f & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) ? 0x0040u : 0) | ((f & NSEventModifierFlagShift) ? 0x0001u : 0);
}
__attribute__((visibility("default"))) char *SDL_GetClipboardText(void) {
    NSString *s = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] ?: @"";
    return strdup(s.UTF8String ?: "");
}
__attribute__((visibility("default"))) void SDL_free(void *p) { free(p); }

// ---- What the module wants of the menu, and the pages' changes ---------------------------------------------------------------------------------------------
static void sm_open_menu(void);
static void smc_tick(void) {
    if (!smc_find()) return;
    if (g_win && g_smc_m.mouse && !g_win.acceptsMouseMovedEvents) g_win.acceptsMouseMovedEvents = YES;   // (the module's hover: smc_play_mouse)
    int w = g_smc_m.wanted ? g_smc_m.wanted() : SMC_WANT_NOTHING;
    if (w >= 0 && w < smc_ntabs()) {   // the module opens the menu on one of its tabs
        if (!g_sm_open) sm_open_menu();
        if (g_sm_open) { g_sm_cat = SM_MODS; smc_enter(w); }
    } else if (w == SMC_WANT_CLOSE && g_sm_open) sm_close();
    if (g_sm_open && (g_smc_on || g_sm_cat == SM_MODS)) sm_redraw();   // its pages' lines and its row's state change by themselves (a join's progress, the players)
}

// ---- Squadala!'s drawing over the picture (its hos_coop_draw.h, as Longshot's patch 0001 and the game look's hos_coop_draw2 give it) --------------------------
// Chat lines, the player list (F6 held), the panels, name tags and the rest the module draws: it says when it has something (hos_coop_draw_wanted), and
// Slingshot then draws its table into a view over the game that takes no clicks, 30 times a second, with Core Graphics. Coordinates are pixels of the
// window's picture from its top left; colours Dear ImGui's (red in the low byte). Its fonts (the game's HUD font, which the module names by file) and its
// pictures (hearts) are loaded once and kept; without one of its fonts the text is in the game's menu font.
typedef struct {
    float width, height; void *user;
    void (*rect)(void *, float, float, float, float, uint32_t, float);
    void (*circle)(void *, float, float, float, uint32_t, float);
    void (*triangle)(void *, float, float, float, float, float, float, uint32_t);
    void (*text)(void *, float, float, float, uint32_t, const char *);
    float (*text_width)(void *, float, const char *);
    int (*font)(void *, const char *);
    void (*use_font)(void *, int);
    int (*image)(void *, int, int, const uint8_t *);
    void (*draw_image)(void *, int, float, float, float, float, uint32_t);
} smc_draw_t;
static int (*g_smc_draw_wanted)(void); static void (*g_smc_draw2)(const smc_draw_t *), (*g_smc_draw1)(const smc_draw_t *);
static int (*g_smc_keys_taken)(void);
static NSMutableArray *g_smc_fonts, *g_smc_images; static NSMutableDictionary<NSString *, NSNumber *> *g_smc_font_ids;
static int g_smc_font_cur;   // 0: the game's menu font
static int smc_find_draw(void) {
    static int found;
    if (found) return g_smc_draw_wanted != NULL;
    if (coop_bridge_state() != 1) return 0;
    found = 1;
    g_smc_draw_wanted = (int (*)(void))coop_bridge_sym("hos_coop_draw_wanted");
    g_smc_draw2 = (void (*)(const smc_draw_t *))coop_bridge_sym("hos_coop_draw2");
    g_smc_draw1 = (void (*)(const smc_draw_t *))coop_bridge_sym("hos_coop_draw");
    g_smc_keys_taken = (int (*)(void))coop_bridge_sym("hos_coop_keys_taken");
    if (!g_smc_draw2 && !g_smc_draw1) g_smc_draw_wanted = NULL;
    g_smc_fonts = [NSMutableArray new]; g_smc_images = [NSMutableArray new]; g_smc_font_ids = [NSMutableDictionary new];
    return g_smc_draw_wanted != NULL;
}
static NSColor *smc_col(uint32_t c) { return [NSColor colorWithSRGBRed:(c & 0xff) / 255.0 green:((c >> 8) & 0xff) / 255.0 blue:((c >> 16) & 0xff) / 255.0 alpha:(c >> 24) / 255.0]; }
static NSFont *smc_font_px(float px) {
    if (px < 1) px = 1;
    if (g_smc_font_cur > 0 && g_smc_font_cur <= (int)g_smc_fonts.count) {
        NSFont *f = (__bridge_transfer NSFont *)CTFontCreateWithFontDescriptor((__bridge CTFontDescriptorRef)g_smc_fonts[g_smc_font_cur - 1], px, NULL);
        if (f) return f;
    }
    return sm_font(SM_REGULAR, px);
}
// A measuring pass (smc_overlay_tick) runs the module's drawing with nothing drawn, only the box around what it would draw kept (in its pixels): the view then
// redraws only where something was or is, not the whole window 30 times a second
static int g_smc_measure; static CGRect g_smc_box;
static void smc_grow(float x0, float y0, float x1, float y1) { g_smc_box = CGRectUnion(g_smc_box, CGRectMake(MIN(x0, x1), MIN(y0, y1), fabsf(x1 - x0), fabsf(y1 - y0))); }
static void smc_d_rect(void *u, float x0, float y0, float x1, float y1, uint32_t c, float r) {
    (void)u; if (g_smc_measure) { smc_grow(x0, y0, x1, y1); return; }
    sm_fill(NSMakeRect(x0, y0, x1 - x0, y1 - y0), smc_col(c), r);
}
static void smc_d_circle(void *u, float x, float y, float r, uint32_t c, float th) {
    if (g_smc_measure) { smc_grow(x - r - th, y - r - th, x + r + th, y + r + th); return; }
    (void)u; NSBezierPath *p = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x - r, y - r, 2 * r, 2 * r)];
    if (th <= 0) { [smc_col(c) setFill]; [p fill]; } else { p.lineWidth = th; [smc_col(c) setStroke]; [p stroke]; }
}
static void smc_d_triangle(void *u, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) {
    if (g_smc_measure) { smc_grow(MIN(x0, MIN(x1, x2)), MIN(y0, MIN(y1, y2)), MAX(x0, MAX(x1, x2)), MAX(y0, MAX(y1, y2))); return; }
    (void)u; NSBezierPath *p = [NSBezierPath bezierPath]; [p moveToPoint:NSMakePoint(x0, y0)]; [p lineToPoint:NSMakePoint(x1, y1)]; [p lineToPoint:NSMakePoint(x2, y2)]; [p closePath];
    [smc_col(c) setFill]; [p fill];
}
static float smc_d_text_width(void *u, float px, const char *s);
static void smc_d_text(void *u, float x, float y, float px, uint32_t c, const char *s) {
    (void)u; if (!s || !*s) return;
    if (g_smc_measure) { smc_grow(x, y, x + smc_d_text_width(u, px, s), y + px * 1.5f); return; }
    [smc_str(s) drawAtPoint:NSMakePoint(x, y) withAttributes:@{NSFontAttributeName: smc_font_px(px), NSForegroundColorAttributeName: smc_col(c)}];
}
static float smc_d_text_width(void *u, float px, const char *s) { (void)u; return s && *s ? (float)[smc_str(s) sizeWithAttributes:@{NSFontAttributeName: smc_font_px(px)}].width : 0; }
static int smc_d_font(void *u, const char *path) {
    (void)u; if (!path || !*path) return 0;
    NSString *p = smc_str(path); NSNumber *have = g_smc_font_ids[p];
    if (have) return have.intValue;
    NSArray *ds = (__bridge_transfer NSArray *)CTFontManagerCreateFontDescriptorsFromURL((__bridge CFURLRef)[NSURL fileURLWithPath:p]);
    int id = 0;
    if (ds.count) { [g_smc_fonts addObject:ds[0]]; id = (int)g_smc_fonts.count; }
    else LOG("slingshot: Squadala!'s font %s could not be loaded: the game's menu font instead", path);
    g_smc_font_ids[p] = @(id);
    return id;
}
static void smc_d_use_font(void *u, int id) { (void)u; g_smc_font_cur = id; }
static int smc_d_image(void *u, int w, int h, const uint8_t *rgba) {
    (void)u; if (w <= 0 || h <= 0 || !rgba) return 0;
    CFDataRef data = CFDataCreate(NULL, rgba, (CFIndex)w * h * 4);
    CGDataProviderRef dp = CGDataProviderCreateWithCFData(data);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef im = CGImageCreate((size_t)w, (size_t)h, 8, 32, (size_t)w * 4, cs, kCGBitmapByteOrderDefault | kCGImageAlphaLast, dp, NULL, true, kCGRenderingIntentDefault);
    CGColorSpaceRelease(cs); CGDataProviderRelease(dp); CFRelease(data);
    if (!im) return 0;
    [g_smc_images addObject:(__bridge_transfer id)im];
    return (int)g_smc_images.count;
}
static void smc_d_draw_image(void *u, int id, float x0, float y0, float x1, float y1, uint32_t tint) {
    (void)u; if (id <= 0 || id > (int)g_smc_images.count) return;
    if (g_smc_measure) { smc_grow(x0, y0, x1, y1); return; }
    CGImageRef im = (__bridge CGImageRef)g_smc_images[id - 1];
    CGContextRef cg = NSGraphicsContext.currentContext.CGContext; CGRect r = CGRectMake(0, 0, x1 - x0, y1 - y0);
    CGContextSaveGState(cg);
    CGContextTranslateCTM(cg, x0, y1); CGContextScaleCTM(cg, 1, -1);   // (the view is flipped; a picture is drawn upright)
    CGContextSetAlpha(cg, (tint >> 24) / 255.0);
    CGContextDrawImage(cg, r, im);
    if ((tint & 0xFFFFFF) != 0xFFFFFF) {   // a tint other than white: the picture's colours times it
        CGContextClipToMask(cg, r, im); CGContextSetBlendMode(cg, kCGBlendModeMultiply);
        CGContextSetRGBFillColor(cg, (tint & 0xff) / 255.0, ((tint >> 8) & 0xff) / 255.0, ((tint >> 16) & 0xff) / 255.0, 1); CGContextFillRect(cg, r);
    }
    CGContextRestoreGState(cg);
}
@interface SmcOverlayView : NSView @end
@implementation SmcOverlayView
- (BOOL)isFlipped { return YES; }
- (NSView *)hitTest:(NSPoint)p { (void)p; return nil; }   // the mouse goes to the game
- (void)drawRect:(NSRect)rect {
    CGContextRef cg = NSGraphicsContext.currentContext.CGContext;
    CGContextClearRect(cg, rect);   // (only the part asked for is redrawn: smc_overlay_tick)
    if (!smc_find_draw() || !g_smc_draw_wanted()) return;
    CGFloat k = self.window.backingScaleFactor ?: 1;
    NSSize z = self.bounds.size;
    CGContextSaveGState(cg); CGContextScaleCTM(cg, 1 / k, 1 / k);   // the module draws in the picture's pixels
    smc_draw_t d = {(float)(z.width * k), (float)(z.height * k), NULL, smc_d_rect, smc_d_circle, smc_d_triangle, smc_d_text, smc_d_text_width,
                    smc_d_font, smc_d_use_font, smc_d_image, smc_d_draw_image};
    g_smc_font_cur = 0;
    if (g_smc_draw2) g_smc_draw2(&d); else g_smc_draw1(&d);
    CGContextRestoreGState(cg);
}
@end
static SmcOverlayView *g_smc_overlay;
static int g_smc_drew;   // the overlay held something at its last draw (one more draw clears it)
static void smc_overlay_tick(void) {
    if (!g_win || !smc_find_draw()) return;
    if (!g_smc_overlay) {
        NSView *cv = g_win.contentView;
        SmcOverlayView *v = [[SmcOverlayView alloc] initWithFrame:cv.bounds];
        v.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable; v.wantsLayer = YES; v.layer.opaque = NO;
        v.layer.drawsAsynchronously = YES;   // (the drawing's commands run off the main thread)
        v.hidden = YES;                      // (shown only while the module draws: a hidden layer is not composited over the game's)
        if (g_sm_view) [cv addSubview:v positioned:NSWindowBelow relativeTo:g_sm_view]; else if (g_sm_fps) [cv addSubview:v positioned:NSWindowBelow relativeTo:g_sm_fps]; else [cv addSubview:v];
        g_smc_overlay = v;
        LOG("slingshot: Squadala!'s drawing over the picture on (%s)", g_smc_draw2 ? "the game look" : "plain");
    }
    // Redrawn 30 times a second only while the module has something to draw. With nothing (no chat lines, no name tags in sight) the view is hidden, so the
    // window server puts the game's picture on screen alone again: a transparent layer over it costs a composite of the whole window each frame
    // The part redrawn is where something was drawn last time and where it is now (a measuring pass of the module's drawing, nothing drawn), so a name tag
    // moving redraws a few hundred pixels, not the window's 3.7 million
    // The view is shown only while that box holds something: the module can want to draw (players in the session) and draw nothing (none in sight)
    static CGRect was;
    static uint64_t ticks, shown, spent_ns, worst_ns; static double next_log;
    const uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    int want = g_smc_draw_wanted() != 0;
    CGFloat k = g_win.backingScaleFactor ?: 1;
    CGRect now = CGRectNull;
    if (want) {
        NSSize z = g_smc_overlay.bounds.size;
        smc_draw_t d = {(float)(z.width * k), (float)(z.height * k), NULL, smc_d_rect, smc_d_circle, smc_d_triangle, smc_d_text, smc_d_text_width,
                        smc_d_font, smc_d_use_font, smc_d_image, smc_d_draw_image};
        g_smc_box = CGRectNull; g_smc_measure = 1; g_smc_font_cur = 0;
        if (g_smc_draw2) g_smc_draw2(&d); else g_smc_draw1(&d);
        g_smc_measure = 0;
        if (!CGRectIsNull(g_smc_box)) now = CGRectInset(CGRectMake(g_smc_box.origin.x / k, g_smc_box.origin.y / k, g_smc_box.size.width / k, g_smc_box.size.height / k), -6, -6);
    }
    CGRect dirty = CGRectUnion(was, now);
    if (!CGRectIsNull(now) && g_smc_overlay.hidden) g_smc_overlay.hidden = NO;
    if (!CGRectIsNull(dirty)) [g_smc_overlay setNeedsDisplayInRect:NSRectFromCGRect(dirty)];   // (what was drawn last time is cleared: one more draw)
    else if (!g_smc_overlay.hidden) g_smc_overlay.hidden = YES;   // nothing drawn now or last time: hidden, so the game's picture is shown alone
    was = now;
    g_smc_drew = !CGRectIsNull(now);
    // Every 10 s, for frame-rate reports: how often the overlay was up, and what the module's drawing took here (the measuring pass, on the main thread)
    const uint64_t dt = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0;
    ticks++, shown += !g_smc_overlay.hidden, spent_ns += dt; if (dt > worst_ns) worst_ns = dt;
    const double t = CACurrentMediaTime();
    if (!next_log) next_log = t + 10;
    else if (t >= next_log) {
        if (shown || spent_ns > ticks * 100000) LOG("slingshot: Squadala!'s overlay over the last %.0f s: up %.0f%% of the time, the module's drawing measured in %.2f ms a tick (max %.2f) on the main thread",
            t - next_log + 10, 100.0 * (double)shown / (double)ticks, (double)spent_ns / 1e6 / (double)ticks, (double)worst_ns / 1e6);
        ticks = shown = spent_ns = worst_ns = 0; next_log = t + 10;
    }
}

// ---- Keys in play (Longshot's patch 0001: the window offers key presses and characters to the module first) ------------------------------------------------
// With the menu closed, every key press goes to the module first (hos_coop_key): its chat box opens with its key (T unless set), and while it types, or any
// other part of it has the keyboard (hos_coop_keys_taken), the game gets no key and the characters go to it (hos_coop_char). The moment the module takes the
// keyboard the game's held keys are let go, so Link does not keep walking. ⌘V pastes while it has the keyboard. The keys held are kept for its key_down
// (slingshot/src/coop/bridge.c), F6 held for the player list among them.
void coop_bridge_key(uint32_t vk, int down);   // slingshot/src/coop/bridge.c
static int smc_play_key(NSEvent *e) {
    int down = e.type == NSEventTypeKeyDown;
    uint32_t vk = smc_vk(e);
    if (!e.isARepeat && vk) coop_bridge_key(vk, down);
    if (g_sm_open || !smc_find() || !g_smc_m.key) return 0;
    smc_find_draw();
    int had = g_smc_keys_taken && g_smc_keys_taken();
    if (!down) return had;   // a key let go while the module has the keyboard: not the game's
    if (e.modifierFlags & NSEventModifierFlagCommand) {
        if (had && vk == 'V') { g_smc_m.key('V', e.isARepeat); return 1; }
        return 0;   // ⌘ shortcuts stay the Mac's
    }
    int took = vk && g_smc_m.key(vk, e.isARepeat);
    int has = g_smc_keys_taken && g_smc_keys_taken();
    if (has && g_smc_m.chr && !(e.modifierFlags & NSEventModifierFlagControl)) {
        NSString *t = e.characters;
        for (NSUInteger i = 0; i < t.length; i++) {
            uint32_t cp = [t characterAtIndex:i];
            if (CFStringIsSurrogateHighCharacter((UniChar)cp) && i + 1 < t.length) cp = CFStringGetLongCharacterForSurrogatePair((UniChar)cp, [t characterAtIndex:++i]);
            if (cp >= 0x20 && cp != 0x7F && !(cp >= 0xF700 && cp <= 0xF8FF)) g_smc_m.chr(cp);
        }
    }
    if (has && !had) {   // the module took the keyboard: the game's keys are let go, and the pointer shows for its clicks (Longshot lets its mouse capture go)
        g_btn = 0; g_w = g_a = g_s = g_d = 0; g_rl = g_ru = g_rr = g_rd = 0; push();
        [NSCursor setHiddenUntilMouseMoves:NO];
    }
    if (took || has) g_smc_overlay.needsDisplay = YES;
    return took || has;
}

// ---- The mouse in play (Longshot's patch 0001: the cheats panel's clicks) ----------------------------------------------------------------------------------
// With the menu closed, the pointer is offered to the module first (hos_coop_mouse), as a fraction of the window's content (the overlay's pixels): each move,
// a left press and the wheel. A press it takes is not a touch of the game's (nor the drag and release after it); a move is the game's too.
static int g_smc_mouse_held;   // a left press the module took: its drag and release are not the game's either
static NSEvent *smc_play_mouse(NSEvent *e) {
    NSEventType t = e.type;
    if (g_smc_mouse_held && (t == NSEventTypeLeftMouseDragged || t == NSEventTypeLeftMouseUp)) { if (t == NSEventTypeLeftMouseUp) g_smc_mouse_held = 0; return nil; }
    if (g_sm_open || !g_win || e.window != g_win || !smc_find() || !g_smc_m.mouse) return e;
    NSView *cv = g_win.contentView; NSPoint p = [cv convertPoint:e.locationInWindow fromView:nil]; NSSize z = cv.bounds.size;
    if (z.width <= 0 || z.height <= 0 || !NSPointInRect(p, cv.bounds)) return e;
    float fx = (float)(p.x / z.width), fy = (float)(1 - p.y / z.height);   // (AppKit's origin is at the bottom left; the module's at the top left)
    if (t == NSEventTypeMouseMoved || t == NSEventTypeLeftMouseDragged) {   // (at most 120 a second: a trackpad sends far more, each one on the main thread)
        static double last; const double at = e.timestamp;
        if (at - last >= 1.0 / 120 || at < last) { last = at; g_smc_m.mouse(fx, fy, 0); }
        return e;
    }
    if (t == NSEventTypeLeftMouseDown) { if (g_smc_m.mouse(fx, fy, 1)) { g_smc_mouse_held = 1; g_smc_overlay.needsDisplay = YES; return nil; } return e; }
    if (t == NSEventTypeScrollWheel && e.scrollingDeltaY != 0) {
        if (g_smc_m.mouse(fx, fy, e.scrollingDeltaY > 0 ? 2 : 3)) { g_smc_overlay.needsDisplay = YES; return nil; }
    }
    return e;
}

static void smc_start(void) {
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskMouseMoved | NSEventMaskLeftMouseDown | NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp | NSEventMaskScrollWheel
                                          handler:^NSEvent *(NSEvent *e) { return smc_play_mouse(e); }];
    NSTimer *t = [NSTimer timerWithTimeInterval:0.1 repeats:YES block:^(NSTimer *x) { (void)x; smc_tick(); }];
    [NSRunLoop.mainRunLoop addTimer:t forMode:NSRunLoopCommonModes];
    NSTimer *d = [NSTimer timerWithTimeInterval:1.0 / 30 repeats:YES block:^(NSTimer *x) { (void)x; smc_overlay_tick(); }];   // the module's drawing over the picture
    [NSRunLoop.mainRunLoop addTimer:d forMode:NSRunLoopCommonModes];
}

// Slingshot's in-game menu for the Mac front end (front_mac.m includes this after the Home prompt's code, whose helpers it shares).
// It looks like the game's own menus: it draws with the game's fonts and UI textures, which dm/slingshot/tools/ui_assets.py makes from the player's own game files
// into ~/Library/Application Support/Slingshot/ui (SLINGSHOT_UI: another folder). Without them it draws the same layout in plain colours and the system font.
// Layout, on a 1280 × 720 canvas fitted into the window: a header with the title; a column of categories on the left (Resume, Graphics, Controls, Mods, Quit Game);
// the chosen category's page on a parchment on the right, rows of settings, each a label and a value, with a line about the chosen one under them; the button
// guide bottom right. The rows are data (sm_row): the mods (slingshot/src/mods.h) add theirs to the Mods page with sm_add(SM_MODS, ...).
// F1 or Esc, a short tap of a controller's Home button (holding it still opens the quit prompt) or Slingshot › Show Slingshot Menu opens it over the game.
// While it is open the game is told it lost the focus (as with the Home prompt) and gets no controller, keyboard or mouse input; after it closes, the controllers
// reach the game again once the buttons that closed it are up.
// Keyboard: ↑ ↓ (W S) choose, ← → (A D) change, Return / Space / J select, Esc / K back, F1 close. Controller: D-pad or left stick, A select, B back, + close.
// Mouse: point to choose, click to select or change (on a value, its left half goes back), right-click back. The guide shows the buttons of what was used last:
// a controller's own (the game's glyphs for A B X Y, the controller's symbols otherwise) or the keys.
// The FPS counter (in the game's number font, in a corner) and the performance overlay are remembered (NSUserDefaults SlingshotFPSCorner, SlingshotMetalHUD).
#pragma once
#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>
#import <QuartzCore/QuartzCore.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>

uint64_t mtl_present_count(void);   // nvn_mtl.m: frames put on screen
int dm_haptics_enabled(void);       // hle_haptics.c
void dm_haptics_set_enabled(int on);

static int g_sm_open, g_sm_settle, g_sm_quit_armed;
static int g_sm_cat, g_sm_row_i, g_sm_top, g_sm_focus;   // the category, the page's row, the first row shown; focus 0 the categories, 1 the page's rows
static int g_sm_pad_used;                                 // a controller was used last (the guide shows its buttons), else the keyboard or the mouse
static NSView *g_sm_view;
static void sm_close(void);
static void sm_redraw(void) { g_sm_view.needsDisplay = YES; }
// Squadala!'s pages (slingshot/src/squadala.h, included below): while g_smc_on the column holds its module's tabs and the page is the module's
static int g_smc_on;
static void smc_add_row(void); static void smc_input(int what); static void smc_tab_step(int d); static void smc_mouse_at(NSPoint p, int click); static void smc_tell(int now);
static void smc_draw_header(void); static void smc_draw_column(void); static void smc_draw_page(void); static void smc_draw_guide(CGFloat right, CGFloat bottom); static void smc_start(void);

// ---- The look -------------------------------------------------------------------------------------------------------------------------------------------
#define SM_W 1280
#define SM_H 720
#define SM_CREAM hq_color(0.949, 0.925, 0.871, 1)
#define SM_INK hq_color(0.173, 0.118, 0.078, 1)          // writing on the parchment
#define SM_INK_SOFT hq_color(0.290, 0.212, 0.137, 1)
enum { SM_REGULAR, SM_MEDIUM, SM_NUMBER, SM_BUTTONS };   // the game's fonts: Cetra Display Pro Regular and Medium (its menus), its HUD digits, its controller glyphs
static NSString *g_sm_dir;
static NSMutableDictionary<NSString *, id> *g_sm_imgs;
static int g_sm_fonts;      // the game's fonts are registered
static CGFloat g_sm_k = 1;  // view points per canvas point, while the menu draws

static void sm_look(void) {
    if (g_sm_imgs) return;
    g_sm_imgs = [NSMutableDictionary new];
    NSString *env = NSProcessInfo.processInfo.environment[@"SLINGSHOT_UI"];
    g_sm_dir = env.length ? env : [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"Slingshot/ui"];
    int n = 0;
    for (NSString *f in @[@"CetraDisplayPro-Regular", @"CetraDisplayPro-Medium", @"NinDev-NumberDeco", @"External"]) {
        NSURL *u = [NSURL fileURLWithPath:[g_sm_dir stringByAppendingFormat:@"/%@.otf", f]];
        CFErrorRef e = NULL;
        if (CTFontManagerRegisterFontsForURL((__bridge CFURLRef)u, kCTFontManagerScopeProcess, &e)) n++;
        else if (e) { if (CFErrorGetCode(e) == kCTFontManagerErrorAlreadyRegistered) n++; CFRelease(e); }
    }
    g_sm_fonts = n == 4;
    LOG("slingshot: menu look from %s: %s", g_sm_dir.UTF8String, g_sm_fonts ? "the game's fonts and textures" : "plain (no game fonts there: run dm/slingshot/tools/ui_assets.py)");
}
static NSFont *sm_font(int f, CGFloat size) {
    static NSString *const names[] = {@"CetraDisplayPro-Regular", @"CetraDisplayPro-Medium", @"NinDevNumber-Oldstyle", @"External"};
    NSFont *r = g_sm_fonts ? [NSFont fontWithName:names[f] size:size] : nil;
    if (r) return r;
    return f == SM_NUMBER ? [NSFont monospacedDigitSystemFontOfSize:size weight:NSFontWeightSemibold] : [NSFont systemFontOfSize:size weight:f == SM_MEDIUM ? NSFontWeightSemibold : NSFontWeightRegular];
}
static NSImage *sm_img(NSString *name) {
    id i = g_sm_imgs[name];
    if (!i) { i = [[NSImage alloc] initWithContentsOfFile:[g_sm_dir stringByAppendingFormat:@"/%@.png", name]] ?: NSNull.null; g_sm_imgs[name] = i; }
    return [i isKindOfClass:NSImage.class] ? i : nil;
}
// One of the look's pictures into r (mirror: flipped left to right); 0 when it is not there, for the caller's plain stand-in
static int sm_image(NSString *name, NSRect r, CGFloat alpha, int mirror) {
    NSImage *i = sm_img(name); if (!i) return 0;
    [NSGraphicsContext saveGraphicsState];
    if (mirror) { NSAffineTransform *t = [NSAffineTransform transform]; [t translateXBy:NSMinX(r) + NSMaxX(r) yBy:0]; [t scaleXBy:-1 yBy:1]; [t concat]; }
    [i drawInRect:r fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:alpha respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
    [NSGraphicsContext restoreGraphicsState];
    return 1;
}
static void sm_fill(NSRect r, NSColor *c, CGFloat radius) { [c setFill]; [[NSBezierPath bezierPathWithRoundedRect:r xRadius:radius yRadius:radius] fill]; }
static NSShadow *sm_shadow(CGFloat k) {
    NSShadow *s = [NSShadow new]; s.shadowColor = [NSColor colorWithWhite:0 alpha:0.7]; s.shadowBlurRadius = 4 * k; s.shadowOffset = NSMakeSize(0, -1.5 * k); return s;
}
// One line of text, its capitals centred on y, x its left end, centre or right end (align 0, 1, 2); wider than room (> 0), a smaller font down to 75 %.
// Returns its width
static CGFloat sm_text(NSString *s, int font, CGFloat size, NSColor *c, CGFloat x, CGFloat y, int align, int shadow, CGFloat room) {
    NSFont *f = sm_font(font, size);
    NSMutableDictionary *a = [@{NSFontAttributeName: f, NSForegroundColorAttributeName: c} mutableCopy];
    CGFloat w = [s sizeWithAttributes:a].width;
    if (room > 0 && w > room) { f = sm_font(font, fmax(size * 0.75, size * room / w)); a[NSFontAttributeName] = f; w = [s sizeWithAttributes:a].width; }
    if (shadow) a[NSShadowAttributeName] = sm_shadow(g_sm_k);
    CGFloat px = align == 1 ? x - w / 2 : align == 2 ? x - w : x;
    [s drawAtPoint:NSMakePoint(px, y + f.capHeight / 2 - f.ascender) withAttributes:a];
    return w;
}
// A paragraph centred in r
static void sm_para(NSString *s, NSRect r, CGFloat size, NSColor *c) {
    NSMutableParagraphStyle *ps = [NSMutableParagraphStyle new]; ps.alignment = NSTextAlignmentCenter;
    NSDictionary *a = @{NSFontAttributeName: sm_font(SM_REGULAR, size), NSForegroundColorAttributeName: c, NSParagraphStyleAttributeName: ps};
    NSRect b = [s boundingRectWithSize:NSMakeSize(r.size.width, CGFLOAT_MAX) options:NSStringDrawingUsesLineFragmentOrigin attributes:a];
    [s drawWithRect:NSMakeRect(r.origin.x, NSMidY(r) - b.size.height / 2, r.size.width, b.size.height) options:NSStringDrawingUsesLineFragmentOrigin attributes:a];
}

// ---- The FPS counter: the frames put on screen over the last second, in the game's HUD digits with a soft shadow and no box, in a corner -------------
static int g_sm_fps_pos;   // 0 off, 1 top left, 2 top right, 3 bottom left, 4 bottom right
static NSTimer *g_sm_fps_timer; static uint64_t g_sm_fps_n[2]; static double g_sm_fps_t[2]; static int g_sm_fps_i;
static void sm_fps_place(void);
@interface SmFpsView : NSView
@property (nonatomic, copy) NSString *value;
@end
@implementation SmFpsView
- (BOOL)isFlipped { return YES; }
- (NSView *)hitTest:(NSPoint)p { (void)p; return nil; }   // the mouse goes to the game
- (void)resizeWithOldSuperviewSize:(NSSize)old { (void)old; sm_fps_place(); }
- (void)drawRect:(NSRect)r {
    (void)r; NSSize z = self.bounds.size; CGFloat k = z.height / 44;
    NSShadow *sh = sm_shadow(k); sh.shadowBlurRadius = 5 * k; sh.shadowColor = [NSColor colorWithWhite:0 alpha:0.75];
    NSFont *nf = sm_font(SM_NUMBER, 30 * k), *lf = sm_font(SM_MEDIUM, 14 * k);
    NSDictionary *na = @{NSFontAttributeName: nf, NSForegroundColorAttributeName: [SM_CREAM colorWithAlphaComponent:0.95], NSShadowAttributeName: sh};
    NSDictionary *la = @{NSFontAttributeName: lf, NSForegroundColorAttributeName: [SM_CREAM colorWithAlphaComponent:0.8], NSShadowAttributeName: sh};
    NSString *n = self.value ?: @"--";
    CGFloat nw = [n sizeWithAttributes:na].width, lw = [@"FPS" sizeWithAttributes:la].width, gap = 5 * k, w = nw + gap + lw, pad = 6 * k;
    CGFloat x = (g_sm_fps_pos == 2 || g_sm_fps_pos == 4) ? z.width - pad - w : pad, base = z.height / 2 + nf.capHeight / 2;   // both on one line, the digits centred
    [n drawAtPoint:NSMakePoint(x, base - nf.ascender) withAttributes:na];
    [@"FPS" drawAtPoint:NSMakePoint(x + nw + gap, base - lf.ascender) withAttributes:la];
}
@end
static SmFpsView *g_sm_fps;
static void sm_fps_place(void) {
    NSView *v = g_sm_fps; if (!v.superview) return;
    NSSize z = v.superview.bounds.size; CGFloat k = hq_scale(z), w = 170 * k, h = 44 * k, m = 14 * k;
    int right = g_sm_fps_pos == 2 || g_sm_fps_pos == 4, top = g_sm_fps_pos == 1 || g_sm_fps_pos == 2;
    v.frame = NSMakeRect(right ? z.width - m - w : m, (top ^ (int)v.superview.isFlipped) ? z.height - m - h : m, w, h);
    v.needsDisplay = YES;
}
static void sm_fps_tick(void) {
    uint64_t n = mtl_present_count(); double t = CACurrentMediaTime();
    int j = (g_sm_fps_i + 1) % 2;   // the sample a second back (one every half second)
    if (g_sm_fps_t[j] > 0 && t > g_sm_fps_t[j]) {
        NSString *s = [NSString stringWithFormat:@"%.0f", (double)(n - g_sm_fps_n[j]) / (t - g_sm_fps_t[j])];
        if (![s isEqualToString:g_sm_fps.value]) { g_sm_fps.value = s; g_sm_fps.needsDisplay = YES; }
    }
    g_sm_fps_i = j; g_sm_fps_n[j] = n; g_sm_fps_t[j] = t;
}
static int sm_fps_get(void) { return g_sm_fps_pos; }
static void sm_fps_set(int pos) {
    g_sm_fps_pos = pos; [NSUserDefaults.standardUserDefaults setInteger:pos forKey:@"SlingshotFPSCorner"];
    if (pos && !g_sm_fps && g_win) {
        SmFpsView *v = [[SmFpsView alloc] initWithFrame:NSZeroRect]; v.wantsLayer = YES;
        NSView *cv = g_win.contentView;
        if (g_sm_view) [cv addSubview:v positioned:NSWindowBelow relativeTo:g_sm_view]; else [cv addSubview:v];
        g_sm_fps = v; sm_fps_place(); hq_fade(v, 1);
        g_sm_fps_t[0] = g_sm_fps_t[1] = 0; g_sm_fps_i = 0; g_sm_fps_n[0] = mtl_present_count(); g_sm_fps_t[0] = CACurrentMediaTime();
        g_sm_fps_timer = [NSTimer timerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *t) { (void)t; sm_fps_tick(); }];
        [NSRunLoop.mainRunLoop addTimer:g_sm_fps_timer forMode:NSRunLoopCommonModes];
    } else if (!pos && g_sm_fps) {
        [g_sm_fps_timer invalidate]; g_sm_fps_timer = nil; hq_fade(g_sm_fps, 0); g_sm_fps = nil;
    } else sm_fps_place();
    static const char *const where[] = {"off", "top left", "top right", "bottom left", "bottom right"};
    LOG("slingshot: FPS counter %s", where[pos < 0 || pos > 4 ? 0 : pos]);
}

// ---- The settings ---------------------------------------------------------------------------------------------------------------------------------------
static int g_sm_hud_on;
static int sm_hud_get(void) { return g_sm_hud_on; }
static void sm_hud_set(int on) {   // Apple's performance overlay (frame rate, frame and GPU time, memory) on the game's layer
    g_sm_hud_on = on; [NSUserDefaults.standardUserDefaults setBool:on forKey:@"SlingshotMetalHUD"];
    if (@available(macOS 13.0, *)) g_layer.developerHUDProperties = on ? @{@"mode": @"default"} : nil;
    LOG("slingshot: performance overlay %s", on ? "on" : "off");
}
static int sm_display_get(void) { return g_fullscreen ? 1 : 0; }
static void sm_display_set(int v) {
    if (!v == !g_fullscreen) return;
    [g_win toggleFullScreen:nil];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1000 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ sm_redraw(); });   // the value once the window is there
}
static int sm_opmode_get(void) { return dm_opmode() ? 1 : 0; }
static void sm_opmode_set(int v) { dm_set_opmode(v); LOG("slingshot: console mode %s", v ? "docked" : "handheld"); }
// What macOS lets the chip run at: Low Power Mode (System Settings › Battery) holds the CPU and GPU clocks down, so the game cannot hold 60 fps; heat
// (thermal state serious or critical) does the same until the Mac cools. Logged at start and on each change (looked at every 5 s), and shown on the
// Graphics page, so a slow run's log says which it was
static int sm_power_battery(void) {
    CFTypeRef info = IOPSCopyPowerSourcesInfo(); if (!info) return 0;
    CFStringRef from = IOPSGetProvidingPowerSourceType(info);
    int battery = from && CFEqual(from, CFSTR(kIOPMBatteryPowerKey));
    CFRelease(info);
    return battery;
}
static NSString *sm_power_text(void) {
    NSProcessInfo *p = NSProcessInfo.processInfo;
    NSString *s = p.lowPowerModeEnabled ? @"Low Power Mode" : sm_power_battery() ? @"Battery" : @"Plugged in";
    return p.thermalState >= NSProcessInfoThermalStateSerious ? [s stringByAppendingString:@", hot"] : s;
}
static NSString *sm_power_note(void) {
    NSProcessInfo *p = NSProcessInfo.processInfo;
    if (p.lowPowerModeEnabled) return @"Low Power Mode is on: macOS slows the chip down and the game cannot hold 60 fps. Turn it off in System Settings › Battery.";
    if (p.thermalState >= NSProcessInfoThermalStateSerious) return @"The Mac is hot: macOS slows the chip down until it cools.";
    return sm_power_battery() ? @"On battery, at full speed while Low Power Mode is off (System Settings › Battery)." : @"Plugged in: full speed.";
}
static void sm_power_check(void) {
    static NSString *was;
    NSProcessInfo *p = NSProcessInfo.processInfo;
    static const char *const heat[] = {"nominal", "fair", "serious", "critical"};
    NSInteger th = p.thermalState;
    NSString *now = [NSString stringWithFormat:@"%s, Low Power Mode %s, thermal state %s", sm_power_battery() ? "on battery" : "plugged in",
                     p.lowPowerModeEnabled ? "on (the CPU and GPU run slower)" : "off", th >= 0 && th <= 3 ? heat[th] : "unknown"];
    if ([now isEqualToString:was]) return;
    LOG("slingshot: power: %s%s", now.UTF8String, was ? " (changed)" : "");
    was = now;
}
static void sm_power_watch(void) {
    sm_power_check();
    NSTimer *t = [NSTimer timerWithTimeInterval:5 repeats:YES block:^(NSTimer *x) { (void)x; sm_power_check(); }];
    [NSRunLoop.mainRunLoop addTimer:t forMode:NSRunLoopCommonModes];
}
static int sm_rumble_get(void) { return dm_haptics_enabled() ? 1 : 0; }
static void sm_rumble_set(int v) { dm_haptics_set_enabled(v); LOG("slingshot: rumble %s", v ? "on" : "off"); }
static GCExtendedGamepad *sm_gamepad(void) { for (GCController *c in GCController.controllers) if (c.extendedGamepad) return c.extendedGamepad; return nil; }
static NSString *sm_pad_name(void) {
    for (GCController *c in GCController.controllers) if (c.extendedGamepad) return c.vendorName.length ? c.vendorName : @"Controller";
    if (dm_switch2_pads()) return @"Switch 2 controller";
    if (g_jc1_pads) return @"Joy-Con";
    return @"Keyboard and mouse";
}
static NSString *sm_pad_note(void) {
    if (sm_gamepad() || dm_switch2_pads() || g_jc1_pads) return @"Slingshot's button prompts follow this controller.";
    return @"Keys: WASD move, the arrows are the D-pad and Shift + arrows the right stick, J K U I are A B X Y, Q E Z C the shoulder buttons, Return + and Tab −.";
}

// ---- Pages and rows -------------------------------------------------------------------------------------------------------------------------------------
// A row of a page: a label and its value. count: its values (options, get gives the one it has, set chooses one); 0 an action (set(0) does it); -1 information,
// what text gives. note: the line under the rows while it is chosen (about: one made when it is shown, instead)
typedef struct {
    const char *label, *note;
    int count;
    const char *options[16];
    int (*get)(void);
    void (*set)(int);
    NSString *(*text)(void);
    NSString *(*about)(void);
} sm_row;
enum { SM_RESUME, SM_GRAPHICS, SM_CONTROLS, SM_MODS, SM_QUIT, SM_CATS };
#define SM_ROWS_MAX 32
#define SM_VIS 5   // rows a page shows at once; more scroll
typedef struct { const char *name, *about; sm_row rows[SM_ROWS_MAX]; int n; } sm_page;
static sm_page g_sm_pages[SM_CATS] = {
    {"Resume", "Back to the game."},
    {"Graphics", "The frame-rate counter, the performance overlay, the window, the console mode and the Mac's power."},
    {"Controls", "Rumble, and the controller Slingshot sees."},
    {"Mods", "Your mods. Changes show the next time the game loads the files they change."},
    {"Quit Game", "Close the game."},
};
// Adds a row to a page: Slingshot's own settings, and a mod's on SM_MODS
static void sm_add(int page, sm_row r) { sm_page *p = &g_sm_pages[page]; if (p->n < SM_ROWS_MAX) p->rows[p->n++] = r; }
static void smm_start(void);
static void sm_pages(void) {
    static int done; if (done) return; done = 1;
    sm_add(SM_GRAPHICS, (sm_row){"FPS counter", "Shows the frame rate in a corner of the screen.", 5, {"Off", "Top left", "Top right", "Bottom left", "Bottom right"}, sm_fps_get, sm_fps_set});
    sm_add(SM_GRAPHICS, (sm_row){"Performance overlay", "Apple's Metal overlay: frame time, GPU time and memory.", 2, {"Off", "On"}, sm_hud_get, sm_hud_set});
    sm_add(SM_GRAPHICS, (sm_row){"Display", "Play in a window or full screen. A window can run lighter: in nxnative's tests macOS clocked the GPU lower in full screen.", 2, {"Window", "Full screen"}, sm_display_get, sm_display_set});
    sm_add(SM_GRAPHICS, (sm_row){"Console mode", "Docked is the TV mode, where the game draws sharper; handheld runs lighter.", 2, {"Handheld", "Docked"}, sm_opmode_get, sm_opmode_set});
    sm_add(SM_GRAPHICS, (sm_row){"Power", NULL, -1, {0}, NULL, NULL, sm_power_text, sm_power_note});
    sm_add(SM_CONTROLS, (sm_row){"Rumble", "Controller vibration.", 2, {"Off", "On"}, sm_rumble_get, sm_rumble_set});
    sm_add(SM_CONTROLS, (sm_row){"Controller", NULL, -1, {0}, NULL, NULL, sm_pad_name, sm_pad_note});
    smc_add_row();   // Squadala!, first on the Mods page when it is installed
    smm_start();   // the mods: their rows on the Mods page, and the files the game reads from them
}
#include "mods.h"
static NSString *sm_value(const sm_row *w) {
    if (w->count < 0) return w->text ? w->text() : @"";
    if (w->count == 0) return w->text ? w->text() : @"";   // an action: its state, when it has one (Squadala!)
    int v = w->get ? w->get() : 0; if (v < 0 || v >= w->count || !w->options[v]) v = 0;
    return @(w->options[v] ?: "");
}

// ---- Moving about ---------------------------------------------------------------------------------------------------------------------------------------
enum { SM_GO_UP, SM_GO_DOWN, SM_GO_LEFT, SM_GO_RIGHT, SM_GO_OK, SM_GO_BACK, SM_GO_CLOSE, SM_GO_CHANGE };
static sm_page *sm_cur(void) { return &g_sm_pages[g_sm_cat]; }
static void sm_set_cat(int c) {
    c = (c + SM_CATS) % SM_CATS;
    if (c != g_sm_cat) { g_sm_quit_armed = 0; g_sm_row_i = g_sm_top = 0; }
    g_sm_cat = c; sm_redraw();
}
static void sm_set_row(int i) {
    sm_page *p = sm_cur(); if (!p->n) return;
    i = (i + p->n) % p->n; g_sm_row_i = i;
    if (i < g_sm_top) g_sm_top = i;
    if (i >= g_sm_top + SM_VIS) g_sm_top = i - SM_VIS + 1;
    sm_redraw();
}
// The chosen row: dir -1 / 1 its previous / next value, 0 select (the next value, or the action)
static void sm_change(int dir) {
    sm_page *p = sm_cur(); if (g_sm_row_i >= p->n) return;
    const sm_row *w = &p->rows[g_sm_row_i];
    if (w->count == 0) { if (!dir && w->set) w->set(0); }
    else if (w->count > 0 && w->get && w->set) w->set(((w->get() + (dir ? dir : 1)) % w->count + w->count) % w->count);
    sm_redraw();
}
// Select on a category: Resume closes the menu, Quit Game asks once more then quits, the others go to their rows
static void sm_activate(void) {
    if (g_sm_cat == SM_RESUME) { sm_close(); return; }
    if (g_sm_cat == SM_QUIT) {
        if (g_sm_quit_armed) { LOG("slingshot: quit from the menu"); [NSApp terminate:nil]; return; }
        g_sm_quit_armed = 1; sm_redraw(); return;
    }
    if (sm_cur()->n) { g_sm_focus = 1; sm_set_row(g_sm_row_i); }
}
static void sm_input(int what) {
    if (!g_sm_open) return;
    if (what == SM_GO_CLOSE) { sm_close(); return; }
    if (g_smc_on) { smc_input(what); return; }
    if (!g_sm_focus) switch (what) {
        case SM_GO_UP: sm_set_cat(g_sm_cat - 1); break;
        case SM_GO_DOWN: sm_set_cat(g_sm_cat + 1); break;
        case SM_GO_RIGHT: if (sm_cur()->n) { g_sm_focus = 1; sm_set_row(g_sm_row_i); } break;
        case SM_GO_OK: sm_activate(); break;
        case SM_GO_BACK: sm_close(); break;
    } else switch (what) {
        case SM_GO_UP: sm_set_row(g_sm_row_i - 1); break;
        case SM_GO_DOWN: sm_set_row(g_sm_row_i + 1); break;
        case SM_GO_LEFT: if (sm_cur()->rows[g_sm_row_i].count > 0) sm_change(-1); else { g_sm_focus = 0; sm_redraw(); } break;
        case SM_GO_RIGHT: sm_change(1); break;
        case SM_GO_OK: sm_change(0); break;
        case SM_GO_BACK: g_sm_focus = 0; sm_redraw(); break;
    }
}

// ---- Drawing --------------------------------------------------------------------------------------------------------------------------------------------
static void sm_fit(NSSize z, CGFloat *s, CGFloat *ox, CGFloat *oy) { *s = fmin(z.width / SM_W, z.height / SM_H); *ox = (z.width - SM_W * *s) / 2; *oy = (z.height - SM_H * *s) / 2; }
static NSRect sm_cat_rect(int i) { return NSMakeRect(85, 130 + i * 90, 280, 76); }
static NSRect sm_row_rect(int k) { return NSMakeRect(455, 200 + k * 66, 560, 52); }
#define SM_VALUE_X 1090   // the centre of a row's value

// The game's glyph for a button the controller labels A, B, X or Y (its sfSymbolsName), else nil
static NSString *sm_game_glyph(NSString *sym) {
    if ([sym hasPrefix:@"a.circle"]) return @"";
    if ([sym hasPrefix:@"b.circle"]) return @"";
    if ([sym hasPrefix:@"x.circle"]) return @"";
    if ([sym hasPrefix:@"y.circle"]) return @"";
    return nil;
}
// A button of the guide, its right end at x: the controller's (the game's glyph, else the controller's symbol) when one was used last, else the key. Returns its width
static CGFloat sm_button(int which, CGFloat x, CGFloat y) {
    GCExtendedGamepad *g = g_sm_pad_used ? sm_gamepad() : nil;
    if (g) {
        NSString *sym = which == SM_GO_OK ? g.buttonA.sfSymbolsName : which == SM_GO_BACK ? g.buttonB.sfSymbolsName : which == SM_GO_CLOSE ? g.buttonMenu.sfSymbolsName : nil;
        NSString *glyph = which == SM_GO_CHANGE ? @"" : sm_game_glyph(sym);   // U+E065: the D-pad, left and right
        if (glyph && g_sm_fonts) return sm_text(glyph, SM_BUTTONS, 30, SM_CREAM, x, y, 2, 1, 0);
        NSImage *im = sym ? [NSImage imageWithSystemSymbolName:sym accessibilityDescription:nil] : nil;
        if (im) {
            NSImageSymbolConfiguration *cfg = [NSImageSymbolConfiguration configurationWithPointSize:24 weight:NSFontWeightMedium];
            if (@available(macOS 12.0, *)) cfg = [cfg configurationByApplyingConfiguration:[NSImageSymbolConfiguration configurationWithHierarchicalColor:SM_CREAM]];
            im = [im imageWithSymbolConfiguration:cfg];
            NSSize z = im.size;
            [im drawInRect:NSMakeRect(x - z.width, y - z.height / 2, z.width, z.height) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
            return z.width;
        }
    }
    NSString *key = which == SM_GO_OK ? @"Return" : which == SM_GO_BACK ? @"Esc" : which == SM_GO_CHANGE ? @"← →" : @"F1";
    CGFloat w = [key sizeWithAttributes:@{NSFontAttributeName: sm_font(SM_REGULAR, 17)}].width + 18, h = 28;
    NSRect r = NSMakeRect(x - w, y - h / 2, w, h);
    NSBezierPath *bp = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, 0.75, 0.75) xRadius:6 yRadius:6]; bp.lineWidth = 1.5;
    [[SM_CREAM colorWithAlphaComponent:0.8] setStroke]; [bp stroke];
    sm_text(key, SM_REGULAR, 17, SM_CREAM, NSMidX(r), y, 1, 0, 0);
    return w;
}
#include "squadala.h"   // Squadala!'s pages, drawn with the helpers above

static void sm_draw_header(void) {
    if (!sm_image(@"tab", NSMakeRect(530, 6, 220, 70), 1, 0)) sm_fill(NSMakeRect(545, 14, 190, 54), hq_color(0.40, 0.31, 0.19, 1), 4);
    sm_text(@"Slingshot", SM_MEDIUM, 30, SM_CREAM, 640, 41, 1, 1, 180);
    sm_image(@"flourish", NSMakeRect(330, 30, 180, 32), 1, 0);
    sm_image(@"flourish", NSMakeRect(770, 30, 180, 32), 1, 1);
}
static void sm_draw_column(void) {
    NSRect col = NSMakeRect(60, 96, 330, 560);
    if (!sm_image(@"column", col, 1, 0)) sm_fill(col, hq_color(0.141, 0.102, 0.067, 0.95), 4);
    for (int i = 0; i < SM_CATS; i++) {
        NSRect r = sm_cat_rect(i);
        if (!sm_image(@"btn", r, 1, 0)) sm_fill(NSInsetRect(r, 8, 8), hq_color(0.404, 0.322, 0.188, 1), 4);
        if (i == g_sm_cat && !sm_image(@"btn_sel", r, g_sm_focus ? 0.45 : 1, 0)) {
            NSBezierPath *b = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, 6, 6) xRadius:4 yRadius:4]; b.lineWidth = 3;
            [[NSColor colorWithWhite:1 alpha:g_sm_focus ? 0.45 : 1] setStroke]; [b stroke];
        }
        sm_text(@(g_sm_pages[i].name), SM_REGULAR, 27, SM_CREAM, NSMidX(r), NSMidY(r), 1, 1, 230);
    }
}
static void sm_draw_page(void) {
    sm_page *p = sm_cur();
    NSRect paper = NSMakeRect(420, 96, 800, 560);
    if (!sm_image(@"parchment", paper, 1, 0)) sm_fill(paper, hq_color(0.478, 0.380, 0.220, 1), 4);
    CGFloat hw = sm_text(@(p->name), SM_MEDIUM, 38, SM_INK, 820, 150, 1, 0, 400);
    sm_image(@"flourish_dark", NSMakeRect(820 - hw / 2 - 134, 139, 120, 22), 1, 0);
    sm_image(@"flourish_dark", NSMakeRect(820 + hw / 2 + 14, 139, 120, 22), 1, 1);
    NSRect note = NSMakeRect(450, 566, 740, 66);
    if (!p->n) {   // a page without rows
        if (g_sm_cat == SM_RESUME) {
            sm_text(@(DM_GAME_NAME), SM_MEDIUM, 30, SM_INK, 820, 330, 1, 0, 700);
            sm_para(@"Select Resume to go back to the game.", NSMakeRect(450, 360, 740, 60), 21, SM_INK_SOFT);
        } else if (g_sm_cat == SM_QUIT) {
            sm_text(g_sm_quit_armed ? @"Select Quit Game again to quit." : @"Quit the game?", SM_MEDIUM, 30, SM_INK, 820, 330, 1, 0, 700);
            sm_para(@"Progress since you last saved in the game is lost.", NSMakeRect(450, 360, 740, 60), 21, SM_INK_SOFT);
        } else {
            sm_text(@"No mods yet", SM_MEDIUM, 30, SM_INK, 820, 330, 1, 0, 700);
            sm_para(@"Put mods in Application Support › Slingshot › mods in your Library folder, then start the game again.", NSMakeRect(450, 360, 740, 60), 21, SM_INK_SOFT);
        }
        return;
    }
    for (int k = 0; k < SM_VIS && g_sm_top + k < p->n; k++) {
        int i = g_sm_top + k, sel = g_sm_focus && i == g_sm_row_i;
        const sm_row *w = &p->rows[i];
        NSRect b = sm_row_rect(k); CGFloat y = NSMidY(b);
        if (!sm_image(sel ? @"bar_sel" : @"bar", b, 1, 0)) sm_fill(b, sel ? hq_color(0.588, 0.471, 0.282, 0.95) : hq_color(0.173, 0.129, 0.082, 0.85), 2);
        sm_text(@(w->label), SM_REGULAR, 25, SM_CREAM, 478, y, 0, 1, 470);
        NSString *v = sm_value(w);
        if (w->count < 0) { sm_text(v, SM_REGULAR, 24, SM_INK, 1196, y, 2, 0, 240); continue; }
        CGFloat vw = sm_text(v, SM_REGULAR, 25, SM_INK, SM_VALUE_X, y, 1, 0, 170), half = fmax(vw / 2, 40);
        if (sel && w->count > 1) {
            if (!sm_image(@"arrow_left", NSMakeRect(SM_VALUE_X - half - 38, y - 16, 32, 32), 1, 0)) sm_text(@"‹", SM_MEDIUM, 30, SM_INK, SM_VALUE_X - half - 12, y, 2, 0, 0);
            if (!sm_image(@"arrow_right", NSMakeRect(SM_VALUE_X + half + 6, y - 16, 32, 32), 1, 0)) sm_text(@"›", SM_MEDIUM, 30, SM_INK, SM_VALUE_X + half + 12, y, 0, 0, 0);
        }
    }
    if (g_sm_top > 0) sm_text(@"▲", SM_REGULAR, 14, SM_INK_SOFT, 735, 186, 1, 0, 0);
    if (g_sm_top + SM_VIS < p->n) sm_text(@"▼", SM_REGULAR, 14, SM_INK_SOFT, 735, 200 + SM_VIS * 66, 1, 0, 0);
    NSString *line = @(p->about);
    if (g_sm_focus && g_sm_row_i < p->n) { const sm_row *w = &p->rows[g_sm_row_i]; line = w->about ? w->about() : w->note ? @(w->note) : @""; }
    sm_para(line, note, 21, SM_INK_SOFT);
}
static void sm_draw_guide(CGFloat right, CGFloat bottom) {
    NSRect g = NSMakeRect(780, 664, right - 780, bottom - 664);
    if (!sm_image(@"guide", g, 1, 0)) sm_fill(g, [NSColor colorWithWhite:0 alpha:0.45], 0);
    int which[2], n = 0; NSString *label[2];
    if (!g_sm_focus) { which[n] = SM_GO_OK; label[n++] = @"Select"; which[n] = SM_GO_BACK; label[n++] = @"Close"; }
    else {
        sm_page *p = sm_cur(); int c = g_sm_row_i < p->n ? p->rows[g_sm_row_i].count : -1;
        if (c > 0) { which[n] = SM_GO_CHANGE; label[n++] = @"Change"; }
        else if (c == 0) { which[n] = SM_GO_OK; label[n++] = @"Select"; }
        which[n] = SM_GO_BACK; label[n++] = @"Back";
    }
    CGFloat x = 1250, y = 692;
    for (int i = n - 1; i >= 0; i--) { x -= sm_button(which[i], x, y) + 10; x -= sm_text(label[i], SM_REGULAR, 23, SM_CREAM, x, y, 2, 1, 0) + 34; }
}
static void sm_draw_menu(NSSize z) {
    CGFloat s, ox, oy; sm_fit(z, &s, &ox, &oy); g_sm_k = s;
    [[NSColor colorWithWhite:0 alpha:0.6] setFill]; NSRectFillUsingOperation((NSRect){NSZeroPoint, z}, NSCompositingOperationSourceOver);
    [hq_color(0.086, 0.063, 0.039, 0.92) setFill]; NSRectFillUsingOperation(NSMakeRect(0, 0, z.width, oy + 70 * s), NSCompositingOperationSourceOver);   // the header's band, the window's width
    [NSGraphicsContext saveGraphicsState];
    NSAffineTransform *t = [NSAffineTransform transform]; [t translateXBy:ox yBy:oy]; [t scaleBy:s]; [t concat];
    if (g_smc_on) { smc_draw_header(); smc_draw_column(); smc_draw_page(); smc_draw_guide((z.width - ox) / s, (z.height - oy) / s); }
    else { sm_draw_header(); sm_draw_column(); sm_draw_page(); sm_draw_guide((z.width - ox) / s, (z.height - oy) / s); }
    [NSGraphicsContext restoreGraphicsState];
}

// ---- The mouse ------------------------------------------------------------------------------------------------------------------------------------------
static NSPoint sm_point(NSView *v, NSEvent *e) {
    NSPoint p = [v convertPoint:e.locationInWindow fromView:nil]; CGFloat s, ox, oy; sm_fit(v.bounds.size, &s, &ox, &oy);
    return NSMakePoint((p.x - ox) / s, (p.y - oy) / s);
}
static void sm_mouse_at(NSPoint p, int click) {
    if (g_smc_on) { smc_mouse_at(p, click); return; }
    for (int i = 0; i < SM_CATS; i++) if (NSPointInRect(p, sm_cat_rect(i))) {
        if (g_sm_focus || g_sm_cat != i) { g_sm_focus = 0; sm_set_cat(i); }
        if (click) sm_activate();
        return;
    }
    sm_page *pg = sm_cur();
    for (int k = 0; k < SM_VIS && g_sm_top + k < pg->n; k++) {
        NSRect r = sm_row_rect(k); r.size.width = 1210 - r.origin.x;   // the row with its value
        if (!NSPointInRect(p, r)) continue;
        if (!g_sm_focus || g_sm_row_i != g_sm_top + k) { g_sm_focus = 1; sm_set_row(g_sm_top + k); }
        if (click) { int c = pg->rows[g_sm_row_i].count; sm_change(c <= 0 ? 0 : p.x >= 990 && p.x < SM_VALUE_X ? -1 : 1); }
        return;
    }
}
static void sm_mouse(NSPoint p, int click) {
    if (!g_sm_open) return;
    if (g_sm_pad_used) { g_sm_pad_used = 0; sm_redraw(); }
    sm_mouse_at(p, click);
}
@interface SmMenuView : NSView @end
@implementation SmMenuView { CGFloat _scroll; }
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { (void)e; return YES; }
- (void)setFrameSize:(NSSize)z { [super setFrameSize:z]; self.needsDisplay = YES; }
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    for (NSTrackingArea *t in self.trackingAreas) [self removeTrackingArea:t];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect owner:self userInfo:nil]];
}
- (void)mouseMoved:(NSEvent *)e { sm_mouse(sm_point(self, e), 0); }
- (void)mouseDown:(NSEvent *)e { sm_mouse(sm_point(self, e), 1); }
- (void)mouseDragged:(NSEvent *)e { (void)e; }
- (void)mouseUp:(NSEvent *)e { (void)e; }
- (void)rightMouseDown:(NSEvent *)e { (void)e; sm_input(SM_GO_BACK); }
- (void)otherMouseDown:(NSEvent *)e { (void)e; }
- (void)scrollWheel:(NSEvent *)e {
    if (!g_sm_focus || (!g_smc_on && sm_cur()->n <= SM_VIS)) return;
    _scroll += e.scrollingDeltaY * (e.hasPreciseScrollingDeltas ? 0.05 : 1);
    if (g_smc_on) {   // Squadala!'s page: its own choosing, a row at a time
        while (_scroll >= 1) { _scroll -= 1; sm_input(SM_GO_UP); }
        while (_scroll <= -1) { _scroll += 1; sm_input(SM_GO_DOWN); }
        return;
    }
    while (_scroll >= 1) { _scroll -= 1; sm_set_row(g_sm_row_i - 1); }
    while (_scroll <= -1) { _scroll += 1; sm_set_row(g_sm_row_i + 1); }
}
- (void)drawRect:(NSRect)r { (void)r; sm_draw_menu(self.bounds.size); }
@end

// ---- Opening and closing, controllers and keys ----------------------------------------------------------------------------------------------------------
static int g_sm_up, g_sm_down, g_sm_left, g_sm_right, g_sm_a, g_sm_b, g_sm_plus;   // controller buttons at the last step (a press is a change from up to down)
// The controllers' buttons the menu reads, ORed over every controller
static void sm_buttons(int *up, int *down, int *left, int *right, int *a, int *b, int *plus) {
    *up = *down = *left = *right = *a = *b = *plus = 0;
    for (GCController *c in GCController.controllers) {
        GCExtendedGamepad *x = c.extendedGamepad; if (!x) continue;
        float sx = x.leftThumbstick.xAxis.value, sy = x.leftThumbstick.yAxis.value;
        *up |= x.dpad.up.isPressed || sy > 0.6f; *down |= x.dpad.down.isPressed || sy < -0.6f;
        *left |= x.dpad.left.isPressed || sx < -0.6f; *right |= x.dpad.right.isPressed || sx > 0.6f;
        *a |= x.buttonA.isPressed; *b |= x.buttonB.isPressed; *plus |= x.buttonMenu.isPressed;
    }
}
static void sm_snapshot(void) { sm_buttons(&g_sm_up, &g_sm_down, &g_sm_left, &g_sm_right, &g_sm_a, &g_sm_b, &g_sm_plus); }

static void sm_open_menu(void) {
    if (g_sm_open || !g_win || hq_blocks_input(g_hq.phase) || cp_blocks_input(g_cp.phase)) return;
    sm_look(); sm_pages();
    g_sm_open = 1; g_sm_settle = 0; g_sm_quit_armed = 0; g_sm_focus = 0; g_sm_cat = SM_RESUME; g_sm_row_i = g_sm_top = 0; g_smc_on = 0;
    g_sm_pad_used = sm_gamepad() != nil;
    g_btn = 0; g_w = g_a = g_s = g_d = 0; g_rl = g_ru = g_rr = g_rd = 0; push();   // nothing the keyboard held stays pressed in the game
    pads_release();
    if (g_mouse_down) { g_mouse_down = 0; dm_touch_set(NULL, 0); }
    sm_snapshot();   // a button already held (the A of a prompt, the Home that opened it) does not count
    dm_focus_set(2);
    NSView *cv = g_win.contentView;
    SmMenuView *v = [[SmMenuView alloc] initWithFrame:cv.bounds];
    v.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable; v.wantsLayer = YES;
    v.layer.drawsAsynchronously = YES;   // (its drawing runs off the main thread, which also paces the game's presents)
    v.accessibilityElement = YES; v.accessibilityRole = NSAccessibilityGroupRole; v.accessibilityLabel = @"Slingshot menu";
    g_sm_view = v; [cv addSubview:v]; hq_fade(v, 1);
    [NSCursor setHiddenUntilMouseMoves:NO];
    LOG("slingshot: menu open");
}
static void sm_close(void) {
    if (!g_sm_open) return;
    g_sm_open = 0; g_sm_settle = 1; g_sm_quit_armed = 0;
    if (g_smc_on) { smc_tell(-1); g_smc_on = 0; }   // Squadala!'s tab shows no more
    hq_fade(g_sm_view, 0); g_sm_view = nil;
    dm_focus_set(1);
    sm_snapshot(); if (!g_sm_up && !g_sm_down && !g_sm_left && !g_sm_right && !g_sm_a && !g_sm_b && !g_sm_plus) { g_sm_settle = 0; pads_reread(); }
    LOG("slingshot: menu closed");
}
static void sm_toggle(void) { if (g_sm_open) sm_close(); else sm_open_menu(); }
static int sm_blocks(void) { return g_sm_open || g_sm_settle; }
static void sm_hq_prompt(void) { if (g_sm_open) sm_close(); g_sm_settle = 0; }   // the Home button's quit prompt opens: it takes over from the menu

// Every controller's button change (pad_read): 1 when the menu has the controllers (open, or the buttons that closed it are still down)
static int sm_pad(void) {
    int up, down, left, right, a, b, plus; sm_buttons(&up, &down, &left, &right, &a, &b, &plus);
    int was_open = g_sm_open;
    if (g_sm_open) {
        int pressed = (up && !g_sm_up) || (down && !g_sm_down) || (left && !g_sm_left) || (right && !g_sm_right) || (a && !g_sm_a) || (b && !g_sm_b) || (plus && !g_sm_plus);
        if (pressed && !g_sm_pad_used) { g_sm_pad_used = 1; sm_redraw(); }
        if (up && !g_sm_up) sm_input(SM_GO_UP);
        if (down && !g_sm_down) sm_input(SM_GO_DOWN);
        if (left && !g_sm_left) sm_input(SM_GO_LEFT);
        if (right && !g_sm_right) sm_input(SM_GO_RIGHT);
        if (a && !g_sm_a) sm_input(SM_GO_OK);
        else if (b && !g_sm_b) sm_input(SM_GO_BACK);
        else if (plus && !g_sm_plus) sm_input(SM_GO_CLOSE);
    } else if (g_sm_settle && !up && !down && !left && !right && !a && !b && !plus) {
        g_sm_settle = 0; pads_reread();   // the game reads the controllers again, as they are now
        g_sm_up = up; g_sm_down = down; g_sm_left = left; g_sm_right = right; g_sm_a = a; g_sm_b = b; g_sm_plus = plus;
        return 1;
    }
    if (g_sm_open || was_open) { g_sm_up = up; g_sm_down = down; g_sm_left = left; g_sm_right = right; g_sm_a = a; g_sm_b = b; g_sm_plus = plus; }
    return was_open || g_sm_settle;
}

// The keyboard (on_key): 1 when the menu took the key. F1 or Esc opens it; while it is open it has every key
static int sm_key(unsigned short c, int down) {
    if (!g_sm_open) {
        if (down && (c == 122 || c == 53)) { sm_open_menu(); g_sm_pad_used = 0; sm_redraw(); return 1; }   // F1, Esc
        return 0;
    }
    if (!down) return 1;
    if (g_sm_pad_used) { g_sm_pad_used = 0; sm_redraw(); }
    switch (c) {
    case 126: case 13: sm_input(SM_GO_UP); break;                           // ↑ W
    case 125: case 1: sm_input(SM_GO_DOWN); break;                          // ↓ S
    case 123: case 0: sm_input(SM_GO_LEFT); break;                          // ← A
    case 124: case 2: sm_input(SM_GO_RIGHT); break;                         // → D
    case 36: case 76: case 49: case 38: sm_input(SM_GO_OK); break;          // Return, Enter, Space, J
    case 53: case 40: case 51: sm_input(SM_GO_BACK); break;                 // Esc, K, Delete
    case 122: sm_input(SM_GO_CLOSE); break;                                 // F1
    case 12: smc_tab_step(-1); break;                                       // Q, E: Squadala!'s previous / next tab
    case 14: smc_tab_step(1); break;
    }
    return 1;
}

// The menu bar's Slingshot menu
@interface SmMenuTarget : NSObject
- (void)toggleMenu:(id)sender;
@end
@implementation SmMenuTarget
- (void)toggleMenu:(id)sender { (void)sender; sm_toggle(); if (g_sm_open) { g_sm_pad_used = 0; sm_redraw(); } }
@end
static SmMenuTarget *g_sm_target;
static NSMenu *sm_menu_bar_menu(void) {
    g_sm_target = [SmMenuTarget new];
    NSMenu *m = [[NSMenu alloc] initWithTitle:@"Slingshot"];
    NSMenuItem *it = [m addItemWithTitle:@"Show Slingshot Menu (F1)" action:@selector(toggleMenu:) keyEquivalent:@""]; it.target = g_sm_target;
    return m;
}
// At start, once the window is up: the look (so the FPS counter has the game's digits), the pages, the remembered FPS counter and performance overlay
void coop_bridge_init(void);   // slingshot/src/coop/bridge.c: co-op's hooks (the module loads at the game's first frame, when it is there)
static void sm_start(void) {
    sm_look(); sm_pages();
    coop_bridge_init();
    smc_start();   // what Squadala!'s module wants of the menu, asked ten times a second
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    NSInteger pos = [d objectForKey:@"SlingshotFPSCorner"] ? [d integerForKey:@"SlingshotFPSCorner"] : [d boolForKey:@"SlingshotShowFPS"] ? 1 : 0;   // phase 1 kept on / off
    if (pos > 0 && pos <= 4) sm_fps_set((int)pos);
    if ([d boolForKey:@"SlingshotMetalHUD"]) sm_hud_set(1);
    sm_power_watch();
    LOG("slingshot: in-game menu ready (F1, Esc or a tap of Home)");
}

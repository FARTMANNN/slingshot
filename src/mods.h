// Slingshot's mods for the Mac front end: slingshot/src/menu.h includes this, and the mods are rows on its Mods page.
// A mod is a folder in ~/Library/Application Support/Slingshot/mods (SLINGSHOT_MODS: another folder) whose files the game reads before its own (a RomFS overlay,
// hle_fs.c dm_rom_overlay_set, which changes while the game runs):
//   link-skins/   Link Skins: skins/<id>/romfs, one skin worn at a time, picked here; skin.json "name" names it, "dev" marks a dev skin. table/romfs holds the
//                 size table over every skin and mod (dm/slingshot/src/size_table.c, made at each start), which the game gets whether a skin is worn or not: the game
//                 reads its table once, at its start, so with it a skin can be put on while the game runs
//   coop-puppet/  co-op's puppet pack (the other players' Links: Squadala!'s tools/puppet_pack, dm/slingshot/tools/squadala_setup.sh): always on, not listed
//   <other>/      a plain RomFS mod: <other>/romfs, switched on or off (mod.json "name": its name in the menu); found as Longshot finds them, up to 4 folders
//                 deep (contents/0400486024DEC000/<mod>/romfs too, as emulators lay them out; size_table.c slingshot_mods_find)
//   contents/0400486024DEC000/coop-puppet → ../../coop-puppet: where Squadala!'s module looks for its pack (dm/slingshot/tools/squadala_setup.sh links it)
// A change shows the next time the game loads the file: Link's body when the save is loaded again or he changes age, his tunics when they are put on.
// Remembered in NSUserDefaults: SlingshotMod.link-skins (on), SlingshotMod.link-skins.skin (the skin's folder name), SlingshotMod.<folder> (on).
// Squadala!'s mods list (its hookshot/src/hos_coop_mods.h, as Longshot's patch 0011 reads it): Squadala!'s module shares this folder (its game folder's
// mods is a link to it) and its Mods tab, the randomizer's seeds (randomizer-<seed>) among them, writes the order and the switches into coop-mods.list:
// a mod the list names is on or off as it says (the list's first row wins a file two mods have), one it does not name keeps Slingshot's switch and comes
// after them, with the pack, by name (the last by name wins, as Longshot). A switch changed here is written into the list too, so the tab shows it.
// coop-mods.applied says what the game reads now, for the tab.
#pragma once
#import <Foundation/Foundation.h>
int slingshot_size_table(const char *mods);   // dm/slingshot/src/size_table.c
int slingshot_size_merge(const char *mods, const char *const *index, int n);
int slingshot_mods_find(const char *mods, char (*out)[128], int max);
#include <unistd.h>

void dm_rom_overlay_set(const char *roots);   // hle_fs.c

#define SMM_SKINS_MAX 15
#define SMM_MODS_MAX 16
typedef struct { char id[64], name[96]; int dev; } smm_skin;
typedef struct { char id[128], name[96]; int on, rank; } smm_mod;   // rank: its place in coop-mods.list (higher first, 0 not listed)
static NSString *g_smm_dir;
static smm_skin g_smm_skin[SMM_SKINS_MAX]; static int g_smm_nskins;
static int g_smm_skins_on, g_smm_worn, g_smm_table;   // Link Skins on; the skin worn (0 the game's own Link, k g_smm_skin[k - 1]); its size table is there
static smm_mod g_smm_mod[SMM_MODS_MAX]; static int g_smm_nmods;
static int g_smm_coop_pack;   // coop-puppet/romfs is there

static NSString *smm_path(NSString *rel) { return [g_smm_dir stringByAppendingPathComponent:rel]; }
static int smm_is_dir(NSString *p) { BOOL d = NO; return [NSFileManager.defaultManager fileExistsAtPath:p isDirectory:&d] && d; }
static NSDictionary *smm_json(NSString *p) {
    NSData *d = [NSData dataWithContentsOfFile:p];
    if (d.length >= 3 && !memcmp(d.bytes, "\xEF\xBB\xBF", 3)) d = [d subdataWithRange:NSMakeRange(3, d.length - 3)];   // a BOM, as Windows editors write
    id j = d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
    return [j isKindOfClass:NSDictionary.class] ? j : nil;
}
static NSString *smm_text(NSDictionary *j, NSString *key) { id v = j[key]; return [v isKindOfClass:NSString.class] && [v length] ? v : nil; }
static int smm_name_ok(NSString *n) { return n.length && ![n hasPrefix:@"."] && ![n hasPrefix:@"_"] && ![n containsString:@":"] && n.length < 64; }   // ':' parts the overlay's folders

// ---- Squadala!'s list ----
#define SMM_LIST_MAX 256
typedef struct { char name[260]; int off; } smm_lrow;
static smm_lrow g_smm_list[SMM_LIST_MAX]; static int g_smm_nlist, g_smm_list_there;
static int smm_same(const char *a, const char *b) {   // as Hookshot compares names: ASCII letters without case, '\' as '/'
    for (;; a++, b++) {
        int x = *a == '\\' ? '/' : tolower((unsigned char)*a), y = *b == '\\' ? '/' : tolower((unsigned char)*b);
        if (x != y) return 0;
        if (!x) return 1;
    }
}
static void smm_list_read(void) {
    g_smm_nlist = 0;
    NSString *t = [NSString stringWithContentsOfFile:smm_path(@"coop-mods.list") encoding:NSUTF8StringEncoding error:nil];
    g_smm_list_there = t != nil;
    NSArray<NSString *> *lines = [t componentsSeparatedByString:@"\n"];
    if (!lines.count || ![[lines[0] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] isEqualToString:@"coop-mods 1"]) return;
    for (NSUInteger i = 1; i < lines.count && g_smm_nlist < SMM_LIST_MAX; i++) {
        NSString *l = [lines[i] stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"\r"]];
        int off;
        if ([l hasPrefix:@"on "] && l.length > 3) off = 0, l = [l substringFromIndex:3];
        else if ([l hasPrefix:@"off "] && l.length > 4) off = 1, l = [l substringFromIndex:4];
        else continue;
        if (strlen(l.UTF8String) >= sizeof g_smm_list[0].name) continue;
        int known = 0; for (int k = 0; k < g_smm_nlist && !known; k++) known = smm_same(g_smm_list[k].name, l.UTF8String);   // a name twice: its first row
        if (known) continue;
        strlcpy(g_smm_list[g_smm_nlist].name, l.UTF8String, sizeof g_smm_list[0].name); g_smm_list[g_smm_nlist++].off = off;
    }
}
static int smm_rank(const char *name) {   // -1 off; 0 not listed; else higher wins
    for (int k = 0; k < g_smm_nlist; k++) if (smm_same(g_smm_list[k].name, name)) return g_smm_list[k].off ? -1 : g_smm_nlist - k;
    return 0;
}
static void smm_list_set(const char *name, int on) {   // a switch changed here: into the list too (only when Squadala! keeps one)
    if (!g_smm_list_there) return;
    smm_list_read();
    int k = 0; while (k < g_smm_nlist && !smm_same(g_smm_list[k].name, name)) k++;
    if (k == g_smm_nlist) { if (k == SMM_LIST_MAX) return; strlcpy(g_smm_list[k].name, name, sizeof g_smm_list[0].name); g_smm_nlist++; }
    g_smm_list[k].off = !on;
    NSMutableString *t = [NSMutableString stringWithString:@"coop-mods 1\n"];
    for (int i = 0; i < g_smm_nlist; i++) [t appendFormat:@"%s %s\n", g_smm_list[i].off ? "off" : "on", g_smm_list[i].name];
    [t writeToFile:smm_path(@"coop-mods.list") atomically:YES encoding:NSUTF8StringEncoding error:nil];
}
// coop-mods.applied: what the game reads now, in Hookshot's order (the last one wins a file), each folder's file count; the mods that are off
static int smm_files(NSString *dir) { int n = 0; NSDirectoryEnumerator *e = [NSFileManager.defaultManager enumeratorAtPath:dir]; for (NSString *f in e) { (void)f; if ([e.fileAttributes.fileType isEqualToString:NSFileTypeRegular]) n++; } return n; }
static void smm_applied(NSArray<NSString *> *roots) {
    if (!g_smm_list_there) return;
    NSMutableString *t = [NSMutableString stringWithFormat:@"coop-mods-applied 1 %lu\n", (unsigned long)getpid()];
    NSString *base = [g_smm_dir stringByAppendingString:@"/"];
    for (NSString *r in roots.reverseObjectEnumerator) {
        NSString *name = [[r hasPrefix:base] ? [r substringFromIndex:base.length] : r stringByDeletingLastPathComponent];   // its folder under the mods, without /romfs
        if ([name isEqualToString:@"coop-puppet"]) name = @"contents/0400486024DEC000/coop-puppet";   // (where Squadala! names its pack)
        [t appendFormat:@"on %d %@\n", smm_files(r), name];
    }
    for (int i = 0; i < g_smm_nmods; i++) if (!g_smm_mod[i].on) [t appendFormat:@"off %s\n", g_smm_mod[i].id];
    [t writeToFile:smm_path(@"coop-mods.applied") atomically:YES encoding:NSUTF8StringEncoding error:nil];
}

// The mods that are on, the first winning a file, as Longshot orders them (Squadala!'s merge_tables.py load_order, reversed): the rows Squadala!'s list
// names, its first row first; then the mods it does not name and the puppet pack, by name without case, the last by name first. The pack is "coop-puppet"
// here and contents/0400486024DEC000/coop-puppet to Squadala!, which is where its name sorts.
static const char *smm_sort_name(const char *id) { return strcmp(id, "coop-puppet") ? id : "contents/0400486024DEC000/coop-puppet"; }
static int smm_fold_cmp(const char *a, const char *b) {
    for (;; a++, b++) { int x = *a == '\\' ? '/' : tolower((unsigned char)*a), y = *b == '\\' ? '/' : tolower((unsigned char)*b); if (x != y || !x) return x - y; }
}
static NSArray<NSString *> *smm_order(void) {
    NSMutableArray<NSString *> *listed = [NSMutableArray new], *rest = [NSMutableArray new];
    for (int i = 0; i < g_smm_nmods; i++) if (g_smm_mod[i].on) [(g_smm_mod[i].rank > 0 ? listed : rest) addObject:@(g_smm_mod[i].id)];   // (g_smm_mod: the list's rows first, by rank)
    if (g_smm_coop_pack) [rest addObject:@"coop-puppet"];
    [rest sortUsingComparator:^NSComparisonResult(NSString *a, NSString *b) { int c = smm_fold_cmp(smm_sort_name(b.UTF8String), smm_sort_name(a.UTF8String)); return c < 0 ? NSOrderedAscending : c > 0 ? NSOrderedDescending : NSOrderedSame; }];
    [listed addObjectsFromArray:rest];
    return listed;
}
// The overlay folders, in order: Link Skins' table (first, so a skin's own table never stands in for it), the worn skin, then the mods in smm_order
static void smm_apply(void) {
    NSMutableArray<NSString *> *roots = [NSMutableArray new];
    if (g_smm_nskins && g_smm_table) [roots addObject:smm_path(@"link-skins/table/romfs")];
    if (g_smm_skins_on && g_smm_worn > 0 && g_smm_worn <= g_smm_nskins) [roots addObject:smm_path([NSString stringWithFormat:@"link-skins/skins/%s/romfs", g_smm_skin[g_smm_worn - 1].id])];
    for (NSString *m in smm_order()) [roots addObject:smm_path([m stringByAppendingPathComponent:@"romfs"])];
    dm_rom_overlay_set([roots componentsJoinedByString:@":"].UTF8String);
    smm_applied(roots);
}

static int smm_on_get(void) { return g_smm_skins_on; }
static void smm_on_set(int v) {
    g_smm_skins_on = v; [NSUserDefaults.standardUserDefaults setBool:v forKey:@"SlingshotMod.link-skins"]; smm_apply();
    LOG("slingshot: Link Skins %s", v ? "on" : "off");
}
static int smm_worn_get(void) { return g_smm_worn; }
static void smm_worn_set(int v) {
    if (v < 0 || v > g_smm_nskins) v = 0;
    g_smm_worn = v;
    [NSUserDefaults.standardUserDefaults setObject:v ? [NSString stringWithUTF8String:g_smm_skin[v - 1].id] : @"" forKey:@"SlingshotMod.link-skins.skin"];
    if (v && !g_smm_skins_on) { g_smm_skins_on = 1; [NSUserDefaults.standardUserDefaults setBool:YES forKey:@"SlingshotMod.link-skins"]; }   // picking a skin puts Link Skins on
    smm_apply();
    LOG("slingshot: Link's skin: %s", v ? g_smm_skin[v - 1].id : "the game's own Link");
}
static NSString *smm_worn_note(void) {
    if (!g_smm_table) return @"The skins have no size table: the log's \"size table\" line says why. Without it the game may stop on a bigger model.";
    if (!g_smm_skins_on) return @"Link Skins is off: Link is the game's own.";
    return @"Link changes when you load your save again or he changes age; his tunics when you put them on again.";
}
static void smm_mod_set(int i, int v) {
    g_smm_mod[i].on = v; [NSUserDefaults.standardUserDefaults setBool:v forKey:[@"SlingshotMod." stringByAppendingString:[NSString stringWithUTF8String:g_smm_mod[i].id]]];
    smm_list_set(g_smm_mod[i].id, v); smm_apply();
    LOG("slingshot: mod %s %s", g_smm_mod[i].id, v ? "on" : "off");
}
#define SMM_TOGGLE(k) static int smm_get##k(void) { return g_smm_mod[k].on; } static void smm_set##k(int v) { smm_mod_set(k, v); }
SMM_TOGGLE(0) SMM_TOGGLE(1) SMM_TOGGLE(2) SMM_TOGGLE(3) SMM_TOGGLE(4) SMM_TOGGLE(5) SMM_TOGGLE(6) SMM_TOGGLE(7) SMM_TOGGLE(8) SMM_TOGGLE(9) SMM_TOGGLE(10) SMM_TOGGLE(11) SMM_TOGGLE(12) SMM_TOGGLE(13) SMM_TOGGLE(14) SMM_TOGGLE(15)
static int (*const smm_gets[SMM_MODS_MAX])(void) = {smm_get0, smm_get1, smm_get2, smm_get3, smm_get4, smm_get5, smm_get6, smm_get7, smm_get8, smm_get9, smm_get10, smm_get11, smm_get12, smm_get13, smm_get14, smm_get15};
static void (*const smm_sets[SMM_MODS_MAX])(int) = {smm_set0, smm_set1, smm_set2, smm_set3, smm_set4, smm_set5, smm_set6, smm_set7, smm_set8, smm_set9, smm_set10, smm_set11, smm_set12, smm_set13, smm_set14, smm_set15};

// ---- Your own Link's tunic files at each start (Squadala!'s colour-agree: its tools/puppet_pack/self_tunic.py, the kits' Set-KitSelfTunic) ----
// The colour the module will wear and send at this start: LICHII_COOP_COLOR, else coop-options.ini's `color`, else coop.ini's (LICHII_COOP_TUNICS=0 or off:
// none). Its nearest tunic's own files (coop_puppet.json self_files: {colour: {romfs path: file beside the pack}}) go into the pack's romfs, and every file
// any colour may put there is taken away first, so no file of an earlier start stays; no colour: green's own (your own Link never wears the game's tunic
// textures, which every Kokiri's model shares: the module repaints yours by their texels). A broken or older pack puts nothing in. As the kits do, before the
// game starts (smm_scan), so your own screen draws the colour the others are sent.
static const struct { const char *name; unsigned rgb; } k_smm_named[] = {   // the colours the module knows by name (lichii/hook/coop_colors.cpp kNamed)
    {"red", 0xFF3030}, {"orange", 0xFF8020}, {"yellow", 0xFFC020}, {"green", 0x30D040}, {"cyan", 0x20D0E0}, {"blue", 0x3070FF}, {"purple", 0xA040FF},
    {"pink", 0xFF60C0}, {"white", 0xFFFFFF}, {"black", 0x202020}, {"brown", 0x8B5A2B}, {"gray", 0xA0A0A0}, {"grey", 0xA0A0A0}};
static const char *const k_smm_tunics[] = {"red", "orange", "yellow", "cyan", "blue", "purple", "pink", "white", "black", "green"};   // the pack's (tunic.COLORS)
static long smm_named(const char *n) { for (size_t i = 0; i < sizeof k_smm_named / sizeof *k_smm_named; i++) if (!strcmp(k_smm_named[i].name, n)) return k_smm_named[i].rgb; return -1; }
static NSArray<NSString *> *smm_match(NSString *pattern, NSString *s) {   // the groups of a whole-string match, or nil
    NSRegularExpression *re = [NSRegularExpression regularExpressionWithPattern:[NSString stringWithFormat:@"^(?:%@)$", pattern] options:0 error:nil];
    NSTextCheckingResult *m = [re firstMatchInString:s options:0 range:NSMakeRange(0, s.length)];
    if (!m) return nil;
    NSMutableArray *g = [NSMutableArray new];
    for (NSUInteger i = 1; i < m.numberOfRanges; i++) [g addObject:[m rangeAtIndex:i].location == NSNotFound ? @"" : [s substringWithRange:[m rangeAtIndex:i]]];
    return g;
}
static long smm_color_parse(NSString *text) {   // CoopColors::Parse: 0xRRGGBB, or -1
    NSString *t = [text ?: @"" stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (t.length >= 2 && ([t hasPrefix:@"\""] || [t hasPrefix:@"'"]) && [t characterAtIndex:t.length - 1] == [t characterAtIndex:0])
        t = [[t substringWithRange:NSMakeRange(1, t.length - 2)] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSString *low = t.lowercaseString; long v;
    if ((v = smm_named(low.UTF8String)) >= 0) return v;
    NSArray<NSString *> *g;
    if ((g = smm_match(@"rgba?\\(\\s*(\\d{1,3})\\s*,\\s*(\\d{1,3})\\s*,\\s*(\\d{1,3})\\s*(?:,[^)]*)?\\)", low))) {
        int r = g[0].intValue, gg = g[1].intValue, b = g[2].intValue;
        return r > 255 || gg > 255 || b > 255 ? -1 : (r << 16) | (gg << 8) | b;
    }
    if ((g = smm_match(@"(?:#|0x)?([0-9a-f]{6})(?:[0-9a-f]{2})?", low))) return strtol(g[0].UTF8String, NULL, 16);
    if ((g = smm_match(@"#([0-9a-f])([0-9a-f])([0-9a-f])", low))) return strtol([NSString stringWithFormat:@"%@%@%@%@%@%@", g[0], g[0], g[1], g[1], g[2], g[2]].UTF8String, NULL, 16);
    return -1;
}
static void smm_ycc(long v, double *y, double *cb, double *cr) {
    double r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
    *y = 0.299 * r + 0.587 * g + 0.114 * b; *cb = 0.564 * (b - *y); *cr = 0.713 * (r - *y);
}
static const char *smm_tunic_of(NSString *text) {   // the tunic preset nearest a colour (CoopColors::TunicOf: chroma counted double); NULL: not a colour
    long rgb = smm_color_parse(text); if (rgb < 0) return NULL;
    double wy, wb, wr; smm_ycc(rgb, &wy, &wb, &wr);
    const char *best = NULL; double best_d = -1;
    for (size_t i = 0; i < sizeof k_smm_tunics / sizeof *k_smm_tunics; i++) {
        double y, b, r; smm_ycc(smm_named(k_smm_tunics[i]), &y, &b, &r);
        double d = (wy - y) * (wy - y) + 2.0 * ((wb - b) * (wb - b) + (wr - r) * (wr - r));
        if (best_d < 0 || d < best_d) best = k_smm_tunics[i], best_d = d;
    }
    return best;
}
static NSString *smm_ini_color(NSString *path) {   // a settings file's `color` (its last such line without a '#' in front), or ""
    NSString *s = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
    if ([s hasPrefix:@"﻿"]) s = [s substringFromIndex:1];
    NSString *v = @"";
    for (NSString *line in [s componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]) {
        NSArray<NSString *> *g = smm_match(@"\\s*color\\s*=\\s*(.*?)\\s*", line);
        if (g) v = g[0];
    }
    return v;
}
static void smm_self_tunic(NSString *coop) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *pack = smm_path(@"coop-puppet"), *romfs = [pack stringByAppendingPathComponent:@"romfs"];
    NSDictionary *env = NSProcessInfo.processInfo.environment;
    NSString *tunics = [env[@"LICHII_COOP_TUNICS"] ?: @"" lowercaseString], *color = @"";
    if (!([tunics isEqualToString:@"0"] || [tunics isEqualToString:@"off"])) {
        color = env[@"LICHII_COOP_COLOR"] ?: @"";
        if (!color.length) color = smm_ini_color([coop stringByAppendingPathComponent:@"coop-options.ini"]);
        if (!color.length) color = smm_ini_color([coop stringByAppendingPathComponent:@"coop.ini"]);
    }
    const char *want = smm_tunic_of(color) ?: "green";
    NSDictionary *manifest = smm_json([pack stringByAppendingPathComponent:@"coop_puppet.json"]);
    NSRegularExpression *ok = [NSRegularExpression regularExpressionWithPattern:@"^(Tex/[A-Za-z0-9_]+\\.bntx\\.zs|Model/Player_(Adult|Child)\\.Player_(Adult|Child)\\.bfres\\.zs)$" options:0 error:nil];
    BOOL (^okp)(id) = ^BOOL(id rel) { return [rel isKindOfClass:NSString.class] && [ok numberOfMatchesInString:rel options:0 range:NSMakeRange(0, [rel length])] == 1; };
    NSMutableOrderedSet<NSString *> *targets = [NSMutableOrderedSet orderedSetWithArray:@[
        @"Tex/Angin_Tile_A_Alb.bntx.zs", @"Tex/Angin_Tile_A_Dark_Alb.bntx.zs", @"Tex/Mt_Vest_Player_Adult_Alb.bntx.zs", @"Tex/Mt_Angin_Acs_Player_Adult_Alb.bntx.zs",
        @"Tex/Mt_Cloth_Parts_Player_Child_Alb.bntx.zs", @"Tex/Mt_Vest_Player_Child_Alb.bntx.zs", @"Model/Player_Adult.Player_Adult.bfres.zs", @"Model/Player_Child.Player_Child.bfres.zs"]];
    NSDictionary *files = [manifest[@"self_files"] isKindOfClass:NSDictionary.class] ? manifest[@"self_files"] : @{};
    for (id per in files.allValues) if ([per isKindOfClass:NSDictionary.class]) for (id rel in per) if (okp(rel)) [targets addObject:rel];
    NSMutableDictionary<NSString *, NSString *> *chosen = [NSMutableDictionary new];
    NSDictionary *tun = [manifest[@"tunics"] isKindOfClass:NSDictionary.class] ? manifest[@"tunics"] : @{};
    id mine = files[@(want)];
    id g = tun[@"green"];   // (as Python's truth: a value there that is not empty, 0 or false)
    BOOL green = g && g != NSNull.null && ([g isKindOfClass:NSNumber.class] ? [g boolValue] : [g respondsToSelector:@selector(count)] ? [g count] > 0 : [g respondsToSelector:@selector(length)] ? [g length] > 0 : YES);
    if (green && [mine isKindOfClass:NSDictionary.class])
        for (id rel in mine) { id src = mine[rel]; if (okp(rel) && [src isKindOfClass:NSString.class] && [src length]) chosen[rel] = [pack stringByAppendingPathComponent:src]; }
    BOOL main_tile = NO;   // a main tile must be there (your own S tile, or an older pack's game-name tile)
    for (NSString *rel in chosen) if (smm_match(@"Tex/Angin_Tile_[A-Z]_Alb\\.bntx\\.zs", rel) && [fm fileExistsAtPath:chosen[rel]]) main_tile = YES;
    if (!main_tile) [chosen removeAllObjects];
    for (NSString *rel in targets) {
        NSString *dst = [romfs stringByAppendingPathComponent:rel];
        if (!chosen[rel] && [fm fileExistsAtPath:dst]) [fm removeItemAtPath:dst error:nil];
    }
    for (NSString *rel in chosen) {
        NSString *dst = [romfs stringByAppendingPathComponent:rel], *src = chosen[rel];
        [fm removeItemAtPath:dst error:nil];
        if ([fm fileExistsAtPath:src]) {
            [fm createDirectoryAtPath:dst.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
            [fm copyItemAtPath:src toPath:dst error:nil];
        }
    }
    LOG("slingshot: Squadala!: your Link's tunic files: %s%s", chosen.count ? want : "none (an older pack: the game's own)", color.length ? "" : " (no colour chosen)");
}

static void smm_scan(void) {
    NSString *env = NSProcessInfo.processInfo.environment[@"SLINGSHOT_MODS"];
    g_smm_dir = env.length ? env : [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"Slingshot/mods"];
    NSFileManager *fm = NSFileManager.defaultManager; NSUserDefaults *ud = NSUserDefaults.standardUserDefaults;
    g_smm_nskins = g_smm_nmods = 0;
    smm_list_read();
    for (NSString *sid in [[fm contentsOfDirectoryAtPath:smm_path(@"link-skins/skins") error:nil] sortedArrayUsingSelector:@selector(compare:)]) {
        if (!smm_name_ok(sid) || !smm_is_dir(smm_path([NSString stringWithFormat:@"link-skins/skins/%@/romfs", sid]))) continue;
        if (g_smm_nskins == SMM_SKINS_MAX) { LOG("slingshot: skin %s left out: at most %d skins", sid.UTF8String, SMM_SKINS_MAX); continue; }
        NSDictionary *j = smm_json(smm_path([NSString stringWithFormat:@"link-skins/skins/%@/skin.json", sid]));
        smm_skin *s = &g_smm_skin[g_smm_nskins++];
        s->dev = smm_text(j, @"dev") != nil;
        NSString *name = smm_text(j, @"name") ?: sid;
        strlcpy(s->id, sid.UTF8String, sizeof s->id); strlcpy(s->name, (s->dev ? [name stringByAppendingString:@" (dev)"] : name).UTF8String, sizeof s->name);
    }
    slingshot_size_table(g_smm_dir.UTF8String);   // the size table over the mods and skins, made again when they changed (dm/slingshot/src/size_table.c)
    g_smm_table = smm_is_dir(smm_path(@"link-skins/table/romfs"));
    g_smm_skins_on = [ud objectForKey:@"SlingshotMod.link-skins"] ? [ud boolForKey:@"SlingshotMod.link-skins"] : 1;
    NSString *worn = [ud stringForKey:@"SlingshotMod.link-skins.skin"]; g_smm_worn = 0;
    for (int k = 0; k < g_smm_nskins; k++) if ([worn isEqualToString:[NSString stringWithUTF8String:g_smm_skin[k].id]]) g_smm_worn = k + 1;
    static char found[64][128];
    int nfound = slingshot_mods_find(g_smm_dir.UTF8String, found, 64);   // every folder holding romfs, as Longshot finds them (Link Skins' folder and the pack's link left out)
    g_smm_coop_pack = 0;
    for (int f = 0; f < nfound; f++) {
        NSString *m = @(found[f]);
        if ([m isEqualToString:@"coop-puppet"]) { g_smm_coop_pack = 1; continue; }   // co-op's puppet pack: always on, not a row
        // (contents/0400486024DEC000/coop-puppet is the link where Squadala!'s module finds the pack; oot-remake-randomizer/ is the randomizer's settings, no files)
        if (g_smm_nmods == SMM_MODS_MAX) { LOG("slingshot: mod %s left out: at most %d mods", m.UTF8String, SMM_MODS_MAX); continue; }
        smm_mod *x = &g_smm_mod[g_smm_nmods++];
        NSString *key = [@"SlingshotMod." stringByAppendingString:m];
        strlcpy(x->id, m.UTF8String, sizeof x->id);
        strlcpy(x->name, (smm_text(smm_json(smm_path([m stringByAppendingPathComponent:@"mod.json"])), @"name") ?: m.lastPathComponent).UTF8String, sizeof x->name);
        x->on = [ud objectForKey:key] ? [ud boolForKey:key] : 1;
        x->rank = smm_rank(x->id);
        if (x->rank) x->on = x->rank > 0;   // Squadala!'s list names it: its switch
    }
    if (g_smm_coop_pack)   // Squadala!'s pack: your own Link's tunic files for this start, before the game reads them
        smm_self_tunic([NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"Slingshot/coop"]);
    // The list's order: its first row first (it wins a file two mods have), the mods it does not name after them by name (stable: they came sorted)
    for (int i = 1; i < g_smm_nmods; i++) {
        smm_mod m = g_smm_mod[i]; int j = i;
        while (j > 0 && (g_smm_mod[j - 1].rank < 0 ? 0 : g_smm_mod[j - 1].rank) < (m.rank < 0 ? 0 : m.rank)) { g_smm_mod[j] = g_smm_mod[j - 1]; j--; }
        g_smm_mod[j] = m;
    }
}
// At start, before the game runs: the mods found, the overlay they make, and their rows on the Mods page
static void smm_start(void) {
    smm_scan();
    if (!(g_smm_nskins && g_smm_table)) {   // no Link Skins table in front: the kit's start merges the table the game will read (size_table.c slingshot_size_merge)
        NSArray<NSString *> *order = smm_order();
        const char *index[64]; int n = 0;
        for (NSString *m in order.reverseObjectEnumerator) if (n < 64) index[n++] = m.UTF8String;   // (the game's index order: the last one wins)
        slingshot_size_merge(g_smm_dir.UTF8String, index, n);
    }
    smm_apply();
    if (g_smm_nskins) {
        sm_add(SM_MODS, (sm_row){"Link Skins", "Your skins for Link. Off: Link is the game's own.", 2, {"Off", "On"}, smm_on_get, smm_on_set});
        sm_row r = {"Link's skin", NULL, 1 + g_smm_nskins, {"Normal Link"}, smm_worn_get, smm_worn_set, NULL, smm_worn_note};
        for (int k = 0; k < g_smm_nskins; k++) r.options[k + 1] = g_smm_skin[k].name;
        sm_add(SM_MODS, r);
    }
    for (int i = 0; i < g_smm_nmods; i++)
        sm_add(SM_MODS, (sm_row){g_smm_mod[i].name, "A RomFS mod: the files it changes show the next time the game loads them.", 2, {"Off", "On"}, smm_gets[i], smm_sets[i]});
    LOG("slingshot: mods from %s: %d skin%s%s, %d other mod%s%s", g_smm_dir.UTF8String, g_smm_nskins, g_smm_nskins == 1 ? "" : "s",
        g_smm_nskins && !g_smm_table ? " (no size table: see the size table line above)" : "", g_smm_nmods, g_smm_nmods == 1 ? "" : "s",
        g_smm_coop_pack ? ", co-op's puppet pack" : "");
}

// The game's resource size table over the mods, made at each start (Slingshot's port of dm/slingshot/tools/mods_table.py; the kits do the same at each start:
// Squadala!'s tools/puppet_pack/merge_tables.py, the Windows kit's Invoke-KitSizeTableMerge).
// The game reads its size table (System/Resource/ResourceSizeTable.*.rsizetable.zs) once, at its start, and a file larger than its entry there may stop it.
// So that a skin can be put on while the game runs, and a randomizer seed built in the game counts at the next start with no script, this writes
// mods/link-skins/table/romfs/System/Resource/<the game's table>: one table over every mod and skin, which Slingshot gives the game first (dm/slingshot/src/mods.h).
// Its rules are Link Skins 0.4.4's (github.com/FARTMANNN/links-skins, src/link_skins.c), as mods_table.py has them:
//   the game's table, raised by every other mod's table (its larger entries kept; randomizer seeds and Squadala!'s puppet pack among them),
//   and by an entry per skin file: a texture's entry is its unpacked size + 280, a model no larger than the game's keeps the game's entry, a larger or
//   new file gets a guess with room to spare; skin.json "sizes" and a table inside a skin raise it further.
// Made again only when something it reads changed (its key, mods/link-skins/table/key.txt: every table's and skin file's size and time), and written only
// when its bytes change. Beside it, coop-merged.txt: Squadala!'s mark that the table holds the puppet pack's entries (else the module shows the other
// players as fairies). It is made after Squadala!'s own merge_tables.py ran (mods.h smm_merge_step: the kits' start merge, which also puts the puppets'
// rows into other mods' actor tables and keeps the pack's menu files out where the table cannot hold them), so it holds what that merge gave each mod.
// Without that tool and without a skin, slingshot_size_merge (below) does the size-table part of that merge: the table the game will read gets every
// other mod's entries. Read with the vendored zstd decoder; written as a zstd frame of raw blocks.
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "../../host/dm.h"
#include "../../host/romfs_image.h"
#include "third_party/zstd/lib/zstd.h"

#define TABLE_DIR "System/Resource"
#define SLINGSHOT_MOD_ID 128
#define SLINGSHOT_MODS_MAX 64
int slingshot_mods_find(const char *mods, char (*out)[SLINGSHOT_MOD_ID], int max);
static void mark_table(const char *mods, const char *win, const char *fname, const char *shown);

// ---- The game's own files (DM_ROMFS: a folder, or a RomFS image file as the app has it) ----
static romfs_img *g_img; static char g_romfs[1024]; static int g_romfs_is_img;
static int rom_open(void) {
    if (g_romfs[0]) return 1;
    const char *e = getenv("DM_ROMFS"), *root = getenv("DM_ROOT");
    if (e && *e) snprintf(g_romfs, sizeof g_romfs, "%s", e);
    else snprintf(g_romfs, sizeof g_romfs, "%s%s/romfs", root ? root : getenv("HOME") ?: "", root ? "" : "/dm-work");
    struct stat st;
    if (stat(g_romfs, &st)) { g_romfs[0] = 0; return 0; }
    if (S_ISREG(st.st_mode)) { g_romfs_is_img = 1; if (!(g_img = romfs_image_open(g_romfs))) { g_romfs[0] = 0; return 0; } }
    return 1;
}
static int64_t img_lookup(const char *rel) {   // the image's entry of a path, or -1
    uint64_t k = 0;
    for (const char *p = rel; *p;) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *end = strchr(p, '/'); if (!end) end = p + strlen(p);
        size_t len = (size_t)(end - p);
        if (g_img->e[k].type) return -1;
        uint64_t lo = g_img->e[k].a, hi = lo + g_img->e[k].b; int64_t found = -1;
        while (lo < hi) {   // a directory's children are sorted by name, the shorter first on a common start
            uint64_t mid = lo + (hi - lo) / 2; const img_ent *x = &g_img->e[mid];
            size_t l = x->nlen < len ? x->nlen : len; int c = memcmp(g_img->names + x->name, p, l);
            if (!c) c = x->nlen < len ? -1 : x->nlen > len ? 1 : 0;
            if (!c) { found = (int64_t)mid; break; }
            if (c < 0) lo = mid + 1; else hi = mid;
        }
        if (found < 0) return -1;
        k = (uint64_t)found; p = end;
    }
    return (int64_t)k;
}
// A game file's size and its first bytes (up to n); 0 when the game has no such file
static int rom_head(const char *rel, uint8_t *head, size_t n, uint64_t *size) {
    if (!rom_open()) return 0;
    if (g_romfs_is_img) {
        int64_t k = img_lookup(rel); if (k < 0 || !g_img->e[k].type) return 0;
        *size = g_img->e[k].b;
        return !n || romfs_image_read(g_img->fd, head, n < *size ? n : (size_t)*size, (off_t)g_img->e[k].a);
    }
    char p[2048]; snprintf(p, sizeof p, "%s/%s", g_romfs, rel);
    struct stat st; if (stat(p, &st) || !S_ISREG(st.st_mode)) return 0;
    *size = (uint64_t)st.st_size;
    if (!n) return 1;
    FILE *f = fopen(p, "rb"); if (!f) return 0;
    size_t got = fread(head, 1, n, f); fclose(f);
    return got > 0;
}
static uint8_t *rom_read(const char *rel, size_t *n) {   // a whole game file (malloc'd), or NULL
    uint64_t size = 0; uint8_t one;
    if (!rom_head(rel, &one, 0, &size) || !size || size > (256u << 20)) return NULL;
    uint8_t *b = malloc(size); if (!b) return NULL;
    if (!rom_head(rel, b, size, &size)) { free(b); return NULL; }
    *n = size; return b;
}
static int rom_table_name(char *out, size_t cap) {   // the game's table's file name (the first by name, as mods_table.py takes it)
    if (!rom_open()) return 0;
    out[0] = 0;
    if (g_romfs_is_img) {
        int64_t k = img_lookup(TABLE_DIR); if (k < 0 || g_img->e[k].type) return 0;
        for (uint64_t c = g_img->e[k].a; c < g_img->e[k].a + g_img->e[k].b; c++) {   // (children sorted: the first match is the first by name)
            const img_ent *x = &g_img->e[c]; char nm[0x301]; snprintf(nm, sizeof nm, "%.*s", (int)x->nlen, g_img->names + x->name);
            size_t l = strlen(nm);
            if (x->type && !strncmp(nm, "ResourceSizeTable.", 18) && l > 14 && !strcmp(nm + l - 14, ".rsizetable.zs")) { snprintf(out, cap, "%s", nm); return 1; }
        }
        return 0;
    }
    char p[2048]; snprintf(p, sizeof p, "%s/%s", g_romfs, TABLE_DIR);
    DIR *d = opendir(p); if (!d) return 0;
    for (struct dirent *e; (e = readdir(d));) {
        size_t l = strlen(e->d_name);
        if (!strncmp(e->d_name, "ResourceSizeTable.", 18) && l > 14 && !strcmp(e->d_name + l - 14, ".rsizetable.zs") && (!out[0] || strcmp(e->d_name, out) < 0)) snprintf(out, cap, "%s", e->d_name);
    }
    closedir(d);
    return out[0] != 0;
}

// ---- A size table (RESTBL): crc32 rows and named rows ----
typedef struct { uint32_t version, name_len; uint32_t *key, *val; size_t cap, n; char *names; uint32_t *nsize; size_t nn; } table;
static uint32_t crc32_of(const char *s) {
    static uint32_t t[256]; static int made;
    if (!made) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; } made = 1; }
    uint32_t c = 0xFFFFFFFFu; for (; *s; s++) c = t[(c ^ (uint8_t)*s) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static size_t slot(const table *t, uint32_t k) { return (size_t)(k * 2654435761u) & (t->cap - 1); }
static uint32_t *row(table *t, uint32_t k, int make) {   // its size, or NULL (make: a new row of 0)
    if (make && (t->n + 1) * 2 > t->cap) {
        size_t oc = t->cap; uint32_t *ok = t->key, *ov = t->val;
        t->cap = t->cap ? t->cap * 2 : 1 << 16;
        t->key = calloc(t->cap, 4); t->val = calloc(t->cap, 4);
        for (size_t i = 0; i < t->cap; i++) t->val[i] = UINT32_MAX;   // an empty slot (a size is never UINT32_MAX: clamp32 and the game's tables stay below)
        for (size_t i = 0; i < oc; i++) if (ov[i] != UINT32_MAX) { size_t s = slot(t, ok[i]); while (t->val[s] != UINT32_MAX) s = (s + 1) & (t->cap - 1); t->key[s] = ok[i]; t->val[s] = ov[i]; }
        free(ok); free(ov);
    }
    if (!t->cap) return NULL;
    for (size_t s = slot(t, k);; s = (s + 1) & (t->cap - 1)) {
        if (t->val[s] == UINT32_MAX) { if (!make) return NULL; t->key[s] = k; t->val[s] = 0; t->n++; return &t->val[s]; }
        if (t->key[s] == k) return &t->val[s];
    }
}
static void table_free(table *t) { free(t->key); free(t->val); free(t->names); free(t->nsize); memset(t, 0, sizeof *t); }
static int table_parse(table *t, const uint8_t *z, size_t zn) {   // a .rsizetable.zs's bytes
    memset(t, 0, sizeof *t);
    unsigned long long dn = ZSTD_getFrameContentSize(z, zn);
    if (dn == ZSTD_CONTENTSIZE_ERROR || dn == ZSTD_CONTENTSIZE_UNKNOWN || dn > (256u << 20) || dn < 0x16) return 0;
    uint8_t *d = malloc((size_t)dn); if (!d) return 0;
    size_t got = ZSTD_decompress(d, (size_t)dn, z, zn);
    if (ZSTD_isError(got) || got < 0x16 || memcmp(d, "RESTBL", 6)) { free(d); return 0; }
    uint32_t h[4]; memcpy(h, d + 6, 16);
    t->version = h[0]; t->name_len = h[1];
    uint64_t nrows = h[2], nnamed = h[3], need = 0x16 + nrows * 8 + nnamed * ((uint64_t)t->name_len + 4);
    if (need > got || t->name_len == 0 || t->name_len > 1024) { free(d); return 0; }
    for (uint64_t i = 0; i < nrows; i++) { uint32_t kv[2]; memcpy(kv, d + 0x16 + i * 8, 8); uint32_t *v = row(t, kv[0], 1); if (kv[1] > *v) *v = kv[1]; }
    t->nn = nnamed; t->names = calloc(nnamed ? nnamed : 1, t->name_len + 1); t->nsize = calloc(nnamed ? nnamed : 1, 4);
    const uint8_t *q = d + 0x16 + nrows * 8;
    for (uint64_t i = 0; i < nnamed; i++, q += t->name_len + 4) { memcpy(t->names + i * (t->name_len + 1), q, t->name_len); memcpy(&t->nsize[i], q + t->name_len, 4); }
    free(d);
    return 1;
}
static long named_at(const table *t, const char *key) { for (size_t i = 0; i < t->nn; i++) if (!strncmp(t->names + i * (t->name_len + 1), key, t->name_len)) return (long)i; return -1; }
static uint32_t table_get(table *t, const char *key) { long i = named_at(t, key); if (i >= 0) return t->nsize[i]; uint32_t *v = row(t, crc32_of(key), 0); return v ? *v : 0; }
static void table_raise(table *t, const char *key, uint32_t size) {
    long i = named_at(t, key);
    if (i >= 0) { if (size > t->nsize[i]) t->nsize[i] = size; return; }
    uint32_t *v = row(t, crc32_of(key), 1); if (size > *v) *v = size;
}
// Every entry of the other kept (the larger size where both have one), its named rows added as named rows: merge_tables.py's merge(). Rows added, raised
static void table_union(table *t, const table *o, int *added, int *raised) {
    for (size_t s = 0; s < o->cap; s++) if (o->val[s] != UINT32_MAX) {
        uint32_t *v = row(t, o->key[s], 0);
        if (!v) { *row(t, o->key[s], 1) = o->val[s]; (*added)++; } else if (o->val[s] > *v) { *v = o->val[s]; (*raised)++; }
    }
    for (size_t i = 0; i < o->nn; i++) {
        const char *nm = o->names + i * (o->name_len + 1);
        char key[1100]; snprintf(key, sizeof key, "%.*s", (int)(o->name_len < 1099 ? o->name_len : 1099), nm);
        long j = named_at(t, key);
        if (j >= 0) { if (o->nsize[i] > t->nsize[j]) { t->nsize[j] = o->nsize[i]; (*raised)++; } continue; }
        t->names = realloc(t->names, (t->nn + 1) * (t->name_len + 1)); t->nsize = realloc(t->nsize, (t->nn + 1) * 4);
        memset(t->names + t->nn * (t->name_len + 1), 0, t->name_len + 1);
        memcpy(t->names + t->nn * (t->name_len + 1), key, strlen(key) < t->name_len ? strlen(key) : t->name_len);
        t->nsize[t->nn++] = o->nsize[i]; (*added)++;
    }
}
// How many of the pack's entries t lacks or holds smaller (0: it holds them all; a named row of the pack may sit in t as a crc row): missing_pack_entries()
static int table_missing(table *t, table *pack) {
    int missing = 0;
    for (size_t s = 0; s < pack->cap; s++) if (pack->val[s] != UINT32_MAX) { uint32_t *v = row(t, pack->key[s], 0); if (!v || *v < pack->val[s]) missing++; }
    for (size_t i = 0; i < pack->nn; i++) {
        char key[1100]; snprintf(key, sizeof key, "%.*s", (int)(pack->name_len < 1099 ? pack->name_len : 1099), pack->names + i * (pack->name_len + 1));
        long j = named_at(t, key); uint32_t *v = row(t, crc32_of(key), 0);
        int64_t have = j >= 0 ? (int64_t)t->nsize[j] : -1; if (v && (int64_t)*v > have) have = *v;
        if (have < (int64_t)pack->nsize[i]) missing++;
    }
    return missing;
}
static int cmp_u32(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
static uint8_t *table_bytes(table *t, size_t *out_n) {   // RESTBL, rows by crc, then a zstd frame of raw blocks around it (malloc'd)
    uint32_t *keys = malloc((t->n ? t->n : 1) * 4); size_t k = 0;
    for (size_t s = 0; s < t->cap; s++) if (t->val[s] != UINT32_MAX) keys[k++] = t->key[s];
    qsort(keys, k, 4, cmp_u32);
    size_t raw = 0x16 + k * 8 + t->nn * (t->name_len + 4);
    uint8_t *d = calloc(1, raw);
    memcpy(d, "RESTBL", 6); uint32_t h[4] = {t->version, t->name_len, (uint32_t)k, (uint32_t)t->nn}; memcpy(d + 6, h, 16);
    for (size_t i = 0; i < k; i++) { uint32_t kv[2] = {keys[i], *row(t, keys[i], 0)}; memcpy(d + 0x16 + i * 8, kv, 8); }
    uint8_t *q = d + 0x16 + k * 8;
    for (size_t i = 0; i < t->nn; i++, q += t->name_len + 4) { memcpy(q, t->names + i * (t->name_len + 1), t->name_len); memcpy(q + t->name_len, &t->nsize[i], 4); }
    free(keys);
    size_t nblocks = raw ? (raw + (128u << 10) - 1) / (128u << 10) : 1;
    uint8_t *z = malloc(9 + nblocks * 3 + raw), *w = z;
    const uint8_t fh[5] = {0x28, 0xB5, 0x2F, 0xFD, 0xA0};   // zstd's magic; single segment, a 4-byte content size, no checksum
    memcpy(w, fh, 5); uint32_t cs = (uint32_t)raw; memcpy(w + 5, &cs, 4); w += 9;
    for (size_t at = 0; at < raw || !raw;) {   // raw blocks of at most 128 KB, the last one marked
        size_t bn = raw - at > (128u << 10) ? (128u << 10) : raw - at; int last = at + bn >= raw;
        uint32_t bh = (uint32_t)(last | (0 << 1) | (bn << 3));
        w[0] = (uint8_t)bh, w[1] = (uint8_t)(bh >> 8), w[2] = (uint8_t)(bh >> 16); w += 3;
        memcpy(w, d + at, bn); w += bn;
        at += bn; if (!raw) break;
    }
    free(d);
    *out_n = (size_t)(w - z);
    return z;
}
static uint8_t *file_read(const char *p, size_t *n);
// A file written whole (a .new file, then renamed), or left as it is when it holds those bytes already (its time kept: Squadala! stamps seeds by tables' bytes)
static int write_if_changed(const char *path, const uint8_t *b, size_t n) {
    size_t on = 0; uint8_t *old = file_read(path, &on);
    int same = old && on == n && !memcmp(old, b, n);
    free(old);
    if (same) return 1;
    char tmp[2100]; snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *f = fopen(tmp, "wb"); if (!f) return 0;
    int ok = fwrite(b, 1, n, f) == n;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path)) { remove(tmp); return 0; }
    return 1;
}
static int table_write(table *t, const char *path) { size_t n = 0; uint8_t *b = table_bytes(t, &n); int ok = write_if_changed(path, b, n); free(b); return ok; }

// ---- Files ----
static uint8_t *file_read(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    if (l <= 0 || l > (256 << 20)) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)l); size_t got = b ? fread(b, 1, (size_t)l, f) : 0; fclose(f);
    if (got != (size_t)l) { free(b); return NULL; }
    *n = got; return b;
}
static uint64_t zs_content(const uint8_t *head, size_t n) {   // the bytes a .zs frame says it unpacks to; 0 when it does not say
    unsigned long long c = ZSTD_getFrameContentSize(head, n);
    return c == ZSTD_CONTENTSIZE_ERROR || c == ZSTD_CONTENTSIZE_UNKNOWN ? 0 : c;
}
static uint64_t content_size(const char *p) {   // a .zs file's unpacked size; another file: its size
    struct stat st; if (stat(p, &st)) return 0;
    size_t l = strlen(p);
    if (l < 3 || strcmp(p + l - 3, ".zs")) return (uint64_t)st.st_size;
    uint8_t h[18] = {0}; FILE *f = fopen(p, "rb"); if (!f) return 0;
    size_t got = fread(h, 1, sizeof h, f); fclose(f);
    return zs_content(h, got);
}
static uint32_t clamp32(uint64_t v) { return v > 0xFFFFFFF0u ? 0xFFFFFFF0u : (uint32_t)((v + 7) & ~(uint64_t)7); }
static uint32_t needed_size(const char *key, uint64_t mine, uint32_t base, uint64_t orig) {
    size_t l = strlen(key);
    if (l > 5 && !strcmp(key + l - 5, ".bntx")) return clamp32(mine + 280);
    if (base && orig && mine <= orig) return base;
    if (base && orig) return clamp32(base + (mine - orig) + (mine - orig) / 4 + 0x2000);
    return clamp32(mine + mine / 4 + 0x20000);
}
typedef struct { char **v; size_t n, cap; } strs;
static void strs_add(strs *s, const char *x) { if (s->n == s->cap) s->v = realloc(s->v, (s->cap = s->cap ? 2 * s->cap : 64) * sizeof *s->v); s->v[s->n++] = strdup(x); }
static void strs_free(strs *s) { for (size_t i = 0; i < s->n; i++) free(s->v[i]); free(s->v); memset(s, 0, sizeof *s); }
static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void walk(const char *root, const char *rel, strs *out) {   // every file under root (names from '.' left out), as paths relative to it
    char p[2048]; snprintf(p, sizeof p, "%s%s%s", root, *rel ? "/" : "", rel);
    DIR *d = opendir(p); if (!d) return;
    for (struct dirent *e; (e = readdir(d));) {
        if (e->d_name[0] == '.') continue;
        char r[2048]; snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", e->d_name);
        char q[2100]; snprintf(q, sizeof q, "%s/%s", root, r); struct stat st;
        if (stat(q, &st)) continue;
        if (S_ISDIR(st.st_mode)) walk(root, r, out); else if (S_ISREG(st.st_mode)) strs_add(out, r);
    }
    closedir(d);
}
static int is_table(const char *rel) { size_t l = strlen(rel); return !strncmp(rel, TABLE_DIR "/", sizeof TABLE_DIR) && l > 14 && !strcmp(rel + l - 14, ".rsizetable.zs"); }
static void key_add(char *key, size_t cap, const char *p) {   // the make-again key: a file's path, size and time
    struct stat st; size_t l = strlen(key);
    if (!stat(p, &st)) snprintf(key + l, cap - l, "%s %lld %lld\n", p, (long long)st.st_size, (long long)st.st_mtimespec.tv_sec);
}
// skin.json "sizes": {"<key>": n, ...}, read plainly (the file is small; the Link Skins format)
static uint32_t json_size(const char *json, const char *key) {
    if (!json) return 0;
    const char *s = strstr(json, "\"sizes\""); if (!s) return 0;
    char k[1100]; snprintf(k, sizeof k, "\"%s\"", key);
    const char *at = strstr(s, k); if (!at) return 0;
    at = strchr(at + strlen(k), ':'); if (!at) return 0;
    return (uint32_t)strtoul(at + 1, NULL, 10);
}

// Run at each start (dm/slingshot/src/mods.h smm_scan), before the game reads its table. Logs what it did; 1 when a table is there for the game
int slingshot_size_table(const char *mods) {
    char name[512], out[2048], keyp[2048];
    if (!rom_table_name(name, sizeof name)) { LOG("slingshot: size table: the game's table is not in its files (%s): none made", g_romfs[0] ? g_romfs : "no DM_ROMFS"); return 0; }
    char skins_dir[2048]; snprintf(skins_dir, sizeof skins_dir, "%s/link-skins/skins", mods);
    snprintf(out, sizeof out, "%s/link-skins/table/romfs/" TABLE_DIR "/%s", mods, name);
    snprintf(keyp, sizeof keyp, "%s/link-skins/table/key.txt", mods);
    // What it reads: the game's table, every other mod's table, every skin's files
    strs mods_with = {0}, skins = {0};
    {   // every mod Longshot would find (slingshot_mods_find), Link Skins' own folders left out, that has a size-table folder
        static char found[SLINGSHOT_MODS_MAX][SLINGSHOT_MOD_ID];
        int nf = slingshot_mods_find(mods, found, SLINGSHOT_MODS_MAX);
        for (int i = 0; i < nf; i++) {
            char r[2048]; snprintf(r, sizeof r, "%s/%s/romfs/" TABLE_DIR, mods, found[i]); struct stat st;
            if (!stat(r, &st) && S_ISDIR(st.st_mode)) strs_add(&mods_with, found[i]);
        }
    }
    DIR *d = opendir(skins_dir);
    for (struct dirent *e; d && (e = readdir(d));) {
        if (e->d_name[0] == '.' || e->d_name[0] == '_') continue;
        char r[2048]; snprintf(r, sizeof r, "%s/%s/romfs", skins_dir, e->d_name); struct stat st;
        if (!stat(r, &st) && S_ISDIR(st.st_mode)) strs_add(&skins, e->d_name);
    }
    if (d) closedir(d);
    if (mods_with.n) qsort(mods_with.v, mods_with.n, sizeof *mods_with.v, cmp_str);
    if (skins.n) qsort(skins.v, skins.n, sizeof *skins.v, cmp_str);
    if (!skins.n) { strs_free(&mods_with); strs_free(&skins); return 0; }   // no skin: each mod's own table stands, as before (mods_table.py writes none)
    size_t kcap = 1 << 20; char *key = calloc(1, kcap);
    snprintf(key, kcap, "slingshot size table 2\n%s %s\n", g_romfs, name);
    for (size_t i = 0; i < mods_with.n; i++) {
        char r[2048]; snprintf(r, sizeof r, "%s/%s/romfs/" TABLE_DIR, mods, mods_with.v[i]);
        DIR *t = opendir(r);
        for (struct dirent *e; t && (e = readdir(t));) { size_t l = strlen(e->d_name); if (l > 14 && !strcmp(e->d_name + l - 14, ".rsizetable.zs")) { char q[2400]; snprintf(q, sizeof q, "%s/%s", r, e->d_name); key_add(key, kcap, q); } }
        if (t) closedir(t);
    }
    strs files[64] = {{0}}; size_t ns = skins.n < 64 ? skins.n : 64;
    for (size_t i = 0; i < ns; i++) {
        char r[2048]; snprintf(r, sizeof r, "%s/%s/romfs", skins_dir, skins.v[i]);
        walk(r, "", &files[i]); if (files[i].n) qsort(files[i].v, files[i].n, sizeof *files[i].v, cmp_str);
        for (size_t j = 0; j < files[i].n; j++) { char q[4200]; snprintf(q, sizeof q, "%s/%s", r, files[i].v[j]); key_add(key, kcap, q); }
        char js[2100]; snprintf(js, sizeof js, "%s/%s/skin.json", skins_dir, skins.v[i]); key_add(key, kcap, js);
    }
    size_t okn = 0; uint8_t *old = file_read(keyp, &okn); struct stat ost;
    int same = old && okn == strlen(key) && !memcmp(old, key, okn) && !stat(out, &ost);
    free(old);
    int made = 0;
    if (same) LOG("slingshot: size table over %zu skin(s) and %zu mod table(s): unchanged since it was made", ns, mods_with.n);
    else {
        size_t zn = 0; char rel[600]; snprintf(rel, sizeof rel, TABLE_DIR "/%s", name);
        uint8_t *z = rom_read(rel, &zn);
        table t;
        if (!z || !table_parse(&t, z, zn)) LOG("slingshot: [RED] size table: the game's %s could not be read: none made", name);
        else {
            for (size_t i = 0; i < mods_with.n; i++) {   // the other mods' tables: their larger entries are kept
                char r[2048]; snprintf(r, sizeof r, "%s/%s/romfs/" TABLE_DIR, mods, mods_with.v[i]);
                DIR *td = opendir(r);
                for (struct dirent *e; td && (e = readdir(td));) {
                    size_t l = strlen(e->d_name); if (l <= 14 || strcmp(e->d_name + l - 14, ".rsizetable.zs")) continue;
                    char q[2400]; snprintf(q, sizeof q, "%s/%s", r, e->d_name);
                    size_t on = 0; uint8_t *ob = file_read(q, &on); table o;
                    int added = 0, raised = 0;   // (every row of it, named rows too, as the kit's merge_tables.py: the pack's named rows are its puppets' files)
                    if (ob && table_parse(&o, ob, on)) { table_union(&t, &o, &added, &raised); table_free(&o); LOG("slingshot: size table: the larger entries of mods/%s are kept", mods_with.v[i]); }
                    free(ob);
                }
                if (td) closedir(td);
            }
            for (size_t i = 0; i < ns; i++) {
                char r[2048]; snprintf(r, sizeof r, "%s/%s/romfs", skins_dir, skins.v[i]);
                char js[2100]; snprintf(js, sizeof js, "%s/%s/skin.json", skins_dir, skins.v[i]);
                size_t jn = 0; char *json = (char *)file_read(js, &jn); if (json) { json = realloc(json, jn + 1); json[jn] = 0; }
                table own; int have_own = 0;
                for (size_t j = 0; j < files[i].n && !have_own; j++) if (is_table(files[i].v[j])) {
                    char q[4200]; snprintf(q, sizeof q, "%s/%s", r, files[i].v[j]); size_t on = 0; uint8_t *ob = file_read(q, &on);
                    have_own = ob && table_parse(&own, ob, on); free(ob);
                }
                size_t nfiles = 0;
                for (size_t j = 0; j < files[i].n; j++) {
                    const char *rel2 = files[i].v[j];
                    if (is_table(rel2)) continue;
                    nfiles++;
                    char q[4200]; snprintf(q, sizeof q, "%s/%s", r, rel2);
                    uint64_t mine = content_size(q);
                    if (!mine) { LOG("slingshot: size table: skin %s: %s does not say its unpacked size: left out", skins.v[i], rel2); continue; }
                    char k2[1100]; snprintf(k2, sizeof k2, "%s", rel2); size_t l2 = strlen(k2); if (l2 > 3 && !strcmp(k2 + l2 - 3, ".zs")) k2[l2 - 3] = 0;
                    uint8_t gh[18] = {0}; uint64_t gsize = 0, orig = 0;
                    if (rom_head(rel2, gh, sizeof gh, &gsize)) orig = l2 > 3 && !strcmp(rel2 + l2 - 3, ".zs") ? zs_content(gh, sizeof gh) : gsize;
                    uint32_t want = needed_size(k2, mine, table_get(&t, k2), orig);
                    if (have_own) { uint32_t o = table_get(&own, k2); if (o > want) want = o; }
                    uint32_t js_size = json_size(json, k2); if (js_size > want) want = js_size;
                    table_raise(&t, k2, want);
                }
                if (have_own) table_free(&own);
                free(json);
                LOG("slingshot: size table: skin %s: %zu files", skins.v[i], nfiles);
            }
            char dir[2048]; snprintf(dir, sizeof dir, "%s", out);
            for (char *p = dir + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(dir, 0755); *p = '/'; }
            if (table_write(&t, out)) {
                made = 1;
                FILE *kf = fopen(keyp, "w"); if (kf) { fputs(key, kf); fclose(kf); }
                LOG("slingshot: size table over %zu skin(s) and %zu mod table(s) made: %s", ns, mods_with.n, out);
            } else LOG("slingshot: [RED] size table: %s could not be written (errno=%d)", out, errno);
            table_free(&t);
        }
        free(z);
    }
    if (same || made) mark_table(mods, "link-skins/table", name, "Link Skins' table");   // Squadala!'s mark: this table holds the puppets' entries
    for (size_t i = 0; i < ns; i++) strs_free(&files[i]);
    strs_free(&mods_with); strs_free(&skins); free(key);
    if (g_img) { romfs_image_close(g_img); g_img = NULL; }
    g_romfs[0] = 0;
    return same || made;
}

// ---- The mods as Longshot finds them, and the kit's merge (Squadala!'s tools/puppet_pack/merge_tables.py, 0.11.4) ----
// A mod: a folder holding romfs, up to 4 deep under the mods folder (mods/<name>, mods/contents/<title>/<name>, ...), not looked into further; names
// from '.', '-' or '_' left out, as Longshot skips them. Link Skins' folder is Slingshot's own (its rows are the skins), and contents/0400486024DEC000/
// coop-puppet is the link to mods/coop-puppet (dm/slingshot/tools/squadala_setup.sh): both left out. Paths with '/', sorted at each level as find_mods.
static int mods_walk(const char *mods, const char *rel, int depth, char (*out)[SLINGSHOT_MOD_ID], int n, int max) {
    char p[2048]; snprintf(p, sizeof p, "%s%s%s", mods, *rel ? "/" : "", rel);
    DIR *d = opendir(p); if (!d) return n;
    strs names = {0};
    for (struct dirent *e; (e = readdir(d));) if (e->d_name[0] != '.' && e->d_name[0] != '-' && e->d_name[0] != '_') strs_add(&names, e->d_name);
    closedir(d);
    if (names.n) qsort(names.v, names.n, sizeof *names.v, cmp_str);
    for (size_t i = 0; i < names.n && n < max; i++) {
        char r[2048]; snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", names.v[i]);
        if (!*rel && !strcmp(names.v[i], "link-skins")) continue;
        if (!strcasecmp(r, "contents/0400486024DEC000/coop-puppet")) continue;
        char q[2100]; struct stat st;
        snprintf(q, sizeof q, "%s/%s", mods, r); if (stat(q, &st) || !S_ISDIR(st.st_mode)) continue;
        snprintf(q, sizeof q, "%s/%s/romfs", mods, r);
        if (!stat(q, &st) && S_ISDIR(st.st_mode)) { if (strlen(r) < SLINGSHOT_MOD_ID && !strchr(r, ':')) snprintf(out[n++], SLINGSHOT_MOD_ID, "%s", r); }
        else if (depth < 4) n = mods_walk(mods, r, depth + 1, out, n, max);
    }
    strs_free(&names);
    return n;
}
int slingshot_mods_find(const char *mods, char (*out)[SLINGSHOT_MOD_ID], int max) { return mods_walk(mods, "", 1, out, 0, max); }

static uint64_t fnv64(const uint8_t *b, size_t n) { uint64_t h = 0xcbf29ce484222325ull; for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull; return h; }
static int file_line(const char *mods, const char *path, char *out, size_t cap) {   // "<path under the mods> <length> <FNV-1a 64>" (merge_tables.py file_line)
    size_t n = 0; uint8_t *b = file_read(path, &n); if (!b) return 0;
    size_t ml = strlen(mods); const char *rel = !strncmp(path, mods, ml) && path[ml] == '/' ? path + ml + 1 : path;
    snprintf(out, cap, "%s %zu %016llx", rel, n, (unsigned long long)fnv64(b, n)); free(b);
    return 1;
}
static int table_load(const char *path, table *t) { size_t n = 0; uint8_t *b = file_read(path, &n); int ok = b && table_parse(t, b, n); free(b); return ok; }
static void pack_table_path(const char *mods, const char *fname, char *out, size_t cap) {
    snprintf(out, cap, "%s/coop-puppet/romfs/" TABLE_DIR "/%s", mods, fname);
    struct stat st; if (stat(out, &st)) snprintf(out, cap, "%s/contents/0400486024DEC000/coop-puppet/romfs/" TABLE_DIR "/%s", mods, fname);
}
static void unmark(const char *mods, const char *win) { char m[2048]; snprintf(m, sizeof m, "%s/%s/coop-merged.txt", mods, win); remove(m); }
// The module's mark (coop_rando.cpp MergedByKit), beside the winner's romfs: "coop-merged 1", "table <file> <FNV-1a 64>", "pack <FNV-1a 64 of the pack's
// table of that name>", "rule merged|contains". While both tables are still those bytes, Squadala! makes the puppets with that table the one the game reads
static int write_mark(const char *mods, const char *win, const char *fname, const char *rule) {
    char tp[2048], pp[2048], m[2048];
    snprintf(tp, sizeof tp, "%s/%s/romfs/" TABLE_DIR "/%s", mods, win, fname); pack_table_path(mods, fname, pp, sizeof pp);
    snprintf(m, sizeof m, "%s/%s/coop-merged.txt", mods, win);
    size_t tn = 0, pn = 0; uint8_t *tb = file_read(tp, &tn), *pb = file_read(pp, &pn);
    int ok = tb && pb;
    if (ok) {
        char text[1024]; int l = snprintf(text, sizeof text, "coop-merged 1\ntable %s %016llx\npack %016llx\nrule %s\n", fname,
                                          (unsigned long long)fnv64(tb, tn), (unsigned long long)fnv64(pb, pn), rule);
        ok = write_if_changed(m, (const uint8_t *)text, (size_t)l);
    } else remove(m);
    free(tb); free(pb);
    return ok;
}
// Link Skins' table (made above with every mod's entries, the pack's among them): marked when it holds every entry of the pack's table
static void mark_table(const char *mods, const char *win, const char *fname, const char *shown) {
    char tp[2048], pp[2048]; snprintf(tp, sizeof tp, "%s/%s/romfs/" TABLE_DIR "/%s", mods, win, fname); pack_table_path(mods, fname, pp, sizeof pp);
    struct stat st; if (stat(pp, &st)) { unmark(mods, win); return; }   // (no puppet pack, or none with that table: nothing to say)
    table t, pk; int missing = -1;
    if (table_load(tp, &t)) { if (table_load(pp, &pk)) { missing = table_missing(&t, &pk); table_free(&pk); } table_free(&t); }
    if (missing == 0 && write_mark(mods, win, fname, "merged")) LOG("slingshot: size table: %s holds every co-op puppet entry (coop-merged.txt): the other players can be shown", shown);
    else { unmark(mods, win); LOG("slingshot: [RED] size table: %s lacks %d of the co-op puppet pack's entries: Squadala! shows the other players as fairies", shown, missing); }
}

static int fold_cmp(const char *a, const char *b) { for (;; a++, b++) { int x = tolower((unsigned char)(*a == '\\' ? '/' : *a)), y = tolower((unsigned char)(*b == '\\' ? '/' : *b)); if (x != y || !x) return x - y; } }
static const char *squadala_name(const char *mod) { return !strcmp(mod, "coop-puppet") ? "contents/0400486024DEC000/coop-puppet" : mod; }   // the pack as Squadala! names it
static int is_pack(const char *mod) { return !fold_cmp(squadala_name(mod), "contents/0400486024DEC000/coop-puppet"); }
static int is_ours(const char *mod) {   // the randomizer's seeds and its settings folder: Squadala! builds a seed beside every other table
    char top[256]; snprintf(top, sizeof top, "%s", mod); char *s = strchr(top, '/'); if (s) *s = 0;
    for (char *c = top; *c; c++) *c = (char)tolower((unsigned char)*c);
    return !strncmp(top, "randomizer-", 11) || !strcmp(top, "oot-remake-randomizer");
}
static int tables_of(const char *mods, const char *mod, strs *out) {
    char r[2048]; snprintf(r, sizeof r, "%s/%s/romfs/" TABLE_DIR, mods, mod);
    DIR *d = opendir(r); if (!d) return 0;
    for (struct dirent *e; (e = readdir(d));) { size_t l = strlen(e->d_name); if (l > 14 && !strcmp(e->d_name + l - 14, ".rsizetable.zs")) strs_add(out, e->d_name); }
    closedir(d);
    if (out->n) qsort(out->v, out->n, sizeof *out->v, cmp_str);
    return (int)out->n;
}
static void stamp_text(const char *mods, const char *out_line, strs *ins, char *text, size_t cap) {   // merge_tables.py stamp_text
    strs lines = {0};
    for (size_t i = 0; i < ins->n; i++) { char l[2400]; if (file_line(mods, ins->v[i], l + 3, sizeof l - 3)) { memcpy(l, "in ", 3); strs_add(&lines, l); } }
    if (lines.n) qsort(lines.v, lines.n, sizeof *lines.v, cmp_str);
    size_t at = (size_t)snprintf(text, cap, "coop-merged 1\nout %s\n", out_line);
    for (size_t i = 0; i < lines.n && at < cap; i++) at += (size_t)snprintf(text + at, cap - at, "%s\n", lines.v[i]);
    strs_free(&lines);
}
// One table: the winner's own (kept in <winner>/_coop-size-table/) with every entry of the others' (merge_tables.py merge_one). 0 done, 1 failed
static int merge_one(const char *mods, const char *fname, const char *win, strs *others) {
    char tp[2048], keep[2048], orig[2100], stamp[2100], line[2400];
    snprintf(tp, sizeof tp, "%s/%s/romfs/" TABLE_DIR "/%s", mods, win, fname);
    snprintf(keep, sizeof keep, "%s/%s/_coop-size-table", mods, win);
    snprintf(orig, sizeof orig, "%s/%s", keep, fname); snprintf(stamp, sizeof stamp, "%s/merged.txt", keep);
    strs ins = {0};
    for (size_t i = 0; i < others->n; i++) { char q[2400]; snprintf(q, sizeof q, "%s/%s/romfs/" TABLE_DIR "/%s", mods, others->v[i], fname); strs_add(&ins, q); }
    size_t cap = 1 << 16; char *now = malloc(cap), *old = NULL; size_t on = 0;
    old = (char *)file_read(stamp, &on); if (old) { old = realloc(old, on + 1); old[on] = 0; }
    int rc = 1;
    if (!file_line(mods, tp, line, sizeof line)) goto out;
    stamp_text(mods, line, &ins, now, cap);
    if (old && !strcmp(old, now)) {   // merged at an earlier start, nothing changed since
        write_mark(mods, win, fname, "merged");
        LOG("slingshot: size tables: %s has the other mods' entries already (merged at an earlier start)", squadala_name(win));
        rc = 0; goto out;
    }
    struct stat st; char want[2500]; snprintf(want, sizeof want, "out %s\n", line);
    const char *second = old ? strchr(old, '\n') : NULL;
    int ours = second && !strncmp(second + 1, want, strlen(want)) && !stat(orig, &st);
    if (!ours) {   // the mod's own table (the first start with it, or a new seed put there): kept as its original
        mkdir(keep, 0755);
        size_t n = 0; uint8_t *b = file_read(tp, &n);
        if (!b) goto out;
        char tmp[2200]; snprintf(tmp, sizeof tmp, "%s.tmp", orig);
        FILE *f = fopen(tmp, "wb"); int ok = f && fwrite(b, 1, n, f) == n; if (f) ok = fclose(f) == 0 && ok;
        free(b);
        if (!ok || rename(tmp, orig)) { remove(tmp); goto out; }
    }
    size_t rn = 0; uint8_t *raw = file_read(orig, &rn); table t;
    if (!raw || !table_parse(&t, raw, rn)) { free(raw); LOG("slingshot: [RED] size tables: %s could not be merged (its table is not one this reads): the other players can't be shown with it on", squadala_name(win)); unmark(mods, win); goto out; }
    int added = 0, raised = 0;
    for (size_t i = 0; i < ins.n; i++) {
        table o; if (!table_load(ins.v[i], &o)) { LOG("slingshot: size tables: %s's table could not be read: left out", squadala_name(others->v[i])); continue; }
        table_union(&t, &o, &added, &raised); table_free(&o);
    }
    size_t dn = rn; uint8_t *data = raw;
    if (added || raised) data = table_bytes(&t, &dn);
    table_free(&t);
    char rel[2400]; snprintf(rel, sizeof rel, "%s/romfs/" TABLE_DIR "/%s %zu %016llx", win, fname, dn, (unsigned long long)fnv64(data, dn));
    stamp_text(mods, rel, &ins, now, cap);   // the stamp names the table about to be written before it is written (a stop between keeps the original)
    int ok = write_if_changed(stamp, (const uint8_t *)now, strlen(now)) && write_if_changed(tp, data, dn);
    if (data != raw) free(data);
    free(raw);
    if (!ok) { unmark(mods, win); LOG("slingshot: [RED] size tables: %s could not be written (errno=%d): the other players can't be shown with it on", squadala_name(win), errno); goto out; }
    write_mark(mods, win, fname, "merged");
    char names[1024] = ""; for (size_t i = 0; i < others->n; i++) { size_t l = strlen(names); snprintf(names + l, sizeof names - l, "%s%s", i ? ", " : "", is_pack(others->v[i]) ? "the puppet pack" : others->v[i]); }
    LOG("slingshot: size tables: the game reads %s's; merged in the entries of %s (%d added, %d made larger). Its own table is kept in %s/_coop-size-table",
        is_pack(win) ? "the puppet pack" : squadala_name(win), names, added, raised, win);
    rc = 0;
out:
    strs_free(&ins); free(now); free(old);
    return rc;
}
// At start, without Link Skins' table (dm/slingshot/src/mods.h smm_start): the kit's merge over the mods that are on, in the order the game indexes them
// (index[n - 1] wins a file; the pack named coop-puppet). For each table name, the winner's table gets every other mod's entries, so a mod with its own
// size table (a seed from OOTRR's site) does not hide the other players (merge_tables.py merge_mods). The number of tables that could not be merged
int slingshot_size_merge(const char *mods, const char *const *index, int n) {
    strs fnames = {0}; strs *tabs = calloc(n ? (size_t)n : 1, sizeof *tabs);
    for (int i = 0; i < n; i++) { tables_of(mods, index[i], &tabs[i]); for (size_t j = 0; j < tabs[i].n; j++) { int seen = 0; for (size_t k = 0; k < fnames.n && !seen; k++) seen = !strcmp(fnames.v[k], tabs[i].v[j]); if (!seen) strs_add(&fnames, tabs[i].v[j]); } }
    if (fnames.n) qsort(fnames.v, fnames.n, sizeof *fnames.v, cmp_str);
    int failed = 0;
    for (size_t f = 0; f < fnames.n; f++) {
        const char *fname = fnames.v[f];
        int having[256], nh = 0;
        for (int i = 0; i < n && nh < 256; i++) for (size_t j = 0; j < tabs[i].n; j++) if (!strcmp(tabs[i].v[j], fname)) { having[nh++] = i; break; }
        if (!nh) continue;
        const char *win = index[having[nh - 1]];
        strs others = {0}; int pack_in = -1, title_mod = 0;
        if (is_pack(win)) {   // the pack wins (a mod the list does not name sorts under contents/): the others' entries into the pack's table, our seeds' not
            for (int k = 0; k < nh - 1; k++) if (!is_ours(index[having[k]])) strs_add(&others, index[having[k]]);
            if (!others.n) { strs_free(&others); continue; }
        } else {
            if (nh < 2 || is_ours(win)) continue;   // one table, or one of our seeds
            for (int k = 0; k < nh - 1; k++) { strs_add(&others, index[having[k]]); if (is_pack(index[having[k]])) pack_in = (int)others.n - 1; }
            title_mod = !strncasecmp(win, "contents/", 9);
            if (title_mod && pack_in < 0) { strs_free(&others); continue; }   // a title's own mod, nothing of the puppets' under it: left alone
        }
        char keep_stamp[2100]; snprintf(keep_stamp, sizeof keep_stamp, "%s/%s/_coop-size-table/merged.txt", mods, win);
        struct stat st; int merged_before = !stat(keep_stamp, &st);
        if (pack_in >= 0 && (title_mod || (others.n == 1 && !merged_before))) {
            char tp[2048], pp[2048]; snprintf(tp, sizeof tp, "%s/%s/romfs/" TABLE_DIR "/%s", mods, win, fname); pack_table_path(mods, fname, pp, sizeof pp);
            table t, pk; int missing = -1;
            if (table_load(tp, &t)) { if (table_load(pp, &pk)) { missing = table_missing(&t, &pk); table_free(&pk); } table_free(&t); }
            if (missing == 0) {
                write_mark(mods, win, fname, "contains");
                LOG("slingshot: size tables: the game reads %s's, which holds every puppet pack entry already (left as it is): the other players can be shown", win);
                strs_free(&others); continue;
            }
            if (title_mod) {
                unmark(mods, win);
                LOG("slingshot: [RED] size tables: the game reads %s's, which lacks %d of the puppet pack's entries (or holds them smaller): the other players can't be shown with it on. Merge the pack's entries into it, or turn it off", win, missing);
                failed++; strs_free(&others); continue;
            }
        }
        failed += merge_one(mods, fname, win, &others);
        strs_free(&others);
    }
    for (int i = 0; i < n; i++) strs_free(&tabs[i]);
    free(tabs); strs_free(&fnames);
    return failed;
}

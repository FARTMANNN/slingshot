// Texture overrides for a co-op module: Squadala!'s hos_coop_texture.h (version 1), as Longshot's patch 0020 gives them (its host/coop_texture.c).
// A module may have a texture drawn with other texels while the game's memory keeps the game's own: Squadala!'s Link customizer paints your Link's tunic and
// gear with it (your colour, live, with no restart). Nothing of the game is written: the renderer takes the module's bytes where it reads the texture's
// storage (nvn_mtl.m pkg_upload_: such a texture is deswizzled on the CPU from a copy with the overrides laid over it), and an override set or taken away
// marks its pages written, so the texture is uploaded again at its next use, as after a write of the game's.
// The textures (hle_nvn.c: nvnTextureInitialize until nvnTextureFinalize or the object's next Initialize) are kept here with their storage, size, serial,
// width, height, levels and NVN format; the module finds Link's by their texels (it reads the storage itself, through the bridge).
// The two exports are found by name (dlsym RTLD_DEFAULT, as on Linux), by any module, from any thread: hos_coop_textures and hos_coop_texture_override.
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../../gfx/nvn/nvn_obj.h"
#include "../../../gfx/nvn/nvn_dirty.h"
#include "../../../host/dm.h"

typedef struct { uint64_t address, size, serial; uint32_t width, height, levels, format; } hos_tex;   // HosCoopTexture's layout
typedef struct { tex_t t; } ctex;                                          // a live texture (its object as it was made)
typedef struct { uint64_t serial, addr, n; uint8_t *bytes; } covr;         // an override: n bytes over [addr, addr + n), while that texture lives

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static ctex *g_tex; static size_t g_ntex, g_ctex;
static uint64_t *g_key; static uint32_t *g_at; static size_t g_cap;      // serial -> index in g_tex (open addressing, power-of-two size, 0 empty)
static _Atomic uint64_t g_gen;                                             // moves whenever a texture is made or ends
static covr *g_ov; static size_t g_nov, g_cov;
static _Atomic size_t g_nov_pub;                                           // g_nov, read without the lock (no override: nothing to look at)

static size_t slot_of(uint64_t serial) { return (size_t)((serial * 0x9E3779B97F4A7C15ull) >> 20) & (g_cap - 1); }
static void map_grow(void) {
    size_t oc = g_cap; uint64_t *ok = g_key; uint32_t *oa = g_at;
    g_cap = g_cap ? g_cap * 2 : 4096;
    g_key = calloc(g_cap, sizeof *g_key); g_at = calloc(g_cap, sizeof *g_at);
    if (!g_key || !g_at) die("co-op texture table allocation failed");
    for (size_t i = 0; i < oc; i++) if (ok[i]) { size_t s = slot_of(ok[i]); while (g_key[s]) s = (s + 1) & (g_cap - 1); g_key[s] = ok[i]; g_at[s] = oa[i]; }
    free(ok); free(oa);
}
static long map_find(uint64_t serial) {   // its slot, or -1
    if (!g_cap) return -1;
    for (size_t s = slot_of(serial);; s = (s + 1) & (g_cap - 1)) { if (!g_key[s]) return -1; if (g_key[s] == serial) return (long)s; }
}
static void map_del(size_t s) {   // backward-shift deletion: the keys after it in its run move up when their home is not between
    size_t i = s;
    for (size_t j = (i + 1) & (g_cap - 1); g_key[j]; j = (j + 1) & (g_cap - 1)) {
        size_t h = slot_of(g_key[j]);
        int stays = i <= j ? (h > i && h <= j) : (h > i || h <= j);   // its home lies in (i, j]: it may stay
        if (!stays) { g_key[i] = g_key[j]; g_at[i] = g_at[j]; i = j; }
    }
    g_key[i] = 0;
}
static void ov_drop(size_t i) { free(g_ov[i].bytes); g_ov[i] = g_ov[--g_nov]; atomic_store(&g_nov_pub, g_nov); }

// hle_nvn.c, nvnTextureInitialize: a texture made over its storage
void nvn_coop_tex_made(const tex_t *t) {
    if (!t->serial || !t->mem || !t->L.size) return;
    pthread_mutex_lock(&g_mu);
    if ((g_ntex + 1) * 2 > g_cap) map_grow();
    if (g_ntex == g_ctex && !(g_tex = realloc(g_tex, (g_ctex = g_ctex ? 2 * g_ctex : 4096) * sizeof *g_tex))) die("co-op texture list allocation failed");
    size_t s = slot_of(t->serial); while (g_key[s]) s = (s + 1) & (g_cap - 1);
    g_key[s] = t->serial; g_at[s] = (uint32_t)g_ntex; g_tex[g_ntex++].t = *t;
    atomic_fetch_add(&g_gen, 1);
    pthread_mutex_unlock(&g_mu);
}
// nvnTextureFinalize, or the object made again: the texture ends, and its overrides with it (the memory may hold something else next)
void nvn_coop_tex_gone(uint64_t serial) {
    if (!serial) return;
    pthread_mutex_lock(&g_mu);
    long s = map_find(serial);
    if (s >= 0) {
        size_t i = g_at[s], last = g_ntex - 1;
        map_del((size_t)s);
        if (i != last) { g_tex[i] = g_tex[last]; long m = map_find(g_tex[i].t.serial); if (m >= 0) g_at[m] = (uint32_t)i; }
        g_ntex--;
        for (size_t k = 0; k < g_nov;) if (g_ov[k].serial == serial) ov_drop(k); else k++;
        atomic_fetch_add(&g_gen, 1);
    }
    pthread_mutex_unlock(&g_mu);
}
// nvn_mtl.m pkg_upload_: whether an override lies over any of [addr, addr + n) (another texture over the same storage shows it too), and laying them over
// data, a copy of that range of guest memory
int nvn_coop_tex_overridden(uint64_t addr, uint64_t n) {
    if (!atomic_load_explicit(&g_nov_pub, memory_order_relaxed)) return 0;
    int r = 0;
    pthread_mutex_lock(&g_mu);
    for (size_t k = 0; k < g_nov && !r; k++) r = g_ov[k].addr < addr + n && addr < g_ov[k].addr + g_ov[k].n;
    pthread_mutex_unlock(&g_mu);
    return r;
}
void nvn_coop_tex_overlay(uint64_t addr, uint8_t *data, uint64_t n) {
    pthread_mutex_lock(&g_mu);
    for (size_t k = 0; k < g_nov; k++) {
        const covr *o = &g_ov[k];
        uint64_t lo = o->addr > addr ? o->addr : addr, hi = o->addr + o->n < addr + n ? o->addr + o->n : addr + n;
        if (lo < hi) memcpy(data + (lo - addr), o->bytes + (lo - o->addr), hi - lo);
    }
    pthread_mutex_unlock(&g_mu);
}

// ---- The exports (hos_coop_texture.h) ----
__attribute__((visibility("default"))) int hos_coop_textures(uint64_t size_min, uint64_t size_max, hos_tex *out, int max, uint64_t *generation) {
    int n = 0;
    pthread_mutex_lock(&g_mu);
    for (size_t i = 0; i < g_ntex; i++) {
        const tex_t *t = &g_tex[i].t;
        if (t->L.size < size_min || t->L.size > size_max) continue;
        if (out && n < max) out[n] = (hos_tex){(uint64_t)(uintptr_t)t->mem, t->L.size, t->serial, t->d.width, t->d.height, t->d.levels, t->d.format};
        n++;
    }
    if (generation) *generation = atomic_load(&g_gen);
    pthread_mutex_unlock(&g_mu);
    return n;
}
__attribute__((visibility("default"))) int hos_coop_texture_override(uint64_t address, uint64_t serial, const void *bytes, uint64_t n) {
    static _Atomic int said;
    pthread_mutex_lock(&g_mu);
    long s = map_find(serial);
    const tex_t *t = s >= 0 ? &g_tex[g_at[s]].t : NULL;
    uint64_t base = t ? (uint64_t)(uintptr_t)t->mem : 0;
    if (!t || !n || address < base || address + n > base + t->L.size) { pthread_mutex_unlock(&g_mu); return 0; }   // no such texture now, or not inside its storage
    tex_t copy = *t;
    for (size_t k = 0; k < g_nov;)   // that texture's overrides over the range go (a new one takes their place)
        if (g_ov[k].serial == serial && g_ov[k].addr < address + n && address < g_ov[k].addr + g_ov[k].n) ov_drop(k); else k++;
    if (bytes) {
        uint8_t *b = malloc(n);
        if (!b) { pthread_mutex_unlock(&g_mu); return 0; }
        memcpy(b, bytes, n);
        if (g_nov == g_cov && !(g_ov = realloc(g_ov, (g_cov = g_cov ? 2 * g_cov : 64) * sizeof *g_ov))) die("co-op texture override allocation failed");
        g_ov[g_nov++] = (covr){serial, address, n, b};
        atomic_store(&g_nov_pub, g_nov);
    }
    pthread_mutex_unlock(&g_mu);
    if (!atomic_fetch_add(&said, 1))
        LOG("[nvn] co-op texture overrides in use (first: ser=%llu %ux%u format %#x, %llu bytes)", (unsigned long long)serial, copy.d.width, copy.d.height, copy.d.format, (unsigned long long)n);
    // Drawn again with them (or without): taken from its storage from now on, and its pages count as written
    mtl_tex_cpu_written(&copy);
    nvn_dirty_gpu_written((uintptr_t)address, (size_t)n);
    nvn_dirty_touch();
    return 1;
}

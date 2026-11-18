// Squadala!'s proximity voice chat on Slingshot (Longshot's patch 0009: "the module's voices in the game's final mix"; Squadala!'s docs/voice.md).
// The module does the voice itself (who is near, Opus packets, placing each voice at that player's Link). It needs three things from the host, which this
// file gives it once the module is loaded and exports hos_coop_voice_attach and hos_coop_voice_render:
//   - the microphone: an AudioQueue on the Mac's default input (or the one the module's Microphone row picks, by name), 48 kHz mono 16-bit;
//   - libopus: loaded at run time (dlopen), the copy dm/slingshot/tools/squadala_setup.sh puts beside the module (Homebrew's opus), else Homebrew's own;
//     without one the voice stays off and the log says so;
//   - the game's mix: nxnative's audio renderer calls dm_audio_mix_hook with each frame at 48 kHz (dm/host/hle_audren.c), and the module adds the voices.
// The table's layout is declared here as Squadala!'s hookshot/src/hos_coop_voice.h has it (version 1), so the module (GPL-3.0) stays a separate program.
// HOS_COOP_VOICE=0 (or off, no, false) keeps the module unattached, as on Windows. The app asks for the microphone the first time the module starts it
// (NSMicrophoneUsageDescription, dm/mac/pack_app.py).
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <dlfcn.h>
#include <mach/mach_time.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "../../../host/dm.h"

void *coop_bridge_sym(const char *name);                                  // bridge.c: an export of the module while it is on
extern void (*dm_audio_mix_hook)(float *const *out, int channels, int frames);   // dm/host/hle_audren.c

enum { VOICE_VERSION = 1, VOICE_RATE = 48000, VOICE_FRAME = 960 };
typedef struct {
    uint32_t version, size;
    int (*mic_start)(void); int (*mic_read)(int16_t *out, int max); void (*mic_stop)(void);
    void *(*opus_encoder_new)(int bitrate, int loss_percent); int (*opus_encode)(void *encoder, const int16_t *pcm, uint8_t *out, int max);
    void (*opus_encoder_free)(void *encoder);
    void *(*opus_decoder_new)(void); int (*opus_decode)(void *decoder, const uint8_t *data, int size, int fec, int16_t *pcm);
    void (*opus_decoder_free)(void *decoder);
    uint64_t (*now_us)(void);
    int (*mic_list)(char *names, int name_size, int max); void (*mic_choose)(const char *name);
} voice_api_t;

// ---- libopus, loaded at run time ----
enum { OPUS_OK = 0, APP_VOIP = 2048, SET_BITRATE = 4002, SET_COMPLEXITY = 4010, SET_INBAND_FEC = 4012, SET_PACKET_LOSS_PERC = 4014, SET_SIGNAL = 4024, SIGNAL_VOICE = 3001 };
static void *(*op_enc_create)(int32_t rate, int channels, int application, int *error);
static int (*op_enc_ctl)(void *e, int request, ...);
static int32_t (*op_encode)(void *e, const int16_t *pcm, int frame, unsigned char *out, int32_t max);
static void (*op_enc_destroy)(void *e);
static void *(*op_dec_create)(int32_t rate, int channels, int *error);
static int (*op_decode)(void *d, const unsigned char *data, int32_t len, int16_t *pcm, int frame, int fec);
static void (*op_dec_destroy)(void *d);
static int opus_load(char *where, size_t cap) {
    const char *home = getenv("HOME") ?: "", *env = getenv("SLINGSHOT_OPUS");
    char tries[5][1024];
    snprintf(tries[0], sizeof tries[0], "%s", env && *env ? env : "");
    snprintf(tries[1], sizeof tries[1], "%s/Library/Application Support/Slingshot/coop/libopus.0.dylib", home);   // (squadala_setup.sh's copy)
    snprintf(tries[2], sizeof tries[2], "/opt/homebrew/lib/libopus.0.dylib");
    snprintf(tries[3], sizeof tries[3], "%s/.homebrew/lib/libopus.0.dylib", home);
    snprintf(tries[4], sizeof tries[4], "/usr/local/lib/libopus.0.dylib");
    for (int i = 0; i < 5; i++) {
        if (!tries[i][0]) continue;
        void *h = dlopen(tries[i], RTLD_NOW | RTLD_LOCAL);
        if (!h) continue;
        op_enc_create = (void *(*)(int32_t, int, int, int *))dlsym(h, "opus_encoder_create");
        op_enc_ctl = (int (*)(void *, int, ...))dlsym(h, "opus_encoder_ctl");
        op_encode = (int32_t (*)(void *, const int16_t *, int, unsigned char *, int32_t))dlsym(h, "opus_encode");
        op_enc_destroy = (void (*)(void *))dlsym(h, "opus_encoder_destroy");
        op_dec_create = (void *(*)(int32_t, int, int *))dlsym(h, "opus_decoder_create");
        op_decode = (int (*)(void *, const unsigned char *, int32_t, int16_t *, int, int))dlsym(h, "opus_decode");
        op_dec_destroy = (void (*)(void *))dlsym(h, "opus_decoder_destroy");
        if (op_enc_create && op_enc_ctl && op_encode && op_enc_destroy && op_dec_create && op_decode && op_dec_destroy) { snprintf(where, cap, "%s", tries[i]); return 1; }
        dlclose(h);
    }
    return 0;
}
static void *v_encoder_new(int bitrate, int loss) {   // as Longshot's: speech, in-band FEC for the loss asked, complexity 8
    int err = 0; void *e = op_enc_create(VOICE_RATE, 1, APP_VOIP, &err);
    if (!e || err != OPUS_OK) { LOG("coop voice: Opus encoder not made (%d)", err); return NULL; }
    op_enc_ctl(e, SET_BITRATE, (int32_t)(bitrate > 0 ? bitrate : 24000));
    op_enc_ctl(e, SET_SIGNAL, (int32_t)SIGNAL_VOICE);
    op_enc_ctl(e, SET_INBAND_FEC, (int32_t)(loss > 0));
    op_enc_ctl(e, SET_PACKET_LOSS_PERC, (int32_t)(loss > 0 ? loss : 0));
    op_enc_ctl(e, SET_COMPLEXITY, (int32_t)8);
    return e;
}
static int v_encode(void *e, const int16_t *pcm, uint8_t *out, int max) {
    if (!e || !pcm || !out || max <= 0) return 0;
    int32_t n = op_encode(e, pcm, VOICE_FRAME, out, max);
    return n > 0 ? (int)n : 0;
}
static void v_encoder_free(void *e) { if (e) op_enc_destroy(e); }
static void *v_decoder_new(void) {
    int err = 0; void *d = op_dec_create(VOICE_RATE, 1, &err);
    if (!d || err != OPUS_OK) { LOG("coop voice: Opus decoder not made (%d)", err); return NULL; }
    return d;
}
static int v_decode(void *d, const uint8_t *data, int size, int fec, int16_t *pcm) {   // from data, its FEC, or concealed (data NULL)
    if (!d || !pcm) return 0;
    int has = data && size > 0;
    int n = op_decode(d, has ? data : NULL, has ? size : 0, pcm, VOICE_FRAME, has && fec ? 1 : 0);
    if (n <= 0) return 0;
    if (n < VOICE_FRAME) memset(pcm + n, 0, (size_t)(VOICE_FRAME - n) * sizeof *pcm);
    return n;
}
static void v_decoder_free(void *d) { if (d) op_dec_destroy(d); }
static uint64_t v_now_us(void) {
    static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb);
    return mach_absolute_time() * tb.numer / tb.denom / 1000;
}

// ---- The microphone: an AudioQueue, 48 kHz mono 16-bit, into a quarter-second ring ----
enum { RING = VOICE_RATE / 4, QBUF = 3 };
static pthread_mutex_t g_mic_mu = PTHREAD_MUTEX_INITIALIZER;
static AudioQueueRef g_q;
static int16_t g_ring[RING]; static uint64_t g_wrote, g_read;
static char g_mic_name[256];   // the module's Microphone row's choice ("" the Mac's default input)
static void mic_in(void *u, AudioQueueRef q, AudioQueueBufferRef b, const AudioTimeStamp *t, UInt32 np, const AudioStreamPacketDescription *pd) {
    (void)u; (void)t; (void)np; (void)pd;
    const int16_t *s = b->mAudioData; size_t n = b->mAudioDataByteSize / 2;
    pthread_mutex_lock(&g_mic_mu);
    for (size_t i = 0; i < n; i++) g_ring[(g_wrote + i) % RING] = s[i];
    g_wrote += n;
    pthread_mutex_unlock(&g_mic_mu);
    AudioQueueEnqueueBuffer(q, b, 0, NULL);
}
static CFStringRef mic_uid(const char *name) {   // an input device's UID by its name (retained), or NULL
    if (!name || !*name) return NULL;
    AudioObjectPropertyAddress a = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 sz = 0; if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, NULL, &sz) || !sz) return NULL;
    AudioDeviceID *ids = malloc(sz); CFStringRef uid = NULL;
    if (!AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &sz, ids))
        for (UInt32 i = 0; i < sz / sizeof *ids && !uid; i++) {
            CFStringRef nm = NULL; UInt32 ns = sizeof nm; char buf[256];
            AudioObjectPropertyAddress na = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
            if (AudioObjectGetPropertyData(ids[i], &na, 0, NULL, &ns, &nm) || !nm) continue;
            int same = CFStringGetCString(nm, buf, sizeof buf, kCFStringEncodingUTF8) && !strcmp(buf, name);
            CFRelease(nm);
            if (!same) continue;
            AudioObjectPropertyAddress ua = {kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain}; UInt32 us = sizeof uid;
            if (AudioObjectGetPropertyData(ids[i], &ua, 0, NULL, &us, &uid)) uid = NULL;
        }
    free(ids);
    return uid;
}
static int mic_open_locked(void) {
    AudioStreamBasicDescription f = {VOICE_RATE, kAudioFormatLinearPCM, kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked, 2, 1, 2, 1, 16, 0};
    OSStatus st = AudioQueueNewInput(&f, mic_in, NULL, NULL, NULL, 0, &g_q);
    if (st) { LOG("coop voice: [RED] the microphone did not open (%d)", (int)st); g_q = NULL; return 0; }
    CFStringRef uid = mic_uid(g_mic_name);
    if (uid) { AudioQueueSetProperty(g_q, kAudioQueueProperty_CurrentDevice, &uid, sizeof uid); CFRelease(uid); }
    else if (g_mic_name[0]) LOG("coop voice: the microphone %s is not plugged in: silence until it is back", g_mic_name);
    for (int i = 0; i < QBUF; i++) { AudioQueueBufferRef b; if (!AudioQueueAllocateBuffer(g_q, VOICE_FRAME * 2, &b)) AudioQueueEnqueueBuffer(g_q, b, 0, NULL); }
    st = AudioQueueStart(g_q, NULL);
    if (st) { LOG("coop voice: [RED] the microphone did not start (%d): is it allowed in System Settings > Privacy & Security > Microphone?", (int)st); AudioQueueDispose(g_q, true); g_q = NULL; return 0; }
    g_read = g_wrote;
    LOG("coop voice: the microphone is on (%s)", g_mic_name[0] && uid ? g_mic_name : "the Mac's default input");
    return 1;
}
static void mic_close_locked(void) { if (g_q) { AudioQueueStop(g_q, true); AudioQueueDispose(g_q, true); g_q = NULL; } }
// (the queue's own calls run without g_mic_mu: Stop waits for its callback, which takes it)
static int v_mic_start(void) { if (g_q) return 1; return mic_open_locked(); }
static void v_mic_stop(void) { mic_close_locked(); }
static int v_mic_read(int16_t *out, int max) {   // what came since the last read, at most a quarter second (older is dropped)
    if (!out || max <= 0) return 0;
    pthread_mutex_lock(&g_mic_mu);
    if (g_wrote - g_read > RING) g_read = g_wrote - RING;
    int n = 0; for (; n < max && g_read < g_wrote; n++, g_read++) out[n] = g_ring[g_read % RING];
    pthread_mutex_unlock(&g_mic_mu);
    return n;
}
static int v_mic_list(char *names, int name_size, int max) {   // the input devices now, by name
    AudioObjectPropertyAddress a = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 sz = 0; if (!names || name_size <= 0 || max <= 0 || AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, NULL, &sz) || !sz) return 0;
    AudioDeviceID *ids = malloc(sz); int n = 0;
    if (!AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &sz, ids))
        for (UInt32 i = 0; i < sz / sizeof *ids && n < max; i++) {
            AudioObjectPropertyAddress sa = {kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput, kAudioObjectPropertyElementMain}; UInt32 ss = 0;
            if (AudioObjectGetPropertyDataSize(ids[i], &sa, 0, NULL, &ss) || !ss) continue;   // (no input streams: not a microphone)
            CFStringRef nm = NULL; UInt32 ns = sizeof nm;
            AudioObjectPropertyAddress na = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
            if (AudioObjectGetPropertyData(ids[i], &na, 0, NULL, &ns, &nm) || !nm) continue;
            if (CFStringGetCString(nm, names + (size_t)n * (size_t)name_size, name_size, kCFStringEncodingUTF8)) n++;
            CFRelease(nm);
        }
    free(ids);
    return n;
}
static void v_mic_choose(const char *name) {   // the voice's own microphone; NULL or "": the Mac's default input. A started one moves to it at once
    snprintf(g_mic_name, sizeof g_mic_name, "%s", name ? name : "");
    if (g_q) { mic_close_locked(); mic_open_locked(); }
}

// ---- Attaching, and the mix ----
static void (*m_render)(float *const *out, int channels, int frames);
static int g_tried;   // (the mixer's thread) 0 looking, 1 attached, 2 never
static uint64_t g_next_look_us;
static void look(void) {
    const char *e = getenv("HOS_COOP_VOICE");
    if (e && (!strcmp(e, "0") || !strcasecmp(e, "off") || !strcasecmp(e, "no") || !strcasecmp(e, "false"))) { g_tried = 2; LOG("coop voice: off (HOS_COOP_VOICE)"); return; }
    int (*attach)(const voice_api_t *) = (int (*)(const voice_api_t *))coop_bridge_sym("hos_coop_voice_attach");
    void (*render)(float *const *, int, int) = (void (*)(float *const *, int, int))coop_bridge_sym("hos_coop_voice_render");
    if (!attach || !render) {
        if (coop_bridge_sym("hos_coop_start")) { g_tried = 2; LOG("coop voice: this co-op module has no voice"); }   // (loaded, without voice; else not loaded yet: look again)
        return;
    }
    char where[1024];
    if (!opus_load(where, sizeof where)) { g_tried = 2; LOG("coop voice: [RED] no libopus (dm/slingshot/tools/squadala_setup.sh copies Homebrew's opus beside the module): voice chat off"); return; }
    static voice_api_t api;
    api = (voice_api_t){VOICE_VERSION, sizeof api, v_mic_start, v_mic_read, v_mic_stop, v_encoder_new, v_encode, v_encoder_free,
                        v_decoder_new, v_decode, v_decoder_free, v_now_us, v_mic_list, v_mic_choose};
    if (!attach(&api)) { g_tried = 2; LOG("coop voice: the module does not take voice"); return; }
    m_render = render; g_tried = 1;
    LOG("coop voice: on (the voices in the game's mix; libopus %s)", where);
}
// dm/host/hle_audren.c, the renderer's thread, each frame at 48 kHz: the voices added into out (2 channels, full scale 1.0)
static void voice_mix(float *const *out, int channels, int frames) {
    if (g_tried == 0) {
        uint64_t now = v_now_us();
        if (now < g_next_look_us) return;
        g_next_look_us = now + 1000000;
        look();
    }
    if (g_tried == 1 && m_render) m_render(out, channels, frames);
}
void coop_voice_init(void) { dm_audio_mix_hook = voice_mix; }   // bridge.c, once the module is on

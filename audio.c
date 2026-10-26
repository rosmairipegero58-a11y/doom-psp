#include "audio.h"
#include <math.h>
#include <string.h>

#define NV 20
enum { W_SQ, W_SAW, W_SINE, W_NOISE };

typedef struct {
    int act, type, delay, left, total;
    float ph, fr, fmul, g, gmul, lp, lpa;
} Voice;

static Voice vo[NV];
static volatile int qbuf[64];
static volatile int qh, qt;
static volatile int music_on;
static uint32_t rnd = 12345u;
static int mus_cnt, mi;

static const int RIFF[16] = {41,41,52,41,41,55,41,58,41,41,52,41,49,48,46,43};

static float frand(void) {
    rnd = rnd * 1664525u + 1013904223u;
    return (float)((rnd >> 8) & 0xFFFF) / 32768.0f - 1.0f;
}

static Voice *alloc_voice(void) {
    int i, best = 0, bl = 0x7fffffff;
    for (i = 0; i < NV; i++) if (!vo[i].act) return &vo[i];
    for (i = 0; i < NV; i++) if (vo[i].left < bl) { bl = vo[i].left; best = i; }
    return &vo[best];
}

static void tone(float f, float d, int type, float v, float sl, float delay) {
    Voice *o = alloc_voice();
    int n = (int)(d * AUDIO_RATE);
    float f1 = f + sl;
    if (n < 1) n = 1;
    if (f1 < 20.0f) f1 = 20.0f;
    memset(o, 0, sizeof(*o));
    o->type = type; o->total = o->left = n;
    o->delay = (int)(delay * AUDIO_RATE);
    o->fr = f;
    o->fmul = (sl != 0.0f) ? powf(f1 / f, 1.0f / (float)n) : 1.0f;
    o->g = v;
    o->gmul = powf(0.001f / v, 1.0f / (float)n);
    o->act = 1;
}

static void noise(float d, float v, float fq, float delay) {
    Voice *o = alloc_voice();
    int n = (int)(d * AUDIO_RATE);
    float a = 1.0f - expf(-6.2831853f * fq / (float)AUDIO_RATE);
    if (n < 1) n = 1;
    if (a > 1.0f) a = 1.0f;
    memset(o, 0, sizeof(*o));
    o->type = W_NOISE; o->total = o->left = n;
    o->delay = (int)(delay * AUDIO_RATE);
    o->g = v; o->lpa = a;
    o->act = 1;
}

static void do_sfx(int id) {
    switch (id) {
    case SFX_PI:   noise(.1f,.35f,3500,0); tone(180,.1f,W_SQ,.15f,-120,0); break;
    case SFX_SG:   noise(.25f,.5f,1800,0); tone(90,.2f,W_SAW,.2f,-60,0); break;
    case SFX_TIC:  tone(500,.05f,W_SQ,.08f,0,0); break;
    case SFX_SEL:  noise(.08f,.3f,3000,0); tone(200,.08f,W_SQ,.15f,0,0); break;
    case SFX_DOOR: tone(120,.4f,W_SAW,.1f,80,0); break;
    case SFX_PICK: tone(600,.08f,W_SQ,.1f,0,0); tone(900,.1f,W_SQ,.1f,0,.07f); break;
    case SFX_PAIN: tone(220,.15f,W_SAW,.2f,-120,0); break;
    case SFX_DIE:  tone(180,.5f,W_SAW,.2f,-150,0); break;
    case SFX_MON:  tone(90,.25f,W_SAW,.2f,40,0); break;
    case SFX_FB:   noise(.2f,.25f,900,0); tone(300,.2f,W_SAW,.1f,-200,0); break;
    case SFX_BOOM: noise(.6f,.7f,700,0); tone(60,.5f,W_SAW,.3f,-30,0); break;
    case SFX_HURT: tone(150,.2f,W_SQ,.25f,-80,0); break;
    }
}

static void music_tick(void) {
    int n = RIFF[mi % 16];
    float f = 440.0f * powf(2.0f, (float)(n - 57) / 12.0f);
    tone(f, .14f, W_SAW, .045f, 0, 0);
    tone(f / 2, .14f, W_SQ, .04f, 0, 0);
    if (mi % 4 == 0) { noise(.1f,.15f,300,0); tone(60,.1f,W_SINE,.2f,-30,0); }
    if (mi % 2 == 1) noise(.04f,.06f,6000,0);
    mi++;
}

void audio_init(void) {
    memset(vo, 0, sizeof(vo));
    qh = qt = 0; mus_cnt = 0; mi = 0; music_on = 0;
}

void sfx(int id) {
    int nh = (qh + 1) & 63;
    if (nh == qt) return;      /* cola llena: se descarta */
    qbuf[qh] = id;
    qh = nh;
}

void audio_set_music(int on) { music_on = on; }

void audio_mix(int16_t *out, int frames) {
    int i, k;
    while (qt != qh) { do_sfx(qbuf[qt]); qt = (qt + 1) & 63; }
    for (i = 0; i < frames; i++) {
        float acc = 0.0f;
        if (--mus_cnt <= 0) {
            mus_cnt += (int)(0.15f * AUDIO_RATE);
            if (music_on) music_tick();
        }
        for (k = 0; k < NV; k++) {
            Voice *o = &vo[k];
            float s;
            if (!o->act) continue;
            if (o->delay > 0) { o->delay--; continue; }
            if (o->type == W_NOISE) {
                float x = frand() * ((float)o->left / (float)o->total);
                o->lp += o->lpa * (x - o->lp);
                s = o->lp * o->g * 1.6f;
            } else {
                float p = o->ph;
                if (o->type == W_SQ) s = p < 0.5f ? 1.0f : -1.0f;
                else if (o->type == W_SAW) s = 2.0f * p - 1.0f;
                else {   /* seno aproximado (parabola) */
                    float t = p < 0.5f ? p * 4.0f - 1.0f : 3.0f - p * 4.0f;
                    s = t * (1.5f - 0.5f * t * t);
                }
                s *= o->g;
                p += o->fr / (float)AUDIO_RATE;
                if (p >= 1.0f) p -= 1.0f;
                o->ph = p;
                o->fr *= o->fmul;
                o->g *= o->gmul;
            }
            acc += s;
            if (--o->left <= 0) o->act = 0;
        }
        acc *= 0.9f;
        if (acc > 1.0f) acc = 1.0f; else if (acc < -1.0f) acc = -1.0f;
        {
            int16_t v = (int16_t)(acc * 26000.0f);
            out[i * 2] = v; out[i * 2 + 1] = v;
        }
    }
}

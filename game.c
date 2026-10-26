/* DOOM PSP - port a C del juego HTML "DOOM Mobile".
 * Mismo motor (raycasting 320x200), mismos niveles generados por semilla,
 * pero controlado con los botones de la PSP. */
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "game.h"
#include "audio.h"
#include "font.h"

typedef uint32_t u32;

#define VW 320
#define VH 160
#define PI_F 3.14159265f

u32 game_fb[SW * SH];
int game_opt_full = 1;
static u32 snapbuf[SW * SH];
static u32 cpbuf[SW * SH];

/* ------------------------------------------------------------ colores */
#define RGB(r, g, b) (0xFF000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))
#define HX(h) RGB((((h) >> 16) & 255), (((h) >> 8) & 255), ((h) & 255))
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline u32 rgbf(float r, float g, float b) {
    return RGB(clampi((int)r, 0, 255), clampi((int)g, 0, 255), clampi((int)b, 0, 255));
}
static inline float fminf2(float a, float b) { return a < b ? a : b; }
static inline float fmaxf2(float a, float b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

/* ------------------------------------------------------------ aleatorio (jugabilidad) */
static u32 grnd = 0x1234567u;
static float frnd(void) {   /* 0..1 */
    grnd ^= grnd << 13; grnd ^= grnd >> 17; grnd ^= grnd << 5;
    return (float)(grnd & 0xFFFFFF) / 16777216.0f;
}

/* ------------------------------------------------------------ dibujo 2D basico */
static void Rc(u32 c, int x, int y, int w, int h) {
    int x0 = imax(x, 0), y0 = imax(y, 0), x1 = imin(x + w, SW), y1 = imin(y + h, SH), i, j;
    for (j = y0; j < y1; j++) {
        u32 *p = &game_fb[j * SW + x0];
        for (i = x0; i < x1; i++) *p++ = c;
    }
}
static void RcA(int r, int g, int b, float a, int x, int y, int w, int h) {
    int x0 = imax(x, 0), y0 = imax(y, 0), x1 = imin(x + w, SW), y1 = imin(y + h, SH), i, j;
    int ia = (int)(a * 256.0f);
    if (ia <= 0) return;
    if (ia > 256) ia = 256;
    for (j = y0; j < y1; j++) {
        u32 *p = &game_fb[j * SW + x0];
        for (i = x0; i < x1; i++, p++) {
            u32 c = *p;
            int cr = c & 255, cg = (c >> 8) & 255, cb = (c >> 16) & 255;
            cr += ((r - cr) * ia) >> 8; cg += ((g - cg) * ia) >> 8; cb += ((b - cb) * ia) >> 8;
            *p = RGB(cr, cg, cb);
        }
    }
}
static void Circle(u32 c, int cx, int cy, int r) {
    int y, x;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) {
                int px = cx + x, py = cy + y;
                if (px >= 0 && px < SW && py >= 0 && py < SH) game_fb[py * SW + px] = c;
            }
}

#define AL_L 0
#define AL_C 1
static void tx(const char *s, int x, int y, u32 col, int sc, int al, u32 stroke) {
    int n = (int)strlen(s), w = n * 6 * sc - sc, i, r, c, pass;
    int x0 = (al == AL_C) ? x - w / 2 : x;
    int top = y - 7 * sc;
    int t = sc >= 5 ? 2 : (sc >= 2 ? 1 : 1);
    for (pass = (stroke ? 0 : 1); pass < 2; pass++) {
        int oxs = pass == 0 ? -t : 0, oxe = pass == 0 ? t : 0;
        int ox, oy;
        u32 cc = pass == 0 ? stroke : col;
        for (oy = oxs; oy <= oxe; oy++)
            for (ox = oxs; ox <= oxe; ox++) {
                if (pass == 0 && ox == 0 && oy == 0) continue;
                for (i = 0; i < n; i++) {
                    int ch = (unsigned char)s[i];
                    const unsigned char *g;
                    if (ch >= 'a' && ch <= 'z') ch -= 32;
                    if (ch < 32 || ch > 90) ch = '?';
                    g = FONT57[ch - 32];
                    for (r = 0; r < 7; r++)
                        for (c = 0; c < 5; c++)
                            if (g[r] & (0x10 >> c))
                                Rc(cc, x0 + i * 6 * sc + c * sc + ox, top + r * sc + oy, sc, sc);
                }
            }
    }
}
static void txi(int v, int x, int y, u32 col, int sc, int al) {
    char b[16]; snprintf(b, sizeof b, "%d", v); tx(b, x, y, col, sc, al, 0);
}

/* ------------------------------------------------------------ texturas procedurales */
static float nz(float x, float y) {
    float v = sinf(x * 12.9898f + y * 78.233f) * 43758.5453f;
    return v - floorf(v);
}
static u32 TX[9][4096];

static void texpix(int id, int x, int y, float *o) {
    float n, g;
    switch (id) {
    case 0:
        g = 95 + nz(x, y) * 25;
        if (x % 32 < 2 || y % 32 < 2) g = 50;
        if (y > 26 && y < 34 && x % 32 > 6 && x % 32 < 26) { o[0] = 40; o[1] = 90 + (x % 32) * 3; o[2] = 70; return; }
        o[0] = g; o[1] = g; o[2] = g + 10; return;
    case 1: {
        int xx = (x + ((y >> 4) % 2) * 16) % 32;
        if (y % 16 < 2 || xx < 2) { o[0] = 60; o[1] = 50; o[2] = 48; return; }
        n = nz(x, y) * 30; o[0] = 130 + n; o[1] = 40 + n / 2; o[2] = 30; return; }
    case 2:
        n = nz(x, y) * 15;
        if (y < 8 || y > 55) {
            if (((x + y) >> 3) & 1) { o[0] = 200; o[1] = 170; o[2] = 0; }
            else { o[0] = 30; o[1] = 30; o[2] = 30; }
            return;
        }
        o[0] = 110 + n; o[1] = 115 + n; o[2] = 125 + n; return;
    case 3: case 4: case 5:
        n = nz(x, y) * 15;
        if (abs(x - 32) < 9 && abs(y - 32) < 9) {
            if (id == 3) { o[0] = 40; o[1] = 80; o[2] = 255; }
            else if (id == 4) { o[0] = 250; o[1] = 220; o[2] = 30; }
            else { o[0] = 255; o[1] = 30; o[2] = 30; }
            return;
        }
        o[0] = 80 + n; o[1] = 80 + n; o[2] = 88 + n; return;
    case 6:
        if (x > 12 && x < 52 && y > 20 && y < 44) { o[0] = o[1] = o[2] = 240; return; }
        o[0] = 150 + nz(x, y) * 20; o[1] = 20; o[2] = 20; return;
    case 7:
        n = nz(x >> 2, y >> 2) * 40 + nz(x, y) * 15;
        o[0] = 90 + n; o[1] = 85 + n; o[2] = 80 + n; return;
    default:
        if (x % 16 < 1) { o[0] = 40; o[1] = 25; o[2] = 15; return; }
        n = nz(x, y >> 3) * 30 + nz(x, y) * 10;
        o[0] = 120 + n; o[1] = 80 + n / 2; o[2] = 45; return;
    }
}
static void make_textures(void) {
    int id, x, y;
    for (id = 0; id < 9; id++)
        for (y = 0; y < 64; y++)
            for (x = 0; x < 64; x++) {
                float c[3]; texpix(id, x, y, c);
                TX[id][y * 64 + x] = rgbf(c[0], c[1], c[2]);
            }
}

/* ------------------------------------------------------------ sprites procedurales */
enum { S_Z, S_I, S_D, S_DEAD, S_H, S_A, S_G, S_C, S_K, S_B, S_Y, S_R, S_O, S_FB, S_COUNT };
static u32 SPR[S_COUNT][1024];
static const float SC[S_COUNT] = {1, 1, 1.1f, .6f, .5f, .4f, .5f, .5f, .55f, .45f, .45f, .45f, .75f, .35f};
static u32 *cs;
static void R(u32 col, int x, int y, int w, int h) {
    int i, j;
    for (j = y; j < y + h; j++) for (i = x; i < x + w; i++)
        if (i >= 0 && i < 32 && j >= 0 && j < 32) cs[j * 32 + i] = col;
}
static void make_sprites(void) {
    memset(SPR, 0, sizeof SPR);
    cs = SPR[S_Z];
    R(HX(0x3a4a2a),10,12,12,14);R(HX(0x222222),11,26,4,6);R(HX(0x222222),17,26,4,6);R(HX(0xc89070),12,3,8,9);
    R(HX(0xee2222),13,6,2,2);R(HX(0xee2222),17,6,2,2);R(HX(0x222222),22,16,8,3);R(HX(0x3a4a2a),8,13,3,8);R(HX(0x3a4a2a),21,13,3,8);
    cs = SPR[S_I];
    R(HX(0x8a4a2a),9,10,14,16);R(HX(0x5a2a12),10,26,5,6);R(HX(0x5a2a12),17,26,5,6);R(HX(0xa05a30),11,3,10,9);
    R(HX(0xeeeeee),10,1,2,4);R(HX(0xeeeeee),20,1,2,4);R(HX(0xffdd00),13,6,2,2);R(HX(0xffdd00),17,6,2,2);
    R(HX(0x8a4a2a),5,12,4,10);R(HX(0x8a4a2a),23,12,4,10);
    cs = SPR[S_D];
    R(HX(0xd07080),6,10,20,16);R(HX(0xa04858),8,26,6,6);R(HX(0xa04858),18,26,6,6);R(HX(0xe08090),8,3,16,11);
    R(HX(0xffffff),10,11,2,3);R(HX(0xffffff),14,11,2,3);R(HX(0xffffff),18,11,2,3);R(HX(0xffdd00),11,6,2,2);R(HX(0xffdd00),19,6,2,2);
    cs = SPR[S_DEAD];
    R(HX(0x660000),3,26,26,5);R(HX(0x444444),6,24,18,4);R(HX(0xaa5555),22,24,5,3);
    cs = SPR[S_H];
    R(HX(0xeeeeee),8,18,16,10);R(HX(0xdd0000),14,19,4,8);R(HX(0xdd0000),11,22,10,2);
    cs = SPR[S_A];
    R(HX(0xc9a21a),9,20,14,8);R(HX(0x443322),11,22,10,2);
    cs = SPR[S_G];
    R(HX(0x555555),3,24,26,3);R(HX(0x6a4a2a),3,24,9,5);
    cs = SPR[S_C];
    R(HX(0x666666),3,22,26,4);R(HX(0x333333),3,26,26,3);R(HX(0xccaa22),24,20,4,2);
    cs = SPR[S_K];
    R(HX(0x2a8a3a),8,14,16,14);R(HX(0x1a5a28),12,14,8,4);
    cs = SPR[S_B];
    R(HX(0x2288ff),9,16,14,12);R(HX(0xffffff),11,18,10,3);
    cs = SPR[S_Y];
    R(HX(0xeecc22),9,16,14,12);R(HX(0xffffff),11,18,10,3);
    cs = SPR[S_R];
    R(HX(0xee2222),9,16,14,12);R(HX(0xffffff),11,18,10,3);
    cs = SPR[S_O];
    R(HX(0x2f7a3a),9,8,14,22);R(HX(0x1a4a22),9,14,14,2);R(HX(0x1a4a22),9,22,14,2);R(HX(0x77dd44),12,10,3,3);
    cs = SPR[S_FB];
    R(HX(0xff6600),9,9,14,14);R(HX(0xffdd00),12,12,8,8);
}
static int spr_of(int t, int dead) {
    if (dead) return S_DEAD;
    switch (t) {
    case 'z': return S_Z; case 'i': return S_I; case 'd': return S_D; case 'h': return S_H;
    case 'a': return S_A; case 'g': return S_G; case 'c': return S_C; case 'k': return S_K;
    case 'b': return S_B; case 'y': return S_Y; case 'r': return S_R; case 'o': return S_O;
    default: return S_FB;
    }
}

/* ------------------------------------------------------------ datos de niveles */
typedef struct { const char *n; int w, v; float tint[3]; int ce[3], fl[3]; } Theme;
static const Theme THM[12] = {
 {"Metal",0,0,{1,1,1.1f},{70,70,85},{100,100,105}},
 {"Ladrillo infernal",1,1,{1.2f,.7f,.6f},{60,20,20},{90,40,30}},
 {"Toxico",0,7,{.7f,1.2f,.7f},{40,70,40},{50,100,50}},
 {"Piedra",7,7,{1,.9f,.8f},{40,35,30},{70,60,50}},
 {"Hielo",0,0,{.7f,.9f,1.4f},{50,60,90},{80,95,120}},
 {"Noche",7,8,{.8f,.8f,.9f},{10,10,25},{55,55,60}},
 {"Oxido",8,8,{1.2f,.9f,.6f},{50,35,25},{85,65,45}},
 {"Carne",1,7,{1.4f,.5f,.5f},{70,15,15},{110,30,30}},
 {"Gotico",7,1,{.9f,.7f,1.1f},{30,20,40},{60,45,75}},
 {"Lava",7,1,{1.3f,.8f,.4f},{60,25,5},{160,70,10}},
 {"Madera",8,1,{1,.8f,.6f},{45,30,20},{90,60,35}},
 {"Vacio",0,7,{.6f,.6f,1.3f},{5,5,20},{20,20,50}}};
static const char *KN[13] = {"Salas y puertas","Cueva","Laberinto","Arena con pilares","Carretera bloqueada",
 "Fortaleza circular","Apartamentos","Bosque de pilares","Espiral","Celdas de prision","Catedral",
 "Serpiente de pasillos","Plaza en cruz"};
typedef struct { const char *n; int k, t; const char *d; } ClEnt;
static const ClEnt CL[27] = {
 {"Hangar",0,0,"Salas y puertas de la base"},{"Nuclear Plant",6,2,"Edificio de camaras toxicas"},
 {"Toxin Refinery",11,6,"Serpiente de tuberias oxidadas"},{"Command Control",10,4,"Gran sala de mando"},
 {"Phobos Lab",2,11,"Laberinto de laboratorio"},{"Central Processing",5,0,"Anillos de procesamiento"},
 {"Computer Station",12,4,"Plaza en cruz de servidores"},{"Phobos Anomaly",3,9,"Arena de jefes"},
 {"Military Base",8,6,"Espiral secreta"},{"Deimos Anomaly",9,3,"Encerrado en celdas"},
 {"Containment Area",6,7,"Bunker de contencion"},{"Refinery",4,5,"Carretera bloqueada de noche"},
 {"Deimos Lab",1,2,"Cuevas contaminadas"},{"Command Center",0,10,"Mansion de mando"},
 {"Halls of the Damned",2,7,"Laberinto de carne"},{"Spawning Vats",7,2,"Bosque de tanques"},
 {"Tower of Babel",8,8,"Espiral de la torre"},{"Fortress of Mystery",5,10,"Fortaleza circular"},
 {"Hell Keep",10,1,"Salon de columnas infernal"},{"Slough of Despair",1,7,"Cuevas de carne"},
 {"Pandemonium",11,9,"Pasillos de lava"},{"House of Pain",9,7,"Prision sangrienta"},
 {"Unholy Cathedral",12,8,"Catedral en cruz"},{"Mt. Erebus",1,9,"Cavernas de fuego"},
 {"Limbo",3,11,"Arena del vacio"},{"Dis",2,9,"Laberinto de lava"},{"Warrens",4,3,"Carretera de piedra bloqueada"}};
static const char *DS[5] = {"MUY FACIL","FACIL","NORMAL","DIFICIL","NIGHTMARE"};
static const float DM[5] = {.5f, .75f, 1, 1, 1.5f};

typedef struct { char name[40]; int kind, th, lock, en; u32 seed; double L; char desc[80]; } Cfg;

static double rng_next(u32 *s) {
    u32 t;
    *s += 0x6D2B79F5u;
    t = (*s ^ (*s >> 15)) * (1u | *s);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (double)(t ^ (t >> 14)) / 4294967296.0;
}

static void cfg_for(int mode, int i, Cfg *c) {
    if (mode == 'c') {
        const ClEnt *e = &CL[i];
        snprintf(c->name, sizeof c->name, "%s", e->n);
        c->kind = e->k; c->th = e->t; c->seed = (u32)(i * 7919 + 13);
        c->lock = (i % 3 == 2 || e->k == 4);
        c->en = imin(30, 5 + i); c->L = (double)i / 26.0;
        snprintf(c->desc, sizeof c->desc, "%s", e->d);
    } else {
        u32 s = (u32)(i * 977 + 5);
        int k = (i * 7 + (i / 13)) % 13;
        int t = (int)(rng_next(&s) * 12);
        snprintf(c->name, sizeof c->name, "Ultra %d", i + 1);
        c->kind = k; c->th = t; c->seed = (u32)(i * 31337 + 7);
        c->lock = (rng_next(&s) < .45) || k == 4;
        c->en = imin(34, (int)(6 + i * .14));
        c->L = .1 + (double)i / 199.0 * .85;
        snprintf(c->desc, sizeof c->desc, "%s - %s", KN[k], THM[t].n);
    }
}

/* ------------------------------------------------------------ estado del juego */
#define MAXW 48
#define MAXH 32
#define MAXE 700
static char map[MAXH][MAXW];
static int MW, MH;

typedef struct {
    float x, y, vx, vy, hp, cd, pain;
    int t, aw, dead, gone, item, pj;
} Ent;
static Ent ents[MAXE];
static int nents;

typedef struct {
    float x, y, a, hp, ar, cd, rc, bob, hurtT, snd, face;
    int bul, sh, w, rage;
    int has[3], k[3];
} Player;
static Player P;

typedef struct { int kills, tk, items, ti; float time; } Stats;
static Stats ST;

enum { ST_BOOT, ST_TITLE, ST_MENU, ST_CAT, ST_PLAY, ST_DEAD, ST_STATS, ST_PAUSE };
enum { M_MAIN, M_MODE, M_OPT, M_HELP };
static int st = ST_BOOT, menu = M_MAIN, sel, diff = 2, lvl, cm = 'c', csel, cpg;
static float T, msgT;
static char msg[128];
static float fl[3];
static int prog[2] = {1, 1};
static Cfg CFG;
static const Theme *TH = &THM[0];
static struct { float sens; int mus, full; } opts = {1.0f, 1, 1};
static float mo[160];
static int mo_on;
static u32 CE[VH], FL[VH];
static float zb[VW];

static void S(int s) { st = s; T = 0; sel = 0; }
static void say(const char *m) { snprintf(msg, sizeof msg, "%s", m); msgT = 2.5f; }

static void setFlats(const Theme *t) {
    int y;
    for (y = 0; y < VH; y++) {
        int m = y < VH / 2;
        float d = (VH / 2) / (m ? VH / 2 - y + .5f : y - VH / 2 + .5f);
        float s = fmaxf2(.1f, fminf2(1, 1.1f - d / 9));
        const int *c = m ? t->ce : t->fl;
        (m ? CE : FL)[y] = rgbf(c[0] * s, c[1] * s, c[2] * s);
    }
}

static void newP(void) {
    memset(&P, 0, sizeof P);
    P.x = 1.5f; P.y = 1.5f; P.hp = 100; P.bul = 50; P.has[0] = 1;
}

/* ------------------------------------------------------------ generador de niveles */
static u32 rs_;
#define RN() rng_next(&rs_)
static int ri(int a, int b) { return a + (int)(RN() * (b - a + 1)); }
static void cvc(int x, int y, int w, int h, char ch) {
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if (j > 0 && i > 0 && j < MH - 1 && i < MW - 1) map[j][i] = ch;
}
static void cv(int x, int y, int w, int h) { cvc(x, y, w, h, '.'); }
static void lk(int ax, int ay, int bx, int by) {
    cv(imin(ax, bx), ay, abs(ax - bx) + 1, 1);
    cv(bx, imin(ay, by), 1, abs(ay - by) + 1);
}
/* emula el Array.prototype.sort de V8 con comparador aleatorio (para repetir los mismos niveles) */
static double cmpr(void) { return RN() - .5; }
static void v8sort(int *a, int n) {
    int low = 1, run = 2, desc, idx, start;
    double order;
    if (n < 2) return;
    order = cmpr();
    desc = order < 0;
    for (idx = low + 1; idx < n; idx++) {
        order = cmpr();
        if (desc) { if (order >= 0) break; } else { if (order < 0) break; }
        run++;
    }
    if (desc) { int i, j; for (i = 0, j = run - 1; i < j; i++, j--) { int t = a[i]; a[i] = a[j]; a[j] = t; } }
    if (run < n) {
        for (start = run; start < n; start++) {
            int left = 0, right = start, pivot = a[right], p;
            while (left < right) {
                int mid = left + ((right - left) >> 1);
                order = cmpr();
                if (order < 0) right = mid; else left = mid + 1;
            }
            for (p = start; p > left; p--) a[p] = a[p - 1];
            a[left] = pivot;
        }
    }
}

static const int N4[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
static short lab[MAXW * MAXH];
static short stk[MAXW * MAXH][2];
static short comp[MAXW * MAXH][2], best[MAXW * MAXH][2];
static short qq[MAXW * MAXH][2];
static short dist_[MAXW * MAXH];
static short cellsd[MAXW * MAXH];
static short fcl[MAXW * MAXH][3];
static unsigned char used_[MAXW * MAXH];
static int nfc;

static int psb(int x, int y) { return map[y][x] == '.' || map[y][x] == 'D'; }

static int take(int mn, int mx, int *ox, int *oy) {
    int t;
    for (t = 0; t < 90; t++) {
        int idx = (int)(RN() * nfc);
        int cx = fcl[idx][0], cy = fcl[idx][1], cd = fcl[idx][2];
        if (cd >= mn && cd <= mx && !used_[cy * MW + cx] && map[cy][cx] == '.') {
            used_[cy * MW + cx] = 1; *ox = cx; *oy = cy; return 1;
        }
    }
    return 0;
}
static void put(char ch, int n, int mn, int mx) {
    int i, x, y;
    for (i = 0; i < n; i++) if (take(mn, mx, &x, &y)) map[y][x] = ch;
}

static void add_ent(int c, int x, int y) {
    Ent *e;
    int en = (c == 'z' || c == 'i' || c == 'd');
    if (en && diff == 0 && (x + y) % 2) return;
    if (nents >= MAXE) return;
    e = &ents[nents++];
    memset(e, 0, sizeof *e);
    e->x = x + .5f; e->y = y + .5f; e->t = c; e->cd = 1;
    e->hp = c == 'z' ? 20 : c == 'i' ? 60 : c == 'd' ? 150 : c == 'o' ? 20 : 0;
    if (en) ST.tk++; else if (c != 'o') { e->item = 1; ST.ti++; }
}

static void build(const Cfg *c) {
    int k = c->kind, W, H, x, y, i, j;
    int sx, sy, nbest = 0, ncomp, lb = 0, dmax;
    rs_ = c->seed;
    if (k == 4) { W = 46; H = 24; }
    else { W = 26 + (int)(RN() * 10); H = 22 + (int)(RN() * 8); }
    MW = W; MH = H;
    memset(map, 0, sizeof map);
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) map[y][x] = '#';

    if (k == 0 || k == 12) {
        struct { int x, y, w, h; } rs[16];
        int nr = 0;
        if (k == 12) {
            int pos[4][2], q;
            cv(1, H / 2 - 2, W - 2, 4);
            cv(W / 2 - 2, 1, 4, H - 2);
            pos[0][0] = 2; pos[0][1] = 2; pos[1][0] = W - 8; pos[1][1] = 2;
            pos[2][0] = 2; pos[2][1] = H - 7; pos[3][0] = W - 8; pos[3][1] = H - 7;
            for (q = 0; q < 4; q++) { cv(pos[q][0], pos[q][1], 6, 5); rs[nr].x = pos[q][0]; rs[nr].y = pos[q][1]; nr++; }
            for (q = 0; q < nr; q++) lk(rs[q].x + 3, rs[q].y + 2, W / 2, H / 2);
        } else {
            int n;
            for (n = 0; n < 60 && nr < 10; n++) {
                int w = ri(4, 9), h = ri(4, 8), xx = ri(1, W - w - 2), yy = ri(1, H - h - 2), ok = 1, q;
                for (q = 0; q < nr; q++)
                    if (xx < rs[q].x + rs[q].w + 2 && rs[q].x < xx + w + 2 && yy < rs[q].y + rs[q].h + 2 && rs[q].y < yy + h + 2) { ok = 0; break; }
                if (ok) { rs[nr].x = xx; rs[nr].y = yy; rs[nr].w = w; rs[nr].h = h; nr++; cv(xx, yy, w, h); }
            }
            for (i = 1; i < nr; i++)
                lk((int)(rs[i-1].x + rs[i-1].w / 2.0), (int)(rs[i-1].y + rs[i-1].h / 2.0),
                   (int)(rs[i].x + rs[i].w / 2.0), (int)(rs[i].y + rs[i].h / 2.0));
        }
    } else if (k == 1) {
        for (y = 1; y < H - 1; y++) for (x = 1; x < W - 1; x++) map[y][x] = RN() < .43 ? '#' : '.';
        for (i = 0; i < 4; i++) {
            static char o[MAXH][MAXW];
            memcpy(o, map, sizeof o);
            for (y = 1; y < H - 1; y++) for (x = 1; x < W - 1; x++) {
                int n = 0;
                for (j = -1; j < 2; j++) { int ii; for (ii = -1; ii < 2; ii++) if (o[y + j][x + ii] == '#') n++; }
                map[y][x] = n >= 5 ? '#' : '.';
            }
        }
    } else if (k == 2) {
        int sp = 0, n;
        stk[0][0] = 1; stk[0][1] = 1; sp = 1; map[1][1] = '.';
        while (sp) {
            static const int dd[4][2] = {{2,0},{-2,0},{0,2},{0,-2}};
            int c0 = stk[sp-1][0], d0 = stk[sp-1][1], ds[4][2], nd = 0, d, a, b;
            for (d = 0; d < 4; d++) {
                int aa = dd[d][0], bb = dd[d][1];
                if (c0 + aa > 0 && d0 + bb > 0 && c0 + aa < W - 1 && d0 + bb < H - 1 && map[d0 + bb][c0 + aa] == '#') {
                    ds[nd][0] = aa; ds[nd][1] = bb; nd++;
                }
            }
            if (!nd) { sp--; continue; }
            d = (int)(RN() * nd); a = ds[d][0]; b = ds[d][1];
            map[d0 + b / 2][c0 + a / 2] = '.';
            map[d0 + b][c0 + a] = '.';
            stk[sp][0] = c0 + a; stk[sp][1] = d0 + b; sp++;
        }
        for (n = 0; n < 30; n++) {
            int xx = ri(2, W - 3), yy = ri(2, H - 3);
            if (map[yy][xx] == '#' && ((map[yy][xx-1] == '.' && map[yy][xx+1] == '.') || (map[yy-1][xx] == '.' && map[yy+1][xx] == '.')))
                map[yy][xx] = '.';
        }
    } else if (k == 3) {
        int n;
        cv(1, 1, W - 2, H - 2);
        for (n = 0; n < 45; n++) { int r0 = ri(2, H - 3), c0 = ri(2, W - 3); map[r0][c0] = '%'; }
        cvc(W / 2 - 2, H / 2 - 2, 4, 4, '%');
    } else if (k == 4) {
        int my = H / 2, xx, n;
        cv(1, my - 2, W - 2, 5);
        for (xx = 6; xx < W - 4; xx += 5) {
            int a[5] = {0,1,2,3,4}, cnt, q;
            v8sort(a, 5);
            cnt = ri(1, 3);
            for (q = 0; q < cnt; q++) cvc(xx + ri(0, 1), my - 2 + a[q], 2, 1, '%');
        }
        for (xx = 4; xx < W - 8; xx += 9) {
            int up = RN() < .5;
            cv(xx, up ? my - 8 : my + 5, 6, 4);
            cv(xx + 2, up ? my - 4 : my + 3, 1, 2);
        }
        (void)n;
    } else if (k == 5) {
        int q;
        cv(1, 1, W - 2, H - 2);
        for (q = 0; q < 3; q++) {
            int a = 3 + q * 4, x0 = a, y0 = a, x1 = W - 1 - a, y1 = H - 1 - a, s;
            if (x1 - x0 < 4 || y1 - y0 < 4) break;
            for (x = x0; x <= x1; x++) { map[y0][x] = '#'; map[y1][x] = '#'; }
            for (y = y0; y <= y1; y++) { map[y][x0] = '#'; map[y][x1] = '#'; }
            s = ri(0, 3);
            if (s == 0) cv(ri(x0 + 1, x1 - 2), y0, 2, 1);
            else if (s == 1) cv(ri(x0 + 1, x1 - 2), y1, 2, 1);
            else if (s == 2) cv(x0, ri(y0 + 1, y1 - 2), 1, 2);
            else cv(x1, ri(y0 + 1, y1 - 2), 1, 2);
        }
    } else if (k == 6) {
        int xs[16], ys[16], nx = 0, ny = 0, a;
        cv(1, 1, W - 2, H - 2);
        for (x = 7; x < W - 3; x += 7) xs[nx++] = x;
        for (y = 7; y < H - 3; y += 7) ys[ny++] = y;
        for (a = 0; a < nx; a++) for (y = 1; y < H - 1; y++) map[y][xs[a]] = '#';
        for (a = 0; a < ny; a++) for (x = 1; x < W - 1; x++) map[ys[a]][x] = '#';
        for (a = 0; a < nx; a++) for (y = 0; y < H - 1; y += 7) map[imin(H - 2, y + ri(1, 5))][xs[a]] = 'D';
        for (a = 0; a < ny; a++) for (x = 0; x < W - 1; x += 7) map[ys[a]][imin(W - 2, x + ri(1, 5))] = 'D';
    } else if (k == 7) {
        int n;
        cv(1, 1, W - 2, H - 2);
        for (n = 0; n < W * H * .12; n++) { int r0 = ri(2, H - 3), c0 = ri(2, W - 3); map[r0][c0] = RN() < .5 ? '#' : '%'; }
    } else if (k == 8) {
        int l;
        for (l = 0;; l++) {
            int x0 = 1 + 2 * l, y0 = 1 + 2 * l, x1 = W - 2 - 2 * l, y1 = H - 2 - 2 * l;
            if (x1 - x0 < 2 || y1 - y0 < 2) break;
            cv(x0, y0, x1 - x0 + 1, 1); cv(x1, y0, 1, y1 - y0 + 1); cv(x0, y1, x1 - x0 + 1, 1);
            cv(x0, y0 + 2, 1, y1 - y0 - 1); cv(x0 + 1, y0 + 2, 1, 1);
        }
    } else if (k == 9) {
        int my = H / 2;
        cv(1, my - 1, W - 2, 3);
        for (x = 2; x < W - 5; x += 5) {
            cv(x, my - 6, 4, 4); map[my - 2][x + 1] = 'D';
            cv(x, my + 3, 4, 4); map[my + 2][x + 1] = 'D';
        }
    } else if (k == 10) {
        int a, ys3[3];
        cv(2, 2, W - 4, H - 4);
        ys3[0] = 5; ys3[1] = H - 6; ys3[2] = H / 2;
        for (x = 5; x < W - 5; x += 4) for (a = 0; a < 3; a++) map[ys3[a]][x] = '%';
        cv(W / 2 - 3, 1, 6, 2);
    } else {
        int f = 0;
        for (y = 2; y < H - 3; y += 4) {
            cv(1, y, W - 2, 2);
            if (y + 4 < H - 3) { if (f) cv(1, y + 2, 2, 2); else cv(W - 3, y + 2, 2, 2); }
            f ^= 1;
        }
    }

    /* quedarse con la mayor zona conectada */
    memset(lab, 0, sizeof lab);
    for (y = 1; y < H - 1; y++) for (x = 1; x < W - 1; x++)
        if (psb(x, y) && !lab[y * W + x]) {
            int sp = 0;
            ncomp = 0;
            stk[sp][0] = x; stk[sp][1] = y; sp++;
            lab[y * W + x] = ++lb;
            while (sp) {
                int a, b, d;
                sp--; a = stk[sp][0]; b = stk[sp][1];
                comp[ncomp][0] = a; comp[ncomp][1] = b; ncomp++;
                for (d = 0; d < 4; d++) {
                    int nx = a + N4[d][0], ny = b + N4[d][1];
                    if (psb(nx, ny) && !lab[ny * W + nx]) { lab[ny * W + nx] = lb; stk[sp][0] = nx; stk[sp][1] = ny; sp++; }
                }
            }
            if (ncomp > nbest) { memcpy(best, comp, sizeof(short) * 2 * ncomp); nbest = ncomp; }
        }
    {
        static unsigned char inb[MAXW * MAXH];
        memset(inb, 0, sizeof inb);
        for (i = 0; i < nbest; i++) inb[best[i][1] * W + best[i][0]] = 1;
        for (y = 1; y < H - 1; y++) for (x = 1; x < W - 1; x++)
            if (psb(x, y) && !inb[y * W + x]) map[y][x] = '#';
    }
    if (k == 0 || k == 12)
        for (y = 1; y < H - 1; y++) for (x = 1; x < W - 1; x++)
            if (map[y][x] == '.' && RN() < .4 &&
                ((map[y][x-1] == '#' && map[y][x+1] == '#' && map[y-1][x] == '.' && map[y+1][x] == '.') ||
                 (map[y-1][x] == '#' && map[y+1][x] == '#' && map[y][x-1] == '.' && map[y][x+1] == '.')))
                map[y][x] = 'D';

    sx = sy = 1;
    for (i = 0; i < nbest; i++) if (map[best[i][1]][best[i][0]] == '.') { sx = best[i][0]; sy = best[i][1]; break; }
    /* BFS desde la salida del jugador */
    {
        int qh = 0, qn = 0;
        for (i = 0; i < W * H; i++) dist_[i] = -1;
        dist_[sy * W + sx] = 0; qq[qn][0] = sx; qq[qn][1] = sy; qn++;
        nfc = 0; dmax = 0;
        for (; qh < qn; qh++) {
            int a = qq[qh][0], b = qq[qh][1], d = dist_[b * W + a], dd;
            cellsd[qh] = d; dmax = d;
            if (map[b][a] == '.') { fcl[nfc][0] = a; fcl[nfc][1] = b; fcl[nfc][2] = d; nfc++; }
            for (dd = 0; dd < 4; dd++) {
                int nx = a + N4[dd][0], ny = b + N4[dd][1];
                if (psb(nx, ny) && dist_[ny * W + nx] < 0) { dist_[ny * W + nx] = d + 1; qq[qn][0] = nx; qq[qn][1] = ny; qn++; }
            }
        }
    }
    for (i = nfc - 1; i >= 0; i--) {
        int a = fcl[i][0], b = fcl[i][1], d, found = 0;
        for (d = 0; d < 4; d++)
            if (map[b + N4[d][1]][a + N4[d][0]] == '#') { map[b + N4[d][1]][a + N4[d][0]] = 'X'; found = 1; break; }
        if (found) break;
    }
    memset(used_, 0, sizeof used_);
    used_[sy * W + sx] = 1;
    {
        double L = c->L; int n = c->en;
        put('h', 3 + n / 6, 3, 1000000000);
        put('a', 4 + n / 5, 3, 1000000000);
        put('a', 1, 2, 6);
        put('h', 1, 2, 8);
        { int cnt = ri(2, 5); put('o', cnt, 5, 1000000000); }
        if (L > .15) put('k', 1, 6, 1000000000);
        if (L > .04) put('g', 1, 4, 1000000000);
        if (L > .25) put('c', 1, 6, 1000000000);
        for (i = 0; i < n; i++) {
            double u = RN();
            char ch = u < .5 - L * .3 ? 'z' : ((u < .8 || L < .2) ? 'i' : 'd');
            put(ch, 1, 6, 1000000000);
        }
        if (c->lock) put('r', 1, imax(6, (int)(dmax * .4)), 1000000000);
    }
    nents = 0; memset(&ST, 0, sizeof ST);
    for (y = 0; y < H; y++) for (x = 0; x < W; x++)
        if (strchr("zidohagckbyr", map[y][x]) && map[y][x]) { add_ent(map[y][x], x, y); map[y][x] = '.'; }
    P.x = sx + .5f; P.y = sy + .5f;
    {
        static const float angs[4] = {0, PI_F / 2, PI_F, -PI_F / 2};
        static const int D4[4][2] = {{1,0},{0,1},{-1,0},{0,-1}};
        int d, ai_ = 0;
        for (d = 0; d < 4; d++) {
            int nx = sx + D4[d][0], ny = sy + D4[d][1];
            if (nx >= 0 && ny >= 0 && nx < W && ny < H && map[ny][nx] == '.') { ai_ = d; break; }
        }
        P.a = angs[ai_];
    }
}


/* ------------------------------------------------------------ entrada */
static u32 btn, btnp, rptm;
static float axn, ayn, holdT[4];
static unsigned fcount;

static float dz(int v) {
    float f = (float)v / 110.0f, a = fabsf(f);
    if (a < .22f) return 0;
    a = (a - .22f) / .78f;
    if (a > 1) a = 1;
    return f < 0 ? -a : a;
}
static void input_begin(const Input *in, float dt) {
    static u32 prev;
    int k;
    btn = in->buttons; btnp = btn & ~prev; prev = btn; rptm = btnp;
    for (k = 0; k < 4; k++) {
        u32 bit = 1u << k;
        if (btn & bit) { holdT[k] += dt; if (holdT[k] > .35f) { holdT[k] -= .09f; rptm |= bit; } }
        else holdT[k] = 0;
    }
    axn = dz(in->ax); ayn = dz(in->ay);
}

/* ------------------------------------------------------------ progreso */
#define SAVE_MAGIC 0x50445350
static void save_all(void) {
    int v[8];
    v[0] = SAVE_MAGIC; v[1] = prog[0]; v[2] = prog[1]; v[3] = (int)(opts.sens * 2 + .5f);
    v[4] = opts.mus; v[5] = opts.full; v[6] = diff; v[7] = 0;
    plat_save_data(v, sizeof v);
}
static void load_all(void) {
    int v[8];
    if (plat_load_data(v, sizeof v) == (int)sizeof v && v[0] == SAVE_MAGIC) {
        prog[0] = clampi(v[1], 1, 27); prog[1] = clampi(v[2], 1, 200);
        opts.sens = clampi(v[3], 1, 4) / 2.0f; opts.mus = v[4] ? 1 : 0; opts.full = v[5] ? 1 : 0;
        diff = clampi(v[6], 0, 4);
    }
    game_opt_full = opts.full;
}

/* ------------------------------------------------------------ colisiones y rayos */
static float rD, rU;
static int rS, rX, rY;
static char rC;

static int is_solid_c(char c) {
    return c == 0 || c == '#' || c == '%' || c == 'D' || c == 'B' || c == 'Y' || c == 'R' || c == 'X';
}
static char mapc(int x, int y) {
    if (x < 0 || y < 0 || x >= MW || y >= MH) return 0;
    return map[y][x];
}
static int solid(float x, float y) { return is_solid_c(mapc((int)x, (int)y)); }
static int freep(float x, float y, float r) {
    return !(solid(x - r, y - r) || solid(x + r, y - r) || solid(x - r, y + r) || solid(x + r, y + r));
}
static void mvxy(float *x, float *y, float dx, float dy, float r) {
    if (freep(*x + dx, *y, r)) *x += dx;
    if (freep(*x, *y + dy, r)) *y += dy;
}
static void cast(float ox, float oy, float dx, float dy) {
    int mx = (int)ox, my = (int)oy, sx, sy, s = 0, i;
    float ddx, ddy, sdx, sdy, w;
    char c = 0;
    if (fabsf(dx) < 1e-9f) dx = dx < 0 ? -1e-9f : 1e-9f;
    if (fabsf(dy) < 1e-9f) dy = dy < 0 ? -1e-9f : 1e-9f;
    ddx = fabsf(1.0f / dx); ddy = fabsf(1.0f / dy);
    if (dx < 0) { sx = -1; sdx = (ox - mx) * ddx; } else { sx = 1; sdx = (mx + 1 - ox) * ddx; }
    if (dy < 0) { sy = -1; sdy = (oy - my) * ddy; } else { sy = 1; sdy = (my + 1 - oy) * ddy; }
    for (i = 0; i < 48; i++) {
        if (sdx < sdy) { sdx += ddx; mx += sx; s = 0; } else { sdy += ddy; my += sy; s = 1; }
        c = mapc(mx, my);
        if (c == 0) c = '#';
        if (is_solid_c(c)) break;
    }
    if (!is_solid_c(c)) c = '#';
    rD = s ? sdy - ddy : sdx - ddx;
    rC = c; rS = s; rX = mx; rY = my;
    w = s ? ox + rD * dx : oy + rD * dy;
    rU = w - floorf(w);
}

/* ------------------------------------------------------------ combate */
static void hurt(float n) {
    n *= DM[diff];
    if (P.ar > 0) { float a = fminf2(P.ar, n / 3); P.ar -= a; n -= a; }
    P.hp -= n;
    fl[0] = fminf2(.6f, n / 40); P.hurtT = .7f; sfx(SFX_HURT);
    if (P.hp <= 0) { P.hp = 0; sfx(SFX_DIE); S(ST_DEAD); }
}
static void dmgE(Ent *e, float n);
static void boom(Ent *o) {
    int i; float d;
    o->gone = 1; sfx(SFX_BOOM); fl[1] = .6f;
    for (i = 0; i < nents; i++) {
        Ent *e = &ents[i];
        if (e->item || e->gone || e->dead || e->pj || e == o) continue;
        d = hypotf(e->x - o->x, e->y - o->y);
        if (d < 2.5f) dmgE(e, 70 * (1 - d / 2.5f));
    }
    d = hypotf(P.x - o->x, P.y - o->y);
    if (d < 2.5f) hurt(70 * (1 - d / 2.5f));
}
static void dmgE(Ent *e, float n) {
    if (e->dead || e->gone) return;
    e->hp -= n; e->aw = 1;
    if (e->hp <= 0) {
        if (e->t == 'o') { boom(e); return; }
        e->dead = 1; ST.kills++; sfx(SFX_DIE);
    } else if (frnd() < .5f) { e->pain = .3f; sfx(SFX_PAIN); }
}
static void shoot(float spread) {
    float a = P.a + spread, dx = cosf(a), dy = sinf(a), bd;
    Ent *best = NULL;
    int i;
    cast(P.x, P.y, dx, dy); bd = rD;
    for (i = 0; i < nents; i++) {
        Ent *e = &ents[i];
        float ex, ey, al, pp;
        if (e->item || e->dead || e->gone || e->pj) continue;
        ex = e->x - P.x; ey = e->y - P.y;
        al = ex * dx + ey * dy; pp = fabsf(-ex * dy + ey * dx);
        if (al > 0 && al < bd && pp < .35f) { best = e; bd = al; }
    }
    if (best) dmgE(best, 5 + (int)(frnd() * 10));
}
static void fire(void) {
    static const float cds[3] = {.4f, .9f, .11f};
    int w = P.w, i, *ammo = (w == 1) ? &P.sh : &P.bul;
    if (P.cd > 0) return;
    if (*ammo < 1) return;
    (*ammo)--;
    P.cd = cds[w]; P.rc = 1; P.snd = .4f; P.rage = 1;
    sfx(w == 1 ? SFX_SG : SFX_PI);
    for (i = 0; i < (w == 1 ? 7 : 1); i++) shoot((frnd() - .5f) * (w == 1 ? .2f : .04f));
}
static void nextW(void) {
    int i;
    for (i = 1; i <= 3; i++) { int w = (P.w + i) % 3; if (P.has[w]) { P.w = w; sfx(SFX_TIC); return; } }
}
static void use(void) {
    float dx = cosf(P.a), dy = sinf(P.a);
    cast(P.x, P.y, dx, dy);
    if (rD > 1.6f) return;
    if (rC == 'D') { map[rY][rX] = '.'; sfx(SFX_DOOR); }
    else if (rC == 'B' || rC == 'Y' || rC == 'R') {
        int k = rC == 'B' ? 0 : rC == 'Y' ? 1 : 2;
        if (P.k[k]) { map[rY][rX] = '.'; sfx(SFX_DOOR); }
        else { say(k == 0 ? "Necesitas la llave azul" : k == 1 ? "Necesitas la llave amarilla" : "Necesitas la llave roja"); sfx(SFX_TIC); }
    } else if (rC == 'X') {
        int idx = cm == 'c' ? 0 : 1;
        if (CFG.lock && !P.k[2]) { say("La salida necesita la llave roja"); sfx(SFX_TIC); return; }
        if (lvl + 1 == prog[idx] && lvl + 1 < (cm == 'c' ? 27 : 200)) { prog[idx]++; save_all(); }
        sfx(SFX_SEL); S(ST_STATS);
    }
}
static void pick(Ent *e) {
    int t = e->t; const char *m;
    if (t == 'h') { if (P.hp >= 100) return; P.hp = fminf2(100, P.hp + 25); m = "Botiquin"; }
    else if (t == 'a') { P.bul = imin(200, P.bul + 20); m = "Balas"; }
    else if (t == 'g') { P.has[1] = 1; P.sh = imin(50, P.sh + 8); P.w = 1; m = "Escopeta!"; P.face = 1; }
    else if (t == 'c') { P.has[2] = 1; P.bul = imin(200, P.bul + 20); P.w = 2; m = "Ametralladora!"; P.face = 1; }
    else if (t == 'k') { P.ar = 100; m = "Armadura"; }
    else if (t == 'b') { P.k[0] = 1; m = "Llave azul"; }
    else if (t == 'y') { P.k[1] = 1; m = "Llave amarilla"; }
    else { P.k[2] = 1; m = "Llave roja"; }
    e->gone = 1; ST.items++; fl[2] = .3f; say(m); sfx(SFX_PICK);
}
static void spawn_fb(float x, float y, float vx, float vy) {
    int i;
    Ent *e = NULL;
    for (i = 0; i < nents; i++) if (ents[i].pj && ents[i].gone) { e = &ents[i]; break; }
    if (!e) { if (nents >= MAXE) return; e = &ents[nents++]; }
    memset(e, 0, sizeof *e);
    e->t = 'f'; e->pj = 1; e->x = x; e->y = y; e->vx = vx; e->vy = vy;
}
static void ai(Ent *e, float dt) {
    float dx, dy, d, ux, uy, spd, cdm;
    int los;
    if (e->item || e->dead || e->gone || e->t == 'o') return;
    dx = P.x - e->x; dy = P.y - e->y; d = hypotf(dx, dy); if (d < .0001f) d = .01f;
    ux = dx / d; uy = dy / d;
    e->cd -= dt; e->pain -= dt;
    cast(e->x, e->y, ux, uy); los = rD > d;
    if (!e->aw) {
        if ((los && d < 12) || (P.snd > 0 && d < 14)) { e->aw = 1; sfx(SFX_MON); }
        else return;
    }
    if (e->pain > 0) return;
    spd = (e->t == 'z' ? 1.2f : e->t == 'i' ? 1.5f : 3.0f) * (diff == 4 ? 1.6f : 1) * dt;
    cdm = diff == 4 ? .6f : 1;
    if (e->t == 'z') {
        if (los && e->cd <= 0 && d < 12) {
            e->cd = 1.5f * cdm; sfx(SFX_PI);
            if (frnd() < fmaxf2(.3f, 1 - d / 12)) hurt((float)(3 + (int)(frnd() * 12)));
        } else if (d > 3) mvxy(&e->x, &e->y, ux * spd, uy * spd, .3f);
    } else if (e->t == 'i') {
        if (d < 1.2f) { if (e->cd <= 0) { e->cd = 1 * cdm; hurt((float)(3 + (int)(frnd() * 18))); } }
        else if (los && e->cd <= 0 && d < 10) { e->cd = 1.9f * cdm; sfx(SFX_FB); spawn_fb(e->x, e->y, ux * 4, uy * 4); }
        else if (d > 2 || !los) mvxy(&e->x, &e->y, ux * spd, uy * spd, .3f);
    } else {
        if (d < 1) { if (e->cd <= 0) { e->cd = .8f * cdm; hurt((float)(4 + (int)(frnd() * 20))); } }
        else mvxy(&e->x, &e->y, ux * spd, uy * spd, .3f);
    }
}

static void upd(float dt) {
    float f, s, turn, m, spd, ca, sa;
    int i;
    if (st != ST_PLAY) return;
    ST.time += dt;
    f = -ayn; s = 0; turn = axn;
    if (btn & BTN_UP) f += 1;
    if (btn & BTN_DOWN) f -= 1;
    if (btn & BTN_RIGHT) turn += 1;
    if (btn & BTN_LEFT) turn -= 1;
    if (btn & BTN_R) s += 1;
    if (btn & BTN_L) s -= 1;
    if (turn > 1) turn = 1;
    if (turn < -1) turn = -1;
    if (f > 1) f = 1;
    if (f < -1) f = -1;
    P.a += turn * 2.6f * opts.sens * dt;
    m = hypotf(f, s);
    if (m > 1) { f /= m; s /= m; }
    spd = 3.4f * dt; ca = cosf(P.a); sa = sinf(P.a);
    mvxy(&P.x, &P.y, (ca * f - sa * s) * spd, (sa * f + ca * s) * spd, .25f);
    if (m > .1f) P.bob += dt * 9;
    P.cd -= dt; P.rc = fmaxf2(0, P.rc - dt * 6); P.snd -= dt; P.hurtT -= dt;
    P.face = P.face > 0 ? fmaxf2(0, P.face - dt * .5f) : 0;
    if (btn & (BTN_CROSS | BTN_SQUARE)) fire();
    for (i = 0; i < nents; i++) {
        Ent *e = &ents[i];
        if (e->gone) continue;
        if (e->pj) {
            e->x += e->vx * dt; e->y += e->vy * dt;
            if (solid(e->x, e->y)) e->gone = 1;
            else if (hypotf(e->x - P.x, e->y - P.y) < .5f) { e->gone = 1; hurt((float)(3 + (int)(frnd() * 17))); }
        } else if (e->item) {
            if (hypotf(e->x - P.x, e->y - P.y) < .6f) pick(e);
        } else ai(e, dt);
    }
    for (i = 0; i < 3; i++) fl[i] = fmaxf2(0, fl[i] - dt * .8f);
    if (msgT > 0) msgT -= dt;
}

/* ------------------------------------------------------------ render 3D */
static int texi(char c) {
    switch (c) { case 'D': return 2; case 'B': return 3; case 'Y': return 4; case 'R': return 5; case 'X': return 6; default: return 0; }
}
static int ord_[MAXE];
static float ordd_[MAXE], otx_[MAXE], oty_[MAXE];

static void hud(void);

static void render(void) {
    static const float one[3] = {1, 1, 1};
    float ca = cosf(P.a), sa = sinf(P.a), px = -sa * .66f, py = ca * .66f, lt = P.rc * .35f + fl[1] * .4f;
    int x, y, i, n;
    float inv;
    for (x = 0; x < VW; x++) {
        float cmv = 2.0f * x / VW - 1.0f, d, rr;
        int lh, top, wl, ti, tcol, sh, sR, sG, sB, y0, y1, step, tp;
        const u32 *t; const float *tn;
        u32 *dst;
        cast(P.x, P.y, ca + px * cmv, sa + py * cmv);
        d = fmaxf2(rD, .05f); zb[x] = d;
        lh = (int)(VH / d); top = (VH - lh) >> 1;
        wl = (rC == '#' || rC == '%');
        ti = rC == '#' ? TH->w : rC == '%' ? TH->v : texi(rC);
        t = TX[ti]; tn = wl ? TH->tint : one;
        tcol = imin(63, (int)(rU * 64));
        rr = fminf2(1, fmaxf2(.15f, 1.15f - d / 12 + lt)) * (rS ? .78f : 1) * 256;
        sh = (int)rr; sR = (int)(sh * tn[0]); sG = (int)(sh * tn[1]); sB = (int)(sh * tn[2]);
        y0 = imax(top, 0); y1 = imin(top + lh, VH);
        dst = &game_fb[x];
        for (y = 0; y < y0 && y < VH; y++) dst[y * SW] = CE[y];
        step = lh > 0 ? (int)(64.0f * 65536.0f / lh) : 0;
        tp = (y0 - top) * step;
        for (y = y0; y < y1; y++) {
            int v = tp >> 16; u32 c; int cr, cg, cb;
            if (v > 63) v = 63;
            c = t[v * 64 + tcol]; tp += step;
            cb = (((c >> 16) & 255) * sB) >> 8; cg = (((c >> 8) & 255) * sG) >> 8; cr = ((c & 255) * sR) >> 8;
            if (cb > 255) cb = 255;
            if (cg > 255) cg = 255;
            if (cr > 255) cr = 255;
            dst[y * SW] = 0xFF000000u | ((u32)cb << 16) | ((u32)cg << 8) | (u32)cr;
        }
        for (y = imax(y1, 0); y < VH; y++) dst[y * SW] = FL[y];
    }
    /* sprites */
    inv = 1.0f / (px * sa - ca * py);
    n = 0;
    for (i = 0; i < nents; i++) {
        Ent *e = &ents[i];
        float sx, sy, tx_, ty_;
        if (e->gone) continue;
        sx = e->x - P.x; sy = e->y - P.y;
        tx_ = inv * (sa * sx - ca * sy); ty_ = inv * (-py * sx + px * sy);
        if (ty_ < .15f) continue;
        ord_[n] = i; ordd_[n] = sx * sx + sy * sy; otx_[n] = tx_; oty_[n] = ty_; n++;
    }
    { /* ordenar de lejos a cerca (insercion) */
        int a;
        for (a = 1; a < n; a++) {
            int io = ord_[a]; float dd = ordd_[a], tt = otx_[a], yy = oty_[a], b = a - 1;
            int bb = a - 1;
            (void)b;
            while (bb >= 0 && ordd_[bb] < dd) {
                ord_[bb + 1] = ord_[bb]; ordd_[bb + 1] = ordd_[bb]; otx_[bb + 1] = otx_[bb]; oty_[bb + 1] = oty_[bb]; bb--;
            }
            ord_[bb + 1] = io; ordd_[bb + 1] = dd; otx_[bb + 1] = tt; oty_[bb + 1] = yy;
        }
    }
    for (i = 0; i < n; i++) {
        Ent *e = &ents[ord_[i]];
        float ty_ = oty_[i], tx_ = otx_[i], h, hh, scx, top, x0, sh, q;
        const u32 *spr;
        int cx, cxe, ys, ye, key;
        key = spr_of(e->t, e->dead);
        spr = SPR[key];
        h = VH / ty_; hh = h * SC[key]; scx = (VW / 2) * (1 + tx_ / ty_);
        top = VH / 2 + h / 2 - hh; x0 = scx - hh / 2;
        sh = fmaxf2(.2f, fminf2(1, 1.2f - ty_ / 12 + lt));
        q = sh * (e->pain > 0 ? 1.6f : 1.0f);
        cx = imax(0, (int)x0); cxe = (int)ceilf(fminf2(VW, x0 + hh));
        ys = imax(0, (int)top); ye = (int)ceilf(fminf2(VH, top + hh));
        for (; cx < cxe; cx++) {
            int u, y;
            if (zb[cx] < ty_) continue;
            u = (int)((cx - x0) * 32 / hh);
            if (u < 0) u = 0;
            if (u > 31) u = 31;
            for (y = ys; y < ye; y++) {
                int v = (int)((y - top) * 32 / hh); u32 c;
                if (v < 0) v = 0;
                if (v > 31) v = 31;
                c = spr[v * 32 + u];
                if ((c >> 24) < 128) continue;
                game_fb[y * SW + cx] = rgbf((c & 255) * q, ((c >> 8) & 255) * q, ((c >> 16) & 255) * q);
            }
        }
    }
    /* arma */
    {
        static const int wds[3] = {10, 14, 20}, lns[3] = {30, 44, 38};
        static const u32 gc[3] = {0xFF777777u, 0xFF555555u, 0xFF444444u};
        int wd = wds[P.w], ln = lns[P.w];
        int bx = (int)floorf(160 + sinf(P.bob) * 4 + .5f);
        int by = (int)floorf(VH + fabsf(cosf(P.bob)) * 3 + P.rc * 9 + .5f);
        if (P.rc > .4f) Circle(HX(0xffdd44), bx, by - ln - 6, (int)(8 + P.rc * 8));
        Rc(gc[P.w], bx - wd / 2, by - ln, wd, ln);
        Rc(HX(0x222222), bx - 2, by - ln, 4, ln);
        Rc(HX(0xc89070), bx - 13, by - 14, 26, 14);
        Rc(HX(0x44aa44), bx - 13, by - 6, 26, 6);
    }
    RcA(255, 0, 0, fl[0], 0, 0, VW, VH);
    RcA(255, 200, 0, fmaxf2(fl[2], fl[1] * .5f), 0, 0, VW, VH);
    hud();
}

static void hud(void) {
    int i, cx = 170, cy = 181, dd = (st == ST_DEAD);
    u32 lab = HX(0xccbb99), red = HX(0xdd2222), yel = HX(0xeeee33);
    char b[24];
    Rc(HX(0x4a443c), 0, 160, 320, 40); Rc(HX(0x2a2622), 0, 160, 320, 2);
    Rc(HX(0x2a2622), 56, 162, 2, 38); Rc(HX(0x2a2622), 110, 162, 2, 38);
    Rc(HX(0x2a2622), 186, 162, 2, 38); Rc(HX(0x2a2622), 240, 162, 2, 38);
    tx("MUNICION", 4, 171, lab, 1, AL_L, 0);
    txi(P.w == 1 ? P.sh : P.bul, 4, 196, red, 2, AL_L);
    tx("SALUD", 60, 171, lab, 1, AL_L, 0);
    snprintf(b, sizeof b, "%d%%", (int)ceilf(P.hp)); tx(b, 60, 196, red, 2, AL_L, 0);
    tx("ARMAS", 114, 171, lab, 1, AL_L, 0);
    for (i = 0; i < 3; i++) {
        char d[2]; d[0] = (char)('1' + i); d[1] = 0;
        Rc(P.has[i] ? HX(0xeeee33) : HX(0x2a2622), 114 + i * 14, 176, 12, 12);
        tx(d, 120 + i * 14, 186, HX(0x000000), 1, AL_C, 0);
    }
    /* cara */
    Rc(HX(0xc8906a), cx - 13, cy - 15, 26, 30); Rc(HX(0x222222), cx - 13, cy - 15, 26, 5);
    Rc(HX(0xffffff), cx - 8, cy - 6, 5, 4); Rc(HX(0xffffff), cx + 3, cy - 6, 5, 4);
    {
        int ey = dd ? 0 : (int)floorf(sinf(T * 1.3f) + .5f);
        Rc(HX(0x000000), cx - 6 + ey, cy - 6, 2, 4); Rc(HX(0x000000), cx + 5 + ey, cy - 6, 2, 4);
    }
    Rc((P.rage > 0 && P.rc > .2f) ? HX(0x660000) : HX(0x552222), cx - 5, cy + 6, 10, P.face > 0 ? 3 : 2);
    if (P.hurtT > 0) Rc(HX(0xaa0000), cx - 13, cy - 15, 26, 3);
    if (P.hp < 75) Rc(HX(0xaa0000), cx - 13, cy - 4, 4, 8);
    if (P.hp < 50) Rc(HX(0xaa0000), cx + 8, cy, 5, 10);
    if (P.hp < 25) Rc(HX(0xaa0000), cx - 6, cy + 9, 12, 5);
    tx("ARMADURA", 190, 171, lab, 1, AL_L, 0);
    snprintf(b, sizeof b, "%d%%", (int)ceilf(P.ar)); tx(b, 190, 196, red, 2, AL_L, 0);
    Rc(P.k[0] ? HX(0x2288ff) : HX(0x3a3630), 244, 166, 10, 8);
    Rc(P.k[1] ? HX(0xeecc22) : HX(0x3a3630), 244, 177, 10, 8);
    Rc(P.k[2] ? HX(0xee2222) : HX(0x3a3630), 244, 188, 10, 8);
    snprintf(b, sizeof b, "BAL %d", P.bul); tx(b, 260, 175, yel, 1, AL_L, 0);
    snprintf(b, sizeof b, "CAR %d", P.sh); tx(b, 260, 186, yel, 1, AL_L, 0);
    snprintf(b, sizeof b, "NIV %d", lvl + 1); tx(b, 260, 197, yel, 1, AL_L, 0);
    if (msgT > 0) tx(msg, 4, 11, HX(0xeeee44), 1, AL_L, HX(0x000000));
}

/* ------------------------------------------------------------ pantallas */
static const char *BOOT[8] = {"Inicializando memoria... OK", "Cargando datos del nivel... OK", "Generando texturas... OK",
 "Dibujando sprites... OK", "Sintetizando sonidos... OK", "Estado de monstruos... OK", "Estado del HUD... OK", "Todo listo."};

static int menu_count(void) { return menu == M_MAIN ? 4 : menu == M_MODE ? 3 : menu == M_OPT ? 4 : 0; }
static void menu_label(int i, char *o, int n) {
    static const char *sn[4] = {"0.5", "1", "1.5", "2"};
    if (menu == M_MAIN) {
        if (i == 0) snprintf(o, n, "JUGAR");
        else if (i == 1) snprintf(o, n, "DIFICULTAD: %s", DS[diff]);
        else if (i == 2) snprintf(o, n, "OPCIONES");
        else snprintf(o, n, "LEER ESTO");
    } else if (menu == M_MODE) {
        if (i == 0) snprintf(o, n, "CLASICO (27 NIVELES)");
        else if (i == 1) snprintf(o, n, "ULTRA (200 NIVELES)");
        else snprintf(o, n, "VOLVER");
    } else {
        if (i == 0) snprintf(o, n, "GIRO: %s", sn[clampi((int)(opts.sens * 2 + .5f) - 1, 0, 3)]);
        else if (i == 1) snprintf(o, n, "MUSICA: %s", opts.mus ? "SI" : "NO");
        else if (i == 2) snprintf(o, n, "PANTALLA: %s", opts.full ? "COMPLETA" : "ORIGINAL");
        else snprintf(o, n, "VOLVER");
    }
}
static void drawMenuScr(void) {
    int i, n;
    char b[48];
    Rc(HX(0x1a0808), 0, 0, 320, 200);
    for (i = 0; i < 20; i++) Rc(HX(0x2a0c0c), i * 16, 0, 1, 200);
    tx("DOOM", 160, 52, HX(0xcc2222), 5, AL_C, HX(0x000000));
    if (menu == M_HELP) {
        static const char *H[9] = {"STICK / CRUCETA: MOVER Y GIRAR", "L / R: PASO LATERAL", "X o CUADRADO: DISPARAR (MANTENER)",
          "CIRCULO: ABRIR PUERTAS / SALIDA", "TRIANGULO o SELECT: CAMBIAR ARMA", "START: PAUSA", "LLAVES: AZUL, AMARILLA, ROJA", "",
          "PULSA UN BOTON PARA VOLVER"};
        for (i = 0; i < 9; i++) tx(H[i], 160, 80 + i * 14, HX(0xdddddd), 1, AL_C, 0);
        return;
    }
    n = menu_count();
    for (i = 0; i < n; i++) {
        int y = 96 + i * 24;
        menu_label(i, b, sizeof b);
        tx(b, 56, y, i == sel ? HX(0xee3333) : HX(0x8a1c1c), 2, AL_L, HX(0x000000));
        if (i == sel) {
            u32 ec = ((int)(T * 3)) % 2 ? HX(0xff4400) : HX(0x000000);
            Circle(HX(0xdddddd), 36, y - 6, 7);
            Rc(ec, 32, y - 9, 3, 4); Rc(ec, 37, y - 9, 3, 4);
        }
    }
}
static void tile_pos(int k, int *x, int *y, int *w, int *h) {
    if (cm == 'c') { *x = 7 + (k % 9) * 34; *y = 33 + (k / 9) * 46; *w = 32; *h = 30; }
    else { *x = 10 + (k % 10) * 30; *y = 26 + (k / 10) * 36; *w = 28; *h = 32; }
}
static void catScr(void) {
    int cl = cm == 'c', per = cl ? 27 : 40, N = cl ? 27 : 200, k, idx = cl ? 0 : 1;
    Cfg c; char b[120];
    Rc(HX(0x120808), 0, 0, 320, 200);
    tx("O: VOLVER", 4, 12, HX(0x999999), 1, AL_L, 0);
    tx(cl ? "MODO CLASICO" : "MODO ULTRA", 160, 19, HX(0xee3333), 2, AL_C, HX(0x000000));
    if (cl) {
        static const char *E[3] = {"E1 KNEE-DEEP IN THE DEAD", "E2 THE SHORES OF HELL", "E3 INFERNO"};
        int e; for (e = 0; e < 3; e++) tx(E[e], 7, 29 + e * 46, HX(0xaa6666), 1, AL_L, 0);
    }
    for (k = 0; k < per; k++) {
        int i = (cl ? 0 : cpg * 40) + k, x, y, w, h, un;
        if (i >= N) break;
        tile_pos(k, &x, &y, &w, &h);
        un = i < prog[idx];
        Rc(i == csel ? HX(0xffcc33) : HX(0x000000), x - 1, y - 1, w + 2, h + 2);
        Rc(un ? HX(0x7a2020) : HX(0x333333), x, y, w, h);
        if (un) txi(i + 1, x + w / 2, y + h / 2 + 4, HX(0xffffff), 1, AL_C);
        else {
            int lx = x + w / 2, ly = y + h / 2; u32 g = HX(0x888888);
            Rc(g, lx - 4, ly - 1, 9, 7);
            Rc(g, lx - 3, ly - 6, 7, 1); Rc(g, lx - 3, ly - 5, 1, 4); Rc(g, lx + 3, ly - 5, 1, 4);
        }
    }
    cfg_for(cm, csel, &c);
    snprintf(b, sizeof b, "%d. %s", csel + 1, c.name);
    tx(b, 160, cl ? 172 : 176, HX(0xffcc33), 1, AL_C, 0);
    tx(csel < prog[idx] ? c.desc : "Bloqueado: completa el anterior", 160, cl ? 184 : 186, HX(0xbbbbbb), 1, AL_C, 0);
    if (cl) tx("X: JUGAR     O: VOLVER", 160, 196, HX(0x777777), 1, AL_C, 0);
    else {
        snprintf(b, sizeof b, "L <  PAG %d/5  > R   X: JUGAR", cpg + 1);
        tx(b, 160, 197, HX(0xcccccc), 1, AL_C, 0);
    }
}
static void title_scr(void) {
    int y, x;
    for (y = 0; y < SH; y++) {
        float p = y / 200.0f, r, g, b;
        if (p < .6f) { float t = p / .6f; r = 0x33 + (0xaa - 0x33) * t; g = 0x00 + (0x22) * t; b = 0x00 + (0x22) * t; }
        else { float t = (p - .6f) / .4f; r = 0xaa + (0x33 - 0xaa) * t; g = 0x22 + (0x11 - 0x22) * t; b = 0x22 + (0x00 - 0x22) * t; }
        Rc(rgbf(r, g, b), 0, y, 320, 1);
    }
    {
        static const int mp[7][2] = {{0,140},{60,90},{120,130},{200,80},{280,125},{320,100},{320,140}};
        for (x = 0; x < 320; x++) {
            int s; float yy = 140;
            for (s = 0; s < 6; s++)
                if (x >= mp[s][0] && x <= mp[s + 1][0]) {
                    float t = mp[s + 1][0] == mp[s][0] ? 0 : (float)(x - mp[s][0]) / (mp[s + 1][0] - mp[s][0]);
                    yy = mp[s][1] + (mp[s + 1][1] - mp[s][1]) * t; break;
                }
            Rc(HX(0x220000), x, (int)yy, 1, 200 - (int)yy);
        }
    }
    Rc(HX(0xaa3333), 230, 70, 50, 50); Rc(HX(0xffdd00), 240, 86, 8, 6); Rc(HX(0xffdd00), 260, 86, 8, 6);
    Rc(HX(0xeeeeee), 226, 62, 6, 18); Rc(HX(0xeeeeee), 278, 62, 6, 18);
    Rc(HX(0x227711), 60, 120, 36, 56); Rc(HX(0x33aa33), 62, 100, 32, 26); Rc(HX(0x111111), 68, 108, 20, 9);
    Rc(HX(0x555555), 92, 130, 40, 6);
    tx("DOOM", 160, 80, HX(0xdd1111), 7, AL_C, HX(0xffcc00));
    tx("VERSION PSP - ORIGINAL", 160, 98, HX(0xffcc99), 1, AL_C, 0);
    if (((int)(T * 2)) % 2 == 0) tx("PULSA START", 160, 170, HX(0xffffff), 2, AL_C, HX(0x000000));
    tx("2026 - ARTE Y SONIDO PROCEDURALES", 160, 193, HX(0xcccccc), 1, AL_C, 0);
}

static void draw_core(void) {
    int i;
    switch (st) {
    case ST_BOOT: {
        int w;
        Rc(HX(0x000000), 0, 0, 320, 200);
        for (i = 0; i < 8; i++) if (i < T / .3f) tx(BOOT[i], 8, 18 + i * 12, HX(0xbbbbbb), 1, AL_L, 0);
        Rc(HX(0x444444), 8, 150, 304, 10);
        w = (int)(fminf2(1, T / 2.8f) * 302);
        Rc(HX(0xbbbbbb), 9, 151, w, 8);
        break; }
    case ST_TITLE: title_scr(); break;
    case ST_MENU: drawMenuScr(); break;
    case ST_CAT: catScr(); break;
    case ST_PLAY: case ST_DEAD: case ST_PAUSE:
        render();
        if (st == ST_DEAD) {
            RcA(150, 0, 0, .6f, 0, 0, 320, 160);
            tx("HAS MUERTO", 160, 85, HX(0xffffff), 3, AL_C, HX(0x000000));
            tx("PULSA X PARA REINICIAR", 160, 105, HX(0xffffff), 1, AL_C, HX(0x000000));
        } else if (st == ST_PAUSE) {
            RcA(0, 0, 0, .55f, 0, 0, 320, 160);
            tx("PAUSA", 160, 78, HX(0xffffff), 4, AL_C, HX(0x000000));
            tx("START: CONTINUAR", 160, 102, HX(0xffffff), 1, AL_C, HX(0x000000));
            tx("O: SALIR AL MENU", 160, 116, HX(0xffffff), 1, AL_C, HX(0x000000));
        }
        break;
    case ST_STATS: {
        int n = (int)(T * 30);
        char b[40];
        Rc(HX(0x142014), 0, 0, 320, 200);
        snprintf(b, sizeof b, "NIVEL %d COMPLETADO", lvl + 1);
        tx(b, 160, 38, HX(0xee3333), 2, AL_C, HX(0x000000));
        snprintf(b, sizeof b, "MUERTES  %d%%", ST.tk ? imin(ST.kills, n) * 100 / ST.tk : 100);
        tx(b, 60, 82, HX(0xffcc44), 2, AL_L, 0);
        snprintf(b, sizeof b, "OBJETOS  %d%%", ST.ti ? imin(ST.items, n) * 100 / ST.ti : 100);
        tx(b, 60, 106, HX(0xffcc44), 2, AL_L, 0);
        snprintf(b, sizeof b, "TIEMPO   %ds", (int)ST.time);
        tx(b, 60, 130, HX(0xffcc44), 2, AL_L, 0);
        snprintf(b, sizeof b, "PAR      %ds", 60 + ST.tk * 8);
        tx(b, 60, 154, HX(0xffcc44), 2, AL_L, 0);
        if (T > 2) tx("PULSA X PARA CONTINUAR", 160, 186, HX(0xcccccc), 1, AL_C, 0);
        break; }
    }
}
static void draw(float dt) {
    if (mo_on) {
        int i, done = 1, y;
        draw_core();
        memcpy(cpbuf, game_fb, sizeof cpbuf);
        for (i = 0; i < 160; i++) {
            int o;
            mo[i] += 7.0f * 60.0f * dt;
            o = (int)(mo[i] < 0 ? 0 : mo[i]);
            if (o < 200) done = 0;
            if (o >= 200) continue;
            for (y = o; y < 200; y++) {
                int sy = o == 0 ? y : (int)((float)(y - o) * 200.0f / (200 - o));
                if (sy > 199) sy = 199;
                game_fb[y * SW + i * 2] = snapbuf[sy * SW + i * 2];
                game_fb[y * SW + i * 2 + 1] = snapbuf[sy * SW + i * 2 + 1];
            }
        }
        if (done) mo_on = 0;
    } else draw_core();
}

/* ------------------------------------------------------------ control de pantallas */
static void load_level(void) {
    cfg_for(cm, lvl, &CFG);
    TH = &THM[CFG.th]; setFlats(TH);
    P.k[0] = P.k[1] = P.k[2] = 0;
    build(&CFG);
    { char b[120]; snprintf(b, sizeof b, "%s%s", CFG.name, CFG.lock ? " - busca la llave roja" : ""); say(b); }
}
static void snapMelt(void) {
    int i;
    memcpy(snapbuf, game_fb, sizeof snapbuf);
    for (i = 0; i < 160; i++) mo[i] = -(float)(int)(frnd() * 25);
    mo_on = 1;
    S(ST_PLAY);
}
static void startLevel(int i) {
    grnd ^= fcount * 2654435761u; if (!grnd) grnd = 1;
    lvl = i; newP(); if (diff == 0) P.bul = 80; load_level(); snapMelt();
}
static void menu_activate(int i) {
    if (menu == M_MAIN) {
        if (i == 0) { menu = M_MODE; sel = 0; }
        else if (i == 1) { diff = (diff + 1) % 5; save_all(); }
        else if (i == 2) { menu = M_OPT; sel = 0; }
        else menu = M_HELP;
    } else if (menu == M_MODE) {
        if (i == 0) { cm = 'c'; csel = prog[0] - 1; cpg = 0; S(ST_CAT); }
        else if (i == 1) { cm = 'u'; csel = prog[1] - 1; cpg = csel / 40; S(ST_CAT); }
        else { menu = M_MAIN; sel = 0; }
    } else if (menu == M_OPT) {
        if (i == 0) opts.sens = opts.sens >= 2 ? .5f : opts.sens + .5f;
        else if (i == 1) opts.mus ^= 1;
        else if (i == 2) { opts.full ^= 1; game_opt_full = opts.full; }
        else { menu = M_MAIN; sel = 0; save_all(); return; }
        save_all();
    }
}
#define ANYBTN (BTN_CROSS | BTN_CIRCLE | BTN_SQUARE | BTN_TRIANGLE | BTN_START)
static void ui_update(void) {
    switch (st) {
    case ST_BOOT:
        if (T > 3 || (T > .6f && (btnp & ANYBTN))) S(ST_TITLE);
        break;
    case ST_TITLE:
        if (btnp & ANYBTN) { S(ST_MENU); menu = M_MAIN; sfx(SFX_SEL); }
        break;
    case ST_MENU: {
        int n = menu_count();
        if (menu == M_HELP) { if (btnp & ANYBTN) { menu = M_MAIN; sfx(SFX_TIC); } break; }
        if (rptm & BTN_DOWN) { sel = (sel + 1) % n; sfx(SFX_TIC); }
        if (rptm & BTN_UP) { sel = (sel + n - 1) % n; sfx(SFX_TIC); }
        if (btnp & (BTN_CROSS | BTN_START | BTN_RIGHT)) { sfx(SFX_SEL); menu_activate(sel); }
        else if (btnp & BTN_CIRCLE) {
            if (menu != M_MAIN) { menu = M_MAIN; sel = 0; sfx(SFX_TIC); save_all(); }
        }
        break; }
    case ST_CAT: {
        int cl = cm == 'c', N = cl ? 27 : 200, cols = cl ? 9 : 10, idx = cl ? 0 : 1, moved = 0;
        if ((rptm & BTN_LEFT) && csel > 0) { csel--; moved = 1; }
        if ((rptm & BTN_RIGHT) && csel < N - 1) { csel++; moved = 1; }
        if ((rptm & BTN_UP) && csel - cols >= 0) { csel -= cols; moved = 1; }
        if ((rptm & BTN_DOWN) && csel + cols < N) { csel += cols; moved = 1; }
        if (!cl && (btnp & BTN_L) && csel > 0) { csel = imax(0, csel - 40); moved = 1; }
        if (!cl && (btnp & BTN_R) && csel < N - 1) { csel = imin(N - 1, csel + 40); moved = 1; }
        if (moved) { if (!cl) cpg = csel / 40; sfx(SFX_TIC); }
        if (btnp & (BTN_CROSS | BTN_START)) {
            if (csel < prog[idx]) { sfx(SFX_SEL); startLevel(csel); } else sfx(SFX_TIC);
        } else if (btnp & BTN_CIRCLE) { S(ST_MENU); menu = M_MAIN; sfx(SFX_TIC); }
        break; }
    case ST_STATS:
        if (T > 1.5f && (btnp & (BTN_CROSS | BTN_START | BTN_CIRCLE))) {
            int N = cm == 'c' ? 27 : 200;
            csel = imin(lvl + 1, N - 1); cpg = csel / 40; S(ST_CAT);
        }
        break;
    case ST_DEAD:
        if (T > .8f && (btnp & (BTN_CROSS | BTN_START))) {
            grnd ^= fcount * 2654435761u; if (!grnd) grnd = 1;
            newP(); if (diff == 0) P.bul = 80; load_level(); snapMelt();
        }
        break;
    case ST_PAUSE:
        if (btnp & BTN_START) S(ST_PLAY);
        else if (btnp & BTN_CIRCLE) { S(ST_MENU); menu = M_MAIN; }
        break;
    case ST_PLAY:
        if (btnp & BTN_CIRCLE) use();
        if (btnp & (BTN_TRIANGLE | BTN_SELECT)) nextW();
        if (btnp & BTN_START && st == ST_PLAY) S(ST_PAUSE);
        break;
    }
}

/* ------------------------------------------------------------ API publica */
void game_init(void) {
    make_textures(); make_sprites(); setFlats(&THM[0]);
    newP(); load_all(); audio_init();
    st = ST_BOOT; T = 0; menu = M_MAIN; mo_on = 0;
    memset(game_fb, 0, sizeof game_fb);
}
void game_frame(float dt, const Input *in) {
    fcount++;
    if (dt > .05f) dt = .05f;
    if (dt < 0) dt = 0;
    input_begin(in, dt);
    T += dt;
    ui_update();
    upd(dt);
    draw(dt);
    audio_set_music(opts.mus && st != ST_BOOT);
}
#ifdef HOST_TEST
void game_dump_level(int mode, int i, FILE *f, int diffv) {
    Cfg c; int x, y, e;
    diff = diffv; newP(); cfg_for(mode, i, &c); build(&c);
    fprintf(f, "{\"mode\":\"%c\",\"i\":%d,\"W\":%d,\"H\":%d,\"map\":[", mode, i, MW, MH);
    for (y = 0; y < MH; y++) { fprintf(f, "%s\"", y ? "," : ""); for (x = 0; x < MW; x++) fputc(map[y][x], f); fputc('"', f); }
    fprintf(f, "],\"ents\":[");
    for (e = 0; e < nents; e++)
        fprintf(f, "%s[\"%c\",%.1f,%.1f,%.0f]", e ? "," : "", ents[e].t, ents[e].x, ents[e].y, ents[e].hp);
    fprintf(f, "],\"P\":[%.1f,%.1f,%.4f],\"ST\":[%d,%d],\"cfg\":[%d,%d,%d,%d,%u]}\n", P.x, P.y, P.a, ST.tk, ST.ti, c.kind, c.th, c.lock, c.en, c.seed);
}
int game_test_state(void) { return st; }
void game_test_godmode(void) { P.hp = 100000; }
void game_test_teleport_exit(void) {
    int x, y, d;
    static const int D4[4][2] = {{1,0},{0,1},{-1,0},{0,-1}};
    for (y = 0; y < MH; y++) for (x = 0; x < MW; x++) if (map[y][x] == 'X')
        for (d = 0; d < 4; d++) {
            int nx = x + D4[d][0], ny = y + D4[d][1];
            if (map[ny][nx] == '.') {
                P.x = nx + .5f; P.y = ny + .5f;
                P.a = atan2f((float)(y - ny), (float)(x - nx));
                P.k[2] = 1; return;
            }
        }
}
void game_test_set(int mode, int level, int d) { cm = mode; lvl = level; diff = d; }
void game_test_kill(void) { hurt(100000); }
void game_test_start(int mode, int level) { cm = mode; prog[0] = 27; prog[1] = 200; startLevel(level); }
#endif

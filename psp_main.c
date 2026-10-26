/* DOOM PSP - plataforma PSP: pantalla, botones, audio y guardado. */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <psppower.h>
#include <pspge.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "audio.h"

PSP_MODULE_INFO("DOOMPSP", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define LINE 512
#define FB_BYTES (LINE * SCR_H * 4)

static volatile int running = 1;
static char save_path[256] = "ms0:/PSP/SAVEDATA/DOOMPSP/doom.sav";

/* ---------------- guardado ---------------- */
int plat_load_data(void *buf, int n) {
    FILE *f = fopen(save_path, "rb");
    int r;
    if (!f) return 0;
    r = (int)fread(buf, 1, (size_t)n, f);
    fclose(f);
    return r;
}
void plat_save_data(const void *buf, int n) {
    FILE *f = fopen(save_path, "wb");
    if (!f) return;
    fwrite(buf, 1, (size_t)n, f);
    fclose(f);
}

/* ---------------- HOME / salir ---------------- */
static int exit_callback(int a1, int a2, void *arg) {
    (void)a1; (void)a2; (void)arg;
    running = 0;
    sceKernelExitGame();
    return 0;
}
static int callback_thread(SceSize args, void *argp) {
    int cbid;
    (void)args; (void)argp;
    cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, PSP_THREAD_ATTR_USER, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
}

/* ---------------- audio ---------------- */
#define AUDIO_BLOCK 512
static short abuf[2][AUDIO_BLOCK * 2] __attribute__((aligned(64)));
static int audio_thread(SceSize args, void *argp) {
    int ch, cur = 0;
    (void)args; (void)argp;
    ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_BLOCK, PSP_AUDIO_FORMAT_STEREO);
    if (ch < 0) return 0;
    while (running) {
        audio_mix(abuf[cur], AUDIO_BLOCK);
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, abuf[cur]);
        cur ^= 1;
    }
    sceAudioChRelease(ch);
    return 0;
}

/* ---------------- pantalla ---------------- */
static int xmap[SCR_W], ymap[SCR_H];
static int cur_full = -1, dst_x0, dst_w;

static void build_maps(int full) {
    int x, y;
    dst_w = full ? SCR_W : 435;
    dst_x0 = (SCR_W - dst_w) / 2;
    for (x = 0; x < dst_w; x++) xmap[x] = x * SW / dst_w;
    for (y = 0; y < SCR_H; y++) ymap[y] = y * SH / SCR_H;
    cur_full = full;
}
static void clear_fb(unsigned int *vram) {
    int i;
    for (i = 0; i < LINE * SCR_H; i++) vram[i] = 0xFF000000u;
}
static void blit(unsigned int *vram) {
    int x, y;
    for (y = 0; y < SCR_H; y++) {
        const unsigned int *src = &game_fb[ymap[y] * SW];
        unsigned int *dst = vram + y * LINE + dst_x0;
        for (x = 0; x < dst_w; x++) dst[x] = src[xmap[x]];
    }
}

int main(int argc, char **argv) {
    SceCtrlData pad;
    Input in;
    int back = 0, thid;
    unsigned long long last;
    unsigned int *vram[2];

    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);

    /* el progreso va a la tarjeta de memoria (un ISO es de solo lectura) */
    sceIoMkdir("ms0:/PSP/SAVEDATA/DOOMPSP", 0777);
    strcpy(save_path, "ms0:/PSP/SAVEDATA/DOOMPSP/doom.sav");
    (void)argc; (void)argv;

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    sceDisplaySetMode(0, SCR_W, SCR_H);
    vram[0] = (unsigned int *)(0x44000000 + 0);
    vram[1] = (unsigned int *)(0x44000000 + FB_BYTES);
    clear_fb(vram[0]); clear_fb(vram[1]);
    sceDisplaySetFrameBuf((void *)0x04000000, LINE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);

    game_init();

    thid = sceKernelCreateThread("audio", audio_thread, 0x12, 0x10000, PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);

    last = sceKernelGetSystemTimeWide();
    while (running) {
        unsigned long long now = sceKernelGetSystemTimeWide();
        float dt = (float)(now - last) / 1000000.0f;
        unsigned int b;
        last = now;

        sceCtrlPeekBufferPositive(&pad, 1);
        b = 0;
        if (pad.Buttons & PSP_CTRL_UP) b |= BTN_UP;
        if (pad.Buttons & PSP_CTRL_DOWN) b |= BTN_DOWN;
        if (pad.Buttons & PSP_CTRL_LEFT) b |= BTN_LEFT;
        if (pad.Buttons & PSP_CTRL_RIGHT) b |= BTN_RIGHT;
        if (pad.Buttons & PSP_CTRL_CROSS) b |= BTN_CROSS;
        if (pad.Buttons & PSP_CTRL_CIRCLE) b |= BTN_CIRCLE;
        if (pad.Buttons & PSP_CTRL_SQUARE) b |= BTN_SQUARE;
        if (pad.Buttons & PSP_CTRL_TRIANGLE) b |= BTN_TRIANGLE;
        if (pad.Buttons & PSP_CTRL_LTRIGGER) b |= BTN_L;
        if (pad.Buttons & PSP_CTRL_RTRIGGER) b |= BTN_R;
        if (pad.Buttons & PSP_CTRL_START) b |= BTN_START;
        if (pad.Buttons & PSP_CTRL_SELECT) b |= BTN_SELECT;
        in.buttons = b;
        in.ax = (int)pad.Lx - 128;
        in.ay = (int)pad.Ly - 128;

        game_frame(dt, &in);

        if (cur_full != game_opt_full) { build_maps(game_opt_full); clear_fb(vram[0]); clear_fb(vram[1]); }
        blit(vram[back]);
        sceDisplaySetFrameBuf((void *)(0x04000000 + back * FB_BYTES), LINE,
                              PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
        sceDisplayWaitVblankStart();
        back ^= 1;
    }
    sceKernelExitGame();
    return 0;
}

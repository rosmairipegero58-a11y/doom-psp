/* DOOM PSP - nucleo portable del juego (port del HTML original).
 * Este modulo no toca nada especifico de la PSP: la plataforma le pasa
 * los botones y recibe un framebuffer de 320x200 (formato 0xAABBGGRR). */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define SW 320
#define SH 200

/* mascara de botones (la plataforma la rellena) */
enum {
    BTN_UP = 1 << 0, BTN_DOWN = 1 << 1, BTN_LEFT = 1 << 2, BTN_RIGHT = 1 << 3,
    BTN_CROSS = 1 << 4, BTN_CIRCLE = 1 << 5, BTN_SQUARE = 1 << 6, BTN_TRIANGLE = 1 << 7,
    BTN_L = 1 << 8, BTN_R = 1 << 9, BTN_START = 1 << 10, BTN_SELECT = 1 << 11
};

typedef struct {
    uint32_t buttons;  /* mascara BTN_* */
    int ax, ay;        /* stick analogico -127..127 (ay negativo = arriba) */
} Input;

extern uint32_t game_fb[SW * SH]; /* pantalla 320x200, 0xAABBGGRR */
extern int game_opt_full;         /* 1 = estirar a toda la pantalla, 0 = proporcion original */

void game_init(void);
void game_frame(float dt, const Input *in);

/* la plataforma implementa la persistencia (tarjeta de memoria / archivo) */
int plat_load_data(void *buf, int n);        /* devuelve bytes leidos o 0 */
void plat_save_data(const void *buf, int n);

#ifdef HOST_TEST
#include <stdio.h>
void game_dump_level(int mode, int i, FILE *f, int diffv);
int game_test_state(void); void game_test_godmode(void); void game_test_teleport_exit(void); void game_test_set(int mode,int level,int d); void game_test_kill(void); void game_test_start(int mode,int level);
#endif
#endif
